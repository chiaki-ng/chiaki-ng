// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_DUALSENSEBT_H
#define CHIAKI_DUALSENSEBT_H

#include "common.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// DualSense speaker and haptics over Bluetooth, which has no audio endpoint: both travel in an
// undocumented HID output report, one every 64 haptics frames.
#define CHIAKI_DUALSENSE_BT_AUDIO_REPORT_SIZE 547
#define CHIAKI_DUALSENSE_BT_STATE_REPORT_SIZE 78
#define CHIAKI_DUALSENSE_BT_HAPTICS_RATE 3000
#define CHIAKI_DUALSENSE_BT_HAPTICS_FRAMES 64 // stereo int8 frames per report
#define CHIAKI_DUALSENSE_BT_HAPTICS_SIZE (CHIAKI_DUALSENSE_BT_HAPTICS_FRAMES * 2)
#define CHIAKI_DUALSENSE_BT_OPUS_FRAMES 2 // Opus frames per report
#define CHIAKI_DUALSENSE_BT_OPUS_FRAME_SAMPLES 480
#define CHIAKI_DUALSENSE_BT_OPUS_FRAME_SIZE 200
// 960 samples per 64 frames at 3 kHz: the pad plays Opus labelled 48 kHz at this rate
#define CHIAKI_DUALSENSE_BT_SPEAKER_RATE 45000

typedef enum
{
	CHIAKI_DUALSENSE_BT_OUTPUT_DEFAULT,
	CHIAKI_DUALSENSE_BT_OUTPUT_SPEAKER,
	CHIAKI_DUALSENSE_BT_OUTPUT_HEADPHONES
} ChiakiDualSenseBtOutput;

CHIAKI_EXPORT void chiaki_dualsense_bt_fill_crc(uint8_t *report, size_t size);

/**
 * @param report CHIAKI_DUALSENSE_BT_AUDIO_REPORT_SIZE bytes
 * @param seq 4-bit report sequence number, advanced by one by the caller for each report
 * @param counter audio frame counter, advanced by CHIAKI_DUALSENSE_BT_OPUS_FRAMES by the caller
 * @param haptics CHIAKI_DUALSENSE_BT_HAPTICS_SIZE bytes of interleaved int8 samples
 * @param opus CHIAKI_DUALSENSE_BT_OPUS_FRAMES contiguous frames of CHIAKI_DUALSENSE_BT_OPUS_FRAME_SIZE bytes
 * @param headphones whether the Opus frames are for the headphone jack instead of the speaker
 */
CHIAKI_EXPORT void chiaki_dualsense_bt_audio_report(uint8_t *report, uint8_t seq, uint8_t counter, const uint8_t *haptics, const uint8_t *opus, bool headphones);

/**
 * Routes audio to the given output.
 *
 * @param report CHIAKI_DUALSENSE_BT_STATE_REPORT_SIZE bytes
 */
CHIAKI_EXPORT void chiaki_dualsense_bt_output_route_report(uint8_t *report, uint8_t seq, ChiakiDualSenseBtOutput output);

/**
 * @param report an input report as read from the hidraw device
 * @return whether the report says headphones are plugged in. False for reports without that status.
 */
CHIAKI_EXPORT bool chiaki_dualsense_bt_input_has_status(const uint8_t *report, size_t size);
CHIAKI_EXPORT bool chiaki_dualsense_bt_input_headphones(const uint8_t *report, size_t size);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_DUALSENSEBT_H
