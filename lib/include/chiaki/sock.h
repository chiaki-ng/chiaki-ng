// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_SOCK_H
#define CHIAKI_SOCK_H

#include "common.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#include <winsock2.h>
typedef SOCKET chiaki_socket_t;
#define CHIAKI_SOCKET_IS_INVALID(s) ((s) == INVALID_SOCKET)
#define CHIAKI_INVALID_SOCKET INVALID_SOCKET
#define CHIAKI_SOCKET_CLOSE(s) closesocket(s)
#define CHIAKI_SOCKET_ERROR_FMT "%d"
#define CHIAKI_SOCKET_ERROR_VALUE (WSAGetLastError())
#define CHIAKI_SOCKET_EINPROGRESS (WSAGetLastError() == WSAEWOULDBLOCK)
#define CHIAKI_SOCKET_EWOULDBLOCK (WSAGetLastError() == WSAEWOULDBLOCK)
#define CHIAKI_SOCKET_BUF_TYPE char*
#else
#include <unistd.h>
#include <errno.h>
typedef int chiaki_socket_t;
#define CHIAKI_SOCKET_IS_INVALID(s) ((s) < 0)
#define CHIAKI_INVALID_SOCKET (-1)
#define CHIAKI_SOCKET_CLOSE(s) close(s)
#define CHIAKI_SOCKET_ERROR_FMT "%s"
#define CHIAKI_SOCKET_ERROR_VALUE (strerror(errno))
#define CHIAKI_SOCKET_EINPROGRESS (errno == EINPROGRESS)
#define CHIAKI_SOCKET_EWOULDBLOCK (errno == EAGAIN || errno == EWOULDBLOCK)
#define CHIAKI_SOCKET_BUF_TYPE void *
#endif

CHIAKI_EXPORT ChiakiErrorCode chiaki_socket_set_nonblock(chiaki_socket_t sock, bool nonblock);

#ifdef __cplusplus
}
#endif

#endif //CHIAKI_SOCK_H
