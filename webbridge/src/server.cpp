// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "server.hpp"
#include "chiakisource.hpp"
#include "demosource.hpp"
#include "log.hpp"
#include "registration.hpp"
#include "relay.hpp"
#include "wstransport.hpp"

#include <chiaki/base64.h>

#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>

using nlohmann::json;

static const char *BRIDGE_VERSION = "0.1.0";

// Optional shared-secret gate (CHIAKI_WEB_TOKEN in the environment / .env):
// defense in depth while the bridge is exposed publicly. Static assets and
// /api/info stay open; anything that touches the console requires the token.
static std::string web_token()
{
	const char *t = std::getenv("CHIAKI_WEB_TOKEN");
	return t ? t : "";
}

static bool request_authorized(const httplib::Request &req)
{
	std::string token = web_token();
	if(token.empty())
		return true;
	return req.get_header_value("X-Auth-Token") == token;
}

static bool reject_unauthorized(const httplib::Request &req, httplib::Response &res)
{
	if(request_authorized(req))
		return false;
	res.status = 401;
	res.set_content("{\"error\":\"unauthorized\"}", "application/json");
	return true;
}

// Mint short-lived STUN/TURN credentials from Cloudflare's TURN service
// (CHIAKI_TURN_KEY_ID / CHIAKI_TURN_API_TOKEN in the environment / .env).
// Returns the ready-to-use RTCPeerConnection iceServers array — cached until
// shortly before the credentials expire. Empty array when unconfigured or the
// API is unreachable (WebRTC then runs STUN-only, as before).
static json fetch_turn_ice_servers()
{
	const char *key_id = std::getenv("CHIAKI_TURN_KEY_ID");
	const char *api_token = std::getenv("CHIAKI_TURN_API_TOKEN");
	if(!key_id || !*key_id || !api_token || !*api_token)
		return json::array();

	static std::mutex cache_mutex;
	static json cached = json::array();
	static time_t cached_until = 0;
	std::lock_guard<std::mutex> lock(cache_mutex);
	time_t now = time(nullptr);
	if(!cached.empty() && now < cached_until)
		return cached;

	constexpr int ttl = 4 * 3600;
	httplib::Client cli("https://rtc.live.cloudflare.com");
	cli.set_connection_timeout(5, 0);
	cli.set_read_timeout(5, 0);
	httplib::Headers headers = {{"Authorization", std::string("Bearer ") + api_token}};
	auto res = cli.Post(("/v1/turn/keys/" + std::string(key_id) + "/credentials/generate-ice-servers").c_str(),
			headers, json{{"ttl", ttl}}.dump(), "application/json");
	if(!res || res->status < 200 || res->status >= 300)
	{
		CHIAKI_LOGW(bridge_log(), "webbridge: TURN credential fetch failed (%s)",
				res ? std::to_string(res->status).c_str() : "unreachable");
		return cached; // possibly stale-but-valid, else empty
	}
	json j = json::parse(res->body, nullptr, false);
	if(!j.is_object() || !j.contains("iceServers"))
	{
		CHIAKI_LOGW(bridge_log(), "webbridge: TURN credential response malformed");
		return cached;
	}
	json servers = j["iceServers"];
	if(servers.is_object()) // older API shape: single object
		servers = json::array({servers});
	cached = servers;
	cached_until = now + ttl - 300; // refresh well before expiry
	CHIAKI_LOGI(bridge_log(), "webbridge: minted TURN credentials (ttl %d s)", ttl);
	return cached;
}

static std::vector<IceServerEntry> ice_entries_from_json(const json &servers)
{
	std::vector<IceServerEntry> out;
	if(!servers.is_array())
		return out;
	for(const auto &s : servers)
	{
		if(!s.is_object())
			continue;
		IceServerEntry e;
		if(s.contains("urls"))
		{
			if(s["urls"].is_string())
				e.urls.push_back(s["urls"].get<std::string>());
			else if(s["urls"].is_array())
				for(const auto &u : s["urls"])
					if(u.is_string())
						e.urls.push_back(u.get<std::string>());
		}
		e.username = s.value("username", "");
		e.credential = s.value("credential", "");
		if(!e.urls.empty())
			out.push_back(std::move(e));
	}
	return out;
}

BridgeServer::BridgeServer(ServerOptions options_, Config &config_)
	: options(std::move(options_)), config(config_)
{
}

BridgeServer::~BridgeServer()
{
	stop();
}

void BridgeServer::ws_send_json(const std::shared_ptr<rtc::WebSocket> &ws, const json &j)
{
	if(!ws || !ws->isOpen())
		return;
	try
	{
		ws->send(j.dump());
	}
	catch(const std::exception &)
	{
	}
}

bool BridgeServer::start()
{
	// best-effort; REST still works without it. Registered consoles get
	// direct unicast pings in case broadcast doesn't reach them.
	{
		std::vector<std::string> unicast;
		std::lock_guard<std::mutex> lock(config.mutex);
		for(const auto &c : config.consoles)
			unicast.push_back(c.host);
		discovery.start(unicast);
	}

	// --- signaling WebSocket
	try
	{
		rtc::WebSocketServerConfiguration wscfg;
		wscfg.port = options.ws_port;
		wscfg.bindAddress = options.bind_address;
		ws_server = std::make_unique<rtc::WebSocketServer>(wscfg);
		ws_server->onClient([this](std::shared_ptr<rtc::WebSocket> ws) { on_ws_client(ws); });
	}
	catch(const std::exception &e)
	{
		CHIAKI_LOGE(bridge_log(), "webbridge: failed to start signaling server on port %u: %s", options.ws_port, e.what());
		return false;
	}

	// --- HTTP: static frontend + REST
	http = std::make_unique<httplib::Server>();
	// The app is tiny and iterated on constantly; stale JS on phones (Safari
	// caches hard) is far more expensive than re-fetching ~100 KB.
	http->set_post_routing_handler([](const httplib::Request &, httplib::Response &res) {
		res.set_header("Cache-Control", "no-store");
	});
	setup_rest();
	if(!http->set_mount_point("/", options.frontend_dir))
	{
		CHIAKI_LOGE(bridge_log(), "webbridge: frontend dir not found: %s", options.frontend_dir.c_str());
		return false;
	}
	if(!http->bind_to_port(options.bind_address, options.http_port))
	{
		CHIAKI_LOGE(bridge_log(), "webbridge: failed to bind HTTP on port %u", options.http_port);
		return false;
	}
	http_thread = std::thread([this] { http->listen_after_bind(); });

	CHIAKI_LOGI(bridge_log(), "webbridge: http on %s:%u, signaling ws on %s:%u, frontend: %s",
			options.bind_address.c_str(), options.http_port,
			options.bind_address.c_str(), options.ws_port,
			options.frontend_dir.c_str());
	return true;
}

void BridgeServer::stop()
{
	stop_stream(nullptr, "shutting down", false, true);
	if(http)
		http->stop();
	if(http_thread.joinable())
		http_thread.join();
	ws_server.reset();
	discovery.stop();
}

void BridgeServer::setup_rest()
{
	http->Get("/api/info", [this](const httplib::Request &, httplib::Response &res) {
		res.set_content(json{{"name", "chiaki-web"}, {"version", BRIDGE_VERSION}, {"wsPort", options.ws_port}}.dump(), "application/json");
	});

	http->Get("/api/hosts", [this](const httplib::Request &req, httplib::Response &res) {
		if(reject_unauthorized(req, res))
			return;
		json out = json::array();
		auto discovered = discovery.hosts();
		std::lock_guard<std::mutex> lock(config.mutex);
		for(const auto &d : discovered)
		{
			bool registered = false;
			for(const auto &c : config.consoles)
				if(c.host == d.host_addr)
					registered = true;
			out.push_back({
				{"host", d.host_addr},
				{"nickname", d.host_name},
				{"mac", d.host_id},
				{"ps5", d.ps5},
				{"state", d.ready ? "ready" : (d.standby ? "standby" : "unknown")},
				{"discovered", true},
				{"registered", registered},
				{"appName", d.app_name.empty() ? json(nullptr) : json(d.app_name)},
			});
		}
		for(const auto &c : config.consoles)
		{
			bool listed = false;
			for(const auto &j : out)
				if(j["host"] == c.host)
					listed = true;
			if(listed)
				continue;
			out.push_back({
				{"host", c.host},
				{"nickname", c.nickname},
				{"mac", ""},
				{"ps5", c.ps5},
				{"state", "unknown"},
				{"discovered", false},
				{"registered", true},
				{"appName", nullptr},
			});
		}
		res.set_content(out.dump(), "application/json");
	});

	http->Post("/api/wakeup", [this](const httplib::Request &req, httplib::Response &res) {
		if(reject_unauthorized(req, res))
			return;
		json j = json::parse(req.body, nullptr, false);
		std::string host = j.is_object() ? j.value("host", "") : "";
		ConsoleConfig console;
		{
			std::lock_guard<std::mutex> lock(config.mutex);
			const ConsoleConfig *c = config.find_console(host);
			if(!c)
			{
				res.status = 404;
				res.set_content(json{{"error", "console not registered"}}.dump(), "application/json");
				return;
			}
			console = *c;
		}
		std::string key((const char *)console.regist_key.data(),
				strnlen((const char *)console.regist_key.data(), console.regist_key.size()));
		std::string error;
		if(!discovery.send_wakeup(host, key, console.ps5, error))
		{
			res.status = 500;
			res.set_content(json{{"error", error}}.dump(), "application/json");
			return;
		}
		res.status = 204;
	});

	http->Post("/api/register", [this](const httplib::Request &req, httplib::Response &res) {
		if(reject_unauthorized(req, res))
			return;
		json j = json::parse(req.body, nullptr, false);
		if(!j.is_object() || !j.contains("host") || !j.contains("pin"))
		{
			res.status = 400;
			res.set_content(json{{"error", "host and pin required"}}.dump(), "application/json");
			return;
		}
		std::array<uint8_t, 8> account_id = config.psn_account_id;
		if(j.contains("psnAccountId") && j["psnAccountId"].is_string() && !j["psnAccountId"].get<std::string>().empty())
		{
			std::string b64 = j["psnAccountId"];
			size_t sz = account_id.size();
			if(chiaki_base64_decode(b64.c_str(), b64.size(), account_id.data(), &sz) != CHIAKI_ERR_SUCCESS || sz != account_id.size())
			{
				res.status = 400;
				res.set_content(json{{"error", "invalid psnAccountId (expect base64 of 8 bytes)"}}.dump(), "application/json");
				return;
			}
			std::lock_guard<std::mutex> lock(config.mutex);
			config.psn_account_id = account_id;
			config.psn_account_id_set = true;
		}
		else if(!config.psn_account_id_set)
		{
			res.status = 400;
			res.set_content(json{{"error", "psnAccountId required (none stored yet)"}}.dump(), "application/json");
			return;
		}

		uint32_t pin = j["pin"].is_number() ? j["pin"].get<uint32_t>() : (uint32_t)strtoul(j["pin"].get<std::string>().c_str(), nullptr, 10);
		ConsoleConfig console;
		std::string error;
		if(!register_console(j.value("host", ""), j.value("ps5", true), pin, account_id, console, error))
		{
			res.status = 500;
			res.set_content(json{{"error", error}}.dump(), "application/json");
			return;
		}
		config.upsert_console(console);
		res.set_content(json{{"nickname", console.nickname}}.dump(), "application/json");
	});

	http->Get("/api/config", [this](const httplib::Request &req, httplib::Response &res) {
		if(reject_unauthorized(req, res))
			return;
		std::lock_guard<std::mutex> lock(config.mutex);
		res.set_content(config.sanitized().dump(), "application/json");
	});
}

void BridgeServer::on_ws_client(std::shared_ptr<rtc::WebSocket> ws)
{
	CHIAKI_LOGI(bridge_log(), "webbridge: signaling client connected");
	{
		std::lock_guard<std::mutex> lock(clients_mutex);
		clients.push_back(ws);
	}
	auto weak = std::weak_ptr<rtc::WebSocket>(ws);
	ws->onMessage([this, weak](rtc::message_variant msg) {
		auto ws = weak.lock();
		if(!ws)
			return;
		if(std::holds_alternative<rtc::binary>(msg))
		{
			// ws transport upstream data (mic PCM)
			const auto &b = std::get<rtc::binary>(msg);
			handle_ws_binary(ws, reinterpret_cast<const uint8_t *>(b.data()), b.size());
			return;
		}
		try
		{
			handle_ws_message(ws, std::get<std::string>(msg));
		}
		catch(const std::exception &e)
		{
			CHIAKI_LOGE(bridge_log(), "webbridge: signaling handler error: %s", e.what());
		}
	});
	ws->onClosed([this, weak]() {
		auto ws = weak.lock();
		{
			std::lock_guard<std::mutex> lock(clients_mutex);
			clients.erase(std::remove(clients.begin(), clients.end(), ws), clients.end());
		}
		std::shared_ptr<Stream> s;
		{
			std::lock_guard<std::mutex> lock(stream_mutex);
			if(stream && stream->ws == ws)
			{
				s = stream;
				stream.reset();
			}
		}
		if(s)
		{
			CHIAKI_LOGI(bridge_log(), "webbridge: streaming client disconnected, stopping session");
			// this is a libdatachannel callback thread; teardown elsewhere
			std::thread([s]() {
				if(s->source)
					s->source->stop();
				if(auto t = s->get_transport())
					t->close();
			}).detach();
		}
	});
}

void BridgeServer::handle_ws_message(std::shared_ptr<rtc::WebSocket> ws, const std::string &msg)
{
	json j = json::parse(msg, nullptr, false);
	if(!j.is_object())
		return;
	const std::string type = j.value("type", "");

	// "t"-keyed messages are browser input riding the signaling socket
	// (ws transport mode) — distinct namespace from "type"-keyed signaling.
	if(type.empty() && j.contains("t"))
	{
		std::shared_ptr<Stream> s;
		{
			std::lock_guard<std::mutex> lock(stream_mutex);
			s = stream;
		}
		if(s && s->ws == ws && s->source)
			s->source->handle_input(msg);
		return;
	}

	if(type == "start")
	{
		std::string token = web_token();
		if(!token.empty() && j.value("token", "") != token)
		{
			CHIAKI_LOGW(bridge_log(), "webbridge: rejected unauthorized start");
			ws_send_json(ws, {{"type", "quit"}, {"reason", "unauthorized"}, {"error", true}});
			return;
		}
		start_stream(ws, j);
	}
	else if(type == "answer" || type == "candidate")
	{
		std::shared_ptr<Stream> s;
		{
			std::lock_guard<std::mutex> lock(stream_mutex);
			s = stream;
		}
		if(s && s->ws == ws)
			if(auto t = s->get_transport())
				t->handle_signal(j);
	}
	else if(type == "switchTransport")
	{
		switch_transport(ws, j);
	}
	else if(type == "stop")
	{
		stop_stream(ws, "stopped", false, true);
	}
	else if(type == "clientlog")
	{
		CHIAKI_LOGI(bridge_log(), "webbridge[client]: %s", j.value("msg", "").c_str());
	}
}

void BridgeServer::start_stream(std::shared_ptr<rtc::WebSocket> ws, const json &msg)
{
	std::lock_guard<std::mutex> lock(stream_mutex);
	if(stream)
	{
		ws_send_json(ws, {{"type", "quit"}, {"reason", "busy"}, {"error", true}});
		return;
	}

	StreamProfile profile;
	const std::string res = msg.value("resolution", "720p");
	if(res == "360p") { profile.width = 640; profile.height = 360; }
	else if(res == "540p") { profile.width = 960; profile.height = 540; }
	else if(res == "1080p") { profile.width = 1920; profile.height = 1080; }
	else { profile.width = 1280; profile.height = 720; }
	profile.fps = msg.value("fps", 60) == 30 ? 30 : 60;
	profile.hevc = msg.value("codec", "h264") == "hevc";
	profile.bitrate = msg.value("bitrate", 0);

	auto s = std::make_shared<Stream>();
	s->ws = ws;

	bool demo = msg.value("demo", false);
	if(demo)
	{
		profile.hevc = false; // demo encodes H.264
		s->source = std::make_shared<DemoSource>(profile);
	}
	else
	{
		const std::string host = msg.value("host", "");
		ConsoleConfig console;
		{
			std::lock_guard<std::mutex> clock(config.mutex);
			const ConsoleConfig *c = config.find_console(host);
			if(!c)
			{
				ws_send_json(ws, {{"type", "quit"}, {"reason", "console not registered"}, {"error", true}});
				return;
			}
			console = *c;
		}
		s->source = std::make_shared<ChiakiSource>(console, profile, config.psn_account_id);
	}

	auto weak_ws = std::weak_ptr<rtc::WebSocket>(ws);
	std::weak_ptr<Stream> weak_stream = s;

	const bool use_webrtc = msg.value("transport", "webrtc") != "ws";
	if(use_webrtc)
	{
		// TURN relay (if configured) as the middle fallback tier: browsers
		// get the full server list; the Relay filters what libjuice can use.
		json ice_servers = fetch_turn_ice_servers();
		if(!ice_servers.empty())
			ws_send_json(ws, {{"type", "iceServers"}, {"iceServers", ice_servers}});
		s->transport = std::make_shared<Relay>(make_transport_callbacks(ws, weak_stream, true),
				ice_entries_from_json(ice_servers));
	}
	else
	{
		s->transport = std::make_shared<WsTransport>(make_transport_callbacks(ws, weak_stream, false));
	}

	SourceSink sink;
	sink.video_frame = [weak_stream](const uint8_t *buf, size_t size, bool key) {
		if(auto s = weak_stream.lock())
			if(auto t = s->get_transport())
				t->send_video(buf, size, key);
	};
	sink.audio_header = [weak_stream](unsigned rate, unsigned ch, unsigned spf) {
		if(auto s = weak_stream.lock())
		{
			{
				std::lock_guard<std::mutex> lock(s->audio_mutex);
				s->audio_rate = rate;
				s->audio_channels = ch;
				s->audio_spf = spf;
			}
			if(auto t = s->get_transport())
				t->set_audio_params(rate, ch, spf);
		}
	};
	sink.audio_frame = [weak_stream](const uint8_t *buf, size_t size) {
		if(auto s = weak_stream.lock())
			if(auto t = s->get_transport())
				t->send_audio(buf, size);
	};
	sink.event = [weak_stream](const std::string &json_msg) {
		if(auto s = weak_stream.lock())
			if(auto t = s->get_transport())
				t->send_input_json(json_msg);
	};
	sink.quit = [this, weak_ws](const std::string &reason, bool error) {
		// runs on the chiaki session thread; stop_stream joins that thread,
		// so it must run elsewhere
		std::thread([this, weak_ws, reason, error]() {
			if(auto ws = weak_ws.lock())
				stop_stream(ws, reason, error, true);
		}).detach();
	};

	ws_send_json(ws, {
		{"type", "streamInfo"},
		{"codec", profile.hevc ? "hevc" : "h264"},
		{"width", profile.width},
		{"height", profile.height},
		{"fps", profile.fps},
	});

	std::string error;
	if(!s->source->start(sink, error))
	{
		ws_send_json(ws, {{"type", "quit"}, {"reason", error}, {"error", true}});
		return;
	}
	ws_send_json(ws, {{"type", "status"}, {"state", "connecting"}});
	s->transport->setup(); // webrtc: emits the offer; ws: announces connected
	stream = s;
}

Transport::Callbacks BridgeServer::make_transport_callbacks(const std::shared_ptr<rtc::WebSocket> &ws,
		const std::weak_ptr<Stream> &weak_stream, bool webrtc)
{
	auto weak_ws = std::weak_ptr<rtc::WebSocket>(ws);
	Transport::Callbacks cbs;
	cbs.signal_out = [weak_ws](const std::string &out) {
		if(auto ws = weak_ws.lock())
		{
			try
			{
				ws->send(out);
			}
			catch(const std::exception &)
			{
			}
		}
	};
	cbs.binary_out = [weak_ws](const uint8_t *buf, size_t size) -> bool {
		auto ws = weak_ws.lock();
		if(!ws || !ws->isOpen())
			return false;
		try
		{
			ws->send(reinterpret_cast<const std::byte *>(buf), size);
			return true;
		}
		catch(const std::exception &)
		{
			return false;
		}
	};
	cbs.buffered_amount = [weak_ws]() -> size_t {
		if(auto ws = weak_ws.lock())
			return ws->bufferedAmount();
		return SIZE_MAX; // socket gone: behave as saturated
	};
	cbs.input_json = [weak_stream](const std::string &in) {
		if(auto s = weak_stream.lock())
			if(s->source)
				s->source->handle_input(in);
	};
	cbs.mic_pcm = [weak_stream](const uint8_t *buf, size_t size) {
		if(auto s = weak_stream.lock())
			if(s->source)
				s->source->handle_mic_pcm(buf, size);
	};
	cbs.need_idr = [weak_stream]() {
		if(auto s = weak_stream.lock())
			if(s->source)
				s->source->request_idr();
	};
	if(webrtc)
	{
		cbs.closed = [this, weak_ws, weak_stream]() {
			// The pc failed/closed on its own. Give the browser a grace
			// window to switch to the ws fallback transport before killing
			// the (expensive to re-establish) console session. Runs detached:
			// this is a libdatachannel callback thread.
			std::thread([this, weak_ws, weak_stream]() {
				std::shared_ptr<Transport> failed_transport;
				if(auto s = weak_stream.lock())
					failed_transport = s->get_transport();
				else
					return;
				std::this_thread::sleep_for(std::chrono::seconds(8));
				auto s = weak_stream.lock();
				if(!s || s->get_transport() != failed_transport)
					return; // transport was swapped: fallback succeeded
				if(auto ws = weak_ws.lock())
					stop_stream(ws, "peer connection closed", false, true);
			}).detach();
		};
	}
	return cbs;
}

// {"type":"switchTransport","transport":"ws"} — mid-session fallback: the
// browser gave up on WebRTC (or wants to change transports); swap the media
// path while the chiaki session keeps running, then request a keyframe so
// the fresh decoder can sync (the source prepends cached SPS/PPS to it).
void BridgeServer::switch_transport(std::shared_ptr<rtc::WebSocket> ws, const json &msg)
{
	if(msg.value("transport", "") != "ws")
		return;
	std::shared_ptr<Stream> s;
	{
		std::lock_guard<std::mutex> lock(stream_mutex);
		s = stream;
	}
	if(!s || s->ws != ws)
		return;

	CHIAKI_LOGI(bridge_log(), "webbridge: switching transport to ws (client request)");
	std::weak_ptr<Stream> weak_stream = s;
	auto t = std::make_shared<WsTransport>(make_transport_callbacks(ws, weak_stream, false));
	{
		std::lock_guard<std::mutex> lock(s->audio_mutex);
		if(s->audio_rate)
			t->set_audio_params(s->audio_rate, s->audio_channels, s->audio_spf);
	}
	std::shared_ptr<Transport> old;
	{
		std::lock_guard<std::mutex> lock(s->transport_mutex);
		old = s->transport;
		s->transport = t;
	}
	t->setup();
	if(old)
	{
		// close() can block on pc teardown; this may be a ws callback thread
		std::thread([old]() { old->close(); }).detach();
	}
	if(s->source)
		s->source->request_idr();
}

void BridgeServer::handle_ws_binary(std::shared_ptr<rtc::WebSocket> ws, const uint8_t *data, size_t size)
{
	std::shared_ptr<Stream> s;
	{
		std::lock_guard<std::mutex> lock(stream_mutex);
		s = stream;
	}
	if(s && s->ws == ws)
		if(auto t = s->get_transport())
			t->handle_ws_binary(data, size);
}

void BridgeServer::stop_stream(const std::shared_ptr<rtc::WebSocket> &ws, const std::string &reason, bool error, bool notify)
{
	std::shared_ptr<Stream> s;
	{
		std::lock_guard<std::mutex> lock(stream_mutex);
		if(!stream)
			return;
		if(ws && stream->ws != ws)
			return;
		s = stream;
		stream.reset();
	}
	CHIAKI_LOGI(bridge_log(), "webbridge: stopping stream: %s", reason.c_str());
	if(s->source)
		s->source->stop();
	if(auto t = s->get_transport())
		t->close();
	if(notify)
		ws_send_json(s->ws, {{"type", "quit"}, {"reason", reason}, {"error", error}});
}
