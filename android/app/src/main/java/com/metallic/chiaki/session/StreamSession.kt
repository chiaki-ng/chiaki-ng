// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session

import android.graphics.SurfaceTexture
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.*
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import com.metallic.chiaki.common.LogManager
import com.metallic.chiaki.common.Preferences
import com.metallic.chiaki.lib.*
import com.metallic.chiaki.session.output.OutputBackends
import com.metallic.chiaki.session.output.OutputRouter

sealed class StreamState
object StreamStateIdle: StreamState()
object StreamStateConnecting: StreamState()
object StreamStateConnected: StreamState()
data class StreamStateCreateError(val error: CreateError): StreamState()
data class StreamStateQuit(val reason: QuitReason, val reasonString: String?): StreamState()
data class StreamStateLoginPinRequest(val pinIncorrect: Boolean): StreamState()

class StreamSession(
	val connectInfo: ConnectInfo,
	val logManager: LogManager,
	val logVerbose: Boolean,
	val input: StreamInput,
	private val rumbleEnabled: Boolean,
	private val hapticsRumbleLevel: Preferences.HapticsRumbleLevel,
	private val backends: OutputBackends)
{
	var session: Session? = null
		private set

	private val _state = MutableLiveData<StreamState>(StreamStateIdle)
	val state: LiveData<StreamState> get() = _state

	@Volatile private var router: OutputRouter? = null
	@Volatile private var hapticsRumble: HapticsRumble? = null
	private val tickHandler = Handler(Looper.getMainLooper())
	private val hapticsTick = object: Runnable {
		override fun run()
		{
			hapticsRumble?.tick()
			tickHandler.postDelayed(this, HapticsRumble.TICK_MS)
		}
	}

	private var surfaceTexture: SurfaceTexture? = null
	private var surface: Surface? = null

	init
	{
		input.controllerStateChangedCallback = {
			session?.setControllerState(it)
		}
		input.activeControllerChangedCallback = {
			router?.setActiveController(it)
		}
	}

	fun shutdown()
	{
		session?.stop()
		session?.dispose()
		session = null
		tickHandler.removeCallbacks(hapticsTick)
		hapticsRumble?.clear()
		hapticsRumble = null
		router?.close()
		router = null
		_state.value = StreamStateIdle
		//surfaceTexture?.release()
	}

	fun pause()
	{
		shutdown()
	}

	fun resume()
	{
		if(session != null)
			return
		try
		{
			val router = OutputRouter(rumbleEnabled) { backends.create(it) }
			router.setActiveController(input.activeControllerId)
			this.router = router
			hapticsRumble = HapticsRumble(hapticsRumbleLevel) { l, r -> router.onHapticRumble(l, r) }
			tickHandler.post(hapticsTick)
			val session = Session(connectInfo, logManager.createNewFile().file.absolutePath, logVerbose)
			_state.value = StreamStateConnecting
			session.eventCallback = this::eventCallback
			session.start()
			val surface = surface
			if(surface != null)
				session.setSurface(surface)
			this.session = session
		}
		catch(e: CreateError)
		{
			_state.value = StreamStateCreateError(e)
		}
	}

	private fun eventCallback(event: Event)
	{
		when(event)
		{
			is ConnectedEvent -> _state.postValue(StreamStateConnected)
			is QuitEvent -> _state.postValue(
				StreamStateQuit(
					event.reason,
					event.reasonString
				)
			)
			is LoginPinRequestEvent -> _state.postValue(
				StreamStateLoginPinRequest(
					event.pinIncorrect
				)
			)
			is RumbleEvent -> router?.onRumble(event.left, event.right)
			is MotionResetEvent -> input.onMotionReset()
			is TriggerEffectsEvent -> router?.onTriggerEffects(event.typeLeft, event.left, event.typeRight, event.right)
			is LedColorEvent -> router?.onLedColor(event.r, event.g, event.b)
			is PlayerIndexEvent -> router?.onPlayerIndex(event.index)
			is HapticIntensityEvent -> router?.onHapticIntensity(event.intensity)
			is TriggerIntensityEvent -> router?.onTriggerIntensity(event.intensity)
			is HapticStrengthEvent -> hapticsRumble?.push(event.left, event.right)
		}
	}

	fun attachToSurfaceView(surfaceView: SurfaceView)
	{
		surfaceView.holder.addCallback(object: SurfaceHolder.Callback {
			override fun surfaceCreated(holder: SurfaceHolder) { }

			override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int)
			{
				val surface = holder.surface
				this@StreamSession.surface = surface
				session?.setSurface(surface)
			}

			override fun surfaceDestroyed(holder: SurfaceHolder)
			{
				this@StreamSession.surface = null
				session?.setSurface(null)
			}
		})
	}

	fun attachToTextureView(textureView: TextureView)
	{
		textureView.surfaceTextureListener = object: TextureView.SurfaceTextureListener {
			override fun onSurfaceTextureAvailable(surface: SurfaceTexture, width: Int, height: Int)
			{
				if(surfaceTexture != null)
					return
				surfaceTexture = surface
				this@StreamSession.surface = Surface(surfaceTexture)
				session?.setSurface(Surface(surface))
			}

			override fun onSurfaceTextureDestroyed(surface: SurfaceTexture): Boolean
			{
				// return false if we want to keep the surface texture
				return surfaceTexture == null
			}

			override fun onSurfaceTextureSizeChanged(surface: SurfaceTexture, width: Int, height: Int) { }
			override fun onSurfaceTextureUpdated(surface: SurfaceTexture) {}
		}

		val surfaceTexture = surfaceTexture
		if(surfaceTexture != null)
			textureView.setSurfaceTexture(surfaceTexture)
	}

	fun setLoginPin(pin: String)
	{
		session?.setLoginPin(pin)
	}
}