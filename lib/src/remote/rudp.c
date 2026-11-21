#include <chiaki/remote/rudp.h>
#include <chiaki/random.h>
#include <chiaki/thread.h>
#include <chiaki/remote/rudpsendbuffer.h>

#include <math.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#if defined(__SWITCH__)
#include <sys/socket.h>
#endif

#define RUDP_CONSTANT 0x244F244F
#define RUDP_SEND_BUFFER_SIZE 16
#define RUDP_EXPECT_TIMEOUT_MS 1000
typedef struct rudp_t
{
    uint16_t counter;
    uint32_t header;
    ChiakiMutex counter_mutex;
    ChiakiStopPipe stop_pipe;
    chiaki_socket_t sock;
    ChiakiLog *log;
    ChiakiRudpSendBuffer send_buffer;
} RudpInstance;

static uint16_t get_then_increase_counter(RudpInstance *rudp);
static ChiakiErrorCode chiaki_rudp_message_parse(uint8_t *serialized_msg, size_t msg_size, RudpMessage *message);
static void rudp_message_serialize(RudpMessage *message, uint8_t *serialized_msg, size_t *msg_size);
static void print_rudp_message_type(RudpInstance *rudp, RudpPacketType type);
static bool assign_submessage_to_message(RudpMessage *message);


CHIAKI_EXPORT RudpInstance *chiaki_rudp_init(chiaki_socket_t *sock, ChiakiLog *log)
{
    RudpInstance *rudp = (RudpInstance *)calloc(1, sizeof(RudpInstance));
    if(!rudp)
        return NULL;
    rudp->log = log;
    ChiakiErrorCode err;
    err = chiaki_mutex_init(&rudp->counter_mutex, false);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(rudp->log, "Rudp failed initializing, failed creating counter mutex");
        goto cleanup_rudp;
    }
    err = chiaki_stop_pipe_init(&rudp->stop_pipe);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(rudp->log, "Rudp failed initializing, failed creating stop pipe");
        goto cleanup_mutex;
    }
    chiaki_rudp_reset_counter_header(rudp);
    rudp->sock = *sock;
	// The send buffer size MUST be consistent with the acked seqnums array size in rudp_handle_message_ack()
    err = chiaki_rudp_send_buffer_init(&rudp->send_buffer, rudp, log, RUDP_SEND_BUFFER_SIZE);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(rudp->log, "Rudp failed initializing, failed creating send buffer");
        goto cleanup_stop_pipe;
    }
    return rudp;

cleanup_stop_pipe:
    chiaki_stop_pipe_fini(&rudp->stop_pipe);
cleanup_mutex:
    err = chiaki_mutex_fini(&rudp->counter_mutex);
    if(err != CHIAKI_ERR_SUCCESS)
        CHIAKI_LOGE(rudp->log, "Rudp couldn't cleanup counter mutex!");
cleanup_rudp:
    free(rudp);
    return NULL;
}

CHIAKI_EXPORT void chiaki_rudp_reset_counter_header(RudpInstance *rudp)
{
    chiaki_mutex_lock(&rudp->counter_mutex);
    rudp->counter = chiaki_random_32()%0x5E00 + 0x1FF;
    chiaki_mutex_unlock(&rudp->counter_mutex);
    rudp->header = chiaki_random_32() + 0x8000;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_init_message(RudpInstance *rudp)
{
    RudpMessage message;
    uint16_t local_counter = get_then_increase_counter(rudp);
    message.type = INIT_REQUEST;
    message.subMessage = NULL;
    message.data_size = 14;
    uint8_t data[message.data_size];
    size_t alloc_size = 8 + message.data_size;
    uint8_t *serialized_msg = malloc(alloc_size * sizeof(uint8_t));
    if(!serialized_msg)
    {
        CHIAKI_LOGE(rudp->log, "Error allocating memory for rudp message");
        return CHIAKI_ERR_MEMORY;
    }
    size_t msg_size = 0;
    message.size = (0xC << 12) | alloc_size;
    const uint8_t after_header[0x2] = { 0x05, 0x82 };
    const uint8_t after_counter[0x6] = { 0x0B, 0x01, 0x01, 0x00, 0x01, 0x00 };
    *(chiaki_unaligned_uint16_t *)(data) = htons(local_counter);
    memcpy(data + 2, after_counter, sizeof(after_counter));
    *(chiaki_unaligned_uint32_t *)(data + 8) = htonl(rudp->header);
    memcpy(data + 12, after_header, sizeof(after_header));
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);
    free(serialized_msg);
    return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_cookie_message(RudpInstance *rudp, uint8_t *response_buf, size_t response_size)
{
    RudpMessage message;
    uint16_t local_counter = get_then_increase_counter(rudp);
    message.type = COOKIE_REQUEST;
    message.subMessage = NULL;
    message.data_size = 14 + response_size;
    size_t alloc_size = 8 + message.data_size;
    uint8_t *serialized_msg = malloc(alloc_size * sizeof(uint8_t));
    if(!serialized_msg)
    {
        CHIAKI_LOGE(rudp->log, "Error allocating memory for rudp message");
        return CHIAKI_ERR_MEMORY;
    }
    size_t msg_size = 0;
    message.size = (0xC << 12) | alloc_size;
    uint8_t data[message.data_size];
    const uint8_t after_header[0x2] = { 0x05, 0x82 };
    const uint8_t after_counter[0x6] = { 0x0B, 0x01, 0x01, 0x00, 0x01, 0x00 };
    *(chiaki_unaligned_uint16_t *)(data) = htons(local_counter);
    memcpy(data + 2, after_counter, sizeof(after_counter));
    *(chiaki_unaligned_uint32_t *)(data + 8) = htonl(rudp->header);
    memcpy(data + 12, after_header, sizeof(after_header));
    memcpy(data + 14, response_buf, response_size);
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);
    free(serialized_msg);
    return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_session_message(RudpInstance *rudp, uint16_t remote_counter, uint8_t *session_msg, size_t session_msg_size)
{
    RudpMessage subMessage;
    uint16_t local_counter = get_then_increase_counter(rudp);
    subMessage.type = CTRL_MESSAGE;
    subMessage.subMessage = NULL;
    subMessage.data_size = 2 + session_msg_size;
    subMessage.size = (0xC << 12) | (8 + subMessage.data_size);
    uint8_t subdata[subMessage.data_size];
    *(chiaki_unaligned_uint16_t *)(subdata) = htons(local_counter);
    memcpy(subdata + 2, session_msg, session_msg_size);
    subMessage.data = subdata;

    RudpMessage message;
    message.type = SESSION_MESSAGE;
    message.subMessage = &subMessage;
    message.data_size = 4;
    size_t alloc_size = 8 + message.data_size + 8 + subMessage.data_size;
    uint8_t *serialized_msg = malloc(alloc_size * sizeof(uint8_t));
    if(!serialized_msg)
    {
        CHIAKI_LOGE(rudp->log, "Error allocating memory for rudp message");
        return CHIAKI_ERR_MEMORY;
    }
    size_t msg_size = 0;
    message.size = (0xC << 12) | (8 + message.data_size);
    uint8_t data[message.data_size];
    *(chiaki_unaligned_uint16_t *)(data) = htons(local_counter);
    *(chiaki_unaligned_uint16_t *)(data + 2) = htons(remote_counter);
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);
    free(serialized_msg);
    return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_ack_message(RudpInstance *rudp, uint16_t remote_counter)
{
    RudpMessage message;
    uint16_t counter = rudp->counter;
    message.type = ACK;
    message.subMessage = NULL;
    message.data_size = 6;
    size_t alloc_size = 8 + message.data_size;
    uint8_t *serialized_msg = malloc(alloc_size * sizeof(uint8_t));
    if(!serialized_msg)
    {
        CHIAKI_LOGE(rudp->log, "Error allocating memory for rudp message");
        return CHIAKI_ERR_MEMORY;
    }
    size_t msg_size = 0;
    message.size = (0xC << 12) | alloc_size;
    uint8_t data[message.data_size];
    const uint8_t after_counters[0x2] = { 0x00, 0x92 };
    *(chiaki_unaligned_uint16_t *)(data) = htons(counter);
    *(chiaki_unaligned_uint16_t *)(data + 2) = htons(remote_counter);
    memcpy(data + 4, after_counters, sizeof(after_counters));
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);
    free(serialized_msg);
    return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_ctrl_message(RudpInstance *rudp, uint8_t *ctrl_message, size_t ctrl_message_size)
{
    RudpMessage message;
    uint16_t counter = get_then_increase_counter(rudp);
    uint16_t counter_ack = rudp->counter;
    message.type = CTRL_MESSAGE;
    message.subMessage = NULL;
    message.data_size = 2 + ctrl_message_size;
    size_t alloc_size = 8 + message.data_size;
    uint8_t *serialized_msg = malloc(alloc_size * sizeof(uint8_t));
    if(!serialized_msg)
    {
        CHIAKI_LOGE(rudp->log, "Error allocating memory for rudp message");
        return CHIAKI_ERR_MEMORY;
    }
    size_t msg_size = 0;
    message.size = (0xC << 12) | alloc_size;
    uint8_t data[message.data_size];
    *(chiaki_unaligned_uint16_t *)(data) = htons(counter);
    memcpy(data + 2, ctrl_message, ctrl_message_size);
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        free(serialized_msg);
        return err;
    }
    err = chiaki_rudp_send_buffer_push(&rudp->send_buffer, counter_ack, serialized_msg, msg_size);
    return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_switch_to_stream_connection_message(RudpInstance *rudp)
{
    RudpMessage message;
    uint16_t counter = get_then_increase_counter(rudp);
    uint16_t counter_ack = rudp->counter;
    message.type = CTRL_MESSAGE;
    message.subMessage = NULL;
    message.data_size = 26;
    size_t alloc_size = 8 + message.data_size;
    uint8_t *serialized_msg = malloc(alloc_size * sizeof(uint8_t));
    if(!serialized_msg)
    {
        CHIAKI_LOGE(rudp->log, "Error allocating memory for rudp message");
        return CHIAKI_ERR_MEMORY;
    }
    size_t msg_size = 0;
    message.size = (0xC << 12) | alloc_size;
    uint8_t data[message.data_size];
    const size_t buf_size = 16;
    uint8_t buf[buf_size];
    const uint8_t before_buf[8] = { 0x00, 0x00, 0x00, 0x10, 0x00, 0x0D, 0x00, 0x00 };
    chiaki_random_bytes_crypt(buf, buf_size);
    *(chiaki_unaligned_uint16_t *)(data) = htons(counter);
    memcpy(data + 2, before_buf, sizeof(before_buf));
    memcpy(data + 10, buf, buf_size);
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        free(serialized_msg);
        return err;
    }
    err = chiaki_rudp_send_buffer_push(&rudp->send_buffer, counter_ack, serialized_msg, msg_size);
    return err;
}

/**
 * Serializes rudp message into byte array
 *
 * @param[in] message The rudp message to serialize
 * @param[out] serialized_msg The serialized message
 * @param[out] msg_size The size of the serialized message
 * 
*/
static void rudp_message_serialize(
    RudpMessage *message, uint8_t *serialized_msg, size_t *msg_size)
{
    *(chiaki_unaligned_uint16_t *)(serialized_msg) = htons(message->size);
    *(chiaki_unaligned_uint32_t *)(serialized_msg + 2) = htonl(RUDP_CONSTANT);
    *(chiaki_unaligned_uint16_t *)(serialized_msg + 6) = htons(message->type);
    memcpy(serialized_msg + 8, message->data, message->data_size);
    *msg_size += 8 + message->data_size;
    if(message->subMessage)
    {
        rudp_message_serialize(message->subMessage, serialized_msg + 8 + message->data_size, msg_size);
    }
}

/**
 * Parse serialized message into rudp message
 *
 * @param[in] serialized_msg The serialized message to transform to a rudp message
 * @param[in] msg_size The size of the serialized message
 * @param[out] RudpMessage The parsed rudp message
 * @return CHIAKI_ERR_SUCCESS on sucess or error code on failure
 * 
*/
static ChiakiErrorCode chiaki_rudp_message_parse(
    uint8_t *serialized_msg, size_t msg_size, RudpMessage *message)
{
    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
    message->data = NULL;
    message->subMessage = NULL;
    message->subMessage_size = 0;
    message->data_size = 0;
    message->size = ntohs(*(chiaki_unaligned_uint16_t *)(serialized_msg));
    message->type = ntohs(*(chiaki_unaligned_uint16_t *)(serialized_msg + 6));
    message->subtype = serialized_msg[6] & 0xFF;
    // Eliminate 0xC before length (size of header + data but not submessage)
    serialized_msg[0] = serialized_msg[0] & 0x0F;
    message->remote_counter = 0;
    uint16_t length = ntohs(*(chiaki_unaligned_uint16_t *)(serialized_msg));
    int remaining = msg_size - 8;
    int data_size = 0;
    if(length > 8)
    {
        data_size = length - 8;
        if(remaining < data_size)
            data_size = remaining;
        message->data_size = data_size;
        message->data = malloc(message->data_size * sizeof(uint8_t));
        if(!message->data)
            return CHIAKI_ERR_MEMORY;
        memcpy(message->data, serialized_msg + 8, data_size);
        if(data_size >= 2)
            message->remote_counter = ntohs(*(chiaki_unaligned_uint16_t *)(message->data)) + 1;
    }

    remaining = remaining - data_size;
    if (remaining >= 8)
    {
        message->subMessage = malloc(1 * sizeof(RudpMessage));
        if(!message->subMessage)
            return CHIAKI_ERR_MEMORY;
        message->subMessage_size = remaining;
        err = chiaki_rudp_message_parse(serialized_msg + 8 + data_size, remaining, message->subMessage);
    }
    return err;
}

/**
 * Get current rudp local counter and then increase rudp local counter
 *
 * @param[in] rudp The rudp instance to use
 * @return The rudp counter before increasing
 * 
*/
static uint16_t get_then_increase_counter(RudpInstance *rudp)
{
    chiaki_mutex_lock(&rudp->counter_mutex);
    uint16_t tmp = rudp->counter;
    if(rudp->counter >= UINT16_MAX)
        rudp->counter = 0;
    else
        rudp->counter++;
    chiaki_mutex_unlock(&rudp->counter_mutex);
    return tmp;
}

CHIAKI_EXPORT uint16_t chiaki_rudp_get_local_counter(RudpInstance *rudp)
{
    return rudp->counter;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_raw(RudpInstance *rudp, uint8_t *buf, size_t buf_size)
{
    if(CHIAKI_SOCKET_IS_INVALID(rudp->sock))
    {
        return CHIAKI_ERR_DISCONNECTED;
    }
    CHIAKI_LOGV(rudp->log, "Sending Message:");
    chiaki_log_hexdump(rudp->log, CHIAKI_LOG_VERBOSE, buf, buf_size);
	int sent = send(rudp->sock, (CHIAKI_SOCKET_BUF_TYPE) buf, buf_size, 0);
	if(sent < 0)
	{
		CHIAKI_LOGE(rudp->log, "Rudp raw failed to send packet: " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
		return CHIAKI_ERR_NETWORK;
	}
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_select_recv(RudpInstance *rudp, size_t buf_size,  RudpMessage *message)
{
    uint8_t buf[buf_size]; 
	ChiakiErrorCode err;
	CHIAKI_SSIZET_TYPE received_sz;
	// Same as takion_recv: a readable signal without data (WOULDBLOCK) is not an error.
	do
	{
		err = chiaki_stop_pipe_select_single(&rudp->stop_pipe, rudp->sock, false, RUDP_EXPECT_TIMEOUT_MS);
		if(err == CHIAKI_ERR_TIMEOUT || err == CHIAKI_ERR_CANCELED)
			return err;
		if(err != CHIAKI_ERR_SUCCESS)
		{
			CHIAKI_LOGE(rudp->log, "Rudp select failed: " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
			return err;
		}

		received_sz = recv(rudp->sock, (CHIAKI_SOCKET_BUF_TYPE) buf, buf_size, 0);
	} while(received_sz < 0 && CHIAKI_SOCKET_EWOULDBLOCK);
	if(received_sz <= 8)
	{
		if(received_sz < 0)
			CHIAKI_LOGE(rudp->log, "Rudp recv failed: " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
		else
			CHIAKI_LOGE(rudp->log, "Rudp recv returned less than the required 8 byte RUDP header");
		return CHIAKI_ERR_NETWORK;
	}
    CHIAKI_LOGV(rudp->log, "Receiving message:");
    chiaki_log_hexdump(rudp->log, CHIAKI_LOG_VERBOSE, buf, received_sz);

    err = chiaki_rudp_message_parse(buf, received_sz, message);
    
	return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_recv_only(RudpInstance *rudp, size_t buf_size,  RudpMessage *message)
{
    uint8_t buf[buf_size];
	CHIAKI_SSIZET_TYPE received_sz = recv(rudp->sock, (CHIAKI_SOCKET_BUF_TYPE) buf, buf_size, 0);
	if(received_sz <= 8)
	{
		if(received_sz < 0)
			CHIAKI_LOGE(rudp->log, "Rudp recv failed: " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
		else
			CHIAKI_LOGE(rudp->log, "Rudp recv returned less than the required 8 byte RUDP header");
		return CHIAKI_ERR_NETWORK;
	}
    CHIAKI_LOGV(rudp->log, "Receiving message:");
    chiaki_log_hexdump(rudp->log, CHIAKI_LOG_VERBOSE, buf, received_sz);

    ChiakiErrorCode err = chiaki_rudp_message_parse(buf, received_sz, message);

	return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_stop_pipe_select_single(RudpInstance *rudp, ChiakiStopPipe *stop_pipe, uint64_t timeout)
{
	ChiakiErrorCode err = chiaki_stop_pipe_select_single(stop_pipe, rudp->sock, false, timeout);
	if(err == CHIAKI_ERR_TIMEOUT || err == CHIAKI_ERR_CANCELED)
		return err;
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(rudp->log, "Rudp select failed: " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
		return err;
	}
    return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_recv(RudpInstance *rudp, RudpMessage *message, uint8_t *buf, size_t buf_size, uint16_t remote_counter, RudpPacketType send_type, RudpPacketType recv_type, size_t min_data_size, size_t tries)
{
    bool success = false;
    for(int i = 0; i < tries; i++)
    {
        switch(send_type)
        {
            case INIT_REQUEST:
                chiaki_rudp_send_init_message(rudp);
                break;
            case COOKIE_REQUEST:
                chiaki_rudp_send_cookie_message(rudp, buf, buf_size);
                break;
            case ACK:
                chiaki_rudp_send_ack_message(rudp, remote_counter);
                break;
            case SESSION_MESSAGE:
                chiaki_rudp_send_session_message(rudp, remote_counter, buf, buf_size);
                break;
            default:
                CHIAKI_LOGE(rudp->log, "Selected RudpPacketType 0x%04x to send that is not supported by rudp send receive.", send_type);
                return CHIAKI_ERR_INVALID_DATA;
        }
        ChiakiErrorCode err = chiaki_rudp_select_recv(rudp, 1500, message);
        if(err == CHIAKI_ERR_TIMEOUT)
            continue;
        if(err != CHIAKI_ERR_SUCCESS)
            return err;
        bool found = true;
        while(true)
        {
            switch(recv_type)
            {
                case INIT_RESPONSE:
                    if(message->subtype != 0xD0)
                    {
                        if(assign_submessage_to_message(message))
                            continue;
                        CHIAKI_LOGE(rudp->log, "Expected INIT RESPONSE with subtype 0xD0.\nReceived unexpected RUDP message ... retrying");
                        chiaki_rudp_print_message(rudp, message);
                        chiaki_rudp_message_pointers_free(message);
                        found = false;
                        break;
                    }
                    break;
                case COOKIE_RESPONSE:
                    if(message->subtype != 0xA0)
                    {
                        if(assign_submessage_to_message(message))
                            continue;
                        CHIAKI_LOGE(rudp->log, "Expected COOKIE RESPONSE with subtype 0xA0.\nReceived unexpected RUDP message ... retrying");
                        chiaki_rudp_print_message(rudp, message);
                        chiaki_rudp_message_pointers_free(message);
                        found = false;
                        break;
                    }
                    break;
                case CTRL_MESSAGE:
                    if((message->subtype & 0x0F) != 0x2 && (message->subtype & 0x0F) != 0x6)
                    {
                        if(assign_submessage_to_message(message))
                            continue;
                        CHIAKI_LOGE(rudp->log, "Expected CTRL MESSAGE with subtype 0x2 or 0x36.\nReceived unexpected RUDP message ... retrying");
                        chiaki_rudp_print_message(rudp, message);
                        chiaki_rudp_message_pointers_free(message);
                        found = false;
                        break;
                    }
                    break;
                case FINISH:
                    if(message->subtype != 0xC0)
                    {
                        if(assign_submessage_to_message(message))
                            continue;
                        CHIAKI_LOGE(rudp->log, "Expected FINISH MESSAGE with subtype 0xC0 .\nReceived unexpected RUDP message ... retrying");
                        chiaki_rudp_print_message(rudp, message);
                        chiaki_rudp_message_pointers_free(message);
                        found = false;
                        break;
                    }
                    break;
                default:
                    CHIAKI_LOGE(rudp->log, "Selected RudpPacketType 0x%04x to receive that is not supported by rudp send receive.", send_type);
                    chiaki_rudp_message_pointers_free(message);
                    return CHIAKI_ERR_INVALID_DATA;
            }
            break;
        }
        if(!found)
            continue;
        if(message->data_size < min_data_size)
        {
            chiaki_rudp_message_pointers_free(message);
            CHIAKI_LOGE(rudp->log, "Received message with too small of data size");
            continue;
        }
        success = true;
        break;
    }
    if(success)
        return CHIAKI_ERR_SUCCESS;
    else
    {
        CHIAKI_LOGE(rudp->log, "Could not receive correct RUDP message after %llu tries", tries);
        print_rudp_message_type(rudp, recv_type);
        return CHIAKI_ERR_INVALID_RESPONSE;
    }
}

static bool assign_submessage_to_message(RudpMessage *message)
{
    if(message->subMessage)
    {
        if(message->data)
        {
            free(message->data);
            message->data = NULL;
        }
        RudpMessage *tmp = message->subMessage;
        memcpy(message, message->subMessage, sizeof(RudpMessage));
        free(tmp);
        return true;
    }
    return false;
}
CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_ack_packet(RudpInstance *rudp, uint16_t counter_to_ack)
{
	ChiakiSeqNum16 acked_seq_nums[RUDP_SEND_BUFFER_SIZE];
	size_t acked_seq_nums_count = 0;
	ChiakiErrorCode err = chiaki_rudp_send_buffer_ack(&rudp->send_buffer, counter_to_ack, acked_seq_nums, &acked_seq_nums_count);
    return err;
}

CHIAKI_EXPORT void chiaki_rudp_print_message(RudpInstance *rudp, RudpMessage *message)
{
    CHIAKI_LOGI(rudp->log, "-------------RUDP MESSAGE------------");
    print_rudp_message_type(rudp, message->type);
    CHIAKI_LOGI(rudp->log, "Rudp Message Subtype: 0x%02x", message->subtype);
    CHIAKI_LOGI(rudp->log, "Rudp Message Size: %02x", message->size);
    CHIAKI_LOGI(rudp->log, "Rudp Message Data Size: %zu", message->data_size);
    CHIAKI_LOGI(rudp->log, "-----Rudp Message Data ---");
    if(message->data)
        chiaki_log_hexdump(rudp->log, CHIAKI_LOG_INFO, message->data, message->data_size);
    CHIAKI_LOGI(rudp->log, "Rudp Message Remote Counter: %u", message->remote_counter);
    if(message->subMessage)
        chiaki_rudp_print_message(rudp, message->subMessage);
}

CHIAKI_EXPORT void chiaki_rudp_message_pointers_free(RudpMessage *message)
{
    if(message->data)
    {
        free(message->data);
        message->data = NULL;
    }
    if(message->subMessage)
    {
        chiaki_rudp_message_pointers_free(message->subMessage);
        free(message->subMessage);
        message->subMessage = NULL;
    }
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_fini(RudpInstance *rudp)
{
    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
    if(rudp)
    {
        chiaki_rudp_send_buffer_fini(&rudp->send_buffer);
        if (!CHIAKI_SOCKET_IS_INVALID(rudp->sock))
        {
            CHIAKI_SOCKET_CLOSE(rudp->sock);
            rudp->sock = CHIAKI_INVALID_SOCKET;
        }
        err = chiaki_mutex_fini(&rudp->counter_mutex);
        chiaki_stop_pipe_fini(&rudp->stop_pipe);
        free(rudp);
    }
    return err;
}

/**
 * Prints a given rudp message type
 *
 * @param[in] rudp The rudp instance to use
 * @return type The type of packet to print
 *
*/
static void print_rudp_message_type(RudpInstance *rudp, RudpPacketType type)
{
    switch(type)
    {
        case INIT_REQUEST:
            CHIAKI_LOGI(rudp->log, "Message Type: Init Request");
            break;
        case INIT_RESPONSE:
            CHIAKI_LOGI(rudp->log, "Message Type: Init Response");
            break;
        case COOKIE_REQUEST:
            CHIAKI_LOGI(rudp->log, "Message Type: Cookie Request");
            break;
        case COOKIE_RESPONSE:
            CHIAKI_LOGI(rudp->log, "Message Type: Cookie Response");
            break;
        case SESSION_MESSAGE:
            CHIAKI_LOGI(rudp->log, "Message Type: Session Message");
            break;
        case STREAM_CONNECTION_SWITCH_ACK:
            CHIAKI_LOGI(rudp->log, "Message Type: Takion Switch Ack");
            break;
        case ACK:
            CHIAKI_LOGI(rudp->log, "Message Type: Ack");
            break;
        case CTRL_MESSAGE:
            CHIAKI_LOGI(rudp->log, "Message Type: Ctrl Message");
            break;
        case UNKNOWN:
            CHIAKI_LOGI(rudp->log, "Message Type: Unknown");
            break;
        case FINISH:
            CHIAKI_LOGI(rudp->log, "Message Type: Finish");
            break;
        default:
            CHIAKI_LOGI(rudp->log, "Unknown Message Type: %04x", type);
            break;      
    }
}

