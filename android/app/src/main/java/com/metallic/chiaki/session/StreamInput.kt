package com.metallic.chiaki.session

import android.content.Context
import android.hardware.*
import android.hardware.input.InputManager
import android.view.*
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleObserver
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.OnLifecycleEvent
import com.metallic.chiaki.common.Preferences
import com.metallic.chiaki.lib.ControllerState

class StreamInput(val context: Context, val preferences: Preferences)
{
	var controllerStateChangedCallback: ((ControllerState) -> Unit)? = null
	var activeControllerChangedCallback: ((Int?) -> Unit)? = null

	val controllerState: ControllerState get()
	{
		val motionState = when(resolveMotionInput(motionSource, controllerMotion.isAvailable))
		{
			MotionInput.CONTROLLER -> controllerMotion.state.motionOnly()
			MotionInput.PHONE -> phoneMotionState()
			MotionInput.NONE -> ControllerState()
		}
		val controllerState = motionState or keyControllerState or motionControllerState

		// prioritize motion controller's l2 and r2 over key
		// (some controllers send only key, others both but key earlier than full press)
		if(motionControllerState.l2State > 0U)
			controllerState.l2State = motionControllerState.l2State
		if(motionControllerState.r2State > 0U)
			controllerState.r2State = motionControllerState.r2State

		return controllerState or touchControllerState
	}

	private fun phoneMotionState(): ControllerState
	{
		val state = sensorControllerState.motionOnly()
		val windowManager = context.getSystemService(Context.WINDOW_SERVICE) as WindowManager
		@Suppress("DEPRECATION")
		when(windowManager.defaultDisplay.rotation)
		{
			Surface.ROTATION_90 -> {
				state.accelX *= -1.0f
				state.accelZ *= -1.0f
				state.gyroX *= -1.0f
				state.gyroZ *= -1.0f
				state.orientX *= -1.0f
				state.orientZ *= -1.0f
			}
			else -> {}
		}
		return state
	}

	private fun ControllerState.motionOnly() = ControllerState(
		gyroX = gyroX, gyroY = gyroY, gyroZ = gyroZ,
		accelX = accelX, accelY = accelY, accelZ = accelZ,
		orientX = orientX, orientY = orientY, orientZ = orientZ, orientW = orientW)

	private val sensorControllerState = ControllerState() // from Motion Sensors
	private val keyControllerState = ControllerState() // from KeyEvents
	private val motionControllerState = ControllerState() // from MotionEvents
	var touchControllerState = ControllerState()
		set(value)
		{
			field = value
			controllerStateUpdated()
		}

	private val swapCrossMoon = preferences.swapCrossMoon
	private val motionSource = preferences.motionSource
	private val controllerMotion = ControllerMotion { controllerStateUpdated() }
	private var lastInputDeviceId: Int? = null

	val activeControllerId: Int? get() =
		MotionDeviceMatcher.activeController(lastInputDeviceId, InputDeviceInfo.snapshot())?.id

	private val inputManager = context.getSystemService(Context.INPUT_SERVICE) as InputManager
	private val inputDeviceListener = object: InputManager.InputDeviceListener {
		override fun onInputDeviceAdded(deviceId: Int) = activeControllerMaybeChanged()
		override fun onInputDeviceRemoved(deviceId: Int)
		{
			if(deviceId == lastInputDeviceId)
				lastInputDeviceId = null
			activeControllerMaybeChanged()
		}
		override fun onInputDeviceChanged(deviceId: Int) = activeControllerMaybeChanged()
	}

	private val sensorEventListener = object: SensorEventListener {
		override fun onSensorChanged(event: SensorEvent)
		{
			when(event.sensor.type)
			{
				Sensor.TYPE_ACCELEROMETER -> {
					sensorControllerState.accelX = event.values[1] / SensorManager.GRAVITY_EARTH
					sensorControllerState.accelY = event.values[2] / SensorManager.GRAVITY_EARTH
					sensorControllerState.accelZ = event.values[0] / SensorManager.GRAVITY_EARTH
				}
				Sensor.TYPE_GYROSCOPE -> {
					sensorControllerState.gyroX = event.values[1]
					sensorControllerState.gyroY = event.values[2]
					sensorControllerState.gyroZ = event.values[0]
				}
				Sensor.TYPE_ROTATION_VECTOR -> {
					val q = floatArrayOf(0f, 0f, 0f, 0f)
					SensorManager.getQuaternionFromVector(q, event.values)
					sensorControllerState.orientX = q[2]
					sensorControllerState.orientY = q[3]
					sensorControllerState.orientZ = q[1]
					sensorControllerState.orientW = q[0]
				}
				else -> return
			}
			controllerStateUpdated()
		}

		override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) {}
	}

	private val usePhoneMotion = motionSource == Preferences.MotionSource.AUTO || motionSource == Preferences.MotionSource.PHONE
	private val useControllerMotion = motionSource == Preferences.MotionSource.AUTO || motionSource == Preferences.MotionSource.CONTROLLER

	private val lifecycleObserver = object: LifecycleObserver {
		@OnLifecycleEvent(Lifecycle.Event.ON_RESUME)
		fun onResume()
		{
			inputManager.registerInputDeviceListener(inputDeviceListener, null)
			if(usePhoneMotion)
			{
				val samplingPeriodUs = 4000
				val sensorManager = context.getSystemService(Context.SENSOR_SERVICE) as SensorManager
				listOfNotNull(
					sensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER),
					sensorManager.getDefaultSensor(Sensor.TYPE_GYROSCOPE),
					sensorManager.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR)
				).forEach {
					sensorManager.registerListener(sensorEventListener, it, samplingPeriodUs)
				}
			}
			if(useControllerMotion)
			{
				controllerMotion.start()
				controllerMotion.activeControllerId = lastInputDeviceId
			}
		}

		@OnLifecycleEvent(Lifecycle.Event.ON_PAUSE)
		fun onPause()
		{
			inputManager.unregisterInputDeviceListener(inputDeviceListener)
			val sensorManager = context.getSystemService(Context.SENSOR_SERVICE) as SensorManager
			sensorManager.unregisterListener(sensorEventListener)
			controllerMotion.stop()
		}
	}

	fun observe(lifecycleOwner: LifecycleOwner)
	{
		lifecycleOwner.lifecycle.addObserver(lifecycleObserver)
	}

	fun onMotionReset()
	{
		controllerMotion.reset()
	}

	private fun noteInputDevice(event: InputEvent)
	{
		if(!event.isFromSource(InputDevice.SOURCE_GAMEPAD) && !event.isFromSource(InputDevice.SOURCE_JOYSTICK))
			return
		if(event.deviceId == lastInputDeviceId)
			return
		lastInputDeviceId = event.deviceId
		activeControllerMaybeChanged()
	}

	private var lastNotifiedControllerId: Int? = null

	private fun activeControllerMaybeChanged()
	{
		controllerMotion.activeControllerId = lastInputDeviceId
		val id = activeControllerId
		if(id != lastNotifiedControllerId)
		{
			lastNotifiedControllerId = id
			activeControllerChangedCallback?.invoke(id)
		}
	}

	private fun controllerStateUpdated()
	{
		controllerStateChangedCallback?.let { it(controllerState) }
	}

	fun dispatchKeyEvent(event: KeyEvent): Boolean
	{
		//Log.i("StreamSession", "key event $event")
		if(event.action != KeyEvent.ACTION_DOWN && event.action != KeyEvent.ACTION_UP)
			return false
		noteInputDevice(event)

		when(event.keyCode)
		{
			KeyEvent.KEYCODE_BUTTON_L2 -> {
				keyControllerState.l2State = if(event.action == KeyEvent.ACTION_DOWN) UByte.MAX_VALUE else 0U
				return true
			}
			KeyEvent.KEYCODE_BUTTON_R2 -> {
				keyControllerState.r2State = if(event.action == KeyEvent.ACTION_DOWN) UByte.MAX_VALUE else 0U
				return true
			}
		}

		val buttonMask: UInt = when(event.keyCode)
		{
			// dpad handled by MotionEvents
			//KeyEvent.KEYCODE_DPAD_LEFT -> ControllerState.BUTTON_DPAD_LEFT
			//KeyEvent.KEYCODE_DPAD_RIGHT -> ControllerState.BUTTON_DPAD_RIGHT
			//KeyEvent.KEYCODE_DPAD_UP -> ControllerState.BUTTON_DPAD_UP
			//KeyEvent.KEYCODE_DPAD_DOWN -> ControllerState.BUTTON_DPAD_DOWN
			KeyEvent.KEYCODE_BUTTON_A -> if(swapCrossMoon) ControllerState.BUTTON_MOON else ControllerState.BUTTON_CROSS
			KeyEvent.KEYCODE_BUTTON_B -> if(swapCrossMoon) ControllerState.BUTTON_CROSS else ControllerState.BUTTON_MOON
			KeyEvent.KEYCODE_BUTTON_X -> if(swapCrossMoon) ControllerState.BUTTON_PYRAMID else ControllerState.BUTTON_BOX
			KeyEvent.KEYCODE_BUTTON_Y -> if(swapCrossMoon) ControllerState.BUTTON_BOX else ControllerState.BUTTON_PYRAMID
			KeyEvent.KEYCODE_BUTTON_L1 -> ControllerState.BUTTON_L1
			KeyEvent.KEYCODE_BUTTON_R1 -> ControllerState.BUTTON_R1
			KeyEvent.KEYCODE_BUTTON_THUMBL -> ControllerState.BUTTON_L3
			KeyEvent.KEYCODE_BUTTON_THUMBR -> ControllerState.BUTTON_R3
			KeyEvent.KEYCODE_BUTTON_SELECT -> ControllerState.BUTTON_SHARE
			KeyEvent.KEYCODE_BUTTON_START -> ControllerState.BUTTON_OPTIONS
			KeyEvent.KEYCODE_BUTTON_C -> ControllerState.BUTTON_PS
			KeyEvent.KEYCODE_BUTTON_MODE -> ControllerState.BUTTON_PS
			else -> return false
		}

		keyControllerState.buttons = keyControllerState.buttons.run {
			when(event.action)
			{
				KeyEvent.ACTION_DOWN -> this or buttonMask
				KeyEvent.ACTION_UP -> this and buttonMask.inv()
				else -> this
			}
		}

		controllerStateUpdated()
		return true
	}

	fun onGenericMotionEvent(event: MotionEvent): Boolean
	{
		if(event.source and InputDevice.SOURCE_CLASS_JOYSTICK != InputDevice.SOURCE_CLASS_JOYSTICK)
			return false
		noteInputDevice(event)
		fun Float.signedAxis() = (this * Short.MAX_VALUE).toInt().toShort()
		fun Float.unsignedAxis() = (this * UByte.MAX_VALUE.toFloat()).toUInt().toUByte()
		motionControllerState.leftX = event.getAxisValue(MotionEvent.AXIS_X).signedAxis()
		motionControllerState.leftY = event.getAxisValue(MotionEvent.AXIS_Y).signedAxis()
		motionControllerState.rightX = event.getAxisValue(MotionEvent.AXIS_Z).signedAxis()
		motionControllerState.rightY = event.getAxisValue(MotionEvent.AXIS_RZ).signedAxis()
		motionControllerState.l2State = event.getAxisValue(MotionEvent.AXIS_LTRIGGER).unsignedAxis()
		motionControllerState.r2State = event.getAxisValue(MotionEvent.AXIS_RTRIGGER).unsignedAxis()
		motionControllerState.buttons = motionControllerState.buttons.let {
			val dpadX = event.getAxisValue(MotionEvent.AXIS_HAT_X)
			val dpadY = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
			val dpadButtons =
				(if(dpadX > 0.5f) ControllerState.BUTTON_DPAD_RIGHT else 0U) or
						(if(dpadX < -0.5f) ControllerState.BUTTON_DPAD_LEFT else 0U) or
						(if(dpadY > 0.5f) ControllerState.BUTTON_DPAD_DOWN else 0U) or
						(if(dpadY < -0.5f) ControllerState.BUTTON_DPAD_UP else 0U)
			it and (ControllerState.BUTTON_DPAD_RIGHT or
					ControllerState.BUTTON_DPAD_LEFT or
					ControllerState.BUTTON_DPAD_DOWN or
					ControllerState.BUTTON_DPAD_UP).inv() or
					dpadButtons
		}
		//Log.i("StreamSession", "motionEvent => $motionControllerState")
		controllerStateUpdated()
		return true
	}
}