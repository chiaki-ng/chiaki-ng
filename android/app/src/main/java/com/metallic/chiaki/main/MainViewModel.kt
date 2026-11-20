// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.main

import androidx.lifecycle.ViewModel
import androidx.lifecycle.asLiveData
import androidx.lifecycle.viewModelScope
import com.metallic.chiaki.common.*
import com.metallic.chiaki.discovery.DiscoveryManager
import com.metallic.chiaki.discovery.serverMac
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.launch

class MainViewModel(val database: AppDatabase, val preferences: Preferences): ViewModel()
{
	val discoveryManager = DiscoveryManager().also {
		it.active = preferences.discoveryEnabled
		viewModelScope.launch {
			it.discoveryActive.collect { active ->
				preferences.discoveryEnabled = active
			}
		}
	}

	val displayHosts by lazy {
		combine(
			database.manualHostDao().getAll(),
			database.registeredHostDao().getAll(),
			discoveryManager.discoveredHosts
		) { manualHosts, registeredHosts, discoveredHosts ->
			val macRegisteredHosts = registeredHosts.associateBy { it.serverMac }
			val idRegisteredHosts = registeredHosts.associateBy { it.id }
			discoveredHosts.map {
				DiscoveredDisplayHost(it.serverMac?.let { mac -> macRegisteredHosts[mac] }, it)
			} +
			manualHosts.map {
				ManualDisplayHost(it.registeredHost?.let { id -> idRegisteredHosts[id] }, it)
			}
		}.asLiveData()
	}

	val discoveryActive by lazy {
		discoveryManager.discoveryActive.asLiveData()
	}

	fun deleteManualHost(manualHost: ManualHost)
	{
		viewModelScope.launch(Dispatchers.IO) {
			try {
				database.manualHostDao().delete(manualHost)
			} catch(_: Exception) {}
		}
	}

	override fun onCleared()
	{
		super.onCleared()
		discoveryManager.dispose()
	}
}
