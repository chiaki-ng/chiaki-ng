// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class MotionDeviceMatcherTest
{
	private val dsPad = InputDeviceInfo(10, "DualSense Wireless Controller", 0x054c, 0x0ce6, isGamepad = true, hasMotionSensors = false)
	private val dsMotion = InputDeviceInfo(11, "DualSense Wireless Controller Motion Sensors", 0x054c, 0x0ce6, isGamepad = false, hasMotionSensors = true)
	private val xbox = InputDeviceInfo(20, "Xbox Wireless Controller", 0x045e, 0x0b13, isGamepad = true, hasMotionSensors = false)
	private val merged = InputDeviceInfo(30, "Pro Controller", 0x057e, 0x2009, isGamepad = true, hasMotionSensors = true)

	@Test
	fun separateMotionNode_matchedByNamePrefix()
	{
		assertEquals(dsMotion, MotionDeviceMatcher.pick(10, listOf(dsPad, dsMotion, xbox)))
	}

	@Test
	fun mergedDevice_usesItself()
	{
		assertEquals(merged, MotionDeviceMatcher.pick(30, listOf(merged, xbox)))
	}

	@Test
	fun activeWithoutMotion_returnsNull()
	{
		assertNull(MotionDeviceMatcher.pick(20, listOf(dsPad, dsMotion, xbox)))
	}

	@Test
	fun noInputYet_usesFirstGamepad()
	{
		assertEquals(dsMotion, MotionDeviceMatcher.pick(null, listOf(dsPad, dsMotion)))
	}

	@Test
	fun activeRemoved_fallsBackToRemainingGamepad()
	{
		// active id 10 vanished (pad disconnected); Xbox remains and has no motion
		assertNull(MotionDeviceMatcher.pick(10, listOf(xbox)))
		assertEquals(xbox, MotionDeviceMatcher.activeController(10, listOf(xbox)))
	}

	@Test
	fun activeRemoved_noGamepads_returnsNull()
	{
		assertNull(MotionDeviceMatcher.activeController(10, listOf()))
		assertNull(MotionDeviceMatcher.pick(10, listOf()))
	}

	@Test
	fun motionNodeNeverChosenAsActiveController()
	{
		assertEquals(dsPad, MotionDeviceMatcher.activeController(11, listOf(dsPad, dsMotion)))
	}
}
