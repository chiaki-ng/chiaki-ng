// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.stream

import androidx.activity.OnBackPressedDispatcher
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class StreamBackCallbackTest
{
	private var overlayVisible = false
	private var left = 0
	private val dispatcher = OnBackPressedDispatcher().also {
		it.addCallback(StreamBackCallback(
			isOverlayVisible = { overlayVisible },
			showOverlay = { overlayVisible = true },
			leave = { left++ }))
	}

	@Test
	fun back_showsOverlay()
	{
		dispatcher.onBackPressed()
		assertTrue(overlayVisible)
		assertEquals(0, left)
	}

	@Test
	fun backWithOverlayShown_leaves()
	{
		dispatcher.onBackPressed()
		dispatcher.onBackPressed()
		assertEquals(1, left)
	}
}
