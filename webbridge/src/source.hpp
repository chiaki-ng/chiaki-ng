// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#pragma once

#include <cstdint>
#include <functional>
#include <string>

// Everything a media source (real chiaki session or demo generator) pushes
// out to the transport. Callbacks may be invoked from arbitrary source
// threads; the implementation behind them must be thread-safe.
struct SourceSink
{
	// One complete Annex-B access unit.
	std::function<void(const uint8_t *buf, size_t size, bool keyframe)> video_frame;
	// Opus frame, still encoded. samples_per_frame at rate hz (from stream header).
	std::function<void(unsigned rate, unsigned channels, unsigned samples_per_frame)> audio_header;
	std::function<void(const uint8_t *buf, size_t size)> audio_frame;
	// Out-of-band event for the browser, already formatted as input-DC JSON text.
	std::function<void(const std::string &json)> event;
	// Session ended. error=false for clean stop.
	std::function<void(const std::string &reason, bool error)> quit;
};

class Source
{
public:
	virtual ~Source() = default;
	// Sink must outlive the source or be safely disconnectable via stop().
	virtual bool start(SourceSink sink, std::string &error) = 0;
	virtual void stop() = 0;
	// Ask the source to produce a recovery keyframe soon.
	virtual void request_idr() = 0;
	// Input-DC JSON from the browser ("cs", "mo", "pin", "micOn", ...).
	virtual void handle_input(const std::string &json) = 0;
	// Microphone PCM from the browser: 48 kHz mono s16le, any whole number of
	// samples per call (the source re-frames as needed).
	virtual void handle_mic_pcm(const uint8_t *buf, size_t size)
	{
		(void)buf;
		(void)size;
	}
};

struct StreamProfile
{
	unsigned width = 1280;
	unsigned height = 720;
	unsigned fps = 60;
	unsigned bitrate = 0; // kbps, 0 = preset default
	bool hevc = false;
};
