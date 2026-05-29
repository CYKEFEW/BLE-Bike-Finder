package com.example.ble_bike_finder

import android.Manifest
import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.bluetooth.BluetoothManager
import android.bluetooth.le.AdvertiseCallback
import android.bluetooth.le.AdvertiseData
import android.bluetooth.le.AdvertiseSettings
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.service.quicksettings.TileService
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat

data class BroadcastStatus(
    val active: Boolean,
    val title: String,
    val remainingMs: Long?,
    val continuous: Boolean
)

class BikeFinderService : Service() {
    private val handler = Handler(Looper.getMainLooper())
    private var advertising = false
    private var foregroundStarted = false
    private var currentMode = BroadcastMode.find(1)
    private var lastError: String? = null

    private val stopTransientRunnable = Runnable {
        if (!currentMode.continuous) {
            stopAdvertising()
            stopSelf()
        }
    }

    private val advertiseCallback = object : AdvertiseCallback() {
        override fun onStartSuccess(settingsInEffect: AdvertiseSettings?) {
            advertising = true
            lastError = null
            saveBroadcastState(currentMode)
            updateNotification()
            requestTileRefresh()

            if (!currentMode.continuous) {
                handler.removeCallbacks(stopTransientRunnable)
                handler.postDelayed(stopTransientRunnable, TRANSIENT_COMMAND_MS)
            }
        }

        override fun onStartFailure(errorCode: Int) {
            advertising = false
            lastError = "广播启动失败，错误码：$errorCode"
            saveIdleState()
            updateNotification()
            requestTileRefresh()
            stopSelf()
        }
    }

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START_FIND -> startAdvertising(BroadcastMode.find(nextFindSession()))
            ACTION_STOP_FIND -> startAdvertising(BroadcastMode.findOff(currentFindSession()))
            ACTION_TOGGLE_FIND -> {
                if (isFindAdvertisingSaved(this)) {
                    startAdvertising(BroadcastMode.findOff(currentFindSession()))
                } else {
                    startAdvertising(BroadcastMode.find(nextFindSession()))
                }
            }
            ACTION_SEND_OTA_ON -> startAdvertising(BroadcastMode.otaOn())
            ACTION_SEND_OTA_OFF -> startAdvertising(BroadcastMode.otaOff())
            else -> startAdvertising(BroadcastMode.find(nextFindSession()))
        }

        return START_STICKY
    }

    override fun onDestroy() {
        stopAdvertising()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    @SuppressLint("MissingPermission")
    private fun startAdvertising(mode: BroadcastMode) {
        handler.removeCallbacks(stopTransientRunnable)
        stopCurrentAdvertisingOnly()
        currentMode = mode
        ensureForeground()

        if (!hasAdvertisePermission()) {
            lastError = "缺少蓝牙广播权限"
            saveIdleState()
            updateNotification()
            requestTileRefresh()
            stopSelf()
            return
        }

        val bluetoothManager = getSystemService(BluetoothManager::class.java)
        val adapter = bluetoothManager.adapter
        val advertiser = adapter?.bluetoothLeAdvertiser
        if (adapter == null || !adapter.isEnabled || advertiser == null) {
            lastError = "蓝牙未开启或手机不支持 BLE 广播"
            saveIdleState()
            updateNotification()
            requestTileRefresh()
            stopSelf()
            return
        }

        val settings = AdvertiseSettings.Builder()
            .setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_LOW_LATENCY)
            .setTxPowerLevel(AdvertiseSettings.ADVERTISE_TX_POWER_HIGH)
            .setConnectable(false)
            .setTimeout(0)
            .build()

        val data = AdvertiseData.Builder()
            .setIncludeDeviceName(false)
            .addManufacturerData(BleProtocol.COMPANY_ID, mode.payload)
            .build()

        lastError = null
        advertiser.startAdvertising(settings, data, advertiseCallback)
    }

    @SuppressLint("MissingPermission")
    private fun stopCurrentAdvertisingOnly() {
        val advertiser = getSystemService(BluetoothManager::class.java)
            .adapter
            ?.bluetoothLeAdvertiser
        advertiser?.stopAdvertising(advertiseCallback)
        advertising = false
    }

    private fun stopAdvertising() {
        handler.removeCallbacks(stopTransientRunnable)
        stopCurrentAdvertisingOnly()
        saveIdleState()
        requestTileRefresh()
    }

    private fun ensureForeground() {
        if (foregroundStarted) {
            return
        }

        val type = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
        } else {
            0
        }

        ServiceCompat.startForeground(this, NOTIFICATION_ID, buildNotification(), type)
        foregroundStarted = true
    }

    private fun updateNotification() {
        if (!foregroundStarted) {
            return
        }

        val manager = getSystemService(NotificationManager::class.java)
        manager.notify(NOTIFICATION_ID, buildNotification())
    }

    private fun buildNotification(): Notification {
        val openAppIntent = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        val statusText = lastError ?: currentMode.notificationText

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_find_bike)
            .setContentTitle("蓝牙找车器")
            .setContentText(statusText)
            .setContentIntent(openAppIntent)
            .setOngoing(currentMode.continuous)
            .setShowWhen(false)
            .setOnlyAlertOnce(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()
    }

    private fun hasAdvertisePermission(): Boolean {
        return Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
            ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_ADVERTISE) ==
            PackageManager.PERMISSION_GRANTED
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
            return
        }

        val channel = NotificationChannel(
            CHANNEL_ID,
            "找车广播",
            NotificationManager.IMPORTANCE_LOW
        ).apply {
            description = "找车和烧录模式切换广播"
            setShowBadge(false)
        }

        getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
    }

    private fun saveBroadcastState(mode: BroadcastMode) {
        val endAt = if (mode.continuous) 0L else System.currentTimeMillis() + TRANSIENT_COMMAND_MS
        getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
            .edit()
            .putBoolean(KEY_ACTIVE, true)
            .putString(KEY_MODE, mode.key)
            .putString(KEY_TITLE, mode.statusTitle)
            .putBoolean(KEY_CONTINUOUS, mode.continuous)
            .putLong(KEY_END_AT, endAt)
            .apply()
    }

    private fun saveIdleState() {
        getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
            .edit()
            .putBoolean(KEY_ACTIVE, false)
            .putString(KEY_MODE, MODE_IDLE)
            .putString(KEY_TITLE, "广播已关闭")
            .putBoolean(KEY_CONTINUOUS, false)
            .putLong(KEY_END_AT, 0L)
            .apply()
    }

    private fun requestTileRefresh() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            TileService.requestListeningState(
                this,
                android.content.ComponentName(this, BikeFinderTileService::class.java)
            )
        }
    }

    private data class BroadcastMode(
        val key: String,
        val payload: ByteArray,
        val continuous: Boolean,
        val statusTitle: String,
        val notificationText: String
    ) {
        companion object {
            fun find(session: Int) =
                BroadcastMode("find", BleProtocol.findPayload(session), true, "找车广播中", "找车广播正在发送")

            fun findOff(session: Int) =
                BroadcastMode("find_off", BleProtocol.findOffPayload(session), false, "正在关闭找车广播", "正在发送关闭找车命令")

            fun otaOn() =
                BroadcastMode("ota_on", BleProtocol.otaOnPayload(), false, "正在进入烧录模式", "正在发送进入烧录模式命令")

            fun otaOff() =
                BroadcastMode("ota_off", BleProtocol.otaOffPayload(), false, "正在取消烧录模式", "正在发送取消烧录模式命令")
        }
    }

    companion object {
        const val ACTION_START_FIND = "com.example.ble_bike_finder.action.START_FIND"
        const val ACTION_STOP_FIND = "com.example.ble_bike_finder.action.STOP_FIND"
        const val ACTION_TOGGLE_FIND = "com.example.ble_bike_finder.action.TOGGLE_FIND"
        const val ACTION_SEND_OTA_ON = "com.example.ble_bike_finder.action.SEND_OTA_ON"
        const val ACTION_SEND_OTA_OFF = "com.example.ble_bike_finder.action.SEND_OTA_OFF"

        private const val CHANNEL_ID = "bike_finder_advertising"
        private const val NOTIFICATION_ID = 233
        private const val PREFS_NAME = "bike_finder_state"
        private const val KEY_ACTIVE = "active"
        private const val KEY_MODE = "mode"
        private const val KEY_TITLE = "title"
        private const val KEY_CONTINUOUS = "continuous"
        private const val KEY_END_AT = "end_at"
        private const val KEY_FIND_SESSION = "find_session"
        private const val MODE_IDLE = "idle"
        private const val MODE_FIND = "find"
        private const val TRANSIENT_COMMAND_MS = 3000L

        fun isFindAdvertisingSaved(context: Context): Boolean {
            val prefs = context.getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
            return prefs.getBoolean(KEY_ACTIVE, false) &&
                prefs.getString(KEY_MODE, MODE_IDLE) == MODE_FIND
        }

        fun readBroadcastStatus(context: Context): BroadcastStatus {
            val prefs = context.getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
            val active = prefs.getBoolean(KEY_ACTIVE, false)
            val title = prefs.getString(KEY_TITLE, "广播已关闭") ?: "广播已关闭"
            val continuous = prefs.getBoolean(KEY_CONTINUOUS, false)
            val endAt = prefs.getLong(KEY_END_AT, 0L)
            val remaining = if (active && !continuous && endAt > 0L) {
                (endAt - System.currentTimeMillis()).coerceAtLeast(0L)
            } else {
                null
            }

            return if (active && !continuous && remaining == 0L) {
                BroadcastStatus(false, "广播已关闭", null, false)
            } else {
                BroadcastStatus(active, title, remaining, continuous)
            }
        }
    }

    private fun currentFindSession(): Int =
        getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
            .getInt(KEY_FIND_SESSION, 1)
            .coerceIn(1, 255)

    private fun nextFindSession(): Int {
        val prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
        val current = prefs.getInt(KEY_FIND_SESSION, 0)
        val next = if (current >= 255) 1 else current + 1
        prefs.edit().putInt(KEY_FIND_SESSION, next).apply()
        return next
    }
}
