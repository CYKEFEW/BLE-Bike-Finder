package com.example.ble_bike_finder

import android.Manifest
import android.app.PendingIntent
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.service.quicksettings.Tile
import android.service.quicksettings.TileService
import androidx.core.content.ContextCompat

class BikeFinderTileService : TileService() {
    override fun onStartListening() {
        super.onStartListening()
        updateTile()
    }

    override fun onClick() {
        super.onClick()

        if (!hasAdvertisePermission()) {
            openAppFromTile()
            return
        }

        val isAdvertising = BikeFinderService.isFindAdvertisingSaved(this)
        val action = if (isAdvertising) {
            BikeFinderService.ACTION_STOP_FIND
        } else {
            BikeFinderService.ACTION_START_FIND
        }

        val intent = Intent(this, BikeFinderService::class.java).setAction(action)
        if (isAdvertising) {
            startService(intent)
        } else {
            ContextCompat.startForegroundService(this, intent)
        }

        qsTile?.state = if (isAdvertising) Tile.STATE_INACTIVE else Tile.STATE_ACTIVE
        qsTile?.subtitle = if (isAdvertising) "已关闭" else "广播中"
        qsTile?.updateTile()
    }

    private fun updateTile() {
        val isAdvertising = BikeFinderService.isFindAdvertisingSaved(this)
        qsTile?.label = "找车"
        qsTile?.subtitle = if (isAdvertising) "广播中" else "已关闭"
        qsTile?.state = if (isAdvertising) Tile.STATE_ACTIVE else Tile.STATE_INACTIVE
        qsTile?.updateTile()
    }

    private fun openAppFromTile() {
        val intent = Intent(this, MainActivity::class.java).apply {
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            val pendingIntent = PendingIntent.getActivity(
                this,
                0,
                intent,
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
            )
            startActivityAndCollapse(pendingIntent)
        } else {
            @Suppress("DEPRECATION")
            startActivityAndCollapse(intent)
        }
    }

    private fun hasAdvertisePermission(): Boolean {
        return Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
            ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_ADVERTISE) ==
            PackageManager.PERMISSION_GRANTED
    }
}
