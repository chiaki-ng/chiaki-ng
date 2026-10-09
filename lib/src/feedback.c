// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#define _USE_MATH_DEFINES

#include <chiaki/feedback.h>
#include <chiaki/controller.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif
#include <string.h>
#include <math.h>

#define GYRO_MIN -30.0f
#define GYRO_MAX 30.0f
#define ACCEL_MIN -5.0f
#define ACCEL_MAX 5.0f

static uint32_t compress_quat(float *q)
{
	// very similar idea as https://github.com/jpreiss/quatcompress
	size_t largest_i = 0;
	for(size_t i = 1; i < 4; i++)
	{
		if(fabs(q[i]) > fabs(q[largest_i]))
			largest_i = i;
	}
	uint32_t r = (q[largest_i] < 0.0 ? 1 : 0) | (largest_i << 1);
	for(size_t i = 0; i < 3; i++)
	{
		size_t qi = i < largest_i ? i : i + 1;
		float v = q[qi];
		if(v < -M_SQRT1_2)
			v = -M_SQRT1_2;
		if(v > M_SQRT1_2)
			v = M_SQRT1_2;
		v += M_SQRT1_2;
		v *= (float)0x1ff / (2.0f * M_SQRT1_2);
		r |= (uint32_t)v << (3 + i * 9);
	}
	return r;
}

CHIAKI_EXPORT void chiaki_feedback_state_format_v9(uint8_t *buf, ChiakiFeedbackState *state)
{
	buf[0x0] = 0xa0;
	uint16_t v = (uint16_t)(0xffff * ((float)state->gyro_x - GYRO_MIN) / (GYRO_MAX - GYRO_MIN));
	buf[0x1] = v;
	buf[0x2] = v >> 8;
	v = (uint16_t)(0xffff * ((float)state->gyro_y - GYRO_MIN) / (GYRO_MAX - GYRO_MIN));
	buf[0x3] = v;
	buf[0x4] = v >> 8;
	v = (uint16_t)(0xffff * ((float)state->gyro_z - GYRO_MIN) / (GYRO_MAX - GYRO_MIN));
	buf[0x5] = v;
	buf[0x6] = v >> 8;
	v = (uint16_t)(0xffff * ((float)state->accel_x - ACCEL_MIN) / (ACCEL_MAX - ACCEL_MIN));
	buf[0x7] = v;
	buf[0x8] = v >> 8;
	v = (uint16_t)(0xffff * ((float)state->accel_y - ACCEL_MIN) / (ACCEL_MAX - ACCEL_MIN));
	buf[0x9] = v;
	buf[0xa] = v >> 8;
	v = (uint16_t)(0xffff * ((float)state->accel_z - ACCEL_MIN) / (ACCEL_MAX - ACCEL_MIN));
	buf[0xb] = v;
	buf[0xc] = v >> 8;
	float q[4] = { state->orient_x, state->orient_y, state->orient_z, state->orient_w };
	uint32_t qc = compress_quat(q);
	buf[0xd] = qc;
	buf[0xe] = qc >> 0x8;
	buf[0xf] = qc >> 0x10;
	buf[0x10] = qc >> 0x18;
	*((chiaki_unaligned_uint16_t *)(buf + 0x11)) = htons((uint16_t)state->left_x);
	*((chiaki_unaligned_uint16_t *)(buf + 0x13)) = htons((uint16_t)state->left_y);
	*((chiaki_unaligned_uint16_t *)(buf + 0x15)) = htons((uint16_t)state->right_x);
	*((chiaki_unaligned_uint16_t *)(buf + 0x17)) = htons((uint16_t)state->right_y);
}

CHIAKI_EXPORT void chiaki_feedback_state_format_v12(uint8_t *buf, ChiakiFeedbackState *state)
{
	chiaki_feedback_state_format_v9(buf, state);
	buf[0x19] = 0x0;
	buf[0x1a] = 0x0;
	buf[0x1b] = 0x1;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_history_event_set_button(ChiakiFeedbackHistoryEvent *event, uint64_t button, uint8_t state)
{
	// some buttons use a third byte for the state, some don't
	event->buf[0] = 0x80;
	event->len = 2;
	switch(button)
	{
		case CHIAKI_CONTROLLER_BUTTON_CROSS:
			event->buf[1] = 0x88;
			break;
		case CHIAKI_CONTROLLER_BUTTON_MOON:
			event->buf[1] = 0x89;
			break;
		case CHIAKI_CONTROLLER_BUTTON_BOX:
			event->buf[1] = 0x8a;
			break;
		case CHIAKI_CONTROLLER_BUTTON_PYRAMID:
			event->buf[1] = 0x8b;
			break;
		case CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT:
			event->buf[1] = 0x82;
			break;
		case CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT:
			event->buf[1] = 0x83;
			break;
		case CHIAKI_CONTROLLER_BUTTON_DPAD_UP:
			event->buf[1] = 0x80;
			break;
		case CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN:
			event->buf[1] = 0x81;
			break;
		case CHIAKI_CONTROLLER_BUTTON_L1:
			event->buf[1] = 0x84;
			break;
		case CHIAKI_CONTROLLER_BUTTON_R1:
			event->buf[1] = 0x85;
			break;
		case CHIAKI_CONTROLLER_ANALOG_BUTTON_L2:
			event->buf[1] = 0x86;
			break;
		case CHIAKI_CONTROLLER_ANALOG_BUTTON_R2:
			event->buf[1] = 0x87;
			break;
		case CHIAKI_CONTROLLER_BUTTON_L3:
			event->buf[1] = state ? 0xaf : 0x8f;
			return CHIAKI_ERR_SUCCESS;
		case CHIAKI_CONTROLLER_BUTTON_R3:
			event->buf[1] = state ? 0xb0 : 0x90;
			return CHIAKI_ERR_SUCCESS;
		case CHIAKI_CONTROLLER_BUTTON_OPTIONS:
			event->buf[1] = state ? 0xac : 0x8c;
			return CHIAKI_ERR_SUCCESS;
		case CHIAKI_CONTROLLER_BUTTON_SHARE:
			event->buf[1] = state ? 0xad : 0x8d;
			return CHIAKI_ERR_SUCCESS;
		case CHIAKI_CONTROLLER_BUTTON_TOUCHPAD:
			event->buf[1] = state ? 0xb1 : 0x91;
			return CHIAKI_ERR_SUCCESS;
		case CHIAKI_CONTROLLER_BUTTON_PS:
			event->buf[1] = state ? 0xae : 0x8e;
			return CHIAKI_ERR_SUCCESS;
		default:
			return CHIAKI_ERR_INVALID_DATA;
	}
	event->buf[2] = state;
	event->len = 3;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT void chiaki_feedback_history_event_set_touchpad(ChiakiFeedbackHistoryEvent *event,
		bool down, uint8_t pointer_id, uint16_t x, uint16_t y)
{
	event->len = 5;
	event->buf[0] = down ? 0xd0 : 0xc0;
	event->buf[1] = pointer_id & 0x7f;
	event->buf[2] = (uint8_t)(x >> 4);
	event->buf[3] = (uint8_t)((x & 0xf) << 4) | (uint8_t)(y >> 8);
	event->buf[4] = (uint8_t)y;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_history_buffer_init(ChiakiFeedbackHistoryBuffer *feedback_history_buffer, size_t size)
{
	feedback_history_buffer->events = calloc(size, sizeof(ChiakiFeedbackHistoryEvent));
	if(!feedback_history_buffer->events)
		return CHIAKI_ERR_MEMORY;
	feedback_history_buffer->size = size;
	feedback_history_buffer->begin = 0;
	feedback_history_buffer->len = 0;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT void chiaki_feedback_history_buffer_fini(ChiakiFeedbackHistoryBuffer *feedback_history_buffer)
{
	free(feedback_history_buffer->events);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_history_buffer_format(ChiakiFeedbackHistoryBuffer *feedback_history_buffer, uint8_t *buf, size_t *buf_size)
{
	size_t size_max = *buf_size;
	size_t written = 0;

	for(size_t i=0; i<feedback_history_buffer->len; i++)
	{
		ChiakiFeedbackHistoryEvent *event = &feedback_history_buffer->events[(feedback_history_buffer->begin + i) % feedback_history_buffer->size];
		if(written + event->len > size_max)
			return CHIAKI_ERR_BUF_TOO_SMALL;
		memcpy(buf + written, event->buf, event->len);
		written += event->len;
	}

	*buf_size = written;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT void chiaki_feedback_history_buffer_push(ChiakiFeedbackHistoryBuffer *feedback_history_buffer, ChiakiFeedbackHistoryEvent *event)
{
	feedback_history_buffer->begin = (feedback_history_buffer->begin + feedback_history_buffer->size - 1) % feedback_history_buffer->size;
	feedback_history_buffer->len++;
	if(feedback_history_buffer->len >= feedback_history_buffer->size)
		feedback_history_buffer->len = feedback_history_buffer->size;
	feedback_history_buffer->events[feedback_history_buffer->begin] = *event;
}

typedef struct history_recorder_t
{
	ChiakiFeedbackHistoryBuffer *buffer;
	ChiakiLog *log;
	ChiakiFeedbackHistoryEventPushedCallback event_pushed;
	void *event_pushed_user;
	bool recorded;
} HistoryRecorder;

static void history_recorder_push(HistoryRecorder *recorder, ChiakiFeedbackHistoryEvent *event)
{
	chiaki_feedback_history_buffer_push(recorder->buffer, event);
	recorder->recorded = true;
	if(recorder->event_pushed)
		recorder->event_pushed(recorder->event_pushed_user);
}

static void history_recorder_buttons(HistoryRecorder *recorder, uint64_t buttons_prev, uint64_t buttons_now, bool pressed)
{
	for(uint8_t i=0; i<CHIAKI_CONTROLLER_BUTTONS_COUNT; i++)
	{
		uint64_t button_id = 1 << i;
		bool prev = buttons_prev & button_id;
		bool now = buttons_now & button_id;
		if(prev == now || now != pressed)
			continue;
		ChiakiFeedbackHistoryEvent event;
		ChiakiErrorCode err = chiaki_feedback_history_event_set_button(&event, button_id, now ? 0xff : 0);
		if(err != CHIAKI_ERR_SUCCESS)
		{
			CHIAKI_LOGE(recorder->log, "Feedback Sender failed to format button history event for button id %llu", (unsigned long long)button_id);
			continue;
		}
		history_recorder_push(recorder, &event);
	}
}

static void history_recorder_analog(HistoryRecorder *recorder, uint64_t button, uint8_t state_prev, uint8_t state_now)
{
	if(state_prev == state_now)
		return;
	ChiakiFeedbackHistoryEvent event;
	ChiakiErrorCode err = chiaki_feedback_history_event_set_button(&event, button, state_now);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(recorder->log, "Feedback Sender failed to format button history event for %s",
				button == CHIAKI_CONTROLLER_ANALOG_BUTTON_L2 ? "L2" : "R2");
		return;
	}
	history_recorder_push(recorder, &event);
}

CHIAKI_EXPORT bool chiaki_feedback_history_buffer_record_state(ChiakiFeedbackHistoryBuffer *feedback_history_buffer, ChiakiLog *log,
		const ChiakiControllerState *state_prev, const ChiakiControllerState *state_now,
		ChiakiFeedbackHistoryEventPushedCallback event_pushed, void *event_pushed_user)
{
	HistoryRecorder recorder = { feedback_history_buffer, log, event_pushed, event_pushed_user, false };

	// Releases go before touch changes: when a touchpad click and its finger are released
	// in one state change, the finger must not lift while the click is still held.
	history_recorder_buttons(&recorder, state_prev->buttons, state_now->buttons, false);

	for(size_t i=0; i<CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
	{
		if(state_prev->touches[i].id != state_now->touches[i].id && state_prev->touches[i].id >= 0)
		{
			ChiakiFeedbackHistoryEvent event;
			chiaki_feedback_history_event_set_touchpad(&event, false, (uint8_t)state_prev->touches[i].id,
					state_prev->touches[i].x, state_prev->touches[i].y);
			history_recorder_push(&recorder, &event);
		}
		else if(state_now->touches[i].id >= 0
				&& (state_prev->touches[i].id != state_now->touches[i].id
					|| state_prev->touches[i].x != state_now->touches[i].x
					|| state_prev->touches[i].y != state_now->touches[i].y))
		{
			ChiakiFeedbackHistoryEvent event;
			chiaki_feedback_history_event_set_touchpad(&event, true, (uint8_t)state_now->touches[i].id,
					state_now->touches[i].x, state_now->touches[i].y);
			history_recorder_push(&recorder, &event);
		}
	}

	history_recorder_buttons(&recorder, state_prev->buttons, state_now->buttons, true);
	history_recorder_analog(&recorder, CHIAKI_CONTROLLER_ANALOG_BUTTON_L2, state_prev->l2_state, state_now->l2_state);
	history_recorder_analog(&recorder, CHIAKI_CONTROLLER_ANALOG_BUTTON_R2, state_prev->r2_state, state_now->r2_state);
	return recorder.recorded;
}
