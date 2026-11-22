// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/audioreceiver.h>
#include <chiaki/senkusha.h>
#include <chiaki/session.h>
#include <chiaki/http.h>
#include <chiaki/base64.h>
#include <chiaki/random.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>

#ifdef _WIN32
#include <winsock2.h>
#define strcasecmp _stricmp
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#endif

#include "utils.h"


#define SESSION_PORT					9295

#define SESSION_EXPECT_TIMEOUT_MS		5000

#define SESSION_EXPECT_CTRL_START_MS    10000

static void *session_thread_func(void *arg);
static void regist_cb(ChiakiRegistEvent *event, void *user);
static ChiakiErrorCode session_thread_request_session(ChiakiSession *session, ChiakiTarget *target_out);

const char *chiaki_rp_application_reason_string(uint32_t reason)
{
	switch(reason)
	{
		case CHIAKI_RP_APPLICATION_REASON_REGIST_FAILED:
			return "Regist failed, probably invalid PIN";
		case CHIAKI_RP_APPLICATION_REASON_INVALID_PSN_ID:
			return "Invalid PSN ID";
		case CHIAKI_RP_APPLICATION_REASON_IN_USE:
			return "Remote is already in use";
		case CHIAKI_RP_APPLICATION_REASON_CRASH:
			return "Remote Play on Console crashed";
		case CHIAKI_RP_APPLICATION_REASON_RP_VERSION:
			return "RP-Version mismatch";
		default:
			return "unknown";
	}
}

const char *chiaki_rp_version_string(ChiakiTarget version)
{
	switch(version)
	{
		case CHIAKI_TARGET_PS4_8:
			return "8.0";
		case CHIAKI_TARGET_PS4_9:
			return "9.0";
		case CHIAKI_TARGET_PS4_10:
			return "10.0";
		case CHIAKI_TARGET_PS5_1:
			return "1.0";
		default:
			return NULL;
	}
}

CHIAKI_EXPORT ChiakiTarget chiaki_rp_version_parse(const char *rp_version_str, bool is_ps5)
{
	if(is_ps5)
	{
		if(!strcmp(rp_version_str, "1.0"))
			return CHIAKI_TARGET_PS5_1;
		return CHIAKI_TARGET_PS5_UNKNOWN;
	}
	if(!strcmp(rp_version_str, "8.0"))
		return CHIAKI_TARGET_PS4_8;
	if(!strcmp(rp_version_str, "9.0"))
		return CHIAKI_TARGET_PS4_9;
	if(!strcmp(rp_version_str, "10.0"))
		return CHIAKI_TARGET_PS4_10;
	return CHIAKI_TARGET_PS4_UNKNOWN;
}

CHIAKI_EXPORT void chiaki_connect_video_profile_preset(ChiakiConnectVideoProfile *profile, ChiakiVideoResolutionPreset resolution, ChiakiVideoFPSPreset fps)
{
	profile->codec = CHIAKI_CODEC_H264;
	switch(resolution)
	{
		case CHIAKI_VIDEO_RESOLUTION_PRESET_360p:
			profile->width = 640;
			profile->height = 360;
			profile->bitrate = 2000;
			break;
		case CHIAKI_VIDEO_RESOLUTION_PRESET_540p:
			profile->width = 960;
			profile->height = 540;
			profile->bitrate = 6000;
			break;
		case CHIAKI_VIDEO_RESOLUTION_PRESET_720p:
			profile->width = 1280;
			profile->height = 720;
			profile->bitrate = 10000;
			break;
		case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p:
			profile->width = 1920;
			profile->height = 1080;
			profile->bitrate = 15000;
			break;
		default:
			profile->width = 0;
			profile->height = 0;
			profile->bitrate = 0;
			break;
	}

	switch(fps)
	{
		case CHIAKI_VIDEO_FPS_PRESET_30:
			profile->max_fps = 30;
			break;
		case CHIAKI_VIDEO_FPS_PRESET_60:
			profile->max_fps = 60;
			break;
		default:
			profile->max_fps = 0;
			break;
	}
}

CHIAKI_EXPORT const char *chiaki_quit_reason_string(ChiakiQuitReason reason)
{
	switch(reason)
	{
		case CHIAKI_QUIT_REASON_STOPPED:
			return "Stopped";
		case CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN:
			return "Unknown Session Request Error";
		case CHIAKI_QUIT_REASON_SESSION_REQUEST_CONNECTION_REFUSED:
			return "Connection Refused in Session Request";
		case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE:
			return "Remote Play on Console is already in use";
		case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_CRASH:
			return "Remote Play on Console has crashed";
		case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_VERSION_MISMATCH:
			return "RP-Version mismatch";
		case CHIAKI_QUIT_REASON_CTRL_UNKNOWN:
			return "Unknown Ctrl Error";
		case CHIAKI_QUIT_REASON_CTRL_CONNECTION_REFUSED:
			return "Connection Refused in Ctrl";
		case CHIAKI_QUIT_REASON_CTRL_CONNECT_FAILED:
			return "Ctrl failed to connect";
		case CHIAKI_QUIT_REASON_STREAM_CONNECTION_UNKNOWN:
			return "Unknown Error in Stream Connection";
		case CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_DISCONNECTED:
			return "Remote has disconnected from Stream Connection";
		case CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_SHUTDOWN:
			return "Remote has disconnected from Stream Connection the because Server shut down";
		case CHIAKI_QUIT_REASON_PSN_REGIST_FAILED:
			return "The Console Registration using PSN has failed";
		case CHIAKI_QUIT_REASON_NONE:
		default:
			return "Unknown";
	}
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_init(ChiakiSession *session, ChiakiConnectInfo *connect_info,
	ChiakiLog *log)
{
	memset(session, 0, sizeof(ChiakiSession));

	session->log = log;
	session->quit_reason = CHIAKI_QUIT_REASON_NONE;
	session->target = connect_info->ps5 ? CHIAKI_TARGET_PS5_1 : CHIAKI_TARGET_PS4_10;
	session->auto_regist = connect_info->auto_regist;
	session->holepunch_session = connect_info->holepunch_session;
	session->rudp = NULL;
	session->dontfrag = true;

	ChiakiErrorCode err = chiaki_cond_init(&session->state_cond);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error;
		
	err = chiaki_mutex_init(&session->state_mutex, false);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_state_cond;

	err = chiaki_stop_pipe_init(&session->stop_pipe);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_state_mutex;

	chiaki_mutex_lock(&session->state_mutex);
	session->should_stop = false;
	session->ctrl_session_id_received = false;
	session->ctrl_login_pin_requested = false;
	session->login_pin_entered = false;
	session->psn_regist_succeeded = false;
	session->stream_connection_switch_received = false;
	session->login_pin = NULL;
	session->login_pin_size = 0;
	chiaki_mutex_unlock(&session->state_mutex);

	err = chiaki_ctrl_init(&session->ctrl, session);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(session->log, "Ctrl init failed");
		goto error_stop_pipe;
	}

	err = chiaki_stream_connection_init(&session->stream_connection, session, connect_info->packet_loss_max);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(session->log, "StreamConnection init failed");
		goto error_ctrl;
	}

	if(session->holepunch_session)
	{
		memcpy(session->connect_info.psn_account_id, connect_info->psn_account_id, sizeof(connect_info->psn_account_id));
	}
	else
	{
		// make hostname use ipv4 for now
		struct addrinfo hints;
		memset(&hints, 0, sizeof(hints));
		hints.ai_socktype = SOCK_DGRAM;
		char *ipv6 = strchr(connect_info->host, ':');
		if(ipv6)
			hints.ai_family = AF_INET6;
		else
			hints.ai_family = AF_INET;
		int r = getaddrinfo(connect_info->host, NULL, &hints, &session->connect_info.host_addrinfos);
		if(r != 0)
		{
			chiaki_session_fini(session);
			return CHIAKI_ERR_PARSE_ADDR;
		}
		memcpy(session->connect_info.regist_key, connect_info->regist_key, sizeof(session->connect_info.regist_key));
		memcpy(session->connect_info.morning, connect_info->morning, sizeof(session->connect_info.morning));
	}

	chiaki_controller_state_set_idle(&session->controller_state);

	session->connect_info.ps5 = connect_info->ps5;
	const uint8_t did_prefix[] = { 0x00, 0x18, 0x00, 0x00, 0x00, 0x07, 0x00, 0x40, 0x00, 0x80 };
	const uint8_t did_suffix[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	memcpy(session->connect_info.did, did_prefix, sizeof(did_prefix));
	chiaki_random_bytes_crypt(session->connect_info.did + sizeof(did_prefix), sizeof(session->connect_info.did) - sizeof(did_prefix) - sizeof(did_suffix));
	memcpy(session->connect_info.did + sizeof(session->connect_info.did) - sizeof(did_suffix), did_suffix, sizeof(did_suffix));

	if(connect_info->audio_video_disabled & CHIAKI_VIDEO_DISABLED)
		chiaki_connect_video_profile_preset(&session->connect_info.video_profile, CHIAKI_VIDEO_RESOLUTION_PRESET_360p, 0);
	else
		session->connect_info.video_profile = connect_info->video_profile;
	session->connect_info.disable_audio_video = connect_info->audio_video_disabled;
	session->connect_info.video_profile_auto_downgrade = connect_info->video_profile_auto_downgrade;
	session->connect_info.enable_keyboard = connect_info->enable_keyboard;
	session->connect_info.enable_dualsense = connect_info->enable_dualsense;
	session->connect_info.enable_idr_on_fec_failure = connect_info->enable_idr_on_fec_failure;

	return CHIAKI_ERR_SUCCESS;

error_ctrl:
	chiaki_ctrl_fini(&session->ctrl);
error_stop_pipe:
	chiaki_stop_pipe_fini(&session->stop_pipe);
error_state_mutex:
	chiaki_mutex_fini(&session->state_mutex);
error_state_cond:
	chiaki_cond_fini(&session->state_cond);
error:
	if(session->holepunch_session)
		chiaki_holepunch_session_fini(session->holepunch_session);
	return err;
}

CHIAKI_EXPORT void chiaki_session_fini(ChiakiSession *session)
{
	if(!session)
		return;
    ChiakiErrorCode err = chiaki_mutex_lock(&session->state_mutex);
	assert(err == CHIAKI_ERR_SUCCESS);
	free(session->login_pin);
	free(session->quit_reason_str);
	chiaki_mutex_unlock(&session->state_mutex);
	chiaki_stream_connection_fini(&session->stream_connection);
	chiaki_ctrl_fini(&session->ctrl);
	if(session->rudp)
		chiaki_rudp_fini(session->rudp);
	if(session->holepunch_session)
		chiaki_holepunch_session_fini(session->holepunch_session);
	chiaki_stop_pipe_fini(&session->stop_pipe);
	chiaki_cond_fini(&session->state_cond);
	chiaki_mutex_fini(&session->state_mutex);
	freeaddrinfo(session->connect_info.host_addrinfos);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_start(ChiakiSession *session)
{
	ChiakiErrorCode err = chiaki_thread_create(&session->session_thread, session_thread_func, session);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	chiaki_thread_set_name(&session->session_thread, "Chiaki Session");
	return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_set_host(ChiakiSession *session, const char *host)
{
	if(!session || !host || !*host)
		return CHIAKI_ERR_INVALID_DATA;
	// PSN sessions get their address from the holepunch session, not from a host name
	if(session->holepunch_session)
		return CHIAKI_ERR_INVALID_DATA;

	// same resolution rules as chiaki_session_init()
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_family = strchr(host, ':') ? AF_INET6 : AF_INET;
	struct addrinfo *addrinfos = NULL;
	int r = getaddrinfo(host, NULL, &hints, &addrinfos);
	if(r != 0 || !addrinfos)
	{
		CHIAKI_LOGE(session->log, "Failed to resolve new session host %s", host);
		if(addrinfos)
			freeaddrinfo(addrinfos);
		return CHIAKI_ERR_PARSE_ADDR;
	}

	struct addrinfo *old_addrinfos = session->connect_info.host_addrinfos;
	session->connect_info.host_addrinfos = addrinfos;
	// pointed into the old list, gets selected again on the next session request
	session->connect_info.host_addrinfo_selected = NULL;
	if(old_addrinfos)
		freeaddrinfo(old_addrinfos);

	CHIAKI_LOGI(session->log, "Session host changed to %s", host);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_stop(ChiakiSession *session)
{
	ChiakiErrorCode err = chiaki_mutex_lock(&session->state_mutex);
	assert(err == CHIAKI_ERR_SUCCESS);

	session->should_stop = true;
	chiaki_stop_pipe_stop(&session->stop_pipe);
	chiaki_cond_signal(&session->state_cond);

	chiaki_stream_connection_stop(&session->stream_connection);

	chiaki_mutex_unlock(&session->state_mutex);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_join(ChiakiSession *session)
{
	return chiaki_thread_join(&session->session_thread, NULL);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_request_idr(ChiakiSession *session)
{
	return stream_connection_send_idr_request(&session->stream_connection);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_set_controller_state(ChiakiSession *session, ChiakiControllerState *state)
{
	ChiakiErrorCode err = chiaki_mutex_lock(&session->stream_connection.feedback_sender_mutex);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	session->controller_state = *state;
	if(session->stream_connection.feedback_sender_active)
		chiaki_feedback_sender_set_controller_state(&session->stream_connection.feedback_sender, &session->controller_state);
	chiaki_mutex_unlock(&session->stream_connection.feedback_sender_mutex);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_set_login_pin(ChiakiSession *session, const uint8_t *pin, size_t pin_size)
{
	uint8_t *buf = malloc(pin_size);
	if(!buf){
		return CHIAKI_ERR_MEMORY;
	}
	memcpy(buf, pin, pin_size);
	ChiakiErrorCode err = chiaki_mutex_lock(&session->state_mutex);
	assert(err == CHIAKI_ERR_SUCCESS);
	if(session->login_pin_entered)
		free(session->login_pin);
	session->login_pin_entered = true;
	session->login_pin = buf;
	session->login_pin_size = pin_size;
	chiaki_mutex_unlock(&session->state_mutex);
	chiaki_cond_signal(&session->state_cond);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_set_stream_connection_switch_received(ChiakiSession *session)
{
	ChiakiErrorCode err = chiaki_mutex_lock(&session->state_mutex);
	assert(err == CHIAKI_ERR_SUCCESS);
	session->stream_connection_switch_received = true;
	chiaki_mutex_unlock(&session->state_mutex);
	chiaki_cond_signal(&session->state_cond);
	return CHIAKI_ERR_SUCCESS;
}

void chiaki_session_send_event(ChiakiSession *session, ChiakiEvent *event)
{
	if(!session->event_cb)
		return;
	session->event_cb(event, session->event_cb_user);
}


static bool session_check_state_pred(void *user)
{
	ChiakiSession *session = user;
	return session->should_stop
		|| session->ctrl_failed;
}

static bool session_check_state_pred_ctrl_start(void *user)
{
	ChiakiSession *session = user;
	return session->should_stop
		   || session->ctrl_failed
		   || session->ctrl_session_id_received
		   || session->ctrl_login_pin_requested;
}

static bool session_check_state_pred_pin(void *user)
{
	ChiakiSession *session = user;
	return session->should_stop
		   || session->ctrl_failed
		   || session->login_pin_entered;
}

static bool session_check_state_pred_stream_connection_switch(void *user)
{
	ChiakiSession *session = user;
	return session->should_stop
		|| session->ctrl_failed
		|| session->stream_connection_switch_received;
}

static bool session_check_state_pred_regist(void *user)
{
	ChiakiSession *session = user;
	return session->should_stop
		|| session->ctrl_failed
		|| session->psn_regist_succeeded;
}

#define ENABLE_SENKUSHA

static void *session_thread_func(void *arg)
{
	ChiakiSession *session = (ChiakiSession *)arg;
	chiaki_thread_set_affinity(CHIAKI_THREAD_NAME_SESSION);

	chiaki_mutex_lock(&session->state_mutex);

#define QUIT(quit_label) do { \
	chiaki_mutex_unlock(&session->state_mutex); \
	goto quit_label; } while(0)

#define CHECK_STOP(quit_label) do { \
	if(session->should_stop) \
	{ \
		session->quit_reason = CHIAKI_QUIT_REASON_STOPPED; \
		QUIT(quit_label); \
	} } while(0)

	CHECK_STOP(quit);

	if(session->holepunch_session)
	{
		chiaki_socket_t *rudp_sock = chiaki_get_holepunch_sock(session->holepunch_session, CHIAKI_HOLEPUNCH_PORT_TYPE_CTRL);
		session->rudp = chiaki_rudp_init(rudp_sock, session->log);
		if(!session->rudp)
		{
			CHIAKI_LOGE(session->log, "Initializing rudp failed");
			CHECK_STOP(quit);
		}
	}
	// PSN Connection
	if(session->rudp)
	{
		ChiakiRegist regist;
		ChiakiRegistInfo info;
		ChiakiHolepunchRegistInfo hinfo = chiaki_get_regist_info(session->holepunch_session);
		info.holepunch_info = &hinfo;
		info.host = NULL;
		info.broadcast = false;
		info.psn_online_id = NULL;
		info.pin = 0;
		info.console_pin = 0;
		memcpy(info.psn_account_id, session->connect_info.psn_account_id, CHIAKI_PSN_ACCOUNT_ID_SIZE);
		info.rudp = session->rudp;
		info.target = session->connect_info.ps5 ? CHIAKI_TARGET_PS5_1 : CHIAKI_TARGET_PS4_10;
		chiaki_regist_start(&regist, session->log, &info, regist_cb, session);
		chiaki_cond_timedwait_pred(&session->state_cond, &session->state_mutex, 10000, session_check_state_pred_regist, session);
		chiaki_regist_stop(&regist);
		chiaki_regist_fini(&regist);
		CHECK_STOP(quit);
	}
	if(session->auto_regist)
	{
		CHIAKI_LOGI(session->log, "Console auto registered successfully");
		session->quit_reason = CHIAKI_QUIT_REASON_STOPPED;
		QUIT(quit);
	}
	CHIAKI_LOGI(session->log, "Starting session request for %s", session->connect_info.ps5 ? "PS5" : "PS4");

	ChiakiTarget server_target = CHIAKI_TARGET_PS4_UNKNOWN;
	ChiakiErrorCode err = session_thread_request_session(session, &server_target);

	if(err == CHIAKI_ERR_VERSION_MISMATCH && !chiaki_target_is_unknown(server_target))
	{
		CHIAKI_LOGI(session->log, "Attempting to re-request session with Server's RP-Version");
		session->target = server_target;
		err = session_thread_request_session(session, &server_target);
	}
	else if(err != CHIAKI_ERR_SUCCESS)
		QUIT(quit);

	if(err == CHIAKI_ERR_VERSION_MISMATCH && !chiaki_target_is_unknown(server_target))
	{
		CHIAKI_LOGI(session->log, "Attempting to re-request session even harder with Server's RP-Version!!!");
		session->target = server_target;
		err = session_thread_request_session(session, NULL);
	}
	else if(err != CHIAKI_ERR_SUCCESS)
		QUIT(quit);

	if(err != CHIAKI_ERR_SUCCESS)
		QUIT(quit);

	CHIAKI_LOGI(session->log, "Session request successful");

	chiaki_rpcrypt_init_auth(&session->rpcrypt, session->target, session->nonce, session->connect_info.morning);

	// PS4 doesn't always react right away, sleep a bit
	chiaki_cond_timedwait_pred(&session->state_cond, &session->state_mutex, 10, session_check_state_pred, session);

	CHIAKI_LOGI(session->log, "Starting ctrl");

	err = chiaki_ctrl_start(&session->ctrl);
	if(err != CHIAKI_ERR_SUCCESS)
		QUIT(quit);

	err = chiaki_cond_timedwait_pred(&session->state_cond, &session->state_mutex, SESSION_EXPECT_CTRL_START_MS, session_check_state_pred_ctrl_start, session);
	CHECK_STOP(quit_ctrl);

	if(session->ctrl_failed)
	{
		CHIAKI_LOGE(session->log, "Ctrl has failed while waiting for ctrl startup");
		goto ctrl_failed;
	}

	bool pin_incorrect = false;
	while(session->ctrl_login_pin_requested)
	{
		session->ctrl_login_pin_requested = false;
		if(pin_incorrect)
			CHIAKI_LOGI(session->log, "Login PIN was incorrect, requested again by Ctrl");
		else
			CHIAKI_LOGI(session->log, "Ctrl requested Login PIN");
		ChiakiEvent event = { 0 };
		event.type = CHIAKI_EVENT_LOGIN_PIN_REQUEST;
		event.login_pin_request.pin_incorrect = pin_incorrect;
		chiaki_session_send_event(session, &event);
		pin_incorrect = true;
		err = chiaki_cond_timedwait_pred(&session->state_cond, &session->state_mutex, UINT64_MAX, session_check_state_pred_pin, session);
		CHECK_STOP(quit_ctrl);
		if(session->ctrl_failed)
		{
			CHIAKI_LOGE(session->log, "Ctrl has failed while waiting for PIN entry");
			goto ctrl_failed;
		}

		assert(session->login_pin_entered && session->login_pin);
		CHIAKI_LOGI(session->log, "Session received entered Login PIN, forwarding to Ctrl");
		chiaki_ctrl_set_login_pin(&session->ctrl, session->login_pin, session->login_pin_size);
		session->login_pin_entered = false;
		free(session->login_pin);
		session->login_pin = NULL;
		session->login_pin_size = 0;
		// wait for session id or new login pin request
		err = chiaki_cond_timedwait_pred(&session->state_cond, &session->state_mutex, SESSION_EXPECT_CTRL_START_MS, session_check_state_pred_ctrl_start, session);
		CHECK_STOP(quit_ctrl);
	}

	chiaki_socket_t *data_sock = NULL;
	if(session->rudp)
	{
		ChiakiErrorCode err = holepunch_session_create_offer(session->holepunch_session);
		if (err != CHIAKI_ERR_SUCCESS)
		{
			CHIAKI_LOGE(session->log, "!! Failed to create offer msg for data connection");
			CHECK_STOP(quit_ctrl);
		}
		CHIAKI_LOGI(session->log, "Punching hole for data connection");
		ChiakiEvent event_start = { 0 };
		event_start.type = CHIAKI_EVENT_HOLEPUNCH;
		event_start.data_holepunch.finished = false;
		chiaki_session_send_event(session, &event_start);
		err = chiaki_holepunch_session_punch_hole(session->holepunch_session, CHIAKI_HOLEPUNCH_PORT_TYPE_DATA);
		if (err != CHIAKI_ERR_SUCCESS)
		{
			CHIAKI_LOGE(session->log, "!! Failed to punch hole for data connection.");
			QUIT(quit_ctrl);
		}
		CHIAKI_LOGI(session->log, ">> Punched hole for data connection!");
		data_sock = chiaki_get_holepunch_sock(session->holepunch_session, CHIAKI_HOLEPUNCH_PORT_TYPE_DATA);
		ChiakiEvent event_finish = { 0 };
		event_finish.type = CHIAKI_EVENT_HOLEPUNCH;
		event_finish.data_holepunch.finished = true;
		chiaki_session_send_event(session, &event_finish);
		err = chiaki_cond_timedwait_pred(&session->state_cond, &session->state_mutex, SESSION_EXPECT_TIMEOUT_MS, session_check_state_pred_ctrl_start, session);
		CHECK_STOP(quit_ctrl);
	}

	if(!session->ctrl_session_id_received)
	{
		CHIAKI_LOGE(session->log, "Ctrl did not receive session id");
		chiaki_mutex_unlock(&session->state_mutex);
		err = ctrl_message_set_fallback_session_id(&session->ctrl);
		chiaki_mutex_lock(&session->state_mutex);
		if(err != CHIAKI_ERR_SUCCESS)
			goto ctrl_failed;
		ctrl_enable_features(&session->ctrl);
	}

	if(!session->ctrl_session_id_received)
	{
ctrl_failed:
		CHIAKI_LOGE(session->log, "Ctrl has failed, shutting down");
		if(session->quit_reason == CHIAKI_QUIT_REASON_NONE)
			session->quit_reason = CHIAKI_QUIT_REASON_CTRL_UNKNOWN;
		QUIT(quit_ctrl);
	}

#ifdef ENABLE_SENKUSHA
	CHIAKI_LOGI(session->log, "Starting Senkusha");

	ChiakiSenkusha senkusha;
	err = chiaki_senkusha_init(&senkusha, session);
	if(err != CHIAKI_ERR_SUCCESS)
		QUIT(quit_ctrl);

	err = chiaki_senkusha_run(&senkusha, &session->mtu_in, &session->mtu_out, &session->rtt_us, data_sock);
	chiaki_senkusha_fini(&senkusha);
	CHECK_STOP(quit_ctrl);
	if(session->ctrl_failed)
	{
		CHIAKI_LOGE(session->log, "Ctrl has failed since session started, exiting");
		QUIT(quit_ctrl);
	}

	if(err == CHIAKI_ERR_SUCCESS)
		CHIAKI_LOGI(session->log, "Senkusha completed successfully");
	else if(err == CHIAKI_ERR_CANCELED)
		QUIT(quit_ctrl);
	else
	{
		CHIAKI_LOGE(session->log, "Senkusha failed, but we still try to connect with fallback values");
		session->mtu_in = 1454;
		session->mtu_out = 1454;
		session->rtt_us = 1000;
		session->dontfrag = false;
	}
#endif
	if(session->rudp)
	{
		ChiakiErrorCode err;
		err = chiaki_rudp_send_switch_to_stream_connection_message(session->rudp);
		if(err != CHIAKI_ERR_SUCCESS)
		{
			CHIAKI_LOGE(session->log, "Failed to send switch to stream connection message");
			QUIT(quit_ctrl);
		}
		err = chiaki_cond_timedwait_pred(&session->state_cond, &session->state_mutex, SESSION_EXPECT_TIMEOUT_MS, session_check_state_pred_stream_connection_switch, session);
		if(!session->stream_connection_switch_received)
		{
			CHIAKI_LOGE(session->log, "Failed to receive switch to stream connection ack!");
			QUIT(quit_ctrl);
		}
		CHECK_STOP(quit_ctrl);
		CHIAKI_LOGI(session->log, "Received Switch to Stream Connection Ack... Switching to Stream Connection now");
	}

	err = chiaki_random_bytes_crypt(session->handshake_key, sizeof(session->handshake_key));
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(session->log, "Session failed to generate handshake key");
		QUIT(quit_ctrl);
	}

	err = chiaki_ecdh_init(&session->ecdh);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(session->log, "Session failed to initialize ECDH");
		QUIT(quit_ctrl);
	}

	chiaki_mutex_unlock(&session->state_mutex);
	err = chiaki_stream_connection_run(&session->stream_connection, data_sock);
	chiaki_mutex_lock(&session->state_mutex);
	if(err == CHIAKI_ERR_DISCONNECTED)
	{
		CHIAKI_LOGE(session->log, "Remote disconnected from StreamConnection");
		if(!strcmp(session->stream_connection.remote_disconnect_reason, "Server shutting down"))
			session->quit_reason = CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_SHUTDOWN;
		else
			session->quit_reason = CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_DISCONNECTED;
		session->quit_reason_str = strdup(session->stream_connection.remote_disconnect_reason);
	}
	else if(err != CHIAKI_ERR_SUCCESS && err != CHIAKI_ERR_CANCELED)
	{
		CHIAKI_LOGE(session->log, "StreamConnection run failed");
		session->quit_reason = CHIAKI_QUIT_REASON_STREAM_CONNECTION_UNKNOWN;
	}
	else
	{
		CHIAKI_LOGI(session->log, "StreamConnection completed successfully");
		session->quit_reason = CHIAKI_QUIT_REASON_STOPPED;
	}

	chiaki_mutex_unlock(&session->state_mutex);
	chiaki_ecdh_fini(&session->ecdh);

quit_ctrl:
	chiaki_ctrl_stop(&session->ctrl);
	chiaki_ctrl_join(&session->ctrl);
	CHIAKI_LOGI(session->log, "Ctrl stopped");

	ChiakiEvent quit_event;
quit:

	CHIAKI_LOGI(session->log, "Session has quit");
	chiaki_mutex_lock(&session->state_mutex);
	quit_event.type = CHIAKI_EVENT_QUIT;
	quit_event.quit.reason = session->quit_reason;
	quit_event.quit.reason_str = session->quit_reason_str;
	chiaki_mutex_unlock(&session->state_mutex);
	chiaki_session_send_event(session, &quit_event);
	return NULL;

#undef CHECK_STOP
#undef QUIT
}

typedef struct session_response_t
{
	uint32_t error_code;
	const char *nonce;
	const char *rp_version;
	bool success;
} SessionResponse;

static void parse_session_response(SessionResponse *response, ChiakiHttpResponse *http_response)
{
	memset(response, 0, sizeof(SessionResponse));

	for(ChiakiHttpHeader *header=http_response->headers; header; header=header->next)
	{
		if(strcmp(header->key, "RP-Nonce") == 0)
			response->nonce = header->value;
		else if(strcasecmp(header->key, "RP-Version") == 0)
			response->rp_version = header->value;
		else if(strcmp(header->key, "RP-Application-Reason") == 0)
			response->error_code = (uint32_t)strtoul(header->value, NULL, 0x10);
	}

	if(http_response->code == 200)
		response->success = response->nonce != NULL;
	else
		response->success = false;
}


/**
 * @param target_out if NULL, version mismatch means to fail the entire session, otherwise report the target here
 */
static ChiakiErrorCode session_thread_request_session(ChiakiSession *session, ChiakiTarget *target_out)
{
	chiaki_socket_t session_sock = CHIAKI_INVALID_SOCKET;
	uint16_t remote_counter = 0;
	if(session->rudp)
	{
		CHIAKI_LOGI(session->log, "SESSION START THREAD - Starting RUDP session");
		RudpMessage message;
		ChiakiErrorCode err = chiaki_rudp_send_recv(session->rudp, &message, NULL, 0, 0, INIT_REQUEST, INIT_RESPONSE, 8, 3);
		if(err != CHIAKI_ERR_SUCCESS)
		{
			CHIAKI_LOGE(session->log, "SESSION START THREAD - Failed to init rudp");
			return err;
		}
		size_t init_response_size = message.data_size - 8;
		uint8_t init_response[init_response_size];
		memcpy(init_response, message.data + 8, init_response_size);
		chiaki_rudp_message_pointers_free(&message);
		err = chiaki_rudp_send_recv(session->rudp, &message, init_response, init_response_size, 0, COOKIE_REQUEST, COOKIE_RESPONSE, 2, 3);
		if(err != CHIAKI_ERR_SUCCESS)
		{
			CHIAKI_LOGE(session->log, "SESSION START THREAD - Failed to pass rudp cookie");
			return err;
		}
		remote_counter = message.remote_counter;
		chiaki_rudp_message_pointers_free(&message);
	}
	else
	{
		for(struct addrinfo *ai=session->connect_info.host_addrinfos; ai; ai=ai->ai_next)
		{
			//if(ai->ai_protocol != IPPROTO_TCP)
			//	continue;

			struct sockaddr *sa = malloc(ai->ai_addrlen);
			if(!sa)
				continue;
			memcpy(sa, ai->ai_addr, ai->ai_addrlen);

			if(sa->sa_family != AF_INET && sa->sa_family != AF_INET6)
			{
				free(sa);
				continue;
			}

			set_port(sa, htons(SESSION_PORT));

			// TODO: this can block, make cancelable somehow
			int r = getnameinfo(sa, (socklen_t)ai->ai_addrlen, session->connect_info.hostname, sizeof(session->connect_info.hostname), NULL, 0, NI_NUMERICHOST);
			if(r != 0)
			{
				CHIAKI_LOGE(session->log, "getnameinfo failed with %s, filling the hostname with fallback", gai_strerror(r));
				memcpy(session->connect_info.hostname, "unknown", 8);
			}

			CHIAKI_LOGI(session->log, "Trying to request session from %s:%d", session->connect_info.hostname, SESSION_PORT);

			session_sock = socket(ai->ai_family, SOCK_STREAM, 0);
			if(CHIAKI_SOCKET_IS_INVALID(session_sock))
			{
#ifdef _WIN32
				CHIAKI_LOGE(session->log, "Failed to create socket to request session");
#else
				CHIAKI_LOGE(session->log, "Failed to create socket to request session: %s", strerror(errno));
#endif
				free(sa);
				continue;
			}

			ChiakiErrorCode err = chiaki_socket_set_nonblock(session_sock, true);
			if(err != CHIAKI_ERR_SUCCESS)
			{
				CHIAKI_LOGE(session->log, "Failed to set session socket to non-blocking: %s", chiaki_error_string(err));
				CHIAKI_SOCKET_CLOSE(session_sock);
				session_sock = CHIAKI_INVALID_SOCKET;
				free(sa);
				continue;
			}

			chiaki_mutex_unlock(&session->state_mutex);
			err = chiaki_stop_pipe_connect(&session->stop_pipe, session_sock, sa, ai->ai_addrlen, 5000);
			chiaki_mutex_lock(&session->state_mutex);
			if(err == CHIAKI_ERR_CANCELED)
			{
				CHIAKI_LOGI(session->log, "Session stopped while connecting for session request");
				session->quit_reason = CHIAKI_QUIT_REASON_STOPPED;
				if(!CHIAKI_SOCKET_IS_INVALID(session_sock))
				{
					CHIAKI_SOCKET_CLOSE(session_sock);
					session_sock = CHIAKI_INVALID_SOCKET;
				}
				free(sa);
				break;
			}
			else if(err != CHIAKI_ERR_SUCCESS)
			{
				CHIAKI_LOGE(session->log, "Session request connect failed: %s", chiaki_error_string(err));
				if(err == CHIAKI_ERR_CONNECTION_REFUSED)
					session->quit_reason = CHIAKI_QUIT_REASON_SESSION_REQUEST_CONNECTION_REFUSED;
				else
					session->quit_reason = CHIAKI_QUIT_REASON_NONE;
				if(!CHIAKI_SOCKET_IS_INVALID(session_sock))
				{
					CHIAKI_SOCKET_CLOSE(session_sock);
					session_sock = CHIAKI_INVALID_SOCKET;
				}
				free(sa);
				continue;
			}

			free(sa);

			session->connect_info.host_addrinfo_selected = ai;
			break;
		}
	
		if(CHIAKI_SOCKET_IS_INVALID(session_sock))
		{
			CHIAKI_LOGE(session->log, "Session request connect failed eventually.");
			if(session->quit_reason == CHIAKI_QUIT_REASON_NONE)
				session->quit_reason = CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN;
			return CHIAKI_ERR_NETWORK;
		}
		else
			CHIAKI_LOGI(session->log, "Connected to %s:%d", session->connect_info.hostname, SESSION_PORT);
	}

	static const char session_request_fmt[] =
			"GET %s HTTP/1.1\r\n"
			"Host: %s:%d\r\n"
			"User-Agent: remoteplay Windows\r\n"
			"Connection: close\r\n"
			"Content-Length: 0\r\n"
			"RP-Registkey: %s\r\n"
			"Rp-Version: %s\r\n"
			"\r\n";
	const char *path;
	if(session->target == CHIAKI_TARGET_PS4_8 || session->target == CHIAKI_TARGET_PS4_9)
		path = "/sce/rp/session";
	else if(chiaki_target_is_ps5(session->target))
		path = "/sie/ps5/rp/sess/init";
	else
		path = "/sie/ps4/rp/sess/init";

	size_t regist_key_len = sizeof(session->connect_info.regist_key);
	for(size_t i=0; i<regist_key_len; i++)
	{
		if(!session->connect_info.regist_key[i])
		{
			regist_key_len = i;
			break;
		}
	}
	char regist_key_hex[sizeof(session->connect_info.regist_key) * 2 + 1];
	ChiakiErrorCode err = format_hex(regist_key_hex, sizeof(regist_key_hex), (uint8_t *)session->connect_info.regist_key, regist_key_len);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		if(!CHIAKI_SOCKET_IS_INVALID(session_sock))
		{
			CHIAKI_SOCKET_CLOSE(session_sock);
			session_sock = CHIAKI_INVALID_SOCKET;
		}
		session->quit_reason = CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN;
		return CHIAKI_ERR_UNKNOWN;
	}

	const char *rp_version_str = chiaki_rp_version_string(session->target);
	if(!rp_version_str)
	{
		CHIAKI_LOGE(session->log, "Failed to get version for target, probably invalid target value");
		session->quit_reason = CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN;
		return CHIAKI_ERR_INVALID_DATA;
	}

	char send_buf[512];
	int port = SESSION_PORT;
	if(session->holepunch_session)
	{
		chiaki_get_ps_selected_addr(session->holepunch_session, session->connect_info.hostname);
		port = chiaki_get_ps_ctrl_port(session->holepunch_session);
	}
	int request_len = snprintf(send_buf, sizeof(send_buf), session_request_fmt,
			path, session->connect_info.hostname, port, regist_key_hex, rp_version_str);
	if(request_len < 0 || request_len >= sizeof(send_buf))
	{
		if(!CHIAKI_SOCKET_IS_INVALID(session_sock))
		{
			CHIAKI_SOCKET_CLOSE(session_sock);
			session_sock = CHIAKI_INVALID_SOCKET;
		}
		session->quit_reason = CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN;
		return CHIAKI_ERR_UNKNOWN;
	}

	CHIAKI_LOGI(session->log, "Sending session request");
	chiaki_log_hexdump(session->log, CHIAKI_LOG_VERBOSE, (uint8_t *)send_buf, request_len);
	if(!session->rudp)
	{
		chiaki_mutex_unlock(&session->state_mutex);
		err = chiaki_send_fully(&session->stop_pipe, session_sock, (const uint8_t *)send_buf, (size_t)request_len, SESSION_EXPECT_TIMEOUT_MS);
		ChiakiErrorCode mutex_err = chiaki_mutex_lock(&session->state_mutex);
		assert(mutex_err == CHIAKI_ERR_SUCCESS);
		if(err != CHIAKI_ERR_SUCCESS)
		{
			if(err == CHIAKI_ERR_CANCELED)
				CHIAKI_LOGI(session->log, "Session stopped while sending session request");
			else
				CHIAKI_LOGE(session->log, "Failed to send session request");
			if(!CHIAKI_SOCKET_IS_INVALID(session_sock))
			{
				CHIAKI_SOCKET_CLOSE(session_sock);
				session_sock = CHIAKI_INVALID_SOCKET;
			}
			session->quit_reason = err == CHIAKI_ERR_CANCELED
				? CHIAKI_QUIT_REASON_STOPPED
				: CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN;
			return err;
		}
	}
	char buf[512];
	size_t header_size;
	size_t received_size;
	chiaki_mutex_unlock(&session->state_mutex);
	if(session->rudp)
		err = chiaki_send_recv_http_header_psn(session->rudp, session->log, &remote_counter, send_buf, request_len, buf, sizeof(buf), &header_size, &received_size);
	else
		err = chiaki_recv_http_header(session_sock, buf, sizeof(buf), &header_size, &received_size, &session->stop_pipe, SESSION_EXPECT_TIMEOUT_MS);
	ChiakiErrorCode mutex_err = chiaki_mutex_lock(&session->state_mutex);
	assert(mutex_err == CHIAKI_ERR_SUCCESS);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		if(err == CHIAKI_ERR_CANCELED)
		{
			session->quit_reason = CHIAKI_QUIT_REASON_STOPPED;
		}
		else
		{
			CHIAKI_LOGE(session->log, "Failed to receive session request response");
			session->quit_reason = CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN;
		}
		if(!CHIAKI_SOCKET_IS_INVALID(session_sock))
		{
			CHIAKI_SOCKET_CLOSE(session_sock);
			session_sock = CHIAKI_INVALID_SOCKET;
		}
		return CHIAKI_ERR_NETWORK;
	}
	if(session->rudp)
	{
		RudpMessage message;
		err = chiaki_rudp_send_recv(session->rudp, &message, NULL, 0, remote_counter, ACK, FINISH, 0, 3);
		if(err != CHIAKI_ERR_SUCCESS)
			CHIAKI_LOGW(session->log, "SESSION START THREAD - Failed to finish rudp, continuing...");
		else
			chiaki_rudp_message_pointers_free(&message);
	}

	ChiakiHttpResponse http_response;
	CHIAKI_LOGV(session->log, "Session Response Header:");
	chiaki_log_hexdump(session->log, CHIAKI_LOG_VERBOSE, (const uint8_t *)buf, header_size);
	err = chiaki_http_response_parse(&http_response, buf, header_size);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(session->log, "Failed to parse session request response");
		if(!CHIAKI_SOCKET_IS_INVALID(session_sock))
		{
			CHIAKI_SOCKET_CLOSE(session_sock);
			session_sock = CHIAKI_INVALID_SOCKET;
		}
		session_sock = CHIAKI_INVALID_SOCKET;
		session->quit_reason = CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN;
		return CHIAKI_ERR_NETWORK;
	}

	SessionResponse response;
	parse_session_response(&response, &http_response);

	ChiakiErrorCode r = CHIAKI_ERR_UNKNOWN;
	if(response.success)
	{
		size_t nonce_len = CHIAKI_RPCRYPT_KEY_SIZE;
		err = chiaki_base64_decode(response.nonce, strlen(response.nonce), session->nonce, &nonce_len);
		if(err != CHIAKI_ERR_SUCCESS || nonce_len != CHIAKI_RPCRYPT_KEY_SIZE)
		{
			CHIAKI_LOGE(session->log, "Nonce invalid");
			response.success = false;
			session->quit_reason = CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN;
		}
		else
			r = CHIAKI_ERR_SUCCESS;
	}
	else if((response.error_code == CHIAKI_RP_APPLICATION_REASON_RP_VERSION
				|| response.error_code == CHIAKI_RP_APPLICATION_REASON_UNKNOWN)
			&& target_out && response.rp_version && strcmp(rp_version_str, response.rp_version))
	{
		CHIAKI_LOGI(session->log, "Reported RP-Version mismatch. ours = %s, server = %s",
				rp_version_str ? rp_version_str : "", response.rp_version);
		*target_out = chiaki_rp_version_parse(response.rp_version, session->connect_info.ps5);
		if(!chiaki_target_is_unknown(*target_out))
			CHIAKI_LOGI(session->log, "Detected Server RP-Version %s", chiaki_rp_version_string(*target_out));
		else if(!strcmp(response.rp_version, "5.0"))
		{
			CHIAKI_LOGI(session->log, "Reported Server RP-Version is 5.0. This is probably nonsense, let's try with 9.0");
			*target_out = CHIAKI_TARGET_PS4_9;
		}
		else
		{
			CHIAKI_LOGE(session->log, "Server RP-Version is unknown");
			session->quit_reason = CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_VERSION_MISMATCH;
		}
		r = CHIAKI_ERR_VERSION_MISMATCH;
	}
	else
	{
		CHIAKI_LOGE(session->log, "Reported Application Reason: %#x (%s)", (unsigned int)response.error_code, chiaki_rp_application_reason_string(response.error_code));
		switch(response.error_code)
		{
			case CHIAKI_RP_APPLICATION_REASON_IN_USE:
				session->quit_reason = CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE;
				break;
			case CHIAKI_RP_APPLICATION_REASON_CRASH:
				session->quit_reason = CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_CRASH;
				break;
			case CHIAKI_RP_APPLICATION_REASON_RP_VERSION:
				session->quit_reason = CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_VERSION_MISMATCH;
				r = CHIAKI_ERR_VERSION_MISMATCH;
				break;
			default:
				session->quit_reason = CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN;
				break;
		}
	}

	chiaki_http_response_fini(&http_response);
	if(!session->rudp)
	{
		if(!CHIAKI_SOCKET_IS_INVALID(session_sock))
		{
			CHIAKI_SOCKET_CLOSE(session_sock);
			session_sock = CHIAKI_INVALID_SOCKET;
		}
	}
	return r;
}

static void regist_cb(ChiakiRegistEvent *event, void *user)
{
	ChiakiSession *session = user;
	switch(event->type)
	{
		case CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS:
			CHIAKI_LOGI(session->log, "%s successfully registered for Remote Play", event->registered_host->server_nickname);
			if(session->auto_regist)
			{
				ChiakiEvent event_auto_regist = { 0 };
				event_auto_regist.type = CHIAKI_EVENT_REGIST;
				memcpy(&event_auto_regist.host, event->registered_host, sizeof(ChiakiRegisteredHost));
				chiaki_session_send_event(session, &event_auto_regist);
			}
			memcpy(session->connect_info.morning, event->registered_host->rp_key, sizeof(session->connect_info.morning));
			memcpy(session->connect_info.regist_key, event->registered_host->rp_regist_key, sizeof(session->connect_info.regist_key));
			if(!session->connect_info.ps5 && !session->auto_regist)
			{
				ChiakiEvent event_start = { 0 };
				event_start.type = CHIAKI_EVENT_NICKNAME_RECEIVED;
				memcpy(event_start.server_nickname, event->registered_host->server_nickname, sizeof(event->registered_host->server_nickname));
				chiaki_session_send_event(session, &event_start);
			}
			chiaki_mutex_lock(&session->state_mutex); 
			session->psn_regist_succeeded = true;
			chiaki_mutex_unlock(&session->state_mutex);
			chiaki_cond_signal(&session->state_cond);
			break;
		case CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED:
			CHIAKI_LOGI(session->log, "PSN regist was canceled, exiting...");
			chiaki_mutex_lock(&session->state_mutex);
			session->quit_reason = CHIAKI_QUIT_REASON_PSN_REGIST_FAILED;
			session->should_stop = true;
			chiaki_mutex_unlock(&session->state_mutex);
			chiaki_cond_signal(&session->state_cond);
			break;
		case CHIAKI_REGIST_EVENT_TYPE_FINISHED_FAILED:
			CHIAKI_LOGI(session->log, "PSN regist failed, exiting...");
			chiaki_mutex_lock(&session->state_mutex);
			session->quit_reason = CHIAKI_QUIT_REASON_PSN_REGIST_FAILED;
			session->should_stop = true;
			chiaki_mutex_unlock(&session->state_mutex);
			chiaki_cond_signal(&session->state_cond);
		default:
			break;
	}
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_goto_bed(ChiakiSession *session)
{
	return chiaki_ctrl_goto_bed(&session->ctrl);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_toggle_microphone(ChiakiSession *session, bool muted)
{
	ChiakiErrorCode err;
	err = ctrl_message_toggle_microphone(&session->ctrl, muted);
	return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_connect_microphone(ChiakiSession *session)
{
	ChiakiErrorCode err;
	err = ctrl_message_connect_microphone(&session->ctrl);
	return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_keyboard_set_text(ChiakiSession *session, const char *text)
{
	return chiaki_ctrl_keyboard_set_text(&session->ctrl, text);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_keyboard_reject(ChiakiSession *session)
{
	return chiaki_ctrl_keyboard_reject(&session->ctrl);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_keyboard_accept(ChiakiSession *session)
{
	return chiaki_ctrl_keyboard_accept(&session->ctrl);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_go_home(ChiakiSession *session)
{
	ChiakiErrorCode err;
	err = ctrl_message_go_home(&session->ctrl);
	return err;
}
