// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.stream

import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner

/**
 * Runs the stream while it is visible. Something drawn over it, like a system dialog,
 * only pauses the activity and must not end the stream.
 */
class StreamVisibilityObserver(
	private val start: () -> Unit,
	private val stop: () -> Unit): DefaultLifecycleObserver
{
	override fun onStart(owner: LifecycleOwner) = start()
	override fun onStop(owner: LifecycleOwner) = stop()
}
