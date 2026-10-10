// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "relay.hpp"
#include "log.hpp"

#include <rtc/global.hpp>

#include <chrono>
#include <cstring>
#include <random>

using nlohmann::json;

static uint32_t now_ms32()
{
	using namespace std::chrono;
	return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// Route libdatachannel/libjuice logs (ICE candidate gathering, TURN
// allocation, DTLS) into the bridge log. Call once; honors --verbose.
void relay_init_logging(bool verbose)
{
	rtc::InitLogger(verbose ? rtc::LogLevel::Verbose : rtc::LogLevel::Info,
			[](rtc::LogLevel level, std::string message) {
				ChiakiLogLevel l = level <= rtc::LogLevel::Error ? CHIAKI_LOG_ERROR
						: level == rtc::LogLevel::Warning ? CHIAKI_LOG_WARNING
						: level == rtc::LogLevel::Info ? CHIAKI_LOG_INFO
						: CHIAKI_LOG_DEBUG;
				chiaki_log(bridge_log(), l, "rtc: %s", message.c_str());
			});
}

Relay::Relay(Callbacks cbs_, std::vector<IceServerEntry> ice_servers_)
	: cbs(std::move(cbs_)), ice_servers(std::move(ice_servers_))
{
	sender_thread = std::thread([this] { sender_loop(); });
}

Relay::~Relay()
{
	close();
	{
		std::lock_guard<std::mutex> lock(offer_mutex);
		offer_timer_stop = true;
	}
	offer_cv.notify_all();
	if(offer_timer_thread.joinable())
		offer_timer_thread.join();
	{
		std::lock_guard<std::mutex> lock(queue_mutex);
		stopping = true;
	}
	queue_cv.notify_all();
	if(sender_thread.joinable())
		sender_thread.join();
}

void Relay::setup()
{
	std::lock_guard<std::mutex> lock(pc_mutex);

	rtc::Configuration cfg;
	size_t turn_count = 0;
	for(const auto &entry : ice_servers)
	{
		for(const auto &url : entry.urls)
		{
			// libjuice can do STUN and TURN over UDP only; the browser gets
			// the full list (incl. turns/tcp) via signaling instead.
			bool stun = url.rfind("stun:", 0) == 0;
			bool turn_udp = url.rfind("turn:", 0) == 0 && url.find("transport=udp") != std::string::npos;
			if(!stun && !turn_udp)
				continue;
			try
			{
				rtc::IceServer server(url);
				if(turn_udp)
				{
					server.username = entry.username;
					server.password = entry.credential;
					turn_count++;
				}
				cfg.iceServers.push_back(std::move(server));
			}
			catch(const std::exception &e)
			{
				CHIAKI_LOGW(bridge_log(), "webbridge: skipping ICE server %s: %s", url.c_str(), e.what());
			}
		}
	}
	if(cfg.iceServers.empty())
		cfg.iceServers.emplace_back("stun:stun.l.google.com:19302");
	if(turn_count)
		CHIAKI_LOGI(bridge_log(), "webbridge: relay using %zu TURN server(s)", turn_count);
	// Direct P2P from behind a home router: pin all ICE traffic to one UDP
	// port (CHIAKI_ICE_PORT) so it can be port-forwarded / firewall-pinholed.
	// With that single port reachable from the internet, the bridge's srflx
	// candidate works bidirectionally and no relay is needed.
	if(const char *ip = std::getenv("CHIAKI_ICE_PORT"); ip && *ip)
	{
		// Accept "PORT" or "BEGIN-END". A range (rather than one fixed port)
		// lets libjuice pick a fresh port per connection, avoiding the ICE
		// role-conflict / TURN 437 collisions that fixed-port reuse causes on
		// reconnects. Deliberately NOT enableIceUdpMux: mux demuxes by
		// connection and drops DTLS it can't map; plain per-connection sockets
		// handle STUN + DTLS + media cleanly. Forward this whole UDP range.
		uint16_t begin = 0, end = 0;
		if(const char *dash = strchr(ip, '-'))
		{
			begin = (uint16_t)atoi(ip);
			end = (uint16_t)atoi(dash + 1);
		}
		else
			begin = end = (uint16_t)atoi(ip);
		if(begin && end && end >= begin)
		{
			cfg.portRangeBegin = begin;
			cfg.portRangeEnd = end;
			CHIAKI_LOGI(bridge_log(), "webbridge: pinning ICE to UDP ports %u-%u (forward this range)", begin, end);
		}
	}
	// Cap the MTU so the DTLS handshake (large certificate packets) fragments
	// into datagrams that survive a low-MTU direct path. STUN checks are tiny
	// and always cross, so an MTU black hole shows up as "ICE connects, DTLS
	// stalls" — exactly the direct-path symptom here. The relay path hides it
	// because Cloudflare re-segments.
	if(const char *m = std::getenv("CHIAKI_MTU"); m && *m)
	{
		int mtu = atoi(m);
		if(mtu > 0)
		{
			cfg.mtu = (size_t)mtu;
			CHIAKI_LOGI(bridge_log(), "webbridge: MTU capped at %d", mtu);
		}
	}
	// Bind ICE to one local interface (the forwarded IPv4 LAN address). This
	// drops the IPv6 srflx candidate, which would otherwise half-open for
	// remote clients (residential IPv6 inbound is firewalled) and could be
	// nominated over the working, port-forwarded IPv4 path.
	if(const char *ba = std::getenv("CHIAKI_BIND_ADDR"); ba && *ba)
	{
		cfg.bindAddress = ba;
		CHIAKI_LOGI(bridge_log(), "webbridge: binding ICE to %s", ba);
	}
	// Diagnostics/last-resort: CHIAKI_FORCE_RELAY=1 makes the bridge use only
	// TURN-relayed candidates (mirrors the browser's iceTransportPolicy:relay),
	// which is the path that survives UDP-hostile networks on both ends.
	if(const char *fr = std::getenv("CHIAKI_FORCE_RELAY"); fr && *fr && *fr != '0')
	{
		cfg.iceTransportPolicy = rtc::TransportPolicy::Relay;
		CHIAKI_LOGI(bridge_log(), "webbridge: forcing relay-only ICE transport");
	}
	pc = std::make_shared<rtc::PeerConnection>(cfg);

	pc->onLocalDescription([this](rtc::Description desc) {
		// Don't emit the offer here — wait for gathering (see send_offer_now).
		(void)desc;
	});
	pc->onLocalCandidate([this](rtc::Candidate cand) {
		// Candidates found after the offer was sent trickle normally; those
		// before are already embedded in the offer's SDP.
		if(offer_sent.load() && cbs.signal_out)
			cbs.signal_out(json{{"type", "candidate"}, {"candidate", std::string(cand)}, {"mid", cand.mid()}}.dump());
	});
	pc->onGatheringStateChange([this](rtc::PeerConnection::GatheringState state) {
		if(state == rtc::PeerConnection::GatheringState::Complete)
			send_offer_now();
	});
	pc->onStateChange([this](rtc::PeerConnection::State state) {
		CHIAKI_LOGI(bridge_log(), "webbridge: pc state %d", (int)state);
		if(state == rtc::PeerConnection::State::Connected)
			connected_ = true;
		else if(state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Closed
				|| state == rtc::PeerConnection::State::Disconnected)
		{
			bool was = connected_.exchange(false);
			(void)was;
			if(state != rtc::PeerConnection::State::Disconnected && cbs.closed)
				cbs.closed();
		}
	});

	// --- Audio: RTP Opus track. MUST precede createDataChannel (offer is
	// published there), and the SSRC must be in the description so RTCP finds
	// its way back to the NACK responder.
	{
		rtc::Description::Audio audio("audio", rtc::Description::Direction::SendOnly);
		audio.addOpusCodec(111, "minptime=10;useinbandfec=1;stereo=1;sprop-stereo=1");
		std::random_device rd;
		uint32_t ssrc = rd();
		audio.addSSRC(ssrc, "chiaki-audio");
		audio_track = pc->addTrack(audio);
		audio_rtp_config = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, "chiaki-audio", 111,
				rtc::OpusRtpPacketizer::DefaultClockRate);
		auto packetizer = std::make_shared<rtc::OpusRtpPacketizer>(audio_rtp_config);
		packetizer->addToChain(std::make_shared<rtc::RtcpNackResponder>(64));
		audio_track->setMediaHandler(packetizer);
	}

	// --- Video DC: ordered + 3 retransmits (see PROTOCOL.md for rationale).
	{
		rtc::DataChannelInit init;
		init.reliability.unordered = false;
		init.reliability.maxRetransmits = 3;
		init.negotiated = true;
		init.id = 0;
		video_dc = pc->createDataChannel("video", init);
		video_dc->onOpen([this]() {
			CHIAKI_LOGI(bridge_log(), "webbridge: video DC open");
			std::vector<uint8_t> kf;
			{
				std::lock_guard<std::mutex> lock(pending_mutex);
				kf.swap(pending_keyframe);
			}
			if(!kf.empty())
				send_video(kf.data(), kf.size(), true);
		});
	}

	// --- Input DC: reliable + ordered (defaults).
	{
		rtc::DataChannelInit init;
		init.negotiated = true;
		init.id = 2;
		input_dc = pc->createDataChannel("input", init);
		input_dc->onMessage([this](rtc::message_variant msg) {
			if(std::holds_alternative<std::string>(msg) && cbs.input_json)
				cbs.input_json(std::get<std::string>(msg));
		});
	}

	// --- Mic DC (browser -> bridge): raw 48 kHz mono s16le PCM chunks.
	// Unordered + lossy: a late voice frame is worthless, and the lib's
	// encoder tolerates gaps far better than bursts of stale audio.
	{
		rtc::DataChannelInit init;
		init.reliability.unordered = true;
		init.reliability.maxRetransmits = 0;
		init.negotiated = true;
		init.id = 4;
		mic_dc = pc->createDataChannel("mic", init);
		mic_dc->onMessage([this](rtc::message_variant msg) {
			if(std::holds_alternative<rtc::binary>(msg) && cbs.mic_pcm)
			{
				const auto &b = std::get<rtc::binary>(msg);
				cbs.mic_pcm(reinterpret_cast<const uint8_t *>(b.data()), b.size());
			}
		});
	}

	// Safety net: if gathering stalls (e.g. a STUN/TURN server is slow or
	// unreachable), don't hold the offer forever — send whatever we have and
	// let the rest trickle. TURN allocation is normally done within ~3 s.
	offer_timer_thread = std::thread([this]() {
		std::unique_lock<std::mutex> lock(offer_mutex);
		offer_cv.wait_for(lock, std::chrono::seconds(6), [this] { return offer_timer_stop; });
		lock.unlock();
		if(!offer_sent.load())
		{
			CHIAKI_LOGW(bridge_log(), "webbridge: ICE gathering slow, sending offer with partial candidates");
			send_offer_now();
		}
	});
}

void Relay::send_offer_now()
{
	// Emit the local description (offer) with all candidates gathered so far
	// embedded in the SDP. Idempotent: only the first call wins.
	if(offer_sent.exchange(true))
		return;
	{
		std::lock_guard<std::mutex> lock(offer_mutex);
		offer_timer_stop = true;
	}
	offer_cv.notify_all();

	std::shared_ptr<rtc::PeerConnection> pc_local;
	{
		std::lock_guard<std::mutex> lock(pc_mutex);
		pc_local = pc;
	}
	if(!pc_local)
		return;
	auto desc = pc_local->localDescription();
	if(!desc)
	{
		offer_sent.store(false); // allow a later retry
		return;
	}
	if(cbs.signal_out)
		cbs.signal_out(json{{"type", desc->typeString()}, {"sdp", std::string(*desc)}}.dump());
	CHIAKI_LOGI(bridge_log(), "webbridge: offer sent (gathering %s)",
			pc_local->gatheringState() == rtc::PeerConnection::GatheringState::Complete ? "complete" : "partial");
}

void Relay::handle_signal(const json &msg)
{
	std::lock_guard<std::mutex> lock(pc_mutex);
	if(!pc)
		return;
	const std::string type = msg.value("type", "");
	try
	{
		if(type == "answer")
			pc->setRemoteDescription(rtc::Description(msg.value("sdp", ""), "answer"));
		else if(type == "candidate")
			pc->addRemoteCandidate(rtc::Candidate(msg.value("candidate", ""), msg.value("mid", "")));
	}
	catch(const std::exception &e)
	{
		CHIAKI_LOGE(bridge_log(), "webbridge: signaling error on %s: %s", type.c_str(), e.what());
	}
}

void Relay::close()
{
	std::lock_guard<std::mutex> lock(pc_mutex);
	connected_ = false;
	if(video_dc)
		video_dc->onOpen(nullptr);
	if(input_dc)
		input_dc->onMessage(nullptr);
	if(mic_dc)
		mic_dc->onMessage(nullptr);
	if(pc)
	{
		pc->onStateChange(nullptr);
		pc->onLocalDescription(nullptr);
		pc->onLocalCandidate(nullptr);
		pc->close();
	}
}

void Relay::set_audio_params(unsigned rate, unsigned channels, unsigned samples_per_frame)
{
	// RTP timestamps for Opus always tick at 48 kHz regardless of the coded
	// rate; chiaki streams 48 kHz anyway.
	(void)rate;
	(void)channels;
	audio_samples_per_frame = samples_per_frame ? samples_per_frame : 480;
}

void Relay::send_audio(const uint8_t *data, size_t size)
{
	std::shared_ptr<rtc::Track> track;
	{
		std::lock_guard<std::mutex> lock(pc_mutex);
		track = audio_track;
	}
	if(!track || !track->isOpen())
		return;
	try
	{
		track->send(reinterpret_cast<const std::byte *>(data), size);
		audio_rtp_config->timestamp += audio_samples_per_frame;
	}
	catch(const std::exception &e)
	{
		CHIAKI_LOGE(bridge_log(), "webbridge: audio send failed: %s", e.what());
	}
}

void Relay::send_video(const uint8_t *data, size_t size, bool keyframe)
{
	std::shared_ptr<rtc::DataChannel> dc;
	{
		std::lock_guard<std::mutex> lock(pc_mutex);
		dc = video_dc;
	}
	if(!dc || !dc->isOpen())
	{
		if(keyframe)
		{
			// Hold the newest keyframe so the stream starts decodable the
			// moment the channel opens.
			std::lock_guard<std::mutex> lock(pending_mutex);
			pending_keyframe.assign(data, data + size);
		}
		return;
	}

	Job job;
	job.data.assign(data, data + size);
	job.keyframe = keyframe;
	job.frame_id = next_frame_id.fetch_add(1);
	job.backend_ts = now_ms32();

	bool dropped = false;
	{
		std::lock_guard<std::mutex> lock(queue_mutex);
		if(queue.size() >= kMaxQueuedFrames)
		{
			// Drop oldest delta frames; keyframes stay.
			for(auto it = queue.begin(); it != queue.end();)
			{
				if(!it->keyframe)
				{
					it = queue.erase(it);
					dropped = true;
					if(queue.size() < kMaxQueuedFrames)
						break;
				}
				else
					++it;
			}
		}
		queue.push_back(std::move(job));
	}
	queue_cv.notify_one();
	if(dropped && cbs.need_idr)
	{
		CHIAKI_LOGW(bridge_log(), "webbridge: video send queue overrun, dropped deltas and requesting IDR");
		cbs.need_idr();
	}
}

void Relay::sender_loop()
{
	for(;;)
	{
		Job job;
		{
			std::unique_lock<std::mutex> lock(queue_mutex);
			queue_cv.wait(lock, [this] { return stopping || !queue.empty(); });
			if(stopping)
				return;
			job = std::move(queue.front());
			queue.pop_front();
		}
		send_job(job);
	}
}

void Relay::send_job(const Job &job)
{
	std::shared_ptr<rtc::DataChannel> dc;
	{
		std::lock_guard<std::mutex> lock(pc_mutex);
		dc = video_dc;
	}
	if(!dc || !dc->isOpen())
		return;

	const size_t total = job.data.size();
	const size_t chunks = (total + kMaxPayloadSize - 1) / kMaxPayloadSize;
	for(size_t ci = 0; ci < chunks; ci++)
	{
		const size_t off = ci * kMaxPayloadSize;
		const size_t payload = std::min(kMaxPayloadSize, total - off);
		std::vector<std::byte> buf(kFragHeaderSize + payload);
		auto w32 = [&buf](size_t at, uint32_t v) {
			buf[at] = (std::byte)((v >> 24) & 0xff);
			buf[at + 1] = (std::byte)((v >> 16) & 0xff);
			buf[at + 2] = (std::byte)((v >> 8) & 0xff);
			buf[at + 3] = (std::byte)(v & 0xff);
		};
		w32(0, job.frame_id);
		buf[4] = (std::byte)(((uint16_t)ci >> 8) & 0xff);
		buf[5] = (std::byte)((uint16_t)ci & 0xff);
		buf[6] = (std::byte)(((uint16_t)chunks >> 8) & 0xff);
		buf[7] = (std::byte)((uint16_t)chunks & 0xff);
		buf[8] = (std::byte)(job.keyframe ? 1 : 0);
		w32(9, (uint32_t)payload);
		w32(13, job.backend_ts);
		memcpy(buf.data() + kFragHeaderSize, job.data.data() + off, payload);
		try
		{
			dc->send(buf.data(), buf.size());
		}
		catch(const std::exception &e)
		{
			CHIAKI_LOGE(bridge_log(), "webbridge: video send failed: %s", e.what());
			return;
		}
	}
}

void Relay::send_input_json(const std::string &s)
{
	std::shared_ptr<rtc::DataChannel> dc;
	{
		std::lock_guard<std::mutex> lock(pc_mutex);
		dc = input_dc;
	}
	if(!dc || !dc->isOpen())
		return;
	try
	{
		dc->send(s);
	}
	catch(const std::exception &)
	{
	}
}
