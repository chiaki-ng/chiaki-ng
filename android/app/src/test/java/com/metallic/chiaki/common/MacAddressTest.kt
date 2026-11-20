// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.common

import org.junit.Assert.*
import org.junit.Test

class MacAddressTest
{
	@Test
	fun parseAndToString_roundTrips()
	{
		val input = "aa:bb:cc:dd:ee:ff"
		val mac = MacAddress(input)
		assertEquals(input, mac.toString())
	}

	@Test
	fun parseUpperCase_normalizes()
	{
		val mac = MacAddress("AA:BB:CC:DD:EE:FF")
		assertEquals("aa:bb:cc:dd:ee:ff", mac.toString())
	}

	@Test
	fun parseDashSeparator_works()
	{
		val mac = MacAddress("11-22-33-44-55-66")
		assertEquals("11:22:33:44:55:66", mac.toString())
	}

	@Test
	fun fromByteArray_roundTrips()
	{
		val bytes = byteArrayOf(0x01, 0x02, 0x03, 0x04, 0x05, 0x06)
		val mac = MacAddress(bytes)
		assertEquals("01:02:03:04:05:06", mac.toString())
	}

	@Test(expected = IllegalArgumentException::class)
	fun parseInvalidLength_throws()
	{
		MacAddress("aa:bb:cc")
	}

	@Test(expected = IllegalArgumentException::class)
	fun parseGarbage_throws()
	{
		MacAddress("not-a-mac-address")
	}

	@Test(expected = IllegalArgumentException::class)
	fun fromByteArray_wrongLength_throws()
	{
		MacAddress(byteArrayOf(0x01, 0x02, 0x03))
	}

	@Test
	fun equality_sameValue()
	{
		val a = MacAddress("aa:bb:cc:dd:ee:ff")
		val b = MacAddress("aa:bb:cc:dd:ee:ff")
		assertEquals(a, b)
		assertEquals(a.hashCode(), b.hashCode())
	}

	@Test
	fun equality_differentValue()
	{
		val a = MacAddress("aa:bb:cc:dd:ee:ff")
		val b = MacAddress("11:22:33:44:55:66")
		assertNotEquals(a, b)
	}
}
