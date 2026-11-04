// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#pragma once

#include "config.hpp"
#include "source.hpp"

#include <chiaki/opusencoder.h>
#include <chiaki/session.h>

#include <atomic>
#include <mutex>

// Runs a real Remote Play session against a registered console and adapts
// libchiaki's callbacks to the SourceSink interface.
class ChiakiSource : public Source
{
public:
	ChiakiSource(ConsoleConfig console, StreamProfile profile, const std::array<uint8_t, 8> &psn_account_id);
	~ChiakiSource() override;

	bool start(SourceSink sink, std::string &error) override;
	void stop() override;
	void request_idr() override;
	void handle_input(const std::string &json) override;
	void handle_mic_pcm(const uint8_t *buf, size_t size) override;

	bool hevc() const { return profile.hevc; }

private:
	static bool video_cb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user);
	static void audio_header_cb(ChiakiAudioHeader *header, void *user);
	static void audio_frame_cb(uint8_t *buf, size_t buf_size, void *user);
	static void event_cb(ChiakiEvent *event, void *user);

	ConsoleConfig console;
	StreamProfile profile;
	std::array<uint8_t, 8> psn_account_id;

	ChiakiSession session{};
	ChiakiAudioSink audio_sink{};
	bool session_inited = false;
	std::atomic<bool> started{false};
	// cached SPS/PPS(/VPS) with start codes; written and read only on the
	// video callback thread
	std::vector<uint8_t> param_sets;

	SourceSink sink;

	std::mutex state_mutex;
	ChiakiControllerState controller_state{};

	void set_mic_enabled(bool enabled);

	// microphone: browser PCM -> lib Opus encoder -> audio sender
	ChiakiOpusEncoder opus_encoder{};
	bool opus_encoder_inited = false;
	std::mutex mic_mutex;
	bool mic_connected = false;      // connect_microphone sent
	std::atomic<bool> mic_live{false}; // unmuted & accepting PCM
	std::vector<int16_t> mic_pending; // mono samples not yet framed
	std::vector<int16_t> mic_stereo;  // scratch: mono -> interleaved stereo
};
