// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session

import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.InputDevice
import com.metallic.chiaki.lib.ControllerState
import com.metallic.chiaki.lib.MotionTracker

/**
 * Motion from the sensors of the active controller (Android 12+).
 * All methods must be called on the main thread except reset().
 */
class ControllerMotion(private val onChanged: () -> Unit)
{
	companion object
	{
		private const val TAG = "ControllerMotion"
		private const val SAMPLING_PERIOD_US = 4000
	}

	private val handler = Handler(Looper.getMainLooper())
	private var running = false
	private var tracker: MotionTracker? = null
	private var sensorManager: SensorManager? = null
	private var boundDeviceId: Int? = null

	/** gyro/accel/orient fields only */
	val state = ControllerState()

	val isAvailable get() = sensorManager != null

	var activeControllerId: Int? = null
		set(value)
		{
			field = value
			refresh()
		}

	private val sensorListener = object: SensorEventListener {
		override fun onSensorChanged(event: SensorEvent)
		{
			val tracker = tracker ?: return
			val timestampUs = (event.timestamp / 1000).toInt()
			when(event.sensor.type)
			{
				Sensor.TYPE_ACCELEROMETER -> tracker.updateAccel(
					event.values[0] / SensorManager.GRAVITY_EARTH,
					event.values[1] / SensorManager.GRAVITY_EARTH,
					event.values[2] / SensorManager.GRAVITY_EARTH,
					timestampUs)
				Sensor.TYPE_GYROSCOPE -> tracker.updateGyro(event.values[0], event.values[1], event.values[2], timestampUs)
				else -> return
			}
			tracker.applyTo(state)
			onChanged()
		}

		override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) {}
	}

	fun start()
	{
		if(Build.VERSION.SDK_INT < Build.VERSION_CODES.S)
			return
		running = true
		refresh()
	}

	fun stop()
	{
		running = false
		unbind()
	}

	/** Re-evaluate which device's sensors to use, e.g. after devices were added or removed. */
	fun refresh()
	{
		if(!running || Build.VERSION.SDK_INT < Build.VERSION_CODES.S)
			return
		val target = MotionDeviceMatcher.pick(activeControllerId, InputDeviceInfo.snapshot())
		if(target?.id == boundDeviceId && (target == null || sensorManager != null))
			return
		unbind()
		val device = target?.let { InputDevice.getDevice(it.id) }
		if(device != null)
			bind(device)
		onChanged()
	}

	fun reset()
	{
		handler.post {
			val tracker = tracker ?: return@post
			tracker.reset()
			tracker.applyTo(state)
			onChanged()
		}
	}

	private fun bind(device: InputDevice)
	{
		if(Build.VERSION.SDK_INT < Build.VERSION_CODES.S)
			return
		val sm = device.sensorManager
		val gyro = sm.getDefaultSensor(Sensor.TYPE_GYROSCOPE)
		val accel = sm.getDefaultSensor(Sensor.TYPE_ACCELEROMETER)
		if(gyro == null || accel == null)
			return
		tracker = MotionTracker()
		sm.registerListener(sensorListener, accel, SAMPLING_PERIOD_US, handler)
		sm.registerListener(sensorListener, gyro, SAMPLING_PERIOD_US, handler)
		sensorManager = sm
		boundDeviceId = device.id
		Log.i(TAG, "Using motion sensors of ${device.name} (${device.id})")
	}

	private fun unbind()
	{
		sensorManager?.unregisterListener(sensorListener)
		sensorManager = null
		boundDeviceId = null
		tracker?.dispose()
		tracker = null
		MotionTracker.neutral(state)
	}
}
