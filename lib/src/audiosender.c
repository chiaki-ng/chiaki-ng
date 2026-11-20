// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/audiosender.h>
#include <string.h>
#include <stdlib.h>
#include <chiaki/fec.h>

static void chiaki_audio_sender_frame(ChiakiAudioSender *audio_sender, uint8_t *buf, size_t buf_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_audio_sender_init(ChiakiAudioSender *audio_sender, ChiakiLog *log, ChiakiSession *session)
{
    audio_sender->log = log;
    audio_sender->ps5 = session->connect_info.ps5;
    audio_sender->takion = &(session->stream_connection.takion);
    audio_sender->frame_index = 0;
    audio_sender->buf_size_per_unit = 40;
    audio_sender->buf_stride_per_unit = ((audio_sender->buf_size_per_unit + 0xf) / 0x10) * 0x10;
    audio_sender->framea = NULL;
    audio_sender->frameb = NULL;
    audio_sender->frame_buf_size = 3 * audio_sender->buf_size_per_unit;
    audio_sender->frame_buf = malloc(audio_sender->frame_buf_size);
    if(!audio_sender->frame_buf)
        return CHIAKI_ERR_MEMORY;
    audio_sender->filled_packet_buf = malloc(audio_sender->frame_buf_size + 20);
    if(!audio_sender->filled_packet_buf)
    {
        free(audio_sender->frame_buf);
        audio_sender->frame_buf = NULL;
        return CHIAKI_ERR_MEMORY;
    }

    ChiakiErrorCode err = chiaki_mutex_init(&audio_sender->mutex, false);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        free(audio_sender->filled_packet_buf);
        audio_sender->filled_packet_buf = NULL;
        free(audio_sender->frame_buf);
        audio_sender->frame_buf = NULL;
        return err;
    }

    return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT void chiaki_audio_sender_fini(ChiakiAudioSender *audio_sender)
{
    free(audio_sender->frame_buf);
    free(audio_sender->filled_packet_buf);
    if(audio_sender->framea)
        free(audio_sender->framea);
    if(audio_sender->frameb)
        free(audio_sender->frameb);
    chiaki_mutex_fini(&audio_sender->mutex);
}

CHIAKI_EXPORT void chiaki_audio_sender_opus_data(ChiakiAudioSender *audio_sender, uint8_t *opus_sender, size_t opus_sender_size)
{
    // skip audio packets without encoded audio
    // if no audio the packet will have only 3 encoded units because there is no entropy in the packet, otherwise should be max of 40
    if(opus_sender_size != audio_sender->buf_size_per_unit)
        return;
    uint8_t packet_type = 3; // TAKION_PACKET_TYPE_AUDIO
    uint16_t packet_index = htons(audio_sender->frame_index);
    uint16_t frame_index = htons(audio_sender->frame_index + 1);
    uint32_t unit_index = 0;
    uint32_t units_in_frame_total = 3;
    uint32_t units_in_frame_fec_raw = 10273;
    uint32_t units_number = htonl((units_in_frame_fec_raw & 0xffff) | (((units_in_frame_total - 1) & 0xff) << 0x10) | ((unit_index & 0xff) << 0x18));
    uint32_t key_pos = htonl(0);
    uint8_t codec = 5;
    uint32_t gmac = htonl(0);
    uint8_t zero_byte = 0;
    bool is_ps5 = audio_sender->ps5;
    uint8_t ps5_packet = 0;
    if(is_ps5)
        ps5_packet = 1;

    // Each packet carries the new frame, then the two before it, oldest first, so the console can
    // fill a lost packet from the next one. The history starts as two copies of the first frame.
    if(!audio_sender->framea || !audio_sender->frameb)
    {
        if(!audio_sender->framea)
            audio_sender->framea = malloc(audio_sender->buf_size_per_unit);
        if(!audio_sender->frameb)
            audio_sender->frameb = malloc(audio_sender->buf_size_per_unit);
        if(!audio_sender->framea || !audio_sender->frameb)
        {
            CHIAKI_LOGE(audio_sender->log, "Error allocating audio frame history");
            return;
        }
        memcpy(audio_sender->framea, opus_sender, opus_sender_size);
        memcpy(audio_sender->frameb, opus_sender, opus_sender_size);
    }
    // frameb is two frames back and framea one frame back
    memcpy(audio_sender->frame_buf, opus_sender, opus_sender_size);
    memcpy(audio_sender->frame_buf + audio_sender->buf_size_per_unit, audio_sender->frameb, audio_sender->buf_size_per_unit);
    memcpy(audio_sender->frame_buf + 2 * audio_sender->buf_size_per_unit, audio_sender->framea, audio_sender->buf_size_per_unit);
    memcpy(audio_sender->frameb, audio_sender->framea, audio_sender->buf_size_per_unit);
    memcpy(audio_sender->framea, opus_sender, opus_sender_size);

    uint32_t filled_packet_size = audio_sender->frame_buf_size + 19 + ps5_packet;

    audio_sender->filled_packet_buf[0] = packet_type;
    *(chiaki_unaligned_uint16_t *)(audio_sender->filled_packet_buf + 1) = packet_index;
    *(chiaki_unaligned_uint16_t *)(audio_sender->filled_packet_buf + 3) = frame_index;
    *(chiaki_unaligned_uint32_t *)(audio_sender->filled_packet_buf + 5) = units_number;
    audio_sender->filled_packet_buf[9] = codec;
    *(chiaki_unaligned_uint32_t *)(audio_sender->filled_packet_buf + 10) = gmac;
    *(chiaki_unaligned_uint32_t *)(audio_sender->filled_packet_buf + 14) = key_pos;
    audio_sender->filled_packet_buf[18] = zero_byte;
    if(is_ps5)
        audio_sender->filled_packet_buf[19] = zero_byte;
    memcpy(audio_sender->filled_packet_buf + 19 + ps5_packet, audio_sender->frame_buf, audio_sender->frame_buf_size);
	chiaki_audio_sender_frame(audio_sender, audio_sender->filled_packet_buf, filled_packet_size);
    return;
}

static void chiaki_audio_sender_frame(ChiakiAudioSender *audio_sender, uint8_t *buf, size_t buf_size)
{
	ChiakiErrorCode err = chiaki_mutex_lock(&audio_sender->mutex);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(audio_sender->log, "Failed to lock audio sender mutex: %s", chiaki_error_string(err));
		return;
	}

	err = chiaki_takion_send_mic_packet(audio_sender->takion, buf, buf_size, audio_sender->ps5);
	if(err != CHIAKI_ERR_SUCCESS)
		CHIAKI_LOGE(audio_sender->log, "Failed to send mic audio packet: %s", chiaki_error_string(err));

	if(audio_sender->frame_index == UINT16_MAX)
		audio_sender->frame_index = 0;
	else
		audio_sender->frame_index++;
	err = chiaki_mutex_unlock(&audio_sender->mutex);
	if(err != CHIAKI_ERR_SUCCESS)
		CHIAKI_LOGE(audio_sender->log, "Failed to unlock audio sender mutex: %s", chiaki_error_string(err));
}
