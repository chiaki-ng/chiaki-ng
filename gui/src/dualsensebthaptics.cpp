// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <dualsensebthaptics.h>

#include <QtGlobal>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

// The report layout and CRC live in lib/src/dualsensebt.c
#define DS_BT_HAPTICS_BLOCK_SIZE CHIAKI_DUALSENSE_BT_HAPTICS_BLOCK_SIZE
#define DS_BT_HAPTICS_SAMPLE_RATE CHIAKI_DUALSENSE_BT_HAPTICS_RATE
#define DS_BT_HAPTICS_FRAME_BYTES 2 // L + R, one byte each
#define DS_BT_HAPTICS_MAX_FAILURES 50

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
	static_assert(sizeof(report) == CHIAKI_DUALSENSE_BT_HAPTICS_REPORT_SIZE, "unexpected haptics report size");
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
	// Just over one double-block report (21.33 ms), so an effect starts with a full first report
	int prefill_ms = EnvInt("CHIAKI_BT_HAPTICS_PREFILL_MS", 22, 0, 500);
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
	chiaki_dualsense_bt_haptics_report(report, seq, packet_counter, ctrl_buffer, samples, size, double_block);
	seq = (seq + 1) & 0x0F;
	packet_counter += double_block ? 2 : 1;

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
