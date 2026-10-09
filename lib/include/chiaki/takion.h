// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_TAKION_H
#define CHIAKI_TAKION_H

#include "common.h"
#include "thread.h"
#include "log.h"
#include "gkcrypt.h"
#include "seqnum.h"
#include "stoppipe.h"
#include "reorderqueue.h"
#include "feedback.h"
#include "takionsendbuffer.h"

#include <stdbool.h>

#ifdef _WIN32
#include <winsock2.h>
#endif


#ifdef __cplusplus
extern "C" {
#endif

typedef enum chiaki_takion_message_data_type_t {
	CHIAKI_TAKION_MESSAGE_DATA_TYPE_PROTOBUF = 0,
	CHIAKI_TAKION_MESSAGE_DATA_TYPE_RUMBLE = 7,
	CHIAKI_TAKION_MESSAGE_DATA_TYPE_PAD_INFO = 9,
	CHIAKI_TAKION_MESSAGE_DATA_TYPE_TRIGGER_EFFECTS = 11,
} ChiakiTakionMessageDataType;

typedef struct chiaki_takion_av_packet_t
{
	ChiakiSeqNum16 packet_index;
	ChiakiSeqNum16 frame_index;
	bool uses_nalu_info_structs;
	bool is_video;
	bool is_haptics;
	ChiakiSeqNum16 unit_index;
	uint16_t units_in_frame_total; // source + units_in_frame_fec
	uint16_t units_in_frame_fec;
	uint8_t codec;
	uint16_t word_at_0x18;
	uint8_t adaptive_stream_index;
	uint8_t byte_at_0x2c;

	uint64_t key_pos;

	uint8_t *data; // not owned
	size_t data_size;
} ChiakiTakionAVPacket;

static inline uint8_t chiaki_takion_av_packet_audio_unit_size(ChiakiTakionAVPacket *packet)				{ return packet->units_in_frame_fec >> 8; }
static inline uint8_t chiaki_takion_av_packet_audio_source_units_count(ChiakiTakionAVPacket *packet)	{ return packet->units_in_frame_fec & 0xf; }
static inline uint8_t chiaki_takion_av_packet_audio_fec_units_count(ChiakiTakionAVPacket *packet)		{ return (packet->units_in_frame_fec >> 4) & 0xf; }

typedef ChiakiErrorCode (*ChiakiTakionAVPacketParse)(ChiakiTakionAVPacket *packet, ChiakiKeyState *key_state, uint8_t *buf, size_t buf_size);

typedef struct chiaki_takion_congestion_packet_t
{
	uint16_t word_0;
	uint16_t received;
	uint16_t lost;
} ChiakiTakionCongestionPacket;


typedef enum {
	CHIAKI_TAKION_EVENT_TYPE_CONNECTED,
	CHIAKI_TAKION_EVENT_TYPE_DISCONNECT,
	CHIAKI_TAKION_EVENT_TYPE_DATA,
	CHIAKI_TAKION_EVENT_TYPE_DATA_ACK,
	CHIAKI_TAKION_EVENT_TYPE_AV
} ChiakiTakionEventType;

typedef enum {
	CHIAKI_NONE_DISABLED = 0,  //(bits: 00)
	CHIAKI_AUDIO_DISABLED = 1, //(bits: 01)
	CHIAKI_VIDEO_DISABLED = 2, //(bits: 10)
	CHIAKI_AUDIO_VIDEO_DISABLED = 3 //(bits: 11)
} ChiakiDisableAudioVideo;

typedef struct chiaki_takion_event_t
{
	ChiakiTakionEventType type;
	union
	{
		struct
		{
			ChiakiTakionMessageDataType data_type;
			uint8_t *buf;
			size_t buf_size;
		} data;

		struct
		{
			ChiakiSeqNum32 seq_num;
		} data_ack;

		ChiakiTakionAVPacket *av;
	};
} ChiakiTakionEvent;

typedef void (*ChiakiTakionCallback)(ChiakiTakionEvent *event, void *user);

typedef struct chiaki_takion_connect_info_t
{
	ChiakiLog *log;
	struct sockaddr *sa;
	size_t sa_len;
	bool ip_dontfrag;
	ChiakiTakionCallback cb;
	void *cb_user;
	ChiakiDisableAudioVideo disable_audio_video;
	bool enable_crypt;
	bool enable_dualsense;
	uint8_t protocol_version;
	bool close_socket; // close socket when finishing takion
} ChiakiTakionConnectInfo;


typedef struct chiaki_takion_t
{
	ChiakiLog *log;
	uint8_t version;

	// Whether or not audio or video is disabled from further processing beyond basic ack
	ChiakiDisableAudioVideo disable_audio_video;
	/**
	 * Whether encryption should be used.
	 *
	 * If false, encryption and MACs are disabled completely.
	 *
	 * If true, encryption and MACs will be used depending on whether gkcrypt_local and gkcrypt_remote are non-null, respectively.
	 * However, if gkcrypt_remote is null, only control data packets are passed to the callback and all other packets are postponed until
	 * gkcrypt_remote is set, so it has been set, so eventually all MACs will be checked.
	 */
	bool enable_crypt;

	/**
	 * Array to be temporarily allocated when non-data packets come, enable_crypt is true, but gkcrypt_remote is NULL
	 * to not ignore any MACs in this period.
	 */
	struct chiaki_takion_postponed_packet_t *postponed_packets;
	size_t postponed_packets_size;
	size_t postponed_packets_count;

	ChiakiGKCrypt *gkcrypt_local; // if NULL (default), no gmac is calculated and nothing is encrypted
	uint64_t key_pos_local;
	ChiakiMutex gkcrypt_local_mutex;

	ChiakiGKCrypt *gkcrypt_remote; // if NULL (default), remote gmacs are IGNORED (!) and everything is expected to be unencrypted

	ChiakiReorderQueue data_queue;
	ChiakiReorderQueue video_queue;
	bool video_queue_initialized;
	int64_t video_queue_head_wait_start_us;
	uint64_t video_queue_head_wait_seq_num;
	ChiakiTakionSendBuffer send_buffer;

	ChiakiTakionCallback cb;
	void *cb_user;
	chiaki_socket_t sock;
	ChiakiThread thread;
	ChiakiStopPipe stop_pipe;
	uint32_t tag_local;
	uint32_t tag_remote;
	bool close_socket;

	ChiakiSeqNum32 seq_num_local;
	ChiakiMutex seq_num_local_mutex;

	/**
	 * Advertised Receiver Window Credit
	 */
	uint32_t a_rwnd;

	ChiakiTakionAVPacketParse av_packet_parse;

	ChiakiKeyState key_state;

	bool enable_dualsense;
} ChiakiTakion;


CHIAKI_EXPORT ChiakiErrorCode chiaki_takion_connect(ChiakiTakion *takion, ChiakiTakionConnectInfo *info, chiaki_socket_t *sock);
CHIAKI_EXPORT void chiaki_takion_close(ChiakiTakion *takion);

/**
 * Must be called from within the Takion thread, i.e. inside the callback!
 */
static inline void chiaki_takion_set_crypt(ChiakiTakion *takion, ChiakiGKCrypt *gkcrypt_local, ChiakiGKCrypt *gkcrypt_remote)
{
	takion->gkcrypt_local = gkcrypt_local;
	takion->gkcrypt_remote = gkcrypt_remote;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_takion_packet_mac(ChiakiGKCrypt *crypt, uint8_t *buf, size_t buf_size, uint64_t key_pos, uint8_t *mac_out, uint8_t *mac_old_out);

/**
 * Get a new key pos and advance by data_size.
 *
 * Thread-safe while Takion is running.
 * @param key_pos pointer to write the new key pos to. will be 0 if encryption is disabled. Contents undefined on failure.
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_takion_crypt_advance_key_pos(ChiakiTakion *takion, size_t data_size, uint64_t *key_pos);

/**
 * Send a datagram directly on the socket.
 *
 * Thread-safe while Takion is running.
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_takion_send_raw(ChiakiTakion *takion, const uint8_t *buf, size_t buf_size);

/**
 * Calculate the MAC for the packet depending on the type derived from the first byte in buf,
 * assign MAC inside buf at the respective position and send the packet.
 *
 * If encryption is disabled, the MAC will be set to 0.
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_takion_send(ChiakiTakion *takion, uint8_t *buf, size_t buf_size, uint64_t key_pos);

/**
 * Thread-safe while Takion is running.
 *
 * @param optional pointer to write the sequence number of the sent packet to
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_takion_send_message_data(ChiakiTakion *takion, uint8_t chunk_flags, uint16_t channel, uint8_t *buf, size_t buf_size, ChiakiSeqNum32 *seq_num);

/**
 * Thread-safe while Takion is running.
 *
 * @param optional pointer to write the sequence number of the sent packet to
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_takion_send_message_data_cont(ChiakiTakion *takion, uint8_t chunk_flags, uint16_t channel, uint8_t *buf, size_t buf_size, ChiakiSeqNum32 *seq_num);

/**
 * Thread-safe while Takion is running.
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_takion_send_congestion(ChiakiTakion *takion, ChiakiTakionCongestionPacket *packet);

/**
 * Thread-safe while Takion is running.
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_takion_send_feedback_state(ChiakiTakion *takion, ChiakiSeqNum16 seq_num, ChiakiFeedbackState *feedback_state);

CHIAKI_EXPORT ChiakiErrorCode chiaki_takion_send_mic_packet(ChiakiTakion *takion, uint8_t *audio_packet, size_t packet_size, bool ps5);
/**
 * Thread-safe while Takion is running.
 * @param event_count number of history events contained in payload
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_takion_send_feedback_history(ChiakiTakion *takion, ChiakiSeqNum16 seq_num, uint8_t event_count, uint8_t *payload, size_t payload_size);

#define CHIAKI_TAKION_V9_AV_HEADER_SIZE_VIDEO 0x17
#define CHIAKI_TAKION_V9_AV_HEADER_SIZE_AUDIO 0x12

CHIAKI_EXPORT ChiakiErrorCode chiaki_takion_v9_av_packet_parse(ChiakiTakionAVPacket *packet, ChiakiKeyState *key_state, uint8_t *buf, size_t buf_size);

#define CHIAKI_TAKION_V12_AV_HEADER_SIZE_VIDEO 0x17
#define CHIAKI_TAKION_V12_AV_HEADER_SIZE_AUDIO 0x13

CHIAKI_EXPORT ChiakiErrorCode chiaki_takion_v12_av_packet_parse(ChiakiTakionAVPacket *packet, ChiakiKeyState *key_state, uint8_t *buf, size_t buf_size);

#define CHIAKI_TAKION_V7_AV_HEADER_SIZE_BASE					0x12
#define CHIAKI_TAKION_V7_AV_HEADER_SIZE_VIDEO_ADD				0x3
#define CHIAKI_TAKION_V7_AV_HEADER_SIZE_NALU_INFO_STRUCTS_ADD	0x3

CHIAKI_EXPORT ChiakiErrorCode chiaki_takion_v7_av_packet_format_header(uint8_t *buf, size_t buf_size, size_t *header_size_out, ChiakiTakionAVPacket *packet);

CHIAKI_EXPORT ChiakiErrorCode chiaki_takion_v7_av_packet_parse(ChiakiTakionAVPacket *packet, ChiakiKeyState *key_state, uint8_t *buf, size_t buf_size);

#define CHIAKI_TAKION_CONGESTION_PACKET_SIZE 0xf

CHIAKI_EXPORT void chiaki_takion_format_congestion(uint8_t *buf, ChiakiTakionCongestionPacket *packet, uint64_t key_pos);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_TAKION_H
