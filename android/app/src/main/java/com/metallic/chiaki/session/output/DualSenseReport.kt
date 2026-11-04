// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session.output

import java.util.zip.CRC32

/** Everything this app controls on a DualSense, sent in full with every report. */
class DualSenseOutputState
{
	var rumbleLeft: UByte = 0U // low-frequency motor
	var rumbleRight: UByte = 0U // high-frequency motor
	val triggerRight = ByteArray(11) // effect type + 10 data bytes
	val triggerLeft = ByteArray(11)
	var ledR: UByte = 0U
	var ledG: UByte = 0U
	var ledB: UByte = 0U
	var playerLeds = DualSenseReport.playerLedsFor(0) // player 1: the console reports the index only when it changes
	var intensity: UByte = 0U

	fun setTriggers(typeLeft: UByte, left: ByteArray, typeRight: UByte, right: ByteArray)
	{
		triggerLeft[0] = typeLeft.toByte()
		left.copyInto(triggerLeft, 1, 0, 10)
		triggerRight[0] = typeRight.toByte()
		right.copyInto(triggerRight, 1, 0, 10)
	}

	/** Motors off, both triggers set to the "off" effect (as desktop does on controller close). */
	fun setRelease()
	{
		rumbleLeft = 0U
		rumbleRight = 0U
		triggerLeft.fill(0)
		triggerRight.fill(0)
		triggerLeft[0] = TRIGGER_EFFECT_OFF
		triggerRight[0] = TRIGGER_EFFECT_OFF
	}

	private companion object
	{
		const val TRIGGER_EFFECT_OFF: Byte = 0x05
	}
}

/** Bluetooth output report 0x31, layout as in the Linux hid-playstation driver. */
object DualSenseReport
{
	const val LENGTH = 78

	private const val REPORT_ID = 0x31
	private const val TAG = 0x10
	private const val COMMON = 3
	private const val CRC_SEED = 0xA2

	private const val FLAG0_HAPTICS_SELECT = 0x02
	private const val FLAG0_RIGHT_TRIGGER = 0x04
	private const val FLAG0_LEFT_TRIGGER = 0x08
	private const val FLAG1_LIGHTBAR = 0x04
	private const val FLAG1_PLAYER_LEDS = 0x10
	private const val FLAG1_POWER_REDUCTION = 0x40
	private const val FLAG2_VIBRATION_V2 = 0x04

	private val PLAYER_LEDS = intArrayOf(0x04, 0x0A, 0x15, 0x1B, 0x1F)

	fun playerLedsFor(index: Int) = PLAYER_LEDS[if(index < 0) 0 else index % PLAYER_LEDS.size]

	fun build(state: DualSenseOutputState, seq: Int): ByteArray
	{
		val r = ByteArray(LENGTH)
		r[0] = REPORT_ID.toByte()
		r[1] = ((seq and 0x0f) shl 4).toByte()
		r[2] = TAG.toByte()
		r[COMMON + 0] = (FLAG0_HAPTICS_SELECT or FLAG0_RIGHT_TRIGGER or FLAG0_LEFT_TRIGGER).toByte()
		r[COMMON + 1] = (FLAG1_LIGHTBAR or FLAG1_PLAYER_LEDS or FLAG1_POWER_REDUCTION).toByte()
		r[COMMON + 2] = state.rumbleRight.toByte()
		r[COMMON + 3] = state.rumbleLeft.toByte()
		state.triggerRight.copyInto(r, COMMON + 10)
		state.triggerLeft.copyInto(r, COMMON + 21)
		r[COMMON + 36] = state.intensity.toByte()
		r[COMMON + 38] = FLAG2_VIBRATION_V2.toByte()
		r[COMMON + 43] = state.playerLeds.toByte()
		r[COMMON + 44] = state.ledR.toByte()
		r[COMMON + 45] = state.ledG.toByte()
		r[COMMON + 46] = state.ledB.toByte()

		val crc = CRC32()
		crc.update(CRC_SEED)
		crc.update(r, 0, LENGTH - 4)
		val v = crc.value.toInt()
		r[LENGTH - 4] = v.toByte()
		r[LENGTH - 3] = (v ushr 8).toByte()
		r[LENGTH - 2] = (v ushr 16).toByte()
		r[LENGTH - 1] = (v ushr 24).toByte()
		return r
	}
}
