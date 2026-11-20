// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "demosource.hpp"
#include "log.hpp"
#include "nalparse.hpp"

#include <opus/opus.h>

#include <poll.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

DemoSource::DemoSource(StreamProfile profile_)
	: profile(profile_)
{
}

DemoSource::~DemoSource()
{
	stop();
}

bool DemoSource::start(SourceSink sink_, std::string &error)
{
	sink = std::move(sink_);

	char cmd[512];
	snprintf(cmd, sizeof(cmd),
			"exec ffmpeg -loglevel error -re -f lavfi "
			"-i testsrc2=size=%ux%u:rate=%u "
			"-pix_fmt yuv420p -c:v libx264 -preset veryfast -tune zerolatency "
			"-profile:v high -g %u -x264-params scenecut=0:repeat-headers=1 "
			"-bsf:v h264_metadata=aud=insert -f h264 -",
			profile.width, profile.height, profile.fps, profile.fps * 2);
	ffmpeg = popen(cmd, "r");
	if(!ffmpeg)
	{
		error = "failed to start ffmpeg for demo mode";
		return false;
	}
	running = true;
	video_thread = std::thread([this] { video_loop(); });
	audio_thread = std::thread([this] { audio_loop(); });
	CHIAKI_LOGI(bridge_log(), "webbridge: demo source started (%ux%u@%u)", profile.width, profile.height, profile.fps);
	return true;
}

void DemoSource::stop()
{
	if(!running.exchange(false))
		return;
	// video_loop polls `running` and exits promptly; only then reap ffmpeg
	if(video_thread.joinable())
		video_thread.join();
	if(audio_thread.joinable())
		audio_thread.join();
	if(ffmpeg)
	{
		pclose(ffmpeg);
		ffmpeg = nullptr;
	}
}

void DemoSource::request_idr()
{
	// x264 emits periodic IDR every g frames; nothing to do.
}

void DemoSource::handle_input(const std::string &json)
{
	// demo mode ignores input, but confirm the channel works once
	static std::atomic<bool> logged{false};
	if(!logged.exchange(true))
		CHIAKI_LOGI(bridge_log(), "webbridge: input DC delivering (first message: %.100s)", json.c_str());
}

// Scan for an Access Unit Delimiter NAL (type 9) start; AUs run AUD→AUD.
static size_t find_aud(const std::vector<uint8_t> &buf, size_t from)
{
	if(buf.size() < 5)
		return SIZE_MAX;
	for(size_t i = from; i + 4 < buf.size(); i++)
	{
		if(buf[i] == 0 && buf[i + 1] == 0)
		{
			if(buf[i + 2] == 1 && (buf[i + 3] & 0x1f) == 9)
				return i;
			if(i + 5 < buf.size() && buf[i + 2] == 0 && buf[i + 3] == 1 && (buf[i + 4] & 0x1f) == 9)
				return i;
		}
	}
	return SIZE_MAX;
}

void DemoSource::video_loop()
{
	std::vector<uint8_t> buf;
	buf.reserve(1 << 20);
	uint8_t chunk[16384];
	const int fd = fileno(ffmpeg);
	while(running)
	{
		struct pollfd pfd = {fd, POLLIN, 0};
		int pr = poll(&pfd, 1, 200);
		if(pr == 0)
			continue; // timeout: re-check running
		ssize_t n = pr > 0 ? read(fd, chunk, sizeof(chunk)) : -1;
		if(n <= 0)
		{
			if(running && sink.quit)
				sink.quit("demo video source ended", true);
			return;
		}
		buf.insert(buf.end(), chunk, chunk + n);

		// emit every complete AU currently in the buffer (AUD → next AUD)
		for(;;)
		{
			size_t first = find_aud(buf, 0);
			if(first == SIZE_MAX)
				break;
			size_t next = find_aud(buf, first + 4);
			if(next == SIZE_MAX)
				break;
			NalScanResult scan = scan_annexb(buf.data() + first, next - first, false);
			if(sink.video_frame)
				sink.video_frame(buf.data() + first, next - first, scan.keyframe || scan.parameter_sets);
			buf.erase(buf.begin(), buf.begin() + next);
		}
	}
}

void DemoSource::audio_loop()
{
	constexpr unsigned rate = 48000;
	constexpr unsigned channels = 2;
	constexpr unsigned frame_samples = 960; // 20 ms

	if(sink.audio_header)
		sink.audio_header(rate, channels, frame_samples);

	int err = 0;
	OpusEncoder *enc = opus_encoder_create(rate, channels, OPUS_APPLICATION_AUDIO, &err);
	if(!enc || err != OPUS_OK)
	{
		CHIAKI_LOGE(bridge_log(), "webbridge: opus encoder init failed (%d)", err);
		return;
	}
	opus_encoder_ctl(enc, OPUS_SET_BITRATE(96000));

	std::vector<int16_t> pcm(frame_samples * channels);
	std::vector<uint8_t> out(1500);
	double phase_l = 0, phase_r = 0;
	auto next_tick = std::chrono::steady_clock::now();
	uint64_t t = 0;
	while(running)
	{
		// gentle two-tone melody so audio sync/quality is audible
		double f_l = 329.63 * (1.0 + 0.25 * ((t / 48) % 2));
		double f_r = 220.0;
		for(unsigned i = 0; i < frame_samples; i++)
		{
			pcm[i * 2] = (int16_t)(std::sin(phase_l) * 6000);
			pcm[i * 2 + 1] = (int16_t)(std::sin(phase_r) * 6000);
			phase_l += 2 * M_PI * f_l / rate;
			phase_r += 2 * M_PI * f_r / rate;
		}
		int n = opus_encode(enc, pcm.data(), frame_samples, out.data(), (opus_int32)out.size());
		if(n > 0 && sink.audio_frame)
			sink.audio_frame(out.data(), (size_t)n);
		t++;
		next_tick += std::chrono::milliseconds(20);
		std::this_thread::sleep_until(next_tick);
	}
	opus_encoder_destroy(enc);
}
