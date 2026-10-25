/*
 * SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
 *
 * This file is part of Chiaki - A Free and Open Source PS4 Remote Play Client
 *
 * UDP Hole Punching Implementation
 * --------------------------------
 *
 * "Remote Play over Internet" uses a custom UDP-based protocol for communication between the
 * console and the client (see `rudp.h` for details on that). The protocol is designed to work
 * even if both the console and the client are behind NATs, by using UDP hole punching via
 * an intermediate server. This file implements the hole punching logic using PSN APIs.
 *
 * The hole punching process itself i
 */
 
// TODO: Make portable for Switch

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <assert.h>
#include <inttypes.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <iphlpapi.h>
#elif defined(__SWITCH__)
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <poll.h>
#else
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h>
#endif

#include <curl/curl.h>

#include "../aia.h"
#if !defined(__SWITCH__) && !defined(__ANDROID__)
#include <event2/event.h>
#endif
#include <json-c/json_object.h>
#include <json-c/json_tokener.h>
#include <json-c/json_pointer.h>
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>

#include <chiaki/remote/holepunch.h>
#include <chiaki/stoppipe.h>
#include <chiaki/thread.h>
#include <chiaki/base64.h>
#include <chiaki/random.h>
#include <chiaki/sock.h>
#include <chiaki/time.h>

#include "../utils.h"
#include "stun.h"

static char *holepunch_ca_info = NULL;

CHIAKI_EXPORT void chiaki_holepunch_set_ca_info(const char *path)
{
    free(holepunch_ca_info);
    holepunch_ca_info = path ? strdup(path) : NULL;
    chiaki_aia_set_ca_info(path);
}

/**
 * curl_easy_init() plus the CA bundle set with chiaki_holepunch_set_ca_info().
 * Needed where libcurl has no usable default trust store, e.g. on Android.
 */
static CURL *holepunch_curl_easy_init(void)
{
    CURL *curl = curl_easy_init();
    if(curl && holepunch_ca_info)
        curl_easy_setopt(curl, CURLOPT_CAINFO, holepunch_ca_info);
    return curl;
}

#define UUIDV4_STR_LEN 37
#define SECOND_US 1000000L
#define MILLISECONDS_US 1000L
#define WEBSOCKET_PING_INTERVAL_SEC 5
// Maximum WebSocket frame size currently supported by libcurl
#define WEBSOCKET_MAX_FRAME_SIZE 64 * 1024
#define SESSION_CREATION_TIMEOUT_SEC 30
#define SESSION_START_TIMEOUT_SEC 30
#define SESSION_DELETION_TIMEOUT_SEC 3
#define SELECT_CANDIDATE_TIMEOUT_SEC 0.5F
#define SELECT_CANDIDATE_TRIES 20
#define SELECT_CANDIDATE_CONNECTION_SEC 5
#define RANDOM_ALLOCATION_GUESSES_NUMBER 75
#define RANDOM_ALLOCATION_SOCKS_NUMBER 250
#define CHECK_CANDIDATES_REQUEST_NUMBER 1
#define WAIT_RESPONSE_TIMEOUT_SEC 1
#define MSG_TYPE_REQ 0x06000000
#define MSG_TYPE_RESP 0x07000000
#define EXTRA_CANDIDATE_ADDRESSES 3
#define ENABLE_IPV6 false

static const char oauth_header_fmt[] = "Authorization: Bearer %s";
static const char session_id_header_fmt[] = "X-PSN-SESSION-MANAGER-SESSION-IDS: %s";

// Endpoints we're using
static const char device_list_url_fmt[] = "https://web.np.playstation.com/api/cloudAssistedNavigation/v2/users/me/clients?platform=%s&includeFields=device&limit=10&offset=0";
static const char ws_fqdn_api_url[] = "https://mobile-pushcl.np.communication.playstation.net/np/serveraddr?version=2.1&fields=keepAliveStatus&keepAliveStatusType=3";
static const char session_create_url[] = "https://web.np.playstation.com/api/sessionManager/v1/remotePlaySessions";
static const char session_view_url[] = "https://web.np.playstation.com/api/sessionManager/v1/remotePlaySessions?view=v1.0";
static const char user_profile_url[] = "https://asm.np.community.playstation.net/asm/v1/apps/me/baseUrls/userProfile";
static const char wakeup_url_fmt[] = "%s/v1/users/%s/remoteConsole/wakeUp?platform=PS4";
static const char session_command_url[] = "https://web.np.playstation.com/api/cloudAssistedNavigation/v2/users/me/commands";
static const char session_message_url_fmt[] = "https://web.np.playstation.com/api/sessionManager/v1/remotePlaySessions/%s/sessionMessage";
static const char delete_messsage_url_fmt[] = "https://web.np.playstation.com/api/sessionManager/v1/remotePlaySessions/%s/members/me";

// JSON payloads for requests.
// Implemented as string templates due to the broken JSON used by the official app, which we're
// trying to emulate.
static const char session_create_json_fmt[] =
    "{\"remotePlaySessions\":["
        "{\"members\":["
            "{\"accountId\":\"me\","
             "\"deviceUniqueId\":\"me\","
             "\"platform\":\"me\","
             "\"pushContexts\":["
                "{\"pushContextId\":\"%s\"}]}]}]}";  // 0: push context ID
static const char session_start_envelope_fmt[] =
    "{\"commandDetail\":"
        "{\"commandType\":\"remotePlay\","
         "\"duid\":\"%s\","                            // 0: device identifier, lowercase hex string
         "\"messageDestination\":\"SQS\","
         "\"parameters\":{\"initialParams\":\"%s\"},"  // 1: string containing escaped JSON data
         "\"platform\":\"%s\"}}";                      // 2: PS4/PS5
static const char session_wakeup_envelope_fmt[] =
    "{\"data\":"
        "{\"clientType\":\"Windows\","
         "\"data1\":\"%s\","                            // 0: 16 byte data1, base64 encoded
         "\"data2\":\"%s\","                            // 1: 16 byte data2, base64 encoded
         "\"roomId\": 0,"
         "\"protocolVer\":\"10.0\","
         "\"sessionId\":\"%s\"},"                        // 2: session identifier (lowercase UUIDv4)
         "\"dataTypeSuffix\":\"remotePlay\"}";
static const char session_message_envelope_fmt[] =
    "{\"channel\":\"remote_play:1\","
     "\"payload\":\"ver=1.0, type=text, body=%s\","  // 0: message body as JSON string
     "\"to\":["
       "{\"accountId\":\"%" PRId64"\","                     // 1: PSN account ID
        "\"deviceUniqueId\":\"%s\","                 // 2: device UID, lowercase hex string
        "\"platform\":\"%s\"}]}";                    // 3: PS4/PS5

// NOTE: These payloads are JSON-escaped, since they're going to be embedded in a JSON string
static const char session_start_payload_fmt[] =
    "{\\\"accountId\\\":%" PRId64","             // 0: PSN account ID, integer
     "\\\"roomId\\\":0,"
     "\\\"sessionId\\\":\\\"%s\\\","      // 1: session identifier (lowercase UUIDv4)
     "\\\"clientType\\\":\\\"Windows\\\","
     "\\\"data1\\\":\\\"%s\\\","          // 2: 16 byte data1, base64 encoded
     "\\\"data2\\\":\\\"%s\\\"}";         // 3: 16 byte data2, base64 encoded
static const char session_message_fmt[] =
    "{\\\"action\\\":\\\"%s\\\"," // 0: OFFER/RESULT/ACCEPT
     "\\\"reqId\\\":%d,"          // 1: request ID, integer
     "\\\"error\\\":%d,"          // 2: error id, integer
     "\\\"connRequest\\\":%s}";   // 3: connRequest JSON object
static const char session_connrequest_fmt[] =
    "{\\\"sid\\\":%d,"                         // 0: sid
     "\\\"peerSid\\\":%d,"                     // 1: peer sid
     "\\\"skey\\\":\\\"%s\\\","                // 2: skey, 16 byte buffer, base64 encoded
     "\\\"natType\\\":%d,"                     // 3: NAT type
     "\\\"candidate\\\":%s,"                   // 4: Candidates, JSON array of objects
     "\\\"defaultRouteMacAddr\\\":\\\"%s\\\"," // 5: colon-separated lowercase values
     // NOTE: Needs to be an empty string if local peer address is not submitted
     //       This leads to broken JSON, but the official app does it this way as well ¯\_(ツ)_/¯
     "\\\"localPeerAddr\\\":%s,"               // 6: JSON object or **empty string**
     "\\\"localHashedId\\\":\\\"%s\\\"}";      // 7: 16 byte buffer, base64 encoded
static const char session_connrequest_candidate_fmt[] =
    "{\\\"type\\\":\\\"%s\\\","        // 0: STATIC/LOCAL
     "\\\"addr\\\":\\\"%s\\\","        // 1: IP address
     "\\\"mappedAddr\\\":\\\"%s\\\","  // 2: IP address
     "\\\"port\\\":%d,"                // 3: Port
     "\\\"mappedPort\\\":%d}";         // 4: Mapped Port
static const char session_localpeeraddr_fmt[] =
    "{\\\"accountId\\\":\\\"%" PRId64"\\\","   // 0: PSN account ID
     "\\\"platform\\\":\\\"%s\\\"}";    // 1: "PROSPERO" for PS5, "ORBIS" for PS4, "REMOTE_PLAY" for client

typedef enum notification_type_t
{
    NOTIFICATION_TYPE_UNKNOWN = 0,
    // psn:sessionManager:sys:remotePlaySession:created
    NOTIFICATION_TYPE_SESSION_CREATED = 1 << 0,
    // psn:sessionManager:sys:rps:members:created
    NOTIFICATION_TYPE_MEMBER_CREATED = 1 << 1,
    // psn:sessionManager:sys:rps:members:deleted
    NOTIFICATION_TYPE_MEMBER_DELETED = 1 << 2,
    // psn:sessionManager:sys:rps:customData1:updated
    NOTIFICATION_TYPE_CUSTOM_DATA1_UPDATED = 1 << 3,
    // psn:sessionManager:sys:rps:sessionMessage:created
    NOTIFICATION_TYPE_SESSION_MESSAGE_CREATED = 1 << 4,
    // psn:sessionManager:sys:remotePlaySession:deleted
    NOTIFICATION_TYPE_SESSION_DELETED = 1 << 5
} NotificationType;

typedef struct notification_t
{
    struct notification_t *next;

    NotificationType type;
    json_object* json;
    char* json_buf;
    size_t json_buf_size;
} Notification;

typedef struct notification_queue_t
{
    Notification *front, *rear;

} NotificationQueue;

typedef enum session_state_t
{
    SESSION_STATE_INIT = 1 << 0,
    SESSION_STATE_WS_OPEN = 1 << 1,
    SESSION_STATE_CREATED = 1 << 2,
    SESSION_STATE_STARTED = 1 << 3,
    SESSION_STATE_CLIENT_JOINED = 1 << 4,
    SESSION_STATE_DATA_SENT = 1 << 5,
    SESSION_STATE_CONSOLE_JOINED = 1 << 6,
    SESSION_STATE_CUSTOMDATA1_RECEIVED = 1 << 7,
    SESSION_STATE_CTRL_OFFER_RECEIVED = 1 << 8,
    SESSION_STATE_CTRL_OFFER_SENT = 1 << 9,
    SESSION_STATE_CTRL_CONSOLE_ACCEPTED = 1 << 10,
    SESSION_STATE_CTRL_CLIENT_ACCEPTED = 1 << 11,
    SESSION_STATE_CTRL_ESTABLISHED = 1 << 12,
    SESSION_STATE_DATA_OFFER_RECEIVED = 1 << 13,
    SESSION_STATE_DATA_OFFER_SENT = 1 << 14,
    SESSION_STATE_DATA_CONSOLE_ACCEPTED = 1 << 15,
    SESSION_STATE_DATA_CLIENT_ACCEPTED = 1 << 16,
    SESSION_STATE_DATA_ESTABLISHED = 1 << 17,
    SESSION_STATE_DELETED = 1 << 18,
    SESSION_STATE_WS_FAILED = 1 << 19
} SessionState;

typedef struct upnp_gateway_info_t
{
    char lan_ip[INET6_ADDRSTRLEN];
    struct UPNPUrls *urls;
    struct IGDdatas *data;
} UPNPGatewayInfo;

typedef enum upnp_gatway_status_t
{
    GATEWAY_STATUS_UNKNOWN = 1 << 0,
    GATEWAY_STATUS_FOUND = 1 << 1,
    GATEWAY_STATUS_NOT_FOUND = 1 << 2,
} UPNPGatewayStatus;
typedef enum session_message_action_t
{
    SESSION_MESSAGE_ACTION_UNKNOWN = 0,
    SESSION_MESSAGE_ACTION_OFFER = 1,
    SESSION_MESSAGE_ACTION_RESULT = 1 << 2,
    SESSION_MESSAGE_ACTION_ACCEPT = 1 << 3,
    SESSION_MESSAGE_ACTION_TERMINATE = 1 << 4,
} SessionMessageAction;

typedef struct http_response_data_t
{
    char* data;
    size_t size;
} HttpResponseData;

typedef enum candidate_type_t
{
    CANDIDATE_TYPE_STATIC = 0,
    CANDIDATE_TYPE_LOCAL = 1,
    CANDIDATE_TYPE_STUN = 2,
    CANDIDATE_TYPE_DERIVED = 3
} CandidateType;

typedef struct candidate_t
{
    CandidateType type;
    char addr[INET6_ADDRSTRLEN];
    char addr_mapped[INET6_ADDRSTRLEN];
    uint16_t port;
    uint16_t port_mapped;
} Candidate;

typedef struct connection_request_t
{
    uint32_t sid;
    uint32_t peer_sid;
    uint8_t skey[16];
    uint8_t nat_type;
    Candidate *candidates;
    size_t num_candidates;
    uint8_t default_route_mac_addr[6];
    uint8_t local_hashed_id[20];
} ConnectionRequest;

typedef struct session_message_t
{
    SessionMessageAction action;
    uint16_t req_id;
    uint16_t error;
    ConnectionRequest *conn_request;
    Notification *notification;
} SessionMessage;

typedef struct session_t
{
    // TODO: Clean this up, how much of this stuff do we really need?
    char* oauth_header;
    char* session_id_header;
    uint8_t console_uid[32];
    ChiakiHolepunchConsoleType console_type;

    chiaki_socket_t sock;
    Candidate *local_candidates;
    SessionMessage *our_offer_msg;

    uint64_t account_id;
    char *online_id;
    char session_id[UUIDV4_STR_LEN];
    char pushctx_id[UUIDV4_STR_LEN];

    uint16_t sid_local;
    uint16_t sid_console;
    uint8_t hashed_id_local[20];
    uint8_t hashed_id_console[20];
    size_t local_req_id;
    uint8_t local_mac_addr[6];
    uint16_t local_port_ctrl;
    uint16_t local_port_data;
    int32_t stun_allocation_increment;
    bool stun_random_allocation;
    bool force_port_guessing;
    int port_guessing_count;
    int port_guessing_socks;
    StunServer stun_server_list[10];
    StunServer stun_server_list_ipv6[10];
    size_t num_stun_servers;
    size_t num_stun_servers_ipv6;
    UPNPGatewayInfo gw;
    UPNPGatewayStatus gw_status;
    ChiakiThread upnp_thread;
    bool upnp_thread_running;

    uint8_t data1[16];
    uint8_t data2[16];
    uint8_t custom_data1[16];
    char ps_ip[INET6_ADDRSTRLEN];
    uint16_t ctrl_port;
    char client_local_ip[INET6_ADDRSTRLEN];

    CURLSH* curl_share;

    char* ws_fqdn;
    char* ws_fqdn_override;
    ChiakiThread ws_thread;
    bool ws_thread_created;
    NotificationQueue* ws_notification_queue;
    bool ws_thread_should_stop;
    bool ws_open;

    bool main_should_stop;

    ChiakiStopPipe notif_pipe;
    ChiakiStopPipe select_pipe;

    ChiakiMutex notif_mutex;
    ChiakiCond notif_cond;

    SessionState state;
    ChiakiMutex state_mutex;
    ChiakiMutex stop_mutex;
    ChiakiCond state_cond;

    chiaki_socket_t ipv4_sock;
    chiaki_socket_t ipv6_sock;

    chiaki_socket_t ctrl_sock;
    chiaki_socket_t data_sock;

    ChiakiLog *log;
} Session;

static ChiakiErrorCode make_oauth2_header(char** out, const char* token);
static ChiakiErrorCode make_session_id_header(char ** out, const char* session_id);
static ChiakiErrorCode get_websocket_fqdn(
    Session *session, char **fqdn);
static inline size_t curl_write_cb(
    void* ptr, size_t size, size_t nmemb, void* userdata);
static ChiakiErrorCode hex_to_bytes(const char* hex_str, uint8_t* bytes, size_t max_len);
static void bytes_to_hex(const uint8_t* bytes, size_t len, char* hex_str, size_t max_len);
static void random_uuidv4(char* out);
static void *websocket_thread_func(void *user);

static void stop_websocket_thread(Session *session)
{
    if(!session->ws_thread_created)
        return;
    chiaki_mutex_lock(&session->stop_mutex);
    session->ws_thread_should_stop = true;
    chiaki_mutex_unlock(&session->stop_mutex);
    chiaki_stop_pipe_stop(&session->select_pipe);
    chiaki_thread_join(&session->ws_thread, NULL);
    session->ws_thread_created = false;
}
static NotificationType parse_notification_type(ChiakiLog *log, json_object* json);
static ChiakiErrorCode send_offer(Session *session);
static ChiakiErrorCode send_accept(Session *session, int req_id, Candidate *selected_candidate);
static ChiakiErrorCode http_create_session(Session *session);
static ChiakiErrorCode http_check_session(Session *session, bool viewurl);
static ChiakiErrorCode http_start_session(Session *session);
static ChiakiErrorCode http_send_session_message(Session *session, SessionMessage *msg, bool short_msg);
static ChiakiErrorCode http_ps4_session_wakeup(Session *session);
static ChiakiErrorCode get_client_addr_local(Session *session, Candidate *local_console_candidate, char *out, size_t out_len);
static ChiakiErrorCode upnp_get_gateway_info(ChiakiLog *log, UPNPGatewayInfo *info);
static bool get_client_addr_remote_upnp(ChiakiLog *log, UPNPGatewayInfo *gw_info, char *out);
static bool upnp_add_udp_port_mapping(ChiakiLog *log, UPNPGatewayInfo *gw_info, uint16_t port_internal, uint16_t port_external);
static bool upnp_delete_udp_port_mapping(ChiakiLog *log, UPNPGatewayInfo *gw_info, uint16_t port_external);
static bool get_client_addr_remote_stun(Session *session, char *address, uint16_t *port, chiaki_socket_t *sock, bool ipv4);
static ChiakiErrorCode get_stun_servers(Session *session);
// static bool get_mac_addr(ChiakiLog *log, uint8_t *mac_addr);
static void log_session_state(Session *session);
static ChiakiErrorCode decode_customdata1(ChiakiLog *log, const char *customdata1, uint8_t *out, size_t out_len);
static ChiakiErrorCode check_candidates(
    Session *session, Candidate *local_candidates, Candidate *candidates_received, size_t num_candidates, chiaki_socket_t *out,
    Candidate *out_candidate);

static json_object* session_message_get_payload(ChiakiLog *log, json_object *session_message);
// static SessionMessageAction get_session_message_action(json_object *payload);
static ChiakiErrorCode wait_for_notification(
    Session *session, Notification** out,
    uint16_t types, uint64_t timeout_ms);
static ChiakiErrorCode clear_notification(
    Session *session, Notification *notification);
static void notification_queue_free(NotificationQueue* queue);
static NotificationQueue *createNq();
static void dequeueNq(NotificationQueue *nq);
static void enqueueNq(NotificationQueue *nq, Notification *notif);
static Notification* newNotification(NotificationType type, json_object *json, char* json_buf, size_t json_buf_size);
static void remove_substring(char *str, char *substring);

static ChiakiErrorCode wait_for_session_message(
    Session *session, SessionMessage** out,
    uint16_t types, uint64_t timeout_ms);
static ChiakiErrorCode wait_for_session_message_ack(
    Session *session, int req_id, uint64_t timeout_ms);
static ChiakiErrorCode session_message_parse(
    ChiakiLog *log, json_object *message_json, SessionMessage **out);
static ChiakiErrorCode session_message_serialize(
    Session *session, SessionMessage *message, char **out, size_t *out_len);
static ChiakiErrorCode short_message_serialize(
    Session *session, SessionMessage *message, char **out, size_t *out_len);
static ChiakiErrorCode session_message_free(SessionMessage *message);
static ChiakiErrorCode deleteSession(Session *session);
static void print_session_request(ChiakiLog *log, ConnectionRequest *req);
static void print_candidate(ChiakiLog *log, Candidate *candidate);
static ChiakiErrorCode receive_request_send_response_ps(Session *session, chiaki_socket_t *sock,
    Candidate *candidate, size_t timeout);
static ChiakiErrorCode send_response_ps(Session *session, uint8_t *req, chiaki_socket_t *sock,
    Candidate *candidate);
static ChiakiErrorCode send_responseto_ps(Session *session, uint8_t *req, chiaki_socket_t *sock,
    Candidate *candidate, struct sockaddr *addr, socklen_t len);

CHIAKI_EXPORT ChiakiErrorCode chiaki_holepunch_list_devices(
    const char* psn_oauth2_token, ChiakiHolepunchConsoleType console_type,
    ChiakiHolepunchDeviceInfo **devices, size_t *device_count,
    ChiakiLog *log)
{
    CURL *curl = holepunch_curl_easy_init();
    if(!curl)
    {
        CHIAKI_LOGE(log, "Curl could not init");
        return CHIAKI_ERR_MEMORY;
    }
    char url[133];
    char platform[4];
    if (console_type != CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5) {
        CHIAKI_LOGW(log, "Only PS5 is supported by the list devices function!");
        curl_easy_cleanup(curl);
        return CHIAKI_ERR_INVALID_DATA;
    }
    snprintf(platform, sizeof(platform), "%s", "PS5");
    snprintf(url, sizeof(url), device_list_url_fmt, platform);

    char* oauth_header = NULL;
    ChiakiErrorCode err = make_oauth2_header(&oauth_header, psn_oauth2_token);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        curl_easy_cleanup(curl);
        return err;
    }

    HttpResponseData response_data = {
        .data = malloc(0),
        .size = 0,
    };

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept-Language: jp");
    headers = curl_slist_append(headers, oauth_header);

    CURLcode res = curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(log, "chiaki_holepunch_list_devices: CURL setopt CURLOPT_FAILONERROR failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(log, "chiaki_holepunch_list_devices: CURL setopt CURLOPT_TIMEOUT failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_URL, url);
    if(res != CURLE_OK)
        CHIAKI_LOGW(log, "chiaki_holepunch_list_devices: CURL setopt CURLOPT_URL failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if(res != CURLE_OK)
        CHIAKI_LOGW(log, "chiaki_holepunch_list_devices: CURL setopt CURLOPT_HTTPHEADER failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    if(res != CURLE_OK)
        CHIAKI_LOGW(log, "chiaki_holepunch_list_devices: CURL setopt CURLOPT_WRITEFUNCTION failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_data);
    if(res != CURLE_OK)
        CHIAKI_LOGW(log, "chiaki_holepunch_list_devices: CURL setopt CURLOPT_WRITEDATA failed with CURL error %s", curl_easy_strerror(res));

    res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    if (res != CURLE_OK)
    {
        if (res == CURLE_HTTP_RETURNED_ERROR)
        {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            CHIAKI_LOGE(log, "chiaki_holepunch_list_devices: Fetching device list from %s failed with HTTP code %ld", url, http_code);
            CHIAKI_LOGV(log, "Response Body: %.*s.", (int)response_data.size, response_data.data);
            err = CHIAKI_ERR_HTTP_NONOK;
        } else {
            CHIAKI_LOGE(log, "chiaki_holepunch_list_devices: Fetching device list from %s failed with CURL error %s", url, curl_easy_strerror(res));
            err = CHIAKI_ERR_NETWORK;
        }
        goto cleanup;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200)
    {
        CHIAKI_LOGE(log, "chiaki_holepunch_list_devices: Fetching device list from %s failed with HTTP code %ld", url, http_code);
        err = CHIAKI_ERR_HTTP_NONOK;
        goto cleanup;
    }
    json_tokener *tok = json_tokener_new();
    if(!tok)
    {
        CHIAKI_LOGE(log, "Couldn't create new json tokener");
        goto cleanup;
    }
    json_object *json = json_tokener_parse_ex(tok, response_data.data, response_data.size);
    if (json == NULL)
    {
        CHIAKI_LOGE(log, "chiaki_holepunch_list_devices: Parsing JSON failed");
        err = CHIAKI_ERR_UNKNOWN;
        goto cleanup_json_tokener;
    }

    json_object *clients;
    if (!json_object_object_get_ex(json, "clients", &clients))
    {
        CHIAKI_LOGE(log, "chiaki_holepunch_list_devices: JSON does not contain \"clients\" field");
        err = CHIAKI_ERR_UNKNOWN;
        goto cleanup_json;
    } else if (!json_object_is_type(clients, json_type_array))
    {
        CHIAKI_LOGE(log, "chiaki_holepunch_list_devices: JSON \"clients\" field is not an array");
        err = CHIAKI_ERR_UNKNOWN;
        goto cleanup_json;
    }
    CHIAKI_LOGV(log, console_type == CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5 ? "PS5 devices: ": "PS4 devices: ");
    const char *json_str = json_object_to_json_string_ext(clients, JSON_C_TO_STRING_PRETTY);
    CHIAKI_LOGV(log, "chiaki_holepunch_list_devices: retrieved devices \n%s", json_str);
    size_t num_clients = json_object_array_length(clients);
    *devices = malloc(sizeof(ChiakiHolepunchDeviceInfo) * num_clients);
    if(!(*devices))
    {
        CHIAKI_LOGE(log, "chiaki_holepunch_list_devices: Memory could not be allocated for %zu devices", num_clients);
        return CHIAKI_ERR_MEMORY;
    }
    *device_count = num_clients;
    for (size_t i = 0; i < num_clients; i++)
    {
        ChiakiHolepunchDeviceInfo device;
        device.type = console_type;

        json_object *client = json_object_array_get_idx(clients, i);
        json_object *duid;
        if (!json_object_object_get_ex(client, "duid", &duid))
        {
            CHIAKI_LOGE(log, "chiaki_holepunch_list_devices: JSON does not contain \"duid\" field");
            err = CHIAKI_ERR_UNKNOWN;
            goto cleanup_devices;
        } else if (!json_object_is_type(duid, json_type_string))
        {
            CHIAKI_LOGE(log, "chiaki_holepunch_list_devices: JSON \"duid\" field is not a string");
            err = CHIAKI_ERR_UNKNOWN;
            goto cleanup_devices;
        }
        err = hex_to_bytes(json_object_get_string(duid), device.device_uid, sizeof(device.device_uid));
        if(err != CHIAKI_ERR_SUCCESS)
        {
            CHIAKI_LOGE(log, "chiaki_holepunch_list_devices: Could not convert duid to bytes");
            err = CHIAKI_ERR_UNKNOWN;
            goto cleanup_json;
        }

        json_object *device_json;
        if (!json_object_object_get_ex(client, "device", &device_json))
        {
            CHIAKI_LOGE(log, "chiaki_holepunch_list_devices: JSON does not contain \"device\" field");
            err = CHIAKI_ERR_UNKNOWN;
            goto cleanup_devices;
        } else if (!json_object_is_type(device_json, json_type_object))
        {
            CHIAKI_LOGE(log, "chiaki_holepunch_list_devices: JSON \"device\" field is not an object");
            err = CHIAKI_ERR_UNKNOWN;
            goto cleanup_devices;
        }

        json_object *enabled_features;
        if (!json_object_object_get_ex(device_json, "enabledFeatures", &enabled_features))
        {
            CHIAKI_LOGE(log, "chiaki_holepunch_list_devices: JSON does not contain \"enabledFeatures\" field");
            err = CHIAKI_ERR_UNKNOWN;
            goto cleanup_devices;
        } else if (!json_object_is_type(enabled_features, json_type_array))
        {
            CHIAKI_LOGE(log, "chiaki_holepunch_list_devices: JSON \"enabledFeatures\" field is not an array");
            err = CHIAKI_ERR_UNKNOWN;
            goto cleanup_devices;
        }
        device.remoteplay_enabled = false;
        size_t num_enabled_features = json_object_array_length(enabled_features);
        for (size_t j = 0; j < num_enabled_features; j++)
        {
            json_object *feature = json_object_array_get_idx(enabled_features, j);
            if (json_object_is_type(feature, json_type_string) && strcmp(json_object_get_string(feature), "remotePlay") == 0)
            {
                device.remoteplay_enabled = true;
                break;
            }
        }

        json_object *device_name;
        if (!json_object_object_get_ex(device_json, "name", &device_name))
        {
            CHIAKI_LOGE(log, "chiaki_holepunch_list_devices: JSON does not contain \"name\" field");
            err = CHIAKI_ERR_UNKNOWN;
            goto cleanup_devices;
        } else if (!json_object_is_type(device_name, json_type_string))
        {
            CHIAKI_LOGE(log, "chiaki_holepunch_list_devices: JSON \"name\" field is not a string");
            err = CHIAKI_ERR_UNKNOWN;
            goto cleanup_devices;
        }
        strncpy(device.device_name, json_object_get_string(device_name), sizeof(device.device_name));
        (*devices)[i] = device;
    }

cleanup_devices:
if (err != CHIAKI_ERR_SUCCESS)
    chiaki_holepunch_free_device_list(devices);
cleanup_json:
    json_object_put(json);
cleanup_json_tokener:
    json_tokener_free(tok);
cleanup:
    free(oauth_header);
    free(response_data.data);
    curl_easy_cleanup(curl);
    return err;
}

CHIAKI_EXPORT void chiaki_holepunch_free_device_list(ChiakiHolepunchDeviceInfo** devices)
{
    free(*devices);
    *devices = NULL;
}

CHIAKI_EXPORT ChiakiHolepunchRegistInfo chiaki_get_regist_info(Session *session)
{
    ChiakiHolepunchRegistInfo regist_info;
    memcpy(regist_info.data1, session->data1, sizeof(session->data1));
    memcpy(regist_info.data2, session->data2, sizeof(session->data2));
    memcpy(regist_info.custom_data1, session->custom_data1, sizeof(session->custom_data1));
    memcpy(regist_info.regist_local_ip, session->client_local_ip, sizeof(session->client_local_ip));
    return regist_info;
}

CHIAKI_EXPORT void chiaki_get_ps_selected_addr(Session *session, char *ps_ip)
{
    memcpy(ps_ip, session->ps_ip, sizeof(session->ps_ip));
}

CHIAKI_EXPORT uint16_t chiaki_get_ps_ctrl_port(Session *session)
{
    return session->ctrl_port;
}

CHIAKI_EXPORT chiaki_socket_t *chiaki_get_holepunch_sock(ChiakiHolepunchSession session, ChiakiHolepunchPortType type)
{
    switch(type)
    {
        case CHIAKI_HOLEPUNCH_PORT_TYPE_CTRL:
            return &session->ctrl_sock;
        case CHIAKI_HOLEPUNCH_PORT_TYPE_DATA:
            return &session->data_sock;
        default:
            CHIAKI_LOGE(session->log, "chose an invalid holepunch port type!");
            return NULL;
    }

}

CHIAKI_EXPORT bool chiaki_holepunch_session_get_stun_allocation(
    ChiakiHolepunchSession session, int32_t *allocation_increment, bool *random_allocation)
{
    if (!session)
        return false;

    if (allocation_increment)
        *allocation_increment = session->stun_allocation_increment;
    if (random_allocation)
        *random_allocation = session->stun_random_allocation;

    return true;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_holepunch_generate_client_device_uid(
    char *out, size_t *out_size)
{
    if (*out_size < CHIAKI_DUID_STR_SIZE)
    {
        return CHIAKI_ERR_BUF_TOO_SMALL;
    }
    uint8_t random_bytes[16];
    ChiakiErrorCode err = chiaki_random_bytes_crypt(random_bytes, sizeof(random_bytes));
    if (err != CHIAKI_ERR_SUCCESS)
        return err;

    *out_size += sprintf(out, "%s", DUID_PREFIX);
    for (int i = 0; i < sizeof(random_bytes); i++)
    {
        *out_size += sprintf(out + strlen(out), "%02x", random_bytes[i]);
    }

    return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT Session* chiaki_holepunch_session_init(
    const char* psn_oauth2_token, ChiakiLog *log)
{
    Session *session = malloc(sizeof(Session));
    if(!session)
        return NULL;
    make_oauth2_header(&session->oauth_header, psn_oauth2_token);
    session->session_id_header = NULL;
    session->log = log;

    session->ws_fqdn = NULL;
    session->ws_fqdn_override = NULL;
    session->ws_notification_queue = createNq();
    if(!session->ws_notification_queue)
    {
        free(session);
        return NULL;
    }
    session->local_candidates = NULL;
    session->our_offer_msg = NULL;
    session->ws_open = false;
    session->ws_thread_created = false;
    session->online_id = NULL;
    memset(&session->session_id, 0, sizeof(session->session_id));
    memset(&session->console_uid, 0, sizeof(session->console_uid));
    memset(&session->hashed_id_console, 0, sizeof(session->hashed_id_console));
    memset(&session->custom_data1, 0, sizeof(session->custom_data1));
    session->ctrl_sock = CHIAKI_INVALID_SOCKET;
    session->data_sock = CHIAKI_INVALID_SOCKET;
    session->ipv4_sock = CHIAKI_INVALID_SOCKET;
    session->ipv6_sock = CHIAKI_INVALID_SOCKET;
    session->sid_console = 0;
    session->local_req_id = 1;
    session->local_port_ctrl = 0;
    session->local_port_data = 0;
    session->stun_random_allocation = false;
    session->stun_allocation_increment = -1;
    session->force_port_guessing = false;
    session->port_guessing_count = RANDOM_ALLOCATION_GUESSES_NUMBER;
    session->port_guessing_socks = RANDOM_ALLOCATION_SOCKS_NUMBER;
    session->num_stun_servers = 0;
    session->num_stun_servers_ipv6 = 0;
    session->gw.data = NULL;
    session->gw_status = GATEWAY_STATUS_UNKNOWN;
    session->upnp_thread_running = false;

    ChiakiErrorCode err;
    err = chiaki_mutex_init(&session->notif_mutex, false);
    assert(err == CHIAKI_ERR_SUCCESS);
    err = chiaki_cond_init(&session->notif_cond);
    assert(err == CHIAKI_ERR_SUCCESS);
    err = chiaki_stop_pipe_init(&session->notif_pipe);
    assert(err == CHIAKI_ERR_SUCCESS);
    err = chiaki_stop_pipe_init(&session->select_pipe);
    assert(err == CHIAKI_ERR_SUCCESS);
    err = chiaki_mutex_init(&session->state_mutex, false);
    assert(err == CHIAKI_ERR_SUCCESS);
    err = chiaki_mutex_init(&session->stop_mutex, false);
    assert(err == CHIAKI_ERR_SUCCESS);
    err = chiaki_cond_init(&session->state_cond);
    assert(err == CHIAKI_ERR_SUCCESS);

    session->curl_share = curl_share_init();
    assert(session->curl_share != NULL);

    chiaki_mutex_lock(&session->stop_mutex);
    session->main_should_stop = false;
    session->ws_thread_should_stop = false;
    chiaki_mutex_unlock(&session->stop_mutex);

    chiaki_mutex_lock(&session->state_mutex);
    session->state = SESSION_STATE_INIT;
    log_session_state(session);
    chiaki_mutex_unlock(&session->state_mutex);

    random_uuidv4(session->pushctx_id);
    session->sid_local = chiaki_random_32();
    chiaki_random_bytes_crypt(session->hashed_id_local, sizeof(session->hashed_id_local));
    chiaki_random_bytes_crypt(session->data1, sizeof(session->data1));
    chiaki_random_bytes_crypt(session->data2, sizeof(session->data2));

    return session;
}

#define UPNP_DISCOVER_TIMEOUT_MS 7000

static void *upnp_discover_thread_func(void *arg)
{
    Session *session = (Session *)arg;
    ChiakiErrorCode err = upnp_get_gateway_info(session->log, &session->gw);

    chiaki_mutex_lock(&session->state_mutex);
    if (err == CHIAKI_ERR_SUCCESS)
        session->gw_status = GATEWAY_STATUS_FOUND;
    else
    {
        session->gw_status = GATEWAY_STATUS_NOT_FOUND;
        free(session->gw.data);
        session->gw.data = NULL;
        free(session->gw.urls);
        session->gw.urls = NULL;
    }
    session->upnp_thread_running = false;
    chiaki_cond_signal(&session->state_cond);
    chiaki_mutex_unlock(&session->state_mutex);
    return NULL;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_holepunch_upnp_discover(Session *session)
{
    session->gw.data = calloc(1, sizeof(struct IGDdatas));
    if(!session->gw.data)
    {
        return CHIAKI_ERR_MEMORY;
    }
    session->gw.urls = calloc(1, sizeof(struct UPNPUrls));
    if(!session->gw.urls)
    {
        free(session->gw.data);
        return CHIAKI_ERR_MEMORY;
    }

    session->upnp_thread_running = true;
    ChiakiErrorCode err = chiaki_thread_create(&session->upnp_thread, upnp_discover_thread_func, session);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(session->log, "Failed to create UPnP discovery thread, falling back to synchronous");
        session->upnp_thread_running = false;
        err = upnp_get_gateway_info(session->log, &session->gw);
        if (err == CHIAKI_ERR_SUCCESS)
            session->gw_status = GATEWAY_STATUS_FOUND;
        else
        {
            session->gw_status = GATEWAY_STATUS_NOT_FOUND;
            free(session->gw.data);
            session->gw.data = NULL;
            free(session->gw.urls);
            session->gw.urls = NULL;
        }
        return CHIAKI_ERR_SUCCESS;
    }

    chiaki_mutex_lock(&session->state_mutex);
    while(session->upnp_thread_running)
    {
        err = chiaki_cond_timedwait(&session->state_cond, &session->state_mutex, UPNP_DISCOVER_TIMEOUT_MS);
        if(err == CHIAKI_ERR_TIMEOUT)
            break;
    }

    if(session->upnp_thread_running)
    {
        chiaki_mutex_unlock(&session->state_mutex);
        CHIAKI_LOGW(session->log, "UPnP discovery timed out after %d ms, skipping", UPNP_DISCOVER_TIMEOUT_MS);
        session->gw_status = GATEWAY_STATUS_NOT_FOUND;
        return CHIAKI_ERR_SUCCESS;
    }
    chiaki_mutex_unlock(&session->state_mutex);

    chiaki_thread_join(&session->upnp_thread, NULL);
    return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_holepunch_session_create(Session* session)
{
    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
    if(session->ws_fqdn_override)
    {
        if(session->ws_fqdn)
            free(session->ws_fqdn);
        session->ws_fqdn = strdup(session->ws_fqdn_override);
        if(!session->ws_fqdn)
            return CHIAKI_ERR_MEMORY;
        CHIAKI_LOGW(session->log, "chiaki_holepunch_session_create: using overridden websocket FQDN %s", session->ws_fqdn);
    }
    else
    {
        err = get_websocket_fqdn(session, &session->ws_fqdn);
        if (err != CHIAKI_ERR_SUCCESS)
            return err;
    }
    chiaki_mutex_lock(&session->stop_mutex);
    if(session->main_should_stop)
    {
        session->main_should_stop = false;
        chiaki_mutex_unlock(&session->stop_mutex);
        CHIAKI_LOGI(session->log, "chiaki_holepunch_session_create: canceled");
        err = CHIAKI_ERR_CANCELED;
        return err;
    }
    chiaki_mutex_unlock(&session->stop_mutex);

    err = chiaki_thread_create(&session->ws_thread, websocket_thread_func, session);
    if (err != CHIAKI_ERR_SUCCESS)
        return err;
    session->ws_thread_created = true;
    chiaki_thread_set_name(&session->ws_thread, "Chiaki Holepunch WS");
    CHIAKI_LOGV(session->log, "chiaki_holepunch_session_create: Created websocket thread");

    chiaki_mutex_lock(&session->state_mutex);
    bool ws_wait_timed_out = false;
    while (!(session->state & (SESSION_STATE_WS_OPEN | SESSION_STATE_WS_FAILED)))
    {
        CHIAKI_LOGV(session->log, "chiaki_holepunch_session_create: Waiting for websocket to open...");
        err = chiaki_cond_timedwait(&session->state_cond, &session->state_mutex,
            SESSION_CREATION_TIMEOUT_SEC * 1000);
        if(err == CHIAKI_ERR_TIMEOUT)
        {
            ws_wait_timed_out = true;
            break;
        }

        chiaki_mutex_unlock(&session->state_mutex);
        chiaki_mutex_lock(&session->stop_mutex);
        bool should_stop = session->main_should_stop;
        chiaki_mutex_unlock(&session->stop_mutex);
        chiaki_mutex_lock(&session->state_mutex);
        if(should_stop)
            break;
    }
    bool ws_open = (session->state & SESSION_STATE_WS_OPEN) != 0;
    chiaki_mutex_unlock(&session->state_mutex);

    if(ws_wait_timed_out)
    {
        CHIAKI_LOGE(session->log, "chiaki_holepunch_session_create: Timed out after %d seconds waiting for the push notification websocket to open", SESSION_CREATION_TIMEOUT_SEC);
        stop_websocket_thread(session);
        return CHIAKI_ERR_TIMEOUT;
    }

    chiaki_mutex_lock(&session->stop_mutex);
    if(session->main_should_stop)
    {
        session->main_should_stop = false;
        chiaki_mutex_unlock(&session->stop_mutex);
        CHIAKI_LOGI(session->log, "chiaki_holepunch_session_create: canceled");
        err = CHIAKI_ERR_CANCELED;
        return err;
    }
    chiaki_mutex_unlock(&session->stop_mutex);

    if(!ws_open)
    {
        CHIAKI_LOGE(session->log, "chiaki_holepunch_session_create: Push notification websocket failed to open");
        return CHIAKI_ERR_NETWORK;
    }
    err = http_create_session(session);
    if (err != CHIAKI_ERR_SUCCESS)
        return err;
    CHIAKI_LOGV(session->log, "chiaki_holepunch_session_create: Sent holepunch session creation request");
    chiaki_mutex_lock(&session->stop_mutex);
    if(session->main_should_stop)
    {
        session->main_should_stop = false;
        chiaki_mutex_unlock(&session->stop_mutex);
        CHIAKI_LOGI(session->log, "chiaki_holepunch_session_create: canceled");
        err = CHIAKI_ERR_CANCELED;
        return err;
    }
    chiaki_mutex_unlock(&session->stop_mutex);

    // FIXME: We're currently not using a shared timeout for both  notifications, i.e. if the first one
    //        takes 29 secs arrive, and the second one takes 15 secs, we're not going to time out despite
    //        exceeing SESSION_CREATION_TIMEOUT_SEC.
    bool finished = false;
    Notification *notif = NULL;
    int notif_query = NOTIFICATION_TYPE_SESSION_CREATED
                    | NOTIFICATION_TYPE_MEMBER_CREATED;
    while (!finished)
    {
        err = wait_for_notification(session, &notif, notif_query, SESSION_CREATION_TIMEOUT_SEC * 1000);
        if (err == CHIAKI_ERR_TIMEOUT)
        {
            CHIAKI_LOGE(session->log, "chiaki_holepunch_session_create: Timed out waiting for holepunch session creation notifications.");
            return err;
        }
        else if (err == CHIAKI_ERR_CANCELED)
        {
            CHIAKI_LOGI(session->log, "chiaki_holepunch_session_create: canceled");
            return err;
        }
        else if (err != CHIAKI_ERR_SUCCESS)
        {
            CHIAKI_LOGE(session->log, "chiaki_holepunch_session_create: Failed to wait for holepunch session creation notifications.");
            return err;
        }
        chiaki_mutex_lock(&session->state_mutex);
        if (notif->type == NOTIFICATION_TYPE_SESSION_CREATED)
        {
            session->state |= SESSION_STATE_CREATED;
            CHIAKI_LOGV(session->log, "chiaki_holepunch_session_create: Holepunch session created.");
            // Get the user's online id
            json_object *online_id_json = NULL;
            json_pointer_get(notif->json, "/to/onlineId", &online_id_json);
            if (!online_id_json || !json_object_is_type(online_id_json, json_type_string))
            {
                CHIAKI_LOGE(session->log, "chiaki_holepunch_session_create: JSON does not contain member with online Id of user");
                const char *json_str = json_object_to_json_string_ext(notif->json, JSON_C_TO_STRING_PRETTY);
                CHIAKI_LOGV(session->log, "chiaki_holepunch_session_create: JSON was:\n%s", json_str);
                err = CHIAKI_ERR_UNKNOWN;
                break;
            }
            const char *online_id = json_object_get_string(online_id_json);
            if (!online_id_json)
            {
                CHIAKI_LOGE(session->log, "chiaki_holepunch_session_create: could not extra PSN online id string.");
                err = CHIAKI_ERR_UNKNOWN;
                return err;
            }
            session->online_id = malloc((strlen(online_id) + 1) * sizeof(char));
            if(!session->online_id)
            {
                CHIAKI_LOGE(session->log, "chiaki_holepunch_session_create: could not allocate space for PSN online id string.");
                err = CHIAKI_ERR_MEMORY;
                return err;
            }
            strcpy(session->online_id, online_id);
        }
        else if (notif->type == NOTIFICATION_TYPE_MEMBER_CREATED)
        {
            session->state |= SESSION_STATE_CLIENT_JOINED;
            CHIAKI_LOGV(session->log, "chiaki_holepunch_session_create: Client joined.");
        }
        else
        {
            CHIAKI_LOGE(session->log, "chiaki_holepunch_session_create: Got unexpected notification of type %d", notif->type);
            err = CHIAKI_ERR_UNKNOWN;
            return err;
        }
        chiaki_mutex_lock(&session->stop_mutex);
        if(session->main_should_stop)
        {
            session->main_should_stop = false;
            chiaki_mutex_unlock(&session->stop_mutex);
            CHIAKI_LOGI(session->log, "chiaki_holepunch_session_create: canceled");
            err = CHIAKI_ERR_CANCELED;
            return err;
        }
        chiaki_mutex_unlock(&session->stop_mutex);
        http_check_session(session, true);
        log_session_state(session);
        finished = (session->state & SESSION_STATE_CREATED) &&
                        (session->state & SESSION_STATE_CLIENT_JOINED);
        chiaki_mutex_unlock(&session->state_mutex);
        clear_notification(session, notif);
    }
    return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_holepunch_session_start(
    Session* session, const uint8_t* device_uid,
    ChiakiHolepunchConsoleType console_type)
{
    chiaki_mutex_lock(&session->state_mutex);
    if (!(session->state & SESSION_STATE_CREATED))
    {
        CHIAKI_LOGE(session->log, "chiaki_holepunch_session_start: Holepunch session not created yet");
        chiaki_mutex_unlock(&session->state_mutex);
        return CHIAKI_ERR_UNINITIALIZED;
    }
    if (session->state & SESSION_STATE_STARTED)
    {
        CHIAKI_LOGE(session->log, "chiaki_holepunch_session_start: Holepunch session already started");
        chiaki_mutex_unlock(&session->state_mutex);
        return CHIAKI_ERR_UNKNOWN;
    }
    chiaki_mutex_unlock(&session->state_mutex);
    ChiakiErrorCode err;
    session->console_type = console_type;
    if(console_type == CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS4)
    {
        CHIAKI_LOGV(session->log, "chiaki_holepunch_session_start: Starting holepunch session %s for the Main PS4 console registered to your PlayStation account", session->session_id);
        err = http_ps4_session_wakeup(session);
        if(err != CHIAKI_ERR_SUCCESS)
        {
            CHIAKI_LOGE(session->log, "chiaki_holepunch_session_start: Starting holepunch session for PS4 failed with error %d", err);
            return err;
        }
    }
    else
    {
        char duid_str[65];
        bytes_to_hex(device_uid, 32, duid_str, sizeof(duid_str));
        CHIAKI_LOGV(session->log, "chiaki_holepunch_session_start: Starting holepunch session %s for the PS5 console with duid %s", session->session_id, duid_str);
        memcpy(session->console_uid, device_uid, sizeof(session->console_uid));
        err = http_start_session(session);
        if (err != CHIAKI_ERR_SUCCESS)
        {
            CHIAKI_LOGE(session->log, "chiaki_holepunch_session_start: Starting holepunch session for PS5 failed with error %d", err);
            return err;
        }
    }
    chiaki_mutex_lock(&session->stop_mutex);
    if(session->main_should_stop)
    {
        session->main_should_stop = false;
        chiaki_mutex_unlock(&session->stop_mutex);
        CHIAKI_LOGI(session->log, "chiaki_holepunch_session_start: canceled");
        err = CHIAKI_ERR_CANCELED;
        return err;
    }
    chiaki_mutex_unlock(&session->stop_mutex);

    // FIXME: We're currently not using a shared timeout for both  notifications, i.e. if the first one
    //        takes 29 secs arrive, and the second one takes 15 secs, we're not going to time out despite
    //        exceeding SESSION_START_TIMEOUT_SEC.
    bool finished = false;
    Notification *notif = NULL;
    int notif_query = NOTIFICATION_TYPE_MEMBER_CREATED
                    | NOTIFICATION_TYPE_CUSTOM_DATA1_UPDATED;
    while (!finished)
    {
        err = wait_for_notification(session, &notif, notif_query, SESSION_START_TIMEOUT_SEC * 1000);
        if (err == CHIAKI_ERR_TIMEOUT)
        {
            CHIAKI_LOGE(session->log, "chiaki_holepunch_session_start: Timed out waiting for holepunch session start notifications.");
            return CHIAKI_ERR_HOST_DOWN;
        }
        else if (err == CHIAKI_ERR_CANCELED)
        {
            CHIAKI_LOGI(session->log, "chiaki_holepunch_session_start: canceled");
            return err;
        }
        else if (err != CHIAKI_ERR_SUCCESS)
        {
            CHIAKI_LOGE(session->log, "chiaki_holepunch_session_start: Failed to wait for holepunch session start notifications.");
            return CHIAKI_ERR_UNKNOWN;
        }

        assert(notif != NULL);

        chiaki_mutex_lock(&session->state_mutex);
        if (notif->type == NOTIFICATION_TYPE_MEMBER_CREATED)
        {
            // Check if the session now contains the console we requested
            json_object *member_duid_json = NULL;
            json_pointer_get(notif->json, "/body/data/members/0/deviceUniqueId", &member_duid_json);
            if (!member_duid_json || !json_object_is_type(member_duid_json, json_type_string))
            {
                CHIAKI_LOGE(session->log, "chiaki_holepunch_session_start: JSON does not contain member with a deviceUniqueId string field!");
                const char *json_str = json_object_to_json_string_ext(notif->json, JSON_C_TO_STRING_PRETTY);
                CHIAKI_LOGV(session->log, "chiaki_holepunch_session_start: JSON was:\n%s", json_str);
                err = CHIAKI_ERR_UNKNOWN;
                break;
            }
            const char *member_duid = json_object_get_string(member_duid_json);
            if (strlen(member_duid) != 64)
            {
                CHIAKI_LOGE(session->log, "chiaki_holepunch_session_start: \"deviceUniqueId\" has unexpected length, got %zu, expected 64", strlen(member_duid));
                err = CHIAKI_ERR_UNKNOWN;
                break;
            }

            uint8_t duid_bytes[32];
            ChiakiErrorCode err = hex_to_bytes(member_duid, duid_bytes, sizeof(duid_bytes));
            if(err != CHIAKI_ERR_SUCCESS)
            {
                CHIAKI_LOGE(session->log, "chiaki_holepunch_session_start: Could not convert member duid to bytes");
                err = CHIAKI_ERR_UNKNOWN;
                break;
            }
            // We don't have duid beforehand for PS4
            if(console_type == CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS4)
                memcpy(session->console_uid, duid_bytes, sizeof(duid_bytes));
            else if (memcmp(duid_bytes, session->console_uid, sizeof(session->console_uid)) != 0)
            {
                CHIAKI_LOGE(session->log, "chiaki_holepunch_session_start: holepunch session does not contain console");
                err = CHIAKI_ERR_UNKNOWN;
                break;
            }

            session->state |= SESSION_STATE_CONSOLE_JOINED;
        } else if (notif->type == NOTIFICATION_TYPE_CUSTOM_DATA1_UPDATED)
        {
            json_object *custom_data1_json = NULL;
            json_pointer_get(notif->json, "/body/data/customData1", &custom_data1_json);
            if (!custom_data1_json || !json_object_is_type(custom_data1_json, json_type_string))
            {
                CHIAKI_LOGE(session->log, "chiaki_holepunch_session_start: JSON does not contain \"customData1\" string field");
                const char *json_str = json_object_to_json_string_ext(notif->json, JSON_C_TO_STRING_PRETTY);
                CHIAKI_LOGV(session->log, "chiaki_holepunch_session_start: JSON was:\n%s", json_str);
                err = CHIAKI_ERR_UNKNOWN;
                break;
            }
            const char *custom_data1 = json_object_get_string(custom_data1_json);
            if (strlen(custom_data1) != 32)
            {
                CHIAKI_LOGE(session->log, "chiaki_holepunch_session_start: \"customData1\" has unexpected length, got %zu, expected 32", strlen(custom_data1));
                err = CHIAKI_ERR_UNKNOWN;
                break;
            }
            err = decode_customdata1(session->log, custom_data1, session->custom_data1, sizeof(session->custom_data1));
            if (err != CHIAKI_ERR_SUCCESS)
            {
                CHIAKI_LOGE(session->log, "chiaki_holepunch_session_start: Failed to decode \"customData1\": '%s' with error %s", custom_data1, chiaki_error_string(err));
                break;
            }
            session->state |= SESSION_STATE_CUSTOMDATA1_RECEIVED;
        }
        else
        {
            CHIAKI_LOGE(session->log, "chiaki_holepunch_session_start: Got unexpected notification of type %d", notif->type);
            err = CHIAKI_ERR_UNKNOWN;
            break;
        }
        clear_notification(session, notif);
        chiaki_mutex_lock(&session->stop_mutex);
        if(session->main_should_stop)
        {
            session->main_should_stop = false;
            chiaki_mutex_unlock(&session->stop_mutex);
            CHIAKI_LOGI(session->log, "chiaki_holepunch_session_start: canceled");
            err = CHIAKI_ERR_CANCELED;
            return err;
        }
        chiaki_mutex_unlock(&session->stop_mutex);
        http_check_session(session, false);
        finished = (session->state & SESSION_STATE_CONSOLE_JOINED) &&
                   (session->state & SESSION_STATE_CUSTOMDATA1_RECEIVED);
        log_session_state(session);
        chiaki_mutex_unlock(&session->state_mutex);
    }
    chiaki_mutex_unlock(&session->state_mutex);
    return err;
}

/**
 * Wakes up and connects to the main PS4 console connected to a PSN account
 * (only main console can be used for remote connection via PSN due to a limitation imposed by Sony)
 *
 * @param session
 * @param[in] json The pointer to the json object of the notification
 * @param[in] json_buf A pointer to the char representation of the json
 * @param[in] json_buf_size The length of the char representation of the json
 * @return notif Created notification
*/
static ChiakiErrorCode http_ps4_session_wakeup(Session *session)
{
    HttpResponseData response_data = {
        .data = malloc(0),
        .size = 0,
    };

    CURL *curl = holepunch_curl_easy_init();
    if(!curl)
    {
        CHIAKI_LOGE(session->log, "Curl could not init");
        return CHIAKI_ERR_MEMORY;
    }

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, session->oauth_header);
    headers = curl_slist_append(headers, "Host: asm.np.community.playstation.net");
    headers = curl_slist_append(headers, "Connection: Keep-Alive");
    headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");
    headers = curl_slist_append(headers, "User-Agent: RpNetHttpUtilImpl");

    CURLcode res = curl_easy_setopt(curl, CURLOPT_SHARE, session->curl_share);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_ps4_session_wakeup: CURL setopt CURLOPT_SHARE failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_ps4_session_wakeup: CURL setopt CURLOPT_FAILONERROR failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_ps4_session_wakeup: CURL setopt CURLOPT_TIMEOUT failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_URL, user_profile_url);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_ps4_session_wakeup: CURL setopt CURLOPT_URL failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_ps4_session_wakeup: CURL setopt CURLOPT_HTTPHEADER failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_ps4_session_wakeup: CURL setopt CURLOPT_WRITEFUNCTION failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_data);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_ps4_session_wakeup: CURL setopt CURLOPT_WRITEDATA failed with CURL error %s", curl_easy_strerror(res));

    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
    res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    CHIAKI_LOGV(session->log, "http_ps4_session_wakeup: Received JSON:\n%.*s", (int)response_data.size, response_data.data);
    if (res != CURLE_OK)
    {
        if (res == CURLE_HTTP_RETURNED_ERROR)
        {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            CHIAKI_LOGE(session->log, "http_ps4_session_wakeup: Retrieving profile information for PS4 wakeup command failed with HTTP code %ld.", http_code);
            CHIAKI_LOGV(session->log, "Response Body: %.*s.", (int)response_data.size, response_data.data);
            err = CHIAKI_ERR_HTTP_NONOK;
        } else {
            CHIAKI_LOGE(session->log, "http_ps4_session_wakeup: Retrieving profile information for PS4 wakeup command failed with CURL error %s", curl_easy_strerror(res));
            err = CHIAKI_ERR_NETWORK;
        }
        goto cleanup;
    }

    json_tokener *tok = json_tokener_new();
    if(!tok)
    {
        CHIAKI_LOGE(session->log, "Couldn't create new json tokener");
        goto cleanup;
    }
    json_object *json = json_tokener_parse_ex(tok, response_data.data, response_data.size);
    if (json == NULL)
    {
        CHIAKI_LOGE(session->log, "http_ps4_session_wakeup: Parsing JSON failed");
        err = CHIAKI_ERR_UNKNOWN;
        goto cleanup_json_tokener;
    }

    json_object *user_profile_url_json = NULL;
    json_pointer_get(json, "/url", &user_profile_url_json);

    bool schema_bad =
        (user_profile_url_json == NULL
        || !json_object_is_type(user_profile_url_json, json_type_string));
    if (schema_bad)
    {
        CHIAKI_LOGE(session->log, "http_ps4_session_wakeup: Unexpected JSON schema, could not parse user profile url");
        CHIAKI_LOGV(session->log, json_object_to_json_string_ext(json, JSON_C_TO_STRING_PRETTY));
        err = CHIAKI_ERR_UNKNOWN;
        goto cleanup_json;
    }

    const char *user_profile_url = json_object_get_string(user_profile_url_json);
    if (!user_profile_url)
    {
        CHIAKI_LOGE(session->log, "http_ps4_session_wakeup: Could not extract user profile url string.");
        err = CHIAKI_ERR_UNKNOWN;
        goto cleanup_json;
    }

    char host_url_starter[128];
    char host_url[128];
    memcpy(host_url, user_profile_url, strlen(user_profile_url) + 1);
    // Get just the base url
    remove_substring(host_url, "https://");
    remove_substring(host_url, "http://");
    strcpy(host_url_starter, host_url);
    char *ptr = strtok(host_url_starter, "/");
    if(!(ptr == (host_url_starter + strlen(host_url_starter))))
        strcpy(host_url, ptr);
    memset(host_url_starter, 0, 128);
    strcpy(host_url_starter, host_url);
    ptr = strtok(host_url_starter, "?");
    // if string found, copy part before string found, else leave string as-is
    if(!(ptr == (host_url_starter + strlen(host_url_starter))))
        strcpy(host_url, ptr);
    memset(host_url_starter, 0, 128);
    strcpy(host_url_starter, host_url);
    ptr = strtok(host_url_starter, "#");
    if(!(ptr == (host_url_starter + strlen(host_url_starter))))
        strcpy(host_url, ptr);

    curl_easy_cleanup(curl);
    free(response_data.data);
    response_data.data = malloc(0);
    response_data.size = 0;
    headers = NULL;
    char url[128] = {0};
    snprintf(url, sizeof(url), wakeup_url_fmt, user_profile_url, session->online_id);

    char envelope_buf[sizeof(session_wakeup_envelope_fmt) * 2] = {0};

    char data1_base64[25] = {0};
    char data2_base64[25] = {0};
    err = chiaki_base64_encode(session->data1, sizeof(session->data1), data1_base64, sizeof(data1_base64));
    if(err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(session->log, "http_ps4_session_wakeup: Could not encode data1 into base64.");
        goto cleanup_json;
    }
    err = chiaki_base64_encode(session->data2, sizeof(session->data2), data2_base64, sizeof(data2_base64));
    if(err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(session->log, "http_ps4_session_wakeup: Could not encode data2 into base64.");
        goto cleanup_json;
    }
    snprintf(envelope_buf, sizeof(envelope_buf), session_wakeup_envelope_fmt,
        data1_base64,
        data2_base64,
        session->session_id);

    curl = holepunch_curl_easy_init();
    if(!curl)
    {
        CHIAKI_LOGE(session->log, "Curl could not init");
        json_object_put(json);
        json_tokener_free(tok);
        return CHIAKI_ERR_MEMORY;
    }

    char host_url_string[134];
    snprintf(host_url_string, sizeof(host_url_string), "Host: %s", host_url);
    headers = curl_slist_append(headers, session->oauth_header);
    headers = curl_slist_append(headers, host_url_string);
    headers = curl_slist_append(headers, "Connection: Keep-Alive");
    headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");
    headers = curl_slist_append(headers, "User-Agent: RpNetHttpUtilImpl");

    res = curl_easy_setopt(curl, CURLOPT_SHARE, session->curl_share);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_ps4_session_wakeup: CURL setopt CURLOPT_SHARE failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_ps4_session_wakeup: CURL setopt CURLOPT_FAILONERROR failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_ps4_session_wakeup: CURL setopt CURLOPT_TIMEOUT failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_URL, url);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_ps4_session_wakeup: CURL setopt CURLOPT_URL failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_ps4_session_wakeup: CURL setopt CURLOPT_HTTPHEADER failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, envelope_buf);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_ps4_session_wakeup: CURL setopt CURLOPT_POSTFIELDS failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_ps4_session_wakeup: CURL setopt CURLOPT_WRITEFUNCTION failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_data);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_ps4_session_wakeup: CURL setopt CURLOPT_WRITEDATA failed with CURL error %s", curl_easy_strerror(res));

    CHIAKI_LOGV(session->log, "http_ps4_session_wakeup: Sending JSON:\n%s", envelope_buf);

    res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    CHIAKI_LOGV(session->log, "http_ps4_session_wakeup: Received JSON:\n%.*s", (int)response_data.size, response_data.data);
    if (res != CURLE_OK)
    {
        if (res == CURLE_HTTP_RETURNED_ERROR)
        {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            CHIAKI_LOGE(session->log, "http_ps4_session_wakeup: Waking up ps4 console failed with HTTP code %ld.", http_code);
            CHIAKI_LOGV(session->log, "Request Body: %s.", envelope_buf);
            CHIAKI_LOGV(session->log, "Response Body: %.*s.", (int)response_data.size, response_data.data);
            if(http_code == 404)
                CHIAKI_LOGE(session->log, "http_ps4_session_wakeup: Please make sure PS4 is registered to your account and on or in rest mode.");
            err = CHIAKI_ERR_HTTP_NONOK;
        } else {
            CHIAKI_LOGE(session->log, "http_ps4_session_wakeup: Waking up ps4 console failed with CURL error %s", curl_easy_strerror(res));
            err = CHIAKI_ERR_NETWORK;
        }
        goto cleanup_json;
    }

    chiaki_mutex_lock(&session->state_mutex);
    session->state |= SESSION_STATE_DATA_SENT;
    log_session_state(session);
    chiaki_mutex_unlock(&session->state_mutex);

cleanup_json:
    json_object_put(json);
cleanup_json_tokener:
    json_tokener_free(tok);
cleanup:
    curl_easy_cleanup(curl);
    free(response_data.data);

    return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_holepunch_session_punch_hole(Session* session, ChiakiHolepunchPortType port_type)
{
    ChiakiErrorCode err;
    chiaki_mutex_lock(&session->state_mutex);
    if (port_type == CHIAKI_HOLEPUNCH_PORT_TYPE_CTRL
        && !(session->state & SESSION_STATE_CUSTOMDATA1_RECEIVED))
    {
        CHIAKI_LOGE(session->log, "chiaki_holepunch_session_punch_holes: customData1 not received yet.");
        chiaki_mutex_unlock(&session->state_mutex);
        err = CHIAKI_ERR_UNKNOWN;
        goto offer_cleanup;
    }
    else if (port_type == CHIAKI_HOLEPUNCH_PORT_TYPE_DATA
        && !(session->state & SESSION_STATE_CTRL_ESTABLISHED))
    {
        chiaki_mutex_unlock(&session->state_mutex);
        CHIAKI_LOGE(session->log, "chiaki_holepunch_session_punch_holes: Control port not open yet.");
        err = CHIAKI_ERR_UNKNOWN;
        goto offer_cleanup;
    }
    chiaki_mutex_unlock(&session->state_mutex);

    // NOTE: Needs to be kept around until the end, we're using the candidates in the message later on
    SessionMessage *console_offer_msg = NULL;
    err = wait_for_session_message(session, &console_offer_msg, SESSION_MESSAGE_ACTION_OFFER, SESSION_START_TIMEOUT_SEC * 1000);
    if (err == CHIAKI_ERR_TIMEOUT)
    {
        CHIAKI_LOGE(session->log, "chiaki_holepunch_session_punch_holes: Timed out waiting for OFFER holepunch session message.");
        goto offer_cleanup;
    }
    else if (err == CHIAKI_ERR_CANCELED)
    {
        CHIAKI_LOGI(session->log, "chiaki_holepunch_session_punch_holes: canceled");
        goto offer_cleanup;
    }
    else if (err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(session->log, "chiaki_holepunch_session_punch_holes: Failed to wait for OFFER holepunch session message.");
        goto offer_cleanup;
    }
    ConnectionRequest *console_req = console_offer_msg->conn_request;
    memcpy(session->hashed_id_console, console_req->local_hashed_id, sizeof(session->hashed_id_console));
    session->sid_console = console_req->sid;

    chiaki_mutex_lock(&session->state_mutex);
    if(port_type == CHIAKI_HOLEPUNCH_PORT_TYPE_CTRL)
        session->state |= SESSION_STATE_CTRL_OFFER_RECEIVED;
    else
        session->state |= SESSION_STATE_DATA_OFFER_RECEIVED;
    chiaki_mutex_unlock(&session->state_mutex);

    // ACK the message
    SessionMessage ack_msg = {
        .action = SESSION_MESSAGE_ACTION_RESULT,
        .req_id = console_offer_msg->req_id,
        .error = 0,
        .conn_request = NULL,
        .notification = NULL,
    };
    err = http_send_session_message(session, &ack_msg, true);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(session->log, "chiaki_holepunch_session_punch_holes: Couldn't send holepunch session message");
        goto cleanup;
    }
    chiaki_mutex_lock(&session->stop_mutex);
    if(session->main_should_stop)
    {
        session->main_should_stop = false;
        chiaki_mutex_unlock(&session->stop_mutex);
        CHIAKI_LOGI(session->log, "chiaki_holepunch_session_punch_holes: canceled");
        err = CHIAKI_ERR_CANCELED;
        goto cleanup;
    }
    chiaki_mutex_unlock(&session->stop_mutex);

    // Send our own OFFER
    const int our_offer_req_id = session->local_req_id;
    session->local_req_id++;
    session->our_offer_msg->req_id = our_offer_req_id;
    send_offer(session);

    // Wait for ACK of OFFER, ignore other OFFERs, simply ACK them
    err = wait_for_session_message_ack(
        session, our_offer_req_id, SESSION_START_TIMEOUT_SEC * 1000);
    if (err == CHIAKI_ERR_TIMEOUT)
    {
        CHIAKI_LOGE(session->log, "chiaki_holepunch_session_punch_holes: Timed out waiting for ACK of our connection offer.");
        goto cleanup;
    }
    else if (err == CHIAKI_ERR_CANCELED)
    {
        CHIAKI_LOGI(session->log, "chiaki_holepunch_session_punch_holes: canceled");
        goto cleanup;
    }
    else if (err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(session->log, "chiaki_holepunch_session_punch_holes: Failed to wait for ACK of our connection offer.");
        goto cleanup;
    }
    http_check_session(session, true);

    // Find candidate that we can use to connect to the console
    chiaki_socket_t sock = CHIAKI_INVALID_SOCKET;
    for(size_t i = 0; i < console_req->num_candidates; i++)
    {
        print_candidate(session->log, &console_req->candidates[i]);
    }
    Candidate selected_candidate;
    err = check_candidates(session, session->local_candidates, console_req->candidates, console_req->num_candidates, &sock, &selected_candidate);
    if (err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(
            session->log, "chiaki_holepunch_session_punch_holes: Failed to find reachable candidate for %s connection.",
            port_type == CHIAKI_HOLEPUNCH_PORT_TYPE_CTRL ? "control" : "data");
        goto cleanup;
    }
    chiaki_mutex_lock(&session->stop_mutex);
    if(session->main_should_stop)
    {
        session->main_should_stop = false;
        chiaki_mutex_unlock(&session->stop_mutex);
        CHIAKI_LOGI(session->log, "chiaki_holepunch_session_punch_holes: canceled");
        err = CHIAKI_ERR_CANCELED;
        goto cleanup;
    }
    chiaki_mutex_unlock(&session->stop_mutex);

    err = send_accept(session, session->local_req_id, &selected_candidate);
    if (err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(session->log, "chiaki_holepunch_session_punch_holes: Failed to send ACCEPT message.");
        goto cleanup;
    }
    chiaki_mutex_lock(&session->stop_mutex);
    if(session->main_should_stop)
    {
        session->main_should_stop = false;
        chiaki_mutex_unlock(&session->stop_mutex);
        CHIAKI_LOGI(session->log, "chiaki_holepunch_session_punch_holes: canceled");
        err = CHIAKI_ERR_CANCELED;
        goto cleanup;
    }
    chiaki_mutex_unlock(&session->stop_mutex);
    session->local_req_id++;

    SessionMessage *msg = NULL;
    err = wait_for_session_message(session, &msg, SESSION_MESSAGE_ACTION_ACCEPT, SESSION_START_TIMEOUT_SEC * 1000);
    if (err == CHIAKI_ERR_TIMEOUT)
    {
        CHIAKI_LOGE(session->log, "chiaki_holepunch_session_punch_holes: Timed out waiting for ACCEPT holepunch session message.");
        goto cleanup_msg;
    }
    else if (err == CHIAKI_ERR_CANCELED)
    {
        CHIAKI_LOGI(session->log, "chiaki_holepunch_session_punch_holes: canceled");
        goto cleanup_msg;
    }
    else if (err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(session->log, "chiaki_holepunch_session_punch_holes: Failed to wait for ACCEPT or OFFER holepunch session message.");
        goto cleanup_msg;
    }

    // ACK the accept message response
    SessionMessage accept_ack_msg = {
        .action = SESSION_MESSAGE_ACTION_RESULT,
        .req_id = msg->req_id,
        .error = 0,
        .conn_request = NULL,
        .notification = NULL,
    };
    err = http_send_session_message(session, &accept_ack_msg, true);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(session->log, "chiaki_holepunch_session_punch_holes: Couldn't send holepunch session message");
        goto cleanup_msg;
    }
    chiaki_mutex_lock(&session->stop_mutex);
    if(session->main_should_stop)
    {
        session->main_should_stop = false;
        chiaki_mutex_unlock(&session->stop_mutex);
        CHIAKI_LOGI(session->log, "chiaki_holepunch_session_punch_holes: canceled");
        err = CHIAKI_ERR_CANCELED;
        goto cleanup_msg;
    }
    chiaki_mutex_unlock(&session->stop_mutex);
    memcpy(session->ps_ip, selected_candidate.addr, sizeof(session->ps_ip));

    chiaki_mutex_lock(&session->state_mutex);
    if(port_type == CHIAKI_HOLEPUNCH_PORT_TYPE_CTRL)
    {
        session->state |= SESSION_STATE_CTRL_ESTABLISHED;
        session->ctrl_sock = sock;
        session->ctrl_port = selected_candidate.port;
        CHIAKI_LOGV(session->log, "chiaki_holepunch_session_punch_holes: Control connection established.");
    }
    else
    {
        session->state |= SESSION_STATE_DATA_ESTABLISHED;
        session->data_sock = sock;
        CHIAKI_LOGV(session->log, "chiaki_holepunch_session_punch_holes: Data connection established.");
    }
    chiaki_mutex_unlock(&session->state_mutex);

    chiaki_mutex_lock(&session->stop_mutex);
    if(session->main_should_stop)
    {
        session->main_should_stop = false;
        chiaki_mutex_unlock(&session->stop_mutex);
        CHIAKI_LOGI(session->log, "chiaki_holepunch_session_punch_holes: canceled");
        err = CHIAKI_ERR_CANCELED;
        goto cleanup_msg;
    }
    chiaki_mutex_unlock(&session->stop_mutex);
    ChiakiErrorCode pserr = receive_request_send_response_ps(session, &sock, &selected_candidate, WAIT_RESPONSE_TIMEOUT_SEC);
    if(!(pserr == CHIAKI_ERR_SUCCESS || pserr == CHIAKI_ERR_TIMEOUT))
    {
        CHIAKI_LOGE(session->log, "Sending extra request to ps failed");
        err = pserr;
    }
    log_session_state(session);

cleanup_msg:
    chiaki_mutex_lock(&session->notif_mutex);
    session_message_free(msg);
    chiaki_mutex_unlock(&session->notif_mutex);
cleanup:
    if(err != CHIAKI_ERR_SUCCESS)
    {
        if(!CHIAKI_SOCKET_IS_INVALID(session->ipv4_sock))
        {
            CHIAKI_SOCKET_CLOSE(session->ipv4_sock);
            session->ipv4_sock = CHIAKI_INVALID_SOCKET;
        }
        if(!CHIAKI_SOCKET_IS_INVALID(session->ipv6_sock))
        {
            CHIAKI_SOCKET_CLOSE(session->ipv6_sock);
            session->ipv6_sock = CHIAKI_INVALID_SOCKET;
        }
    }
    chiaki_mutex_lock(&session->notif_mutex);
    session_message_free(console_offer_msg);
    chiaki_mutex_unlock(&session->notif_mutex);
offer_cleanup:
    if(session->our_offer_msg)
    {
        session_message_free(session->our_offer_msg);
        session->our_offer_msg = NULL;
    }
    if(session->local_candidates)
    {
        free(session->local_candidates);
        session->local_candidates = NULL;
    }
    return err;
}

CHIAKI_EXPORT void chiaki_holepunch_session_force_port_guessing(Session *session, bool enabled)
{
    session->force_port_guessing = enabled;
}

CHIAKI_EXPORT void chiaki_holepunch_session_set_ws_fqdn_override(Session *session, const char *fqdn)
{
    if(session->ws_fqdn_override)
    {
        free(session->ws_fqdn_override);
        session->ws_fqdn_override = NULL;
    }
    if(fqdn && *fqdn)
        session->ws_fqdn_override = strdup(fqdn);
}

CHIAKI_EXPORT void chiaki_holepunch_session_set_port_guessing_ports(Session* session, int count)
{
    if(count > 0)
        session->port_guessing_count = count;
}

CHIAKI_EXPORT void chiaki_holepunch_session_set_port_guessing_socks(Session* session, int count)
{
    if(count > 0)
        session->port_guessing_socks = count;
}

CHIAKI_EXPORT void chiaki_holepunch_session_fini(Session* session)
{
    chiaki_mutex_lock(&session->state_mutex);
    bool ws_open = session->ws_open;
    chiaki_mutex_unlock(&session->state_mutex);
    if(ws_open && session->ws_thread_created)
    {
        ChiakiErrorCode err = deleteSession(session);
        if(err != CHIAKI_ERR_SUCCESS)
            CHIAKI_LOGE(session->log, "Couldn't remove our holepunch session gracefully from PlayStation servers.");
        bool finished = false;
        Notification *notif = NULL;
        int notif_query = NOTIFICATION_TYPE_MEMBER_DELETED | NOTIFICATION_TYPE_SESSION_DELETED;
        while (!finished)
        {
            err = wait_for_notification(session, &notif, notif_query, SESSION_DELETION_TIMEOUT_SEC * 1000);
            if (err == CHIAKI_ERR_TIMEOUT)
            {
                CHIAKI_LOGE(session->log, "chiaki_holepunch_session_fini: Timed out waiting for holepunch session deletion notifications.");
                break;
            }
            else if (err != CHIAKI_ERR_SUCCESS)
            {
                CHIAKI_LOGE(session->log, "chiaki_holepunch_session_fini: Failed to wait for holepunch session deletion notifications.");
                break;
            }

            if (notif->type == NOTIFICATION_TYPE_MEMBER_DELETED || notif->type == NOTIFICATION_TYPE_SESSION_DELETED)
            {
                chiaki_mutex_lock(&session->state_mutex);
                session->state |= SESSION_STATE_DELETED;
                chiaki_mutex_unlock(&session->state_mutex);
                log_session_state(session);
                CHIAKI_LOGI(session->log, "chiaki_holepunch_session_fini: Holepunch session deleted.");
                finished = true;
            }
            else
            {
                CHIAKI_LOGE(session->log, "chiaki_holepunch_session_fini: Got unexpected notification of type %d", notif->type);
                err = CHIAKI_ERR_UNKNOWN;
                break;
            }
            clear_notification(session, notif);
        }
    }
    stop_websocket_thread(session);
    if(session->upnp_thread_running)
    {
        CHIAKI_LOGI(session->log, "Waiting for UPnP discovery thread to finish...");
        chiaki_thread_join(&session->upnp_thread, NULL);
        session->upnp_thread_running = false;
    }
    if(session->gw.data)
    {
        if(session->local_port_ctrl != 0)
        {
            if(upnp_delete_udp_port_mapping(session->log, &session->gw, session->local_port_ctrl))
                CHIAKI_LOGI(session->log, "Deleted UPNP local port ctrl mapping");
            else
                CHIAKI_LOGE(session->log, "Couldn't delete UPNP local port ctrl mapping");
        }
        if(session->local_port_data != 0)
        {
            if(upnp_delete_udp_port_mapping(session->log, &session->gw, session->local_port_data))
                CHIAKI_LOGI(session->log, "Deleted UPNP local port data mapping");
            else
                CHIAKI_LOGE(session->log, "Couldn't delete UPNP local port data mapping");
        }
        free(session->gw.data);
        free(session->gw.urls);
    }
    if (session->oauth_header)
        free(session->oauth_header);
    if(session->session_id_header)
        free(session->session_id_header);
    if (session->online_id)
        free(session->online_id);
    if (session->curl_share)
        curl_share_cleanup(session->curl_share);
    if (session->ws_fqdn)
        free(session->ws_fqdn);
    if (session->ws_fqdn_override)
        free(session->ws_fqdn_override);
    if (session->ws_notification_queue)
    {
        chiaki_mutex_lock(&session->notif_mutex);
        notification_queue_free(session->ws_notification_queue);
        chiaki_mutex_unlock(&session->notif_mutex);
    }
    for(int i=0; i < session->num_stun_servers; i++)
    {
        free(session->stun_server_list[i].host);
    }
    for(int i=0; i < session->num_stun_servers_ipv6; i++)
    {
        free(session->stun_server_list_ipv6[i].host);
    }
    chiaki_stop_pipe_fini(&session->select_pipe);
    chiaki_stop_pipe_fini(&session->notif_pipe);
    chiaki_mutex_fini(&session->notif_mutex);
    chiaki_cond_fini(&session->notif_cond);
    chiaki_mutex_fini(&session->state_mutex);
    chiaki_cond_fini(&session->state_cond);
}

CHIAKI_EXPORT void chiaki_holepunch_main_thread_cancel(Session *session, bool stop_thread)
{
    chiaki_mutex_lock(&session->stop_mutex);
    if(stop_thread)
    {
        session->ws_thread_should_stop = true;
        chiaki_stop_pipe_stop(&session->select_pipe);
    }
    else
        CHIAKI_LOGI(session->log, "Canceling establishing connection over PSN");
    session->main_should_stop = true;
    chiaki_mutex_unlock(&session->stop_mutex);
    chiaki_cond_signal(&session->notif_cond);
    chiaki_cond_signal(&session->state_cond);
}

void notification_queue_free(NotificationQueue *nq)
{
    while(nq->front != NULL)
    {
        dequeueNq(nq);
    }
}

static ChiakiErrorCode make_oauth2_header(char** out, const char* token)
{
    size_t oauth_header_len = sizeof(oauth_header_fmt) + strlen(token) + 1;
    *out = malloc(oauth_header_len);
    if(!(*out))
        return CHIAKI_ERR_MEMORY;
    snprintf(*out, oauth_header_len, oauth_header_fmt, token);
    return CHIAKI_ERR_SUCCESS;
}

static ChiakiErrorCode make_session_id_header(char **out, const char* session_id)
{
    size_t session_id_header_len = sizeof(session_id_header_fmt) + strlen(session_id) + 1;
    *out = malloc(session_id_header_len);
    if(!(*out))
        return CHIAKI_ERR_MEMORY;
    snprintf(*out, session_id_header_len, session_id_header_fmt, session_id);
    return CHIAKI_ERR_SUCCESS;
}

/**
 * Get the fully qualified domain name of the websocket server that we can
 * get PSN notifications from.
 *
 * @param[in] session Session instance
 * @param[out] fqdn Pointer to a char* that will be set to the FQDN of the
 *                 websocket server. Must be freed by the caller.
 * @return CHIAKI_ERR_SUCCESS on success, otherwise an error code
*/
static ChiakiErrorCode get_websocket_fqdn(Session *session, char **fqdn)
{
    HttpResponseData response_data = {
        .data = malloc(0),
        .size = 0,
    };

    CURL *curl = holepunch_curl_easy_init();
    if(!curl)
    {
        CHIAKI_LOGE(session->log, "Curl could not init");
        return CHIAKI_ERR_MEMORY;
    }
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, session->oauth_header);

    CURLcode res = curl_easy_setopt(curl, CURLOPT_SHARE, session->curl_share);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "get_websocket_fqdn: CURL setopt CURLOPT_SHARE failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "get_websocket_fqdn: CURL setopt CURLOPT_FAILONERROR failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "get_websocket_fqdn: CURL setopt CURLOPT_TIMEOUT failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_URL, ws_fqdn_api_url);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "get_websocket_fqdn: CURL setopt CURLOPT_URL failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "get_websocket_fqdn: CURL setopt CURLOPT_HTTPHEADER failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "get_websocket_fqdn: CURL setopt CURLOPT_WRITEFUNCTION failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_data);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "get_websocket_fqdn: CURL setopt CURLOPT_WRITEDATA failed with CURL error %s", curl_easy_strerror(res));

    res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
    if (res != CURLE_OK)
    {
        if (res == CURLE_HTTP_RETURNED_ERROR)
        {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            CHIAKI_LOGE(session->log, "get_websocket_fqdn: Fetching websocket FQDN from %s failed with HTTP code %ld", ws_fqdn_api_url, http_code);
            err = CHIAKI_ERR_HTTP_NONOK;
        } else {
            CHIAKI_LOGE(session->log, "get_websocket_fqdn: Fetching websocket FQDN from %s failed with CURL error %s", ws_fqdn_api_url, curl_easy_strerror(res));
            err = CHIAKI_ERR_NETWORK;
        }
        goto cleanup;
    }

    json_tokener *tok = json_tokener_new();
    if(!tok)
    {
        CHIAKI_LOGE(session->log, "Couldn't create new json tokener");
        goto cleanup;
    }
    json_object *json = json_tokener_parse_ex(tok, response_data.data, response_data.size);
    if (json == NULL)
    {
        CHIAKI_LOGE(session->log, "get_websocket_fqdn: Parsing JSON failed");
        err = CHIAKI_ERR_UNKNOWN;
        goto cleanup_json_tokener;
    }
    json_object *fqdn_json;
    if (!json_object_object_get_ex(json, "fqdn", &fqdn_json))
    {
        CHIAKI_LOGE(session->log, "get_websocket_fqdn: JSON does not contain \"fqdn\" field");
        err = CHIAKI_ERR_UNKNOWN;
        goto cleanup_json;
    } else if (!json_object_is_type(fqdn_json, json_type_string))
    {
        CHIAKI_LOGE(session->log, "get_websocket_fqdn: JSON \"fqdn\" field is not a string");
        err = CHIAKI_ERR_UNKNOWN;
        goto cleanup_json;
    }
    *fqdn = strdup(json_object_get_string(fqdn_json));

cleanup_json:
    json_object_put(json);
cleanup_json_tokener:
    json_tokener_free(tok);
cleanup:
    curl_easy_cleanup(curl);
    free(response_data.data);
    return err;
}

/**
 * Write callback for CURL calls.
 *
 * @param[in] ptr Pointer to the data to write
 * @param[in] size Size of each element
 * @param[in] nmemb Number of elements
 * @param[in] userdata Pointer to a HttpResponseData struct
 * @return Number of bytes written
*/
static inline size_t curl_write_cb(
    void* ptr, size_t size, size_t nmemb, void* userdata)
{
    size_t realsize = size * nmemb;
    HttpResponseData* response_data = (HttpResponseData*) userdata;
    char* tmp = NULL;
    tmp = realloc(response_data->data, response_data->size + realsize + 1);
    if (tmp)
        response_data->data = tmp;
    else
    {
        free(response_data->data);
        // TODO: Use Chiaki logger!
        fprintf(stderr, "curl_write_cb: realloc failed\n");
        return 0;
    }
    memcpy(&(response_data->data[response_data->size]), ptr, realsize);
    response_data->size += realsize;
    response_data->data[response_data->size] = 0;

    return realsize;
}

static ChiakiErrorCode hex_to_bytes(const char* hex_str, uint8_t* bytes, size_t max_len) {
    size_t len = strlen(hex_str);
    if (len > max_len * 2) {
        len = max_len * 2;
    }
    for (size_t i = 0; i < len; i += 2) {
        if(sscanf(hex_str + i, "%2hhx", &bytes[i / 2]) != 1)
            return CHIAKI_ERR_INVALID_DATA;
    }
    return CHIAKI_ERR_SUCCESS;
}

static void bytes_to_hex(const uint8_t* bytes, size_t len, char* hex_str, size_t max_len) {
    if (len > max_len * 2) {
        len = max_len * 2;
    }
    for (size_t i = 0; i < len; i++) {
        snprintf(hex_str + i * 2, 3, "%02x", bytes[i]);
    }
}

/**
 * Generate a random UUIDv4
 *
 * @param[out] out Buffer to write the UUID to, must be at least 37 bytes long
 */
static void random_uuidv4(char* out)
{
    srand((unsigned int)time(NULL));
    const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            out[i] = '-';
        } else if (i == 14) {
            out[i] = '4';
        } else if (i == 19) {
            out[i] = hex[(rand() % 4) + 8];
        } else {
            out[i] = hex[rand() % 16];
        }
    }
    out[36] = '\0';
}

/**
 * Thread function to run that pings the console every 5 seconds and receives notifications over the websocket
 *
 * @param user Pointer to the session context
*/
static bool websocket_recovery_canceled(void *user)
{
    Session *session = user;
    chiaki_mutex_lock(&session->stop_mutex);
    bool stop = session->ws_thread_should_stop || session->main_should_stop;
    chiaki_mutex_unlock(&session->stop_mutex);
    return stop;
}

static int websocket_transfer_progress(void *user, curl_off_t total_down, curl_off_t down,
    curl_off_t total_up, curl_off_t up)
{
    (void)total_down; (void)down; (void)total_up; (void)up;
    return chiaki_aia_should_stop(user) ? 1 : 0;
}

static void* websocket_thread_func(void *user) {
    Session* session = (Session*) user;
    ChiakiAiaControl recovery_control = {
        .canceled = websocket_recovery_canceled,
        .user = session,
        .deadline_ms = chiaki_time_now_monotonic_ms() + SESSION_CREATION_TIMEOUT_SEC * 1000,
    };
    chiaki_thread_set_affinity(CHIAKI_THREAD_NAME_HOLEPUNCH);

    char ws_url[128] = {0};
    snprintf(ws_url, sizeof(ws_url), "wss://%s/np/pushNotification", session->ws_fqdn);

    CURL* curl = holepunch_curl_easy_init();
    if(!curl)
    {
        CHIAKI_LOGE(session->log, "Curl could not init");
        return NULL;
    }
    struct curl_slist *headers = NULL;
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, websocket_transfer_progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &recovery_control);
    recovery_control.connection = curl;
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)SESSION_CREATION_TIMEOUT_SEC);
    headers = curl_slist_append(headers, session->oauth_header);
    headers = curl_slist_append(headers, "Sec-WebSocket-Protocol: np-pushpacket");
    headers = curl_slist_append(headers, "User-Agent: WebSocket++/0.8.2");
    headers = curl_slist_append(headers, "X-PSN-APP-TYPE: REMOTE_PLAY");
    headers = curl_slist_append(headers, "X-PSN-APP-VER: RemotePlay/1.0");
    headers = curl_slist_append(headers, "X-PSN-KEEP-ALIVE-STATUS-TYPE: 3");
    headers = curl_slist_append(headers, "X-PSN-OS-VER: Windows/10.0");
    headers = curl_slist_append(headers, "X-PSN-PROTOCOL-VERSION: 2.1");
    headers = curl_slist_append(headers, "X-PSN-RECONNECTION: false");

    CURLcode res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "websocket_thread_func: CURL setopt CURLOPT_HTTPHEADER failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_SHARE, session->curl_share);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "websocket_thread_func: CURL setopt CURLOPT_SHAREfailed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "websocket_thread_func: CURL setopt CURLOPT_FAILONERROR failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "websocket_thread_func: CURL setopt CURLOPT_TIMEOUT failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_URL, ws_url);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "websocket_thread_func: CURL setopt CURLOPT_URL failed with CURL error %s", curl_easy_strerror(res));
    curl_easy_setopt(curl, CURLOPT_CERTINFO, 1L);
    res = curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "websocket_thread_func: CURL setopt CURLOPT_CONNECT_ONLY failed with CURL error %s", curl_easy_strerror(res));
    bool aia_attempted = false;
    bool aia_blob_supported =
        curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, NULL) == CURLE_OK;
    const curl_version_info_data *curl_version = curl_version_info(CURLVERSION_NOW);
    bool schannel_aia = false;
    if(curl_version && curl_version->ssl_version && strstr(curl_version->ssl_version, "Schannel"))
    {
#ifdef CHIAKI_CURL_SCHANNEL_AIA
        schannel_aia = true;
        CHIAKI_LOGI(session->log, "websocket_thread_func: Schannel AIA recovery uses untrusted issuers and native Windows roots");
#else
        // Schannel treats CAINFO_BLOB as an exclusive root store. Keep native
        // verification until recovered issuers can be supplied as untrusted data.
        aia_blob_supported = false;
        CHIAKI_LOGI(session->log, "websocket_thread_func: custom chain recovery disabled for Schannel to preserve Windows certificate trust");
#endif
    }
#ifdef CHIAKI_LIB_ENABLE_MBEDTLS
    // Certificate-chain recovery currently requires the OpenSSL implementation.
    aia_blob_supported = false;
#endif
    if(!aia_blob_supported)
        CHIAKI_LOGW(session->log, "websocket_thread_func: custom chain repair unavailable for this TLS backend");

ws_connect_attempt:
    if(chiaki_aia_should_stop(&recovery_control)) {
        res = CURLE_ABORTED_BY_CALLBACK;
        goto ws_after_recovery;
    }
    {
        size_t aia_blob_len = 0;
        char *aia_blob = NULL;
        if(aia_blob_supported && chiaki_aia_blob_len() > 0)
        {
            if(schannel_aia)
                aia_blob = chiaki_aia_blob_take(&aia_blob_len);
            else {
                const char *ca_path = NULL;
                CURLcode ca_info_res = CURLE_OK;
                if(holepunch_ca_info)
                    ca_path = holepunch_ca_info;
                else
                    ca_info_res = curl_easy_getinfo(curl, CURLINFO_CAINFO, &ca_path);
                FILE *ca_file = ca_path ? fopen(ca_path, "rb") : NULL;
                if(ca_info_res == CURLE_OK && (!ca_path || ca_file))
                    aia_blob = chiaki_aia_blob_with_ca(ca_file, &aia_blob_len);
                if(ca_file)
                    fclose(ca_file);
            }
            if(!aia_blob)
            {
                CHIAKI_LOGW(session->log, "websocket_thread_func: cannot preserve the configured CA bundle, skipping chain repair");
                aia_blob_supported = false;
            }
        }
        if(aia_blob)
        {
            struct curl_blob ca_blob;
            ca_blob.data = aia_blob;
            ca_blob.len = aia_blob_len;
            ca_blob.flags = CURL_BLOB_COPY;
            // Cached intermediates must not terminate verification before a root.
            long ssl_options = CURLSSLOPT_NO_PARTIALCHAIN;
#ifdef CHIAKI_CURL_SCHANNEL_AIA
            if(schannel_aia)
                ssl_options |= CURLSSLOPT_SCHANNEL_AIA;
#endif
            CURLcode blob_res = curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, ssl_options);
            if(blob_res == CURLE_OK)
                blob_res = curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &ca_blob);
            if(blob_res != CURLE_OK)
            {
                CHIAKI_LOGE(session->log, "websocket_thread_func: could not apply the repaired chain: %s",
                    curl_easy_strerror(blob_res));
                aia_blob_supported = false;
            }
            free(aia_blob);
        }
    }
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    CHIAKI_LOGI(session->log, "websocket_thread_func: connecting to %s with peer verification ENABLED (aia blob %zu bytes, gen %u)",
        session->ws_fqdn, chiaki_aia_blob_len(), chiaki_aia_blob_generation());

    res = curl_easy_perform(curl);

    if(res != CURLE_OK && res != CURLE_HTTP_RETURNED_ERROR && !aia_attempted && !aia_blob_supported)
    {
        aia_attempted = true;
        CHIAKI_LOGE(session->log, "websocket_thread_func: %s failed; custom certificate chain repair is disabled or unavailable, keeping the original TLS trust configuration",
            session->ws_fqdn);
    }
    else if(res != CURLE_OK && res != CURLE_HTTP_RETURNED_ERROR && !aia_attempted)
    {
        aia_attempted = true;

        long verify_result = 0;
        curl_easy_getinfo(curl, CURLINFO_SSL_VERIFYRESULT, &verify_result);
        bool certificate_failure =
            res == CURLE_PEER_FAILED_VERIFICATION ||
            res == CURLE_SSL_CERTPROBLEM ||
            res == CURLE_SSL_CACERT ||
            verify_result != 0;

        if(!certificate_failure)
        {
            CHIAKI_LOGE(session->log, "websocket_thread_func: %s failed with %s, not a certificate problem, not attempting chain recovery",
                session->ws_fqdn, curl_easy_strerror(res));
            goto ws_after_recovery;
        }

        uint8_t *leaf_der = NULL;
        size_t leaf_len = 0;
        if(!chiaki_aia_peek_leaf(session->ws_fqdn, &leaf_der, &leaf_len, session->log, &recovery_control))
            CHIAKI_LOGE(session->log, "websocket_thread_func: %s served no certificate to walk from", session->ws_fqdn);
        else
        {
            char *recovered = NULL;
            size_t recovered_len = 0;
            ChiakiErrorCode aia_err = chiaki_aia_recover(leaf_der, leaf_len,
                &recovered, &recovered_len, session->log, &recovery_control);
            free(leaf_der);

            if(aia_err == CHIAKI_ERR_SUCCESS && recovered)
            {
                bool cached = chiaki_aia_blob_add_pem(recovered, recovered_len);
                free(recovered);
                if(cached)
                    goto ws_connect_attempt;
            }
            else
                free(recovered);
        }

        CHIAKI_LOGE(session->log, "websocket_thread_func: chain recovery failed for %s, failing closed", session->ws_fqdn);
    }

ws_after_recovery:

    curl_slist_free_all(headers);
    if (res != CURLE_OK)
    {
        if (res == CURLE_HTTP_RETURNED_ERROR)
        {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            CHIAKI_LOGE(session->log, "websocket_thread_func: Connecting to push notification WebSocket %s failed with HTTP code %ld", ws_url, http_code);
        } else {
            CHIAKI_LOGE(session->log, "websocket_thread_func: Connecting to push notification WebSocket %s failed with CURL error %s", ws_url, curl_easy_strerror(res));
        }
        goto cleanup;
    }
    CHIAKI_LOGV(session->log, "websocket_thread_func: Connected to push notification WebSocket %s", ws_url);
    ChiakiErrorCode err = chiaki_mutex_lock(&session->state_mutex);
    assert(err == CHIAKI_ERR_SUCCESS);
    session->ws_open = true;
    session->state |= SESSION_STATE_WS_OPEN;
    log_session_state(session);
    err = chiaki_cond_signal(&session->state_cond);
    assert(err == CHIAKI_ERR_SUCCESS);
    err = chiaki_mutex_unlock(&session->state_mutex);
    assert(err == CHIAKI_ERR_SUCCESS);

    // Get the socket curl uses for the websocket so we can select() on it
    curl_socket_t sockfd;
    res = curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &sockfd);
    if(res != CURLE_OK) {
        CHIAKI_LOGE(session->log, "websocket_thread_func: Getting active socket failed with CURL error %s", curl_easy_strerror(res));
        goto cleanup;
    }
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sockfd, &fds);

    // Need to send a ping every 5secs
    uint64_t timeout = WEBSOCKET_PING_INTERVAL_SEC * 1000;
    uint64_t now = 0;
    uint64_t last_ping_sent = 0;

    json_tokener *tok = json_tokener_new();
    if(!tok)
    {
        CHIAKI_LOGE(session->log, "Couldn't create new json tokener");
        goto cleanup;
    }
    const struct curl_ws_frame *meta;
    char *buf = malloc(WEBSOCKET_MAX_FRAME_SIZE);
    if(!buf)
    {
        json_tokener_free(tok);
        goto cleanup;
    }
    size_t rlen;
    size_t wlen;
    bool expecting_pong = false;
    while (true)
    {
        chiaki_mutex_lock(&session->stop_mutex);
        bool should_stop = session->ws_thread_should_stop;
        chiaki_mutex_unlock(&session->stop_mutex);

        if (should_stop)
            break;

        now = chiaki_time_now_monotonic_us();

        if (expecting_pong && now - last_ping_sent > 5LL * SECOND_US)
        {
            CHIAKI_LOGE(session->log, "websocket_thread_func: Did not receive PONG in time.");
            goto cleanup_json;
        }

        if (now - last_ping_sent > 5LL * SECOND_US)
        {
            res = curl_ws_send(curl, buf, 0, &wlen, 0, CURLWS_PING);
            if (res != CURLE_OK)
            {
                CHIAKI_LOGE(session->log, "websocket_thread_func: Sending WebSocket PING failed with CURL error %s.", curl_easy_strerror(res));
                goto cleanup_json;
            }
            CHIAKI_LOGV(session->log, "websocket_thread_func: PING.");
            last_ping_sent = now;
            expecting_pong = true;
        }

        memset(buf, 0, WEBSOCKET_MAX_FRAME_SIZE);
        res = curl_ws_recv(curl, buf, WEBSOCKET_MAX_FRAME_SIZE, &rlen, &meta);
        if (res != CURLE_OK)
        {
            if (res == CURLE_AGAIN)
            {
                ChiakiErrorCode err = chiaki_stop_pipe_select_single(&session->select_pipe, sockfd, false, timeout);
                if(err == CHIAKI_ERR_CANCELED)
                {
                    CHIAKI_LOGE(session->log, "websocket_thread_func: Select canceled.");
                    goto cleanup_json;
                }
                if(err != CHIAKI_ERR_SUCCESS && err != CHIAKI_ERR_TIMEOUT)
                {
                    CHIAKI_LOGE(session->log, "websocket_thread_func: Select failed.");
                    goto cleanup_json;
                }
                else
                    continue;
            } else
            {
                CHIAKI_LOGE(session->log, "websocket_thread_func: Receiving WebSocket frame failed with CURL error %s", curl_easy_strerror(res));
                goto cleanup_json;
            }
        }

        CHIAKI_LOGV(session->log, "websocket_thread_func: Received WebSocket frame of length %zu with flags %d", rlen, meta->flags);
        if (meta->flags & CURLWS_PONG)
        {
            CHIAKI_LOGV(session->log, "websocket_thread_func: Received PONG.");
            expecting_pong = false;
        }
        if (meta->flags & CURLWS_PING)
        {
            CHIAKI_LOGV(session->log, "websocket_thread_func: Received PING.");
            res = curl_ws_send(curl, buf, rlen, &wlen, 0, CURLWS_PONG);
            if (res != CURLE_OK)
            {
                CHIAKI_LOGE(session->log, "websocket_thread_func: Sending WebSocket PONG failed with CURL error %s", curl_easy_strerror(res));
                goto cleanup_json;
            }
            CHIAKI_LOGV(session->log, "websocket_thread_func: Sent PONG.");
        }
        if (meta->flags & CURLWS_CLOSE)
        {
            CHIAKI_LOGE(session->log, "websocket_thread_func: WebSocket closed");
            goto cleanup_json;
        }
        if (meta->flags & CURLWS_TEXT || meta->flags & CURLWS_BINARY)
        {
            CHIAKI_LOGV(session->log, "websocket_thread_func: Received WebSocket frame with %zu bytes of payload.", rlen);
            json_object *json = json_tokener_parse_ex(tok, buf, rlen);
            if (json == NULL)
            {
                CHIAKI_LOGE(session->log, "websocket_thread_func: Parsing JSON from payload failed");
                CHIAKI_LOGV(session->log, "websocket_thread_func: Payload was:\n%s", buf);
                continue;
            }
            CHIAKI_LOGV(session->log, json_object_to_json_string_ext(json, JSON_C_TO_STRING_PRETTY));

            NotificationType type = parse_notification_type(session->log, json);
            char *json_buf = malloc(rlen);
            if(!json_buf)
                goto cleanup_json;
            memcpy(json_buf, buf, rlen);
            size_t json_buf_size = rlen;
            CHIAKI_LOGV(session->log, "Received notification of type %d", type);
            Notification *notif = newNotification(type, json, json_buf, json_buf_size);
            if(!notif)
                goto cleanup_json;

            // Automatically ACK OFFER session messages if we're not currently explicitly
            // waiting on offers
            chiaki_mutex_lock(&session->state_mutex);
            bool should_ack_offers =
                // We're not expecting any offers after receiving one for the control port and before it's established, afterwards we expect
                // one for the data port, so we don't auto-ACK in between
                (session->state & SESSION_STATE_CTRL_OFFER_RECEIVED
                 && !(session->state & SESSION_STATE_CTRL_ESTABLISHED))
                 // At this point all offers were received and we don't care for new ones anymore
                || session->state & SESSION_STATE_DATA_OFFER_RECEIVED;
            chiaki_mutex_unlock(&session->state_mutex);
            if (should_ack_offers && notif->type == NOTIFICATION_TYPE_SESSION_MESSAGE_CREATED)
            {
                SessionMessage *msg = NULL;
                json_object *payload = session_message_get_payload(session->log, json);
                err = session_message_parse(session->log, payload, &msg);
                json_object_put(payload);
                if (err != CHIAKI_ERR_SUCCESS)
                {
                    CHIAKI_LOGE(session->log, "websocket_thread_func: Failed to parse session message for ACKing.");
                    continue;
                }
                if (msg->action == SESSION_MESSAGE_ACTION_OFFER)
                {
                    SessionMessage ack_msg = {
                        .action = SESSION_MESSAGE_ACTION_RESULT,
                        .req_id = msg->req_id,
                        .error = 0,
                        .conn_request = NULL,
                        .notification = NULL,
                    };
                    http_send_session_message(session, &ack_msg, true);
                }
                session_message_free(msg);
            }
            bool session_deleted = false;
            if (notif->type == NOTIFICATION_TYPE_SESSION_DELETED)
            {
                const char *deleted_id = NULL;
                json_object *body_json = NULL, *data_json = NULL, *sid_json = NULL;
                if(notif->json
                    && json_object_object_get_ex(notif->json, "body", &body_json)
                    && json_object_object_get_ex(body_json, "data", &data_json)
                    && json_object_object_get_ex(data_json, "sessionId", &sid_json)
                    && json_object_is_type(sid_json, json_type_string))
                {
                    deleted_id = json_object_get_string(sid_json);
                }

                if(deleted_id && session->session_id[0]
                    && strcmp(deleted_id, session->session_id) != 0)
                {
                    CHIAKI_LOGI(session->log, "websocket_thread_func: ignoring deletion of unrelated session %s (ours is %s)",
                        deleted_id, session->session_id);
                }
                else
                    session_deleted = true;
            }
            // Publishing transfers ownership to consumers, which may free notif immediately.
            ChiakiErrorCode mutex_err = chiaki_mutex_lock(&session->notif_mutex);
            assert(mutex_err == CHIAKI_ERR_SUCCESS);
            enqueueNq(session->ws_notification_queue, notif);
            chiaki_cond_signal(&session->notif_cond);
            chiaki_mutex_unlock(&session->notif_mutex);
            if (session_deleted)
            {
                CHIAKI_LOGI(session->log, "websocket_thread_func: Holepunch session was deleted on PSN server, exiting....");
                goto cleanup_json;
            }
        }
    }

cleanup_json:
    json_tokener_free(tok);
    free(buf);
cleanup:
    curl_easy_cleanup(curl);
    chiaki_mutex_lock(&session->state_mutex);
    session->ws_open = false;
    if(!(session->state & SESSION_STATE_WS_OPEN))
    {
        session->state |= SESSION_STATE_WS_FAILED;
        chiaki_cond_signal(&session->state_cond);
    }
    chiaki_mutex_unlock(&session->state_mutex);

    return NULL;
}

static NotificationType parse_notification_type(
    ChiakiLog *log, json_object* json
) {
    json_object* datatype;
    if (!json_object_object_get_ex(json, "dataType", &datatype))
    {
        CHIAKI_LOGE(log, "parse_notification_type: JSON does not contain \"datatype\" field\n");
        return NOTIFICATION_TYPE_UNKNOWN;
    } else if (!json_object_is_type(datatype, json_type_string))
    {
        CHIAKI_LOGE(log, "parse_notification_type: JSON \"datatype\" field is not a string\n");
        return NOTIFICATION_TYPE_UNKNOWN;
    }
    const char* datatype_str = json_object_get_string(datatype);

    if (strcmp(datatype_str, "psn:sessionManager:sys:remotePlaySession:created") == 0)
    {
        return NOTIFICATION_TYPE_SESSION_CREATED;
    } else if (strcmp(datatype_str, "psn:sessionManager:sys:rps:members:created") == 0)
    {
        return NOTIFICATION_TYPE_MEMBER_CREATED;
    } else if (strcmp(datatype_str, "psn:sessionManager:sys:rps:customData1:updated") == 0)
    {
        return NOTIFICATION_TYPE_CUSTOM_DATA1_UPDATED;
    } else if (strcmp(datatype_str, "psn:sessionManager:sys:rps:sessionMessage:created") == 0)
    {
        return NOTIFICATION_TYPE_SESSION_MESSAGE_CREATED;
    } else if (strcmp(datatype_str, "psn:sessionManager:sys:rps:members:deleted") == 0)
    {
        return NOTIFICATION_TYPE_MEMBER_DELETED;
    } else if (strcmp(datatype_str, "psn:sessionManager:sys:remotePlaySession:deleted") == 0)
    {
        return NOTIFICATION_TYPE_SESSION_DELETED;
    }else
    {
        CHIAKI_LOGW(log, "parse_notification_type: Unknown notification type \"%s\"", datatype_str);
        CHIAKI_LOGV(log, "parse_notification_type: JSON was:\n%s", json_object_to_json_string_ext(json, JSON_C_TO_STRING_PRETTY));
        return NOTIFICATION_TYPE_UNKNOWN;
    }
}


CHIAKI_EXPORT ChiakiErrorCode holepunch_session_create_offer(Session *session)
{
    if(session->our_offer_msg)
    {
        CHIAKI_LOGW(session->log, "Overwriting previously unsent offer message. Make sure you're punching the control hole before the data hole!");
        session_message_free(session->our_offer_msg);
        session->our_offer_msg = NULL;
    }
    if(session->local_candidates)
    {
        CHIAKI_LOGW(session->log, "Overwriting previously unused message. Make sure you're punching the control hole before the data hole!");
        free(session->local_candidates);
        session->local_candidates = NULL;
    }
    // Create socket with available local port for connection
    session->ipv4_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (CHIAKI_SOCKET_IS_INVALID(session->ipv4_sock))
    {
        CHIAKI_LOGE(session->log, "holepunch_session_create_offer: Creating ipv4 socket failed");
        return CHIAKI_ERR_UNKNOWN;
    }
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = INADDR_ANY;
    client_addr.sin_port = 0;
    socklen_t client_addr_len = sizeof(client_addr);
    const int enable = 1;
    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
#if defined(SO_REUSEPORT)
        if (setsockopt(session->ipv4_sock, SOL_SOCKET, SO_REUSEPORT, (const void *)&enable, sizeof(int)) < 0)
        {
            CHIAKI_LOGE(session->log, "holepunch_session_create_offer: setsockopt(SO_REUSEPORT) for ipv4 socket failed with error " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
            return CHIAKI_ERR_UNKNOWN;
        }
#else
        if (setsockopt(session->ipv4_sock, SOL_SOCKET, SO_REUSEADDR, (const void *)&enable, sizeof(int)) < 0)
        {
            CHIAKI_LOGE(session->log, "holepunch_session_create_offer: setsockopt(SO_REUSEADDR) for ipv4 socket failed with error" CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
            err = CHIAKI_ERR_UNKNOWN;
            goto cleanup_socket;
        }
#endif
    if (bind(session->ipv4_sock, (struct sockaddr*)&client_addr, client_addr_len) < 0)
    {
        CHIAKI_LOGE(session->log, "holepunch_session_create_offer: Binding ipv4 socket failed with error " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
        if(!CHIAKI_SOCKET_IS_INVALID(session->ipv4_sock))
        {
            CHIAKI_SOCKET_CLOSE(session->ipv4_sock);
            session->ipv4_sock = CHIAKI_INVALID_SOCKET;
        }
        err = CHIAKI_ERR_UNKNOWN;
        goto cleanup_socket;
    }
    if(getsockname(session->ipv4_sock, (struct sockaddr*)&client_addr, &client_addr_len) < 0)
    {
        CHIAKI_LOGE(session->log, "holepunch_session_create_offer: Getting ipv4 socket name failed with error " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
        if(!CHIAKI_SOCKET_IS_INVALID(session->ipv4_sock))
        {
            CHIAKI_SOCKET_CLOSE(session->ipv4_sock);
            session->ipv4_sock = CHIAKI_INVALID_SOCKET;
        }
        err = CHIAKI_ERR_UNKNOWN;
        goto cleanup_socket;
    }

    uint16_t local_port = ntohs(client_addr.sin_port);
#ifndef __SWITCH__
    // Switch doesn't support IPv6 - skip IPv6 socket creation
    session->ipv6_sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (CHIAKI_SOCKET_IS_INVALID(session->ipv6_sock))
    {
        CHIAKI_LOGE(session->log, "holepunch_session_create_offer: Creating ipv6 socket failed with error " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
        err = CHIAKI_ERR_UNKNOWN;
        goto cleanup_socket;
    }
    struct sockaddr_in6 client_addr_ipv6;
    memset(&client_addr_ipv6, 0, sizeof(client_addr_ipv6));
    client_addr_ipv6.sin6_family = AF_INET6;
    client_addr_ipv6.sin6_addr = in6addr_any;
    client_addr_ipv6.sin6_port = htons(local_port);
    socklen_t client_addr_ipv6_len = sizeof(client_addr_ipv6);
#if defined(SO_REUSEPORT)
    if (setsockopt(session->ipv6_sock, SOL_SOCKET, SO_REUSEPORT, (const void *)&enable, sizeof(int)) < 0)
    {
        CHIAKI_LOGE(session->log, "holepunch_session_create_offer: setsockopt(SO_REUSEPORT) for ipv6 socket failed with error " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
        err = CHIAKI_ERR_UNKNOWN;
        goto cleanup_socket;
    }
#else
    if (setsockopt(session->ipv6_sock, SOL_SOCKET, SO_REUSEADDR, (const void *)&enable, sizeof(int)) < 0)
    {
        CHIAKI_LOGE(session->log, "holepunch_session_create_offer: setsockopt(SO_REUSEADDR) for ipv6 socket failed with error " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
        err = CHIAKI_ERR_UNKNOWN;
        goto cleanup_socket;
    }
#endif
    if (bind(session->ipv6_sock, (struct sockaddr*)&client_addr_ipv6, client_addr_ipv6_len) < 0)
    {
        CHIAKI_LOGE(session->log, "holepunch_session_create_offer: Binding ipv6 socket failed with error " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
        if(!CHIAKI_SOCKET_IS_INVALID(session->ipv6_sock))
        {
            CHIAKI_SOCKET_CLOSE(session->ipv6_sock);
            session->ipv6_sock = CHIAKI_INVALID_SOCKET;
        }
        err = CHIAKI_ERR_UNKNOWN;
        goto cleanup_socket;
    }
#endif // __SWITCH__

    size_t our_offer_msg_req_id = session->local_req_id;
    session->local_req_id++;
    SessionMessage msg = {
        .action = SESSION_MESSAGE_ACTION_OFFER,
        .req_id = our_offer_msg_req_id,
        .error = 0,
        .conn_request = malloc(sizeof(ConnectionRequest)),
        .notification = NULL,
    };
    if(!msg.conn_request)
    {
        err = CHIAKI_ERR_MEMORY;
        goto cleanup_socket;
    }
    if(session->local_port_ctrl == 0)
        session->local_port_ctrl = local_port;
    else
        session->local_port_data = local_port;

    msg.conn_request->sid = session->sid_local;
    msg.conn_request->peer_sid = session->sid_console;
    msg.conn_request->nat_type = 2;
    memset(msg.conn_request->skey, 0, sizeof(msg.conn_request->skey));
    memset(msg.conn_request->default_route_mac_addr, 0, sizeof(msg.conn_request->default_route_mac_addr));
    memcpy(msg.conn_request->local_hashed_id, session->hashed_id_local, sizeof(session->hashed_id_local));
    msg.conn_request->num_candidates = 2;
    // allocate extra space for stun candidates
    msg.conn_request->candidates = calloc(4, sizeof(Candidate));
    if(!msg.conn_request->candidates)
    {
        free(msg.conn_request);
        if(!CHIAKI_SOCKET_IS_INVALID(session->ipv4_sock))
        {
            CHIAKI_SOCKET_CLOSE(session->ipv4_sock);
            session->ipv4_sock = CHIAKI_INVALID_SOCKET;
        }
        if(!CHIAKI_SOCKET_IS_INVALID(session->ipv6_sock))
        {
            CHIAKI_SOCKET_CLOSE(session->ipv6_sock);
            session->ipv6_sock = CHIAKI_INVALID_SOCKET;
        }
        return CHIAKI_ERR_MEMORY;
    }

    Candidate *candidate_local = &msg.conn_request->candidates[2];
    candidate_local->type = CANDIDATE_TYPE_LOCAL;
    memcpy(candidate_local->addr_mapped, "0.0.0.0", 8);
    candidate_local->port = local_port;
    candidate_local->port_mapped = 0;

    bool have_addr = false;
    Candidate *candidate_remote = &msg.conn_request->candidates[1];
    candidate_remote->type = CANDIDATE_TYPE_STATIC;
    switch(session->gw_status)
    {
        case GATEWAY_STATUS_UNKNOWN:
        case GATEWAY_STATUS_NOT_FOUND: {
            get_client_addr_local(session, candidate_local, candidate_local->addr, sizeof(candidate_local->addr));
            break;
        }
        case GATEWAY_STATUS_FOUND: {
            memcpy(candidate_local->addr, session->gw.lan_ip, sizeof(session->gw.lan_ip));
            if(upnp_add_udp_port_mapping(session->log, &session->gw, local_port, local_port))
            {
                CHIAKI_LOGI(session->log, "holepunch_session_create_offer: Added local UPNP port mapping to port %u", local_port);
                have_addr = get_client_addr_remote_upnp(session->log, &session->gw, candidate_remote->addr);
            }
            else
            {
                CHIAKI_LOGE(session->log, "holepunch_session_create_offer: Adding upnp port mapping failed");
            }
            break;
        }
    }
    memcpy(session->client_local_ip, candidate_local->addr, sizeof(candidate_local->addr));
    if (!have_addr)
    {
        // Move current candidates behind STUN candidates so when the console reaches out to our STUN candidate it will be using the correct port if behind symmetric NAT
        Candidate *candidate_stun = &msg.conn_request->candidates[0];
        candidate_stun->type = CANDIDATE_TYPE_STUN;
        memcpy(candidate_stun->addr_mapped, "0.0.0.0", 8);
        candidate_stun->port_mapped = 0;
        have_addr = get_client_addr_remote_stun(session, candidate_stun->addr, &candidate_stun->port, &session->ipv4_sock, true);
        if(have_addr)
        {
            memcpy(candidate_remote->addr, candidate_stun->addr, sizeof(candidate_stun->addr));
            // Local port is used externally so don't make duplicate STUN candidate since STATIC candidate will have same ip and port number
            uint16_t stun_port = candidate_stun->port;
            if(session->stun_allocation_increment != 0)
            {
                Candidate original_candidates[3];
                memcpy(original_candidates, msg.conn_request->candidates, sizeof(Candidate) * 3);
                candidate_stun = &original_candidates[0];
                if(!session->stun_random_allocation)
                {
                    Candidate *tmp = NULL;
                    tmp = realloc(msg.conn_request->candidates, sizeof(Candidate) * 11);
                    if(tmp)
                        msg.conn_request->candidates = tmp;
                    else
                    {
                        err = CHIAKI_ERR_MEMORY;
                        goto cleanup;
                    }
                    int32_t port_check = candidate_stun->port;
                    // Setup extra stun candidate in case there was an allocation in between the stun request and our allocation
                    for(int i=0; i<8; i++)
                    {
                        Candidate *candidate_stun2 = &msg.conn_request->candidates[i];
                        candidate_stun2->type = CANDIDATE_TYPE_STUN;
                        memcpy(candidate_stun2->addr_mapped, "0.0.0.0", 8);
                        candidate_stun2->port_mapped = 0;
                        memcpy(candidate_stun2->addr, candidate_stun->addr, sizeof(candidate_stun->addr));
                        int32_t tmp = port_check;
                        // skip well known ports 0-1024 unless current allocation is within that range since most routers don't use these (it it is in range implies router uses those ports)
                        port_check += session->stun_allocation_increment;
                        if(port_check < 1024 && tmp > 1024)
                            port_check = UINT16_MAX - (1024 - port_check);
                        else if(port_check < 1)
                            port_check += UINT16_MAX;
                        else if(port_check > UINT16_MAX)
                            port_check = port_check - UINT16_MAX + 1024;
                        candidate_stun2->port = port_check;
                    }
                    memcpy(&msg.conn_request->candidates[8], &original_candidates[1], sizeof(Candidate));
                    memcpy(&msg.conn_request->candidates[9], &original_candidates[2], sizeof(Candidate));
                    candidate_remote = &msg.conn_request->candidates[8];
                    candidate_local = &msg.conn_request->candidates[9];
                    msg.conn_request->num_candidates = 10;
                }
                else
                {
                    Candidate *tmp = NULL;
                    tmp = realloc(msg.conn_request->candidates, sizeof(Candidate) * (session->port_guessing_count + 3));
                    if(tmp)
                        msg.conn_request->candidates = tmp;
                    else
                    {
                        err = CHIAKI_ERR_MEMORY;
                        goto cleanup;
                    }
                    int guess_count = session->port_guessing_count;
                    // Setup session->port_guessing_count STUN candidates because we have a random allocation and usually 64 port blocks are minimum
                    CHIAKI_LOGI(session->log, "Initiating random allocation guesses with %d guesses", guess_count);
                    int32_t base_port = candidate_stun->port;
                    for(int i=0; i<guess_count; i++)
                    {
                        int delta;
                        if(i == 0)
                            delta = 0;
                        else if(i % 2 == 1)
                            delta = (i + 1) / 2;
                        else
                            delta = -(i / 2);
                        int32_t port = base_port + delta;
                        if(port > UINT16_MAX)
                            port = 49152 + (port - UINT16_MAX - 1);
                        else if(port < 1024)
                            port = UINT16_MAX - (1024 - port);
                        Candidate *candidate_stun2 = &msg.conn_request->candidates[i];
                        candidate_stun2->type = CANDIDATE_TYPE_STUN;
                        memcpy(candidate_stun2->addr_mapped, "0.0.0.0", 8);
                        candidate_stun2->port_mapped = 0;
                        candidate_stun2->port = port;
                        memcpy(candidate_stun2->addr, candidate_stun->addr, sizeof(candidate_stun->addr));
                    }
                    memcpy(&msg.conn_request->candidates[session->port_guessing_count], &original_candidates[1], sizeof(Candidate));
                    memcpy(&msg.conn_request->candidates[session->port_guessing_count + 1], &original_candidates[2], sizeof(Candidate));
                    candidate_remote = &msg.conn_request->candidates[session->port_guessing_count];
                    candidate_local = &msg.conn_request->candidates[session->port_guessing_count + 1];
                    msg.conn_request->num_candidates = session->port_guessing_count + 2;
                }
            }
            else
            {
                if(local_port == stun_port)
                {
                    memcpy(&msg.conn_request->candidates[0], &msg.conn_request->candidates[1], sizeof(Candidate));
                    memcpy(&msg.conn_request->candidates[1], &msg.conn_request->candidates[2], sizeof(Candidate));
                    candidate_remote = &msg.conn_request->candidates[0];
                    candidate_local = &msg.conn_request->candidates[1];
                }
                else if(session->force_port_guessing)
                {
                    // Possible double NAT or port-rewriting cone NAT:
                    // local_port != stun_port but increment is 0 (EIM on outermost NAT).
                    // Since we can't predict the actual external port mapping, force
                    // the random allocation path: send sequential port guesses as STUN
                    // candidates for the console to try, and open many sockets in
                    // check_candidates so the NAT assigns us many external ports,
                    // maximizing the chance of overlap with the console's connection attempts.
                    int guess_count = session->port_guessing_count;
                    CHIAKI_LOGI(session->log, "holepunch_session_create_offer: Port rewriting NAT detected (local_port %u != stun_port %u with increment 0), forcing random allocation with %d guesses", local_port, stun_port, guess_count);
                    session->stun_random_allocation = true;
                    session->stun_allocation_increment = 1;
                    Candidate original_candidates[3];
                    memcpy(original_candidates, msg.conn_request->candidates, sizeof(Candidate) * 3);
                    candidate_stun = &original_candidates[0];
                    Candidate *tmp = NULL;
                    tmp = realloc(msg.conn_request->candidates, sizeof(Candidate) * (guess_count + 3));
                    if(tmp)
                        msg.conn_request->candidates = tmp;
                    else
                    {
                        err = CHIAKI_ERR_MEMORY;
                        goto cleanup;
                    }
                    int32_t base_port = candidate_stun->port;
                    for(int i=0; i<guess_count; i++)
                    {
                        int delta;
                        if(i == 0)
                            delta = 0;
                        else if(i % 2 == 1)
                            delta = (i + 1) / 2;
                        else
                            delta = -(i / 2);
                        int32_t port = base_port + delta;
                        if(port > UINT16_MAX)
                            port = 49152 + (port - UINT16_MAX - 1);
                        else if(port < 1024)
                            port = UINT16_MAX - (1024 - port);
                        Candidate *candidate_stun2 = &msg.conn_request->candidates[i];
                        candidate_stun2->type = CANDIDATE_TYPE_STUN;
                        memcpy(candidate_stun2->addr_mapped, "0.0.0.0", 8);
                        candidate_stun2->port_mapped = 0;
                        candidate_stun2->port = port;
                        memcpy(candidate_stun2->addr, candidate_stun->addr, sizeof(candidate_stun->addr));
                    }
                    memcpy(&msg.conn_request->candidates[guess_count], &original_candidates[1], sizeof(Candidate));
                    memcpy(&msg.conn_request->candidates[guess_count + 1], &original_candidates[2], sizeof(Candidate));
                    candidate_remote = &msg.conn_request->candidates[guess_count];
                    candidate_local = &msg.conn_request->candidates[guess_count + 1];
                    msg.conn_request->num_candidates = guess_count + 2;
                }
                else
                {
                    msg.conn_request->num_candidates = 3;
                    candidate_remote = &msg.conn_request->candidates[1];
                    candidate_local = &msg.conn_request->candidates[2];
                }
            }
        }
        else
            CHIAKI_LOGE(session->log, "holepunch_session_create_offer: Could not get remote address from STUN");
        if(CHIAKI_SOCKET_IS_INVALID(session->ipv4_sock))
        {
            CHIAKI_LOGE(session->log, "holepunch_session_create_offer: STUN caused socket to close due to error");
            err = CHIAKI_ERR_UNKNOWN;
            goto cleanup;
        }
#ifdef _WIN32
        int timeout = 0;
#else
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;
#endif
        if (setsockopt(session->ipv4_sock, SOL_SOCKET, SO_RCVTIMEO, (const CHIAKI_SOCKET_BUF_TYPE)&timeout, sizeof(timeout)) < 0)
        {
            CHIAKI_LOGE(session->log, "holepunch_session_create_offer: Failed to unset socket timeout, error was " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
            err = CHIAKI_ERR_UNKNOWN;
            goto cleanup;
        }
        // Only PS5 supports ipv6
        if(session->console_type == CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5 && ENABLE_IPV6)
        {
            Candidate *candidate_stun_ipv6 = &msg.conn_request->candidates[msg.conn_request->num_candidates];
            candidate_stun_ipv6->type = CANDIDATE_TYPE_STUN;
            memcpy(candidate_stun_ipv6->addr_mapped, "0.0.0.0", 8);
            candidate_stun_ipv6->port_mapped = 0;
            if(get_client_addr_remote_stun(session, candidate_stun_ipv6->addr, &candidate_stun_ipv6->port, &session->ipv6_sock, false))
            {
                if (setsockopt(session->ipv6_sock, SOL_SOCKET, SO_RCVTIMEO, (const CHIAKI_SOCKET_BUF_TYPE)&timeout, sizeof(timeout)) < 0)
                {
                    CHIAKI_LOGE(session->log, "holepunch_session_create_offer: Failed to unset socket timeout, error was " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
                    if(!CHIAKI_SOCKET_IS_INVALID(session->ipv6_sock))
                    {
                        CHIAKI_SOCKET_CLOSE(session->ipv6_sock);
                        session->ipv6_sock = CHIAKI_INVALID_SOCKET;
                    }
                }
                else
                    msg.conn_request->num_candidates++;
            }
            else
            {
                CHIAKI_LOGW(session->log, "holepunch_session_create_offer: IPV6 NOT supported by device. Couldn't get IPV6 STUN address.");
                if(!CHIAKI_SOCKET_IS_INVALID(session->ipv6_sock))
                {
                    CHIAKI_SOCKET_CLOSE(session->ipv6_sock);
                    session->ipv6_sock = CHIAKI_INVALID_SOCKET;
                }
            }
        }
        else
        {
            CHIAKI_LOGI(session->log, "holepunch_session_create_offer: IPV6 NOT supported by your PlayStation console. Skipping IPV6 connection");
            if(!CHIAKI_SOCKET_IS_INVALID(session->ipv6_sock))
            {
                CHIAKI_SOCKET_CLOSE(session->ipv6_sock);
                session->ipv6_sock = CHIAKI_INVALID_SOCKET;
            }
        }
    }
    else
    {
        // If no STUN address the static and local candidates are our first candidates
        memcpy(&msg.conn_request->candidates[0], &msg.conn_request->candidates[1], sizeof(Candidate));
        memcpy(&msg.conn_request->candidates[1], &msg.conn_request->candidates[2], sizeof(Candidate));
        candidate_remote = &msg.conn_request->candidates[0];
        candidate_local = &msg.conn_request->candidates[1];
    }
    if (!have_addr) {
        CHIAKI_LOGE(session->log, "holepunch_session_create_offer: Could not get remote address");
        err = CHIAKI_ERR_UNKNOWN;
        goto cleanup;
    }
    err = chiaki_socket_set_nonblock(session->ipv4_sock, true);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(session->log, "holepunch_session_create_offer: Failed to set ipv4 socket to non-blocking: %s", chiaki_error_string(err));
        err = CHIAKI_ERR_UNKNOWN;
        goto cleanup;
    }
    if(!CHIAKI_SOCKET_IS_INVALID(session->ipv6_sock))
    {
        err = chiaki_socket_set_nonblock(session->ipv6_sock, true);
        if(err != CHIAKI_ERR_SUCCESS)
        {
            CHIAKI_LOGE(session->log, "holepunch_session_create_offer: Failed to set ipv6 socket to non-blocking: %s", chiaki_error_string(err));
            err = CHIAKI_ERR_UNKNOWN;
            goto cleanup;
        }
    }
    memcpy(candidate_remote->addr_mapped, "0.0.0.0", 8);
    candidate_remote->port = local_port;
    candidate_remote->port_mapped = 0;
    session->local_candidates = calloc(2, sizeof(Candidate));
    if(!session->local_candidates)
    {
        err = CHIAKI_ERR_MEMORY;
        goto cleanup;
    }
    memcpy(&session->local_candidates[0], candidate_local, sizeof(Candidate));
    // either STUN candidate if it exists, else STATIC candidate
    memcpy(&session->local_candidates[1], &msg.conn_request->candidates[0], sizeof(Candidate));

cleanup:
    if(err == CHIAKI_ERR_SUCCESS)
    {
        session->our_offer_msg = malloc(sizeof(SessionMessage));
        if(!session->our_offer_msg)
            err = CHIAKI_ERR_MEMORY;
        else
            memcpy(session->our_offer_msg, &msg, sizeof(SessionMessage));
    }
    else
    {
        free(msg.conn_request->candidates);
        free(msg.conn_request);
    }
cleanup_socket:
    if(err != CHIAKI_ERR_SUCCESS)
    {
        if(!CHIAKI_SOCKET_IS_INVALID(session->ipv4_sock))
        {
            CHIAKI_SOCKET_CLOSE(session->ipv4_sock);
            session->ipv4_sock = CHIAKI_INVALID_SOCKET;
        }
        if(!CHIAKI_SOCKET_IS_INVALID(session->ipv6_sock))
        {
            CHIAKI_SOCKET_CLOSE(session->ipv6_sock);
            session->ipv6_sock = CHIAKI_INVALID_SOCKET;
        }
    }
    return err;
}

static ChiakiErrorCode send_offer(Session *session)
{
    print_session_request(session->log, session->our_offer_msg->conn_request);
    ChiakiErrorCode err = http_send_session_message(session, session->our_offer_msg, false);
    if (err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(session->log, "send_offer: Sending session message failed");
    }
    return err;
}

static ChiakiErrorCode send_accept(Session *session, int req_id, Candidate *selected_candidate)
{
    SessionMessage msg = {
        .action = SESSION_MESSAGE_ACTION_ACCEPT,
        .req_id = req_id,
        .error = 0,
        .conn_request = calloc(1, sizeof(ConnectionRequest)),
        .notification = NULL,
    };
    if(!msg.conn_request)
        return CHIAKI_ERR_MEMORY;
    msg.conn_request->sid = session->sid_local;
    msg.conn_request->peer_sid = session->sid_console;
    msg.conn_request->nat_type = 0;
    memset(msg.conn_request->skey, 0, sizeof(msg.conn_request->skey));
    memset(msg.conn_request->default_route_mac_addr, 0, sizeof(msg.conn_request->default_route_mac_addr));
    memset(msg.conn_request->local_hashed_id, 0, sizeof(msg.conn_request->local_hashed_id));
    msg.conn_request->num_candidates = 1;
    msg.conn_request->candidates = calloc(1, sizeof(Candidate));
    if(!msg.conn_request->candidates)
    {
        free(msg.conn_request);
        return CHIAKI_ERR_MEMORY;
    }
    memcpy(&msg.conn_request->candidates[0], selected_candidate, sizeof(Candidate));

    ChiakiErrorCode err = http_send_session_message(session, &msg, false);
    free(msg.conn_request->candidates);
    free(msg.conn_request);
    return err;
}

/**
 * Creates a session on the PSN server.
 *
 * @param session The Session instance.
 * @return CHIAKI_ERR_SUCCESS on success, or an error code on failure.
*/
static ChiakiErrorCode http_create_session(Session *session)
{
    size_t session_create_json_len = sizeof(session_create_json_fmt) + strlen(session->pushctx_id);
    char* session_create_json = malloc(session_create_json_len);
    if(!session_create_json)
        return CHIAKI_ERR_MEMORY;
    snprintf(session_create_json, session_create_json_len, session_create_json_fmt, session->pushctx_id);
    CHIAKI_LOGV(session->log, "http_create_session: Sending JSON:\n%s", session_create_json);

    HttpResponseData response_data = {
        .data = malloc(0),
        .size = 0,
    };

    CURL* curl = holepunch_curl_easy_init();
    if(!curl)
    {
        free(response_data.data);
        free(session_create_json);
        CHIAKI_LOGE(session->log, "Curl could not init");
        return CHIAKI_ERR_MEMORY;
    }
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, session->oauth_header);
    headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");

    CURLcode res = curl_easy_setopt(curl, CURLOPT_SHARE, session->curl_share);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_create_session: CURL setopt CURLOPT_SHARE failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_create_session: CURL setopt CURLOPT_FAILONERROR failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_URL, session_create_url);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_create_session: CURL setopt CURLOPT_URL failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_create_session: CURL setopt CURLOPT_TIMEOUT failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_create_session: CURL setopt CURLOPT_HTTPHEADER failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, session_create_json);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_create_session: CURL setopt CURLOPT_POSTFIELDS failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_create_session: CURL setopt CURLOPT_WRITEFUNCTION failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_data);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_create_session: CURL setopt CURLOPT_WRITEDATA failed with CURL error %s", curl_easy_strerror(res));

    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
    res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    if (res != CURLE_OK)
    {
        if (res == CURLE_HTTP_RETURNED_ERROR)
        {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            CHIAKI_LOGE(session->log, "http_create_session: Creating holepunch session failed with HTTP code %ld", http_code);
            err = CHIAKI_ERR_HTTP_NONOK;
        } else {
            CHIAKI_LOGE(session->log, "http_create_session: Creating holepunch session failed with CURL error %s", curl_easy_strerror(res));
            err = CHIAKI_ERR_NETWORK;
        }
        goto cleanup;
    }

    json_tokener *tok = json_tokener_new();
    if(!tok)
    {
        CHIAKI_LOGE(session->log, "Couldn't create new json tokener");
        goto cleanup;
    }
    CHIAKI_LOGV(session->log, "http_create_session: Received JSON:\n%s", response_data.data);
    json_object *json = json_tokener_parse_ex(tok, response_data.data, response_data.size);
    if (json == NULL)
    {
        CHIAKI_LOGE(session->log, "http_create_session: Parsing JSON failed");
        err = CHIAKI_ERR_UNKNOWN;
        goto cleanup_json_tokener;
    }

    json_object* session_id_json;
    json_pointer_get(json, "/remotePlaySessions/0/sessionId", &session_id_json);
    json_object* account_id_json;
    json_pointer_get(json, "/remotePlaySessions/0/members/0/accountId", &account_id_json);

    bool schema_bad =
        session_id_json == NULL
        || !json_object_is_type(session_id_json, json_type_string)
        || account_id_json == NULL
        || (!json_object_is_type(account_id_json, json_type_string)
            && !json_object_is_type(account_id_json, json_type_int));
    if (schema_bad)
    {
        CHIAKI_LOGE(session->log, "http_create_session: Unexpected JSON schema, could not parse sessionId and accountId.");
        CHIAKI_LOGV(session->log, json_object_to_json_string_ext(json, JSON_C_TO_STRING_PRETTY));
        err = CHIAKI_ERR_UNKNOWN;
        goto cleanup_json;
    }
    const char* session_id = json_object_get_string(session_id_json);
    if (strlen(session_id) != 36)
    {
        CHIAKI_LOGE(session->log, "http_create_session: Unexpected JSON schema, sessionId is not a UUIDv4, was '%s'.", session_id);
        CHIAKI_LOGV(session->log, json_object_to_json_string_ext(json, JSON_C_TO_STRING_PRETTY));
        err = CHIAKI_ERR_UNKNOWN;
        goto cleanup_json;
    }
    memcpy(session->session_id, json_object_get_string(session_id_json), 36);
    make_session_id_header(&session->session_id_header, session->session_id);

    session->account_id = json_object_get_int64(account_id_json);

cleanup_json:
    json_object_put(json);
cleanup_json_tokener:
    json_tokener_free(tok);
cleanup:
    free(session_create_json);
    free(response_data.data);
    curl_easy_cleanup(curl);

    return err;
}

/**
 * Checks a session on the PSN server.
 *
 * @param session The Session instance.
 * @return CHIAKI_ERR_SUCCESS on success, or an error code on failure.
*/
static ChiakiErrorCode http_check_session(Session *session, bool viewurl)
{
    HttpResponseData response_data = {
        .data = malloc(0),
        .size = 0,
    };

    CURL* curl = holepunch_curl_easy_init();
    if(!curl)
    {
        free(response_data.data);
        CHIAKI_LOGE(session->log, "Curl could not init");
        return CHIAKI_ERR_MEMORY;
    }
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, session->oauth_header);
    headers = curl_slist_append(headers, session->session_id_header);

    CURLcode res = curl_easy_setopt(curl, CURLOPT_SHARE, session->curl_share);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_check_session: CURL setopt CURLOPT_SHARE failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_check_session: CURL setopt CURLOPT_FAILONERROR failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_URL, viewurl ? session_view_url : session_create_url);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_check_session: CURL setopt CURLOPT_URL failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_check_session: CURL setopt CURLOPT_TIMEOUT failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_check_session: CURL setopt CURLOPT_HTTPHEADER failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_check_session: CURL setopt CURLOPT_WRITEFUNCTION failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_data);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_check_session: CURL setopt CURLOPT_WRITEDATA failed with CURL error %s", curl_easy_strerror(res));

    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
    res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    if (res != CURLE_OK)
    {
        if (res == CURLE_HTTP_RETURNED_ERROR)
        {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            CHIAKI_LOGE(session->log, "http_check_session: Creating holepunch session failed with HTTP code %ld", http_code);
            err = CHIAKI_ERR_HTTP_NONOK;
        } else {
            CHIAKI_LOGE(session->log, "http_check_session: Creating holepunch session failed with CURL error %s", curl_easy_strerror(res));
            err = CHIAKI_ERR_NETWORK;
        }
        goto cleanup;
    }
    json_tokener *tok = json_tokener_new();
    if(!tok)
    {
        CHIAKI_LOGE(session->log, "http_check_session: Couldn't create new json tokener");
        goto cleanup;
    }
    json_object *json = json_tokener_parse_ex(tok, response_data.data, response_data.size);
    if (json == NULL)
    {
        CHIAKI_LOGE(session->log, "http_check_session: Parsing JSON failed");
        err = CHIAKI_ERR_UNKNOWN;
        goto cleanup_json_tokener;
    }
    const char *json_str = json_object_to_json_string_ext(json, JSON_C_TO_STRING_PRETTY);
    CHIAKI_LOGV(session->log, "http_check_session: retrieved session data \n%s", json_str);

    json_object_put(json);
    cleanup_json_tokener:
        json_tokener_free(tok);
    cleanup:
        free(response_data.data);
        curl_easy_cleanup(curl);
        return err;
}
/**
 * Starts a session on the PSN server. Session must have been created before.
 *
 * @param session The Session instance.
 * @return CHIAKI_ERR_SUCCESS on success, or an error code on failure.
*/
static ChiakiErrorCode http_start_session(Session *session)
{
    char payload_buf[sizeof(session_start_payload_fmt) * 3] = {0};
    char envelope_buf[sizeof(session_start_envelope_fmt) * 2 + sizeof(payload_buf)] = {0};

    char data1_base64[25] = {0};
    char data2_base64[25] = {0};
    ChiakiErrorCode err = chiaki_base64_encode(session->data1, sizeof(session->data1), data1_base64, sizeof(data1_base64));
    if(err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(session->log, "http_start_session: Could not encode data1 into base64.");
        goto offer_cleanup;
    }
    err = chiaki_base64_encode(session->data2, sizeof(session->data2), data2_base64, sizeof(data2_base64));
    if(err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(session->log, "http_start_session: Could not encode data2 into base64.");
        goto offer_cleanup;
    }
    snprintf(payload_buf, sizeof(payload_buf), session_start_payload_fmt,
        session->account_id,
        session->session_id,
        data1_base64,
        data2_base64);

    char device_uid_str[sizeof(session->console_uid) * 2 + 1];
    bytes_to_hex(session->console_uid, sizeof(session->console_uid), device_uid_str, sizeof(device_uid_str));

    snprintf(
        envelope_buf, sizeof(envelope_buf), session_start_envelope_fmt,
        device_uid_str,
        payload_buf,
        session->console_type == CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS4 ? "PS4" : "PS5");

    HttpResponseData response_data = {
        .data = malloc(0),
        .size = 0,
    };

    CURL *curl = holepunch_curl_easy_init();
    if(!curl)
    {
        free(response_data.data);
        CHIAKI_LOGE(session->log, "Curl could not init");
        err = CHIAKI_ERR_MEMORY;
        goto offer_cleanup;
    }

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, session->oauth_header);
    headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");
    headers = curl_slist_append(headers, "User-Agent: RpNetHttpUtilImpl");

    CURLcode res = curl_easy_setopt(curl, CURLOPT_SHARE, session->curl_share);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_start_session: CURL setopt CURLOPT_SHARE failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_start_session: CURL setopt CURLOPT_FAILONERROR failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_URL, session_command_url);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_start_session: CURL setopt CURLOPT_URL failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_start_session: CURL setopt CURLOPT_TIMEOUT failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_start_session: CURL setopt CURLOPT_HTTPHEADER failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, envelope_buf);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_start_session: CURL setopt CURLOPT_POSTFIELDS failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_start_session: CURL setopt CURLOPT_WRITEFUNCTION failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_data);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_start_session: CURL setopt CURLOPT_WRITEDATA failed with CURL error %s", curl_easy_strerror(res));

    CHIAKI_LOGV(session->log, "http_start_session: Sending JSON:\n%s", envelope_buf);

    res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    CHIAKI_LOGV(session->log, "http_start_session: Received JSON:\n%.*s", (int)response_data.size, response_data.data);
    if (res != CURLE_OK)
    {
        if (res == CURLE_HTTP_RETURNED_ERROR)
        {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            CHIAKI_LOGE(session->log, "http_start_session: Starting holepunch session failed with HTTP code %ld.", http_code);
            CHIAKI_LOGV(session->log, "Request Body: %s.", envelope_buf);
            CHIAKI_LOGV(session->log, "Response Body: %.*s.", (int)response_data.size, response_data.data);
            err = CHIAKI_ERR_HTTP_NONOK;
        } else {
            CHIAKI_LOGE(session->log, "http_start_session: Starting holepunch session failed with CURL error %s.", curl_easy_strerror(res));
            err = CHIAKI_ERR_NETWORK;
        }
        goto cleanup;
    }

    chiaki_mutex_lock(&session->state_mutex);
    session->state |= SESSION_STATE_DATA_SENT;
    log_session_state(session);
    chiaki_mutex_unlock(&session->state_mutex);

cleanup:
    curl_easy_cleanup(curl);
    free(response_data.data);
offer_cleanup:
    if(err != CHIAKI_ERR_SUCCESS)
    {
        if(session->our_offer_msg)
        {
            session_message_free(session->our_offer_msg);
            session->our_offer_msg = NULL;
        }
        if(session->local_candidates)
        {
            free(session->local_candidates);
            session->local_candidates = NULL;
        }
    }
    return err;
}

/**
 * Sends a session message to the PSN server.
 *
 * @param session The Session instance.
 * @param message The session message to send, will be addressed to the console defined in the session
 * @return CHIAKI_ERR_SUCCESS on success, or an error code on failure.
*/
static ChiakiErrorCode http_send_session_message(Session *session, SessionMessage *message, bool short_msg)
{
    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;

    HttpResponseData response_data = {
        .data = malloc(0),
        .size = 0,
    };

    char url[128] = {0};
    snprintf(url, sizeof(url), session_message_url_fmt, session->session_id);

    char console_uid_str[sizeof(session->console_uid) * 2 + 1] = {0};
    bytes_to_hex(session->console_uid, sizeof(session->console_uid), console_uid_str, sizeof(console_uid_str));

    char *payload_str = NULL;
    size_t payload_len = 0;
    if(short_msg)
        short_message_serialize(session, message, &payload_str, &payload_len);
    else
        session_message_serialize(session, message, &payload_str, &payload_len);
    char msg_buf[sizeof(session_message_envelope_fmt) * 2 + payload_len];
    snprintf(
        msg_buf, sizeof(msg_buf), session_message_envelope_fmt,
        payload_str, session->account_id, console_uid_str,
        session->console_type == CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS4 ? "PS4" : "PS5"
    );
    CHIAKI_LOGV(session->log, "Message to send: %s", msg_buf);
    CURL *curl = holepunch_curl_easy_init();
    if(!curl)
    {
        free(payload_str);
        free(response_data.data);
        CHIAKI_LOGE(session->log, "Curl could not init");
        return CHIAKI_ERR_MEMORY;
    }

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, session->oauth_header);
    headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");

    CURLcode res = curl_easy_setopt(curl, CURLOPT_SHARE, session->curl_share);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_send_session_message: CURL setopt CURLOPT_SHARE failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_send_session_message: CURL setopt CURLOPT_FAILONERROR failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_URL, url);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_send_session_message: CURL setopt CURLOPT_URL failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_send_session_message: CURL setopt CURLOPT_TIMEOUT failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_send_session_message: CURL setopt CURLOPT_HTTPHEADER failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, msg_buf);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_send_session_message: CURL setopt CURLOPT_POSTFIELDS failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_send_session_message: CURL setopt CURLOPT_WRITEFUNCTION failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_data);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "http_send_session_message: CURL setopt CURLOPT_WRITEDATA failed with CURL error %s", curl_easy_strerror(res));

    res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    if (res != CURLE_OK)
    {
        if (res == CURLE_HTTP_RETURNED_ERROR)
        {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            CHIAKI_LOGE(session->log, "http_send_session_message: Sending holepunch session message failed with HTTP code %ld.", http_code);
            CHIAKI_LOGV(session->log, "Request Body: %s.", msg_buf);
            err = CHIAKI_ERR_HTTP_NONOK;
        } else {
            CHIAKI_LOGE(session->log, "http_send_session_message: Sending holepunch session message failed with CURL error %s.", curl_easy_strerror(res));
            err = CHIAKI_ERR_NETWORK;
        }
        goto cleanup;
    }

cleanup:
    curl_easy_cleanup(curl);
    free(payload_str);
    free(response_data.data);
    return err;
}

/**
 * Deletes session from PlayStation servers
 *
 * @param session The Session instance.
 * @return CHIAKI_ERR_SUCCESS on success, or an error code on failure.
*/
static ChiakiErrorCode deleteSession(Session *session)
{
    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;

    HttpResponseData response_data = {
        .data = malloc(0),
        .size = 0,
    };

    char url[128] = {0};
    snprintf(url, sizeof(url), delete_messsage_url_fmt, session->session_id);

    CURL *curl = holepunch_curl_easy_init();
    if(!curl)
    {
        free(response_data.data);
        CHIAKI_LOGE(session->log, "Curl could not init");
        return CHIAKI_ERR_MEMORY;
    }

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, session->oauth_header);
    headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");

    CURLcode res = curl_easy_setopt(curl, CURLOPT_SHARE, session->curl_share);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "delete_session: CURL setopt CURLOPT_SHARE failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "delete_session: CURL setopt CURLOPT_FAILONERROR failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_URL, url);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "delete_session: CURL setopt CURLOPT_URL failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "delete_session: CURL setopt CURLOPT_TIMEOUT failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "delete_session: CURL setopt CURLOPT_HTTPHEADER failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "delete_session: CURL setopt CURLOPT_CUSTOMREQUEST failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "delete_session: CURL setopt CURLOPT_WRITEFUNCTION failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_data);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "delete_session: CURL setopt CURLOPT_WRITEDATA failed with CURL error %s", curl_easy_strerror(res));

    res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    if (res != CURLE_OK)
    {
        if (res == CURLE_HTTP_RETURNED_ERROR)
        {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            CHIAKI_LOGE(session->log, "http_send_session_message: Sending holepunch session message failed with HTTP code %ld.", http_code);
            err = CHIAKI_ERR_HTTP_NONOK;
        } else {
            CHIAKI_LOGE(session->log, "http_send_session_message: Sending holepunch session message failed with CURL error %s.", curl_easy_strerror(res));
            err = CHIAKI_ERR_NETWORK;
        }
        goto cleanup;
    }

cleanup:
    curl_easy_cleanup(curl);
    free(response_data.data);
    return err;
}

/**
 * Retrieves the IP address on the local network of the client.
 *
 * @param session The Session instance.
 * @param local_console_candidate A local console candidate.
 * @return The IP address of the client on the local network, or NULL if the IP address could not be retrieved. Needs to be freed by the caller.
*/
static ChiakiErrorCode get_client_addr_local(Session *session, Candidate *local_console_candidate, char *out, size_t out_len)
{
    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
    bool status = false;
#ifdef _WIN32
    PIP_ADAPTER_INFO pAdapterInfo;
    PIP_ADAPTER_INFO pAdapter = NULL;
#define MALLOC(x) HeapAlloc(GetProcessHeap(), 0, (x))
#define FREE(x) HeapFree(GetProcessHeap(), 0, (x))
    DWORD dwRetVal = 0;
    bool ethernet = false;
    ULONG ulOutBufLen = sizeof (IP_ADAPTER_INFO);
    pAdapterInfo = (IP_ADAPTER_INFO *) MALLOC(sizeof (IP_ADAPTER_INFO));
    if (pAdapterInfo == NULL) {
        CHIAKI_LOGE(session->log, "Error allocating memory needed to call GetAdaptersinfo\n");
        return CHIAKI_ERR_MEMORY;
    }
    if (GetAdaptersInfo(pAdapterInfo, &ulOutBufLen) == ERROR_BUFFER_OVERFLOW) {
        FREE(pAdapterInfo);
        pAdapterInfo = (IP_ADAPTER_INFO *) MALLOC(ulOutBufLen);
        if (pAdapterInfo == NULL) {
            CHIAKI_LOGE(session->log, "Error allocating memory needed to call GetAdaptersinfo\n");
            return CHIAKI_ERR_MEMORY;
        }
    }

    if ((dwRetVal = GetAdaptersInfo(pAdapterInfo, &ulOutBufLen)) == NO_ERROR) {
        pAdapter = pAdapterInfo;
        while (pAdapter) {
            if(pAdapter->Type != IF_TYPE_IEEE80211 && pAdapter->Type != MIB_IF_TYPE_ETHERNET)
            {
                pAdapter = pAdapter->Next;
                continue;
            }
            for(IP_ADDR_STRING *str = &pAdapter->IpAddressList; str != NULL; str = str->Next)
            {
                if(strcmp(str->IpAddress.String, "") == 0)
                {
                    continue;
                }
                if(strcmp(str->IpAddress.String, "0.0.0.0") == 0)
                {
                    continue;
                }
                memcpy(local_console_candidate->addr, str->IpAddress.String, strlen(str->IpAddress.String) + 1);
                status = true;
                if(pAdapter->Type == MIB_IF_TYPE_ETHERNET)
                    ethernet = true;
                else
                    ethernet = false;
                break;
            }
            if(status && !ethernet)
                break;
            pAdapter = pAdapter->Next;
        }
    } else {
        CHIAKI_LOGE(session->log, "GetAdaptersInfo failed with error: %d\n", dwRetVal);

    }
    if (pAdapterInfo)
        FREE(pAdapterInfo);

    if(!status)
    {
        CHIAKI_LOGE(session->log, "Couldn't find a valid local address!");
        return CHIAKI_ERR_NETWORK;
    }
    return err;
#undef MALLOC
#undef FREE
#elif defined(__SWITCH__)
    // switch does not have ifaddrs.h, use arpa/inet.h
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = gethostid();
    if (!inet_ntop(AF_INET,  &(addr.sin_addr), local_console_candidate->addr, sizeof(local_console_candidate->addr)))
    {
        CHIAKI_LOGE(session->log, "%s: inet_ntop failed with error: " CHIAKI_SOCKET_ERROR_FMT "\n", addr.sin_addr.s_addr, CHIAKI_SOCKET_ERROR_VALUE);
        CHIAKI_LOGE(session->log, "Couldn't find a valid external address!");
        return CHIAKI_ERR_NETWORK;
    }
    return err;
#else
    struct ifaddrs *local_addrs, *current_addr;
    void *in_addr;
    struct sockaddr_in *res4 = NULL;

    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if(getifaddrs(&local_addrs) != 0)
    {
        CHIAKI_LOGE(session->log, "Couldn't get local address");
        return CHIAKI_ERR_NETWORK;
    }
    for (current_addr = local_addrs; current_addr != NULL; current_addr = current_addr->ifa_next)
    {
        if (current_addr->ifa_addr == NULL)
            continue;
        if ((current_addr->ifa_flags & (IFF_UP|IFF_RUNNING|IFF_LOOPBACK)) != (IFF_UP|IFF_RUNNING))
            continue;
        switch (current_addr->ifa_addr->sa_family)
        {
            case AF_INET:
                res4 = (struct sockaddr_in *)current_addr->ifa_addr;
                in_addr = &res4->sin_addr;
                break;
            default:
                continue;
        }
        if (!inet_ntop(current_addr->ifa_addr->sa_family, in_addr, local_console_candidate->addr, sizeof(local_console_candidate->addr)))
        {
            CHIAKI_LOGE(session->log, "%s: inet_ntop failed with error: " CHIAKI_SOCKET_ERROR_FMT "\n", current_addr->ifa_name, CHIAKI_SOCKET_ERROR_VALUE);
            continue;
        }
        status = true;
        break;
    }
    if(!status)
    {
        CHIAKI_LOGE(session->log, "Couldn't find a valid external address!");
        return CHIAKI_ERR_NETWORK;
    }
    freeifaddrs(local_addrs);
    return err;
#endif
}

/**
 * Retrieves the gateway information using UPnP.
 *
 * @param log The ChiakiLog instance for logging.
 * @param[out] info Pointer to the UPNPGatewayInfo structure to store the retrieved information.
 * @return true if the gateway information was successfully retrieved, false otherwise.
 */
static ChiakiErrorCode upnp_get_gateway_info(ChiakiLog *log, UPNPGatewayInfo *info)
{
    int success = 0;
    struct UPNPDev *devlist = upnpDiscover(
        2000 /** ms, delay*/, NULL, NULL, 0, 0, 2, &success);
    if (devlist == NULL || success != UPNPDISCOVER_SUCCESS) {
        CHIAKI_LOGI(log, "Failed to find UPnP-capable devices on network: err=%d", success);
        return CHIAKI_ERR_NETWORK;
    }

    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
#if MINIUPNPC_API_VERSION >= 18
    int igd_ret = UPNP_GetValidIGD(devlist, info->urls, info->data, info->lan_ip, sizeof(info->lan_ip), NULL, 0);
#else
    int igd_ret = UPNP_GetValidIGD(devlist, info->urls, info->data, info->lan_ip, sizeof(info->lan_ip));
#endif
    if (igd_ret != 1) {
        CHIAKI_LOGI(log, "Failed to discover internet gateway via UPnP: err=%d", igd_ret);
        err = CHIAKI_ERR_NETWORK;
    }

    freeUPNPDevlist(devlist);
    return err;
}

/**
 * Retrieves the external IP address of the gateway.
 *
 * @param gw_info The UPNPGatewayInfo structure containing the gateway information.
 * @param[out] out Pointer to the buffer where the external IP address will be stored, needs to be at least 16 bytes long.
 */
static bool get_client_addr_remote_upnp(ChiakiLog *log, UPNPGatewayInfo *gw_info, char* out)
{
    int res = UPNP_GetExternalIPAddress(
        gw_info->urls->controlURL, gw_info->data->first.servicetype, out);
    bool success = (res == UPNPCOMMAND_SUCCESS);
    if(!success)
        CHIAKI_LOGE(log, "UPNP error getting external IP Address: %s", strupnperror(res));
    return success;
}

/**
 * Adds a port mapping to the gateway.
 *
 * @param log The log to write to
 * @param gw_info The UPNPGatewayInfo structure containing the gateway information.
 * @param port_internal The internal port to map.
 * @param port_external The external port to map.
 * @return true if the port mapping was successfully added, false otherwise.
*/
static bool upnp_add_udp_port_mapping(ChiakiLog* log, UPNPGatewayInfo *gw_info, uint16_t port_internal, uint16_t port_external)
{
    char port_internal_str[6];
    snprintf(port_internal_str, sizeof(port_internal_str), "%d", port_internal);
    char port_external_str[6];
    snprintf(port_external_str, sizeof(port_external_str), "%d", port_external);

    int res = UPNP_AddPortMapping(
        gw_info->urls->controlURL, gw_info->data->first.servicetype,
        port_external_str, port_internal_str, gw_info->lan_ip, "Chiaki Streaming", "UDP", NULL, "0");

    bool success = (res == UPNPCOMMAND_SUCCESS);
    if(!success)
        CHIAKI_LOGE(log, "UPNP error adding port mapping: %s", strupnperror(res));
    return success;
}

/**
 * Deletes a port mapping from the gateway.
 *
 * @param gw_info The UPNPGatewayInfo structure containing the gateway information.
 * @param port_external The external port to delete the mapping for.
 * @return true if the port mapping was successfully deleted, false otherwise.
*/
static bool upnp_delete_udp_port_mapping(ChiakiLog *log, UPNPGatewayInfo *gw_info, uint16_t port_external)
{
    char port_external_str[6];
    snprintf(port_external_str, sizeof(port_external_str), "%d", port_external);

    int res = UPNP_DeletePortMapping(
        gw_info->urls->controlURL, gw_info->data->first.servicetype,
        port_external_str, "UDP", NULL);
    bool success = (res == UPNPCOMMAND_SUCCESS);
    if(!success)
        CHIAKI_LOGE(log, "UPNP error deleting port mapping: %s", strupnperror(res));
    return success;
}

/**
 * Retrieves the external IP address (i.e. internet-visible) of the client using STUN.
 *
 * @param log The ChiakiLog instance for logging.
 * @return The external IP address of the client, or NULL if the IP address could not be retrieved. Needs to be freed by the caller.
*/
static bool get_client_addr_remote_stun(Session *session, char *address, uint16_t *port, chiaki_socket_t *sock, bool ipv4)
{
    // run STUN test if it hasn't been run yet
    if(session->stun_allocation_increment == -1)
    {
        ChiakiErrorCode err = get_stun_servers(session);
        if(err != CHIAKI_ERR_SUCCESS)
        {
            CHIAKI_LOGW(session->log, "Getting stun servers returned error %s", chiaki_error_string(err));
        }
        if (!stun_port_allocation_test(session->log, address, port, &session->stun_allocation_increment, &session->stun_random_allocation, session->stun_server_list, session->num_stun_servers, sock))
        {
            CHIAKI_LOGE(session->log, "get_client_addr_remote_stun: Failed to get external address");
            return false;
        }
        return true;
    }
    if(ipv4)
    {
        if (!stun_get_external_address(session->log, address, port, session->stun_server_list, session->num_stun_servers, sock, ipv4))
        {
            CHIAKI_LOGE(session->log, "get_client_addr_remote_stun: Failed to get external address");
            return false;
        }
    }
    else
    {
        if (!stun_get_external_address(session->log, address, port, session->stun_server_list_ipv6, session->num_stun_servers_ipv6, sock, ipv4))
        {
            CHIAKI_LOGE(session->log, "get_client_addr_remote_stun: Failed to get external address");
            return false;
        }
    }
    return true;
}

/**
 * Retrieves the MAC address associated with the given IP address.
 *
 * @param ip_addr The IP address for which to retrieve the MAC address.
 * @param mac_addr Pointer to the buffer where the MAC address will be stored, needs to be at least 6 bytes long.
 * @return True if the MAC address was successfully retrieved, false otherwise.
 */
// static bool get_mac_addr(ChiakiLog *log, uint8_t *mac_addr)
// {
//     struct ifreq req;
//     struct ifconf conf;
//     char buf[1024];

//     int sock = socket(AF_INET, SOCK_DGRAM, 0);
//     if (CHIAKI_SOCKET_IS_INVALID(sock)) {
//         return false;
//     }

//     memset(&req, 0, sizeof(req));
//     conf.ifc_len = sizeof(buf);
//     conf.ifc_buf = buf;
//     if(ioctl(sock, SIOCGIFCONF, &conf) < 0)
//     {
//         CHIAKI_LOGE(log, "Error getting local IFConfig:  " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
//         return false;
//     }

//     struct ifreq* it = conf.ifc_req;
//     const struct ifreq* const end = it + (conf.ifc_len / sizeof(struct ifreq));
//     bool success = false;
//     for (; it != end; it++)
//     {
//         strcpy(req.ifr_name, it->ifr_name);
//         if (ioctl(sock, SIOCGIFFLAGS, &req) == 0) {
//             if (! (req.ifr_flags & IFF_LOOPBACK)) { // don't count loopback
//                 if (ioctl(sock, SIOCGIFHWADDR, &req) == 0) {
//                     success = true;
//                     break;
//                 }
//             }
//         }
//         else
//             CHIAKI_LOGE(log, "Error getting IFFlags for local device:  " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
//     }
//     if(!success)
//         return false;
//     memcpy(mac_addr, req.ifr_hwaddr.sa_data, 6);
//     return true;
// }


/**
 * Linking to a responsive PlayStation candidate from the available console candidates
 *
 * @param[in] session Pointer to the session context
 * @param[in] local_candidates Pointer to the client's candidates
 * @param[in] candidates Candidates for the console to check against
 * @param[out] out Pointer to the socket where the connection was established with the selected candidate
*/

#if !defined(__SWITCH__) && !defined(__ANDROID__)
typedef struct {
    struct event_base *base;
    struct event **events;
    size_t events_capacity;
    size_t events_count;
    bool event_triggered;
    chiaki_socket_t ready_sock;
} CandidateEventContext;

static void candidate_event_cb(evutil_socket_t fd, short events, void *arg)
{
    (void)events;
    CandidateEventContext *ctx = arg;
    ctx->ready_sock = (chiaki_socket_t)fd;
    ctx->event_triggered = true;
    if (ctx->base)
        event_base_loopexit(ctx->base, NULL);
}

static bool candidate_event_add_socket(CandidateEventContext *ctx, chiaki_socket_t sock)
{
    if (CHIAKI_SOCKET_IS_INVALID(sock))
        return true;
    if (!ctx || ctx->events_count >= ctx->events_capacity)
        return false;
    struct event *ev = event_new(ctx->base, sock, EV_READ | EV_PERSIST, candidate_event_cb, ctx);
    if (!ev)
        return false;
    if (event_add(ev, NULL) < 0) {
        event_free(ev);
        return false;
    }
    ctx->events[ctx->events_count++] = ev;
    return true;
}
#endif

static ChiakiErrorCode check_candidates(
    Session *session, Candidate* local_candidates, Candidate *candidates_received, size_t num_candidates, chiaki_socket_t *out,
    Candidate *out_candidate)
{
    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;

    // Set up request buffer
    uint8_t request_buf[CHECK_CANDIDATES_REQUEST_NUMBER][88] = {0};
    uint8_t request_id[CHECK_CANDIDATES_REQUEST_NUMBER][5] = {0};

    // send CHECK_CANDIDATES_REQUEST_NUMBER requests for connection pairing with ps
    for(int i = 0; i < CHECK_CANDIDATES_REQUEST_NUMBER; i++)
    {
        chiaki_random_bytes_crypt(request_id[i], sizeof(request_id[i]));
        *(uint32_t*)&request_buf[i][0x00] = htonl(MSG_TYPE_REQ);
        memcpy(&request_buf[i][0x04], session->hashed_id_local, sizeof(session->hashed_id_local));
        memcpy(&request_buf[i][0x24], session->hashed_id_console, sizeof(session->hashed_id_console));
        *(uint16_t*)&request_buf[i][0x44] = htons(session->sid_local);
        *(uint16_t*)&request_buf[i][0x46] = htons(session->sid_console);
        memcpy(&request_buf[i][0x4b], request_id[i], sizeof(request_id[i]));
    }

    Candidate *local_candidate = &local_candidates[0];
    Candidate *remote_candidate = &local_candidates[1];

    size_t extra_addresses_used = 0;
    // Set up addresses for each candidate + extras (use sockaddr_storage and cast bc needs to be at least that big if we get ipv6)
    struct sockaddr_storage addrs[num_candidates + EXTRA_CANDIDATE_ADDRESSES];
    socklen_t lens[num_candidates + EXTRA_CANDIDATE_ADDRESSES];
    Candidate candidates[num_candidates + EXTRA_CANDIDATE_ADDRESSES];
    memcpy(candidates, candidates_received, num_candidates * sizeof(Candidate));
    int responses_received[num_candidates + EXTRA_CANDIDATE_ADDRESSES];
#ifdef __SWITCH__
    // Use poll() on Switch — select() fails when FD numbers >= FD_SETSIZE (256)
    struct pollfd *pollfds = NULL;
#endif
#ifdef __ANDROID__
    fd_set fds;
#endif
    bool failed = true;
    char service_remote[6];
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family = AF_UNSPEC;
    struct addrinfo *addr_remote;
    int socks_count = 0;
    int socks_capacity = 0;
    chiaki_socket_t *socks = NULL;

    if(session->stun_random_allocation)
    {
        socks_capacity = session->port_guessing_socks;
        socks = calloc(socks_capacity, sizeof(chiaki_socket_t));
        if(!socks)
        {
            err = CHIAKI_ERR_MEMORY;
            goto cleanup_sockets;
        }
        for(int i = 0; i < socks_capacity; i++)
            socks[i] = CHIAKI_INVALID_SOCKET;
        int socks_to_open = socks_capacity;
        for (int i=0; i < socks_to_open; i++)
        {
            socks[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (CHIAKI_SOCKET_IS_INVALID(socks[i]))
            {
                CHIAKI_LOGE(session->log, "check_candidates: Creating ipv4 socket %d failed, stopping (opened %d sockets)", i, socks_count);
                break;
            }
            struct sockaddr_in client_addr;
            memset(&client_addr, 0, sizeof(client_addr));
            client_addr.sin_family = AF_INET;
            client_addr.sin_addr.s_addr = INADDR_ANY;
            client_addr.sin_port = 0;
            socklen_t client_addr_len = sizeof(client_addr);
            const int enable = 1;
#if defined(SO_REUSEPORT)
            if (setsockopt(socks[i], SOL_SOCKET, SO_REUSEPORT, (const void *)&enable, sizeof(int)) < 0)
            {
                CHIAKI_LOGE(session->log, "check_candidates: setsockopt(SO_REUSEPORT) failed with error " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
                if (CHIAKI_SOCKET_IS_INVALID(socks[i]))
                {
                    CHIAKI_SOCKET_CLOSE(socks[i]);
                    socks[i] = CHIAKI_INVALID_SOCKET;
                }
                continue;
            }
#else
            if (setsockopt(socks[i], SOL_SOCKET, SO_REUSEADDR, (const void *)&enable, sizeof(int)) < 0)
            {
                CHIAKI_LOGE(session->log, "check_candidates: setsockopt(SO_REUSEADDR) failed with error" CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
                if (!CHIAKI_SOCKET_IS_INVALID(socks[i]))
                {
                    CHIAKI_SOCKET_CLOSE(socks[i]);
                    socks[i] = CHIAKI_INVALID_SOCKET;
                }
                continue;
            }
#endif
                // set low ttl so packets just punch hole in NAT
#ifdef _WIN32
            DWORD ttl = 2;
#else
            int ttl = 2;
#endif
            if (setsockopt(socks[i], IPPROTO_IP, IP_TTL, (const CHIAKI_SOCKET_BUF_TYPE)&ttl, sizeof(ttl)) < 0)
            {
                CHIAKI_LOGE(session->log, "check_candidates: setsockopt(IP_TTL) failed with error" CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
                if (!CHIAKI_SOCKET_IS_INVALID(socks[i]))
                {
                    CHIAKI_SOCKET_CLOSE(socks[i]);
                    socks[i] = CHIAKI_INVALID_SOCKET;
                }
                continue;
            }
            if(bind(socks[i], (struct sockaddr*)&client_addr, client_addr_len) < 0)
            {
                CHIAKI_LOGE(session->log, "check_candidates: Binding ipv4 socket failed with error " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
                if(!CHIAKI_SOCKET_IS_INVALID(socks[i]))
                {
                    CHIAKI_SOCKET_CLOSE(socks[i]);
                    socks[i] = CHIAKI_INVALID_SOCKET;
                }
                continue;
            }
            err = chiaki_socket_set_nonblock(socks[i], true);
            if(err != CHIAKI_ERR_SUCCESS)
            {
                CHIAKI_LOGE(session->log, "check_candidates: Failed to set ipv4 socket %d to non-blocking: %s", i, chiaki_error_string(err));
                if (!CHIAKI_SOCKET_IS_INVALID(socks[i]))
                {
                    CHIAKI_SOCKET_CLOSE(socks[i]);
                    socks[i] = CHIAKI_INVALID_SOCKET;
                }
                continue;
            }
            socks_count++;
        }
        CHIAKI_LOGI(session->log, "check_candidates: Opened %d NAT probing sockets", socks_count);
    }
    for (int i=0; i < num_candidates; i++)
    {
        Candidate *candidate = &candidates[i];
        responses_received[i] = 0;

        sprintf(service_remote, "%d", candidate->port);

        if (getaddrinfo(candidate->addr, service_remote, &hints, &addr_remote) != 0)
        {
            CHIAKI_LOGE(session->log, "check_candidates: getaddrinfo failed for %s:%d with error " CHIAKI_SOCKET_ERROR_FMT, candidate->addr, candidate->port, CHIAKI_SOCKET_ERROR_VALUE);
            err = CHIAKI_ERR_UNKNOWN;
            continue;
        }
        memcpy((struct sockaddr *)&addrs[i], addr_remote->ai_addr, addr_remote->ai_addrlen);
        lens[i] = addr_remote->ai_addrlen;
        bool sent = false;
        switch(((struct sockaddr *)&addrs[i])->sa_family)
        {
            case AF_INET:
                if(!CHIAKI_SOCKET_IS_INVALID(session->ipv4_sock))
                {
                    if (sendto(session->ipv4_sock, (CHIAKI_SOCKET_BUF_TYPE) request_buf[0], sizeof(request_buf[0]), 0, (struct sockaddr *)&addrs[i], lens[i]) < 0)
                    {
                        CHIAKI_LOGW(session->log, "check_candidates: Sending request failed for %s:%d (type %d) with error: " CHIAKI_SOCKET_ERROR_FMT, candidate->addr, candidate->port, candidate->type, CHIAKI_SOCKET_ERROR_VALUE);
                        err = CHIAKI_ERR_NETWORK;
                        freeaddrinfo(addr_remote);
                        continue;
                    }
                }
                if(session->stun_random_allocation && ((candidate->type == CANDIDATE_TYPE_STATIC && !sent) || candidate->type == CANDIDATE_TYPE_STUN))
                {
                    for (int j=0; j<socks_count; j++)
                    {
                        if(CHIAKI_SOCKET_IS_INVALID(socks[j]))
                            continue;
                        if (sendto(socks[j], (CHIAKI_SOCKET_BUF_TYPE) request_buf[0], sizeof(request_buf[0]), 0, (struct sockaddr *)&addrs[i], lens[i]) < 0)
                        {
                            CHIAKI_LOGW(session->log, "check_candidates: Sending request for socket %d failed for %s:%d with error, closing socket: " CHIAKI_SOCKET_ERROR_FMT, j, candidate->addr, candidate->port, CHIAKI_SOCKET_ERROR_VALUE);
                            if (!CHIAKI_SOCKET_IS_INVALID(socks[j]))
                            {
                                CHIAKI_SOCKET_CLOSE(socks[j]);
                                socks[j] = CHIAKI_INVALID_SOCKET;
                            }
                            continue;
                        }
                        sent = true;
                    }
                }
                break;
            case AF_INET6:
                if(!CHIAKI_SOCKET_IS_INVALID(session->ipv6_sock))
                {
                    if (sendto(session->ipv6_sock, (CHIAKI_SOCKET_BUF_TYPE) request_buf[0], sizeof(request_buf[0]), 0, (struct sockaddr *)&addrs[i], lens[i]) < 0)
                    {
                        CHIAKI_LOGW(session->log, "check_candidates: Sending request failed for %s:%d (type %d) with error: " CHIAKI_SOCKET_ERROR_FMT, candidate->addr, candidate->port, candidate->type, CHIAKI_SOCKET_ERROR_VALUE);
                        err = CHIAKI_ERR_NETWORK;
                        freeaddrinfo(addr_remote);
                        continue;
                    }
                }
                break;
            default:
                CHIAKI_LOGW(session->log, "Unsupported address family, skipping...");
        }
        failed = false;
        freeaddrinfo(addr_remote);
    }
    if(failed)
    {
        err = CHIAKI_ERR_NETWORK;
        goto cleanup_sockets;
    }

    // Wait for responses
    uint8_t response_buf[88];

    chiaki_socket_t selected_sock = CHIAKI_INVALID_SOCKET;
    Candidate *selected_candidate = NULL;
    bool received_response = false;
    bool responded = false;
    bool connecting = false;
    int retry_counter = 0;
#if !defined(__SWITCH__) && !defined(__ANDROID__)
    CandidateEventContext poll_ctx = {0};
    size_t socket_slots = 0;
    if (!CHIAKI_SOCKET_IS_INVALID(session->ipv4_sock))
        socket_slots++;
    if (!CHIAKI_SOCKET_IS_INVALID(session->ipv6_sock))
        socket_slots++;
    if (session->stun_random_allocation)
        socket_slots += socks_count;
    if (socket_slots == 0)
    {
        err = CHIAKI_ERR_NETWORK;
        goto cleanup_sockets;
    }
    poll_ctx.base = event_base_new();
    if (!poll_ctx.base)
    {
        err = CHIAKI_ERR_MEMORY;
        goto cleanup_sockets;
    }
    poll_ctx.events = calloc(socket_slots, sizeof(struct event *));
    if (!poll_ctx.events)
    {
        err = CHIAKI_ERR_MEMORY;
        goto cleanup_sockets;
    }
    poll_ctx.events_capacity = socket_slots;
    if (!candidate_event_add_socket(&poll_ctx, session->ipv4_sock) ||
        !candidate_event_add_socket(&poll_ctx, session->ipv6_sock))
    {
        err = CHIAKI_ERR_MEMORY;
        goto cleanup_sockets;
    }
    if (session->stun_random_allocation)
    {
        for (int i = 0; i < socks_count; i++)
        {
            if (CHIAKI_SOCKET_IS_INVALID(socks[i]))
                continue;
            if (!candidate_event_add_socket(&poll_ctx, socks[i]))
            {
                err = CHIAKI_ERR_MEMORY;
                goto cleanup_sockets;
            }
        }
    }
#endif
#ifdef __SWITCH__
    int poll_capacity = 0;
    if(!CHIAKI_SOCKET_IS_INVALID(session->ipv4_sock))
        poll_capacity++;
    if(!CHIAKI_SOCKET_IS_INVALID(session->ipv6_sock))
        poll_capacity++;
    if(session->stun_random_allocation)
        poll_capacity += socks_count;
    if(poll_capacity == 0)
    {
        err = CHIAKI_ERR_NETWORK;
        goto cleanup_sockets;
    }
    pollfds = calloc(poll_capacity, sizeof(struct pollfd));
    if(!pollfds)
    {
        err = CHIAKI_ERR_MEMORY;
        goto cleanup_sockets;
    }
#endif

    while (!selected_candidate)
    {
        bool timed_out = false;
        chiaki_socket_t ready_sock = CHIAKI_INVALID_SOCKET;
#if !defined(__SWITCH__)
        struct timeval timeout;
#endif
#ifdef __SWITCH__
        nfds_t npollfds = 0;
        if(!CHIAKI_SOCKET_IS_INVALID(session->ipv4_sock))
        {
            pollfds[npollfds].fd = session->ipv4_sock;
            pollfds[npollfds].events = POLLIN;
            pollfds[npollfds].revents = 0;
            npollfds++;
        }
        if(!CHIAKI_SOCKET_IS_INVALID(session->ipv6_sock))
        {
            pollfds[npollfds].fd = session->ipv6_sock;
            pollfds[npollfds].events = POLLIN;
            pollfds[npollfds].revents = 0;
            npollfds++;
        }
        if(session->stun_random_allocation)
        {
            for(int i=0; i<socks_count; i++)
            {
                if(CHIAKI_SOCKET_IS_INVALID(socks[i]))
                    continue;
                pollfds[npollfds].fd = socks[i];
                pollfds[npollfds].events = POLLIN;
                pollfds[npollfds].revents = 0;
                npollfds++;
            }
        }
        int timeout_ms = connecting
            ? SELECT_CANDIDATE_CONNECTION_SEC * 1000
            : (int)(SELECT_CANDIDATE_TIMEOUT_SEC * 1000);

        int ret = poll(pollfds, npollfds, timeout_ms);
        if (ret < 0 && errno != EINTR)
        {
            CHIAKI_LOGE(session->log, "check_candidates: poll() failed with error: " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
            err = CHIAKI_ERR_NETWORK;
            goto cleanup_sockets;
        }
        else if (ret == 0)
        {
            timed_out = true;
        }
        else
        {
            for(nfds_t pi = 0; pi < npollfds; pi++)
            {
                if(!(pollfds[pi].revents & POLLIN))
                    continue;
                ready_sock = pollfds[pi].fd;
                break;
            }
        }
#elif defined(__ANDROID__)
        chiaki_socket_t maxfd = -1;
        FD_ZERO(&fds);
        if(!CHIAKI_SOCKET_IS_INVALID(session->ipv4_sock))
        {
            FD_SET(session->ipv4_sock, &fds);
            maxfd = session->ipv4_sock;
        }
        if(!CHIAKI_SOCKET_IS_INVALID(session->ipv6_sock))
        {
            FD_SET(session->ipv6_sock, &fds);
            if(session->ipv6_sock > maxfd)
                maxfd = session->ipv6_sock;
        }
        if(session->stun_random_allocation)
        {
            for(int i=0; i<socks_count; i++)
            {
                if(CHIAKI_SOCKET_IS_INVALID(socks[i]))
                    continue;
                if(socks[i] > maxfd)
                    maxfd = socks[i];
                FD_SET(socks[i], &fds);
            }
        }
        maxfd = maxfd + 1;
        if(connecting)
        {
            timeout.tv_sec = SELECT_CANDIDATE_CONNECTION_SEC;
            timeout.tv_usec = 0;
        }
        else
        {
            timeout.tv_usec = (int)(SELECT_CANDIDATE_TIMEOUT_SEC * SECOND_US);
            timeout.tv_sec = 0;
        }
        int ret = select(maxfd, &fds, NULL, NULL, &timeout);
        if (ret < 0 && errno != EINTR)
        {
            CHIAKI_LOGE(session->log, "check_candidates: Select failed with error: " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
            err = CHIAKI_ERR_NETWORK;
            goto cleanup_sockets;
        }
        else if (ret == 0)
        {
            timed_out = true;
        }
        else
        {
            if (!CHIAKI_SOCKET_IS_INVALID(session->ipv4_sock) && FD_ISSET(session->ipv4_sock, &fds))
            {
                ready_sock = session->ipv4_sock;
            }
            else if (!CHIAKI_SOCKET_IS_INVALID(session->ipv6_sock) && FD_ISSET(session->ipv6_sock, &fds))
            {
                ready_sock = session->ipv6_sock;
            }
            else
            {
                for(int i=0; i<socks_count; i++)
                {
                    if(CHIAKI_SOCKET_IS_INVALID(socks[i]))
                        continue;
                    if(!FD_ISSET(socks[i], &fds))
                        continue;
                    ready_sock = socks[i];
                    int ttl = 64;
                    if (setsockopt(ready_sock, IPPROTO_IP, IP_TTL, (const CHIAKI_SOCKET_BUF_TYPE)&ttl, sizeof(ttl)) < 0)
                    {
                        CHIAKI_LOGE(session->log, "setsockopt(IP_TTL) failed with error" CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
                        if (!CHIAKI_SOCKET_IS_INVALID(socks[i]))
                        {
                            CHIAKI_SOCKET_CLOSE(socks[i]);
                            socks[i] = CHIAKI_INVALID_SOCKET;
                        }
                        err = CHIAKI_ERR_UNKNOWN;
                        goto cleanup_sockets;
                    }
                    break;
                }
            }
        }
#else
        if (connecting)
        {
            timeout.tv_sec = SELECT_CANDIDATE_CONNECTION_SEC;
            timeout.tv_usec = 0;
        }
        else
        {
            timeout.tv_sec = 0;
            timeout.tv_usec = (int)(SELECT_CANDIDATE_TIMEOUT_SEC * SECOND_US);
        }
        poll_ctx.event_triggered = false;
        poll_ctx.ready_sock = CHIAKI_INVALID_SOCKET;
        event_base_loopexit(poll_ctx.base, &timeout);
        int ret = event_base_dispatch(poll_ctx.base);
        if (ret < 0)
        {
            CHIAKI_LOGE(session->log, "check_candidates: event_base_dispatch failed");
            err = CHIAKI_ERR_NETWORK;
            goto cleanup_sockets;
        }
        timed_out = !poll_ctx.event_triggered;
        ready_sock = poll_ctx.ready_sock;
#endif
        if (timed_out)
        {
            if (CHIAKI_SOCKET_IS_INVALID(selected_sock))
            {
                if(retry_counter < SELECT_CANDIDATE_TRIES && !received_response)
                {
                    retry_counter++;
                    Candidate *candidate = NULL;
                    chiaki_socket_t sock = CHIAKI_INVALID_SOCKET;
                    CHIAKI_LOGI(session->log, "check_candidates: Resending requests to all candidates TRY %d... waiting for 1st response", retry_counter);
                    for (int i=0; i < num_candidates + extra_addresses_used; i++)
                    {
                        if(((struct sockaddr *)&addrs[i])->sa_family == AF_INET)
                            sock = session->ipv4_sock;
                        else if(((struct sockaddr *)&addrs[i])->sa_family == AF_INET6)
                            sock = session->ipv6_sock;
                        else
                        {
                            CHIAKI_LOGE(session->log, "check_candidates: Got an address with an unsupported address family %d, skipping ...", ((struct sockaddr *)&addrs[i])->sa_family);
                            continue;
                        }
                        if(CHIAKI_SOCKET_IS_INVALID(sock))
                            continue;
                        candidate = &candidates[i];
                        if (sendto(sock, (CHIAKI_SOCKET_BUF_TYPE) request_buf[0], sizeof(request_buf[0]), 0, (struct sockaddr *)&addrs[i], lens[i]) < 0)
                        {
                            CHIAKI_LOGE(session->log, "check_candidates: Sending request failed for %s:%d with error: " CHIAKI_SOCKET_ERROR_FMT, candidate->addr, candidate->port, CHIAKI_SOCKET_ERROR_VALUE);
                            continue;
                        }
                    }
                    continue;
                }
                else if(received_response && !connecting)
                {
                    connecting = true;
                    continue;
                }
                CHIAKI_LOGE(session->log, "check_candidates: Waiting for candidate responses timed out");
                err = CHIAKI_ERR_HOST_UNREACH;
                goto cleanup_sockets;
            }
            break;
        }

        Candidate *candidate = NULL;
        chiaki_socket_t candidate_sock = ready_sock;
        socklen_t recv_len = sizeof(struct sockaddr_in6);
        if(candidate_sock == session->ipv4_sock)
        {
            recv_len = sizeof(struct sockaddr_in);
        }
        else if(candidate_sock == session->ipv6_sock)
        {
            recv_len = sizeof(struct sockaddr_in6);
        }
        else if(session->stun_random_allocation)
        {
            bool found = false;
            for(int j=0; j<socks_count; j++)
            {
                if(socks[j] != candidate_sock)
                    continue;
                found = true;
                recv_len = sizeof(struct sockaddr_in);
#ifdef _WIN32
                DWORD ttl = 64;
#else
                int ttl = 64;
#endif
                if (setsockopt(candidate_sock, IPPROTO_IP, IP_TTL, (const CHIAKI_SOCKET_BUF_TYPE)&ttl, sizeof(ttl)) < 0)
                {
                    CHIAKI_LOGE(session->log, "setsockopt(IP_TTL) failed with error" CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
                    if (!CHIAKI_SOCKET_IS_INVALID(socks[j]))
                    {
                        CHIAKI_SOCKET_CLOSE(socks[j]);
                        socks[j] = CHIAKI_INVALID_SOCKET;
                    }
                    err = CHIAKI_ERR_UNKNOWN;
                    goto cleanup_sockets;
                }
                break;
            }
            if (!found)
                candidate_sock = CHIAKI_INVALID_SOCKET;
        }
        if(CHIAKI_SOCKET_IS_INVALID(candidate_sock))
        {
            CHIAKI_LOGE(session->log, "check_candidates: Waiting loop returned but no socket has data!");
            err = CHIAKI_ERR_UNKNOWN;
            goto cleanup_sockets;
        }


        struct sockaddr* recv_address;
        // allocate up to sockaddr in6 since that's what may be needed
        recv_address = malloc(sizeof(struct sockaddr_in6));
        if(!recv_address)
        {
            err = CHIAKI_ERR_MEMORY;
            goto cleanup_sockets;
        }
        char recv_address_string[INET6_ADDRSTRLEN];
        uint16_t recv_address_port = 0;
        int i = 0;
        CHIAKI_SSIZET_TYPE response_len = recvfrom(candidate_sock, (CHIAKI_SOCKET_BUF_TYPE) response_buf, sizeof(response_buf), 0, recv_address, &recv_len);
        if (response_len < 0)
        {
            CHIAKI_LOGE(session->log, "check_candidates: Receiving response failed with error: " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
            free(recv_address);
            continue;
        }
        if(recv_address->sa_family == AF_INET)
        {
            if (!inet_ntop(AF_INET, &(((struct sockaddr_in *)recv_address)->sin_addr), recv_address_string, sizeof(recv_address_string)))
            {
                CHIAKI_LOGE(session->log, "check_candidates: Couldn't retrieve address from recv address!");
                free(recv_address);
                continue;
            }
            recv_address_port = ntohs(((struct sockaddr_in *)recv_address)->sin_port);
        }
        else if (recv_address->sa_family == AF_INET6)
        {
            if (!inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)recv_address)->sin6_addr), recv_address_string, sizeof(recv_address_string)))
            {
                CHIAKI_LOGE(session->log, "check_candidates: Couldn't retrieve address from recv address!");
                free(recv_address);
                continue;
            }
            recv_address_port = ntohs(((struct sockaddr_in6 *)recv_address)->sin6_port);
        }
        else
        {
            CHIAKI_LOGE(session->log, "check_candidates: Got an address with an unsupported address family %d, skipping ...", recv_address->sa_family);
            free(recv_address);
            continue;
        }
        bool existing_candidate = false;
        for (; i < num_candidates + extra_addresses_used; i++)
        {
            candidate = &candidates[i];
            if((strcmp(candidate->addr, recv_address_string) == 0) && (candidate->port == recv_address_port))
            {
                existing_candidate = true;
                break;
            }
        }
        if(!existing_candidate)
        {
            if(extra_addresses_used >= EXTRA_CANDIDATE_ADDRESSES)
            {
                CHIAKI_LOGI(session->log, "check_candidates: Received more than %d extra candidates skipping this one", EXTRA_CANDIDATE_ADDRESSES);
                free(recv_address);
                continue;
            }
            else
            {
                candidate = &candidates[i];
                responses_received[i] = 0;
                memcpy(candidate->addr, recv_address_string, sizeof(recv_address_string));
                candidate->port_mapped = 0;
                candidate->type = CANDIDATE_TYPE_DERIVED;
                if(recv_address->sa_family == AF_INET)
                {
                    candidate->port = recv_address_port;
                    memcpy(candidate->addr_mapped, "0.0.0.0", 8);
                }
                else if(recv_address->sa_family == AF_INET6)
                {
                    candidate->port = recv_address_port;
                    memcpy(candidate->addr_mapped, "0:0:0:0:0:0:0:0", 16);
                }
                else
                {
                    CHIAKI_LOGE(session->log, "check_candidates: Got an address with an unsupported address family %d, skipping ...", recv_address->sa_family);
                    free(recv_address);
                    continue;
                }
                memcpy((struct sockaddr *)&addrs[i], recv_address, recv_len);
                lens[i] = recv_len;
                extra_addresses_used++;
                CHIAKI_LOGI(session->log, "check_candidates: Received new candidate at %s:%d", candidate->addr, candidate->port);
            }
            free(recv_address);
        }
        CHIAKI_LOGV(session->log, "check_candidates: Received data from %s:%d", candidate->addr, candidate->port);
        if (response_len != sizeof(response_buf))
        {
            if(candidate->type == CANDIDATE_TYPE_DERIVED)
                continue;
            CHIAKI_LOGE(session->log, "check_candidates: Received response of unexpected size %zd from %s:%d", response_len, candidate->addr, candidate->port);
            err = CHIAKI_ERR_NETWORK;
            goto cleanup_sockets;
        }
        uint32_t msg_type = ntohl(*((uint32_t*)(response_buf)));
        if (msg_type == MSG_TYPE_REQ)
        {
            CHIAKI_LOGI(session->log, "Responding to request");
            responded = true;
            err = send_responseto_ps(session, response_buf, &candidate_sock, candidate, (struct sockaddr *)&addrs[i], lens[i]);
            if(err != CHIAKI_ERR_SUCCESS && candidate->type != CANDIDATE_TYPE_DERIVED)
                goto cleanup_sockets;
            if((session->stun_random_allocation || candidate->type == CANDIDATE_TYPE_DERIVED) && responses_received[i] == 0)
            {
                if (sendto(candidate_sock, (CHIAKI_SOCKET_BUF_TYPE) request_buf[0], sizeof(request_buf[0]), 0, (struct sockaddr *)&addrs[i], lens[i]) < 0)
                {
                    CHIAKI_LOGE(session->log, "check_candidates: Sending request failed for %s:%d with error: " CHIAKI_SOCKET_ERROR_FMT, candidate->addr, candidate->port, CHIAKI_SOCKET_ERROR_VALUE);
                    goto cleanup_sockets;
                }
            }
            continue;
        }
        if (msg_type != MSG_TYPE_RESP)
        {
            CHIAKI_LOGE(session->log, "check_candidates: Received response of unexpected type %"PRIu32" from %s:%d", msg_type, candidate->addr, candidate->port);
            chiaki_log_hexdump(session->log, CHIAKI_LOG_ERROR, response_buf, 88);
            err = CHIAKI_ERR_UNKNOWN;
            if(candidate->type == CANDIDATE_TYPE_DERIVED)
                continue;
            goto cleanup_sockets;
        }
        // TODO: More validation of localHashedIds, sids and the weird data at 0x4b?
        int responses = responses_received[i];
        if(memcmp(response_buf + 0x4b, request_id[responses], sizeof(request_id[responses])) != 0)
        {
            CHIAKI_LOGE(session->log, "check_candidates: Received response with unexpected request ID from %s:%d", candidate->addr, candidate->port);
            CHIAKI_LOGE(session->log, "check_candidates: Request ID expected:");
            chiaki_log_hexdump(session->log, CHIAKI_LOG_ERROR, request_id[responses], 5);
            CHIAKI_LOGE(session->log, "check_candidates: Request ID received:");
            chiaki_log_hexdump(session->log, CHIAKI_LOG_ERROR, response_buf + 0x4b, 5);
            CHIAKI_LOGE(session->log, "check_candidates: Full response received:");
            chiaki_log_hexdump(session->log, CHIAKI_LOG_ERROR, response_buf, 88);
            continue;
        }
        received_response = true;
        responses_received[i]++;
        responses = responses_received[i];
        CHIAKI_LOGV(session->log, "Received response %d", responses);
        if(responses > (CHECK_CANDIDATES_REQUEST_NUMBER - 1))
        {
            selected_sock = candidate_sock;
            selected_candidate = candidate;
            if (connect(selected_sock, (struct sockaddr *)&addrs[i], lens[i]) < 0)
            {
                CHIAKI_LOGE(session->log, "check_candidates: Connecting socket failed for %s:%d with error " CHIAKI_SOCKET_ERROR_FMT, selected_candidate->addr, selected_candidate->port, CHIAKI_SOCKET_ERROR_VALUE);
                err = CHIAKI_ERR_NETWORK;
                goto cleanup_sockets;
            }
            CHIAKI_LOGV(session->log, "Selected Candidate");
            print_candidate(session->log, selected_candidate);
            break;
        }
        else
        {
            if (sendto(candidate_sock, (CHIAKI_SOCKET_BUF_TYPE) request_buf[responses], sizeof(request_buf[responses]), 0, (struct sockaddr *)&addrs[i], lens[i]) < 0)
            {
                CHIAKI_LOGE(session->log, "check_candidates: Sending request failed for %s:%d with error: " CHIAKI_SOCKET_ERROR_FMT, candidate->addr, candidate->port, CHIAKI_SOCKET_ERROR_VALUE);
                err = CHIAKI_ERR_NETWORK;
                    continue;
            }
        }
    }
    *out = selected_sock;
    // Close non-chosen sockets
    if (session->ipv4_sock != *out && (!CHIAKI_SOCKET_IS_INVALID(session->ipv4_sock)))
    {
        CHIAKI_SOCKET_CLOSE(session->ipv4_sock);
        session->ipv4_sock = CHIAKI_INVALID_SOCKET;
    }
    if (session->ipv6_sock != *out && (!CHIAKI_SOCKET_IS_INVALID(session->ipv6_sock)))
    {
        CHIAKI_SOCKET_CLOSE(session->ipv6_sock);
        session->ipv6_sock = CHIAKI_INVALID_SOCKET;
    }
    if(session->stun_random_allocation)
    {
        for(int j=0; j<socks_count; j++)
        {
            if(!CHIAKI_SOCKET_IS_INVALID(socks[j]) && socks[j] != selected_sock)
            {
                CHIAKI_SOCKET_CLOSE(socks[j]);
                socks[j] = CHIAKI_INVALID_SOCKET;
            }
        }
    }

    err = receive_request_send_response_ps(session, out, selected_candidate, WAIT_RESPONSE_TIMEOUT_SEC);
    if(err == CHIAKI_ERR_TIMEOUT)
    {
        if(!responded)
            goto cleanup_sockets;
    }
    else if(err != CHIAKI_ERR_SUCCESS)
        goto cleanup_sockets;

#if !defined(__SWITCH__) && !defined(__ANDROID__)
    for (size_t i = 0; i < poll_ctx.events_count; i++)
        event_free(poll_ctx.events[i]);
    free(poll_ctx.events);
    poll_ctx.events = NULL;
    if (poll_ctx.base)
    {
        event_base_free(poll_ctx.base);
        poll_ctx.base = NULL;
    }
#endif

    memset(selected_candidate->addr_mapped, 0, sizeof(selected_candidate->addr_mapped));
    bool local = false;
    if(selected_candidate->type == CANDIDATE_TYPE_DERIVED)
    {
        char *search_ptr = strchr(selected_candidate->addr, '.');
        if(search_ptr)
        {
            if (strncmp(selected_candidate->addr, "10.", 4) == 0)
                local = true;
            else if (strncmp(selected_candidate->addr, "192.168.", 9) == 0)
                local = true;
            else
            {
                for (int j = 16; j < 32; j++)
                {
                    char compare_addr[9] = {0};
                    sprintf(compare_addr, "172.%d.", j);
                    if (strncmp(selected_candidate->addr, compare_addr, 9) == 0)
                    {
                        local = true;
                        break;
                    }
                }
            }
        }
        else
        {
            if (strncmp(selected_candidate->addr, "FC", 3) == 0)
                local = true;
            else if (strncmp(selected_candidate->addr, "fc", 3) == 0)
                local = true;
            else if (strncmp(selected_candidate->addr, "FD", 3) == 0)
                local = true;
            else if (strncmp(selected_candidate->addr, "fd", 3) == 0)
                local = true;
        }
    }
    if(selected_candidate->type == CANDIDATE_TYPE_LOCAL || local)
    {
        memcpy(selected_candidate->addr_mapped, local_candidate->addr, sizeof(local_candidate->addr));
        selected_candidate->port_mapped = local_candidate->port;
    }
    else
    {
        memcpy(selected_candidate->addr_mapped, remote_candidate->addr, sizeof(remote_candidate->addr));
        selected_candidate->port_mapped = remote_candidate->port;
    }

    memcpy(out_candidate, selected_candidate, sizeof(Candidate));
    session->ipv4_sock = CHIAKI_INVALID_SOCKET;
    session->ipv6_sock = CHIAKI_INVALID_SOCKET;
#ifdef __SWITCH__
    free(pollfds);
#endif
    free(socks);
    return CHIAKI_ERR_SUCCESS;

cleanup_sockets:
#if !defined(__SWITCH__) && !defined(__ANDROID__)
    for (size_t i = 0; poll_ctx.events && i < poll_ctx.events_count; i++)
        event_free(poll_ctx.events[i]);
    free(poll_ctx.events);
    poll_ctx.events = NULL;
    if (poll_ctx.base)
    {
        event_base_free(poll_ctx.base);
        poll_ctx.base = NULL;
    }
#endif
#ifdef __SWITCH__
    free(pollfds);
    pollfds = NULL;
#endif
    if(!CHIAKI_SOCKET_IS_INVALID(session->ipv4_sock))
    {
        CHIAKI_SOCKET_CLOSE(session->ipv4_sock);
        session->ipv4_sock = CHIAKI_INVALID_SOCKET;
    }
    if(!CHIAKI_SOCKET_IS_INVALID(session->ipv6_sock))
    {
        CHIAKI_SOCKET_CLOSE(session->ipv6_sock);
        session->ipv6_sock = CHIAKI_INVALID_SOCKET;
    }
    if(session->stun_random_allocation)
    {
        for(int j=0; j<socks_count; j++)
        {
            if(!CHIAKI_SOCKET_IS_INVALID(socks[j]))
            {
                CHIAKI_SOCKET_CLOSE(socks[j]);
                socks[j] = CHIAKI_INVALID_SOCKET;
            }
        }
    }
    free(socks);
    return err;
}

/**
 * Sends a response buf for a given request buf from the console
 *
 * @param session Pointer to session instance
 * @param[in] req Buffer with the request
 * @param[in] sock Pointer to a socket to use to send the response
 * @param[in] candidate Pointer to the candidate to send the response to
 * @return CHIAKI_ERR_SUCCESS on success or an error code on failure
 */
static ChiakiErrorCode send_response_ps(Session *session, uint8_t *req, chiaki_socket_t *sock, Candidate *candidate)
{
        uint8_t confirm_buf[88] = {0};
        *(uint32_t*)&confirm_buf[0] = htonl(MSG_TYPE_RESP);
        memcpy(&confirm_buf[0x4], session->hashed_id_local, sizeof(session->hashed_id_local));
        memcpy(&confirm_buf[0x24], session->hashed_id_console, sizeof(session->hashed_id_console));
        *(uint16_t*)&confirm_buf[0x44] = htons(session->sid_local);
        *(uint16_t*)&confirm_buf[0x46] = htons(session->sid_console);
        memcpy(&confirm_buf[0x4b], &req[0x4b], 5);
        uint8_t console_addr[16];
        uint8_t console_port[2];
        char *search_ptr = strchr(candidate->addr, '.');
        if(search_ptr)
        {
            if(inet_pton(AF_INET, candidate->addr, console_addr) <= 0)
            {
                CHIAKI_LOGE(session->log, "%s: inet_pton failed with error: " CHIAKI_SOCKET_ERROR_FMT "\n", candidate->addr, CHIAKI_SOCKET_ERROR_VALUE);
                return CHIAKI_ERR_INVALID_DATA;
            }
        }
        else
        {
            if(inet_pton(AF_INET6, candidate->addr, console_addr) <= 0)
            {
                CHIAKI_LOGE(session->log, "%s: inet_pton failed with error: " CHIAKI_SOCKET_ERROR_FMT "\n", candidate->addr, CHIAKI_SOCKET_ERROR_VALUE);
                return CHIAKI_ERR_INVALID_DATA;
            }
        }
        *(uint16_t*)console_port = htons(candidate->port);
        *(uint16_t*)&confirm_buf[0x50] = htons(session->sid_local);
        *(uint16_t*)&confirm_buf[0x52] = htons(session->sid_console);
        *(uint16_t*)&confirm_buf[0x54] = htons(session->sid_local);
        xor_bytes(&confirm_buf[0x50], console_addr, 4);
        xor_bytes(&confirm_buf[0x54], console_port, 2);

        if (send(*sock, (CHIAKI_SOCKET_BUF_TYPE) confirm_buf, sizeof(confirm_buf), 0) < 0)
        {
            CHIAKI_LOGE(session->log, "check_candidates: Sending confirmation failed for %s:%d with error: " CHIAKI_SOCKET_ERROR_FMT, candidate->addr, candidate->port, CHIAKI_SOCKET_ERROR_VALUE);
            return CHIAKI_ERR_NETWORK;
        }
        CHIAKI_LOGI(session->log, "Sent response to %s:%d", candidate->addr, candidate->port);
        return CHIAKI_ERR_SUCCESS;
}

/**
 * Sends a response buf for a given request buf from the console
 *
 * @param session Pointer to session instance
 * @param[in] req Buffer with the request
 * @param[in] sock Pointer to a socket to use to send the response
 * @param[in] candidate Pointer to the candidate to send the response to
 * @return CHIAKI_ERR_SUCCESS on success or an error code on failure
 */
static ChiakiErrorCode send_responseto_ps(Session *session, uint8_t *req, chiaki_socket_t *sock, Candidate *candidate, struct sockaddr *addr, socklen_t len)
{
        uint8_t confirm_buf[88] = {0};
        *(uint32_t*)&confirm_buf[0] = htonl(MSG_TYPE_RESP);
        memcpy(&confirm_buf[0x4], session->hashed_id_local, sizeof(session->hashed_id_local));
        memcpy(&confirm_buf[0x24], session->hashed_id_console, sizeof(session->hashed_id_console));
        *(uint16_t*)&confirm_buf[0x44] = htons(session->sid_local);
        *(uint16_t*)&confirm_buf[0x46] = htons(session->sid_console);
        memcpy(&confirm_buf[0x4b], &req[0x4b], 5);
        uint8_t console_addr[16];
        uint8_t console_port[2];
        char *search_ptr = strchr(candidate->addr, '.');
        if(search_ptr)
        {
            if(inet_pton(AF_INET, candidate->addr, console_addr) <= 0)
            {
                CHIAKI_LOGE(session->log, "%s: inet_pton failed with error: " CHIAKI_SOCKET_ERROR_FMT "\n", candidate->addr, CHIAKI_SOCKET_ERROR_VALUE);
                return CHIAKI_ERR_INVALID_DATA;
            }
        }
        else
        {
            if(inet_pton(AF_INET6, candidate->addr, console_addr) <= 0)
            {
                CHIAKI_LOGE(session->log, "%s: inet_pton failed with error: " CHIAKI_SOCKET_ERROR_FMT "\n", candidate->addr, CHIAKI_SOCKET_ERROR_VALUE);
                return CHIAKI_ERR_INVALID_DATA;
            }
        }
        *(uint16_t*)console_port = htons(candidate->port);
        *(uint16_t*)&confirm_buf[0x50] = htons(session->sid_local);
        *(uint16_t*)&confirm_buf[0x52] = htons(session->sid_console);
        *(uint16_t*)&confirm_buf[0x54] = htons(session->sid_local);
        xor_bytes(&confirm_buf[0x50], console_addr, 4);
        xor_bytes(&confirm_buf[0x54], console_port, 2);

        if (sendto(*sock, (CHIAKI_SOCKET_BUF_TYPE) confirm_buf, sizeof(confirm_buf), 0, addr, len) < 0)
        {
            CHIAKI_LOGE(session->log, "check_candidates: Sending confirmation failed for %s:%d with error: %s", candidate->addr, candidate->port, CHIAKI_SOCKET_ERROR_VALUE);
            return CHIAKI_ERR_NETWORK;
        }
        CHIAKI_LOGI(session->log, "Sent response to %s:%d", candidate->addr, candidate->port);
        return CHIAKI_ERR_SUCCESS;
}

/**
 * Receives a request and sends a response buf for that request
 *
 * @param session Pointer to session instance
 * @param[in] sock Pointer to a socket to use to send the response
 * @param[in] candidate Pointer to the candidate to send the response to
 * @param[in] timeout Amount of time to wait for next request
 * @return CHIAKI_ERR_SUCCESS on success or an error code on failure
 */
static ChiakiErrorCode receive_request_send_response_ps(Session *session, chiaki_socket_t *sock, Candidate *candidate, size_t timeout)
{
    CHIAKI_SSIZET_TYPE len = 0;
    uint8_t req[88] = {0};
    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
    bool received = false;
    // Wait for followup request from responsive candidate
    while (true)
    {
        err = chiaki_stop_pipe_select_single(&session->select_pipe, *sock, false, timeout * 1000);
        if(err == CHIAKI_ERR_TIMEOUT && received)
            return CHIAKI_ERR_SUCCESS;
        if(err != CHIAKI_ERR_SUCCESS)
            return err;

        len = recv(*sock, (CHIAKI_SOCKET_BUF_TYPE) req, sizeof(req), 0);
        if (len < 0)
        {
            CHIAKI_LOGE(session->log, "check_candidates: Receiving response from %s:%d failed with error: " CHIAKI_SOCKET_ERROR_FMT, candidate->addr, candidate->port, CHIAKI_SOCKET_ERROR_VALUE);
            continue;
        }
        if (len != sizeof(req))
        {
            CHIAKI_LOGE(session->log, "check_candidates: Received request of unexpected size %zd from %s:%d", len, candidate->addr, candidate->port);
            return CHIAKI_ERR_NETWORK;
        }
        uint32_t msg_type = ntohl(*(uint32_t*)(req));
        if(msg_type == MSG_TYPE_RESP)
        {
            CHIAKI_LOGI(session->log, "Received an extra response, ignoring....");
            continue;
        }
        else if(msg_type != MSG_TYPE_REQ)
        {
            CHIAKI_LOGE(session->log, "check_candidates: Received response of unexpected type %"PRIu32" from %s:%d", msg_type, candidate->addr, candidate->port);
            chiaki_log_hexdump(session->log, CHIAKI_LOG_ERROR, req, 88);
            err = CHIAKI_ERR_UNKNOWN;
            return err;
        }
        received = true;

        err = send_response_ps(session, req, sock, candidate);
        if(err != CHIAKI_ERR_SUCCESS)
            return err;
    }
}

/**
 * Log the current session state
 *
 * @param[in] session A pointer to the session context
*/
static void log_session_state(Session *session)
{
    char state_str[1024];
    state_str[0] = '[';
    state_str[1] = '\0';
    if (session->state & SESSION_STATE_INIT)
        strcat(state_str, " ✅INIT");
    if (session->state & SESSION_STATE_WS_OPEN)
        strcat(state_str, " ✅WS_OPEN");
    if (session->state & SESSION_STATE_DELETED)
        strcat(state_str, " ✅DELETED");
    if (session->state & SESSION_STATE_CREATED)
        strcat(state_str, " ✅CREATED");
    if (session->state & SESSION_STATE_STARTED)
        strcat(state_str, " ✅STARTED");
    if (session->state & SESSION_STATE_CLIENT_JOINED)
        strcat(state_str, " ✅CLIENT_JOINED");
    if (session->state & SESSION_STATE_DATA_SENT)
        strcat(state_str, " ✅DATA_SENT");
    if (session->state & SESSION_STATE_CONSOLE_JOINED)
        strcat(state_str, " ✅CONSOLE_JOINED");
    if (session->state & SESSION_STATE_CUSTOMDATA1_RECEIVED)
        strcat(state_str, " ✅CUSTOMDATA1_RECEIVED");
    if (session->state &SESSION_STATE_CTRL_OFFER_RECEIVED)
        strcat(state_str, " ✅CTRL_OFFER_RECEIVED");
    if (session->state & SESSION_STATE_CTRL_OFFER_SENT)
        strcat(state_str, " ✅CTRL_OFFER_SENT");
    if (session->state & SESSION_STATE_CTRL_CONSOLE_ACCEPTED)
        strcat(state_str, " ✅CTRL_CONSOLE_ACCEPTED");
    if (session->state & SESSION_STATE_CTRL_CLIENT_ACCEPTED)
        strcat(state_str, " ✅CTRL_CLIENT_ACCEPTED");
    if (session->state & SESSION_STATE_CTRL_ESTABLISHED)
        strcat(state_str, " ✅CTRL_ESTABLISHED");
    if (session->state & SESSION_STATE_DATA_OFFER_RECEIVED)
        strcat(state_str, " ✅DATA_OFFER_RECEIVED");
    if (session->state & SESSION_STATE_DATA_OFFER_SENT)
        strcat(state_str, " ✅DATA_OFFER_SENT");
    if (session->state & SESSION_STATE_DATA_CONSOLE_ACCEPTED)
        strcat(state_str, " ✅DATA_CONSOLE_ACCEPTED");
    if (session->state & SESSION_STATE_DATA_CLIENT_ACCEPTED)
        strcat(state_str, " ✅DATA_CLIENT_ACCEPTED");
    if (session->state & SESSION_STATE_DATA_ESTABLISHED)
        strcat(state_str, " ✅DATA_ESTABLISHED");
    strcat(state_str, " ]");
    CHIAKI_LOGV(session->log, "Holepunch session state: %d = %s", session->state, state_str);
}

/**
 * Decode the customdata1 for use
 *
 * @param[in] customdata1 A char pointer to the customdata1 that arrived via the websocket
 * @param[in] log Pointer to a ChiakiLog used for reporting unexpected lengths
 * @param[out] out The decoded customdata1 for use in the remote registration
 * @param[in] out_len The length of the decoded customdata1
 */

#define CUSTOMDATA1_EXTRA_BYTES_MAX 4

static ChiakiErrorCode decode_customdata1(ChiakiLog *log, const char *customdata1, uint8_t *out, size_t out_len)
{
    uint8_t customdata1_round1[24];
    uint8_t customdata1_round2[24];
    size_t round1_len = sizeof(customdata1_round1);
    size_t round2_len = sizeof(customdata1_round2);
    ChiakiErrorCode err = chiaki_base64_decode(customdata1, strlen(customdata1), customdata1_round1, &round1_len);
    if (err != CHIAKI_ERR_SUCCESS)
        return err;
    err = chiaki_base64_decode((const char*)customdata1_round1, round1_len, customdata1_round2, &round2_len);
    if (err != CHIAKI_ERR_SUCCESS)
        return err;
    if (round2_len < out_len)
        return CHIAKI_ERR_UNKNOWN;
    if (round2_len > out_len + CUSTOMDATA1_EXTRA_BYTES_MAX)
    {
        CHIAKI_LOGV(log, "decode_customdata1: customData1 decoded to %zu bytes (max %zu)", round2_len, out_len + CUSTOMDATA1_EXTRA_BYTES_MAX);
        return CHIAKI_ERR_UNKNOWN;
    }
    if (round2_len > out_len)
        CHIAKI_LOGI(log, "decode_customdata1: customData1 contains %zu extra byte(s); ignoring extras", round2_len - out_len);
    memcpy(out, customdata1_round2, out_len);
    return CHIAKI_ERR_SUCCESS;
}

/**
 * Get the SessionMessage json from the payload field of the message that arrivved over the websocket
 *
 * @param[in] log Pointer to a ChiakiLog object for logging
 * @param session_message The payload field json that is transformed into the session message json via parsing
*/

static json_object* session_message_get_payload(ChiakiLog *log, json_object *session_message)
{
    json_object *payload_json;
    if (json_pointer_get(session_message, "/body/data/sessionMessage/payload", &payload_json) < 0)
    {
        CHIAKI_LOGE(log, "session_message_get_payload: Failed to get payload");
        CHIAKI_LOGV(log, json_object_to_json_string_ext(session_message, JSON_C_TO_STRING_PRETTY));
        return NULL;
    }

    if (!json_object_is_type(payload_json, json_type_string))
    {
        CHIAKI_LOGE(log, "session_message_get_payload: Payload is not a string");
        CHIAKI_LOGV(log, json_object_to_json_string_ext(session_message, JSON_C_TO_STRING_PRETTY));
        return NULL;
    }

    const char* payload_str = json_object_get_string(payload_json);

    char* body = strstr(payload_str, "body=");
    if (body == NULL) {
        CHIAKI_LOGE(log, "session_message_get_payload: Failed to find body of payload");
        CHIAKI_LOGV(log, payload_str);
        return NULL;
    }


    char *json = body + 5;
    // The JSON for a session message is kind of peculiar, as it's sometimes invalid JSON.
    // This happens when there is no value for the `localPeerAddr` field. Instead of the value
    // being `undefined` or the empty object, the field simply doesn't have a value, i.e. the
    // colon is immediately followed by a comma. This obviously breaks our parser, so we fix
    // the JSON if the field value is missing.
    json_object *message_json;
    char *peeraddr_key = "\"localPeerAddr\":";
    char *peeraddr_start = strstr(json, peeraddr_key);

    // No localPeerAddr, nothing to fix
    if (peeraddr_start == NULL)
        return json_tokener_parse(json);

    char *peeraddr_end = peeraddr_start + strlen(peeraddr_key);
    if (*peeraddr_end == '{')
    {
        // Valid JSON, we can parse without modifications
        message_json = json_tokener_parse(json);
    }
    else
    {
        // Insert empty object as key for localPeerAddr key
        size_t prefix_len = peeraddr_end - json;
        size_t suffix_len = strlen(peeraddr_end);
        char fixed_json[strlen(json) + 3]; // {} + \0
        memset(fixed_json, 0, sizeof(fixed_json));
        strncpy(fixed_json, json, prefix_len);
        fixed_json[prefix_len] = '{';
        fixed_json[prefix_len + 1] = '}';
        strncpy(fixed_json + prefix_len + 2, peeraddr_end, suffix_len);

        message_json = json_tokener_parse(fixed_json);
        if(message_json == NULL)
        {
            CHIAKI_LOGE(log, "Couldn't parse the following fixed json: %s", fixed_json);
            CHIAKI_LOGE(log, json_object_to_json_string_ext(payload_json, JSON_C_TO_STRING_PRETTY));
        }
    }
        // check if parse fails
        if(message_json == NULL)
        {
            CHIAKI_LOGE(log, "Couldn't parse the following json: %s", json);
            CHIAKI_LOGE(log, json_object_to_json_string_ext(payload_json, JSON_C_TO_STRING_PRETTY));
        }

    return message_json;
}

// static SessionMessageAction get_session_message_action(json_object *payload)
// {
//     json_object *action_json;
//     json_object_object_get_ex(payload, "action", &action_json);
//     assert(json_object_is_type(action_json, json_type_string));
//     const char *action = json_object_get_string(action_json);
//     if (strcmp(action, "OFFER") == 0)
//         return SESSION_MESSAGE_ACTION_OFFER;
//     else if (strcmp(action, "ACCEPT") == 0)
//         return SESSION_MESSAGE_ACTION_ACCEPT;
//     else if (strcmp(action, "TERMINATE") == 0)
//         return SESSION_MESSAGE_ACTION_TERMINATE;
//     else if (strcmp(action, "RESULT") == 0)
//         return SESSION_MESSAGE_ACTION_RESULT;
//     else
//         return SESSION_MESSAGE_ACTION_UNKNOWN;
// }

/**
 * Wait for notification to arrive
 *
 * @param[in] session Pointer to the session context
 * @param[out] out The new Notification object that's been created from the arrived json over the websocket
 * @param[in] types The types of notifications to look for (ORed together if multiple)
 * @param[in] timeout_ms The amount of time to wait before timing out
*/
static ChiakiErrorCode wait_for_notification(
    Session *session, Notification** out,
    uint16_t types, uint64_t timeout_ms)
{
    uint64_t waiting_since = chiaki_time_now_monotonic_us();
    uint64_t now = waiting_since;
    Notification *last_known = NULL;

    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
    chiaki_mutex_lock(&session->notif_mutex);
    while (true) {
        while (session->ws_notification_queue->rear == last_known || session->ws_notification_queue->rear == NULL)
        {
            err = chiaki_cond_timedwait(&session->notif_cond, &session->notif_mutex, timeout_ms);
            if (err == CHIAKI_ERR_TIMEOUT)
            {
                now = chiaki_time_now_monotonic_us();
                if ((now - waiting_since) > (timeout_ms * MILLISECONDS_US))
                {
                    CHIAKI_LOGE(session->log, "wait_for_notification: Timed out waiting for holepunch session messages");
                    err = CHIAKI_ERR_TIMEOUT;
                    goto cleanup;
                }
            }
            assert(err == CHIAKI_ERR_SUCCESS || err == CHIAKI_ERR_TIMEOUT);
            chiaki_mutex_lock(&session->stop_mutex);
            if(session->main_should_stop)
            {
                session->main_should_stop = false;
                chiaki_mutex_unlock(&session->stop_mutex);
                err = CHIAKI_ERR_CANCELED;
                goto cleanup;
            }
            chiaki_mutex_unlock(&session->stop_mutex);
        }
        Notification *notif;
        if(last_known)
            notif = last_known->next;
        else
            notif = session->ws_notification_queue->front;
        while (notif != NULL && notif != last_known)
        {
            if (notif->type & types)
            {
                CHIAKI_LOGV(session->log, "wait_for_notification: Found notification of type %d", notif->type);
                *out = notif;
                err = CHIAKI_ERR_SUCCESS;
                goto cleanup;
            }
            last_known = notif;
            notif = notif->next;
        }
    }

cleanup:
    chiaki_mutex_unlock(&session->notif_mutex);
    return err;
}

static ChiakiErrorCode clear_notification(
    Session *session, Notification *notification)
{
    NotificationQueue *nq = session->ws_notification_queue;
    chiaki_mutex_lock(&session->notif_mutex);
    Notification *prev = NULL;
    Notification *curr = nq->front;
    while (curr != NULL && curr != notification)
    {
        prev = curr;
        curr = curr->next;
    }

    if (curr == NULL)
    {
        chiaki_mutex_unlock(&session->notif_mutex);
        return CHIAKI_ERR_UNKNOWN;
    }

    if (prev)
        prev->next = curr->next;
    else
        nq->front = curr->next;

    if (curr == nq->rear)
        nq->rear = prev;

    json_object_put(curr->json);
    curr->json = NULL;
    free(curr->json_buf);
    curr->json_buf = NULL;
    free(curr);

    chiaki_mutex_unlock(&session->notif_mutex);
    return CHIAKI_ERR_SUCCESS;
}

/**
 * Wait for a SessionMessage to arrive
 *
 * @param[in] session Pointer to the session context
 * @param[out] out The new SessionMessage object that's been created from the arrived json over the websocket
 * @param[in] types The types of messages to look for (ORed together if multiple)
 * @param[in] timeout_ms The amount of time to wait before timing out
*/
static ChiakiErrorCode wait_for_session_message(
    Session *session, SessionMessage** out,
    uint16_t types, uint64_t timeout_ms)
{
    ChiakiErrorCode err;
    bool finished = false;
    Notification *notif = NULL;
    SessionMessage *msg = NULL;
    uint32_t notif_query = NOTIFICATION_TYPE_SESSION_MESSAGE_CREATED;
    while (!finished)
    {
        err = wait_for_notification(session, &notif, notif_query, SESSION_START_TIMEOUT_SEC * 1000);
        if (err == CHIAKI_ERR_TIMEOUT)
        {
            CHIAKI_LOGE(session->log, "Timed out waiting for holepunch session message notification.");
            return err;
        }
        else if (err != CHIAKI_ERR_SUCCESS)
        {
            CHIAKI_LOGE(session->log, "Failed to wait for holepunch session message notification.");
            return err;
        }
        chiaki_mutex_lock(&session->stop_mutex);
        if(session->main_should_stop)
        {
            session->main_should_stop = false;
            chiaki_mutex_unlock(&session->stop_mutex);
            err = CHIAKI_ERR_CANCELED;
            return err;
        }
        chiaki_mutex_unlock(&session->stop_mutex);
        json_object *payload = session_message_get_payload(session->log, notif->json);
        err = session_message_parse(session->log, payload, &msg);
        json_object_put(payload);
        if (err != CHIAKI_ERR_SUCCESS)
        {
            CHIAKI_LOGE(session->log, "Failed to parse holepunch session message");
            session_message_free(msg);
            return err;
        }
        if(msg->action & SESSION_MESSAGE_ACTION_TERMINATE)
        {
            CHIAKI_LOGW(session->log, "Holepunch session received Terminate message, terminating %d", msg->action);
            err = CHIAKI_ERR_CANCELED;
            session_message_free(msg);
            return err;
        }
        if (!(msg->action & types))
        {
            CHIAKI_LOGV(session->log, "Ignoring holepunch session message with action %d", msg->action);
            session_message_free(msg);
            msg = NULL;
            clear_notification(session, notif);
            continue;
        }
        finished = true;
    }
    msg->notification = notif;
    *out = msg;
    return CHIAKI_ERR_SUCCESS;
}

/**
 * Wait for an ack for a SessionMessage
 *
 * @param[in] session Pointer to the session context
 * @param[in] req_id The request id of the message to be acked (will also be the request id of the ack)
 * @param[in] timeout_ms The amount of time to wait before timing out
*/

static ChiakiErrorCode wait_for_session_message_ack(
    Session *session, int req_id, uint64_t timeout_ms)
{
    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
    SessionMessage *msg = NULL;
    uint32_t msg_query = SESSION_MESSAGE_ACTION_RESULT;
    bool finished = false;
    while (!finished)
    {
        err = wait_for_session_message(session, &msg, msg_query, SESSION_START_TIMEOUT_SEC * 1000);
        if (err == CHIAKI_ERR_TIMEOUT)
        {
            CHIAKI_LOGE(session->log, "wait_for_session_message_ack: Timed out waiting for holepunch session connection offer ACK notification.");
            return err;
        }
        else if (err != CHIAKI_ERR_SUCCESS)
        {
            CHIAKI_LOGE(session->log, "wait_for_session_message_ack: Failed to wait for holepunch session connection offer ACK notification.");
            return err;
        }
        chiaki_mutex_lock(&session->stop_mutex);
        if(session->main_should_stop)
        {
            session->main_should_stop = false;
            session_message_free(msg);
            chiaki_mutex_unlock(&session->stop_mutex);
            err = CHIAKI_ERR_CANCELED;
            return err;
        }
        if (msg->req_id != req_id)
        {
            CHIAKI_LOGE(session->log, "wait_for_session_message_ack: Got ACK for unexpected request ID %d", msg->req_id);
            session_message_free(msg);
            msg = NULL;
            chiaki_mutex_unlock(&session->stop_mutex);
            continue;
        }
        finished = true;
        chiaki_mutex_unlock(&session->stop_mutex);
        chiaki_mutex_lock(&session->notif_mutex);
        session_message_free(msg);
        chiaki_mutex_unlock(&session->notif_mutex);
    }

    return err;
}

/**
 * Parse the json object received over the websocket into a SessionMessage
 *
 * @param[in] log Pointer to a ChiakiLog object for logging
 * @param[in] message_json Pointer to the the json object to transform into a SessionMessage
 * @param[in] out Pointer to a pointer to the created SessionMessage
*/
static ChiakiErrorCode session_message_parse(
    ChiakiLog *log, json_object *message_json, SessionMessage **out)
{
    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
    SessionMessage *msg = calloc(1, sizeof(SessionMessage));
    if(!msg)
        return CHIAKI_ERR_MEMORY;
    msg->notification = NULL;

    json_object *action_json;
    json_object_object_get_ex(message_json, "action", &action_json);
    if (action_json == NULL || !json_object_is_type(action_json, json_type_string))
        goto invalid_schema;
    const char *action = json_object_get_string(action_json);
    if (strcmp(action, "OFFER") == 0)
        msg->action = SESSION_MESSAGE_ACTION_OFFER;
    else if (strcmp(action, "ACCEPT") == 0)
        msg->action = SESSION_MESSAGE_ACTION_ACCEPT;
    else if (strcmp(action, "TERMINATE") == 0)
        msg->action = SESSION_MESSAGE_ACTION_TERMINATE;
    else if (strcmp(action, "RESULT") == 0)
        msg->action = SESSION_MESSAGE_ACTION_RESULT;
    else
        msg->action = SESSION_MESSAGE_ACTION_UNKNOWN;

    json_object *reqid_json = NULL;
    json_object_object_get_ex(message_json, "reqId", &reqid_json);
    if (reqid_json == NULL || !json_object_is_type(reqid_json, json_type_int))
    {
        CHIAKI_LOGE(log, "Coudln't parse reqid field from message json.");
        goto invalid_schema;
    }
    msg->req_id = json_object_get_int(reqid_json);

    json_object *error_json = NULL;
    json_object_object_get_ex(message_json, "error", &error_json);
    if (error_json == NULL || !json_object_is_type(error_json, json_type_int))
    {
        CHIAKI_LOGE(log, "Coudln't parse error field from message json.");
        goto invalid_schema;
    }
    msg->error = json_object_get_int(error_json);

    json_object *conn_request_json = NULL;
    json_object_object_get_ex(message_json, "connRequest", &conn_request_json);
    if (conn_request_json == NULL || !json_object_is_type(conn_request_json, json_type_object))
    {
        CHIAKI_LOGE(log, "Coudln't parse connRequest field from message json.");
        goto invalid_schema;
    }
    if (json_object_object_length(conn_request_json) > 0)
    {
        msg->conn_request = calloc(1, sizeof(ConnectionRequest));
        if(!msg->conn_request)
        {
            err = CHIAKI_ERR_MEMORY;
            goto cleanup;
        }

        json_object *obj;
        json_object_object_get_ex(conn_request_json, "sid", &obj);
        if (obj == NULL || !json_object_is_type(obj, json_type_int))
        {
            CHIAKI_LOGE(log, "Coudln't parse sid field from connection request json.");
            goto invalid_schema;
        }
        msg->conn_request->sid = json_object_get_int(obj);

        json_object_object_get_ex(conn_request_json, "peerSid", &obj);
        if (obj == NULL || !json_object_is_type(obj, json_type_int))
        {
            CHIAKI_LOGE(log, "Coudln't parse peer sid field from connection request json.");
            goto invalid_schema;
        }
        msg->conn_request->peer_sid = json_object_get_int(obj);

        json_object_object_get_ex(conn_request_json, "skey", &obj);
        if (obj == NULL || !json_object_is_type(obj, json_type_string))
        {
            CHIAKI_LOGE(log, "Coudln't parse skey field from connection request json.");
            goto invalid_schema;
        }
        const char *skey_str = json_object_get_string(obj);
        size_t skey_len = strlen(skey_str);
        err = chiaki_base64_decode(skey_str, strlen(skey_str), msg->conn_request->skey, &skey_len);
        if (err != CHIAKI_ERR_SUCCESS)
        {
            CHIAKI_LOGE(log, "session_message_parse: Failed to decode skey: '%s'", skey_str);
            goto cleanup;
        }

        json_object_object_get_ex(conn_request_json, "natType", &obj);
        if (obj == NULL || !json_object_is_type(obj, json_type_int))
            goto invalid_schema;
        msg->conn_request->nat_type = json_object_get_int(obj);

        json_object_object_get_ex(conn_request_json, "defaultRouteMacAddr", &obj);
        if (obj == NULL || !json_object_is_type(obj, json_type_string))
        {
            CHIAKI_LOGE(log, "Coudln't parse defaultRouteMacAddr field from connection request json.");
            goto invalid_schema;
        }
        const char *mac_str = json_object_get_string(obj);
        if (strlen(mac_str) == 17)
        {
            // Parse MAC address
            char *end;
            for (int i = 0; i < 6; i++)
            {
                msg->conn_request->default_route_mac_addr[i] = strtoul(mac_str, &end, 16);
                if (i < 5)
                {
                    if (*end != ':')
                        goto invalid_schema;
                    mac_str = end + 1;
                }
            }
        }

        json_object_object_get_ex(conn_request_json, "localHashedId", &obj);
        if (obj == NULL || !json_object_is_type(obj, json_type_string))
        {
            CHIAKI_LOGE(log, "Coudln't parse localHashedId field from connection request json.");
            goto invalid_schema;
        }
        const char *local_hashed_id_str = json_object_get_string(obj);
        size_t local_hashed_id_len = sizeof(msg->conn_request->local_hashed_id);
        err = chiaki_base64_decode(
            local_hashed_id_str, strlen(local_hashed_id_str),
            msg->conn_request->local_hashed_id, &local_hashed_id_len);
        if (err != CHIAKI_ERR_SUCCESS)
        {
            CHIAKI_LOGE(log, "session_message_parse: Failed to decode localHashedId: '%s'", local_hashed_id_str);
            goto cleanup;
        }

        json_object_object_get_ex(conn_request_json, "candidate", &obj);
        if (obj == NULL || !json_object_is_type(obj, json_type_array))
        {
            CHIAKI_LOGE(log, "Coudln't parse candidate field from connection request json.");
            goto invalid_schema;
        }
        size_t num_candidates = json_object_array_length(obj);
        msg->conn_request->num_candidates = num_candidates;
        msg->conn_request->candidates = calloc(num_candidates, sizeof(Candidate));
        if(!msg->conn_request->candidates)
        {
            free(msg->conn_request);
            msg->conn_request = NULL;
            err = CHIAKI_ERR_MEMORY;
            goto cleanup;
        }
        for (size_t i = 0; i < num_candidates; i++)
        {
            Candidate candidate;
            json_object *candidate_json = json_object_array_get_idx(obj, i);

            json_object *jobj = NULL;
            json_object_object_get_ex(candidate_json, "type", &jobj);
            if (jobj == NULL || !json_object_is_type(jobj, json_type_string))
            {
                CHIAKI_LOGE(log, "Coudln't parse type field from candidate json.");
                goto invalid_schema;
            }
            const char *type_str = json_object_get_string(jobj);
            if (strcmp(type_str, "LOCAL") == 0)
                candidate.type = CANDIDATE_TYPE_LOCAL;
            else if (strcmp(type_str, "STUN") == 0)
                candidate.type = CANDIDATE_TYPE_STUN;
            else if (strcmp(type_str, "DERIVED") == 0)
                candidate.type = CANDIDATE_TYPE_DERIVED;
            else
                candidate.type = CANDIDATE_TYPE_STATIC;

            json_object_object_get_ex(candidate_json, "addr", &jobj);
            if (jobj == NULL || !json_object_is_type(jobj, json_type_string))
            {
                CHIAKI_LOGE(log, "Coudln't parse addr field from candidate json.");
                goto invalid_schema;
            }
            const char *addr_str = json_object_get_string(jobj);
            strncpy(candidate.addr, addr_str, sizeof(candidate.addr));

            json_object_object_get_ex(candidate_json, "mappedAddr", &jobj);
            if (jobj == NULL || !json_object_is_type(jobj, json_type_string))
            {
                CHIAKI_LOGE(log, "Coudln't parse mappedAddr field from candidate json.");
                goto invalid_schema;
            }
            const char *mapped_addr_str = json_object_get_string(jobj);
            strncpy(candidate.addr_mapped, mapped_addr_str, sizeof(candidate.addr_mapped));

            json_object_object_get_ex(candidate_json, "port", &jobj);
            if (jobj == NULL || !json_object_is_type(jobj, json_type_int))
            {
                CHIAKI_LOGE(log, "Coudln't parse port field from candidate json.");
                goto invalid_schema;
            }
            candidate.port = json_object_get_int(jobj);

            json_object_object_get_ex(candidate_json, "mappedPort", &jobj);
            if (jobj == NULL || !json_object_is_type(jobj, json_type_int))
            {
                CHIAKI_LOGE(log, "Coudln't parse mapped port field from candidate json.");
                goto invalid_schema;
            }
            candidate.port_mapped = json_object_get_int(jobj);
            memcpy(&msg->conn_request->candidates[i], &candidate, sizeof(Candidate));
        }
    }
    *out = msg;
    goto cleanup;

invalid_schema:
    CHIAKI_LOGE(log, "session_message_parse: Unexpected JSON schema for holepunch session message.");
    CHIAKI_LOGV(log, json_object_to_json_string_ext(message_json, JSON_C_TO_STRING_PRETTY));
    err = CHIAKI_ERR_UNKNOWN;

cleanup:
    if (msg != NULL && err != CHIAKI_ERR_SUCCESS)
    {
        session_message_free(msg);
        msg = NULL;
    }
    return err;
}

/**
 * Serialize a session message into a array to send over the websocket
 *
 * @param[in] session Pointer to the session context
 * @param[in] message Pointer to the session message to serialize
 * @param[out] out Pointer to the the serialized msg array
 * @param[out] out_len Pointer to the sizse of the serialized msg array
*/
static ChiakiErrorCode session_message_serialize(
    Session *session, SessionMessage *message, char **out, size_t *out_len)
{
    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;

    // Since the official remote play app doesn't send valid JSON half the time,
    // we can't use a proper JSON library to serialize the message. Instead, we
    // use snprintf to build the JSON string manually.
    char localpeeraddr_json[128] = {0};
    size_t localpeeraddr_len = snprintf(
        localpeeraddr_json, sizeof(localpeeraddr_json), session_localpeeraddr_fmt,
        session->account_id, "REMOTE_PLAY");

    size_t candidate_str_len = sizeof(session_connrequest_candidate_fmt) * 2;
    char *candidates_json = calloc(1, candidate_str_len * message->conn_request->num_candidates + 3);
    if(!candidates_json)
        return CHIAKI_ERR_MEMORY;
    *(candidates_json) = '[';
    size_t candidates_len = 1;
    char *candidate_str = calloc(1, candidate_str_len);
    if(!candidate_str)
    {
        free(candidates_json);
        return CHIAKI_ERR_MEMORY;
    }
    for (int i=0; i < message->conn_request->num_candidates; i++)
    {
        Candidate candidate = message->conn_request->candidates[i];
        char candidate_type[8] = {0};
        switch(candidate.type)
        {
            case CANDIDATE_TYPE_LOCAL:
                strcpy(candidate_type, "LOCAL");
                break;
            case CANDIDATE_TYPE_STATIC:
                strcpy(candidate_type, "STATIC");
                break;
            case CANDIDATE_TYPE_STUN:
                strcpy(candidate_type, "STUN");
                break;
            case CANDIDATE_TYPE_DERIVED:
                strcpy(candidate_type, "DERIVED");
                break;
            default:
                CHIAKI_LOGE(session->log, "Undefined candidate type %d", candidate.type);
                err = CHIAKI_ERR_INVALID_DATA;
                goto cleanup_candidate_initial;
        }
        memset(candidate_str, 0, candidate_str_len);
        size_t candidate_len = snprintf(
            candidate_str, candidate_str_len, session_connrequest_candidate_fmt,
            candidate_type, candidate.addr, candidate.addr_mapped, candidate.port,
            candidate.port_mapped);
        snprintf(candidates_json + candidates_len, candidate_len, "%s", candidate_str);
        // Take into account that char arrays are null-terminated
        if(i < (message->conn_request->num_candidates - 1))
        {
            *(candidates_json + candidates_len + candidate_len - 1) = '}';
            *(candidates_json + candidates_len + candidate_len) = ',';
            candidates_len += candidate_len + 1;
        }
        else
        {
            *(candidates_json + candidates_len + candidate_len - 1) = '}';
            candidates_len += candidate_len;
        }
    }
    *(candidates_json + candidates_len) = ']';
    *(candidates_json + candidates_len + 1) = '\0';
    candidates_len += 2;

    char localhashedid_str[29] = {0};
    uint8_t zero_bytes0[sizeof(message->conn_request->local_hashed_id)] = {0};
    if(memcmp(message->conn_request->local_hashed_id, zero_bytes0, sizeof(zero_bytes0)) == 0)
        localhashedid_str[0] = '\0';
    else
    {
        err = chiaki_base64_encode(
            message->conn_request->local_hashed_id, sizeof(message->conn_request->local_hashed_id),
            localhashedid_str, sizeof(localhashedid_str));
        if(err != CHIAKI_ERR_SUCCESS)
        {
            CHIAKI_LOGE(session->log, "session_message_serialize: Could not encode hashed_id into base64.");
            goto cleanup_candidate_initial;
        }
    }
    char skey_str[25] = {0};
    err = chiaki_base64_encode (
        message->conn_request->skey, sizeof(message->conn_request->skey), skey_str, sizeof(skey_str));
    if(err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(session->log, "session_message_serialize: Could not encode skey into base64.");
        goto cleanup_candidate_initial;
    }
    size_t connreq_json_len = sizeof(session_connrequest_fmt) * 2 + localpeeraddr_len + candidates_len;
    char *connreq_json = calloc(
        1, connreq_json_len);
    if(!connreq_json)
    {
        err = CHIAKI_ERR_MEMORY;
        goto cleanup_candidate_initial;
    }
    char mac_addr[1] = { '\0' };
    CHIAKI_SSIZET_TYPE connreq_len = snprintf(
        connreq_json, connreq_json_len, session_connrequest_fmt,
        message->conn_request->sid, message->conn_request->peer_sid,
        skey_str, message->conn_request->nat_type,
        candidates_json, mac_addr,
        localpeeraddr_json, localhashedid_str);

    char* action_str;
    switch (message->action)
    {
        case SESSION_MESSAGE_ACTION_OFFER:
            action_str = "OFFER";
            break;
        case SESSION_MESSAGE_ACTION_ACCEPT:
            action_str = "ACCEPT";
            break;
        case SESSION_MESSAGE_ACTION_TERMINATE:
            action_str = "TERMINATE";
            break;
        case SESSION_MESSAGE_ACTION_RESULT:
            action_str = "RESULT";
            break;
        default:
            action_str = "UNKNOWN";
            break;
    }
    CHIAKI_SSIZET_TYPE serialized_msg_len = sizeof(session_message_envelope_fmt) * 2 + connreq_len;
    char *serialized_msg = calloc(1, serialized_msg_len);
    if(!serialized_msg)
    {
        err = CHIAKI_ERR_MEMORY;
        goto cleanup;
    }
    CHIAKI_SSIZET_TYPE msg_len = snprintf(
        serialized_msg, serialized_msg_len, session_message_fmt,
        action_str, message->req_id, message->error, connreq_json);

    *out = serialized_msg;
    *out_len = msg_len;

cleanup:
    free(connreq_json);
cleanup_candidate_initial:
    free(candidate_str);
    free(candidates_json);

    return err;
}

/**
 * Serialize a short message used for acks
 *
 * @param[in] session Pointer to the session context
 * @param[in] message Pointer to the session message to serialize
 * @param[out] out Pointer to the the serialized msg array
 * @param[out] out_len Pointer to the sizse of the serialized msg array
*/
static ChiakiErrorCode short_message_serialize(
    Session *session, SessionMessage *message, char **out, size_t *out_len)
{
    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;

    // Since the official remote play app doesn't send valid JSON half the time,
    // we can't use a proper JSON library to serialize the message. Instead, we
    // use snprintf to build the JSON string manually.

    char connreq_json[3] = { '{', '}', '\0' };
    size_t connreq_len = sizeof(connreq_json);

    char* action_str;
    switch (message->action)
    {
        case SESSION_MESSAGE_ACTION_OFFER:
            action_str = "OFFER";
            break;
        case SESSION_MESSAGE_ACTION_ACCEPT:
            action_str = "ACCEPT";
            break;
        case SESSION_MESSAGE_ACTION_TERMINATE:
            action_str = "TERMINATE";
            break;
        case SESSION_MESSAGE_ACTION_RESULT:
            action_str = "RESULT";
            break;
        default:
            action_str = "UNKNOWN";
            break;
    }
    CHIAKI_SSIZET_TYPE serialized_msg_len = sizeof(session_message_envelope_fmt) * 2 + connreq_len;
    char *serialized_msg = calloc(1, serialized_msg_len);
    if(!serialized_msg)
        return CHIAKI_ERR_MEMORY;
    CHIAKI_SSIZET_TYPE msg_len = snprintf(
        serialized_msg, serialized_msg_len, session_message_fmt,
        action_str, message->req_id, message->error, connreq_json);

    *out = serialized_msg;
    *out_len = msg_len;

    return err;
}

/**
 * Frees a session message
 *
 * @param message The message to free
*/
static ChiakiErrorCode session_message_free(SessionMessage *message)
{
    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
    if(message)
    {
        if (message->conn_request != NULL)
        {
            if (message->conn_request->candidates != NULL)
                free(message->conn_request->candidates);
            free(message->conn_request);
        }
        message->notification = NULL;
        free(message);
    }
    return err;
}

/**
 * Gets stun servers from updated list of online STUN servers
 *
 * @param session Pointer to the holepunch session
 * @return ChiakiErrSuccess on success or error code on failure
*/
static ChiakiErrorCode get_stun_servers(Session *session)
{
    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
    const char STUN_HOSTS_URL[] = "https://raw.githubusercontent.com/pradt2/always-online-stun/master/valid_hosts.txt";
    CURL *curl = holepunch_curl_easy_init();
    if(!curl)
    {
        CHIAKI_LOGE(session->log, "Curl could not init");
        return CHIAKI_ERR_MEMORY;
    }

    HttpResponseData response_data = {
        .data = malloc(0),
        .size = 0,
    };

    CURLcode res = curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "get_stun_servers: CURL setopt CURLOPT_FAILONERROR failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "get_stun_servers: CURL setopt CURLOPT_TIMEOUT failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_URL, STUN_HOSTS_URL);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "get_stun_servers: CURL setopt CURLOPT_URL failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "get_stun_servers: CURL setopt CURLOPT_WRITEFUNCTION failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_data);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "get_stun_servers: CURL setopt CURLOPT_WRITEDATA failed with CURL error %s", curl_easy_strerror(res));

    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        if (res == CURLE_HTTP_RETURNED_ERROR)
        {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            CHIAKI_LOGE(session->log, "Getting stun servers from %s failed with HTTP code %ld", STUN_HOSTS_URL, http_code);
            CHIAKI_LOGV(session->log, "Response Body: %.*s.", (int)response_data.size, response_data.data);
            err = CHIAKI_ERR_HTTP_NONOK;
        } else {
            CHIAKI_LOGE(session->log, "Getting stun servers from %s failed with CURL error %s", STUN_HOSTS_URL, curl_easy_strerror(res));
            err = CHIAKI_ERR_NETWORK;
        }
        goto cleanup;
    }
    // hostname has max of 253 chars + 1 char for colon : + port has max of 4 chars + 1 char for null termination
    char server_strings[10][259];
    char *ptr = strtok(response_data.data, "\n");
    while(ptr != NULL && session->num_stun_servers <= 9)
    {
        strcpy(server_strings[session->num_stun_servers], ptr);
        session->num_stun_servers++;
		ptr = strtok(NULL, "\n");
    }
    ptr = NULL;
    for(int i = 0; i < session->num_stun_servers; i++)
    {
        ptr = strtok(server_strings[i], ":");
        if(ptr == NULL)
        {
            CHIAKI_LOGW(session->log, "Problem reading stun server list host");
            session->num_stun_servers = i;
            return CHIAKI_ERR_INVALID_DATA;
        }
        session->stun_server_list[i].host = malloc((strlen(ptr) + 1) * sizeof(char));
        if(!session->stun_server_list[i].host)
        {
            CHIAKI_LOGW(session->log, "Problem allocating memory for stun server list host");
            session->num_stun_servers = i;
            return CHIAKI_ERR_MEMORY;
        }
        strcpy(session->stun_server_list[i].host, ptr);
        ptr = strtok(NULL, ":");
        if(ptr == NULL)
        {
            CHIAKI_LOGW(session->log, "Problem reading stun server list port");
            session->num_stun_servers = i;
            return CHIAKI_ERR_INVALID_DATA;
        }
        session->stun_server_list[i].port = strtol(ptr, NULL, 10);
        ptr = NULL;
    }

    free(response_data.data);
    response_data.data = malloc(0);
    response_data.size = 0;
    curl_easy_cleanup(curl);
    curl = NULL;
    const char STUN_HOSTS_URL_IPV6[] = "https://raw.githubusercontent.com/pradt2/always-online-stun/master/valid_ipv6s.txt";
    curl = holepunch_curl_easy_init();
    if(!curl)
    {
        CHIAKI_LOGE(session->log, "Curl could not init");
        return CHIAKI_ERR_MEMORY;
    }

    res = curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "get_stun_servers: CURL setopt CURLOPT_FAILONERROR failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "get_stun_servers: CURL setopt CURLOPT_TIMEOUT failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_URL, STUN_HOSTS_URL_IPV6);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "get_stun_servers: CURL setopt CURLOPT_URL failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "get_stun_servers: CURL setopt CURLOPT_WRITEFUNCTION failed with CURL error %s", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_data);
    if(res != CURLE_OK)
        CHIAKI_LOGW(session->log, "get_stun_servers: CURL setopt CURLOPT_WRITEDATA failed with CURL error %s", curl_easy_strerror(res));

    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        if (res == CURLE_HTTP_RETURNED_ERROR)
        {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            CHIAKI_LOGE(session->log, "Getting IPV6 stun servers from %s failed with HTTP code %ld", STUN_HOSTS_URL, http_code);
            CHIAKI_LOGV(session->log, "Response Body: %.*s.", (int)response_data.size, response_data.data);
            err = CHIAKI_ERR_HTTP_NONOK;
        } else {
            CHIAKI_LOGE(session->log, "Getting IPV6 stun servers from %s failed with CURL error %s", STUN_HOSTS_URL, curl_easy_strerror(res));
            err = CHIAKI_ERR_NETWORK;
        }
        goto cleanup;
    }
    // ipv6 string has max of 45 chars: 39 chars + 2 chars for [] + 1 char for colon : + port has max of 4 chars + 1 char for null termination
    char server_strings_ipv6[10][47];
    ptr = strtok(response_data.data, "\n");
    while(ptr != NULL && session->num_stun_servers_ipv6 <= 9)
    {
        // omit leading [
        strcpy(server_strings_ipv6[session->num_stun_servers_ipv6], ptr + 1);
        session->num_stun_servers_ipv6++;
		ptr = strtok(NULL, "\n");
    }
    ptr = NULL;
    for(int i = 0; i < session->num_stun_servers_ipv6; i++)
    {
        ptr = strtok(server_strings_ipv6[i], "]");
        if(ptr == NULL)
        {
            CHIAKI_LOGW(session->log, "Problem reading stun server list host");
            session->num_stun_servers_ipv6 = i;
            return CHIAKI_ERR_INVALID_DATA;
        }
        session->stun_server_list_ipv6[i].host = malloc((strlen(ptr) + 1) * sizeof(char));
        if(!session->stun_server_list_ipv6[i].host)
        {
            CHIAKI_LOGW(session->log, "Problem allocating memory for stun server list host");
            session->num_stun_servers_ipv6 = i;
            return CHIAKI_ERR_MEMORY;
        }
        strcpy(session->stun_server_list_ipv6[i].host, ptr);
        ptr = strtok(NULL, "]");
        if(ptr == NULL)
        {
            CHIAKI_LOGW(session->log, "Problem reading stun server list port");
            session->num_stun_servers_ipv6 = i;
            return CHIAKI_ERR_INVALID_DATA;
        }
        // omit :
        session->stun_server_list_ipv6[i].port = strtol(ptr + 1, NULL, 10);
        ptr = NULL;
    }

cleanup:
    free(response_data.data);
    curl_easy_cleanup(curl);
    return err;
}

/**
 * Prints a session request
 *
 * @param[in] log pointer to a Chiaki log instance
 * @param[in] req Pointer to the ConnectionRequest that will be printed.
*/

static void print_session_request(ChiakiLog *log, ConnectionRequest *req)
{
    CHIAKI_LOGV(log, "-----------------CONNECTION REQUEST---------------------");
    CHIAKI_LOGV(log, "sid: %"PRIu32, req->sid);
    CHIAKI_LOGV(log, "peer_sid: %"PRIu32, req->peer_sid);
    char skey[25];
    ChiakiErrorCode err = chiaki_base64_encode(req->skey, sizeof(req->skey), skey, sizeof(skey));
    if(err != CHIAKI_ERR_SUCCESS)
    {
        char hex[33];
        bytes_to_hex(req->skey, sizeof(req->skey), hex, sizeof(hex));
        CHIAKI_LOGE(log, "Error with base64 encoding of skey: %s", hex);
    }
    CHIAKI_LOGV(log, "skey: %s", skey);
    CHIAKI_LOGV(log, "nat type %u", req->nat_type);
    uint8_t zero_bytes0[sizeof(req->default_route_mac_addr)] = {0};
    if(memcmp(zero_bytes0, req->default_route_mac_addr, sizeof(req->default_route_mac_addr)) != 0)
    {
        char mac_addr[18] = {0};
        uint8_t *mac = req->default_route_mac_addr;
        snprintf(mac_addr, sizeof(mac_addr), "%02x:%02x:%02x:%02x:%02x:%02x",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        CHIAKI_LOGV(log, "default_route_mac_addr: %s", mac_addr);
    }
    uint8_t zero_bytes[sizeof(req->local_hashed_id)] = {0};
    if(memcmp(req->local_hashed_id, zero_bytes, sizeof(req->local_hashed_id)) != 0)
    {
        char local_hashed_id[29];
        err = chiaki_base64_encode(req->local_hashed_id, sizeof(req->local_hashed_id), local_hashed_id, sizeof(local_hashed_id));
        if(err != CHIAKI_ERR_SUCCESS)
        {
            char hex[41];
            bytes_to_hex(req->local_hashed_id, sizeof(req->local_hashed_id), hex, sizeof(hex));
            CHIAKI_LOGE(log, "Error with base64 encoding of local hashed id: %s", hex);
        }
        CHIAKI_LOGV(log, "local hashed id %s", local_hashed_id);
    }
}

/**
 * Prints a candidate
 *
 * @param[in] log pointer to a Chiaki log instance
 * @param[in] candidate Pointer to the Candidate that will be printed.
*/

static void print_candidate(ChiakiLog *log, Candidate *candidate)
{
    switch(candidate->type)
    {
        case CANDIDATE_TYPE_LOCAL:
            CHIAKI_LOGV(log, "--------------LOCAL CANDIDATE---------------------");
            break;
        case CANDIDATE_TYPE_STATIC:
            CHIAKI_LOGV(log, "--------------REMOTE CANDIDATE--------------------");
            break;
        case CANDIDATE_TYPE_DERIVED:
            CHIAKI_LOGV(log, "--------------DERIVED CANDIDATE--------------------");
            break;
        case CANDIDATE_TYPE_STUN:
            CHIAKI_LOGV(log, "--------------STUN CANDIDATE--------------------");
            break;
        default:
            CHIAKI_LOGV(log, "--------------CANDIDATE TYPE UNKNOWN--------------");
            break;    
    }
    CHIAKI_LOGV(log, "Address: %s", candidate->addr);
    CHIAKI_LOGV(log, "Mapped Address: %s", candidate->addr_mapped);
    CHIAKI_LOGV(log, "Port: %u", candidate->port);
    CHIAKI_LOGV(log, "Mapped Port: %u", candidate->port_mapped);
}

/**
 * Create a notification queue for holding notifications for a session.
 *
 * @return nq Pointer to a NotificationQueue to be used for the session.
*/

static NotificationQueue *createNq()
{
    NotificationQueue *nq = (NotificationQueue*)malloc(sizeof(NotificationQueue));
    if(!nq)
        return NULL;
    nq->front = nq->rear = NULL;
    return nq;
}

/**
 * Delete a notification from the queue
 *
 * @param nq the notification queue to dequeue from
*/

static void dequeueNq(NotificationQueue *nq)
{
    if(nq->front == NULL)
        return;

    Notification *notif = nq->front;

    nq->front = nq->front->next;

    if(nq->front == NULL)
        nq->rear = NULL;

    json_object_put(notif->json);
    notif->json = NULL;
    free(notif->json_buf);
    notif->json_buf = NULL;
    free(notif);
}

/**
 * Adds a notification to the queue.
 *
 * @param nq Notification queue to add to
 * @param[in] notif Notification to add to the queue
*/

static void enqueueNq(NotificationQueue *nq, Notification*notif)
{
    if(nq->rear == NULL)
    {
        nq->front = nq->rear = notif;
        return;
    }
    nq->rear->next = notif;
    nq->rear = notif;
}

/**
 * Creates a notification
 *
 * @param[in] type The type of notification
 * @param[in] json The pointer to the json object of the notification
 * @param[in] json_buf A pointer to the char representation of the json
 * @param[in] json_buf_size The length of the char representation of the json
 * @return notif Created notification
*/
static Notification* newNotification(NotificationType type, json_object *json, char* json_buf, size_t json_buf_size)
{
    Notification *notif = (Notification *)malloc(sizeof(Notification));
    if(!notif)
        return NULL;
    notif->type = type;
    notif->json = json;
    notif->json_buf = json_buf;
    notif->json_buf_size = json_buf_size;
    notif->next = NULL;
    return notif;
}

/**
 * Removes a substring from a string
 *
 * @param string The string to remove the substring from
 * @param[in] substring The substring to remove from the string
*/
static void remove_substring(char *str, char *substring)
{
    char *start = strstr(str, substring);
    // substring doesn't exist within string we're done
    if(start == NULL)
        return;
    char *end = start + strlen(substring);
    memmove(start, start + strlen(substring), strlen(end) + 1);
}
