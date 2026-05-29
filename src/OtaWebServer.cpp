#include "OtaWebServer.h"

#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "AppConfig.h"
#include "AppState.h"
#include "BleScanner.h"
#include "BuzzerController.h"

namespace {
WebServer server(80);
bool routesConfigured = false;
bool otaUploadHadError = false;
String otaUploadError;

String htmlPage(const String &title, const String &body) {
  return String(F("<!doctype html><html lang=\"zh-CN\"><head>"
                  "<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                  "<title>")) +
         title +
         F("</title><style>"
           "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;margin:0;padding:28px;background:#f6f8fb;color:#172033;}"
           "main{max-width:620px;margin:0 auto;background:#fff;border:1px solid #d8dee9;border-radius:8px;padding:24px;box-shadow:0 8px 24px rgba(20,32,54,.08);}"
           "h1{font-size:22px;margin:0 0 18px;}p{line-height:1.6;color:#3d485c;}"
           "input,button{font-size:16px;}button{margin-top:16px;padding:10px 16px;border:0;border-radius:6px;background:#2563eb;color:#fff;}"
           "button:disabled{background:#9aa6b2;}.bar{height:16px;background:#e5e7eb;border-radius:999px;overflow:hidden;margin-top:18px;}"
           ".fill{height:100%;width:0;background:#22c55e;transition:width .15s ease;}.status{margin-top:12px;font-weight:600;color:#172033;}"
           "</style></head><body><main><h1>") +
         title + F("</h1>") + body + F("</main></body></html>");
}

void handleRoot() {
  const String pageBody = F(
      "<form id=\"uploadForm\">"
      "<p>请选择 PlatformIO 生成的 firmware.bin 固件文件。上传过程中请保持手机连接热点，不要关闭页面。</p>"
      "<input id=\"firmware\" type=\"file\" name=\"firmware\" accept=\".bin\" required>"
      "<br><button id=\"submitButton\" type=\"submit\">上传并烧录</button>"
      "</form>"
      "<div class=\"bar\"><div id=\"progressFill\" class=\"fill\"></div></div>"
      "<div id=\"progressText\" class=\"status\">等待选择固件</div>"
      "<script>"
      "const form=document.getElementById('uploadForm');"
      "const fileInput=document.getElementById('firmware');"
      "const button=document.getElementById('submitButton');"
      "const fill=document.getElementById('progressFill');"
      "const text=document.getElementById('progressText');"
      "form.addEventListener('submit',function(e){"
      "e.preventDefault();"
      "if(!fileInput.files.length){text.textContent='请选择固件文件';return;}"
      "const data=new FormData();"
      "data.append('firmware',fileInput.files[0]);"
      "const xhr=new XMLHttpRequest();"
      "button.disabled=true;"
      "text.textContent='正在上传 0%';"
      "fill.style.width='0%';"
      "xhr.upload.onprogress=function(evt){"
      "if(evt.lengthComputable){"
      "const pct=Math.round((evt.loaded/evt.total)*100);"
      "fill.style.width=pct+'%';"
      "text.textContent='正在上传 '+pct+'%';"
      "}"
      "};"
      "xhr.onload=function(){"
      "fill.style.width='100%';"
      "text.textContent=xhr.status>=200&&xhr.status<300?xhr.responseText:('烧录失败：'+xhr.responseText);"
      "};"
      "xhr.onerror=function(){text.textContent='连接中断，设备将自动重启恢复';};"
      "xhr.open('POST','/update');"
      "xhr.send(data);"
      "});"
      "</script>");

  server.send(200, F("text/html; charset=utf-8"), htmlPage(F("BLE Bike Finder 固件烧录"), pageBody));
}

void handleNotFound() {
  server.send(404, F("text/plain; charset=utf-8"), F("未找到页面"));
}

void handleUpdateFinished() {
  server.sendHeader(F("Connection"), F("close"));

  if (otaUploadHadError || Update.hasError()) {
    if (otaUploadError.length() == 0) {
      otaUploadError = Update.errorString();
    }
    server.send(500, F("text/plain; charset=utf-8"),
                String(F("固件上传或写入失败，设备将自动重启恢复。错误：")) + otaUploadError);
    scheduleRestart();
    return;
  }

  server.send(200, F("text/plain; charset=utf-8"), F("固件写入完成，设备将自动重启。"));
  scheduleRestart();
}

void failUpload(const String &message) {
  otaUploadHadError = true;
  otaUploadError = message;
  Update.abort();
}

void handleUpdateUpload() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    currentMode = OTA_UPDATING;
    buzzerOff();
    stopBleScan();
    otaUploadHadError = false;
    otaUploadError = "";

    String filename = upload.filename;
    filename.toLowerCase();
    if (!filename.endsWith(F(".bin"))) {
      failUpload(F("文件类型错误，请上传 .bin 固件。"));
      return;
    }

    const size_t updateSize = upload.totalSize > 0 ? upload.totalSize : UPDATE_SIZE_UNKNOWN;
    if (!Update.begin(updateSize, U_FLASH)) {
      failUpload(Update.errorString());
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (otaUploadHadError) {
      return;
    }

    const size_t written = Update.write(upload.buf, upload.currentSize);
    if (written != upload.currentSize) {
      failUpload(Update.errorString());
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (otaUploadHadError) {
      return;
    }

    if (!Update.end(true)) {
      failUpload(Update.errorString());
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    failUpload(F("连接中断，上传未完成。"));
  }
}

void configureRoutes() {
  if (routesConfigured) {
    return;
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/update", HTTP_POST, handleUpdateFinished, handleUpdateUpload);
  server.onNotFound(handleNotFound);
  routesConfigured = true;
}
} // namespace

void otaWebBegin() {
  configureRoutes();
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println(F("热点启动失败，设备将重启恢复"));
    scheduleRestart();
    return;
  }

  server.begin();
  Serial.print(F("已进入烧录模式，热点 IP："));
  Serial.println(WiFi.softAPIP());
}

void otaWebStop() {
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}

void otaWebHandleClient() {
  server.handleClient();
}
