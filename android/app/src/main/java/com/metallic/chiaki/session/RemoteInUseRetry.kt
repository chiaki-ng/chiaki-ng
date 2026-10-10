// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session

import com.metallic.chiaki.lib.QuitReason

/**
 * Right after a session stopped, the console may still report Remote Play as in use: for a few
 * seconds, or until its timeout if the network was cut (e.g. by Battery Saver) before the
 * disconnect got out. Decides whether a session that quit like that is started again.
 */
class RemoteInUseRetry(
	private val windowMs: Long = 20_000L,
	private val nowMs: () -> Long = { System.nanoTime() / 1_000_000 })
{
	@Volatile private var stoppedAtMs: Long? = null

	fun sessionStopped()
	{
		stoppedAtMs = nowMs()
	}

	fun shouldRetry(quitReason: Int): Boolean
	{
		val stoppedAtMs = stoppedAtMs ?: return false
		return quitReason == QuitReason.SESSION_REQUEST_RP_IN_USE && nowMs() - stoppedAtMs < windowMs
	}
}
