// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session

import com.metallic.chiaki.lib.QuitReason
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class RemoteInUseRetryTest
{
	private var nowMs = 1000L
	private val retry = RemoteInUseRetry(windowMs = 20_000L, nowMs = { nowMs })

	@Test
	fun firstConnect_doesNotRetry()
	{
		// someone else streaming from the console must be reported right away
		assertFalse(retry.shouldRetry(QuitReason.SESSION_REQUEST_RP_IN_USE))
	}

	@Test
	fun inUseRightAfterOwnStop_retries()
	{
		retry.sessionStopped()
		nowMs += 500
		assertTrue(retry.shouldRetry(QuitReason.SESSION_REQUEST_RP_IN_USE))
	}

	@Test
	fun inUseAfterWindow_doesNotRetry()
	{
		retry.sessionStopped()
		nowMs += 20_000
		assertFalse(retry.shouldRetry(QuitReason.SESSION_REQUEST_RP_IN_USE))
	}

	@Test
	fun otherReason_doesNotRetry()
	{
		retry.sessionStopped()
		assertFalse(retry.shouldRetry(QuitReason.SESSION_REQUEST_RP_IN_USE + 1))
	}
}
