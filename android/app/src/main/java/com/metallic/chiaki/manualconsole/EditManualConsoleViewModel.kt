// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.manualconsole

import android.util.Log
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.ViewModel
import androidx.lifecycle.asLiveData
import androidx.lifecycle.viewModelScope
import com.metallic.chiaki.common.AppDatabase
import com.metallic.chiaki.common.ManualHost
import com.metallic.chiaki.common.RegisteredHost
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class EditManualConsoleViewModel(val database: AppDatabase, manualHostId: Long?): ViewModel()
{
	val registeredHosts by lazy {
		database.registeredHostDao().getAll()
			.onEach { hosts ->
				val selectedHost = selectedRegisteredHost.value
				if(selectedHost != null)
					selectedRegisteredHost.value = hosts.firstOrNull { it.id == selectedHost.id }
			}
			.map { listOf(null) + it }
			.asLiveData()
	}

	val existingHost: LiveData<ManualHost>? =
		if(manualHostId != null)
		{
			val liveData = MutableLiveData<ManualHost>()
			viewModelScope.launch(Dispatchers.IO) {
				try {
					val result = database.manualHostDao().getByIdWithRegisteredHost(manualHostId)
					withContext(Dispatchers.Main) {
						selectedRegisteredHost.value = result.registeredHost
						liveData.value = result.manualHost
					}
				} catch(e: Exception) {
					Log.e("EditManualConsole", "Failed to fetch existing manual host", e)
				}
			}
			liveData
		}
		else
			null

	var selectedRegisteredHost = MutableLiveData<RegisteredHost?>(null)

	fun saveHost(host: String)
	{
		viewModelScope.launch(Dispatchers.IO) {
			val registeredHost = selectedRegisteredHost.value?.id
			val existingHost = existingHost?.value
			if(existingHost != null)
				database.manualHostDao().update(ManualHost(id = existingHost.id, host = host, registeredHost = registeredHost))
			else
				database.manualHostDao().insert(ManualHost(host = host, registeredHost = registeredHost))
		}
	}
}
