// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.common

import android.content.Context
import android.content.SharedPreferences
import androidx.annotation.StringRes
import androidx.preference.PreferenceManager
import com.metallic.chiaki.R
import com.metallic.chiaki.lib.Codec
import com.metallic.chiaki.lib.ConnectVideoProfile
import com.metallic.chiaki.lib.VideoFPSPreset
import com.metallic.chiaki.lib.VideoResolutionPreset
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlin.math.max
import kotlin.math.min

class Preferences(context: Context)
{
	enum class Resolution(val value: String, @StringRes val title: Int, val preset: VideoResolutionPreset)
	{
		RES_360P("360p", R.string.preferences_resolution_title_360p, VideoResolutionPreset.RES_360P),
		RES_540P("540p", R.string.preferences_resolution_title_540p, VideoResolutionPreset.RES_540P),
		RES_720P("720p", R.string.preferences_resolution_title_720p, VideoResolutionPreset.RES_720P),
		RES_1080P("1080p", R.string.preferences_resolution_title_1080p, VideoResolutionPreset.RES_1080P),
	}

	enum class FPS(val value: String, @StringRes val title: Int, val preset: VideoFPSPreset)
	{
		FPS_30("30", R.string.preferences_fps_title_30, VideoFPSPreset.FPS_30),
		FPS_60("60", R.string.preferences_fps_title_60, VideoFPSPreset.FPS_60)
	}

	enum class Codec(val value: String, @StringRes val title: Int, val codec: com.metallic.chiaki.lib.Codec)
	{
		CODEC_H264("h264", R.string.preferences_codec_title_h264, com.metallic.chiaki.lib.Codec.CODEC_H264),
		CODEC_H265("h265", R.string.preferences_codec_title_h265, com.metallic.chiaki.lib.Codec.CODEC_H265)
	}

	enum class MotionSource(val value: String, @StringRes val title: Int)
	{
		AUTO("auto", R.string.preferences_motion_source_title_auto),
		CONTROLLER("controller", R.string.preferences_motion_source_title_controller),
		PHONE("phone", R.string.preferences_motion_source_title_phone),
		OFF("off", R.string.preferences_motion_source_title_off)
	}

	enum class DualSenseMode(val value: String, @StringRes val title: Int)
	{
		AUTO("auto", R.string.preferences_dualsense_mode_title_auto),
		ON("on", R.string.preferences_dualsense_mode_title_on),
		OFF("off", R.string.preferences_dualsense_mode_title_off)
	}

	enum class HapticsRumbleLevel(val value: String, @StringRes val title: Int)
	{
		OFF("off", R.string.preferences_haptics_rumble_title_off),
		VERY_WEAK("very_weak", R.string.preferences_haptics_rumble_title_very_weak),
		WEAK("weak", R.string.preferences_haptics_rumble_title_weak),
		NORMAL("normal", R.string.preferences_haptics_rumble_title_normal),
		STRONG("strong", R.string.preferences_haptics_rumble_title_strong),
		VERY_STRONG("very_strong", R.string.preferences_haptics_rumble_title_very_strong)
	}

	companion object
	{
		val resolutionDefault = Resolution.RES_720P
		val resolutionAll = Resolution.values()
		val fpsDefault = FPS.FPS_60
		val fpsAll = FPS.values()
		val codecDefault = Codec.CODEC_H265
		val codecAll = Codec.values()
		val motionSourceDefault = MotionSource.AUTO
		val motionSourceAll = MotionSource.values()
		val dualSenseModeDefault = DualSenseMode.AUTO
		val dualSenseModeAll = DualSenseMode.values()
		val hapticsRumbleLevelDefault = HapticsRumbleLevel.NORMAL
		val hapticsRumbleLevelAll = HapticsRumbleLevel.values()

		fun motionSourceFromStored(stored: String?, legacyMotionEnabled: Boolean): MotionSource
		{
			if(stored == null)
				return if(legacyMotionEnabled) MotionSource.AUTO else MotionSource.OFF
			return MotionSource.values().firstOrNull { it.value == stored } ?: motionSourceDefault
		}
	}

	private val sharedPreferences = PreferenceManager.getDefaultSharedPreferences(context)
	private val sharedPreferenceChangeListener = SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
		when(key)
		{
			resolutionKey -> _bitrateAutoFlow.value = bitrateAuto
		}
	}.also { sharedPreferences.registerOnSharedPreferenceChangeListener(it) }

	private val resources = context.resources

	val discoveryEnabledKey get() = resources.getString(R.string.preferences_discovery_enabled_key)
	var discoveryEnabled
		get() = sharedPreferences.getBoolean(discoveryEnabledKey, true)
		set(value) { sharedPreferences.edit().putBoolean(discoveryEnabledKey, value).apply() }

	val onScreenControlsEnabledKey get() = resources.getString(R.string.preferences_on_screen_controls_enabled_key)
	var onScreenControlsEnabled
		get() = sharedPreferences.getBoolean(onScreenControlsEnabledKey, true)
		set(value) { sharedPreferences.edit().putBoolean(onScreenControlsEnabledKey, value).apply() }

	val touchpadOnlyEnabledKey get() = resources.getString(R.string.preferences_touchpad_only_enabled_key)
	var touchpadOnlyEnabled
		get() = sharedPreferences.getBoolean(touchpadOnlyEnabledKey, false)
		set(value) { sharedPreferences.edit().putBoolean(touchpadOnlyEnabledKey, value).apply() }

	val rumbleEnabledKey get() = resources.getString(R.string.preferences_rumble_enabled_key)
	var rumbleEnabled
		get() = sharedPreferences.getBoolean(rumbleEnabledKey, true)
		set(value) { sharedPreferences.edit().putBoolean(rumbleEnabledKey, value).apply() }

	val motionEnabledKey get() = resources.getString(R.string.preferences_motion_enabled_key)

	val motionSourceKey get() = resources.getString(R.string.preferences_motion_source_key)
	var motionSource
		get() = motionSourceFromStored(
			sharedPreferences.getString(motionSourceKey, null),
			sharedPreferences.getBoolean(motionEnabledKey, true))
		set(value) { sharedPreferences.edit().putString(motionSourceKey, value.value).apply() }

	val dualSenseModeKey get() = resources.getString(R.string.preferences_dualsense_mode_key)
	var dualSenseMode
		get() = sharedPreferences.getString(dualSenseModeKey, dualSenseModeDefault.value)?.let { value ->
			DualSenseMode.values().firstOrNull { it.value == value }
		} ?: dualSenseModeDefault
		set(value) { sharedPreferences.edit().putString(dualSenseModeKey, value.value).apply() }

	val adaptiveTriggersKey get() = resources.getString(R.string.preferences_adaptive_triggers_key)
	var adaptiveTriggers
		get() = sharedPreferences.getBoolean(adaptiveTriggersKey, false)
		set(value) { sharedPreferences.edit().putBoolean(adaptiveTriggersKey, value).apply() }

	val hapticsRumbleLevelKey get() = resources.getString(R.string.preferences_haptics_rumble_key)
	var hapticsRumbleLevel
		get() = sharedPreferences.getString(hapticsRumbleLevelKey, hapticsRumbleLevelDefault.value)?.let { value ->
			HapticsRumbleLevel.values().firstOrNull { it.value == value }
		} ?: hapticsRumbleLevelDefault
		set(value) { sharedPreferences.edit().putString(hapticsRumbleLevelKey, value.value).apply() }

	val buttonHapticEnabledKey get() = resources.getString(R.string.preferences_button_haptic_enabled_key)
	var buttonHapticEnabled
		get() = sharedPreferences.getBoolean(buttonHapticEnabledKey, true)
		set(value) { sharedPreferences.edit().putBoolean(buttonHapticEnabledKey, value).apply() }

	val logVerboseKey get() = resources.getString(R.string.preferences_log_verbose_key)
	var logVerbose
		get() = sharedPreferences.getBoolean(logVerboseKey, false)
		set(value) { sharedPreferences.edit().putBoolean(logVerboseKey, value).apply() }

	val swapCrossMoonKey get() = resources.getString(R.string.preferences_swap_cross_moon_key)
	var swapCrossMoon
		get() = sharedPreferences.getBoolean(swapCrossMoonKey, false)
		set(value) { sharedPreferences.edit().putBoolean(swapCrossMoonKey, value).apply() }

	val resolutionKey get() = resources.getString(R.string.preferences_resolution_key)
	var resolution
		get() = sharedPreferences.getString(resolutionKey, resolutionDefault.value)?.let { value ->
			Resolution.values().firstOrNull { it.value == value }
		} ?: resolutionDefault
		set(value) { sharedPreferences.edit().putString(resolutionKey, value.value).apply() }

	val fpsKey get() = resources.getString(R.string.preferences_fps_key)
	var fps
		get() = sharedPreferences.getString(fpsKey, fpsDefault.value)?.let { value ->
			FPS.values().firstOrNull { it.value == value }
		}  ?: fpsDefault
		set(value) { sharedPreferences.edit().putString(fpsKey, value.value).apply() }

	fun validateBitrate(bitrate: Int) = max(2000, min(50000, bitrate))
	val bitrateKey get() = resources.getString(R.string.preferences_bitrate_key)
	var bitrate
		get() = sharedPreferences.getInt(bitrateKey, 0).let { if(it == 0) null else validateBitrate(it) }
		set(value) { sharedPreferences.edit().putInt(bitrateKey, if(value != null) validateBitrate(value) else 0).apply() }
	val bitrateAuto get() = videoProfileDefaultBitrate.bitrate
	private val _bitrateAutoFlow by lazy { MutableStateFlow(bitrateAuto) }
	val bitrateAutoFlow: StateFlow<Int> get() = _bitrateAutoFlow.asStateFlow()

	val codecKey get() = resources.getString(R.string.preferences_codec_key)
	var codec
		get() = sharedPreferences.getString(codecKey, codecDefault.value)?.let { value ->
			Codec.values().firstOrNull { it.value == value }
		}  ?: codecDefault
		set(value) { sharedPreferences.edit().putString(codecKey, value.value).apply() }

	private val videoProfileDefaultBitrate get() = ConnectVideoProfile.preset(resolution.preset, fps.preset, codec.codec)
	val videoProfile get() = videoProfileDefaultBitrate.let {
		val bitrate = bitrate
		if(bitrate == null)
			it
		else
			it.copy(bitrate = bitrate)
	}
}