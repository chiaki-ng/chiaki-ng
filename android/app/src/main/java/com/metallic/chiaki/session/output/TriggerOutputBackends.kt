// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session.output

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.InputDevice
import android.widget.Toast
import androidx.core.content.ContextCompat
import com.metallic.chiaki.R
import com.metallic.chiaki.common.Preferences
import com.metallic.chiaki.session.DualSense
import com.metallic.chiaki.session.InputDeviceInfo

/** Prefers raw Bluetooth reports for a DualSense when adaptive triggers are enabled. */
class TriggerOutputBackends(context: Context, preferences: Preferences): OutputBackends(context)
{
	companion object
	{
		private const val TAG = "TriggerOutputBackends"
	}

	/** Called on the main thread when outputs should be recreated (proxy ready, raw output failed). */
	var onBackendChanged: (() -> Unit)? = null

	private val handler = Handler(Looper.getMainLooper())
	private val rawUnavailable = mutableSetOf<Int>()
	private var toastShown = false

	private val hidHost: HidHostProxy? = when
	{
		!preferences.adaptiveTriggers || Build.VERSION.SDK_INT < Build.VERSION_CODES.S -> null
		ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED -> {
			unavailable()
			null
		}
		else -> HidHostProxy(context) { handler.post { onBackendChanged?.invoke() } }
	}

	override fun create(deviceId: Int?): ControllerOutput
	{
		val hidHost = hidHost
		if(hidHost != null && deviceId != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
			&& hidHost.isReady && synchronized(rawUnavailable) { deviceId !in rawUnavailable })
		{
			val device = InputDevice.getDevice(deviceId)
			if(device != null && DualSense.isDualSense(InputDeviceInfo.from(device)))
			{
				val transport = hidHost.transportFor(device)
				if(transport != null)
				{
					Log.i(TAG, "Using raw Bluetooth output for ${device.name}")
					return DualSenseBtOutput(transport, { rawFailed(deviceId) })
				}
			}
		}
		return super.create(deviceId)
	}

	private fun rawFailed(deviceId: Int)
	{
		Log.w(TAG, "Raw output to device $deviceId failed, falling back")
		synchronized(rawUnavailable) { rawUnavailable.add(deviceId) }
		handler.post {
			unavailable()
			onBackendChanged?.invoke()
		}
	}

	private fun unavailable()
	{
		handler.post {
			if(toastShown)
				return@post
			toastShown = true
			Toast.makeText(context, R.string.adaptive_triggers_unavailable, Toast.LENGTH_LONG).show()
		}
	}

	override fun close()
	{
		hidHost?.close()
	}
}
