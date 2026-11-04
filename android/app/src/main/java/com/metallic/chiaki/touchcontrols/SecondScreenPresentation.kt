// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.touchcontrols

import android.app.Presentation
import android.content.Context
import android.os.Bundle
import android.view.Display
import android.view.View
import android.view.WindowManager
import android.widget.FrameLayout
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.metallic.chiaki.databinding.FragmentControlsBinding
import com.metallic.chiaki.databinding.FragmentSecondScreenCompactBinding
import com.metallic.chiaki.databinding.SecondScreenMenuButtonBinding
import com.metallic.chiaki.lib.ControllerState
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.flatMapLatest

/**
 * Hosts touch controls on a secondary Display (e.g. the bottom screen of a
 * dual-screen handheld) via the standard Presentation API.
 *
 * Must stay FLAG_NOT_FOCUSABLE: a Presentation on this class of device that takes
 * focus is known to steal input focus from the physical controller on touch, see
 * https://github.com/shashwahpple/daggerfall-unity-android/issues/1
 */
class SecondScreenPresentation(outerContext: Context, display: Display) :
	Presentation(outerContext, display, com.metallic.chiaki.R.style.StreamTheme)
{
	private lateinit var root: FrameLayout
	private lateinit var fullBinding: FragmentControlsBinding
	private lateinit var compactBinding: FragmentSecondScreenCompactBinding
	private lateinit var fullMenuBinding: SecondScreenMenuButtonBinding

	private val fullControllerState = MutableStateFlow(ControllerState())
	private val compactButtonState = MutableStateFlow(ControllerState())
	private val controllerStateSource = MutableStateFlow<Flow<ControllerState>>(fullControllerState)

	@OptIn(ExperimentalCoroutinesApi::class)
	val controllerState: Flow<ControllerState> get() = controllerStateSource.flatMapLatest { it }

	// Simplified layout (touchpad + PS button) instead of the full button layout.
	var compact: Boolean = false
		set(value)
		{
			if(field == value)
				return
			field = value
			fullControllerState.value = ControllerState()
			compactButtonState.value = ControllerState()
			applyLayoutVisibility()
			controllerStateSource.value = activeControllerState()
		}

	var menuButtonEnabled: Boolean = false
		set(value)
		{
			field = value
			if(::root.isInitialized)
				applyLayoutVisibility()
		}

	var onMenuClicked: (() -> Unit)? = null
	var onSwapClicked: (() -> Unit)? = null

	// User-controlled opacity of the whole controls layout on the second screen.
	var opacity: Float = 1.0f
		set(value)
		{
			field = value
			if(::root.isInitialized)
				root.alpha = value
		}

	override fun onCreate(savedInstanceState: Bundle?)
	{
		super.onCreate(savedInstanceState)
		window?.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE)

		root = FrameLayout(context)
		root.alpha = opacity
		fullBinding = FragmentControlsBinding.inflate(layoutInflater, root, true)
		compactBinding = FragmentSecondScreenCompactBinding.inflate(layoutInflater, root, true)
		fullMenuBinding = SecondScreenMenuButtonBinding.inflate(layoutInflater, root, true)
		setContentView(root)

		// ButtonView reports press/release rather than clicks, so act on release.
		fullMenuBinding.menuButtonView.buttonPressedCallback = { pressed -> if(!pressed) onMenuClicked?.invoke() }
		compactBinding.menuButtonView.buttonPressedCallback = { pressed -> if(!pressed) onMenuClicked?.invoke() }
		compactBinding.swapButtonView.buttonPressedCallback = { pressed -> if(!pressed) onSwapClicked?.invoke() }

		// Without this, the system nav bar sits on top of our content and its own
		// Home button intercepts touches meant for our PS button underneath it.
		window?.let { window ->
			WindowCompat.setDecorFitsSystemWindows(window, false)
			val insetsController = WindowCompat.getInsetsController(window, root)
			insetsController.hide(WindowInsetsCompat.Type.systemBars())
			insetsController.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
		}

		fullBinding.touchpadView.alwaysVisible = true
		bindTouchControls(fullBinding) { change -> fullControllerState.value = fullControllerState.value.copy().apply(change) }
		bindCompactControls()
		applyLayoutVisibility()
		controllerStateSource.value = activeControllerState()
	}

	private fun activeControllerState(): Flow<ControllerState> =
		if(compact)
			combine(compactButtonState, compactBinding.touchpadView.controllerState) { a, b -> a or b }
		else
			combine(fullControllerState, fullBinding.touchpadView.controllerState) { a, b -> a or b }

	private fun applyLayoutVisibility()
	{
		fullBinding.root.visibility = if(compact) View.GONE else View.VISIBLE
		fullMenuBinding.root.visibility = if(!compact && menuButtonEnabled) View.VISIBLE else View.GONE
		compactBinding.root.visibility = if(compact) View.VISIBLE else View.GONE
		compactBinding.menuButtonView.visibility = if(menuButtonEnabled) View.VISIBLE else View.GONE
	}

	private fun bindCompactControls()
	{
		compactBinding.touchpadView.alwaysVisible = true
		compactBinding.psButtonView.buttonPressedCallback = { pressed ->
			compactButtonState.value = compactButtonState.value.copy().apply {
				buttons = if(pressed) buttons or ControllerState.BUTTON_PS else buttons and ControllerState.BUTTON_PS.inv()
			}
		}
	}
}
