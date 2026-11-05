// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.common

import com.metallic.chiaki.lib.Codec
import com.metallic.chiaki.lib.ConnectInfo
import com.metallic.chiaki.lib.ConnectVideoProfile
import com.metallic.chiaki.lib.VideoFPSPreset
import com.metallic.chiaki.lib.VideoResolutionPreset

typealias VideoProfilePreset = (VideoResolutionPreset, VideoFPSPreset, Codec) -> ConnectVideoProfile

/**
 * Which parts of the stream are played on this device.
 * Without either the phone acts only as a controller, e.g. while watching on the TV.
 */
data class StreamMedia(val video: Boolean = true, val audio: Boolean = true)
{
	/** The console sends video anyway, so without it ask for the cheapest stream. */
	fun videoProfile(preferred: ConnectVideoProfile, preset: VideoProfilePreset = ConnectVideoProfile.Companion::preset) =
		if(video)
			preferred
		else
			preset(VideoResolutionPreset.RES_360P, VideoFPSPreset.FPS_30, preferred.codec)

	fun connectInfo(ps5: Boolean, host: String, registKey: ByteArray, morning: ByteArray, preferred: ConnectVideoProfile,
		preset: VideoProfilePreset = ConnectVideoProfile.Companion::preset) =
		ConnectInfo(ps5, host, registKey, morning, videoProfile(preferred, preset), videoEnabled = video, audioEnabled = audio)
}
