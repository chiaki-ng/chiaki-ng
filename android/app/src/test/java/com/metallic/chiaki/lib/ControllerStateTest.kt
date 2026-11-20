// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.lib

import org.junit.Assert.*
import org.junit.Test

class ControllerStateTest
{
	@Test
	fun or_mergesButtons()
	{
		val a = ControllerState(buttons = ControllerState.BUTTON_CROSS)
		val b = ControllerState(buttons = ControllerState.BUTTON_MOON)
		val merged = a or b
		assertEquals(ControllerState.BUTTON_CROSS or ControllerState.BUTTON_MOON, merged.buttons)
	}

	@Test
	fun or_mergesAnalogSticks()
	{
		val a = ControllerState(leftX = 100, leftY = 200)
		val b = ControllerState(rightX = 300, rightY = 400)
		val merged = a or b
		assertEquals(100, merged.leftX.toInt())
		assertEquals(200, merged.leftY.toInt())
		assertEquals(300, merged.rightX.toInt())
		assertEquals(400, merged.rightY.toInt())
	}

	@Test
	fun or_mergesTriggers()
	{
		val a = ControllerState(l2State = 128U)
		val b = ControllerState(r2State = 255U)
		val merged = a or b
		assertEquals(128.toUByte(), merged.l2State)
		assertEquals(255.toUByte(), merged.r2State)
	}

	@Test
	fun default_isZero()
	{
		val state = ControllerState()
		assertEquals(0U, state.buttons)
		assertEquals(0.toUByte(), state.l2State)
		assertEquals(0.toUByte(), state.r2State)
		assertEquals(0, state.leftX.toInt())
	}
}
