// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session.output

import android.content.Context
import android.os.Build
import android.view.InputDevice

/** Chooses the output for a controller: its own motors when it has any, otherwise the phone. */
open class OutputBackends(protected val context: Context)
{
	open fun create(deviceId: Int?): ControllerOutput
	{
		if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && deviceId != null)
		{
			val device = InputDevice.getDevice(deviceId)
			if(device != null && InputDeviceOutput.canRumble(device))
				return InputDeviceOutput(device)
		}
		return PhoneVibratorOutput(context)
	}

	open fun close() {}
}
