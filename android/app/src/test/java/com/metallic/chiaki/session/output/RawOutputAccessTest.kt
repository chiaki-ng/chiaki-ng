// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session.output

import android.os.Build
import org.junit.Assert.*
import org.junit.Test

class RawOutputAccessTest
{
	@Test
	fun of_settingOff_isOff()
	{
		assertEquals(RawOutputAccess.OFF, RawOutputAccess.of(enabled = false, sdkInt = Build.VERSION_CODES.S, granted = true))
	}

	@Test
	fun of_beforeAndroid12_isOff()
	{
		assertEquals(RawOutputAccess.OFF, RawOutputAccess.of(enabled = true, sdkInt = Build.VERSION_CODES.R, granted = false))
	}

	@Test
	fun of_withoutPermission_needsPermission()
	{
		assertEquals(RawOutputAccess.NEEDS_PERMISSION, RawOutputAccess.of(enabled = true, sdkInt = Build.VERSION_CODES.S, granted = false))
	}

	@Test
	fun of_withPermission_isReady()
	{
		assertEquals(RawOutputAccess.READY, RawOutputAccess.of(enabled = true, sdkInt = Build.VERSION_CODES.TIRAMISU, granted = true))
	}
}
