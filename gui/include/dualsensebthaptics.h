// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_DUALSENSEBTHAPTICS_H
#define CHIAKI_DUALSENSEBTHAPTICS_H

#include <chiaki/log.h>

#include <QByteArray>
#include <QObject>
#include <QString>

#include <SDL.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

#if !SDL_VERSION_ATLEAST(2, 0, 18)
// SDL_hid_* is only available since SDL 2.0.18; Open() fails and rumble is used instead
typedef struct SDL_hid_device_ SDL_hid_device;
#endif

// Over Bluetooth the DualSense does not expose an audio device, so the console's
// 3 kHz haptics stream is written directly as HID output report 0x32.
//
// The console's haptics arrive in network-paced bursts, but the controller plays
// them at a fixed rate. PushSamples() only queues samples; a dedicated sender
// thread drains the queue on a steady clock (one report per 64 or 32 frames),
// keeping a small jitter buffer and bounding latency by dropping the oldest data.
//
// Tuning through environment variables (read once in Open()):
//   CHIAKI_BT_HAPTICS_SINGLE=1      one 32-frame block per report every 10.67 ms
//                                   (default: two blocks per report every 21.33 ms)
//   CHIAKI_BT_HAPTICS_CTRL_BUFFER=n controller-side audio buffer length, 16..128 (default 48)
//   CHIAKI_BT_HAPTICS_PREFILL_MS=n  host jitter buffer before playback starts (default 32)
//   CHIAKI_BT_HAPTICS_MAX_MS=n      host queue cap before the oldest data is dropped (default 96)
class DualSenseBtHaptics : public QObject
{
	Q_OBJECT

	public:
		explicit DualSenseBtHaptics(ChiakiLog *log, QObject *parent = nullptr);
		~DualSenseBtHaptics() override;

		bool Open(const QString &hid_path);
		void Close();
		bool IsOpen() const { return dev != nullptr && !failed.load(); }
		QString GetPath() const { return path; }

		// Interleaved stereo (L, R) signed 8-bit samples at 3 kHz
		void PushSamples(const QByteArray &samples);

	private:
		void SenderLoop();
		bool SendReport(const uint8_t *samples, size_t size);

		ChiakiLog *log;
		SDL_hid_device *dev = nullptr;
		QString path;

		// configuration
		bool double_block = true;
		uint8_t ctrl_buffer = 48;
		size_t report_bytes = 128;      // sample bytes per report
		size_t prefill_bytes = 192;
		size_t max_bytes = 576;

		// report state (sender thread only)
		uint8_t report[142];
		uint8_t seq = 0;
		uint8_t packet_counter = 0;
		int consecutive_failures = 0;

		// queue shared between PushSamples() and the sender thread
		std::mutex mutex;
		std::condition_variable cond;
		std::deque<uint8_t> queue;
		bool stop = false;
		std::thread sender;
		std::atomic<bool> failed{false};

		// statistics, logged on close
		uint64_t stat_reports = 0;
		uint64_t stat_underruns = 0;
		uint64_t stat_dropped_bytes = 0;
};

#endif // CHIAKI_DUALSENSEBTHAPTICS_H
