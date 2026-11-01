// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session.output

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test

class DualSenseReportTest
{
	private fun hex(s: String) = s.chunked(2).map { it.toInt(16).toByte() }.toByteArray()

	@Test
	fun defaultState()
	{
		val expected = hex("3100100e540000000000000000000000000000000000000000000000000000000000000000000000000400000000040000000000000000000000000000000000000000000000000000003ebfe835")
		assertArrayEquals(expected, DualSenseReport.build(DualSenseOutputState(), seq = 0))
	}

	@Test
	fun fullState()
	{
		val state = DualSenseOutputState()
		state.rumbleLeft = 0x80U
		state.rumbleRight = 0x40U
		state.setTriggers(
			0x26U, ByteArray(10) { (11 + it).toByte() },
			0x21U, ByteArray(10) { (1 + it).toByte() })
		state.ledR = 0x10U
		state.ledG = 0x20U
		state.ledB = 0x30U
		state.playerLeds = DualSenseReport.playerLedsFor(1)
		state.intensity = 0x62U
		val expected = hex("3150100e544080000000000000210102030405060708090a260b0c0d0e0f101112131400000000620004000000000a10203000000000000000000000000000000000000000000000000045deef30")
		assertArrayEquals(expected, DualSenseReport.build(state, seq = 5))
	}

	@Test
	fun releaseState()
	{
		val state = DualSenseOutputState()
		state.rumbleLeft = 0xffU
		state.setTriggers(0x21U, ByteArray(10) { 7 }, 0x21U, ByteArray(10) { 7 })
		state.setRelease()
		val expected = hex("3100100e540000000000000000050000000000000000000005000000000000000000000000000000000400000000040000000000000000000000000000000000000000000000000000004ab4079d")
		assertArrayEquals(expected, DualSenseReport.build(state, seq = 0))
	}

	@Test
	fun defaultState_showsPlayerOne()
	{
		// the console only sends PLAYER_INDEX when it changes from the initial index 0
		assertEquals(0x04, DualSenseReport.build(DualSenseOutputState(), seq = 0)[46].toInt())
	}

	@Test
	fun seqWrapsAtSixteen()
	{
		assertEquals(0x10.toByte(), DualSenseReport.build(DualSenseOutputState(), seq = 17)[1])
	}

	@Test
	fun playerLedsMatchKernelTable()
	{
		assertEquals(listOf(0x04, 0x0A, 0x15, 0x1B, 0x1F, 0x04), (0..5).map { DualSenseReport.playerLedsFor(it) })
		assertEquals(0x04, DualSenseReport.playerLedsFor(-1))
	}
}
