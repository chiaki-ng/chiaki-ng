// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// STUN/TURN server entry (as minted by a TURN credential API).
struct IceServerEntry
{
	std::vector<std::string> urls;
	std::string username;
	std::string credential;
};

// A media/input path between the bridge and one browser. Two implementations:
// Relay (WebRTC: RTP audio + DataChannels) and WsTransport (everything
// multiplexed over the signaling WebSocket for NAT/UDP-hostile networks).
// The server can swap the transport mid-session ("switchTransport") without
// touching the chiaki session, so all senders must tolerate being discarded.
class Transport
{
public:
	struct Callbacks
	{
		std::function<void(const std::string &)> signal_out;       // signaling JSON towards browser
		std::function<bool(const uint8_t *, size_t)> binary_out;   // binary on the signaling WS (WsTransport)
		std::function<size_t()> buffered_amount;                   // signaling WS send backlog (WsTransport)
		std::function<void(const std::string &)> input_json;       // input message from browser
		std::function<void(const uint8_t *, size_t)> mic_pcm;      // mic PCM from browser
		std::function<void()> need_idr;                            // we dropped delta frames
		std::function<void()> closed;                              // transport ended on its own
	};

	virtual ~Transport() = default;

	virtual void setup() = 0;
	virtual void handle_signal(const nlohmann::json &msg) { (void)msg; }
	// Binary message that arrived on the signaling WebSocket.
	virtual void handle_ws_binary(const uint8_t *data, size_t size)
	{
		(void)data;
		(void)size;
	}
	virtual void close() = 0;

	virtual void set_audio_params(unsigned rate, unsigned channels, unsigned samples_per_frame) = 0;
	virtual void send_video(const uint8_t *data, size_t size, bool keyframe) = 0;
	virtual void send_audio(const uint8_t *data, size_t size) = 0;
	virtual void send_input_json(const std::string &s) = 0;
};
