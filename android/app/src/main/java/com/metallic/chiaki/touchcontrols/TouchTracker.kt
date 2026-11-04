// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.touchcontrols

import android.view.MotionEvent

class TouchTracker
{
	var currentPosition: Vector? = null
		private set(value)
		{
			field = value
			positionChangedCallback?.let { it(field) }
		}

	var positionChangedCallback: ((Vector?) -> Unit)? = null

	private var pointerId: Int? = null

	fun touchEvent(event: MotionEvent) =
		touchEvent(event.actionMasked, event.getPointerId(event.actionIndex)) {
			val pointerIndex = event.findPointerIndex(it)
			if(pointerIndex >= 0) Vector(event.getX(pointerIndex), event.getY(pointerIndex)) else null
		}

	/**
	 * @param actionPointerId id of the pointer the action is about
	 * @param position current position of the pointer with the given id, null if it is not in the event
	 */
	fun touchEvent(actionMasked: Int, actionPointerId: Int, position: (Int) -> Vector?)
	{
		when(actionMasked)
		{
			MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN ->
			{
				if(pointerId == null)
				{
					pointerId = actionPointerId
					currentPosition = position(actionPointerId)
				}
			}

			MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP ->
			{
				if(actionPointerId == pointerId)
				{
					pointerId = null
					currentPosition = null
				}
			}

			// the touch was taken over, e.g. by a system gesture
			MotionEvent.ACTION_CANCEL ->
			{
				pointerId = null
				currentPosition = null
			}

			MotionEvent.ACTION_MOVE ->
			{
				val pointerId = pointerId
				if(pointerId != null)
					position(pointerId)?.let { currentPosition = it }
			}
		}
	}
}