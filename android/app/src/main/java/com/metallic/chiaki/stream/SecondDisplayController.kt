// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.stream

import android.content.Context
import android.hardware.display.DisplayManager
import android.hardware.input.InputManager
import android.os.Handler
import android.os.Looper
import android.view.Display
import android.view.InputDevice
import com.metallic.chiaki.common.Preferences
import com.metallic.chiaki.lib.ControllerState
import com.metallic.chiaki.touchcontrols.SecondScreenPresentation
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.flatMapLatest

/**
 * Drives touch controls on a secondary Display (e.g. the bottom screen of a
 * dual-screen handheld) via SecondScreenPresentation, when one is
 * available. Display discovery is by capability (DISPLAY_CATEGORY_PRESENTATION),
 * not tied to any specific device, since the physical display ID is not stable
 * across sessions.
 */
class SecondDisplayController(private val context: Context)
{
	private val displayManager = context.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
	private val inputManager = context.getSystemService(Context.INPUT_SERVICE) as InputManager
	private val preferences = Preferences(context)
	private val handler = Handler(Looper.getMainLooper())

	private var presentation: SecondScreenPresentation? = null
	private val presentationFlow = MutableStateFlow<Flow<ControllerState>>(MutableStateFlow(ControllerState()))

	@OptIn(ExperimentalCoroutinesApi::class)
	val controllerState: Flow<ControllerState> get() = presentationFlow.flatMapLatest { it }

	private val _isActive = MutableStateFlow(false)
	val isActive: StateFlow<Boolean> get() = _isActive.asStateFlow()

	private var started = false

	var onMenuRequested: (() -> Unit)? = null

	// Lets the user release the second display mid-session (e.g. to use another app
	// on it) and reclaim it later, without the display/input listeners taking it back.
	var enabled: Boolean = true
		set(value)
		{
			if(field == value)
				return
			field = value
			if(value)
				refreshDisplay()
			else
				dismissPresentation()
		}

	private val displayListener = object: DisplayManager.DisplayListener
	{
		override fun onDisplayAdded(displayId: Int) = refreshDisplay()
		override fun onDisplayRemoved(displayId: Int) = refreshDisplay()
		override fun onDisplayChanged(displayId: Int) {}
	}

	private val inputDeviceListener = object: InputManager.InputDeviceListener
	{
		override fun onInputDeviceAdded(deviceId: Int) = refreshCompactMode()
		override fun onInputDeviceRemoved(deviceId: Int) = refreshCompactMode()
		override fun onInputDeviceChanged(deviceId: Int) {}
	}

	fun start()
	{
		started = true
		displayManager.registerDisplayListener(displayListener, handler)
		inputManager.registerInputDeviceListener(inputDeviceListener, handler)
		refreshDisplay()
	}

	fun stop()
	{
		started = false
		displayManager.unregisterDisplayListener(displayListener)
		inputManager.unregisterInputDeviceListener(inputDeviceListener)
		dismissPresentation()
	}

	private fun secondDisplay(): Display? =
		displayManager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION).firstOrNull()

	val hasDisplay: Boolean get() = secondDisplay() != null

	// Generic gamepad/joystick check, not specific to any device's own built-in
	// controls -- on a handheld whose physical controls always register as a gamepad,
	// this is expected to be true for the whole session.
	private fun controllerConnected(): Boolean =
		inputManager.inputDeviceIds.any { id ->
			val device = InputDevice.getDevice(id) ?: return@any false
			!device.isVirtual &&
				(device.sources and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD ||
					device.sources and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK)
		}

	private fun desiredCompact() = preferences.secondScreenLayout.isSimplified(controllerConnected())

	private fun refreshDisplay()
	{
		if(!started || !enabled)
			return
		val display = secondDisplay()
		if(display == null)
		{
			dismissPresentation()
			return
		}
		if(presentation == null || presentation?.display?.displayId != display.displayId)
		{
			presentation?.dismiss()
			presentation = SecondScreenPresentation(context, display).also {
				it.onMenuClicked = { onMenuRequested?.invoke() }
				it.onSwapClicked = { enabled = false }
				it.menuButtonEnabled = preferences.secondScreenMenuButtonEnabled
				it.show()
				it.compact = desiredCompact()
				it.opacity = preferences.secondScreenOpacity / 100f
				presentationFlow.value = it.controllerState
			}
		}
		_isActive.value = true
	}

	private fun refreshCompactMode()
	{
		presentation?.compact = desiredCompact()
	}

	private fun dismissPresentation()
	{
		presentation?.dismiss()
		presentation = null
		presentationFlow.value = MutableStateFlow(ControllerState())
		_isActive.value = false
	}
}
