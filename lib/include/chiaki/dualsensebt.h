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

// DualSense haptics over Bluetooth, which has no audio endpoint: the console's 3 kHz haptics
// stream travels in the undocumented HID output report 0x32, as used by DS5Dongle.
#define CHIAKI_DUALSENSE_BT_HAPTICS_REPORT_SIZE 142
#define CHIAKI_DUALSENSE_BT_HAPTICS_RATE 3000
#define CHIAKI_DUALSENSE_BT_HAPTICS_BLOCK_FRAMES 32 // stereo int8 frames per block, one or two blocks per report
#define CHIAKI_DUALSENSE_BT_HAPTICS_BLOCK_SIZE (CHIAKI_DUALSENSE_BT_HAPTICS_BLOCK_FRAMES * 2)

/**
 * Fills the last 4 bytes of a Bluetooth output report with its CRC32.
 */
CHIAKI_EXPORT void chiaki_dualsense_bt_fill_crc(uint8_t *report, size_t size);

/**
 * Builds a haptics-only output report 0x32, including its CRC.
 *
 * @param report CHIAKI_DUALSENSE_BT_HAPTICS_REPORT_SIZE bytes
 * @param seq 4-bit report sequence number, advanced by one by the caller for each report
 * @param counter packet counter, advanced by the number of blocks by the caller
 * @param buffer_length controller-side audio buffer length
 * @param samples interleaved int8 samples, one or two blocks; shorter input is padded with silence
 * @param double_block whether the report carries two blocks (21.33 ms) instead of one (10.67 ms)
 */
CHIAKI_EXPORT void chiaki_dualsense_bt_haptics_report(uint8_t *report, uint8_t seq, uint8_t counter, uint8_t buffer_length,
	const uint8_t *samples, size_t samples_size, bool double_block);

/**
 * Converts the console's int16 haptics samples to the int8 samples of the report.
 *
 * @param gain multiplier applied before scaling down, clamped to the int8 range
 * @return whether any output sample is non-zero
 */
CHIAKI_EXPORT bool chiaki_dualsense_bt_haptics_convert(int8_t *out, const int16_t *in, size_t count, float gain);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_DUALSENSEBT_H
