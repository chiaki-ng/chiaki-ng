// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session.output

/**
 * Latest-wins slot for output reports, sent at most every minIntervalNs.
 * A worker thread loops on step(); offer() may be called from any thread.
 */
class ReportCoalescer(
	private val minIntervalNs: Long,
	private val clock: () -> Long,
	private val sink: (ByteArray) -> Unit)
{
	private val lock = Object()
	private var pending: ByteArray? = null
	private var lastSendNs = Long.MIN_VALUE / 2
	private var running = true

	fun offer(report: ByteArray) = synchronized(lock) {
		pending = report.copyOf()
		lock.notifyAll()
	}

	fun stop() = synchronized(lock) {
		running = false
		lock.notifyAll()
	}

	/** One scheduling round. Returns false once stopped. */
	fun step(): Boolean
	{
		val toSend: ByteArray
		synchronized(lock) {
			while(running && pending == null)
				lock.wait()
			if(!running)
				return false
			val wait = lastSendNs + minIntervalNs - clock()
			if(wait > 0)
			{
				lock.wait(wait / 1_000_000, (wait % 1_000_000).toInt())
				return true // re-evaluate; newer reports may have arrived
			}
			toSend = pending!!
			pending = null
			lastSendNs = clock()
		}
		sink(toSend)
		return true
	}
}
