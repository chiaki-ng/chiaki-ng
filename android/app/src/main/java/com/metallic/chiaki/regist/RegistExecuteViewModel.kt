// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.regist

import android.util.Log
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.ViewModel
import androidx.lifecycle.asLiveData
import androidx.lifecycle.viewModelScope
import com.metallic.chiaki.common.AppDatabase
import com.metallic.chiaki.common.MacAddress
import com.metallic.chiaki.common.RegisteredHost
import com.metallic.chiaki.lib.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class RegistExecuteViewModel(val database: AppDatabase): ViewModel()
{
	enum class State
	{
		IDLE,
		RUNNING,
		STOPPED,
		FAILED,
		SUCCESSFUL,
		SUCCESSFUL_DUPLICATE,
	}

	private val _state = MutableLiveData<State>(State.IDLE)
	val state: LiveData<State> get() = _state

	private val log = ChiakiRxLog(ChiakiLog.Level.ALL.value/* and ChiakiLog.Level.VERBOSE.value.inv()*/)
	private var regist: Regist? = null

	val logText: LiveData<String> = log.logText.asLiveData()

	var host: RegistHost? = null
		private set

	private var assignManualHostId: Long? = null

	fun start(info: RegistInfo, assignManualHostId: Long?)
	{
		if(regist != null)
			return
		try
		{
			regist = Regist(info, log.log, this::registEvent)
			this.assignManualHostId = assignManualHostId
			_state.value = State.RUNNING
		}
		catch(error: CreateError)
		{
			log.log.e("Failed to create Regist: ${error.errorCode}")
			_state.value = State.FAILED
		}
	}

	fun stop()
	{
		regist?.stop()
	}

	private fun registEvent(event: RegistEvent)
	{
		when(event)
		{
			is RegistEventCanceled -> _state.postValue(State.STOPPED)
			is RegistEventFailed -> _state.postValue(State.FAILED)
			is RegistEventSuccess -> registSuccess(event.host)
		}
	}

	private fun registSuccess(host: RegistHost)
	{
		this.host = host
		viewModelScope.launch {
			val existing = withContext(Dispatchers.IO) {
				database.registeredHostDao().getByMac(MacAddress(host.serverMac))
			}
			if(existing != null)
			{
				_state.value = State.SUCCESSFUL_DUPLICATE
			}
			else
			{
				saveHost()
			}
		}
	}

	fun saveHost()
	{
		val host = host ?: return
		val assignManualHostId = assignManualHostId
		viewModelScope.launch(Dispatchers.IO) {
			val dao = database.registeredHostDao()
			val manualHostDao = database.manualHostDao()
			val registeredHost = RegisteredHost(host)
			dao.deleteByMac(registeredHost.serverMac)
			val registeredHostId = dao.insert(registeredHost)
			if(assignManualHostId != null)
			{
				manualHostDao.assignRegisteredHost(assignManualHostId, registeredHostId)
			}
			withContext(Dispatchers.Main) {
				Log.i("RegistExecute", "Registered Host saved in db")
				_state.value = State.SUCCESSFUL
			}
		}
	}

	override fun onCleared()
	{
		super.onCleared()
		regist?.dispose()
	}
}
