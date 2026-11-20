// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.settings

import androidx.lifecycle.ViewModel
import androidx.lifecycle.asLiveData
import androidx.lifecycle.viewModelScope
import com.metallic.chiaki.common.AppDatabase
import com.metallic.chiaki.common.RegisteredHost
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

class SettingsRegisteredHostsViewModel(val database: AppDatabase): ViewModel()
{
	val registeredHosts by lazy {
		database.registeredHostDao().getAll().asLiveData()
	}

	fun deleteHost(host: RegisteredHost)
	{
		viewModelScope.launch(Dispatchers.IO) {
			try {
				database.registeredHostDao().delete(host)
			} catch(_: Exception) {}
		}
	}
}
