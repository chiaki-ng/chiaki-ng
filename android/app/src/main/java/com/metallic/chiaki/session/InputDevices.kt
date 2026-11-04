// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session

import android.hardware.Sensor
import android.os.Build
import android.view.InputDevice

data class InputDeviceInfo(
	val id: Int,
	val name: String,
	val vendorId: Int,
	val productId: Int,
	val isGamepad: Boolean,
	val hasMotionSensors: Boolean
)
{
	companion object
	{
		fun from(device: InputDevice): InputDeviceInfo
		{
			val sources = device.sources
			val isGamepad = sources and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD
					|| sources and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK
			val hasMotion = Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && device.sensorManager.let {
				it.getDefaultSensor(Sensor.TYPE_GYROSCOPE) != null && it.getDefaultSensor(Sensor.TYPE_ACCELEROMETER) != null
			}
			return InputDeviceInfo(device.id, device.name, device.vendorId, device.productId, isGamepad, hasMotion)
		}

		fun snapshot(): List<InputDeviceInfo> =
			InputDevice.getDeviceIds().asList()
				.mapNotNull { InputDevice.getDevice(it) }
				.filter { !it.isVirtual }
				.map { from(it) }
	}
}

object MotionDeviceMatcher
{
	/**
	 * The controller currently in use: the device that last sent gamepad input
	 * if it is still connected, otherwise the first connected gamepad.
	 */
	fun activeController(activeId: Int?, devices: List<InputDeviceInfo>): InputDeviceInfo? =
		devices.firstOrNull { it.id == activeId && it.isGamepad } ?: devices.firstOrNull { it.isGamepad }

	/**
	 * The device whose sensors carry the active controller's motion: the controller
	 * itself, or its separate "<name> Motion Sensors" node (DualSense, DS4, ...).
	 */
	fun pick(activeId: Int?, devices: List<InputDeviceInfo>): InputDeviceInfo?
	{
		val active = activeController(activeId, devices) ?: return null
		if(active.hasMotionSensors)
			return active
		return devices.firstOrNull { it.id != active.id && it.hasMotionSensors && it.name.startsWith(active.name) }
	}
}
