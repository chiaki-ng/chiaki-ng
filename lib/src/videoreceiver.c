// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/videoreceiver.h>
#include <chiaki/time.h>
#include "../include/chiaki/session.h"

#include <string.h>

// While waiting for a requested IDR frame, request it again this often, in case the request
// or the IDR frame itself was lost; otherwise the picture would stay frozen
#define VIDEO_IDR_RETRY_INTERVAL_MS 1000

static ChiakiErrorCode chiaki_video_receiver_flush_frame(ChiakiVideoReceiver *video_receiver);

static void add_ref_frame(ChiakiVideoReceiver *video_receiver, int32_t frame)
{
	if(video_receiver->reference_frames[0] != -1)
	{
		memmove(&video_receiver->reference_frames[1], &video_receiver->reference_frames[0], sizeof(int32_t) * 15);
		video_receiver->reference_frames[0] = frame;
		return;
	}
	for(int i=15; i>=0; i--)
	{
		if(video_receiver->reference_frames[i] == -1)
		{
			video_receiver->reference_frames[i] = frame;
			return;
		}
	}
}

static bool have_ref_frame(ChiakiVideoReceiver *video_receiver, int32_t frame)
{
	for(int i=0; i<16; i++)
		if(video_receiver->reference_frames[i] == frame)
			return true;
	return false;
}

CHIAKI_EXPORT void chiaki_video_receiver_init(ChiakiVideoReceiver *video_receiver, struct chiaki_session_t *session, ChiakiPacketStats *packet_stats)
{
	video_receiver->session = session;
	video_receiver->log = session->log;
	memset(video_receiver->profiles, 0, sizeof(video_receiver->profiles));
	video_receiver->profiles_count = 0;
	video_receiver->profile_cur = -1;

	video_receiver->frame_index_cur = -1;
	video_receiver->frame_index_prev = -1;
	video_receiver->frame_index_prev_complete = 0;

	chiaki_frame_processor_init(&video_receiver->frame_processor, video_receiver->log);
	video_receiver->packet_stats = packet_stats;

	video_receiver->frames_lost = 0;
	video_receiver->frames_lost_total = 0;
	memset(video_receiver->reference_frames, -1, sizeof(video_receiver->reference_frames));
	chiaki_bitstream_init(&video_receiver->bitstream, video_receiver->log, video_receiver->session->connect_info.video_profile.codec);
	chiaki_mutex_init(&video_receiver->waiting_for_idr_mutex, false);
	video_receiver->waiting_for_idr = false;
	video_receiver->idr_request_last_ms = 0;
	chiaki_mutex_init(&video_receiver->frames_lost_mutex, false);
}

CHIAKI_EXPORT void chiaki_video_receiver_fini(ChiakiVideoReceiver *video_receiver)
{
	for(size_t i=0; i<video_receiver->profiles_count; i++)
		free(video_receiver->profiles[i].header);
	chiaki_mutex_fini(&video_receiver->waiting_for_idr_mutex);
	chiaki_mutex_fini(&video_receiver->frames_lost_mutex);
	chiaki_frame_processor_fini(&video_receiver->frame_processor);
}

CHIAKI_EXPORT void chiaki_video_receiver_set_waiting_for_idr(ChiakiVideoReceiver *video_receiver, bool waiting_for_idr)
{
	chiaki_mutex_lock(&video_receiver->waiting_for_idr_mutex);
	video_receiver->waiting_for_idr = waiting_for_idr;
	if(waiting_for_idr)
		video_receiver->idr_request_last_ms = chiaki_time_now_monotonic_ms();
	chiaki_mutex_unlock(&video_receiver->waiting_for_idr_mutex);
}

/**
 * While waiting for an IDR frame, request it again once VIDEO_IDR_RETRY_INTERVAL_MS has passed
 * since it was last requested.
 */
static void video_receiver_retry_idr_request(ChiakiVideoReceiver *video_receiver)
{
	chiaki_mutex_lock(&video_receiver->waiting_for_idr_mutex);
	uint64_t now_ms = chiaki_time_now_monotonic_ms();
	bool due = video_receiver->waiting_for_idr
		&& now_ms - video_receiver->idr_request_last_ms >= VIDEO_IDR_RETRY_INTERVAL_MS;
	if(due)
		video_receiver->idr_request_last_ms = now_ms;
	chiaki_mutex_unlock(&video_receiver->waiting_for_idr_mutex);
	if(!due)
		return;

	ChiakiErrorCode err = stream_connection_send_idr_request(&video_receiver->session->stream_connection);
	if(err == CHIAKI_ERR_SUCCESS)
		CHIAKI_LOGI(video_receiver->log, "Still waiting for IDR frame, requested it again");
	else
		CHIAKI_LOGW(video_receiver->log, "Still waiting for IDR frame and the request could not be sent again: %s", chiaki_error_string(err));
}

CHIAKI_EXPORT bool chiaki_video_receiver_get_waiting_for_idr(ChiakiVideoReceiver *video_receiver)
{
	bool waiting_for_idr;
	chiaki_mutex_lock(&video_receiver->waiting_for_idr_mutex);
	waiting_for_idr = video_receiver->waiting_for_idr;
	chiaki_mutex_unlock(&video_receiver->waiting_for_idr_mutex);
	return waiting_for_idr;
}

CHIAKI_EXPORT int32_t chiaki_video_receiver_get_frames_lost_total(ChiakiVideoReceiver *video_receiver)
{
	int32_t total;
	chiaki_mutex_lock(&video_receiver->frames_lost_mutex);
	total = video_receiver->frames_lost_total;
	chiaki_mutex_unlock(&video_receiver->frames_lost_mutex);
	return total;
}

CHIAKI_EXPORT void chiaki_video_receiver_stream_info(ChiakiVideoReceiver *video_receiver, ChiakiVideoProfile *profiles, size_t profiles_count)
{
	if(video_receiver->profiles_count > 0)
	{
		CHIAKI_LOGE(video_receiver->log, "Video Receiver profiles already set");
		return;
	}

	memcpy(video_receiver->profiles, profiles, profiles_count * sizeof(ChiakiVideoProfile));
	video_receiver->profiles_count = profiles_count;

	CHIAKI_LOGI(video_receiver->log, "Video Profiles:");
	for(size_t i=0; i<video_receiver->profiles_count; i++)
	{
		ChiakiVideoProfile *profile = &video_receiver->profiles[i];
		CHIAKI_LOGI(video_receiver->log, "  %zu: %ux%u", i, profile->width, profile->height);
		//chiaki_log_hexdump(video_receiver->log, CHIAKI_LOG_DEBUG, profile->header, profile->header_sz);
	}
}

CHIAKI_EXPORT void chiaki_video_receiver_av_packet(ChiakiVideoReceiver *video_receiver, ChiakiTakionAVPacket *packet)
{
	// old frame?
	ChiakiSeqNum16 frame_index = packet->frame_index;
	ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
	if(video_receiver->frame_index_cur >= 0
		&& chiaki_seq_num_16_lt(frame_index, (ChiakiSeqNum16)video_receiver->frame_index_cur))
	{
		CHIAKI_LOGW(video_receiver->log, "Video Receiver received old frame packet");
		return;
	}

	// check adaptive stream index
	if(video_receiver->profile_cur < 0 || video_receiver->profile_cur != packet->adaptive_stream_index)
	{
		if(packet->adaptive_stream_index >= video_receiver->profiles_count)
		{
			CHIAKI_LOGE(video_receiver->log, "Packet has invalid adaptive stream index %u >= %u",
					(unsigned int)packet->adaptive_stream_index,
					(unsigned int)video_receiver->profiles_count);
			return;
		}
		video_receiver->profile_cur = packet->adaptive_stream_index;

		ChiakiVideoProfile *profile = video_receiver->profiles + video_receiver->profile_cur;
		CHIAKI_LOGI(video_receiver->log, "Switched to profile %d, resolution: %ux%u", video_receiver->profile_cur, profile->width, profile->height);
		if(video_receiver->session->video_sample_cb)
			video_receiver->session->video_sample_cb(profile->header, profile->header_sz, 0, false, video_receiver->session->video_sample_cb_user);
		if(!chiaki_bitstream_header(&video_receiver->bitstream, profile->header, profile->header_sz))
			CHIAKI_LOGW(video_receiver->log, "Failed to parse video header");
	}

	// next frame?
	if(video_receiver->frame_index_cur < 0 ||
		chiaki_seq_num_16_gt(frame_index, (ChiakiSeqNum16)video_receiver->frame_index_cur))
	{
		if(video_receiver->packet_stats)
			chiaki_frame_processor_report_packet_stats(&video_receiver->frame_processor, video_receiver->packet_stats);

		// last frame not flushed yet?
		if(video_receiver->frame_index_cur >= 0 && video_receiver->frame_index_prev != video_receiver->frame_index_cur)
			err = chiaki_video_receiver_flush_frame(video_receiver);

		if(err != CHIAKI_ERR_SUCCESS)
			CHIAKI_LOGW(video_receiver->log, "Video receiver could not flush frame.");

		ChiakiSeqNum16 next_frame_expected = (ChiakiSeqNum16)(video_receiver->frame_index_prev_complete + 1);
		if(chiaki_seq_num_16_gt(frame_index, next_frame_expected)
			&& !(frame_index == 1 && video_receiver->frame_index_cur < 0)) // ok for frame 1
		{
			CHIAKI_LOGW(video_receiver->log, "Detected missing or corrupt frame(s) from %d to %d", next_frame_expected, (int)frame_index);
			err = stream_connection_send_corrupt_frame(&video_receiver->session->stream_connection, next_frame_expected, frame_index - 1);
			if(err != CHIAKI_ERR_SUCCESS)
				CHIAKI_LOGW(video_receiver->log, "Error sending corrupt frame.");
		}

		video_receiver->frame_index_cur = frame_index;
		err = chiaki_frame_processor_alloc_frame(&video_receiver->frame_processor, packet);
		if(err != CHIAKI_ERR_SUCCESS)
			CHIAKI_LOGW(video_receiver->log, "Video receiver could not allocate frame for packet.");
	}

	err = chiaki_frame_processor_put_unit(&video_receiver->frame_processor, packet);
	if(err != CHIAKI_ERR_SUCCESS)
		CHIAKI_LOGW(video_receiver->log, "Video receiver could not put unit.");

	// if we are currently building up a frame
	if(video_receiver->frame_index_cur != video_receiver->frame_index_prev)
	{
		// if we already have enough for the whole frame, flush it already
		if(chiaki_frame_processor_flush_possible(&video_receiver->frame_processor) || packet->unit_index == packet->units_in_frame_total - 1)
			err = chiaki_video_receiver_flush_frame(video_receiver);
		if(err != CHIAKI_ERR_SUCCESS)
			CHIAKI_LOGW(video_receiver->log, "Video receiver could not flush frame.");
	}
}

static ChiakiErrorCode chiaki_video_receiver_flush_frame(ChiakiVideoReceiver *video_receiver)
{
	uint8_t *frame;
	size_t frame_size;
	ChiakiFrameProcessorFlushResult flush_result = chiaki_frame_processor_flush(&video_receiver->frame_processor, &frame, &frame_size);

	if(flush_result == CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FAILED
		|| flush_result == CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FEC_FAILED)
	{
		if (flush_result == CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FEC_FAILED)
		{
			ChiakiSeqNum16 next_frame_expected = (ChiakiSeqNum16)(video_receiver->frame_index_prev_complete + 1);
			stream_connection_send_corrupt_frame(&video_receiver->session->stream_connection, next_frame_expected, video_receiver->frame_index_cur);
			if(video_receiver->session->connect_info.enable_idr_on_fec_failure)
			{
				bool waiting_for_idr = chiaki_video_receiver_get_waiting_for_idr(video_receiver);
				bool idr_request_sent = waiting_for_idr;
				if(!waiting_for_idr)
				{
					ChiakiErrorCode err = stream_connection_send_idr_request(&video_receiver->session->stream_connection);
					idr_request_sent = err == CHIAKI_ERR_SUCCESS;
					if(err == CHIAKI_ERR_SUCCESS)
					{
						chiaki_video_receiver_set_waiting_for_idr(video_receiver, true);
						CHIAKI_LOGI(video_receiver->log, "FEC failed, waiting for IDR frame");
					}
					else
					{
						CHIAKI_LOGW(video_receiver->log, "FEC failed and IDR request could not be sent: %s", chiaki_error_string(err));
					}
				}
				else
				{
					CHIAKI_LOGW(video_receiver->log, "Video FEC failure, already waiting for requested IDR");
					video_receiver_retry_idr_request(video_receiver);
				}
				ChiakiEvent event = { 0 };
				event.type = CHIAKI_EVENT_VIDEO_FEC_FAILURE;
				event.video_fec_failure.frame_index = video_receiver->frame_index_cur;
				event.video_fec_failure.idr_request_sent = idr_request_sent;
				chiaki_session_send_event(video_receiver->session, &event);
			}
		int32_t lost = video_receiver->frame_index_cur - next_frame_expected + 1;
		chiaki_mutex_lock(&video_receiver->frames_lost_mutex);
		video_receiver->frames_lost += lost;
		video_receiver->frames_lost_total += lost;
		chiaki_mutex_unlock(&video_receiver->frames_lost_mutex);
		video_receiver->frame_index_prev = video_receiver->frame_index_cur;
	}
		CHIAKI_LOGW(video_receiver->log, "Failed to complete frame %d", (int)video_receiver->frame_index_cur);
		return CHIAKI_ERR_UNKNOWN;
	}

	bool succ = flush_result != CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FEC_FAILED;
	bool recovered = false;

	ChiakiBitstreamSlice slice;
	if(chiaki_bitstream_slice(&video_receiver->bitstream, frame, frame_size, &slice))
	{
		if(chiaki_video_receiver_get_waiting_for_idr(video_receiver))
		{
			if(slice.slice_type == CHIAKI_BITSTREAM_SLICE_I)
			{
				chiaki_video_receiver_set_waiting_for_idr(video_receiver, false);
				CHIAKI_LOGI(video_receiver->log, "Received IDR frame, resuming decode");
			}
			else
			{
				CHIAKI_LOGV(video_receiver->log, "Skipping P-frame %d while waiting for IDR", (int)video_receiver->frame_index_cur);
				video_receiver_retry_idr_request(video_receiver);
				video_receiver->frame_index_prev = video_receiver->frame_index_cur;
				return CHIAKI_ERR_SUCCESS;
			}
		}

		if(slice.slice_type == CHIAKI_BITSTREAM_SLICE_P)
		{
			ChiakiSeqNum16 ref_frame_index = video_receiver->frame_index_cur - slice.reference_frame - 1;
			if(slice.reference_frame != 0xff && !have_ref_frame(video_receiver, ref_frame_index))
			{
				for(unsigned i=slice.reference_frame+1; i<16; i++)
				{
					ChiakiSeqNum16 ref_frame_index_new = video_receiver->frame_index_cur - i - 1;
					if(have_ref_frame(video_receiver, ref_frame_index_new))
					{
						if(chiaki_bitstream_slice_set_reference_frame(&video_receiver->bitstream, frame, frame_size, i))
						{
							recovered = true;
							CHIAKI_LOGW(video_receiver->log, "Missing reference frame %d for decoding frame %d -> changed to %d", (int)ref_frame_index, (int)video_receiver->frame_index_cur, (int)ref_frame_index_new);
						}
						break;
					}
				}
				if(!recovered)
				{
					succ = false;
					chiaki_mutex_lock(&video_receiver->frames_lost_mutex);
					video_receiver->frames_lost++;
					video_receiver->frames_lost_total++;
					chiaki_mutex_unlock(&video_receiver->frames_lost_mutex);
					CHIAKI_LOGW(video_receiver->log, "Missing reference frame %d for decoding frame %d", (int)ref_frame_index, (int)video_receiver->frame_index_cur);
				}
			}
		}
	}

	if(succ && video_receiver->session->video_sample_cb)
	{
		bool cb_succ = video_receiver->session->video_sample_cb(frame, frame_size, video_receiver->frames_lost, recovered, video_receiver->session->video_sample_cb_user);
		chiaki_mutex_lock(&video_receiver->frames_lost_mutex);
		video_receiver->frames_lost = 0;
		chiaki_mutex_unlock(&video_receiver->frames_lost_mutex);
		if(!cb_succ)
		{
			succ = false;
			CHIAKI_LOGW(video_receiver->log, "Video callback did not process frame successfully.");
		}
		else
		{
			add_ref_frame(video_receiver, video_receiver->frame_index_cur);
			CHIAKI_LOGV(video_receiver->log, "Added reference %c frame %d", slice.slice_type == CHIAKI_BITSTREAM_SLICE_I ? 'I' : 'P', (int)video_receiver->frame_index_cur);
		}
	}

	video_receiver->frame_index_prev = video_receiver->frame_index_cur;

	if(succ)
		video_receiver->frame_index_prev_complete = video_receiver->frame_index_cur;

	return CHIAKI_ERR_SUCCESS;
}
