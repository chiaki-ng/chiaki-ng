// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_CTRL_H
#define CHIAKI_CTRL_H

#include "common.h"
#include "thread.h"
#include "stoppipe.h"

#include <stdint.h>
#include <stdbool.h>

#if _WIN32
#include <winsock2.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ChiakiCantDisplayCb)(void *user, bool cant_display);

typedef struct chiaki_ctrl_message_queue_t ChiakiCtrlMessageQueue;

typedef struct chiaki_ctrl_display_sink_t
{
	void *user;
	ChiakiCantDisplayCb cantdisplay_cb;
} ChiakiCtrlDisplaySink;

typedef struct chiaki_ctrl_t
{
	struct chiaki_session_t *session;
	ChiakiThread thread;

	bool should_stop;
	bool login_pin_entered;
	uint8_t *login_pin;
	size_t login_pin_size;
	ChiakiCtrlMessageQueue *msg_queue;
	ChiakiStopPipe stop_pipe;
	ChiakiStopPipe notif_pipe;
	ChiakiMutex notif_mutex;

	bool login_pin_requested;
	bool cant_displaya;
	bool cant_displayb;

	chiaki_socket_t sock;

#ifdef __GNUC__
	__attribute__((aligned(__alignof__(uint32_t))))
#endif
	uint8_t recv_buf[512];
	uint8_t rudp_recv_buf[520];

	size_t recv_buf_size;
	uint64_t crypt_counter_local;
	uint64_t crypt_counter_remote;
	uint32_t keyboard_text_counter;
	// when the last heartbeat reply was sent, 0 until the console first asks for one
	uint64_t heartbeat_rep_last_ms;
} ChiakiCtrl;

CHIAKI_EXPORT ChiakiErrorCode chiaki_ctrl_init(ChiakiCtrl *ctrl, struct chiaki_session_t *session);
CHIAKI_EXPORT ChiakiErrorCode chiaki_ctrl_start(ChiakiCtrl *ctrl);
CHIAKI_EXPORT void chiaki_ctrl_stop(ChiakiCtrl *ctrl);
CHIAKI_EXPORT ChiakiErrorCode chiaki_ctrl_join(ChiakiCtrl *ctrl);
CHIAKI_EXPORT void chiaki_ctrl_fini(ChiakiCtrl *ctrl);
CHIAKI_EXPORT ChiakiErrorCode chiaki_ctrl_send_message(ChiakiCtrl *ctrl, uint16_t type, const uint8_t *payload, size_t payload_size);
CHIAKI_EXPORT ChiakiErrorCode ctrl_message_toggle_microphone(ChiakiCtrl *ctrl, bool muted);
CHIAKI_EXPORT ChiakiErrorCode ctrl_message_connect_microphone(ChiakiCtrl *ctrl);
CHIAKI_EXPORT void chiaki_ctrl_set_login_pin(ChiakiCtrl *ctrl, const uint8_t *pin, size_t pin_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_ctrl_goto_bed(ChiakiCtrl *ctrl);
CHIAKI_EXPORT ChiakiErrorCode chiaki_ctrl_keyboard_set_text(ChiakiCtrl *ctrl, const char* text);
CHIAKI_EXPORT ChiakiErrorCode chiaki_ctrl_keyboard_accept(ChiakiCtrl *ctrl);
CHIAKI_EXPORT ChiakiErrorCode chiaki_ctrl_keyboard_reject(ChiakiCtrl *ctrl);
CHIAKI_EXPORT ChiakiErrorCode ctrl_message_go_home(ChiakiCtrl *ctrl);
CHIAKI_EXPORT ChiakiErrorCode ctrl_message_set_fallback_session_id(ChiakiCtrl *ctrl);
CHIAKI_EXPORT void ctrl_enable_features(ChiakiCtrl *ctrl);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_CTRL_H
