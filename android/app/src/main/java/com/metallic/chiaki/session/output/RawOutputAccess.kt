// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session.output

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import androidx.core.content.ContextCompat
import com.metallic.chiaki.common.Preferences

/** Whether raw Bluetooth output (adaptive triggers) can be used. */
enum class RawOutputAccess
{
	OFF,
	NEEDS_PERMISSION,
	READY;

	companion object
	{
		fun of(enabled: Boolean, sdkInt: Int, granted: Boolean) = when
		{
			!enabled || sdkInt < Build.VERSION_CODES.S -> OFF
			!granted -> NEEDS_PERMISSION
			else -> READY
		}

		fun of(context: Context, preferences: Preferences) = of(
			preferences.adaptiveTriggers,
			Build.VERSION.SDK_INT,
			Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
				&& ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED)
	}
}
