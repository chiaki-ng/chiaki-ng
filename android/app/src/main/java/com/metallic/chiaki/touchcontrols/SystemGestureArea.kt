// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.touchcontrols

import android.view.MotionEvent
import android.view.View
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat

/** Whether window position x, y lies in the system gesture insets at the edges of a width x height window. */
fun inSystemGestureArea(x: Float, y: Float, width: Int, height: Int, left: Int, top: Int, right: Int, bottom: Int) =
	x < left || y < top || x >= width - right || y >= height - bottom

/**
 * Whether the pointer that went down in this event started in a system gesture area, like the edges
 * for back and home. Such a touch is meant for the system, so controls should not react to it.
 */
fun View.downInSystemGestureArea(event: MotionEvent): Boolean
{
	if(event.actionMasked != MotionEvent.ACTION_DOWN && event.actionMasked != MotionEvent.ACTION_POINTER_DOWN)
		return false
	val insets = ViewCompat.getRootWindowInsets(this)?.getInsets(WindowInsetsCompat.Type.systemGestures()) ?: return false
	val location = IntArray(2)
	getLocationInWindow(location)
	return inSystemGestureArea(
		location[0] + event.getX(event.actionIndex), location[1] + event.getY(event.actionIndex),
		rootView.width, rootView.height,
		insets.left, insets.top, insets.right, insets.bottom)
}
