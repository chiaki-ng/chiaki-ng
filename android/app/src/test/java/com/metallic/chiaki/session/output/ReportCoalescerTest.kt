// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session.output

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ReportCoalescerTest
{
	private var now = 0L
	private val sent = mutableListOf<Byte>()
	private val coalescer = ReportCoalescer(4_000_000L, { now }) { sent.add(it[0]) }

	@Test
	fun latestOfferWins()
	{
		coalescer.offer(byteArrayOf(1))
		coalescer.offer(byteArrayOf(2))
		assertTrue(coalescer.step())
		assertEquals(listOf<Byte>(2), sent)
	}

	@Test
	fun respectsMinimumInterval()
	{
		coalescer.offer(byteArrayOf(1))
		coalescer.step()
		now = 1_000_000L
		coalescer.offer(byteArrayOf(2))
		assertTrue(coalescer.step()) // too early: waits (≤3 ms real time) and returns without sending
		assertEquals(listOf<Byte>(1), sent)
		now = 5_000_000L
		assertTrue(coalescer.step())
		assertEquals(listOf<Byte>(1, 2), sent)
	}

	@Test
	fun stop_endsStepping()
	{
		coalescer.stop()
		assertFalse(coalescer.step())
	}

	@Test
	fun offerCopiesTheArray()
	{
		val report = byteArrayOf(1)
		coalescer.offer(report)
		report[0] = 9
		coalescer.step()
		assertEquals(listOf<Byte>(1), sent)
	}
}
