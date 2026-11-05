// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.common

import com.metallic.chiaki.lib.Codec
import com.metallic.chiaki.lib.ConnectVideoProfile
import com.metallic.chiaki.lib.VideoFPSPreset
import com.metallic.chiaki.lib.VideoResolutionPreset
import org.junit.Assert.*
import org.junit.Test

class StreamMediaTest
{
	private val preferred = ConnectVideoProfile(1920, 1080, 60, 12345, Codec.CODEC_H265)

	private val presetCalls = mutableListOf<Triple<VideoResolutionPreset, VideoFPSPreset, Codec>>()
	private val preset = { res: VideoResolutionPreset, fps: VideoFPSPreset, codec: Codec ->
		presetCalls.add(Triple(res, fps, codec))
		ConnectVideoProfile(640, 360, 30, 2000, codec)
	}

	@Test
	fun videoProfile_withVideo_keepsPreferredProfile()
	{
		val profile = StreamMedia(video = true, audio = true).videoProfile(preferred, preset)
		assertEquals(preferred, profile)
		assertTrue(presetCalls.isEmpty())
	}

	@Test
	fun videoProfile_withoutVideo_requestsCheapestStreamInPreferredCodec()
	{
		val profile = StreamMedia(video = false, audio = true).videoProfile(preferred, preset)
		assertEquals(listOf(Triple(VideoResolutionPreset.RES_360P, VideoFPSPreset.FPS_30, Codec.CODEC_H265)), presetCalls)
		assertEquals(ConnectVideoProfile(640, 360, 30, 2000, Codec.CODEC_H265), profile)
	}

	@Test
	fun connectInfo_carriesMediaChoice()
	{
		val key = ByteArray(16)
		val info = StreamMedia(video = false, audio = true).connectInfo(true, "192.168.1.2", key, key, preferred, preset)
		assertFalse(info.videoEnabled)
		assertTrue(info.audioEnabled)
		assertEquals(360, info.videoProfile.height)
		assertEquals("192.168.1.2", info.host)
	}

	@Test
	fun connectInfo_defaultsToVideoAndAudio()
	{
		val key = ByteArray(16)
		val info = StreamMedia().connectInfo(true, "h", key, key, preferred, preset)
		assertTrue(info.videoEnabled)
		assertTrue(info.audioEnabled)
		assertEquals(preferred, info.videoProfile)
	}
}
