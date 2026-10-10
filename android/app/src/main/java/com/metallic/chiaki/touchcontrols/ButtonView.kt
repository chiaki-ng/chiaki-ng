// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.touchcontrols

import android.content.Context
import android.graphics.Canvas
import android.graphics.drawable.Drawable
import android.util.AttributeSet
import android.util.Log
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import androidx.core.view.children
import com.metallic.chiaki.R

class ButtonView @JvmOverloads constructor(
	context: Context, attrs: AttributeSet? = null, defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr)
{
	private val haptics = ButtonHaptics(context)

	var buttonPressed = false
		private set(value)
		{
			val diff = field != value
			field = value
			if(diff)
			{
				if(value)
					haptics.trigger()
				invalidate()
				buttonPressedCallback?.let { it(field) }
			}
		}

	var buttonPressedCallback: ((Boolean) -> Unit)? = null

	private val drawableIdle: Drawable?
	private val drawablePressed: Drawable?

	init
	{
		context.theme.obtainStyledAttributes(attrs, R.styleable.ButtonView, 0, 0).apply {
			drawableIdle = getDrawable(R.styleable.ButtonView_drawableIdle)
			drawablePressed = getDrawable(R.styleable.ButtonView_drawablePressed)
			recycle()
		}

		isClickable = true
	}

	override fun onDraw(canvas: Canvas)
	{
		super.onDraw(canvas)
		val drawable = if(buttonPressed) drawablePressed else drawableIdle
		drawable?.setBounds(paddingLeft, paddingTop, width - paddingRight, height - paddingBottom)
		drawable?.draw(canvas)
	}

	/**
	 * If this button overlaps with others in the same layout,
	 * let the one whose center is closest to the touch handle it.
	 */
	private fun bestFittingTouchView(x: Float, y: Float): View
	{
		val loc = locationOnScreen + Vector(x, y)
		return (parent as? ViewGroup)?.children?.filter {
			it is ButtonView
		}?.filter {
			val pos = it.locationOnScreen
			loc.x >= pos.x && loc.x < pos.x + it.width && loc.y >= pos.y && loc.y < pos.y + it.height
		}?.sortedBy {
			(loc - (it.locationOnScreen + Vector(it.width.toFloat(), it.height.toFloat()) * 0.5f)).lengthSq
		}?.firstOrNull() ?: this
	}

	override fun onTouchEvent(event: MotionEvent): Boolean
	{
		when(event.actionMasked)
		{
			MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
				if(downInSystemGestureArea(event)
						|| bestFittingTouchView(event.getX(event.actionIndex), event.getY(event.actionIndex)) != this)
					return false
				buttonPressed = true
			}
			MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
				buttonPressed = false
			}
		}
		return true
	}
}