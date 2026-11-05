// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <dualsensebtaudio.h>

#include <QDir>
#include <QFile>
#include <QMutexLocker>

#include <opus/opus.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>

// Older libopus has no RESTRICTED_CELT, which is the same CELT-only coding path
#ifndef OPUS_APPLICATION_RESTRICTED_CELT
#define OPUS_APPLICATION_RESTRICTED_CELT OPUS_APPLICATION_RESTRICTED_LOWDELAY
#endif

#define REPORT_PERIOD_NS (1000000000LL * CHIAKI_DUALSENSE_BT_HAPTICS_FRAMES / CHIAKI_DUALSENSE_BT_HAPTICS_RATE)
#define OPUS_BITRATE (CHIAKI_DUALSENSE_BT_OPUS_FRAME_SIZE * 8 * 100)
#define SPEAKER_SAMPLES_PER_REPORT (CHIAKI_DUALSENSE_BT_OPUS_FRAMES * CHIAKI_DUALSENSE_BT_OPUS_FRAME_SAMPLES)
#define SPEAKER_ROUTE_INTERVAL_REPORTS 47 // ~1 s
#define MAX_CONSECUTIVE_WRITE_FAILURES 25
#define MAX_LATE_REPORTS 3
// The Edge isn't known to take these reports, so it keeps rumble haptics
#define DUALSENSE_BT_HID_ID "0005:0000054C:00000CE6"

static void TimespecAddNs(struct timespec *ts, long long ns)
{
	ns += ts->tv_nsec;
	ts->tv_sec += ns / 1000000000LL;
	ts->tv_nsec = ns % 1000000000LL;
}

DualSenseBtAudio::DualSenseBtAudio(ChiakiLog *log)
	: log(log)
{
}

DualSenseBtAudio::~DualSenseBtAudio()
{
	Stop();
}

QString DualSenseBtAudio::FindDevice()
{
	QDir dir("/sys/class/hidraw");
	const auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
	for(const auto &entry : entries)
	{
		QFile uevent(dir.filePath(entry) + "/device/uevent");
		if(!uevent.open(QIODevice::ReadOnly))
			continue;
		if(uevent.readAll().contains("HID_ID=" DUALSENSE_BT_HID_ID))
			return "/dev/" + entry;
	}
	return QString();
}

bool DualSenseBtAudio::Start(const QString &device, bool speaker)
{
	Stop();
	fd = open(qPrintable(device), O_WRONLY | O_CLOEXEC);
	if(fd < 0)
	{
		CHIAKI_LOGW(log, "Could not open %s for DualSense Bluetooth audio: %s", qPrintable(device), strerror(errno));
		return false;
	}
	// A second descriptor as only this one mustn't block; reading doesn't take reports from SDL
	input_fd = open(qPrintable(device), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if(input_fd < 0)
		CHIAKI_LOGW(log, "Could not open %s to detect DualSense headphones: %s", qPrintable(device), strerror(errno));
	headphones = false;
	int error = OPUS_OK;
	encoder = opus_encoder_create(48000, 2, OPUS_APPLICATION_RESTRICTED_CELT, &error);
	if(error != OPUS_OK)
	{
		CHIAKI_LOGE(log, "Could not create the DualSense Bluetooth audio encoder: %s", opus_strerror(error));
		encoder = nullptr;
		close(fd);
		fd = -1;
		if(input_fd >= 0)
		{
			close(input_fd);
			input_fd = -1;
		}
		return false;
	}
	opus_encoder_ctl(encoder, OPUS_SET_EXPERT_FRAME_DURATION(OPUS_FRAMESIZE_10_MS));
	opus_encoder_ctl(encoder, OPUS_SET_BITRATE(OPUS_BITRATE));
	opus_encoder_ctl(encoder, OPUS_SET_VBR(0));
	this->speaker = speaker;
	ResetBuffers();
	stop_requested = false;
	active = true;
	QThread::start(QThread::TimeCriticalPriority);
	CHIAKI_LOGI(log, "DualSense Bluetooth audio started on %s, speaker %s", qPrintable(device), speaker ? "on" : "off");
	return true;
}

void DualSenseBtAudio::Stop()
{
	stop_requested = true;
	wait();
	active = false;
	if(fd >= 0)
	{
		if(speaker)
			SendOutputRoute(CHIAKI_DUALSENSE_BT_OUTPUT_DEFAULT);
		close(fd);
		fd = -1;
	}
	if(input_fd >= 0)
	{
		close(input_fd);
		input_fd = -1;
	}
	if(encoder)
	{
		opus_encoder_destroy(encoder);
		encoder = nullptr;
	}
}

void DualSenseBtAudio::ResetBuffers()
{
	QMutexLocker locker(&mutex);
	speaker_read = speaker_fill = 0;
	speaker_playing = false;
	haptics_read = haptics_fill = 0;
	audio_pos = 0.0;
	audio_last[0] = audio_last[1] = 0;
}

void DualSenseBtAudio::SetAudioFormat(unsigned int channels, unsigned int rate)
{
	QMutexLocker locker(&mutex);
	audio_channels = channels;
	audio_step = static_cast<double>(rate) / CHIAKI_DUALSENSE_BT_SPEAKER_RATE;
}

void DualSenseBtAudio::PushHaptics(const uint8_t *buf, size_t frames, float gain)
{
	if(!active)
		return;
	QMutexLocker locker(&mutex);
	for(size_t i = 0; i < frames * 2; i++)
	{
		int16_t sample;
		memcpy(&sample, buf + i * sizeof(sample), sizeof(sample));
		const int8_t value = static_cast<int8_t>(std::clamp(static_cast<int>(sample * gain) / 256, -128, 127));
		if(haptics_fill == HAPTICS_RING_SIZE)
		{
			// Drop the oldest so that haptics never fall behind the picture
			haptics_read = (haptics_read + 1) % HAPTICS_RING_SIZE;
			haptics_fill--;
		}
		haptics_ring[(haptics_read + haptics_fill) % HAPTICS_RING_SIZE] = value;
		haptics_fill++;
	}
}

void DualSenseBtAudio::PushAudio(const int16_t *buf, size_t frames)
{
	if(!active || !speaker)
		return;
	QMutexLocker locker(&mutex);
	if(!audio_channels)
		return;
	for(size_t i = 0; i < frames; i++)
	{
		const int16_t *frame = buf + i * audio_channels;
		const int16_t left = frame[0];
		const int16_t right = audio_channels > 1 ? frame[1] : frame[0];
		while(audio_pos < 1.0)
		{
			if(speaker_fill == SPEAKER_RING_SIZE)
			{
				speaker_read = (speaker_read + 1) % SPEAKER_RING_SIZE;
				speaker_fill--;
			}
			const size_t pos = (speaker_read + speaker_fill) % SPEAKER_RING_SIZE;
			speaker_ring[pos * 2] = static_cast<int16_t>(audio_last[0] + (left - audio_last[0]) * audio_pos);
			speaker_ring[pos * 2 + 1] = static_cast<int16_t>(audio_last[1] + (right - audio_last[1]) * audio_pos);
			speaker_fill++;
			audio_pos += audio_step;
		}
		audio_pos -= 1.0;
		audio_last[0] = left;
		audio_last[1] = right;
	}
}

void DualSenseBtAudio::TakeHaptics(uint8_t *out)
{
	QMutexLocker locker(&mutex);
	const size_t count = std::min<size_t>(haptics_fill, CHIAKI_DUALSENSE_BT_HAPTICS_SIZE);
	for(size_t i = 0; i < count; i++)
		out[i] = static_cast<uint8_t>(haptics_ring[(haptics_read + i) % HAPTICS_RING_SIZE]);
	memset(out + count, 0, CHIAKI_DUALSENSE_BT_HAPTICS_SIZE - count);
	haptics_read = (haptics_read + count) % HAPTICS_RING_SIZE;
	haptics_fill -= count;
}

void DualSenseBtAudio::TakeAudio(int16_t *out)
{
	QMutexLocker locker(&mutex);
	// Wait for a full report after running dry instead of playing fragments
	if(!speaker_playing && speaker_fill >= SPEAKER_SAMPLES_PER_REPORT)
		speaker_playing = true;
	size_t count = 0;
	if(speaker_playing)
	{
		count = std::min<size_t>(speaker_fill, SPEAKER_SAMPLES_PER_REPORT);
		for(size_t i = 0; i < count; i++)
		{
			const size_t pos = (speaker_read + i) % SPEAKER_RING_SIZE;
			const int16_t left = speaker_ring[pos * 2];
			const int16_t right = speaker_ring[pos * 2 + 1];
			if(headphones)
			{
				out[i * 2] = left;
				out[i * 2 + 1] = right;
			}
			else // The pad's single speaker takes one channel
				out[i * 2] = out[i * 2 + 1] = static_cast<int16_t>((left + right) / 2);
		}
		speaker_read = (speaker_read + count) % SPEAKER_RING_SIZE;
		speaker_fill -= count;
		if(count < SPEAKER_SAMPLES_PER_REPORT)
			speaker_playing = false;
	}
	memset(out + count * 2, 0, (SPEAKER_SAMPLES_PER_REPORT - count) * 2 * sizeof(int16_t));
}

void DualSenseBtAudio::PollHeadphones()
{
	if(input_fd < 0)
		return;
	uint8_t report[CHIAKI_DUALSENSE_BT_STATE_REPORT_SIZE];
	bool seen = false;
	bool plugged = false;
	ssize_t size;
	// Only the newest report matters, but all that arrived since the last poll have to be drained
	while((size = read(input_fd, report, sizeof(report))) > 0)
	{
		if(!chiaki_dualsense_bt_input_has_status(report, size))
			continue;
		seen = true;
		plugged = chiaki_dualsense_bt_input_headphones(report, size);
	}
	if(!seen || plugged == headphones)
		return;
	headphones = plugged;
	CHIAKI_LOGI(log, "DualSense headphones %s", headphones ? "plugged in" : "unplugged");
	SendOutputRoute(headphones ? CHIAKI_DUALSENSE_BT_OUTPUT_HEADPHONES : CHIAKI_DUALSENSE_BT_OUTPUT_SPEAKER);
}

bool DualSenseBtAudio::SendReport(const uint8_t *report, size_t size)
{
	return write(fd, report, size) == static_cast<ssize_t>(size);
}

bool DualSenseBtAudio::SendOutputRoute(ChiakiDualSenseBtOutput output)
{
	uint8_t report[CHIAKI_DUALSENSE_BT_STATE_REPORT_SIZE];
	chiaki_dualsense_bt_output_route_report(report, seq++ & 0xf, output);
	return SendReport(report, sizeof(report));
}

void DualSenseBtAudio::run()
{
	struct timespec next;
	clock_gettime(CLOCK_MONOTONIC, &next);
	int failures = 0;
	int route_countdown = 0;
	int last_errno = 0;
	while(!stop_requested && failures < MAX_CONSECUTIVE_WRITE_FAILURES)
	{
		if(speaker)
			PollHeadphones();

		uint8_t haptics[CHIAKI_DUALSENSE_BT_HAPTICS_SIZE];
		TakeHaptics(haptics);
		bool has_haptics = false;
		for(uint8_t sample : haptics)
			has_haptics = has_haptics || sample;

		// With the speaker off, a pad with no haptics to play has no reason to keep its radio busy
		if(speaker || has_haptics)
		{
			if(speaker && route_countdown-- <= 0)
			{
				// Repeated because the kernel driver resets the route when a headset is plugged or unplugged
				SendOutputRoute(headphones ? CHIAKI_DUALSENSE_BT_OUTPUT_HEADPHONES : CHIAKI_DUALSENSE_BT_OUTPUT_SPEAKER);
				route_countdown = SPEAKER_ROUTE_INTERVAL_REPORTS;
			}
			int16_t pcm[SPEAKER_SAMPLES_PER_REPORT * 2];
			TakeAudio(pcm);
			uint8_t opus[CHIAKI_DUALSENSE_BT_OPUS_FRAMES * CHIAKI_DUALSENSE_BT_OPUS_FRAME_SIZE] = {};
			for(int i = 0; i < CHIAKI_DUALSENSE_BT_OPUS_FRAMES; i++)
			{
				const int encoded = opus_encode(encoder, pcm + i * CHIAKI_DUALSENSE_BT_OPUS_FRAME_SAMPLES * 2,
					CHIAKI_DUALSENSE_BT_OPUS_FRAME_SAMPLES, opus + i * CHIAKI_DUALSENSE_BT_OPUS_FRAME_SIZE, CHIAKI_DUALSENSE_BT_OPUS_FRAME_SIZE);
				if(encoded < 0)
					CHIAKI_LOGE(log, "DualSense Bluetooth audio encoding failed: %s", opus_strerror(encoded));
			}
			uint8_t report[CHIAKI_DUALSENSE_BT_AUDIO_REPORT_SIZE];
			counter += CHIAKI_DUALSENSE_BT_OPUS_FRAMES;
			chiaki_dualsense_bt_audio_report(report, seq++ & 0xf, counter, haptics, opus, headphones);
			if(SendReport(report, sizeof(report)))
				failures = 0;
			else
			{
				failures++;
				last_errno = errno;
			}
		}

		TimespecAddNs(&next, REPORT_PERIOD_NS);
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		const long long late_ns = (now.tv_sec - next.tv_sec) * 1000000000LL + (now.tv_nsec - next.tv_nsec);
		if(late_ns > MAX_LATE_REPORTS * REPORT_PERIOD_NS)
			next = now; // A stall must not turn into a burst, which would overrun the pad's buffer
		else if(late_ns < 0)
			clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);
	}
	if(failures >= MAX_CONSECUTIVE_WRITE_FAILURES)
		CHIAKI_LOGW(log, "DualSense Bluetooth audio stopped, writes are failing: %s", strerror(last_errno));
	active = false;
}
