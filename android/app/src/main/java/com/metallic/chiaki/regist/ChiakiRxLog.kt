// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.regist

import com.metallic.chiaki.lib.ChiakiLog
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

class ChiakiRxLog(levelMask: Int)
{
	private val _logText = MutableStateFlow("")
	private val accMutex = ReentrantLock()
	val logText: StateFlow<String> get() = _logText.asStateFlow()

	val log = ChiakiLog(levelMask, callback = { level, text ->
		accMutex.withLock {
			val cur = _logText.value
			_logText.value = cur + (if(cur.isEmpty()) "" else "\n") + ChiakiLog.formatLog(level, text)
		}
	})
}
