// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.settings

import androidx.lifecycle.ViewModel
import androidx.lifecycle.asLiveData
import com.metallic.chiaki.common.AppDatabase
import com.metallic.chiaki.common.Preferences

class SettingsViewModel(val database: AppDatabase, val preferences: Preferences): ViewModel()
{
	val registeredHostsCount by lazy {
		database.registeredHostDao().count().asLiveData()
	}

	val bitrateAuto by lazy {
		preferences.bitrateAutoFlow.asLiveData()
	}
}
