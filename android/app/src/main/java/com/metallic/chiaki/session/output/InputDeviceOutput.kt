// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session.output

import android.graphics.Color
import android.hardware.lights.Light
import android.hardware.lights.LightState
import android.hardware.lights.LightsRequest
import android.os.Build
import android.os.CombinedVibration
import android.os.VibrationEffect
import android.view.InputDevice
import androidx.annotation.RequiresApi

/** Rumble and lights of a controller through Android's public input device APIs. */
@RequiresApi(Build.VERSION_CODES.S)
class InputDeviceOutput(device: InputDevice): ControllerOutput
{
	companion object
	{
		private const val RUMBLE_DURATION_MS = 1000L

		fun canRumble(device: InputDevice) = device.vibratorManager.vibratorIds.isNotEmpty()
	}

	private val vibratorManager = device.vibratorManager
	private val vibratorIds = vibratorManager.vibratorIds
	private val rgbLights: List<Light>
	private val playerLights: List<Light>
	private val lightsSession = device.lightsManager.let { manager ->
		val lights = manager.lights
		rgbLights = lights.filter { it.type == Light.LIGHT_TYPE_INPUT && it.hasRgbControl() }
		playerLights = lights.filter { it.type == Light.LIGHT_TYPE_PLAYER_ID }
		if(rgbLights.isNotEmpty() || playerLights.isNotEmpty()) manager.openSession() else null
	}

	override fun rumble(left: UByte, right: UByte)
	{
		vibratorManager.cancel()
		val l = left.toInt()
		val r = right.toInt()
		if(l == 0 && r == 0)
			return
		val vibration = when(vibratorIds.size)
		{
			0 -> return
			1 -> CombinedVibration.createParallel(oneShot(maxOf(l, r)))
			else -> CombinedVibration.startParallel().apply {
				if(l > 0)
					addVibrator(vibratorIds[0], oneShot(l))
				if(r > 0)
					addVibrator(vibratorIds[1], oneShot(r))
			}.combine()
		}
		vibratorManager.vibrate(vibration)
	}

	private fun oneShot(amplitude: Int) = VibrationEffect.createOneShot(RUMBLE_DURATION_MS, amplitude.coerceIn(1, 255))

	override fun led(r: UByte, g: UByte, b: UByte)
	{
		val color = Color.rgb(r.toInt(), g.toInt(), b.toInt())
		request(rgbLights) { LightState.Builder().setColor(color).build() }
	}

	override fun playerIndex(index: Int)
	{
		request(playerLights) { LightState.Builder().setPlayerId(index + 1).build() }
	}

	private fun request(lights: List<Light>, state: () -> LightState)
	{
		val session = lightsSession ?: return
		if(lights.isEmpty())
			return
		val lightState = state()
		session.requestLights(LightsRequest.Builder().apply {
			lights.forEach { addLight(it, lightState) }
		}.build())
	}

	override fun close()
	{
		vibratorManager.cancel()
		lightsSession?.close()
	}
}
