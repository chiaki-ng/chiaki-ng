// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#pragma once

#include "transport.hpp"

#include <atomic>
#include <mutex>

struct OpusDecoder;

// Fallback transport for networks where WebRTC cannot connect (UDP-hostile
// wifi, symmetric NATs with no TURN): everything is multiplexed over the
// signaling WebSocket, which is plain TCP and already traverses whatever
// proxy/tunnel delivered the page. Binary messages carry a 1-byte channel
// prefix (see PROTOCOL.md "WebSocket transport"):
//
//   0x01 bridge->browser  video: 17-byte fragment header + Annex-B AU
//   0x02 bridge->browser  audio: interleaved s16le PCM (decoded on the bridge,
//                         since there is no RTP track to lean on)
//   0x03 browser->bridge  mic:   48 kHz mono s16le PCM
//
// Input and events stay JSON text on the same socket ("t"-keyed, so they
// cannot collide with "type"-keyed signaling).
class WsTransport : public Transport
{
public:
	explicit WsTransport(Callbacks cbs);
	~WsTransport() override;

	void setup() override; // announces {"type":"status","state":"connected"}
	void handle_ws_binary(const uint8_t *data, size_t size) override;
	void close() override;

	void set_audio_params(unsigned rate, unsigned channels, unsigned samples_per_frame) override;
	void send_video(const uint8_t *data, size_t size, bool keyframe) override;
	void send_audio(const uint8_t *data, size_t size) override;
	void send_input_json(const std::string &s) override;

private:
	static constexpr uint8_t kChanVideo = 0x01;
	static constexpr uint8_t kChanAudio = 0x02;
	static constexpr uint8_t kChanMic = 0x03;
	static constexpr size_t kFragHeaderSize = 17;
	// TCP backpressure policy: past this backlog delta frames get dropped (an
	// IDR is requested once the send buffer drains), audio gets dropped at a
	// tighter bound (stale voice is worse than a gap), and a truly saturated
	// socket drops even keyframes.
	static constexpr size_t kDeltaDropThreshold = 1u << 20;  // 1 MiB
	static constexpr size_t kAudioDropThreshold = 1u << 18;  // 256 KiB
	static constexpr size_t kHardDropThreshold = 8u << 20;   // 8 MiB

	Callbacks cbs;

	std::atomic<bool> closed_{false};
	std::atomic<uint32_t> next_frame_id{0};
	std::atomic<bool> idr_pending{false}; // dropped deltas, IDR not yet requested
	uint32_t last_idr_req_ms = 0;

	std::mutex send_mutex;

	std::mutex audio_mutex;
	OpusDecoder *opus = nullptr;
	unsigned audio_rate = 48000;
	unsigned audio_channels = 2;
	std::vector<int16_t> pcm_scratch;
	std::vector<uint8_t> audio_msg;
};
