// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.touchcontrols

import com.metallic.chiaki.lib.ControllerState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Test

class ControllerStateMergeTest
{
	@Test
	fun everySourceContributes()
	{
		// the stream screen hosts two controls fragments; input from either must reach the session
		val onScreenControls = MutableStateFlow(ControllerState(buttons = ControllerState.BUTTON_CROSS, leftX = 1000))
		val touchpadOnly = MutableStateFlow(ControllerState(buttons = ControllerState.BUTTON_TOUCHPAD))
		val merged = runBlocking { mergeControllerStates(listOf(onScreenControls, touchpadOnly)).first() }
		assertEquals(ControllerState.BUTTON_CROSS or ControllerState.BUTTON_TOUCHPAD, merged.buttons)
		assertEquals(1000, merged.leftX.toInt())
	}

	@Test
	fun singleSource_passesThrough()
	{
		val only = MutableStateFlow(ControllerState(buttons = ControllerState.BUTTON_MOON))
		assertEquals(ControllerState.BUTTON_MOON, runBlocking { mergeControllerStates(listOf(only)).first() }.buttons)
	}
}
