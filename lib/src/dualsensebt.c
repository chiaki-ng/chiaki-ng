// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/dualsensebt.h>

#include <string.h>

#define AUDIO_REPORT_ID 0x39
#define STATE_REPORT_ID 0x31
#define STATE_REPORT_TAG 0x10
#define BLOCK_LAST 0x80
#define BLOCK_DOUBLE 0x40
#define BLOCK_CONFIG 0x11
#define BLOCK_HAPTICS 0x12
#define BLOCK_SPEAKER 0x13
#define BLOCK_HEADPHONES 0x16
#define BLOCK_HAPTICS_FRAME_SIZE 64
// Bit 0 of the config mask would make the pad stream its microphone in the input reports
#define CONFIG_MASK 0x7e
#define CONFIG_MASK_LEN 6
#define AUDIO_BUFFER_LENGTH 48

#define STATE_ALLOW_HEADPHONE_VOLUME 0x10
#define STATE_ALLOW_SPEAKER_VOLUME 0x20
#define STATE_ALLOW_AUDIO_CONTROL 0x80
#define STATE_ALLOW_AUDIO_CONTROL2 0x80
#define STATE_SPEAKER_VOLUME 0x64
// About two thirds of the 0x7f maximum: nothing in the stream sets a volume, so err on the side of quiet
#define STATE_HEADPHONE_VOLUME 0x50
#define STATE_OUTPUT_PATH_SPEAKER 0x30
// The speaker is attenuated with noise cancelling cleared
#define STATE_NOISE_CANCEL 0x08
#define STATE_SPEAKER_PREAMP 2

#define INPUT_REPORT_ID 0x31
#define INPUT_STATUS_OFFSET 55
#define INPUT_STATUS_HEADPHONES 0x01

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

CHIAKI_EXPORT void chiaki_dualsense_bt_audio_report(uint8_t *report, uint8_t seq, uint8_t counter, const uint8_t *haptics, const uint8_t *opus, bool headphones)
{
	memset(report, 0, CHIAKI_DUALSENSE_BT_AUDIO_REPORT_SIZE);
	report[0] = AUDIO_REPORT_ID;
	report[1] = seq << 4;
	report[2] = BLOCK_CONFIG | BLOCK_LAST;
	report[3] = CONFIG_MASK_LEN;
	report[4] = CONFIG_MASK;
	memset(report + 5, AUDIO_BUFFER_LENGTH, 4);
	report[9] = counter;
	report[10] = BLOCK_HAPTICS | BLOCK_DOUBLE | BLOCK_LAST;
	report[11] = BLOCK_HAPTICS_FRAME_SIZE;
	memcpy(report + 12, haptics, CHIAKI_DUALSENSE_BT_HAPTICS_SIZE);
	report[140] = (headphones ? BLOCK_HEADPHONES : BLOCK_SPEAKER) | BLOCK_DOUBLE | BLOCK_LAST;
	report[141] = CHIAKI_DUALSENSE_BT_OPUS_FRAME_SIZE;
	memcpy(report + 142, opus, CHIAKI_DUALSENSE_BT_OPUS_FRAMES * CHIAKI_DUALSENSE_BT_OPUS_FRAME_SIZE);
	chiaki_dualsense_bt_fill_crc(report, CHIAKI_DUALSENSE_BT_AUDIO_REPORT_SIZE);
}

CHIAKI_EXPORT void chiaki_dualsense_bt_output_route_report(uint8_t *report, uint8_t seq, ChiakiDualSenseBtOutput output)
{
	memset(report, 0, CHIAKI_DUALSENSE_BT_STATE_REPORT_SIZE);
	report[0] = STATE_REPORT_ID;
	report[1] = seq << 4;
	report[2] = STATE_REPORT_TAG;
	uint8_t *state = report + 3;
	state[0] = STATE_ALLOW_AUDIO_CONTROL;
	state[1] = STATE_ALLOW_AUDIO_CONTROL2;
	switch(output)
	{
		case CHIAKI_DUALSENSE_BT_OUTPUT_SPEAKER:
			state[0] |= STATE_ALLOW_SPEAKER_VOLUME;
			state[5] = STATE_SPEAKER_VOLUME;
			state[7] = STATE_OUTPUT_PATH_SPEAKER | STATE_NOISE_CANCEL;
			state[37] = STATE_SPEAKER_PREAMP;
			break;
		case CHIAKI_DUALSENSE_BT_OUTPUT_HEADPHONES:
			state[0] |= STATE_ALLOW_HEADPHONE_VOLUME;
			state[4] = STATE_HEADPHONE_VOLUME;
			break;
		case CHIAKI_DUALSENSE_BT_OUTPUT_DEFAULT:
			break;
	}
	chiaki_dualsense_bt_fill_crc(report, CHIAKI_DUALSENSE_BT_STATE_REPORT_SIZE);
}

CHIAKI_EXPORT bool chiaki_dualsense_bt_input_has_status(const uint8_t *report, size_t size)
{
	return size > INPUT_STATUS_OFFSET && report[0] == INPUT_REPORT_ID;
}

CHIAKI_EXPORT bool chiaki_dualsense_bt_input_headphones(const uint8_t *report, size_t size)
{
	return chiaki_dualsense_bt_input_has_status(report, size) && (report[INPUT_STATUS_OFFSET] & INPUT_STATUS_HEADPHONES);
}
