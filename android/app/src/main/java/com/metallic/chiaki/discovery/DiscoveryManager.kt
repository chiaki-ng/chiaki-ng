// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.discovery

import android.util.Log
import com.metallic.chiaki.common.MacAddress
import com.metallic.chiaki.common.ext.hexToByteArray
import com.metallic.chiaki.lib.CreateError
import com.metallic.chiaki.lib.DiscoveryHost
import com.metallic.chiaki.lib.DiscoveryService
import com.metallic.chiaki.lib.DiscoveryServiceOptions
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.net.InetSocketAddress
import java.nio.charset.StandardCharsets

val DiscoveryHost.serverMac get() = this.hostId?.hexToByteArray()?.let {
	if(it.size == MacAddress.LENGTH)
		MacAddress(it)
	else
		null
}

class DiscoveryManager
{
	companion object
	{
		const val HOSTS_MAX: ULong = 16U
		const val DROP_PINGS: ULong = 3U
		const val PING_MS: ULong = 500U
		const val PORT = 987
	}

	private var discoveryService: DiscoveryService? = null

	private val _discoveryActive = MutableStateFlow(false)
	val discoveryActive: StateFlow<Boolean> get() = _discoveryActive.asStateFlow()
	var active = false
		set(value)
		{
			field = value
			_discoveryActive.value = value
			updateService()
		}
	private var paused = false

	private val _discoveredHosts = MutableStateFlow<List<DiscoveryHost>>(listOf())
	val discoveredHosts: StateFlow<List<DiscoveryHost>> get() = _discoveredHosts.asStateFlow()

	fun resume()
	{
		paused = false
		updateService()
	}

	fun pause()
	{
		paused = true
		updateService()
	}

	fun dispose()
	{
		active = false
	}

	fun sendWakeup(host: String, registKey: ByteArray, ps5: Boolean)
	{
		val registKeyString = registKey.indexOfFirst { it == 0.toByte() }.let { end -> registKey.copyOfRange(0, if(end >= 0) end else registKey.size) }.toString(StandardCharsets.UTF_8)
		val credential = try { registKeyString.toULong(16) } catch(e: NumberFormatException) {
			Log.e("DiscoveryManager", "Failed to convert registKey to int", e)
			return
		}
		DiscoveryService.wakeup(discoveryService, host, credential, ps5)
	}

	private fun updateService()
	{
		if(active && !paused && discoveryService == null)
		{
			_discoveredHosts.value = listOf()
			try
			{
				discoveryService = DiscoveryService(DiscoveryServiceOptions(
					HOSTS_MAX, DROP_PINGS, PING_MS, InetSocketAddress("255.255.255.255", PORT)
				)) { hosts -> _discoveredHosts.value = hosts }
			}
			catch(e: CreateError)
			{
				Log.e("DiscoveryManager", "Failed to start Discovery Service: $e")
			}
		}
		else if((!active || paused) && discoveryService != null)
		{
			val service = discoveryService ?: return
			service.dispose()
			discoveryService = null
			if(!active)
				_discoveredHosts.value = listOf()
		}
	}
}
