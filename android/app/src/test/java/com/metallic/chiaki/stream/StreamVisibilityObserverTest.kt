// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.stream

import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class StreamVisibilityObserverTest
{
	private class Owner: LifecycleOwner
	{
		val registry = LifecycleRegistry.createUnsafe(this)
		override val lifecycle: Lifecycle get() = registry
	}

	private var running = false
	private var starts = 0
	private val owner = Owner().also {
		it.lifecycle.addObserver(StreamVisibilityObserver(
			start = { running = true; starts++ },
			stop = { running = false }))
	}

	@Test
	fun coveredByDialog_keepsStreaming()
	{
		owner.registry.currentState = Lifecycle.State.RESUMED
		// a system dialog over the stream only pauses the activity
		owner.registry.currentState = Lifecycle.State.STARTED
		assertTrue(running)
		owner.registry.currentState = Lifecycle.State.RESUMED
		assertEquals(1, starts)
	}

	@Test
	fun hidden_stopsStreaming()
	{
		owner.registry.currentState = Lifecycle.State.RESUMED
		owner.registry.currentState = Lifecycle.State.CREATED
		assertFalse(running)
	}

	@Test
	fun shownAgain_startsStreaming()
	{
		owner.registry.currentState = Lifecycle.State.RESUMED
		owner.registry.currentState = Lifecycle.State.CREATED
		owner.registry.currentState = Lifecycle.State.RESUMED
		assertTrue(running)
		assertEquals(2, starts)
	}
}
