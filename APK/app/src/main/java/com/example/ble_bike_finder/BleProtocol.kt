package com.example.ble_bike_finder

object BleProtocol {
    const val COMPANY_ID = 0xFFFF

    private const val VERSION = 0x01.toByte()
    private const val COMMAND_FIND = 0x01.toByte()
    private const val COMMAND_FIND_OFF = 0x02.toByte()
    private const val COMMAND_OTA_ON = 0xA0.toByte()
    private const val COMMAND_OTA_OFF = 0xA1.toByte()

    private val magic = byteArrayOf('B'.code.toByte(), 'B'.code.toByte(), 'F'.code.toByte())
    private val deviceId = byteArrayOf(0xB1.toByte(), 0x4E.toByte(), 0xF1.toByte(), 0x32.toByte())

    fun findPayload(session: Int): ByteArray = payload(COMMAND_FIND, session)

    fun findOffPayload(session: Int): ByteArray = payload(COMMAND_FIND_OFF, session)

    fun otaOnPayload(): ByteArray = payload(COMMAND_OTA_ON, 0)

    fun otaOffPayload(): ByteArray = payload(COMMAND_OTA_OFF, 0)

    private fun payload(command: Byte, session: Int): ByteArray {
        val body = magic + byteArrayOf(VERSION) + deviceId + byteArrayOf(command, session.toByte())
        return body + byteArrayOf(checksum(body))
    }

    private fun checksum(bytes: ByteArray): Byte =
        bytes.fold(0.toByte()) { acc, value -> (acc.toInt() xor value.toInt()).toByte() }
}
