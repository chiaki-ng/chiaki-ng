// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#pragma once

#include "source.hpp"

#include <atomic>
#include <cstdio>
#include <thread>

// Test source needing no console: H.264 test pattern from the ffmpeg CLI
// (Annex-B, AUD-delimited) plus an Opus test tone from libopus. Exercises the
// whole browser pipeline (fragmentation, WebCodecs decode, RTP Opus audio).
class DemoSource : public Source
{
public:
	explicit DemoSource(StreamProfile profile);
	~DemoSource() override;

	bool start(SourceSink sink, std::string &error) override;
	void stop() override;
	void request_idr() override;
	void handle_input(const std::string &json) override;

private:
	void video_loop();
	void audio_loop();

	StreamProfile profile;
	SourceSink sink;
	std::atomic<bool> running{false};
	FILE *ffmpeg = nullptr;
	std::thread video_thread;
	std::thread audio_thread;
};
