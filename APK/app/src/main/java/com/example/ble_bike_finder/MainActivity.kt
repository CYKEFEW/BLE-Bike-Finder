package com.example.ble_bike_finder

import android.Manifest
import android.app.StatusBarManager
import android.content.ComponentName
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.drawable.Icon
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.example.ble_bike_finder.ui.theme.BLE_Bike_FinderTheme

class MainActivity : ComponentActivity() {
    private val handler = Handler(Looper.getMainLooper())
    private val statusText = mutableStateOf("请授予权限，并把“找车”添加到快捷设置面板。")
    private val broadcastStatus = mutableStateOf(BroadcastStatus(false, "广播已关闭", null, false))

    private val statusTicker = object : Runnable {
        override fun run() {
            refreshBroadcastStatus()
            handler.postDelayed(this, 500)
        }
    }

    private val permissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
            updateStatusAfterPermission()
            requestQuickSettingsTile()
            refreshBroadcastStatus()
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            BLE_Bike_FinderTheme {
                Scaffold(
                    modifier = Modifier.fillMaxSize(),
                    containerColor = TechYellow.Background
                ) { innerPadding ->
                    FinderApp(
                        status = statusText.value,
                        broadcastStatus = broadcastStatus.value,
                        modifier = Modifier.padding(innerPadding),
                        onGrantPermissions = { ensurePermissions() },
                        onAddTile = { requestQuickSettingsTile() },
                        onToggleFind = { toggleFindBroadcast() },
                        onOtaOn = { sendCommand(BikeFinderService.ACTION_SEND_OTA_ON, "已发送进入烧录模式命令") },
                        onOtaOff = { sendCommand(BikeFinderService.ACTION_SEND_OTA_OFF, "已发送取消烧录模式命令") }
                    )
                }
            }
        }
        ensurePermissions()
    }

    override fun onResume() {
        super.onResume()
        handler.removeCallbacks(statusTicker)
        handler.post(statusTicker)
    }

    override fun onPause() {
        handler.removeCallbacks(statusTicker)
        super.onPause()
    }

    private fun ensurePermissions() {
        val missingPermissions = requiredRuntimePermissions().filter { permission ->
            ContextCompat.checkSelfPermission(this, permission) != PackageManager.PERMISSION_GRANTED
        }

        if (missingPermissions.isNotEmpty()) {
            statusText.value = "请允许蓝牙广播和通知权限。"
            permissionLauncher.launch(missingPermissions.toTypedArray())
            return
        }

        updateStatusAfterPermission()
        requestQuickSettingsTile()
    }

    private fun toggleFindBroadcast() {
        val action = if (BikeFinderService.isFindAdvertisingSaved(this)) {
            BikeFinderService.ACTION_STOP_FIND
        } else {
            BikeFinderService.ACTION_START_FIND
        }
        val message = if (action == BikeFinderService.ACTION_START_FIND) {
            "已开启找车广播"
        } else {
            "已发送关闭找车命令"
        }
        sendCommand(action, message)
    }

    private fun sendCommand(action: String, successText: String) {
        if (!allRuntimePermissionsGranted()) {
            ensurePermissions()
            return
        }

        ContextCompat.startForegroundService(
            this,
            Intent(this, BikeFinderService::class.java).setAction(action)
        )
        statusText.value = successText
        handler.postDelayed({ refreshBroadcastStatus() }, 200)
    }

    private fun updateStatusAfterPermission() {
        statusText.value = if (allRuntimePermissionsGranted()) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                "权限已就绪。可在 App 内控制广播，也可添加快捷设置按钮。"
            } else {
                "权限已就绪。旧版系统需手动编辑快捷设置添加“找车”。"
            }
        } else {
            "缺少权限，无法发送 BLE 广播。"
        }
    }

    private fun requestQuickSettingsTile() {
        if (!allRuntimePermissionsGranted()) {
            return
        }

        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
            statusText.value = "请下拉快捷设置面板，点击编辑，手动添加“找车”按钮。"
            return
        }

        val statusBarManager = getSystemService(StatusBarManager::class.java)
        statusBarManager.requestAddTileService(
            ComponentName(this, BikeFinderTileService::class.java),
            "找车",
            Icon.createWithResource(this, R.drawable.ic_find_bike),
            mainExecutor
        ) { result ->
            statusText.value = when (result) {
                StatusBarManager.TILE_ADD_REQUEST_RESULT_TILE_ADDED -> "“找车”快捷设置按钮已添加。"
                StatusBarManager.TILE_ADD_REQUEST_RESULT_TILE_ALREADY_ADDED -> "“找车”快捷设置按钮已存在。"
                else -> "未添加快捷设置按钮，可稍后再次点击添加。"
            }
        }
    }

    private fun refreshBroadcastStatus() {
        broadcastStatus.value = BikeFinderService.readBroadcastStatus(this)
    }

    private fun allRuntimePermissionsGranted(): Boolean =
        requiredRuntimePermissions().all { permission ->
            ContextCompat.checkSelfPermission(this, permission) == PackageManager.PERMISSION_GRANTED
        }

    private fun requiredRuntimePermissions(): List<String> {
        val permissions = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            permissions += Manifest.permission.BLUETOOTH_ADVERTISE
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            permissions += Manifest.permission.POST_NOTIFICATIONS
        }
        return permissions
    }
}

@Composable
fun FinderApp(
    status: String,
    broadcastStatus: BroadcastStatus,
    modifier: Modifier = Modifier,
    onGrantPermissions: () -> Unit = {},
    onAddTile: () -> Unit = {},
    onToggleFind: () -> Unit = {},
    onOtaOn: () -> Unit = {},
    onOtaOff: () -> Unit = {}
) {
    Column(
        modifier = modifier
            .fillMaxSize()
            .background(TechYellow.Background)
            .padding(horizontal = 22.dp, vertical = 28.dp),
        verticalArrangement = Arrangement.Center
    ) {
        Text(
            text = "蓝牙找车器",
            color = TechYellow.TextPrimary,
            style = MaterialTheme.typography.headlineLarge,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(8.dp))
        Text(
            text = "找车广播 / 烧录模式切换",
            color = TechYellow.TextSecondary,
            style = MaterialTheme.typography.titleMedium
        )
        Spacer(modifier = Modifier.height(20.dp))

        StatusPanel(status = status, broadcastStatus = broadcastStatus)

        Spacer(modifier = Modifier.height(14.dp))

        TechButton(
            text = if (broadcastStatus.active && broadcastStatus.continuous) "关闭找车广播" else "开启找车广播",
            modifier = Modifier.fillMaxWidth(),
            onClick = onToggleFind
        )

        Spacer(modifier = Modifier.height(12.dp))

        Row(modifier = Modifier.fillMaxWidth()) {
            TechButton(
                text = "授权",
                modifier = Modifier.weight(1f),
                onClick = onGrantPermissions
            )
            Spacer(modifier = Modifier.width(12.dp))
            TechButton(
                text = "添加快捷设置",
                modifier = Modifier.weight(1f),
                onClick = onAddTile
            )
        }

        Spacer(modifier = Modifier.height(14.dp))

        Surface(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(8.dp),
            color = TechYellow.PanelAlt,
            border = BorderStroke(1.dp, TechYellow.Border)
        ) {
            Column(modifier = Modifier.padding(18.dp)) {
                Text(
                    text = "烧录模式",
                    color = TechYellow.TextPrimary,
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold
                )
                Spacer(modifier = Modifier.height(8.dp))
                Text(
                    text = "命令会瞬时广播约 3 秒，ESP32 收到后切换热点烧录或恢复找车。",
                    color = TechYellow.TextSecondary,
                    style = MaterialTheme.typography.bodyMedium
                )
                Spacer(modifier = Modifier.height(16.dp))
                TechButton(
                    text = "进入烧录模式",
                    modifier = Modifier.fillMaxWidth(),
                    onClick = onOtaOn
                )
                Spacer(modifier = Modifier.height(10.dp))
                OutlinedButton(
                    onClick = onOtaOff,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(48.dp),
                    shape = RoundedCornerShape(8.dp),
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = TechYellow.TextPrimary),
                    border = BorderStroke(1.dp, TechYellow.Accent)
                ) {
                    Text("取消烧录模式", textAlign = TextAlign.Center)
                }
            }
        }
    }
}

@Composable
private fun StatusPanel(status: String, broadcastStatus: BroadcastStatus) {
    val countdownText = broadcastStatus.remainingMs?.let {
        "剩余 ${((it + 999) / 1000).coerceAtLeast(0)} 秒"
    }

    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(8.dp),
        color = TechYellow.Panel,
        border = BorderStroke(1.dp, TechYellow.Border)
    ) {
        Column(modifier = Modifier.padding(18.dp)) {
            Text(
                text = "广播状态",
                color = TechYellow.TextPrimary,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold
            )
            Spacer(modifier = Modifier.height(8.dp))
            Text(
                text = broadcastStatus.title,
                color = if (broadcastStatus.active) TechYellow.Active else TechYellow.TextSecondary,
                style = MaterialTheme.typography.bodyLarge,
                fontWeight = FontWeight.SemiBold
            )
            if (countdownText != null) {
                Spacer(modifier = Modifier.height(4.dp))
                Text(
                    text = countdownText,
                    color = TechYellow.TextSecondary,
                    style = MaterialTheme.typography.bodyMedium
                )
            }
            Spacer(modifier = Modifier.height(12.dp))
            Text(
                text = status,
                color = TechYellow.TextSecondary,
                style = MaterialTheme.typography.bodyMedium
            )
        }
    }
}

@Composable
private fun TechButton(
    text: String,
    modifier: Modifier = Modifier,
    onClick: () -> Unit
) {
    Button(
        onClick = onClick,
        modifier = modifier.height(48.dp),
        shape = RoundedCornerShape(8.dp),
        colors = ButtonDefaults.buttonColors(
            containerColor = TechYellow.Accent,
            contentColor = TechYellow.TextPrimary
        )
    ) {
        Text(text, textAlign = TextAlign.Center, fontWeight = FontWeight.SemiBold)
    }
}

private object TechYellow {
    val Background = Color(0xFFFFF4C7)
    val Panel = Color(0xFFFFFBEC)
    val PanelAlt = Color(0xFFFFF0AE)
    val Accent = Color(0xFFFFC83D)
    val Border = Color(0xFFD8A51D)
    val TextPrimary = Color(0xFF211A08)
    val TextSecondary = Color(0xFF6E5A24)
    val Active = Color(0xFF1F7A34)
}

@Preview(showBackground = true)
@Composable
fun FinderAppPreview() {
    BLE_Bike_FinderTheme {
        FinderApp(
            status = "权限已就绪。可在 App 内控制广播，也可添加快捷设置按钮。",
            broadcastStatus = BroadcastStatus(true, "找车广播中", null, true)
        )
    }
}
