// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_FEEDBACKSENDER_H
#define CHIAKI_FEEDBACKSENDER_H

#include "controller.h"
#include "takion.h"
#include "thread.h"
#include "common.h"

#define CHIAKI_FEEDBACK_HISTORY_PACKET_BUF_SIZE 0x300
#define CHIAKI_FEEDBACK_HISTORY_PACKET_QUEUE_SIZE 0x40

#ifdef __cplusplus
extern "C" {
#endif

typedef struct chiaki_feedback_sender_t
{
	ChiakiLog *log;
	ChiakiTakion *takion;
	ChiakiThread thread;

	ChiakiSeqNum16 state_seq_num;

	ChiakiSeqNum16 history_seq_num;
	ChiakiFeedbackHistoryBuffer history_buf;
	uint8_t history_packets[CHIAKI_FEEDBACK_HISTORY_PACKET_QUEUE_SIZE][CHIAKI_FEEDBACK_HISTORY_PACKET_BUF_SIZE];
	size_t history_packet_sizes[CHIAKI_FEEDBACK_HISTORY_PACKET_QUEUE_SIZE];
	uint8_t history_packet_event_counts[CHIAKI_FEEDBACK_HISTORY_PACKET_QUEUE_SIZE];
	size_t history_packet_begin;
	size_t history_packet_len;
	// Last history packet sent, repeated a few times while nothing new happens (see feedbacksender.c)
	uint8_t history_last_packet[CHIAKI_FEEDBACK_HISTORY_PACKET_BUF_SIZE];
	size_t history_last_size;
	uint8_t history_last_event_count;
	uint64_t history_last_sent_ms;
	unsigned int history_tail_resends;

	bool should_stop;
	ChiakiControllerState controller_state_prev;
	ChiakiControllerState controller_state_history_prev;
	ChiakiControllerState controller_state;
	bool controller_state_changed;
	bool history_dirty;
	ChiakiMutex state_mutex;
	ChiakiCond state_cond;
} ChiakiFeedbackSender;

CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_sender_init(ChiakiFeedbackSender *feedback_sender, ChiakiTakion *takion);
CHIAKI_EXPORT void chiaki_feedback_sender_fini(ChiakiFeedbackSender *feedback_sender);
CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_sender_set_controller_state(ChiakiFeedbackSender *feedback_sender, ChiakiControllerState *state);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_FEEDBACKSENDER_H
