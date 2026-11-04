// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session

import com.metallic.chiaki.common.Preferences.HapticsRumbleLevel

/**
 * Turns PS5 haptics strength (one value per haptics packet, see the JNI
 * haptics sink) into rumble, like the desktop client does for controllers
 * without a haptics audio device. tick() must be called every TICK_MS.
 */
class HapticsRumble(
	private val level: HapticsRumbleLevel,
	private val emit: (left: UByte, right: UByte) -> Unit)
{
	companion object
	{
		const val TICK_MS = 30L
		private const val PACKETS_PER_TICK = 3
		private const val MIN_STRENGTH = 1 shl 9
		private const val MAX_STRENGTH = 0xffff
	}

	private val queue = ArrayDeque<Int>()
	private var on = false

	@Synchronized
	fun push(left: Int, right: Int)
	{
		val (l, r) = when(level)
		{
			HapticsRumbleLevel.OFF -> return
			HapticsRumbleLevel.VERY_WEAK -> left / 5 to right / 5
			HapticsRumbleLevel.WEAK -> left / 2 to right / 2
			HapticsRumbleLevel.NORMAL -> left to right
			HapticsRumbleLevel.STRONG -> left * 2 to right * 2
			HapticsRumbleLevel.VERY_STRONG -> left * 5 to right * 5
		}
		queue.addLast(maxOf(clamp(l), clamp(r)))
	}

	@Synchronized
	fun tick()
	{
		var sum = 0
		repeat(PACKETS_PER_TICK) { queue.removeFirstOrNull()?.let { sum += it } }
		val strength = sum / PACKETS_PER_TICK
		if(strength > 0 || on)
		{
			val value = (strength shr 8).toUByte()
			emit(value, value)
		}
		on = strength > 0
	}

	@Synchronized
	fun clear()
	{
		queue.clear()
		if(on)
			emit(0U, 0U)
		on = false
	}

	private fun clamp(value: Int) = when
	{
		value <= 0 -> 0
		value < MIN_STRENGTH -> MIN_STRENGTH
		else -> minOf(value, MAX_STRENGTH)
	}
}
