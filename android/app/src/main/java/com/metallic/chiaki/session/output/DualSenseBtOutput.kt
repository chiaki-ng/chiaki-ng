// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session.output

import kotlin.concurrent.thread

interface RawReportTransport
{
	/** Send one output report; false or an exception means it was not delivered. */
	fun send(report: ByteArray): Boolean
	fun close()
}

/**
 * Drives a DualSense over Bluetooth with full output reports:
 * adaptive triggers, rumble, lightbar, player LEDs, intensity.
 */
class DualSenseBtOutput(
	private val transport: RawReportTransport,
	private val onUnavailable: () -> Unit,
	coalesce: Boolean = true): ControllerOutput
{
	companion object
	{
		const val MAX_FAILURES = 5
		private const val MIN_INTERVAL_NS = 4_000_000L
	}

	override val appliesIntensity get() = true

	/** Last send exception, for diagnostics (no android.util.Log here: the class is JVM-tested). */
	@Volatile var lastError: Exception? = null
		private set

	private val lock = Object()
	private val state = DualSenseOutputState()
	private var seq = 0
	private var closed = false
	private var failures = 0
	private var reportedUnavailable = false

	private val coalescer = if(coalesce) ReportCoalescer(MIN_INTERVAL_NS, System::nanoTime) { sendNow(it) } else null
	private val worker = coalescer?.let { c -> thread(name = "dualsense-output") { while(c.step()) { } } }

	override fun rumble(left: UByte, right: UByte) = update {
		state.rumbleLeft = left
		state.rumbleRight = right
	}

	override fun led(r: UByte, g: UByte, b: UByte) = update {
		state.ledR = r
		state.ledG = g
		state.ledB = b
	}

	override fun playerIndex(index: Int) = update { state.playerLeds = DualSenseReport.playerLedsFor(index) }

	override fun triggerEffects(typeLeft: UByte, left: ByteArray, typeRight: UByte, right: ByteArray) =
		update { state.setTriggers(typeLeft, left, typeRight, right) }

	override fun intensity(value: UByte) = update { state.intensity = value }

	override fun close()
	{
		val report: ByteArray
		synchronized(lock) {
			if(closed)
				return
			closed = true
			state.setRelease()
			report = nextReport()
		}
		coalescer?.stop()
		worker?.join(100)
		// bypass the coalescer: this report must not be dropped
		sendNow(report)
		transport.close()
	}

	private fun update(change: () -> Unit)
	{
		synchronized(lock) {
			if(closed)
				return
			change()
			// hand over under the lock, so reports leave in the order they were built
			val report = nextReport()
			if(coalescer != null)
				coalescer.offer(report)
			else
				sendNow(report)
		}
	}

	private fun nextReport(): ByteArray
	{
		val report = DualSenseReport.build(state, seq)
		seq = (seq + 1) and 0x0f
		return report
	}

	private fun sendNow(report: ByteArray)
	{
		val ok = try
		{
			transport.send(report)
		}
		catch(e: Exception)
		{
			lastError = e
			false
		}
		val notify: Boolean
		synchronized(lock) {
			if(ok)
			{
				failures = 0
				return
			}
			failures++
			notify = failures >= MAX_FAILURES && !reportedUnavailable
			if(notify)
				reportedUnavailable = true
		}
		if(notify)
			onUnavailable()
	}
}
