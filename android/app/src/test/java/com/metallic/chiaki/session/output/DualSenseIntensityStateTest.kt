// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session.output

import com.metallic.chiaki.lib.DualSenseIntensity
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class DualSenseIntensityStateTest
{
	@Test
	fun defaultIsFullStrength()
	{
		val s = DualSenseIntensityState()
		assertEquals(0x00.toUByte(), s.byte)
		assertTrue(s.rumbleOn)
		assertTrue(s.triggersOn)
		assertEquals(200.toUByte(), s.scale(200U))
	}

	@Test
	fun byteCombinesTriggerAndRumbleNibbles()
	{
		val s = DualSenseIntensityState()
		s.setTrigger(DualSenseIntensity.WEAK)
		s.setRumble(DualSenseIntensity.MEDIUM)
		assertEquals(0x92.toUByte(), s.byte)
		s.setTrigger(DualSenseIntensity.MEDIUM)
		s.setRumble(DualSenseIntensity.WEAK)
		assertEquals(0x63.toUByte(), s.byte)
	}

	@Test
	fun offDisablesAndSetsFullReduction()
	{
		val s = DualSenseIntensityState()
		s.setTrigger(DualSenseIntensity.OFF)
		s.setRumble(DualSenseIntensity.OFF)
		assertEquals(0xFF.toUByte(), s.byte)
		assertFalse(s.rumbleOn)
		assertFalse(s.triggersOn)
		assertEquals(0.toUByte(), s.scale(200U))
	}

	@Test
	fun scaleFollowsRumbleIntensity()
	{
		val s = DualSenseIntensityState()
		s.setRumble(DualSenseIntensity.MEDIUM)
		assertEquals(100.toUByte(), s.scale(200U))
		s.setRumble(DualSenseIntensity.WEAK)
		assertEquals(66.toUByte(), s.scale(200U))
	}
}
