// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#pragma once

#include "config.hpp"
#include "discoverymgr.hpp"
#include "source.hpp"
#include "transport.hpp"

#include <rtc/rtc.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace httplib
{
class Server;
}

struct ServerOptions
{
	uint16_t http_port = 9080;
	uint16_t ws_port = 9081;
	std::string bind_address = "0.0.0.0";
	std::string frontend_dir = "frontend";
};

class BridgeServer
{
public:
	BridgeServer(ServerOptions options, Config &config);
	~BridgeServer();

	bool start();
	void stop();

private:
	// One active streaming client and its session. The transport can be
	// swapped mid-session (WebRTC -> WebSocket fallback) without touching
	// the source; senders go through get_transport().
	struct Stream
	{
		std::shared_ptr<rtc::WebSocket> ws;
		std::shared_ptr<Source> source;

		std::mutex transport_mutex;
		std::shared_ptr<Transport> transport;

		// audio params cached so a swapped-in transport can be primed
		std::mutex audio_mutex;
		unsigned audio_rate = 0, audio_channels = 0, audio_spf = 0;

		std::shared_ptr<Transport> get_transport()
		{
			std::lock_guard<std::mutex> lock(transport_mutex);
			return transport;
		}
	};

	void setup_rest();
	void on_ws_client(std::shared_ptr<rtc::WebSocket> ws);
	void handle_ws_message(std::shared_ptr<rtc::WebSocket> ws, const std::string &msg);
	void handle_ws_binary(std::shared_ptr<rtc::WebSocket> ws, const uint8_t *data, size_t size);
	void start_stream(std::shared_ptr<rtc::WebSocket> ws, const nlohmann::json &msg);
	void switch_transport(std::shared_ptr<rtc::WebSocket> ws, const nlohmann::json &msg);
	void stop_stream(const std::shared_ptr<rtc::WebSocket> &ws, const std::string &reason, bool error, bool notify);

	Transport::Callbacks make_transport_callbacks(const std::shared_ptr<rtc::WebSocket> &ws,
			const std::weak_ptr<Stream> &weak_stream, bool webrtc);

	static void ws_send_json(const std::shared_ptr<rtc::WebSocket> &ws, const nlohmann::json &j);

	ServerOptions options;
	Config &config;
	DiscoveryManager discovery;

	std::unique_ptr<httplib::Server> http;
	std::thread http_thread;
	std::unique_ptr<rtc::WebSocketServer> ws_server;

	std::mutex stream_mutex;
	std::shared_ptr<Stream> stream; // null when idle

	// keep connected signaling clients alive (rtc::WebSocketServer hands out
	// shared_ptrs but does not retain them)
	std::mutex clients_mutex;
	std::vector<std::shared_ptr<rtc::WebSocket>> clients;
};
