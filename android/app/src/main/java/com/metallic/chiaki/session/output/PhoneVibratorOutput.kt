// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session.output

import android.content.Context
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import kotlin.math.min

class PhoneVibratorOutput(context: Context): ControllerOutput
{
	@Suppress("DEPRECATION")
	private val vibrator = context.getSystemService(Context.VIBRATOR_SERVICE) as Vibrator

	override fun rumble(left: UByte, right: UByte)
	{
		val amplitude = min(255, (left.toInt() + right.toInt()) / 2)
		vibrator.cancel()
		if(amplitude == 0)
			return
		if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
			vibrator.vibrate(VibrationEffect.createOneShot(1000, amplitude))
		else
			@Suppress("DEPRECATION")
			vibrator.vibrate(1000)
	}

	override fun close()
	{
		vibrator.cancel()
	}
}
