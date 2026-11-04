// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session.output

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread

class FakeTransport(var succeed: Boolean = true): RawReportTransport
{
	val reports = mutableListOf<ByteArray>()
	var closed = false
	override fun send(report: ByteArray): Boolean { reports.add(report); return succeed }
	override fun close() { closed = true }
}

class DualSenseBtOutputTest
{
	private val transport = FakeTransport()
	private var unavailable = 0
	private val output = DualSenseBtOutput(transport, { unavailable++ }, coalesce = false)

	private fun last() = transport.reports.last()
	private fun u(b: Byte) = b.toInt() and 0xff

	@Test
	fun rumble_writesMotorBytes()
	{
		output.rumble(0x80U, 0x40U)
		assertEquals(0x40, u(last()[5])) // high-frequency (right) motor
		assertEquals(0x80, u(last()[6])) // low-frequency (left) motor
	}

	@Test
	fun triggerEffects_writesBothBlocks()
	{
		output.triggerEffects(0x26U, ByteArray(10) { 1 }, 0x21U, ByteArray(10) { 2 })
		assertEquals(0x21, u(last()[13])) // right trigger type at common+10
		assertEquals(2, u(last()[14]))
		assertEquals(0x26, u(last()[24])) // left trigger type at common+21
		assertEquals(1, u(last()[25]))
	}

	@Test
	fun stateAccumulatesAcrossCalls()
	{
		output.led(1U, 2U, 3U)
		output.playerIndex(1)
		output.intensity(0x62U)
		output.rumble(9U, 9U)
		val r = last()
		assertEquals(listOf(1, 2, 3), listOf(u(r[47]), u(r[48]), u(r[49])))
		assertEquals(0x0A, u(r[46]))
		assertEquals(0x62, u(r[39]))
		assertEquals(9, u(r[5]))
	}

	@Test
	fun sequenceIncrements()
	{
		output.rumble(1U, 1U)
		output.rumble(2U, 2U)
		assertEquals(0x00, u(transport.reports[0][1]))
		assertEquals(0x10, u(transport.reports[1][1]))
	}

	@Test
	fun fiveConsecutiveFailures_reportUnavailableOnce()
	{
		transport.succeed = false
		repeat(7) { output.rumble(it.toUByte(), 0U) }
		assertEquals(1, unavailable)
	}

	@Test
	fun successResetsFailureCount()
	{
		transport.succeed = false
		repeat(4) { output.rumble(1U, 0U) }
		transport.succeed = true
		output.rumble(2U, 0U)
		transport.succeed = false
		repeat(4) { output.rumble(3U, 0U) }
		assertEquals(0, unavailable)
	}

	@Test
	fun close_sendsClearReportSynchronously()
	{
		output.triggerEffects(0x21U, ByteArray(10) { 7 }, 0x21U, ByteArray(10) { 7 })
		output.rumble(200U, 200U)
		output.close()
		val r = last()
		assertEquals(0x05, u(r[13]))
		assertEquals(0x05, u(r[24]))
		assertEquals(0, u(r[14]))
		assertEquals(0, u(r[5]))
		assertEquals(0, u(r[6]))
		assertTrue(transport.closed)
	}

	@Test
	fun callsAfterClose_ignored()
	{
		output.close()
		val count = transport.reports.size
		output.rumble(1U, 1U)
		assertEquals(count, transport.reports.size)
	}

	@Test
	fun concurrentUpdates_reachTransportInBuildOrder()
	{
		// the first send (rumble, old triggers) stalls in the transport while another
		// thread sets new triggers; the stale report must not be delivered last
		val firstInSend = CountDownLatch(1)
		val releaseFirst = CountDownLatch(1)
		val delivered = mutableListOf<ByteArray>()
		val slow = object: RawReportTransport {
			private var first = true
			override fun send(report: ByteArray): Boolean
			{
				val isFirst = synchronized(this) { first.also { first = false } }
				if(isFirst)
				{
					firstInSend.countDown()
					releaseFirst.await(2, TimeUnit.SECONDS)
				}
				synchronized(delivered) { delivered.add(report) }
				return true
			}
			override fun close() {}
		}
		val out = DualSenseBtOutput(slow, {}, coalesce = false)
		val t1 = thread { out.rumble(1U, 1U) }
		firstInSend.await(2, TimeUnit.SECONDS)
		val t2 = thread { out.triggerEffects(0x21U, ByteArray(10) { 3 }, 0x26U, ByteArray(10) { 4 }) }
		t2.join(200)
		releaseFirst.countDown()
		t1.join()
		t2.join()
		assertEquals(0x26, u(delivered.last()[13])) // right trigger type
		assertEquals(0x21, u(delivered.last()[24])) // left trigger type
	}

	@Test
	fun sendException_countsAsFailure()
	{
		val throwing = object: RawReportTransport {
			override fun send(report: ByteArray): Boolean = throw IllegalStateException("gone")
			override fun close() {}
		}
		val out = DualSenseBtOutput(throwing, { unavailable++ }, coalesce = false)
		repeat(5) { out.rumble(1U, 1U) }
		assertEquals(1, unavailable)
	}
}
