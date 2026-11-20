// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "chiakisource.hpp"
#include "log.hpp"
#include "nalparse.hpp"

#include <chiaki/videoreceiver.h>

#include <nlohmann/json.hpp>

#include <cstring>

using nlohmann::json;

ChiakiSource::ChiakiSource(ConsoleConfig console_, StreamProfile profile_, const std::array<uint8_t, 8> &psn_account_id_)
	: console(std::move(console_)), profile(profile_), psn_account_id(psn_account_id_)
{
	chiaki_controller_state_set_idle(&controller_state);
}

ChiakiSource::~ChiakiSource()
{
	stop();
}

bool ChiakiSource::start(SourceSink sink_, std::string &error)
{
	sink = std::move(sink_);

	ChiakiConnectInfo info = {};
	info.ps5 = console.ps5;
	info.host = console.host.c_str();
	static_assert(sizeof(info.regist_key) == 16, "regist key size");
	memcpy(info.regist_key, console.regist_key.data(), sizeof(info.regist_key));
	memcpy(info.morning, console.rp_key.data(), sizeof(info.morning));
	memcpy(info.psn_account_id, psn_account_id.data(), sizeof(info.psn_account_id));
	info.video_profile.width = profile.width;
	info.video_profile.height = profile.height;
	info.video_profile.max_fps = profile.fps;
	info.video_profile.codec = profile.hevc ? CHIAKI_CODEC_H265 : CHIAKI_CODEC_H264;
	// bitrate presets follow the GUI's mapping loosely; console clamps anyway
	unsigned kbps = profile.bitrate;
	if(!kbps)
	{
		if(profile.height >= 1080)
			kbps = 15000;
		else if(profile.height >= 720)
			kbps = 10000;
		else
			kbps = 4000;
	}
	info.video_profile.bitrate = kbps;
	info.video_profile_auto_downgrade = true;
	info.enable_keyboard = false;
	info.enable_dualsense = true;
	info.audio_video_disabled = CHIAKI_NONE_DISABLED;
	info.packet_loss_max = 0.05;
	info.enable_idr_on_fec_failure = true;

	ChiakiErrorCode err = chiaki_session_init(&session, &info, bridge_log());
	if(err != CHIAKI_ERR_SUCCESS)
	{
		error = std::string("session init failed: ") + chiaki_error_string(err);
		return false;
	}
	session_inited = true;

	chiaki_session_set_video_sample_cb(&session, video_cb, this);
	audio_sink.user = this;
	audio_sink.header_cb = audio_header_cb;
	audio_sink.frame_cb = audio_frame_cb;
	chiaki_session_set_audio_sink(&session, &audio_sink);
	chiaki_session_set_event_cb(&session, event_cb, this);

	// Microphone: lib-side Opus encoder + audio sender, fed with PCM from the
	// browser. Same parameters the Qt GUI uses (2ch/16bit/48kHz, 480-sample
	// frames — the console requires the resulting exact 40-byte Opus frames).
	chiaki_opus_encoder_init(&opus_encoder, bridge_log());
	opus_encoder_inited = true;
	ChiakiAudioHeader mic_header;
	chiaki_audio_header_set(&mic_header, 2, 16, 48000, 480);
	chiaki_opus_encoder_header(&mic_header, &opus_encoder, &session);

	err = chiaki_session_start(&session);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		error = std::string("session start failed: ") + chiaki_error_string(err);
		chiaki_session_fini(&session);
		session_inited = false;
		return false;
	}
	started = true;
	CHIAKI_LOGI(bridge_log(), "webbridge: session started to %s (%s, %ux%u@%u %s)",
			console.host.c_str(), console.ps5 ? "PS5" : "PS4",
			profile.width, profile.height, profile.fps, profile.hevc ? "hevc" : "h264");
	return true;
}

void ChiakiSource::stop()
{
	mic_live = false;
	if(started.exchange(false))
	{
		chiaki_session_stop(&session);
		chiaki_session_join(&session);
	}
	if(opus_encoder_inited)
	{
		std::lock_guard<std::mutex> lock(mic_mutex);
		chiaki_opus_encoder_fini(&opus_encoder);
		opus_encoder_inited = false;
	}
	if(session_inited)
	{
		chiaki_session_fini(&session);
		session_inited = false;
	}
}

void ChiakiSource::request_idr()
{
	if(!started)
		return;
	// Explicitly ask the console for an IDR (the desktop client's path). The
	// console uses long GOPs during gameplay, so it will NOT emit a keyframe
	// on its own — only this Takion IDRREQUEST reliably produces one. (The
	// old approach of just returning false from the video callback merely
	// reports a corrupt frame and does not force a keyframe here.)
	ChiakiErrorCode err = chiaki_session_request_idr(&session);
	if(err == CHIAKI_ERR_SUCCESS)
	{
		// Discard deltas until the IDR lands so libchiaki stops logging
		// "missing reference frame" for every frame in the gap.
		if(session.stream_connection.video_receiver)
			chiaki_video_receiver_set_waiting_for_idr(session.stream_connection.video_receiver, true);
	}
	else
		CHIAKI_LOGW(bridge_log(), "webbridge: IDR request failed: %s", chiaki_error_string(err));
}

bool ChiakiSource::video_cb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user)
{
	auto *self = static_cast<ChiakiSource *>(user);
	NalScanResult scan = scan_annexb(buf, buf_size, self->profile.hevc);

	// The console sends SPS/PPS(/VPS) only once, in a separate header AU at
	// stream start. A browser whose DataChannel opens later (or that drops
	// that one frame) can never configure its decoder. Cache the parameter
	// sets and prepend them to every keyframe that lacks them, so any
	// keyframe is a valid decoder entry point.
	if(scan.parameter_sets)
		self->param_sets = extract_param_sets(buf, buf_size, self->profile.hevc);
	if(self->sink.video_frame)
	{
		if(scan.keyframe && !scan.parameter_sets && !self->param_sets.empty())
		{
			std::vector<uint8_t> au;
			au.reserve(self->param_sets.size() + buf_size);
			au.insert(au.end(), self->param_sets.begin(), self->param_sets.end());
			au.insert(au.end(), buf, buf + buf_size);
			self->sink.video_frame(au.data(), au.size(), true);
		}
		else
			self->sink.video_frame(buf, buf_size, scan.keyframe || scan.parameter_sets);
	}
	(void)frames_lost;
	(void)frame_recovered;
	return true;
}

void ChiakiSource::audio_header_cb(ChiakiAudioHeader *header, void *user)
{
	auto *self = static_cast<ChiakiSource *>(user);
	CHIAKI_LOGI(bridge_log(), "webbridge: audio header rate=%u ch=%u frame=%u",
			(unsigned)header->rate, (unsigned)header->channels, (unsigned)header->frame_size);
	if(self->sink.audio_header)
		self->sink.audio_header(header->rate, header->channels, header->frame_size);
}

void ChiakiSource::audio_frame_cb(uint8_t *buf, size_t buf_size, void *user)
{
	auto *self = static_cast<ChiakiSource *>(user);
	if(self->sink.audio_frame)
		self->sink.audio_frame(buf, buf_size);
}

void ChiakiSource::event_cb(ChiakiEvent *event, void *user)
{
	auto *self = static_cast<ChiakiSource *>(user);
	auto send = [self](const json &j) {
		if(self->sink.event)
			self->sink.event(j.dump());
	};
	switch(event->type)
	{
		case CHIAKI_EVENT_CONNECTED:
			CHIAKI_LOGI(bridge_log(), "webbridge: session connected");
			break;
		case CHIAKI_EVENT_RUMBLE:
			send({{"t", "rumble"}, {"l", event->rumble.left}, {"r", event->rumble.right}});
			break;
		case CHIAKI_EVENT_LED_COLOR:
			send({{"t", "led"}, {"r", event->led_state[0]}, {"g", event->led_state[1]}, {"b", event->led_state[2]}});
			break;
		case CHIAKI_EVENT_LOGIN_PIN_REQUEST:
			send({{"t", "pinRequest"}, {"incorrect", event->login_pin_request.pin_incorrect}});
			break;
		case CHIAKI_EVENT_NICKNAME_RECEIVED:
			send({{"t", "nickname"}, {"name", std::string(event->server_nickname, strnlen(event->server_nickname, sizeof(event->server_nickname)))}});
			break;
		case CHIAKI_EVENT_QUIT:
		{
			const char *reason = chiaki_quit_reason_string(event->quit.reason);
			bool error = chiaki_quit_reason_is_error(event->quit.reason);
			CHIAKI_LOGI(bridge_log(), "webbridge: session quit: %s", reason);
			if(self->sink.quit)
				self->sink.quit(reason ? reason : "unknown", error);
			break;
		}
		default:
			break;
	}
}

void ChiakiSource::handle_input(const std::string &msg)
{
	if(!started)
		return;
	json j;
	try
	{
		j = json::parse(msg);
	}
	catch(const std::exception &)
	{
		return;
	}
	const std::string t = j.value("t", "");
	if(t == "cs")
	{
		std::lock_guard<std::mutex> lock(state_mutex);
		ChiakiControllerState &s = controller_state;
		uint32_t new_buttons = j.value("b", 0u);
		if(new_buttons != s.buttons)
			CHIAKI_LOGI(bridge_log(), "webbridge: buttons 0x%08x -> 0x%08x", s.buttons, new_buttons);
		s.buttons = new_buttons;
		s.l2_state = (uint8_t)j.value("l2", 0);
		s.r2_state = (uint8_t)j.value("r2", 0);
		s.left_x = (int16_t)j.value("lx", 0);
		s.left_y = (int16_t)j.value("ly", 0);
		s.right_x = (int16_t)j.value("rx", 0);
		s.right_y = (int16_t)j.value("ry", 0);
		for(auto &touch : s.touches)
			touch.id = -1;
		if(j.contains("tp") && j["tp"].is_array())
		{
			size_t i = 0;
			for(const auto &tp : j["tp"])
			{
				if(i >= CHIAKI_CONTROLLER_TOUCHES_MAX || !tp.is_array() || tp.size() < 3)
					break;
				s.touches[i].id = (int8_t)((int)tp[0] & 0x7f);
				s.touches[i].x = (uint16_t)std::min<int>(std::max<int>((int)tp[1], 0), 1920);
				s.touches[i].y = (uint16_t)std::min<int>(std::max<int>((int)tp[2], 0), 942);
				i++;
			}
		}
		chiaki_session_set_controller_state(&session, &s);
	}
	else if(t == "mo")
	{
		std::lock_guard<std::mutex> lock(state_mutex);
		ChiakiControllerState &s = controller_state;
		s.gyro_x = j.value("gx", 0.0f);
		s.gyro_y = j.value("gy", 0.0f);
		s.gyro_z = j.value("gz", 0.0f);
		s.accel_x = j.value("ax", 0.0f);
		s.accel_y = j.value("ay", 1.0f);
		s.accel_z = j.value("az", 0.0f);
		s.orient_x = j.value("ox", 0.0f);
		s.orient_y = j.value("oy", 0.0f);
		s.orient_z = j.value("oz", 0.0f);
		s.orient_w = j.value("ow", 1.0f);
		chiaki_session_set_controller_state(&session, &s);
	}
	else if(t == "idr")
	{
		request_idr();
	}
	else if(t == "micOn")
	{
		set_mic_enabled(true);
	}
	else if(t == "micOff")
	{
		set_mic_enabled(false);
	}
	else if(t == "pin")
	{
		std::string pin = j.value("pin", "");
		if(!pin.empty())
			chiaki_session_set_login_pin(&session, (const uint8_t *)pin.data(), pin.size());
	}
}

void ChiakiSource::set_mic_enabled(bool enabled)
{
	if(!started)
		return;
	std::lock_guard<std::mutex> lock(mic_mutex);
	if(enabled)
	{
		if(!mic_connected)
		{
			chiaki_session_connect_microphone(&session);
			mic_connected = true;
		}
		if(!mic_live)
		{
			// toggle_microphone takes the state being LEFT: true = "was
			// muted" = unmute now (see ctrl_message_toggle_microphone)
			chiaki_session_toggle_microphone(&session, true);
			mic_live = true;
			CHIAKI_LOGI(bridge_log(), "webbridge: microphone unmuted");
		}
	}
	else if(mic_live)
	{
		chiaki_session_toggle_microphone(&session, false);
		mic_live = false;
		mic_pending.clear();
		CHIAKI_LOGI(bridge_log(), "webbridge: microphone muted");
	}
}

void ChiakiSource::handle_mic_pcm(const uint8_t *buf, size_t size)
{
	if(!started || !mic_live)
		return;
	std::lock_guard<std::mutex> lock(mic_mutex);
	if(!opus_encoder_inited || !opus_encoder.opus_encoder)
		return;

	// Append incoming mono samples (drop a trailing odd byte, tolerate any
	// chunking) and emit full 480-sample frames, duplicated to stereo for the
	// encoder's 2-channel header.
	const size_t samples = size / 2;
	const int16_t *pcm = reinterpret_cast<const int16_t *>(buf);
	mic_pending.insert(mic_pending.end(), pcm, pcm + samples);

	constexpr size_t frame = 480;
	size_t off = 0;
	while(mic_pending.size() - off >= frame)
	{
		mic_stereo.resize(frame * 2);
		for(size_t i = 0; i < frame; i++)
		{
			mic_stereo[i * 2] = mic_pending[off + i];
			mic_stereo[i * 2 + 1] = mic_pending[off + i];
		}
		chiaki_opus_encoder_frame(mic_stereo.data(), &opus_encoder);
		off += frame;
	}
	mic_pending.erase(mic_pending.begin(), mic_pending.begin() + off);
	// Bound memory if the browser floods us faster than real time.
	if(mic_pending.size() > 48000)
		mic_pending.clear();
}
