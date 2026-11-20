// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_FEEDBACK_H
#define CHIAKI_FEEDBACK_H

#include "common.h"
#include "log.h"
#include "controller.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct chiaki_feedback_state_t
{
	float gyro_x, gyro_y, gyro_z;
	float accel_x, accel_y, accel_z;
	float orient_x, orient_y, orient_z, orient_w;
	int16_t left_x;
	int16_t left_y;
	int16_t right_x;
	int16_t right_y;
} ChiakiFeedbackState;

#define CHIAKI_FEEDBACK_STATE_BUF_SIZE_MAX 0x1c

#define CHIAKI_FEEDBACK_STATE_BUF_SIZE_V9 0x19

/**
 * @param buf buffer of at least CHIAKI_FEEDBACK_STATE_BUF_SIZE_V9
 */
CHIAKI_EXPORT void chiaki_feedback_state_format_v9(uint8_t *buf, ChiakiFeedbackState *state);

#define CHIAKI_FEEDBACK_STATE_BUF_SIZE_V12 0x1c

/**
 * @param buf buffer of at least CHIAKI_FEEDBACK_STATE_BUF_SIZE_V12
 */
CHIAKI_EXPORT void chiaki_feedback_state_format_v12(uint8_t *buf, ChiakiFeedbackState *state);

#define CHIAKI_HISTORY_EVENT_SIZE_MAX 0x5

typedef struct chiaki_feedback_history_event_t
{
	uint8_t buf[CHIAKI_HISTORY_EVENT_SIZE_MAX];
	size_t len;
} ChiakiFeedbackHistoryEvent;

/**
 * @param button ChiakiControllerButton or ChiakiControllerAnalogButton
 * @param state 0x0 for not pressed, 0xff for pressed, intermediate values for analog triggers
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_history_event_set_button(ChiakiFeedbackHistoryEvent *event, uint64_t button, uint8_t state);

/**
 * @param pointer_id identifier for the touch from 0 to 127
 * @param x from 0 to 1920
 * @param y from 0 to 942
 */
CHIAKI_EXPORT void chiaki_feedback_history_event_set_touchpad(ChiakiFeedbackHistoryEvent *event,
		bool down, uint8_t pointer_id, uint16_t x, uint16_t y);

/**
 * Ring buffer of ChiakiFeedbackHistoryEvent
 */
typedef struct chiaki_feedback_history_buffer_t
{
	ChiakiFeedbackHistoryEvent *events;
	size_t size;
	size_t begin;
	size_t len;
} ChiakiFeedbackHistoryBuffer;

CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_history_buffer_init(ChiakiFeedbackHistoryBuffer *feedback_history_buffer, size_t size);
CHIAKI_EXPORT void chiaki_feedback_history_buffer_fini(ChiakiFeedbackHistoryBuffer *feedback_history_buffer);

/**
 * @param buf_size Pointer to the allocated size of buf, will contain the written size after a successful formatting.
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_history_buffer_format(ChiakiFeedbackHistoryBuffer *feedback_history_buffer, uint8_t *buf, size_t *buf_size);

/**
 * Push an event to the front of the buffer
 */
CHIAKI_EXPORT void chiaki_feedback_history_buffer_push(ChiakiFeedbackHistoryBuffer *feedback_history_buffer, ChiakiFeedbackHistoryEvent *event);

typedef void (*ChiakiFeedbackHistoryEventPushedCallback)(void *user);

/**
 * Push the events for the change from state_prev to state_now, in the order a real controller produces them:
 * button releases, then touch changes, then button presses.
 * event_pushed is called after each push: the console applies only one new event per history packet,
 * so every event needs a packet of its own.
 * @return true if any event was pushed
 */
CHIAKI_EXPORT bool chiaki_feedback_history_buffer_record_state(ChiakiFeedbackHistoryBuffer *feedback_history_buffer, ChiakiLog *log,
		const ChiakiControllerState *state_prev, const ChiakiControllerState *state_now,
		ChiakiFeedbackHistoryEventPushedCallback event_pushed, void *event_pushed_user);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_FEEDBACK_H
