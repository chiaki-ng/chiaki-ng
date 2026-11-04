// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session

import com.metallic.chiaki.common.Preferences.HapticsRumbleLevel
import org.junit.Assert.assertEquals
import org.junit.Test

class HapticsRumbleTest
{
	private val emitted = mutableListOf<Pair<Int, Int>>()
	private fun rumble(level: HapticsRumbleLevel) =
		HapticsRumble(level) { l, r -> emitted.add(l.toInt() to r.toInt()) }

	@Test
	fun averagesThreePacketsPerTick()
	{
		val h = rumble(HapticsRumbleLevel.NORMAL)
		repeat(3) { h.push(3000, 1000) }
		h.tick()
		assertEquals(listOf(11 to 11), emitted) // max(3000,1000)=3000, avg 3000, >>8 = 11
	}

	@Test
	fun dividesByThreeEvenWithFewerPackets()
	{
		val h = rumble(HapticsRumbleLevel.NORMAL)
		h.push(3072, 0)
		h.tick()
		assertEquals(listOf(4 to 4), emitted) // 3072/3 = 1024, >>8 = 4
	}

	@Test
	fun smallValuesRaisedToMinimum()
	{
		val h = rumble(HapticsRumbleLevel.NORMAL)
		repeat(3) { h.push(200, 0) }
		h.tick()
		assertEquals(listOf(2 to 2), emitted) // raised to 512, >>8 = 2
	}

	@Test
	fun levelsScaleAndClamp()
	{
		val veryWeak = rumble(HapticsRumbleLevel.VERY_WEAK)
		repeat(3) { veryWeak.push(5120, 0) }
		veryWeak.tick()
		val veryStrong = rumble(HapticsRumbleLevel.VERY_STRONG)
		repeat(3) { veryStrong.push(20000, 0) }
		veryStrong.tick()
		assertEquals(listOf(4 to 4, 255 to 255), emitted) // 1024>>8=4; 100000 clamped to 65535 >>8 = 255
	}

	@Test
	fun off_ignoresPushes()
	{
		val h = rumble(HapticsRumbleLevel.OFF)
		repeat(3) { h.push(30000, 30000) }
		h.tick()
		assertEquals(listOf<Pair<Int, Int>>(), emitted)
	}

	@Test
	fun stopsWithOneZeroAfterActivity()
	{
		val h = rumble(HapticsRumbleLevel.NORMAL)
		repeat(3) { h.push(3000, 0) }
		h.tick()
		h.tick()
		h.tick()
		assertEquals(listOf(11 to 11, 0 to 0), emitted)
	}

	@Test
	fun afterClear_ticksSilent()
	{
		val h = rumble(HapticsRumbleLevel.NORMAL)
		repeat(3) { h.push(3000, 0) }
		h.tick()
		h.push(3000, 0)
		h.clear()
		h.tick()
		assertEquals(listOf(11 to 11, 0 to 0), emitted) // clear emits the stop, tick after it is silent
	}
}
