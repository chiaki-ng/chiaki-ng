// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_FRAMEPROCESSOR_H
#define CHIAKI_FRAMEPROCESSOR_H

#include "common.h"
#include "takion.h"
#include "packetstats.h"

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct chiaki_stream_stats_t
{
	uint64_t frames;
	uint64_t bytes;
} ChiakiStreamStats;

CHIAKI_EXPORT void chiaki_stream_stats_reset(ChiakiStreamStats *stats);
CHIAKI_EXPORT void chiaki_stream_stats_frame(ChiakiStreamStats *stats, uint64_t size);
CHIAKI_EXPORT uint64_t chiaki_stream_stats_bitrate(ChiakiStreamStats *stats, uint64_t framerate);

struct chiaki_frame_unit_t;
typedef struct chiaki_frame_unit_t ChiakiFrameUnit;

typedef struct chiaki_frame_processor_t
{
	ChiakiLog *log;
	uint8_t *frame_buf;
	size_t frame_buf_size;
	size_t frame_buf_prefix_size; // reserved, 16-byte aligned headroom before unit data
	size_t buf_size_per_unit;
	size_t buf_stride_per_unit;
	unsigned int units_source_expected;
	unsigned int units_fec_expected;
	unsigned int units_source_received;
	unsigned int units_fec_received;
	ChiakiFrameUnit *unit_slots;
	size_t unit_slots_size;
	bool flushed; // whether we have already flushed the current frame, i.e. are only interested in stats, not data.
	ChiakiStreamStats stream_stats;
} ChiakiFrameProcessor;

typedef enum chiaki_frame_flush_result_t {
	CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_SUCCESS = 0,
	CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FEC_SUCCESS = 1,
	CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FEC_FAILED = 2,
	CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FAILED = 3
} ChiakiFrameProcessorFlushResult;

CHIAKI_EXPORT void chiaki_frame_processor_init(ChiakiFrameProcessor *frame_processor, ChiakiLog *log);
CHIAKI_EXPORT void chiaki_frame_processor_fini(ChiakiFrameProcessor *frame_processor);

CHIAKI_EXPORT void chiaki_frame_processor_report_packet_stats(ChiakiFrameProcessor *frame_processor, ChiakiPacketStats *packet_stats);
CHIAKI_EXPORT ChiakiErrorCode chiaki_frame_processor_alloc_frame(ChiakiFrameProcessor *frame_processor, ChiakiTakionAVPacket *packet);
CHIAKI_EXPORT ChiakiErrorCode chiaki_frame_processor_put_unit(ChiakiFrameProcessor *frame_processor, ChiakiTakionAVPacket *packet);

/**
 * @param frame unless CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FAILED returned, will receive a pointer into the internal buffer of frame_processor.
 * MUST NOT be used after the next call to this frame processor!
 */
CHIAKI_EXPORT ChiakiFrameProcessorFlushResult chiaki_frame_processor_flush(ChiakiFrameProcessor *frame_processor, uint8_t **frame, size_t *frame_size);

static inline bool chiaki_frame_processor_flush_possible(ChiakiFrameProcessor *frame_processor)
{
	return frame_processor->units_source_received + frame_processor->units_fec_received
		>= frame_processor->units_source_expected;
}

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_FRAMEPROCESSOR_H
