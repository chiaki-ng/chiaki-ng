// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#pragma once

#include "transport.hpp"

#include <nlohmann/json.hpp>
#include <rtc/rtc.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// One WebRTC peer: audio as a native RTP Opus track, video as fragmented
// Annex-B access units on a partially-reliable DataChannel, input as JSON on
// a reliable DataChannel. Layout and rationale follow moonlight-web (see
// webbridge/PROTOCOL.md).
class Relay : public Transport
{
public:
	// ice_servers: optional STUN/TURN entries (e.g. Cloudflare TURN creds
	// minted per session); only stun: and turn:...?transport=udp URLs are
	// used (libjuice has no TURN over TCP/TLS). Empty = default STUN.
	Relay(Callbacks cbs, std::vector<IceServerEntry> ice_servers = {});
	~Relay() override;

	// Creates the PeerConnection; the SDP offer is emitted via signal_out.
	// Track must be added before the first DataChannel (libdatachannel
	// publishes the offer on createDataChannel).
	void setup() override;
	void handle_signal(const nlohmann::json &msg) override; // answer / candidate
	void close() override;

	bool connected() const { return connected_.load(); }

	void set_audio_params(unsigned rate, unsigned channels, unsigned samples_per_frame) override;
	void send_video(const uint8_t *data, size_t size, bool keyframe) override;
	void send_audio(const uint8_t *data, size_t size) override;
	void send_input_json(const std::string &s) override;

private:
	static constexpr size_t kFragHeaderSize = 17;
	static constexpr size_t kMaxPayloadSize = 16000;
	static constexpr size_t kMaxQueuedFrames = 60;

	struct Job
	{
		std::vector<uint8_t> data;
		bool keyframe;
		uint32_t frame_id;
		uint32_t backend_ts;
	};

	void sender_loop();
	void send_job(const Job &job);

	Callbacks cbs;
	std::vector<IceServerEntry> ice_servers;

	std::shared_ptr<rtc::PeerConnection> pc;
	std::shared_ptr<rtc::Track> audio_track;
	std::shared_ptr<rtc::RtpPacketizationConfig> audio_rtp_config;
	std::shared_ptr<rtc::DataChannel> video_dc;
	std::shared_ptr<rtc::DataChannel> input_dc;
	std::shared_ptr<rtc::DataChannel> mic_dc;

	std::mutex pc_mutex;

	std::atomic<bool> connected_{false};
	std::atomic<bool> stopping{false};
	std::atomic<uint32_t> next_frame_id{0};

	// Offer delivery: hold the offer until ICE gathering completes (capped by
	// a timer) so it carries all candidates — crucially the TURN relay
	// candidate, which takes a few seconds to allocate. Trickling it in late
	// lets a restrictive-NAT browser exhaust its checklist and give up before
	// the relay pair can form. After the offer is sent, later candidates (if
	// any) trickle normally.
	void send_offer_now();
	std::mutex offer_mutex;
	std::atomic<bool> offer_sent{false};
	std::thread offer_timer_thread;
	std::condition_variable offer_cv;
	bool offer_timer_stop = false;

	unsigned audio_samples_per_frame = 480;

	// pending keyframe if the video DC wasn't open yet
	std::mutex pending_mutex;
	std::vector<uint8_t> pending_keyframe;

	std::mutex queue_mutex;
	std::condition_variable queue_cv;
	std::deque<Job> queue;
	std::thread sender_thread;
};
