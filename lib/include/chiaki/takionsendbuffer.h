// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_TAKIONSENDBUFFER_H
#define CHIAKI_TAKIONSENDBUFFER_H

#include "common.h"
#include "log.h"
#include "thread.h"
#include "seqnum.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct chiaki_takion_t ChiakiTakion;

// Unacknowledged reliable packets kept for resending: enough for every packet within the
// console's receive window, so a burst sent during loss is resent rather than sent once untracked
#define CHIAKI_TAKION_SEND_BUFFER_SIZE 1024

typedef struct chiaki_takion_send_buffer_packet_t ChiakiTakionSendBufferPacket;

typedef struct chiaki_takion_send_buffer_t
{
	ChiakiLog *log;
	ChiakiTakion *takion;

	ChiakiTakionSendBufferPacket *packets;
	size_t packets_size; // allocated size
	size_t packets_count; // current count

	ChiakiMutex mutex;
	ChiakiCond cond;
	bool should_stop;
	ChiakiThread thread;
} ChiakiTakionSendBuffer;


/**
 * Init a Send Buffer and start a thread that automatically re-sends packets on takion.
 *
 * @param takion if NULL, the Send Buffer thread will effectively do nothing (for unit testing)
 * @param size number of packet slots
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_takion_send_buffer_init(ChiakiTakionSendBuffer *send_buffer, ChiakiTakion *takion, size_t size);
CHIAKI_EXPORT void chiaki_takion_send_buffer_fini(ChiakiTakionSendBuffer *send_buffer);

/**
 * @param buf ownership of this is taken by the ChiakiTakionSendBuffer, which will free it automatically later!
 * On error, buf is freed immediately.
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_takion_send_buffer_push(ChiakiTakionSendBuffer *send_buffer, ChiakiSeqNum32 seq_num, uint8_t *buf, size_t buf_size);

/**
 * @param acked_seq_nums optional array of size of at least send_buffer->packets_size where acked seq nums will be stored
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_takion_send_buffer_ack(ChiakiTakionSendBuffer *send_buffer, ChiakiSeqNum32 seq_num, ChiakiSeqNum32 *acked_seq_nums, size_t *acked_seq_nums_count);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_TAKIONSENDBUFFER_H
