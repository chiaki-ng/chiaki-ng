#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#endif

#include <chiaki/log.h>
#include <chiaki/seqnum.h>
#include <chiaki/sock.h>
#include <chiaki/random.h>

#define STUN_REPLY_TIMEOUT_SEC 5

#define STUN_HEADER_SIZE 20
#define STUN_MSG_TYPE_BINDING_REQUEST 0x0001
#define STUN_MSG_TYPE_BINDING_RESPONSE 0x0101
#define STUN_MAGIC_COOKIE 0x2112A442UL
#define STUN_TRANSACTION_ID_LENGTH 12
#define STUN_ATTRIB_MAPPED_ADDRESS 0x0001
#define STUN_ATTRIB_XOR_MAPPED_ADDRESS 0x0020
#define STUN_MAPPED_ADDR_FAMILY_IPV4 0x01
#define STUN_MAPPED_ADDR_FAMILY_IPV6 0x02


typedef struct stun_server_t {
    char* host;
    uint16_t port;
} StunServer;

StunServer STUN_SERVERS[] = {
    {"stun.moonlight-stream.org", 3478},
    {"stun.l.google.com", 19302},
    {"stun.l.google.com", 19305},
    {"stun1.l.google.com", 19302},
    {"stun1.l.google.com", 19305},
    {"stun2.l.google.com", 19302},
    {"stun2.l.google.com", 19305},
    {"stun3.l.google.com", 19302},
    {"stun3.l.google.com", 19305},
    {"stun4.l.google.com", 19302},
    {"stun4.l.google.com", 19305}
};

static bool stun_get_external_address_from_server(ChiakiLog *log, StunServer *server, char *address, uint16_t *port, chiaki_socket_t *sock, bool ipv4);

/**
 * Get external address and port using STUN.
 *
 * This will try multiple STUN servers, preferring the STUN server of the Moonlight
 * project, and if that fails, trying other STUN servers in random order.
 *
 * @param log Log context
 * @param[out] address Buffer to store address in
 * @param[out] port Buffer to store port in
 * @return true if successful, false otherwise
 */
static bool stun_get_external_address(ChiakiLog *log, char *address, uint16_t *port, StunServer *passed_servers, size_t num_passed_servers, chiaki_socket_t *sock, bool ipv4)
{
    // Try servers preferred by user (i.e., known to be online)
    bool ipv6_tried = false;
    if(num_passed_servers > 0)
    {
       for (int i = 0; i < num_passed_servers; i++)
        {
            if(CHIAKI_SOCKET_IS_INVALID(*sock))
                return false;
            if (stun_get_external_address_from_server(log, &passed_servers[i], address, port, sock, ipv4))
                return true;
            CHIAKI_LOGW(log, "Failed to get external address from %s:%d, retrying with another STUN server...", passed_servers[i].host, passed_servers[i].port);
            // Only try 1 IPV6 server
            if(!ipv4)
            {
                ipv6_tried = true;
                break;
            }
        }
    }
    if(ipv4 || !ipv6_tried)
    {
        // Shuffle order of servers other than moonlight server
        size_t num_servers = sizeof(STUN_SERVERS) / sizeof(StunServer);
        for (int i = num_servers - 1; i > 1; i--) {
            int j = 1 + chiaki_random_32() % (i - 1);
            StunServer temp = STUN_SERVERS[i];
            STUN_SERVERS[i] = STUN_SERVERS[j];
            STUN_SERVERS[j] = temp;
        }
        // Try other servers
        for (int i = 0; i < num_servers; i++) {
            if(CHIAKI_SOCKET_IS_INVALID(*sock))
                return false;
            if (stun_get_external_address_from_server(log, &STUN_SERVERS[i], address, port, sock, ipv4)) {
                return true;
            }
            CHIAKI_LOGW(log, "Failed to get external address from %s:%d, retrying with another STUN server...", STUN_SERVERS[i].host, STUN_SERVERS[i].port);
            // Only try 1 IPV6 server
            if(!ipv4)
                break;
        }
    }
    CHIAKI_LOGE(log, "Failed to get external address from any STUN server.");
    return false;
}

/**
 * Get external address and port using STUN.
 *
 * This will try multiple STUN servers, preferring the STUN server of the Moonlight
 * project, and if that fails, trying other STUN servers in random order.
 *
 * @param log Log context
 * @param[out] address Buffer to store address in
 * @param[out] port Buffer to store port in
 * @return true if successful, false otherwise
 */
CHIAKI_EXPORT bool stun_port_allocation_test(ChiakiLog *log, char *address, uint16_t *port, int32_t *allocation_increment, bool *random_allocation, StunServer *passed_servers, size_t num_passed_servers, chiaki_socket_t *sock)
{
    // skip testing if outgoing port changes with same internal ip and port if send to same ip and different port bc that doesn't apply in our case (we will be using a different address anyway)
    uint16_t port1 = 0;
    uint16_t port2 = 0;
    uint16_t port3 = 0;
    uint16_t port4 = 0;
    char addr1[INET6_ADDRSTRLEN];
    char addr2[INET6_ADDRSTRLEN];
    char addr3[INET6_ADDRSTRLEN];
    char addr4[INET6_ADDRSTRLEN];

    // Try servers preferred by user (i.e., known to be online)
    if(num_passed_servers > 0)
    {
        for (int i = 0; i < num_passed_servers; i++)
        {
            if(port1 == 0)
            {
                if(CHIAKI_SOCKET_IS_INVALID(*sock))
                    break;
                if (!stun_get_external_address_from_server(log, &passed_servers[i], addr1, &port1, sock, true))
                    CHIAKI_LOGW(log, "Failed to get external address from %s:%d, retrying with another STUN server...", passed_servers[i].host, passed_servers[i].port);
                else
                    CHIAKI_LOGV(log, "Got response from STUN server %s:%d", passed_servers[i].host, passed_servers[i].port);
            }
            else if(port2 == 0)
            {
                if(CHIAKI_SOCKET_IS_INVALID(*sock))
                    break;
                if (!stun_get_external_address_from_server(log, &passed_servers[i], addr2, &port2, sock, true))
                    CHIAKI_LOGW(log, "Failed to get external address from %s:%d, retrying with another STUN server...", passed_servers[i].host, passed_servers[i].port);
                else
                    CHIAKI_LOGV(log, "Got response from STUN server %s:%d", passed_servers[i].host, passed_servers[i].port);
            }
            else if(port3 == 0)
            {
                if(CHIAKI_SOCKET_IS_INVALID(*sock))
                    break;
                if (!stun_get_external_address_from_server(log, &passed_servers[i], addr3, &port3, sock, true))
                    CHIAKI_LOGW(log, "Failed to get external address from %s:%d, retrying with another STUN server...", passed_servers[i].host, passed_servers[i].port);
                else
                    CHIAKI_LOGV(log, "Got response from STUN server %s:%d", passed_servers[i].host, passed_servers[i].port);
            }
            else if(port4 == 0)
            {
                if(CHIAKI_SOCKET_IS_INVALID(*sock))
                    break;
                if (stun_get_external_address_from_server(log, &passed_servers[i], addr4, &port4, sock, true))
                {
                    CHIAKI_LOGV(log, "Got response from STUN server %s:%d", passed_servers[i].host, passed_servers[i].port);
                    break;
                }
                else
                    CHIAKI_LOGW(log, "Failed to get external address from %s:%d, retrying with another STUN server...", passed_servers[i].host, passed_servers[i].port);
            }
        }
    }
    if(port4 == 0)
    {
        size_t num_servers = sizeof(STUN_SERVERS) / sizeof(StunServer);
        // Shuffle order of servers other than moonlight server
        for (int i = num_servers - 1; i > 1; i--) {
            int j = (chiaki_random_32() % (i - 1)) + 1;
            StunServer temp = STUN_SERVERS[i];
            STUN_SERVERS[i] = STUN_SERVERS[j];
            STUN_SERVERS[j] = temp;
        }
        // Try other servers
        for (int i = 0; i < num_servers; i++)
        {
            if(port1 == 0)
            {
                if(CHIAKI_SOCKET_IS_INVALID(*sock))
                    break;
                if (!stun_get_external_address_from_server(log, &STUN_SERVERS[i], addr1, &port1, sock, true))
                    CHIAKI_LOGW(log, "Failed to get external address from %s:%d, retrying with another STUN server...", STUN_SERVERS[i].host, STUN_SERVERS[i].port);
                else
                    CHIAKI_LOGV(log, "Got response from STUN server %s:%d", STUN_SERVERS[i].host, STUN_SERVERS[i].port);
            }
            else if(port2 == 0)
            {
                if(CHIAKI_SOCKET_IS_INVALID(*sock))
                    break;
                if (!stun_get_external_address_from_server(log, &STUN_SERVERS[i], addr2, &port2, sock, true))
                    CHIAKI_LOGW(log, "Failed to get external address from %s:%d, retrying with another STUN server...", STUN_SERVERS[i].host, STUN_SERVERS[i].port);
                else
                    CHIAKI_LOGV(log, "Got response from STUN server %s:%d", STUN_SERVERS[i].host, STUN_SERVERS[i].port);
            }
            else if(port3 == 0)
            {
                if(CHIAKI_SOCKET_IS_INVALID(*sock))
                    break;
                if (!stun_get_external_address_from_server(log, &STUN_SERVERS[i], addr3, &port3, sock, true))
                    CHIAKI_LOGW(log, "Failed to get external address from %s:%d, retrying with another STUN server...", STUN_SERVERS[i].host, STUN_SERVERS[i].port);
                else
                    CHIAKI_LOGV(log, "Got response from STUN server %s:%d", STUN_SERVERS[i].host, STUN_SERVERS[i].port);
            }
            else if(port4 == 0)
            {
                if(CHIAKI_SOCKET_IS_INVALID(*sock))
                    break;
                if (stun_get_external_address_from_server(log, &STUN_SERVERS[i], addr4, &port4, sock, true))
                {
                    CHIAKI_LOGV(log, "Got response from STUN server %s:%d", STUN_SERVERS[i].host, STUN_SERVERS[i].port);
                    break;
                }
                else
                    CHIAKI_LOGW(log, "Failed to get external address from %s:%d, retrying with another STUN server...", STUN_SERVERS[i].host, STUN_SERVERS[i].port);
            }
        }
    }
    // No servers returned
    if(port1 == 0)
    {
        CHIAKI_LOGE(log, "Failed to get external address from any STUN server.");
        return false;
    }
    // 1 server returned
    else if(port2 == 0)
    {
        CHIAKI_LOGW(log, "Only 1 STUN server responded for packet allocation calculation.");
        CHIAKI_LOGW(log, "Couldn't determine packet allocation because not enough STUN servers responded.");
        memcpy(address, addr1, sizeof(addr1));
        *port = port1;
        *allocation_increment = 0;
    }
    // 2 servers returned
    else if(port3 == 0)
    {
        memcpy(address, addr2, sizeof(addr2));
        CHIAKI_LOGW(log, "Only 2 STUN servers responded for packet allocation calculation.");
        // address changed between requests use most recent one
        if(strcmp(addr1, addr2) != 0)
        {
            CHIAKI_LOGW(log, "Got different addresses between 2 responses, using 2nd one...");
            CHIAKI_LOGW(log, "Couldn't determine packet allocation because not enough STUN servers responded with the same address defaulting to 0.");
            *allocation_increment = 0;
        }
        else
        {
            *allocation_increment = port2 - port1;
        }
        *port = port2 + 2 * (*allocation_increment);
    }
    // 3 servers returned
    else if(port4 == 0)
    {
        CHIAKI_LOGW(log, "Only 3 STUN servers responded for packet allocation calculation.");
        memcpy(address, addr3, sizeof(addr3));
        if(strcmp(addr1, addr2) != 0)
        {
            if(strcmp(addr1, addr3) == 0)
            {
                CHIAKI_LOGW(log, "Calculating packet allocation based on 2 responses with the same address");
                *allocation_increment = (port3 - port1) / 2;
            }
            else if(strcmp(addr2, addr3) == 0)
            {
                CHIAKI_LOGW(log, "Calculating packet allocation based on 2 responses with the same address");
                *allocation_increment = port3 - port2;
            }
            else
            {
                CHIAKI_LOGW(log, "Got 3 different addresses between 3 responses, using 3rd one...");
                CHIAKI_LOGW(log, "Couldn't determine packet allocation because not enough STUN servers responded with the same address.");
                *allocation_increment = 0;
            }
        }
        else
        {
            if(strcmp(addr1, addr3) != 0)
            {
                CHIAKI_LOGW(log, "Calculating packet allocation based on 2 responses with the same address");
                *allocation_increment = port2 - port1;
            }
            else
            {
                CHIAKI_LOGW(log, "Calculating packet allocation based on 3 responses with the same address");
                *allocation_increment = port2 - port1;
                int32_t allocation_increment1 = port3 - port2;
                if(allocation_increment1 != (*allocation_increment))
                {
                    *random_allocation = true;
                    if(*allocation_increment == 0)
                        *allocation_increment = allocation_increment1;
                    CHIAKI_LOGW(log, "Got different allocation increment calculations from different ports.\nIncrement0: %d, Increment1: %d", *allocation_increment, allocation_increment1);
                }
            }
        }
        *port = port3 + (*allocation_increment);
    }
    // all 4 servers returned
    else
    {
        memcpy(address, addr4, sizeof(addr4));
        *port = port4;
        if(strcmp(addr1, addr2) != 0)
        {
            if((strcmp(addr1, addr3) == 0) || (strcmp(addr1, addr4) == 0))
            {
                if((strcmp(addr1, addr4) == 0))
                {
                    if(strcmp(addr1, addr3) == 0)
                    {
                        CHIAKI_LOGW(log, "Calculating packet allocation based on 3 responses with the same address");
                        *allocation_increment = port4 - port3;
                        int32_t allocation_increment1 = (port3 - port1) / 2;
                        if(allocation_increment1 != (*allocation_increment))
                        {
                            *random_allocation = true;
                            if(*allocation_increment == 0)
                                *allocation_increment = allocation_increment1;
                            CHIAKI_LOGW(log, "Got different allocation increment calculations from different ports.\nIncrement0: %d, Increment1: %d", *allocation_increment, allocation_increment1);
                        }
                    }
                    else
                    {
                        CHIAKI_LOGW(log, "Calculating packet allocation based on 2 responses with the same address");
                        *allocation_increment = (port4 - port1) / 4;
                    }
                }
                else
                {
                    CHIAKI_LOGW(log, "Calculating packet allocation based on 2 responses with the same address");
                    *allocation_increment = (port3 - port1) / 2;
                }
            }
            else
            {
                if((strcmp(addr2, addr3) == 0) || (strcmp(addr2, addr4) == 0))
                {
                    if((strcmp(addr2, addr4) == 0))
                    {
                        if(strcmp(addr2, addr3) == 0)
                        {
                            CHIAKI_LOGW(log, "Calculating packet allocation based on 3 responses with the same address");
                            *allocation_increment = port4 - port3;
                            int32_t allocation_increment1 = port3 - port2;
                            if(allocation_increment1 != (*allocation_increment))
                            {
                                *random_allocation = true;
                                if(*allocation_increment == 0)
                                    *allocation_increment = allocation_increment1;
                                CHIAKI_LOGW(log, "Got different allocation increment calculations from different ports.\nIncrement0: %d, Increment1: %d", *allocation_increment, allocation_increment1);
                            }
                        }
                        else
                        {
                            CHIAKI_LOGW(log, "Calculating packet allocation based on 2 responses with the same address");
                            *allocation_increment = (port4 - port1) / 4;
                        }
                    }
                    else
                    {
                        CHIAKI_LOGW(log, "Calculating packet allocation based on 2 responses with the same address");
                        *allocation_increment = (port3 - port1) / 2;
                    }

                }
                else if (strcmp(addr3, addr4) == 0)
                {
                    CHIAKI_LOGW(log, "Calculating packet allocation based on 2 responses with the same address");
                    *allocation_increment = port3 - port4;
                }
                else
                {
                    CHIAKI_LOGW(log, "Got 4 different addresses between 4 responses, using 4th one...");
                    CHIAKI_LOGW(log, "Couldn't determine packet allocation because not enough STUN servers responded with the same address.");
                    *allocation_increment = 0;
                }
            }
        }
        else if(strcmp(addr2, addr3) != 0)
        {
            *allocation_increment = port2 - port1;
            if(strcmp(addr2, addr4) == 0)
            {
                CHIAKI_LOGW(log, "Calculating packet allocation based on 3 responses with the same address");
                int32_t allocation_increment1 = (port4 - port2) / 2;
                if(allocation_increment1 != (*allocation_increment))
                {
                    *random_allocation = true;
                    if(*allocation_increment == 0)
                        *allocation_increment = allocation_increment1;
                    CHIAKI_LOGW(log, "Got different allocation increment calculations from different ports.\nIncrement0: %d, Increment1: %d", *allocation_increment, allocation_increment1);
                }
            }
            else
            {
                CHIAKI_LOGW(log, "Calculating packet allocation based on 2 responses with the same address");
            }
        }
        else if(strcmp(addr3, addr4) != 0)
        {
            CHIAKI_LOGW(log, "Calculating packet allocation based on 3 responses with the same address");
            *allocation_increment = port2 - port1;
            int32_t allocation_increment1 = port3 - port2;
            if(allocation_increment1 != (*allocation_increment))
            {
                *random_allocation = true;
                if(*allocation_increment == 0)
                    *allocation_increment = allocation_increment1;
                CHIAKI_LOGW(log, "Got different allocation increment calculations from different ports.\nIncrement0: %d, Increment1: %d", *allocation_increment, allocation_increment1);
            }
        }
        else
        {
            CHIAKI_LOGI(log, "Calculating packet allocation based on 4 responses with the same address");
            uint16_t increment0 = port2 - port1;
            uint16_t increment1 = port3 - port2;
            uint16_t increment2 = port4 - port3;
            if((increment0 == increment1) && (increment1 == increment2))
            {
                *allocation_increment = increment0;
                CHIAKI_LOGV(log, "Got 3 idential allocation increment calculations out of 3,\nIncrement %d", increment0);
            }
            else if (increment0 == increment1 || increment0 == increment2)
            {
                CHIAKI_LOGV(log, "Got 2 idential allocation increment calculations out of 3 from different ports.\nIncrement 0: %d, Increment 1: %d, Increment 2: %d", increment0, increment1, increment2);
                *allocation_increment = increment0;
            }
            else if(increment1 == increment2)
            {
                CHIAKI_LOGV(log, "Got 2 idential allocation increment calculations out of 3 from different ports.\nIncrement 0: %d, Increment 1: %d, Increment 2: %d", increment0, increment1, increment2);
                *allocation_increment = increment1;
            }
            else
            {
                *random_allocation = true;
                CHIAKI_LOGW(log, "Got different allocation increment calculations from different ports.\nIncrement 0: %d, Increment 1: %d, Increment 2: %d", increment0, increment1, increment2);
                if(increment0 != 0)
                    *allocation_increment = increment0;
                else
                    *allocation_increment = increment1;
            }
        }
    }

    return true;
}

static bool stun_get_external_address_from_server(ChiakiLog *log, StunServer *server, char *address, uint16_t *port, chiaki_socket_t *sock, bool ipv4)
{
    struct addrinfo* resolved;
    struct addrinfo hints;
    struct sockaddr* server_addr;
    socklen_t server_addr_len;
    memset(&hints, 0, sizeof(hints));
    if(ipv4)
        hints.ai_family = AF_INET;
    else
    {
        hints.ai_family = AF_INET6;
        hints.ai_flags = AI_ADDRCONFIG;
    }
    char server_service[6];
    sprintf(server_service, "%d", server->port);
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(server->host, server_service, &hints, &resolved) != 0) {
        CHIAKI_LOGE(log, "remote/stun.h: Failed to resolve STUN server '%s', error was " CHIAKI_SOCKET_ERROR_FMT, server->host, CHIAKI_SOCKET_ERROR_VALUE);
        return false;
    }
    server_addr = resolved->ai_addr;
    if(ipv4)
        server_addr_len = sizeof(struct sockaddr_in);
    else
        server_addr_len = sizeof(struct sockaddr_in6);

    uint8_t binding_req[STUN_HEADER_SIZE];
    memset(binding_req, 0, sizeof(binding_req));
    *(uint16_t*)(&binding_req[0]) = htons(STUN_MSG_TYPE_BINDING_REQUEST);
    *(uint16_t*)(&binding_req[2]) = htons(0);  // Length
    *(int*)(&binding_req[4]) = htonl(STUN_MAGIC_COOKIE);
    chiaki_random_bytes_crypt(&binding_req[8], STUN_TRANSACTION_ID_LENGTH);

    CHIAKI_SSIZET_TYPE sent = sendto(*sock, (CHIAKI_SOCKET_BUF_TYPE)binding_req, sizeof(binding_req), 0, server_addr, server_addr_len);
    if (sent != sizeof(binding_req)) {
        CHIAKI_LOGE(log, "remote/stun.h: Failed to send STUN request, error was " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
        if (!CHIAKI_SOCKET_IS_INVALID(*sock))
        {
            CHIAKI_SOCKET_CLOSE(*sock);
            *sock = CHIAKI_INVALID_SOCKET;
        }
        freeaddrinfo(resolved);
        return false;
    }
    freeaddrinfo(resolved);

#ifdef _WIN32
    int timeout = STUN_REPLY_TIMEOUT_SEC * 1000;
#else
    struct timeval timeout;
    timeout.tv_sec = STUN_REPLY_TIMEOUT_SEC;
    timeout.tv_usec = 0;
#endif
    if (setsockopt(*sock, SOL_SOCKET, SO_RCVTIMEO, (const CHIAKI_SOCKET_BUF_TYPE)&timeout, sizeof(timeout)) < 0) {
        CHIAKI_LOGE(log, "remote/stun.h: Failed to set socket timeout, error was " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
        if (!CHIAKI_SOCKET_IS_INVALID(*sock))
        {
            CHIAKI_SOCKET_CLOSE(*sock);
            *sock = CHIAKI_INVALID_SOCKET;
        }
        return false;
    }

    uint8_t binding_resp[256];
    CHIAKI_SSIZET_TYPE received = recvfrom(*sock, (CHIAKI_SOCKET_BUF_TYPE)binding_resp, sizeof(binding_resp), 0, NULL, NULL);
    if (received < 0) {
        CHIAKI_LOGE(log, "remote/stun.h: Failed to receive STUN response, error was " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
        return false;
    }

    if(received < STUN_HEADER_SIZE)
    {
        CHIAKI_LOGE(log, "remote/stun.h: Received message is too small");
        return false;
    }


    if (*(uint16_t*)(&binding_resp[0]) != htons(STUN_MSG_TYPE_BINDING_RESPONSE)) {
        CHIAKI_LOGE(log, "remote/stun.h: Received STUN response with invalid message type");
        return false;
    }

    // Verify length stored in binding_resp[2] is correct
    size_t expected_size = ntohs(*(uint16_t*)(&binding_resp[2])) + STUN_HEADER_SIZE;
    if (received != expected_size) {
        CHIAKI_LOGE(log, "remote/stun.h: Received STUN response with invalid length: %zd received, %zu expected", received, expected_size);
        return false;
    }

    if (*(int*)(&binding_resp[4]) != htonl(STUN_MAGIC_COOKIE)) {
        CHIAKI_LOGE(log, "remote/stun.h: Received STUN response with invalid magic cookie");
        return false;
    }

    if (memcmp(&binding_resp[8], &binding_req[8], STUN_TRANSACTION_ID_LENGTH) != 0) {
        CHIAKI_LOGE(log, "remote/stun.h: Received STUN response with invalid transaction ID");
        return false;
    }

    //uint16_t response_attrs_length = ntohs(*(uint16_t*)(&binding_resp[2]));
    size_t response_pos = STUN_HEADER_SIZE;
    // Check we can read 4 bytes of attribute data
    while (response_pos + 4 <= (size_t)received)
    {
        uint16_t attr_type = ntohs(*(uint16_t*)(&binding_resp[response_pos]));
        uint16_t attr_length = ntohs(*(uint16_t*)(&binding_resp[response_pos + 2]));
        // check that the whole advertised message has been received
        if(attr_length > (size_t)received - response_pos - (sizeof(attr_type) + sizeof(attr_length)))
        {
            CHIAKI_LOGE(log, "remote/stun.h: Received STUN response with invalid data");
            return false;
        }
        if (attr_type != STUN_ATTRIB_MAPPED_ADDRESS && attr_type != STUN_ATTRIB_XOR_MAPPED_ADDRESS)
        {
            // attribute values are padded to a multiple of 4 bytes
            response_pos += sizeof(attr_type) + sizeof(attr_length) + (((size_t)attr_length + 3) & ~(size_t)3);
            continue;
        }

        if (attr_length < 8)
        {
            CHIAKI_LOGE(log, "remote/stun.h: Received STUN mapped address with invalid length %u", attr_length);
            return false;
        }
        bool xored = attr_type == STUN_ATTRIB_XOR_MAPPED_ADDRESS;
        uint8_t family = binding_resp[response_pos + 5];
        if (family == STUN_MAPPED_ADDR_FAMILY_IPV4) {
            if (attr_length != 8) {
                 CHIAKI_LOGE(log, "remote/stun.h: Received STUN_MAPPED_ADDR_FAMILY_IPV4 with invalid data!");
                 CHIAKI_LOGE(log, "remote/stun.h: Expected atribute length: 8. Received attribute length %u", attr_length);
                 return false;
            }
            if (xored) {
                uint16_t xored_port = *(uint16_t*)(&binding_resp[response_pos + 6]) ^ (uint16_t)(htonl(STUN_MAGIC_COOKIE));
                *port = ntohs(xored_port);
                uint32_t xored_addr = *(uint32_t*)(&binding_resp[response_pos + 8]) ^ htonl(STUN_MAGIC_COOKIE);
                uint32_t addr = xored_addr;
                inet_ntop(AF_INET, &addr, address, INET_ADDRSTRLEN);
            } else {
                *port = ntohs(*(uint16_t*)(&binding_resp[response_pos + 6]));
                uint32_t addr = *(uint32_t*)(&binding_resp[response_pos + 8]);
                inet_ntop(AF_INET, &addr, address, INET_ADDRSTRLEN);
            }
        } else if (family == STUN_MAPPED_ADDR_FAMILY_IPV6) {
            if (attr_length != 20) {
                 CHIAKI_LOGE(log, "remote/stun.h: Received STUN_MAPPED_ADDR_FAMILY_IPV6 with invalid data!");
                 CHIAKI_LOGE(log, "remote/stun.h: Expected atribute length: 20. Received attribute length %"PRId16, attr_length);
                 return false;
            }
            if (xored) {
                uint16_t xored_port = *(uint16_t*)(&binding_resp[response_pos + 6]) ^ (uint16_t)(htonl(STUN_MAGIC_COOKIE));
                *port = ntohs(xored_port);

                // XOR address with concat(STUN_MAGIC_COOKIE, transaction_id)
                // NOTE: RFC5389 says we need to convert from network to host
                // endianness here, but this seems to be misleading, see
                // https://stackoverflow.com/a/40325004
                uint8_t xored_addr[16];
                for (int i = 0; i < 16; i++) {
                    xored_addr[i] = binding_resp[response_pos + 8 + i] ^ binding_req[4 + i];
                }
                inet_ntop(AF_INET6, &xored_addr, address, INET6_ADDRSTRLEN);
            } else {
                *port = ntohs(*(uint16_t*)(&binding_resp[response_pos + 6]));
                inet_ntop(AF_INET6, &binding_resp[response_pos + 8], address, INET6_ADDRSTRLEN);
            }
        } else {
            CHIAKI_LOGE(log, "remote/stun.h: Received STUN response with invalid address family: %d", family);
            return false;
        }

        return true;
    }

    return false;
}
