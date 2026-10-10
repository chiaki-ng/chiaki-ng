// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session

import com.metallic.chiaki.common.Preferences
import com.metallic.chiaki.common.Preferences.MotionSource
import org.junit.Assert.assertEquals
import org.junit.Test

class MotionSourceTest
{
	@Test
	fun resolve_table()
	{
		assertEquals(MotionInput.CONTROLLER, resolveMotionInput(MotionSource.AUTO, true))
		assertEquals(MotionInput.PHONE, resolveMotionInput(MotionSource.AUTO, false))
		assertEquals(MotionInput.CONTROLLER, resolveMotionInput(MotionSource.CONTROLLER, true))
		assertEquals(MotionInput.NONE, resolveMotionInput(MotionSource.CONTROLLER, false))
		assertEquals(MotionInput.PHONE, resolveMotionInput(MotionSource.PHONE, true))
		assertEquals(MotionInput.PHONE, resolveMotionInput(MotionSource.PHONE, false))
		assertEquals(MotionInput.NONE, resolveMotionInput(MotionSource.OFF, true))
		assertEquals(MotionInput.NONE, resolveMotionInput(MotionSource.OFF, false))
	}

	@Test
	fun migrate_noStoredValue_usesLegacySwitch()
	{
		assertEquals(MotionSource.AUTO, Preferences.motionSourceFromStored(null, legacyMotionEnabled = true))
		assertEquals(MotionSource.OFF, Preferences.motionSourceFromStored(null, legacyMotionEnabled = false))
	}

	@Test
	fun migrate_storedValueWins()
	{
		assertEquals(MotionSource.PHONE, Preferences.motionSourceFromStored("phone", legacyMotionEnabled = false))
		assertEquals(MotionSource.CONTROLLER, Preferences.motionSourceFromStored("controller", legacyMotionEnabled = true))
	}

	@Test
	fun migrate_unknownStoredValue_fallsBackToDefault()
	{
		assertEquals(Preferences.motionSourceDefault, Preferences.motionSourceFromStored("bogus", legacyMotionEnabled = false))
	}
}
