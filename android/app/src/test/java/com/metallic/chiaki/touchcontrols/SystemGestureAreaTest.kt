// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.touchcontrols

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SystemGestureAreaTest
{
	// 1000x500 window, back gesture zones of 40 on the sides, home zone of 60 at the bottom
	private fun inArea(x: Float, y: Float) = inSystemGestureArea(x, y, 1000, 500, left = 40, top = 0, right = 40, bottom = 60)

	@Test
	fun leftEdge_isGestureArea() = assertTrue(inArea(10f, 250f))

	@Test
	fun rightEdge_isGestureArea() = assertTrue(inArea(990f, 250f))

	@Test
	fun bottomEdge_isGestureArea() = assertTrue(inArea(500f, 460f))

	@Test
	fun inside_isNotGestureArea() = assertFalse(inArea(100f, 300f))
}
