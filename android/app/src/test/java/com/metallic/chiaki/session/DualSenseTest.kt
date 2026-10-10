// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session

import com.metallic.chiaki.common.Preferences.DualSenseMode
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class DualSenseTest
{
	private fun pad(vendor: Int, product: Int, gamepad: Boolean = true) =
		InputDeviceInfo(1, "pad", vendor, product, isGamepad = gamepad, hasMotionSensors = false)

	@Test
	fun recognisesDualSenseAndEdge()
	{
		assertTrue(DualSense.isDualSense(pad(0x054c, 0x0ce6)))
		assertTrue(DualSense.isDualSense(pad(0x054c, 0x0df2)))
		assertFalse(DualSense.isDualSense(pad(0x054c, 0x09cc))) // DS4
		assertFalse(DualSense.isDualSense(pad(0x045e, 0x0b13))) // Xbox
	}

	@Test
	fun auto_onlyWithDualSenseGamepadConnected()
	{
		assertTrue(DualSense.shouldEnable(DualSenseMode.AUTO, listOf(pad(0x045e, 0x0b13), pad(0x054c, 0x0ce6))))
		assertFalse(DualSense.shouldEnable(DualSenseMode.AUTO, listOf(pad(0x045e, 0x0b13))))
		assertFalse(DualSense.shouldEnable(DualSenseMode.AUTO, listOf()))
		// a DualSense motion node alone is not a gamepad
		assertFalse(DualSense.shouldEnable(DualSenseMode.AUTO, listOf(pad(0x054c, 0x0ce6, gamepad = false))))
	}

	@Test
	fun onAndOffIgnoreDevices()
	{
		assertTrue(DualSense.shouldEnable(DualSenseMode.ON, listOf()))
		assertFalse(DualSense.shouldEnable(DualSenseMode.OFF, listOf(pad(0x054c, 0x0ce6))))
	}
}
