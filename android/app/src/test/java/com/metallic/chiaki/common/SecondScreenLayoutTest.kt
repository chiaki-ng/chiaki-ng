// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.common

import com.metallic.chiaki.common.Preferences.SecondScreenLayout
import org.junit.Assert.*
import org.junit.Test

class SecondScreenLayoutTest
{
	@Test
	fun auto_followsControllerConnection()
	{
		assertTrue(SecondScreenLayout.AUTO.isSimplified(controllerConnected = true))
		assertFalse(SecondScreenLayout.AUTO.isSimplified(controllerConnected = false))
	}

	@Test
	fun full_ignoresController()
	{
		assertFalse(SecondScreenLayout.FULL.isSimplified(controllerConnected = true))
		assertFalse(SecondScreenLayout.FULL.isSimplified(controllerConnected = false))
	}

	@Test
	fun simplified_ignoresController()
	{
		assertTrue(SecondScreenLayout.SIMPLIFIED.isSimplified(controllerConnected = true))
		assertTrue(SecondScreenLayout.SIMPLIFIED.isSimplified(controllerConnected = false))
	}
}
