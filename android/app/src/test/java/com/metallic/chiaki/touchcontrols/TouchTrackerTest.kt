// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.touchcontrols

import android.view.MotionEvent
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class TouchTrackerTest
{
	private val tracker = TouchTracker()
	private val finger = Vector(10f, 20f)

	@Test
	fun cancel_releasesTouch()
	{
		tracker.touchEvent(MotionEvent.ACTION_DOWN, 0) { finger }
		// a system gesture, like swiping home from the controls, takes the touch over
		tracker.touchEvent(MotionEvent.ACTION_CANCEL, 0) { null }
		assertNull(tracker.currentPosition)
	}

	@Test
	fun up_releasesTouch()
	{
		tracker.touchEvent(MotionEvent.ACTION_DOWN, 0) { finger }
		tracker.touchEvent(MotionEvent.ACTION_UP, 0) { finger }
		assertNull(tracker.currentPosition)
	}

	@Test
	fun move_followsTrackedPointer()
	{
		tracker.touchEvent(MotionEvent.ACTION_DOWN, 0) { finger }
		tracker.touchEvent(MotionEvent.ACTION_MOVE, 0) { if(it == 0) Vector(30f, 40f) else null }
		assertEquals(Vector(30f, 40f), tracker.currentPosition)
	}

	@Test
	fun secondPointerUp_keepsFirstTouch()
	{
		tracker.touchEvent(MotionEvent.ACTION_DOWN, 0) { finger }
		tracker.touchEvent(MotionEvent.ACTION_POINTER_DOWN, 1) { Vector(50f, 50f) }
		tracker.touchEvent(MotionEvent.ACTION_POINTER_UP, 1) { Vector(50f, 50f) }
		assertEquals(finger, tracker.currentPosition)
	}
}
