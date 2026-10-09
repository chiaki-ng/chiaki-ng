// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/feedbacksender.h>
#include <chiaki/time.h>

#include <string.h>

#define FEEDBACK_STATE_TIMEOUT_MIN_MS 8 // minimum time to wait between sending 2 packets
#define FEEDBACK_STATE_TIMEOUT_MAX_MS 200 // maximum time to wait between sending 2 packets

#define FEEDBACK_HISTORY_BUFFER_SIZE 0x10
#define FEEDBACK_HISTORY_RESEND_EVENT_COUNT 0x4

static void *feedback_sender_thread_func(void *user);
static void feedback_sender_send_state(ChiakiFeedbackSender *feedback_sender, const ChiakiControllerState *state);
static void feedback_sender_send_history_packet(ChiakiFeedbackSender *feedback_sender, const uint8_t *buf, size_t buf_size);
static void feedback_sender_flush_history_locked(ChiakiFeedbackSender *feedback_sender);
static void feedback_sender_record_history(ChiakiFeedbackSender *feedback_sender, const ChiakiControllerState *state_prev, const ChiakiControllerState *state_now);

CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_sender_init(ChiakiFeedbackSender *feedback_sender, ChiakiTakion *takion)
{
	feedback_sender->log = takion->log;
	feedback_sender->takion = takion;

	chiaki_controller_state_set_idle(&feedback_sender->controller_state_prev);
	chiaki_controller_state_set_idle(&feedback_sender->controller_state_history_prev);
	chiaki_controller_state_set_idle(&feedback_sender->controller_state);

	feedback_sender->state_seq_num = 0;

	feedback_sender->history_seq_num = 0;
	feedback_sender->history_packet_begin = 0;
	feedback_sender->history_packet_len = 0;
	feedback_sender->should_stop = false;
	feedback_sender->controller_state_changed = false;
	feedback_sender->history_dirty = false;
	ChiakiErrorCode err = chiaki_feedback_history_buffer_init(&feedback_sender->history_buf, FEEDBACK_HISTORY_BUFFER_SIZE);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	err = chiaki_mutex_init(&feedback_sender->state_mutex, false);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_history_buffer;

	err = chiaki_cond_init(&feedback_sender->state_cond);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_mutex;

	err = chiaki_thread_create(&feedback_sender->thread, feedback_sender_thread_func, feedback_sender);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_cond;

	chiaki_thread_set_name(&feedback_sender->thread, "Chiaki Feedback Sender");

	return CHIAKI_ERR_SUCCESS;
error_cond:
	chiaki_cond_fini(&feedback_sender->state_cond);
error_mutex:
	chiaki_mutex_fini(&feedback_sender->state_mutex);
error_history_buffer:
	chiaki_feedback_history_buffer_fini(&feedback_sender->history_buf);
	return err;
}

CHIAKI_EXPORT void chiaki_feedback_sender_fini(ChiakiFeedbackSender *feedback_sender)
{
	chiaki_mutex_lock(&feedback_sender->state_mutex);
	feedback_sender->should_stop = true;
	chiaki_mutex_unlock(&feedback_sender->state_mutex);
	chiaki_cond_signal(&feedback_sender->state_cond);
	chiaki_thread_join(&feedback_sender->thread, NULL);
	chiaki_cond_fini(&feedback_sender->state_cond);
	chiaki_mutex_fini(&feedback_sender->state_mutex);
	chiaki_feedback_history_buffer_fini(&feedback_sender->history_buf);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_sender_set_controller_state(ChiakiFeedbackSender *feedback_sender, ChiakiControllerState *state)
{
	ChiakiErrorCode err = chiaki_mutex_lock(&feedback_sender->state_mutex);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	if(chiaki_controller_state_equals(&feedback_sender->controller_state, state))
	{
		chiaki_mutex_unlock(&feedback_sender->state_mutex);
		return CHIAKI_ERR_SUCCESS;
	}

	feedback_sender->controller_state = *state;
	feedback_sender_record_history(feedback_sender, &feedback_sender->controller_state_history_prev, &feedback_sender->controller_state);
	feedback_sender->controller_state_history_prev = feedback_sender->controller_state;
	feedback_sender->controller_state_changed = true;

	chiaki_mutex_unlock(&feedback_sender->state_mutex);
	chiaki_cond_signal(&feedback_sender->state_cond);

	return CHIAKI_ERR_SUCCESS;
}

static bool controller_state_equals_for_feedback_state(ChiakiControllerState *a, ChiakiControllerState *b)
{
	if(!(a->left_x == b->left_x
		&& a->left_y == b->left_y
		&& a->right_x == b->right_x
		&& a->right_y == b->right_y))
		return false;
#define CHECKF(n) if(a->n < b->n - 0.0000001f || a->n > b->n + 0.0000001f) return false
	CHECKF(gyro_x);
	CHECKF(gyro_y);
	CHECKF(gyro_z);
	CHECKF(accel_x);
	CHECKF(accel_y);
	CHECKF(accel_z);
	CHECKF(orient_x);
	CHECKF(orient_y);
	CHECKF(orient_z);
	CHECKF(orient_w);
#undef CHECKF
	return true;
}

static void feedback_sender_send_state(ChiakiFeedbackSender *feedback_sender, const ChiakiControllerState *state)
{
	ChiakiFeedbackState feedback_state;
	feedback_state.left_x = state->left_x;
	feedback_state.left_y = state->left_y;
	feedback_state.right_x = state->right_x;
	feedback_state.right_y = state->right_y;
	feedback_state.gyro_x = state->gyro_x;
	feedback_state.gyro_y = state->gyro_y;
	feedback_state.gyro_z = state->gyro_z;
	feedback_state.accel_x = state->accel_x;
	feedback_state.accel_y = state->accel_y;
	feedback_state.accel_z = state->accel_z;

	feedback_state.orient_x = state->orient_x;
	feedback_state.orient_y = state->orient_y;
	feedback_state.orient_z = state->orient_z;
	feedback_state.orient_w = state->orient_w;

	ChiakiErrorCode err = chiaki_takion_send_feedback_state(feedback_sender->takion, feedback_sender->state_seq_num++, &feedback_state);
	if(err != CHIAKI_ERR_SUCCESS)
		CHIAKI_LOGE(feedback_sender->log, "FeedbackSender failed to send Feedback State");
}

static void feedback_sender_send_history_packet(ChiakiFeedbackSender *feedback_sender, const uint8_t *buf, size_t buf_size)
{
	//CHIAKI_LOGD(feedback_sender->log, "Feedback History:");
	//chiaki_log_hexdump(feedback_sender->log, CHIAKI_LOG_DEBUG, buf, buf_size);
	chiaki_takion_send_feedback_history(feedback_sender->takion, feedback_sender->history_seq_num++, (uint8_t *)buf, buf_size);
}

static void feedback_sender_flush_history_locked(ChiakiFeedbackSender *feedback_sender)
{
	if(!feedback_sender->history_dirty)
		return;

	size_t packet_index = (feedback_sender->history_packet_begin + feedback_sender->history_packet_len)
		% CHIAKI_FEEDBACK_HISTORY_PACKET_QUEUE_SIZE;
	size_t packet_size = CHIAKI_FEEDBACK_HISTORY_PACKET_BUF_SIZE;
	ChiakiErrorCode err = chiaki_feedback_history_buffer_format(
			&feedback_sender->history_buf,
			feedback_sender->history_packets[packet_index],
			&packet_size);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(feedback_sender->log, "Feedback Sender failed to format history buffer");
		return;
	}

	if(feedback_sender->history_packet_len < CHIAKI_FEEDBACK_HISTORY_PACKET_QUEUE_SIZE)
	{
		feedback_sender->history_packet_sizes[packet_index] = packet_size;
		feedback_sender->history_packet_len++;
	}
	else
	{
		feedback_sender->history_packet_sizes[feedback_sender->history_packet_begin] = packet_size;
		memcpy(
			feedback_sender->history_packets[feedback_sender->history_packet_begin],
			feedback_sender->history_packets[packet_index],
			packet_size);
		feedback_sender->history_packet_begin = (feedback_sender->history_packet_begin + 1)
			% CHIAKI_FEEDBACK_HISTORY_PACKET_QUEUE_SIZE;
		CHIAKI_LOGW(feedback_sender->log, "Feedback Sender history packet queue overflow");
	}

	if(feedback_sender->history_buf.len > FEEDBACK_HISTORY_RESEND_EVENT_COUNT)
		feedback_sender->history_buf.len = FEEDBACK_HISTORY_RESEND_EVENT_COUNT;
	feedback_sender->history_dirty = false;
}

static void feedback_sender_history_event_pushed(void *user)
{
	ChiakiFeedbackSender *feedback_sender = user;
	feedback_sender->history_dirty = true;
	feedback_sender_flush_history_locked(feedback_sender);
}

static void feedback_sender_record_history(ChiakiFeedbackSender *feedback_sender, const ChiakiControllerState *state_prev, const ChiakiControllerState *state_now)
{
	chiaki_feedback_history_buffer_record_state(&feedback_sender->history_buf, feedback_sender->log, state_prev, state_now,
			feedback_sender_history_event_pushed, feedback_sender);
}

static bool state_cond_check(void *user)
{
	ChiakiFeedbackSender *feedback_sender = user;
	return feedback_sender->should_stop
		|| feedback_sender->controller_state_changed;
}

static void *feedback_sender_thread_func(void *user)
{
	ChiakiFeedbackSender *feedback_sender = user;
	chiaki_thread_set_affinity(CHIAKI_THREAD_NAME_FEEDBACK);

	ChiakiErrorCode err = chiaki_mutex_lock(&feedback_sender->state_mutex);
	if(err != CHIAKI_ERR_SUCCESS)
		return NULL;

	uint64_t last_feedback_state_ms = chiaki_time_now_monotonic_ms();
	while(true)
	{
		if(feedback_sender->history_packet_len == 0)
		{
			uint64_t now_ms = chiaki_time_now_monotonic_ms();
			uint64_t next_timeout = FEEDBACK_STATE_TIMEOUT_MAX_MS;
			if(now_ms - last_feedback_state_ms < FEEDBACK_STATE_TIMEOUT_MAX_MS)
				next_timeout = FEEDBACK_STATE_TIMEOUT_MAX_MS - (now_ms - last_feedback_state_ms);

			err = chiaki_cond_timedwait_pred(&feedback_sender->state_cond, &feedback_sender->state_mutex, next_timeout, state_cond_check, feedback_sender);
			if(err != CHIAKI_ERR_SUCCESS && err != CHIAKI_ERR_TIMEOUT)
				break;
		}
		else
		{
			err = CHIAKI_ERR_SUCCESS;
		}

		if(feedback_sender->should_stop)
			break;

		uint64_t now_ms = chiaki_time_now_monotonic_ms();
		bool send_feedback_state = now_ms - last_feedback_state_ms >= FEEDBACK_STATE_TIMEOUT_MAX_MS;
		ChiakiControllerState state_now = feedback_sender->controller_state;
		bool send_feedback_history = false;
		uint8_t history_buf[CHIAKI_FEEDBACK_HISTORY_PACKET_BUF_SIZE];
		size_t history_buf_size = 0;

		if(feedback_sender->controller_state_changed)
		{
			// TODO: FEEDBACK_STATE_TIMEOUT_MIN_MS
			feedback_sender->controller_state_changed = false;
			send_feedback_state = true;

			// don't need to send feedback state if nothing relevant changed
			if(controller_state_equals_for_feedback_state(&state_now, &feedback_sender->controller_state_prev))
				send_feedback_state = false;
		} // else: timeout

		if(feedback_sender->history_packet_len > 0)
		{
			size_t packet_index = feedback_sender->history_packet_begin;
			history_buf_size = feedback_sender->history_packet_sizes[packet_index];
			memcpy(history_buf, feedback_sender->history_packets[packet_index], history_buf_size);
			feedback_sender->history_packet_begin = (feedback_sender->history_packet_begin + 1)
				% CHIAKI_FEEDBACK_HISTORY_PACKET_QUEUE_SIZE;
			feedback_sender->history_packet_len--;
			send_feedback_history = true;
		}
		chiaki_mutex_unlock(&feedback_sender->state_mutex);

		if(send_feedback_state)
			feedback_sender_send_state(feedback_sender, &state_now);

		if(send_feedback_history)
			feedback_sender_send_history_packet(feedback_sender, history_buf, history_buf_size);

		err = chiaki_mutex_lock(&feedback_sender->state_mutex);
		if(err != CHIAKI_ERR_SUCCESS)
			return NULL;
		if(send_feedback_state)
		{
			feedback_sender->controller_state_prev = state_now;
			last_feedback_state_ms = chiaki_time_now_monotonic_ms();
		}
	}

	chiaki_mutex_unlock(&feedback_sender->state_mutex);

	return NULL;
}
