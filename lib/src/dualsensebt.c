// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/dualsensebt.h>

#include <string.h>

// Layout of output report 0x32:
//   [0]        report id
//   [1]        sequence number in the high nibble
//   [2..7]     config block 0x11 (4 bytes): audio control flags, controller buffer length (x2), packet counter
//   [8..9]     haptics block 0x12 header, 64 bytes per block
//   [10..137]  samples: one or two blocks
//   [138..141] CRC32
#define HAPTICS_REPORT_ID 0x32
#define BLOCK_LAST 0x80
#define BLOCK_DOUBLE 0x40
#define BLOCK_CONFIG 0x11
#define BLOCK_HAPTICS 0x12
#define CONFIG_LEN 4
// Bit 0 would make the pad stream its microphone, bit 5 applies the buffer length
#define CONFIG_AUDIO_FLAGS 0x66
#define SAMPLES_OFFSET 10
#define CRC_SIZE 4

static uint32_t crc32_update(uint32_t crc, uint8_t byte)
{
	crc ^= byte;
	for(int i = 0; i < 8; i++)
		crc = (crc & 1) ? (crc >> 1) ^ 0xedb88320u : crc >> 1;
	return crc;
}

CHIAKI_EXPORT void chiaki_dualsense_bt_fill_crc(uint8_t *report, size_t size)
{
	// The checksum includes the HIDP header, which the kernel adds to the report on the way out
	uint32_t crc = crc32_update(0xffffffffu, 0xa2);
	for(size_t i = 0; i < size - 4; i++)
		crc = crc32_update(crc, report[i]);
	crc = ~crc;
	for(size_t i = 0; i < 4; i++)
		report[size - 4 + i] = (uint8_t)(crc >> (8 * i));
}

CHIAKI_EXPORT void chiaki_dualsense_bt_haptics_report(uint8_t *report, uint8_t seq, uint8_t counter, uint8_t buffer_length,
	const uint8_t *samples, size_t samples_size, bool double_block)
{
	memset(report, 0, CHIAKI_DUALSENSE_BT_HAPTICS_REPORT_SIZE);
	report[0] = HAPTICS_REPORT_ID;
	report[1] = (uint8_t)(seq << 4);
	report[2] = BLOCK_CONFIG | BLOCK_LAST;
	report[3] = CONFIG_LEN;
	report[4] = CONFIG_AUDIO_FLAGS;
	report[5] = buffer_length;
	report[6] = buffer_length;
	report[7] = counter;
	report[8] = BLOCK_HAPTICS | BLOCK_LAST | (double_block ? BLOCK_DOUBLE : 0);
	report[9] = CHIAKI_DUALSENSE_BT_HAPTICS_BLOCK_SIZE;
	size_t max = (double_block ? 2 : 1) * CHIAKI_DUALSENSE_BT_HAPTICS_BLOCK_SIZE;
	memcpy(report + SAMPLES_OFFSET, samples, samples_size < max ? samples_size : max);
	chiaki_dualsense_bt_fill_crc(report, CHIAKI_DUALSENSE_BT_HAPTICS_REPORT_SIZE);
}

CHIAKI_EXPORT bool chiaki_dualsense_bt_haptics_convert(int8_t *out, const int16_t *in, size_t count, float gain)
{
	bool nonsilent = false;
	for(size_t i = 0; i < count; i++)
	{
		int32_t v = (int32_t)(in[i] * gain) / 256;
		v = v > INT8_MAX ? INT8_MAX : v;
		v = v < INT8_MIN ? INT8_MIN : v;
		if(v != 0)
			nonsilent = true;
		out[i] = (int8_t)v;
	}
	return nonsilent;
}
