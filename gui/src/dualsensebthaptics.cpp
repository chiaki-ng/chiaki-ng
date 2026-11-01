// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <dualsensebthaptics.h>

#include <QtGlobal>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

// Layout of output report 0x32 (142 bytes, Bluetooth only), as used by DS5Dongle:
//   [0]        report id 0x32
//   [1]        sequence number in the high nibble
//   [2..7]     packet 0x11 (sized, 4 bytes): audio control flags, controller buffer
//              length (x2), packet counter
//   [8..9]     packet 0x12 header (sized, 64); bit 6 set = two 64-byte blocks follow
//   [10..137]  samples: signed 8-bit stereo frames at 3 kHz (32 or 64 frames)
//   [138..141] CRC32 (little endian) over the 0xA2 BT output header + bytes [0..137]
#define DS_BT_HAPTICS_REPORT_ID 0x32
#define DS_BT_HAPTICS_REPORT_SIZE 142
#define DS_BT_HAPTICS_SAMPLES_OFFSET 10
#define DS_BT_HAPTICS_BLOCK_SIZE 64
#define DS_BT_HAPTICS_CRC_OFFSET (DS_BT_HAPTICS_REPORT_SIZE - 4)
#define DS_BT_HAPTICS_SAMPLE_RATE 3000
#define DS_BT_HAPTICS_FRAME_BYTES 2 // L + R, one byte each
#define DS_BT_OUTPUT_CRC_SEED 0xA2
#define DS_BT_HAPTICS_MAX_FAILURES 50
// Audio control flags (bit 0: controller mic, bit 5: apply buffer length)
#define DS_BT_HAPTICS_AUDIO_FLAGS 0x66

static_assert(DS_BT_HAPTICS_SAMPLES_OFFSET + 2 * DS_BT_HAPTICS_BLOCK_SIZE <= DS_BT_HAPTICS_CRC_OFFSET, "haptics samples overlap crc");

static uint32_t Crc32Update(uint32_t crc, const uint8_t *data, size_t size)
{
	while(size--)
	{
		crc ^= *data++;
		for(int i = 0; i < 8; i++)
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return crc;
}

static int EnvInt(const char *name, int def, int min, int max)
{
	bool ok = false;
	int v = qEnvironmentVariableIntValue(name, &ok);
	if(!ok)
		return def;
	return std::clamp(v, min, max);
}

static size_t MsToBytes(int ms)
{
	size_t frames = ((size_t)ms * DS_BT_HAPTICS_SAMPLE_RATE + 999) / 1000;
	return frames * DS_BT_HAPTICS_FRAME_BYTES;
}

DualSenseBtHaptics::DualSenseBtHaptics(ChiakiLog *log, QObject *parent)
	: QObject(parent), log(log)
{
	static_assert(sizeof(report) == DS_BT_HAPTICS_REPORT_SIZE, "unexpected haptics report size");
	memset(report, 0, sizeof(report));
}

DualSenseBtHaptics::~DualSenseBtHaptics()
{
	Close();
}

bool DualSenseBtHaptics::Open(const QString &hid_path)
{
	Close();
	if(hid_path.isEmpty())
		return false;
#if SDL_VERSION_ATLEAST(2, 0, 18)
	double_block = qEnvironmentVariableIntValue("CHIAKI_BT_HAPTICS_SINGLE") == 0;
	ctrl_buffer = (uint8_t)EnvInt("CHIAKI_BT_HAPTICS_CTRL_BUFFER", 48, 16, 128);
	report_bytes = double_block ? 2 * DS_BT_HAPTICS_BLOCK_SIZE : DS_BT_HAPTICS_BLOCK_SIZE;
	int prefill_ms = EnvInt("CHIAKI_BT_HAPTICS_PREFILL_MS", 32, 0, 500);
	int max_ms = EnvInt("CHIAKI_BT_HAPTICS_MAX_MS", 96, 0, 1000);
	prefill_bytes = MsToBytes(prefill_ms);
	max_bytes = std::max(MsToBytes(max_ms), prefill_bytes + report_bytes);

	if(SDL_hid_init() != 0)
	{
		CHIAKI_LOGE(log, "DualSense BT haptics: SDL_hid_init failed: %s", SDL_GetError());
		return false;
	}
	dev = SDL_hid_open_path(hid_path.toUtf8().constData(), 0);
	if(!dev)
	{
		CHIAKI_LOGE(log, "DualSense BT haptics: could not open HID device %s: %s", hid_path.toUtf8().constData(), SDL_GetError());
		SDL_hid_exit();
		return false;
	}
	path = hid_path;
	seq = 0;
	packet_counter = 0;
	consecutive_failures = 0;
	stat_reports = stat_underruns = stat_dropped_bytes = 0;
	failed = false;
	{
		std::lock_guard<std::mutex> lk(mutex);
		queue.clear();
		stop = false;
	}
	sender = std::thread([this] { SenderLoop(); });
	CHIAKI_LOGI(log, "DualSense BT haptics: streaming haptics to %s via output report 0x32 "
		"(%s blocks, controller buffer %u, prefill %d ms, max %d ms)",
		hid_path.toUtf8().constData(), double_block ? "double" : "single",
		(unsigned)ctrl_buffer, prefill_ms, max_ms);
	return true;
#else
	CHIAKI_LOGW(log, "DualSense BT haptics: requires SDL 2.0.18 or newer, using rumble haptics instead");
	return false;
#endif
}

void DualSenseBtHaptics::Close()
{
	if(sender.joinable())
	{
		{
			std::lock_guard<std::mutex> lk(mutex);
			stop = true;
		}
		cond.notify_all();
		sender.join();
	}
	if(!dev)
		return;
#if SDL_VERSION_ATLEAST(2, 0, 18)
	SDL_hid_close(dev);
	SDL_hid_exit();
#endif
	dev = nullptr;
	CHIAKI_LOGI(log, "DualSense BT haptics: closed %s (reports sent %llu, playback gaps %llu, dropped %llu ms of backlog)",
		path.toUtf8().constData(), (unsigned long long)stat_reports, (unsigned long long)stat_underruns,
		(unsigned long long)(stat_dropped_bytes / DS_BT_HAPTICS_FRAME_BYTES * 1000 / DS_BT_HAPTICS_SAMPLE_RATE));
	path.clear();
}

void DualSenseBtHaptics::PushSamples(const QByteArray &samples)
{
	if(!IsOpen() || samples.isEmpty())
		return;
	{
		std::lock_guard<std::mutex> lk(mutex);
		queue.insert(queue.end(), samples.constBegin(), samples.constEnd());
	}
	cond.notify_one();
}

void DualSenseBtHaptics::SenderLoop()
{
	using clock = std::chrono::steady_clock;
	const size_t frames_per_report = report_bytes / DS_BT_HAPTICS_FRAME_BYTES;
	const auto period = std::chrono::nanoseconds((int64_t)frames_per_report * 1000000000LL / DS_BT_HAPTICS_SAMPLE_RATE);
	const auto prefill_wait = period * ((prefill_bytes + report_bytes - 1) / report_bytes + 1);

	std::vector<uint8_t> block(report_bytes);
	bool playing = false;
	clock::time_point next = clock::now();

	std::unique_lock<std::mutex> lk(mutex);
	while(!stop)
	{
		if(!playing)
		{
			// Wait for data, then let a small jitter buffer build up before playback starts.
			cond.wait(lk, [&] { return stop || !queue.empty(); });
			if(stop)
				break;
			if(queue.size() < prefill_bytes)
			{
				cond.wait_until(lk, clock::now() + prefill_wait, [&] { return stop || queue.size() >= prefill_bytes; });
				if(stop)
					break;
			}
			playing = true;
			next = clock::now();
		}

		// Keep latency bounded: if the backlog grew too large, drop the oldest samples.
		if(queue.size() > max_bytes)
		{
			size_t drop = queue.size() - prefill_bytes;
			drop -= drop % DS_BT_HAPTICS_FRAME_BYTES;
			queue.erase(queue.begin(), queue.begin() + (std::ptrdiff_t)drop);
			stat_dropped_bytes += drop;
		}

		if(queue.empty())
		{
			// Ran dry: stop sending and rebuild the jitter buffer when data returns.
			playing = false;
			stat_underruns++;
			continue;
		}

		size_t n = std::min(report_bytes, queue.size());
		std::copy(queue.begin(), queue.begin() + (std::ptrdiff_t)n, block.begin());
		std::fill(block.begin() + (std::ptrdiff_t)n, block.end(), 0);
		queue.erase(queue.begin(), queue.begin() + (std::ptrdiff_t)n);

		lk.unlock();
		bool ok = SendReport(block.data(), block.size());
		lk.lock();
		if(!ok && failed)
			break;

		next += period;
		auto now = clock::now();
		if(now > next + period * 4)
			next = now; // fell far behind (e.g. a stalled write): resync instead of bursting
		cond.wait_until(lk, next, [&] { return stop; });
	}
}

bool DualSenseBtHaptics::SendReport(const uint8_t *samples, size_t size)
{
	memset(report, 0, sizeof(report));
	report[0] = DS_BT_HAPTICS_REPORT_ID;
	report[1] = (uint8_t)(seq << 4);
	seq = (seq + 1) & 0x0F;
	// packet 0x11: id 0x11 | sized flag (0x80), length 4
	report[2] = 0x11 | 0x80;
	report[3] = 4;
	report[4] = DS_BT_HAPTICS_AUDIO_FLAGS;
	report[5] = ctrl_buffer;
	report[6] = ctrl_buffer;
	report[7] = packet_counter;
	packet_counter += double_block ? 2 : 1;
	// packet 0x12: id 0x12 | sized flag (0x80) | double block flag (0x40), length 64
	report[8] = 0x12 | 0x80 | (double_block ? 0x40 : 0);
	report[9] = DS_BT_HAPTICS_BLOCK_SIZE;
	memcpy(report + DS_BT_HAPTICS_SAMPLES_OFFSET, samples, std::min(size, (size_t)(DS_BT_HAPTICS_CRC_OFFSET - DS_BT_HAPTICS_SAMPLES_OFFSET)));

	const uint8_t crc_seed = DS_BT_OUTPUT_CRC_SEED;
	uint32_t crc = Crc32Update(0xFFFFFFFFu, &crc_seed, 1);
	crc = ~Crc32Update(crc, report, DS_BT_HAPTICS_CRC_OFFSET);
	report[DS_BT_HAPTICS_CRC_OFFSET + 0] = (uint8_t)(crc & 0xFF);
	report[DS_BT_HAPTICS_CRC_OFFSET + 1] = (uint8_t)((crc >> 8) & 0xFF);
	report[DS_BT_HAPTICS_CRC_OFFSET + 2] = (uint8_t)((crc >> 16) & 0xFF);
	report[DS_BT_HAPTICS_CRC_OFFSET + 3] = (uint8_t)((crc >> 24) & 0xFF);

#if SDL_VERSION_ATLEAST(2, 0, 18)
	int written = SDL_hid_write(dev, report, sizeof(report));
#else
	int written = -1;
#endif
	if(written < 0)
	{
		if(consecutive_failures++ == 0)
			CHIAKI_LOGW(log, "DualSense BT haptics: HID write failed: %s", SDL_GetError());
		if(consecutive_failures >= DS_BT_HAPTICS_MAX_FAILURES)
		{
			CHIAKI_LOGE(log, "DualSense BT haptics: %d consecutive write failures, giving up", consecutive_failures);
			failed = true;
		}
		return false;
	}
	consecutive_failures = 0;
	stat_reports++;
	return true;
}
