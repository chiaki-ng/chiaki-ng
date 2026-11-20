// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.common

import com.squareup.moshi.JsonDataException
import com.squareup.moshi.Moshi
import org.junit.Assert.*
import org.junit.Test

class MacAddressJsonTest
{
	private val moshi = Moshi.Builder()
		.add(MacAddressJsonAdapter())
		.build()

	private val adapter = moshi.adapter(MacAddress::class.java)

	@Test
	fun toJson_producesString()
	{
		val mac = MacAddress("aa:bb:cc:dd:ee:ff")
		val json = adapter.toJson(mac)
		assertEquals("\"aa:bb:cc:dd:ee:ff\"", json)
	}

	@Test
	fun fromJson_roundTrips()
	{
		val json = "\"11:22:33:44:55:66\""
		val mac = adapter.fromJson(json)
		assertNotNull(mac)
		assertEquals("11:22:33:44:55:66", mac!!.toString())
	}

	@Test(expected = JsonDataException::class)
	fun fromJson_invalidMac_throws()
	{
		adapter.fromJson("\"not-a-mac\"")
	}
}
