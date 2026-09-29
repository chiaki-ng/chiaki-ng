// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.touchcontrols

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.lifecycle.LiveData
import androidx.lifecycle.Observer
import com.metallic.chiaki.databinding.FragmentControlsBinding
import com.metallic.chiaki.lib.ControllerState
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.flatMapLatest

abstract class TouchControlsFragment : Fragment()
{
	protected var ownControllerState = ControllerState()
		set(value)
		{
			val diff = field != value
			field = value
			if(diff)
				_ownControllerStateFlow.value = ownControllerState
		}

	protected val _ownControllerStateFlow = MutableStateFlow(ControllerState())

	// Proxy to delay attaching to the touchpadView until it's available.
	// Starts emitting ownControllerState, then switches to combined flow when touchpad is ready.
	protected val _controllerStateSource = MutableStateFlow<Flow<ControllerState>>(_ownControllerStateFlow)

	@OptIn(kotlinx.coroutines.ExperimentalCoroutinesApi::class)
	val controllerState: Flow<ControllerState> get() =
		_controllerStateSource.flatMapLatest { it }

	var onScreenControlsEnabled: LiveData<Boolean>? = null
}

class DefaultTouchControlsFragment : TouchControlsFragment()
{
	private var _binding: FragmentControlsBinding? = null
	private val binding get() = _binding!!

	override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View =
		FragmentControlsBinding.inflate(inflater, container, false).let {
			_binding = it
			_controllerStateSource.value =
				combine(_ownControllerStateFlow, binding.touchpadView.controllerState) { a, b -> a or b }
			it.root
		}

	override fun onViewCreated(view: View, savedInstanceState: Bundle?)
	{
		super.onViewCreated(view, savedInstanceState)
		bindTouchControls(binding) { change -> ownControllerState = ownControllerState.copy().apply(change) }

		onScreenControlsEnabled?.observe(viewLifecycleOwner, Observer {
			view.visibility = if(it) View.VISIBLE else View.GONE
		})
	}
}

// Wires every control in the layout except the touchpad, which callers combine separately.
// [update] applies a change to a copy of the caller's current controller state.
fun bindTouchControls(binding: FragmentControlsBinding, update: (ControllerState.() -> Unit) -> Unit)
{
	fun buttonStateChanged(buttonMask: UInt) = { pressed: Boolean ->
		update {
			buttons =
				if(pressed)
					buttons or buttonMask
				else
					buttons and buttonMask.inv()
		}
	}

	binding.dpadView.stateChangeCallback = { direction ->
		update {
			buttons = ((buttons
						and ControllerState.BUTTON_DPAD_LEFT.inv()
						and ControllerState.BUTTON_DPAD_RIGHT.inv()
						and ControllerState.BUTTON_DPAD_UP.inv()
						and ControllerState.BUTTON_DPAD_DOWN.inv())
					or when(direction)
					{
						DPadView.Direction.UP -> ControllerState.BUTTON_DPAD_UP
						DPadView.Direction.DOWN -> ControllerState.BUTTON_DPAD_DOWN
						DPadView.Direction.LEFT -> ControllerState.BUTTON_DPAD_LEFT
						DPadView.Direction.RIGHT -> ControllerState.BUTTON_DPAD_RIGHT
						DPadView.Direction.LEFT_UP -> ControllerState.BUTTON_DPAD_LEFT or ControllerState.BUTTON_DPAD_UP
						DPadView.Direction.LEFT_DOWN -> ControllerState.BUTTON_DPAD_LEFT or ControllerState.BUTTON_DPAD_DOWN
						DPadView.Direction.RIGHT_UP -> ControllerState.BUTTON_DPAD_RIGHT or ControllerState.BUTTON_DPAD_UP
						DPadView.Direction.RIGHT_DOWN -> ControllerState.BUTTON_DPAD_RIGHT or ControllerState.BUTTON_DPAD_DOWN
						null -> 0U
					})
		}
	}

	binding.crossButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_CROSS)
	binding.moonButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_MOON)
	binding.pyramidButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_PYRAMID)
	binding.boxButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_BOX)
	binding.l1ButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_L1)
	binding.r1ButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_R1)
	binding.l3ButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_L3)
	binding.r3ButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_R3)
	binding.optionsButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_OPTIONS)
	binding.shareButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_SHARE)
	binding.psButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_PS)

	binding.l2ButtonView.buttonPressedCallback = { pressed -> update { l2State = if(pressed) 255U else 0U } }
	binding.r2ButtonView.buttonPressedCallback = { pressed -> update { r2State = if(pressed) 255U else 0U } }

	val quantizeStick = { f: Float ->
		(Short.MAX_VALUE * f).toInt().toShort()
	}

	binding.leftAnalogStickView.stateChangedCallback = { stick -> update {
		leftX = quantizeStick(stick.x)
		leftY = quantizeStick(stick.y)
	}}

	binding.rightAnalogStickView.stateChangedCallback = { stick -> update {
		rightX = quantizeStick(stick.x)
		rightY = quantizeStick(stick.y)
	}}
}
