// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_GUI_DUALSENSEBTAUDIO_H
#define CHIAKI_GUI_DUALSENSEBTAUDIO_H

#include <chiaki/dualsensebt.h>
#include <chiaki/log.h>

#include <QMutex>
#include <QString>
#include <QThread>

#include <atomic>

struct OpusEncoder;

/**
 * Sends haptics and speaker audio to a DualSense connected over Bluetooth through its hidraw
 * device, paced on a thread of its own.
 */
class DualSenseBtAudio : private QThread
{
	public:
		explicit DualSenseBtAudio(ChiakiLog *log);
		~DualSenseBtAudio() override;

		/**
		 * @return the hidraw device of a DualSense connected over Bluetooth, or empty if there is none
		 */
		static QString FindDevice();

		bool Start(const QString &device, bool speaker);
		void Stop();
		/**
		 * False until started, and again once the device is gone.
		 */
		bool IsActive() const { return active; }

		/**
		 * @param buf interleaved stereo int16 samples at CHIAKI_DUALSENSE_BT_HAPTICS_RATE
		 */
		void PushHaptics(const uint8_t *buf, size_t frames, float gain);
		/**
		 * @param buf interleaved int16 samples
		 */
		void PushAudio(const int16_t *buf, size_t frames);
		void SetAudioFormat(unsigned int channels, unsigned int rate);

	private:
		void run() override;
		bool SendReport(const uint8_t *report, size_t size);
		bool SendOutputRoute(ChiakiDualSenseBtOutput output);
		void PollHeadphones();
		void TakeHaptics(uint8_t *out);
		void TakeAudio(int16_t *out);
		void ResetBuffers();

		ChiakiLog *log;
		int fd = -1;
		int input_fd = -1;
		bool headphones = false;
		OpusEncoder *encoder = nullptr;
		bool speaker = false;
		uint8_t seq = 0;
		uint8_t counter = 0;
		std::atomic<bool> active{false};
		std::atomic<bool> stop_requested{false};

		QMutex mutex;
		unsigned int audio_channels = 2;
		double audio_step = 48000.0 / CHIAKI_DUALSENSE_BT_SPEAKER_RATE;
		double audio_pos = 0.0;
		int16_t audio_last[2] = {0, 0};
		static constexpr size_t SPEAKER_RING_SIZE = CHIAKI_DUALSENSE_BT_SPEAKER_RATE / 12; // ~83 ms
		int16_t speaker_ring[SPEAKER_RING_SIZE * 2]; // stereo, mixed down for the speaker when sent
		size_t speaker_read = 0;
		size_t speaker_fill = 0;
		bool speaker_playing = false;
		static constexpr size_t HAPTICS_RING_SIZE = CHIAKI_DUALSENSE_BT_HAPTICS_FRAMES * 3 * 2; // 64 ms
		int8_t haptics_ring[HAPTICS_RING_SIZE];
		size_t haptics_read = 0;
		size_t haptics_fill = 0;
};

#endif // CHIAKI_GUI_DUALSENSEBTAUDIO_H
