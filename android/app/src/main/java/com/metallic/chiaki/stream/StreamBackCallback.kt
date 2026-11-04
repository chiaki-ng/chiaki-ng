// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.stream

import androidx.activity.OnBackPressedCallback

/**
 * Back first shows the overlay, Back again while it is shown leaves the stream.
 * Swiping in the hidden system bars can't show the overlay, as transient bars are not reported to the app.
 */
class StreamBackCallback(
	private val isOverlayVisible: () -> Boolean,
	private val showOverlay: () -> Unit,
	private val leave: () -> Unit): OnBackPressedCallback(true)
{
	override fun handleOnBackPressed()
	{
		if(isOverlayVisible())
			leave()
		else
			showOverlay()
	}
}
