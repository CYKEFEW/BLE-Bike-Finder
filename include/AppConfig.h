#pragma once

#include <Arduino.h>

// =========================
// 重要可配置参数集中区
// =========================
// 调试或换车时优先修改本文件；其它 cpp 文件只引用这些常量，避免协议和阈值分散在各处。

// 有源蜂鸣器控制脚：高电平响，低电平关闭。
constexpr uint8_t BUZZER_PIN = 17;

// 烧录模式指示灯控制脚：默认高电平点亮，进入 OTA/上传中点亮，恢复找车或重启前关闭。
constexpr uint8_t OTA_MODE_LED_PIN = 16;

// 找车广播超时时间：超过此时间没有再收到 FIND 广播，认为手机信号丢失。
constexpr uint32_t FIND_TIMEOUT_MS = 1500;

// 蜂鸣器每次短响的高电平持续时间。
constexpr uint32_t BUZZER_PULSE_ON_MS = 60;

// 信号丢失后的提示音：快速响 3 下，每个开/关阶段 100ms，共 6 个阶段。
constexpr uint32_t BUZZER_LOST_ALERT_STEP_MS = 100;
constexpr uint8_t BUZZER_LOST_ALERT_PHASES = 6;

// RSSI 蜂鸣映射说明：
// RSSI 数值越接近 0 表示越近；数值越小表示越远。
// 当前逻辑不是分段表，而是在“近距离边界”和“极限距离边界”之间做线性平滑过渡：
// 越近周期越长、响得越慢；越远周期越短、响得越快；低于极限距离边界时持续长响。
constexpr int RSSI_SLOW_EDGE_DBM = -50;       // 大于等于该值：距离较近，使用最慢短响周期。
constexpr int RSSI_CONTINUOUS_EDGE_DBM = -80; // 小于该值：达到极限距离，蜂鸣器持续长响。

// 平滑短响周期端点；只在 RSSI_SLOW_EDGE_DBM 到 RSSI_CONTINUOUS_EDGE_DBM 之间插值使用。
constexpr uint32_t BUZZER_SLOW_EDGE_CYCLE_MS = 1000; // 近距离端点：慢响。
constexpr uint32_t BUZZER_FAST_EDGE_CYCLE_MS = 180;  // 远距离端点：快响。

// OTA 烧录热点参数：收到 OTA_ON 蓝牙广播后开启该热点和 Web 烧录页面。
constexpr char AP_SSID[] = "BLE_Bike_Finder";
constexpr char AP_PASSWORD[] = "233233233";

// =========================
// 蓝牙广播协议
// =========================
// 完整 manufacturer data 格式：
// Company ID(2字节，小端，0xFFFF)
// + Magic(3字节，"BBF")
// + Version(1字节，0x01)
// + DeviceId(4字节)
// + Command(1字节)
//
// Android BLE 广播 API 添加 manufacturer data 时，Company ID 单独传入；
// ESP32 扫描到的 manufacturer data 中会包含 Company ID。
constexpr uint16_t BLE_COMPANY_ID = 0xFFFF;
constexpr uint8_t BLE_MAGIC[] = {'B', 'B', 'F'};
constexpr uint8_t BLE_PROTOCOL_VERSION = 0x01;

// 设备 ID：手机 App 和 ESP32 固件必须保持一致，否则广播会被忽略。
// 量产时建议为每台找车器分配不同 DeviceId，降低互相误触发概率。
constexpr uint8_t BLE_DEVICE_ID[] = {0xB1, 0x4E, 0xF1, 0x32};

// 命令字：同一套广播协议通过最后 1 字节区分不同动作。
constexpr uint8_t BLE_COMMAND_FIND = 0x01;     // 开启/维持找车蜂鸣。
constexpr uint8_t BLE_COMMAND_FIND_OFF = 0x02; // 关闭找车蜂鸣。
constexpr uint8_t BLE_COMMAND_OTA_ON = 0xA0;   // 进入烧录模式，开启热点和 WebServer。
constexpr uint8_t BLE_COMMAND_OTA_OFF = 0xA1;  // 取消烧录模式，关闭热点并恢复找车。

// 找车会话号：用于区分“旧 FIND 残留包”和“新一轮找车”。
// App 每次开启找车都会递增 session；关闭找车时发送同一个 session 的 FIND_OFF。
constexpr size_t BLE_SESSION_SIZE = 1;

// ESP32 收到的 manufacturer data 需要包含 Company ID，因此总长度包含前 2 字节。
constexpr size_t BLE_PAYLOAD_SIZE = 2 + sizeof(BLE_MAGIC) + 1 + sizeof(BLE_DEVICE_ID) + 1 + BLE_SESSION_SIZE;

// 命令抗干扰参数：
// 1. 新版广播最后增加 1 字节校验和，ESP32 只接受校验通过的数据。
// 2. OTA_ON/OTA_OFF 必须在确认窗口内重复收到指定次数才执行。
// 3. 收到模式命令期间暂时忽略 FIND，防止持续找车广播和瞬时模式命令互相干扰。
constexpr size_t BLE_CHECKSUM_SIZE = 1;
constexpr size_t BLE_PAYLOAD_WITH_CHECKSUM_SIZE = BLE_PAYLOAD_SIZE + BLE_CHECKSUM_SIZE;
constexpr uint8_t BLE_MODE_COMMAND_CONFIRM_COUNT = 2;
constexpr uint32_t BLE_MODE_COMMAND_CONFIRM_WINDOW_MS = 1200;
constexpr uint32_t BLE_COMMAND_GUARD_MS = 2500;

// BLE 扫描刷新周期：定期重启扫描并清空已见设备缓存，避免手机停止/重开广播后被旧扫描结果影响。
constexpr uint32_t BLE_SCAN_REFRESH_MS = 2000;
