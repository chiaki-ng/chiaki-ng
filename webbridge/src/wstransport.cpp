// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "wstransport.hpp"
#include "log.hpp"

#include <opus/opus.h>

#include <chrono>
#include <cstring>

using nlohmann::json;

static uint32_t now_ms32()
{
	using namespace std::chrono;
	return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

WsTransport::WsTransport(Callbacks cbs_)
	: cbs(std::move(cbs_))
{
}

WsTransport::~WsTransport()
{
	close();
}

void WsTransport::setup()
{
	// The socket is the signaling WS, already open — we are "connected" the
	// moment the browser knows to expect this transport.
	if(cbs.signal_out)
	{
		{
			std::lock_guard<std::mutex> lock(audio_mutex);
			if(opus)
				cbs.signal_out(json{{"type", "audioInfo"}, {"rate", audio_rate}, {"channels", audio_channels}}.dump());
		}
		cbs.signal_out(json{{"type", "status"}, {"state", "connected"}}.dump());
	}
	CHIAKI_LOGI(bridge_log(), "webbridge: ws transport active");
}

void WsTransport::close()
{
	closed_ = true;
	std::lock_guard<std::mutex> lock(audio_mutex);
	if(opus)
	{
		opus_decoder_destroy(opus);
		opus = nullptr;
	}
}

void WsTransport::handle_ws_binary(const uint8_t *data, size_t size)
{
	if(closed_ || size < 1)
		return;
	if(data[0] == kChanMic && cbs.mic_pcm)
		cbs.mic_pcm(data + 1, size - 1);
}

void WsTransport::set_audio_params(unsigned rate, unsigned channels, unsigned samples_per_frame)
{
	(void)samples_per_frame;
	std::lock_guard<std::mutex> lock(audio_mutex);
	if(opus)
	{
		opus_decoder_destroy(opus);
		opus = nullptr;
	}
	audio_rate = rate ? rate : 48000;
	audio_channels = channels ? channels : 2;
	int err = 0;
	opus = opus_decoder_create((opus_int32)audio_rate, (int)audio_channels, &err);
	if(!opus)
	{
		CHIAKI_LOGE(bridge_log(), "webbridge: opus decoder init failed: %s", opus_strerror(err));
		return;
	}
	// 120 ms is the maximum opus frame duration
	pcm_scratch.resize((audio_rate / 1000) * 120 * audio_channels);
	if(cbs.signal_out)
		cbs.signal_out(json{{"type", "audioInfo"}, {"rate", audio_rate}, {"channels", audio_channels}}.dump());
}

void WsTransport::send_audio(const uint8_t *data, size_t size)
{
	if(closed_)
		return;
	if(cbs.buffered_amount && cbs.buffered_amount() > kAudioDropThreshold)
		return; // stale voice is worse than a dropout; the player conceals gaps
	std::lock_guard<std::mutex> lock(audio_mutex);
	if(!opus)
		return;
	int samples = opus_decode(opus, data, (opus_int32)size, pcm_scratch.data(),
			(int)(pcm_scratch.size() / audio_channels), 0);
	if(samples <= 0)
	{
		CHIAKI_LOGW(bridge_log(), "webbridge: opus decode failed: %s", opus_strerror(samples));
		return;
	}
	const size_t bytes = (size_t)samples * audio_channels * sizeof(int16_t);
	audio_msg.resize(1 + bytes);
	audio_msg[0] = kChanAudio;
	memcpy(audio_msg.data() + 1, pcm_scratch.data(), bytes);
	std::lock_guard<std::mutex> slock(send_mutex);
	if(cbs.binary_out)
		cbs.binary_out(audio_msg.data(), audio_msg.size());
}

void WsTransport::send_video(const uint8_t *data, size_t size, bool keyframe)
{
	if(closed_)
		return;

	const size_t buffered = cbs.buffered_amount ? cbs.buffered_amount() : 0;
	if(buffered > (keyframe ? kHardDropThreshold : kDeltaDropThreshold))
	{
		idr_pending = true;
		return;
	}
	// Once the backlog drained after drops, get a keyframe to resynchronize.
	if(idr_pending.exchange(false) && !keyframe && cbs.need_idr)
	{
		uint32_t now = now_ms32();
		if(now - last_idr_req_ms > 1000)
		{
			last_idr_req_ms = now;
			CHIAKI_LOGW(bridge_log(), "webbridge: ws transport dropped frames on backpressure, requesting IDR");
			cbs.need_idr();
		}
	}

	// Single message per AU: [chan][17-byte fragment header][payload], header
	// identical to the DataChannel format with totalChunks=1 so the frontend
	// reuses its reassembly path unchanged.
	std::vector<uint8_t> buf(1 + kFragHeaderSize + size);
	buf[0] = kChanVideo;
	uint8_t *h = buf.data() + 1;
	auto w32 = [h](size_t at, uint32_t v) {
		h[at] = (uint8_t)((v >> 24) & 0xff);
		h[at + 1] = (uint8_t)((v >> 16) & 0xff);
		h[at + 2] = (uint8_t)((v >> 8) & 0xff);
		h[at + 3] = (uint8_t)(v & 0xff);
	};
	w32(0, next_frame_id.fetch_add(1));
	h[4] = 0;
	h[5] = 0; // chunkIndex 0
	h[6] = 0;
	h[7] = 1; // totalChunks 1
	h[8] = keyframe ? 1 : 0;
	w32(9, (uint32_t)size);
	w32(13, now_ms32());
	memcpy(buf.data() + 1 + kFragHeaderSize, data, size);

	std::lock_guard<std::mutex> lock(send_mutex);
	if(cbs.binary_out && !cbs.binary_out(buf.data(), buf.size()))
		CHIAKI_LOGW(bridge_log(), "webbridge: ws transport video send failed");
}

void WsTransport::send_input_json(const std::string &s)
{
	if(closed_)
		return;
	if(cbs.signal_out)
		cbs.signal_out(s); // "t"-keyed, distinct from "type"-keyed signaling
}
