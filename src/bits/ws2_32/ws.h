#ifndef SCREW_WS_H
#define SCREW_WS_H

#include <screw/bit.h>
#include <screw/teb.h>
#include <screw/unicode.h>
#include <screw/win.h>

#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

typedef uint64_t SOCKET;

#define INVALID_SOCKET ((SOCKET)~0ull)
#define SOCKET_ERROR (-1)

#define W_AF_INET6 23
#define W_SOL_SOCKET 0xFFFF

#define WSAEINTR 10004
#define WSAEBADF 10009
#define WSAEACCES 10013
#define WSAEFAULT 10014
#define WSAEINVAL 10022
#define WSAEMFILE 10024
#define WSAEWOULDBLOCK 10035
#define WSAEINPROGRESS 10036
#define WSAEALREADY 10037
#define WSAENOTSOCK 10038
#define WSAEDESTADDRREQ 10039
#define WSAEMSGSIZE 10040
#define WSAEPROTOTYPE 10041
#define WSAENOPROTOOPT 10042
#define WSAEPROTONOSUPPORT 10043
#define WSAEOPNOTSUPP 10045
#define WSAEAFNOSUPPORT 10047
#define WSAEADDRINUSE 10048
#define WSAEADDRNOTAVAIL 10049
#define WSAENETDOWN 10050
#define WSAENETUNREACH 10051
#define WSAECONNABORTED 10053
#define WSAECONNRESET 10054
#define WSAENOBUFS 10055
#define WSAEISCONN 10056
#define WSAENOTCONN 10057
#define WSAESHUTDOWN 10058
#define WSAETIMEDOUT 10060
#define WSAECONNREFUSED 10061
#define WSAEHOSTUNREACH 10065
#define WSANOTINITIALISED 10093
#define WSAHOST_NOT_FOUND 11001
#define WSATRY_AGAIN 11002
#define WSANO_RECOVERY 11003
#define WSANO_DATA 11004

extern const struct bit_table ws_socket;
extern const struct bit_table ws_resolve;

int ws_error_of(int err);
int ws_fail(int err);
int ws_fd(SOCKET s);

int addr_in(const void *win, int len, struct sockaddr_storage *host, socklen_t *host_len);
int addr_out(const struct sockaddr *host, socklen_t host_len, void *win, int *len);
int family_in(int f);
int family_out(int f);

#endif
