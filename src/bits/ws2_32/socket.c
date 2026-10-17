#include "ws.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define W_FIONBIO 0x8004667Eu
#define W_FIONREAD 0x4004667Fu
#define W_SIOCATMARK 0x40047307u

#define W_SO_DEBUG 0x0001
#define W_SO_ACCEPTCONN 0x0002
#define W_SO_REUSEADDR 0x0004
#define W_SO_KEEPALIVE 0x0008
#define W_SO_DONTROUTE 0x0010
#define W_SO_BROADCAST 0x0020
#define W_SO_LINGER 0x0080
#define W_SO_OOBINLINE 0x0100
#define W_SO_DONTLINGER (~W_SO_LINGER)
#define W_SO_EXCLUSIVEADDRUSE (~W_SO_REUSEADDR)
#define W_SO_SNDBUF 0x1001
#define W_SO_RCVBUF 0x1002
#define W_SO_SNDTIMEO 0x1005
#define W_SO_RCVTIMEO 0x1006
#define W_SO_ERROR 0x1007
#define W_SO_TYPE 0x1008

#define W_IPV6_V6ONLY 27
#define W_IP_TTL 4
#define W_IP_MULTICAST_IF 9
#define W_IP_MULTICAST_TTL 10
#define W_IP_MULTICAST_LOOP 11
#define W_IP_ADD_MEMBERSHIP 12
#define W_IP_DROP_MEMBERSHIP 13

#define W_MSG_WAITALL 0x8

#define W_FD_SETSIZE 64

typedef struct {
    WORD version;
    WORD high_version;
    unsigned short max_sockets;
    unsigned short max_udp;
    char *vendor;
    char description[257];
    char status[129];
} WSADATA;

typedef struct {
    LONG sec;
    LONG usec;
} WTIMEVAL;

typedef struct {
    unsigned count;
    SOCKET fds[W_FD_SETSIZE];
} WFD_SET;

typedef struct {
    SOCKET fd;
    short events;
    short revents;
} WPOLLFD;

typedef struct {
    ULONG len;
    char *buf;
} WSABUF;

typedef struct {
    unsigned short onoff;
    unsigned short linger;
} WLINGER;

static atomic_int started;

int ws_error_of(int err)
{
    switch (err) {
    case EINTR: return WSAEINTR;
    case EBADF: return WSAENOTSOCK;
    case EACCES:
    case EPERM: return WSAEACCES;
    case EFAULT: return WSAEFAULT;
    case EINVAL: return WSAEINVAL;
    case EMFILE:
    case ENFILE: return WSAEMFILE;
    case EAGAIN:
    case EINPROGRESS: return WSAEWOULDBLOCK;
    case EALREADY: return WSAEALREADY;
    case ENOTSOCK: return WSAENOTSOCK;
    case EDESTADDRREQ: return WSAEDESTADDRREQ;
    case EMSGSIZE: return WSAEMSGSIZE;
    case EPROTOTYPE: return WSAEPROTOTYPE;
    case ENOPROTOOPT: return WSAENOPROTOOPT;
    case EPROTONOSUPPORT: return WSAEPROTONOSUPPORT;
    case EOPNOTSUPP: return WSAEOPNOTSUPP;
    case EAFNOSUPPORT: return WSAEAFNOSUPPORT;
    case EADDRINUSE: return WSAEADDRINUSE;
    case EADDRNOTAVAIL: return WSAEADDRNOTAVAIL;
    case ENETDOWN: return WSAENETDOWN;
    case ENETUNREACH: return WSAENETUNREACH;
    case ECONNABORTED: return WSAECONNABORTED;
    case ECONNRESET:
    case EPIPE: return WSAECONNRESET;
    case ENOBUFS:
    case ENOMEM: return WSAENOBUFS;
    case EISCONN: return WSAEISCONN;
    case ENOTCONN: return WSAENOTCONN;
    case ESHUTDOWN: return WSAESHUTDOWN;
    case ETIMEDOUT: return WSAETIMEDOUT;
    case ECONNREFUSED: return WSAECONNREFUSED;
    case EHOSTUNREACH: return WSAEHOSTUNREACH;
    default: return WSAEINVAL;
    }
}

int ws_fail(int err)
{
    last_error_set(err);
    return SOCKET_ERROR;
}

int ws_fd(SOCKET s) { return s > INT32_MAX ? -1 : (int)s; }

int family_in(int f) { return f == W_AF_INET6 ? AF_INET6 : f; }
int family_out(int f) { return f == AF_INET6 ? W_AF_INET6 : f; }

int addr_in(const void *win, int len, struct sockaddr_storage *host, socklen_t *host_len)
{
    if (!win || len < 2 || len > (int)sizeof *host)
        return -1;
    memcpy(host, win, len);
    host->ss_family = family_in(((const uint16_t *)win)[0]);
    *host_len = len;
    return 0;
}

int addr_out(const struct sockaddr *host, socklen_t host_len, void *win, int *len)
{
    if (!win || !len)
        return 0;
    if (*len < (int)host_len) {
        *len = host_len;
        return -1;
    }
    memcpy(win, host, host_len);
    ((uint16_t *)win)[0] = family_out(host->sa_family);
    *len = host_len;
    return 0;
}

static int errno_fail(void) { return ws_fail(ws_error_of(errno)); }

static int WINAPI WSAStartup(WORD version, WSADATA *data)
{
    atomic_fetch_add(&started, 1);
    if (data) {
        memset(data, 0, sizeof *data);
        data->version = version > 0x0202 ? 0x0202 : version;
        data->high_version = 0x0202;
        strcpy(data->description, "Screw Sockets 2.2");
        strcpy(data->status, "Running");
    }
    return 0;
}

static int WINAPI WSACleanup(void)
{
    if (atomic_load(&started) <= 0)
        return ws_fail(WSANOTINITIALISED);
    atomic_fetch_sub(&started, 1);
    return 0;
}

static int WINAPI WSAGetLastError(void) { return last_error_get(); }
static void WINAPI WSASetLastError(int e) { last_error_set(e); }

static SOCKET make_socket(int af, int type, int proto)
{
    int fd = socket(family_in(af), type | SOCK_CLOEXEC, proto);
    if (fd < 0) {
        errno_fail();
        return INVALID_SOCKET;
    }
    if (family_in(af) == AF_INET6) {
        int off = 0;
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    return fd;
}

static SOCKET WINAPI socket_(int af, int type, int proto) { return make_socket(af, type, proto); }

static SOCKET WINAPI WSASocketA(int af, int type, int proto, void *info, unsigned group, DWORD flags)
{
    return make_socket(af, type, proto);
}

static SOCKET WINAPI WSASocketW(int af, int type, int proto, void *info, unsigned group, DWORD flags)
{
    return make_socket(af, type, proto);
}

static int WINAPI closesocket(SOCKET s) { return close(ws_fd(s)) ? errno_fail() : 0; }

static int WINAPI bind_(SOCKET s, const void *addr, int len)
{
    struct sockaddr_storage h;
    socklen_t hl;
    if (addr_in(addr, len, &h, &hl))
        return ws_fail(WSAEFAULT);
    return bind(ws_fd(s), (struct sockaddr *)&h, hl) ? errno_fail() : 0;
}

static int WINAPI listen_(SOCKET s, int backlog)
{
    return listen(ws_fd(s), backlog < 0 || backlog > 4096 ? 4096 : backlog) ? errno_fail() : 0;
}

static SOCKET WINAPI accept_(SOCKET s, void *addr, int *len)
{
    struct sockaddr_storage h;
    socklen_t hl = sizeof h;
    int fd;
    do
        fd = accept4(ws_fd(s), (struct sockaddr *)&h, &hl, SOCK_CLOEXEC);
    while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        errno_fail();
        return INVALID_SOCKET;
    }
    if (addr_out((struct sockaddr *)&h, hl, addr, len)) {
        close(fd);
        ws_fail(WSAEFAULT);
        return INVALID_SOCKET;
    }
    return fd;
}

static int WINAPI connect_(SOCKET s, const void *addr, int len)
{
    struct sockaddr_storage h;
    socklen_t hl;
    if (addr_in(addr, len, &h, &hl))
        return ws_fail(WSAEFAULT);
    int r;
    do
        r = connect(ws_fd(s), (struct sockaddr *)&h, hl);
    while (r && errno == EINTR);
    return r ? errno_fail() : 0;
}

static int flags_in(int f) { return (f & 0x7) | (f & W_MSG_WAITALL ? MSG_WAITALL : 0) | MSG_NOSIGNAL; }

static int WINAPI send_(SOCKET s, const char *buf, int len, int flags)
{
    ssize_t n;
    do
        n = send(ws_fd(s), buf, len, flags_in(flags));
    while (n < 0 && errno == EINTR);
    return n < 0 ? errno_fail() : (int)n;
}

static int WINAPI recv_(SOCKET s, char *buf, int len, int flags)
{
    ssize_t n;
    do
        n = recv(ws_fd(s), buf, len, flags_in(flags) & ~MSG_NOSIGNAL);
    while (n < 0 && errno == EINTR);
    return n < 0 ? errno_fail() : (int)n;
}

static int WINAPI sendto_(SOCKET s, const char *buf, int len, int flags, const void *to, int tolen)
{
    struct sockaddr_storage h;
    socklen_t hl = 0;
    if (to && addr_in(to, tolen, &h, &hl))
        return ws_fail(WSAEFAULT);
    ssize_t n = sendto(ws_fd(s), buf, len, flags_in(flags), to ? (struct sockaddr *)&h : NULL, hl);
    return n < 0 ? errno_fail() : (int)n;
}

static int WINAPI recvfrom_(SOCKET s, char *buf, int len, int flags, void *from, int *fromlen)
{
    struct sockaddr_storage h;
    socklen_t hl = sizeof h;
    ssize_t n;
    do
        n = recvfrom(ws_fd(s), buf, len, flags_in(flags) & ~MSG_NOSIGNAL, (struct sockaddr *)&h, &hl);
    while (n < 0 && errno == EINTR);
    if (n < 0)
        return errno_fail();
    if (from && addr_out((struct sockaddr *)&h, hl, from, fromlen))
        return ws_fail(WSAEFAULT);
    return n;
}

static int WINAPI shutdown_(SOCKET s, int how) { return shutdown(ws_fd(s), how) ? errno_fail() : 0; }

static int WINAPI getsockname_(SOCKET s, void *addr, int *len)
{
    struct sockaddr_storage h;
    socklen_t hl = sizeof h;
    if (getsockname(ws_fd(s), (struct sockaddr *)&h, &hl))
        return errno_fail();
    return addr_out((struct sockaddr *)&h, hl, addr, len) ? ws_fail(WSAEFAULT) : 0;
}

static int WINAPI getpeername_(SOCKET s, void *addr, int *len)
{
    struct sockaddr_storage h;
    socklen_t hl = sizeof h;
    if (getpeername(ws_fd(s), (struct sockaddr *)&h, &hl))
        return errno_fail();
    return addr_out((struct sockaddr *)&h, hl, addr, len) ? ws_fail(WSAEFAULT) : 0;
}

static bool option_in(int level, int name, int *hl, int *hn)
{
    if (level == W_SOL_SOCKET) {
        *hl = SOL_SOCKET;
        switch (name) {
        case W_SO_DEBUG: *hn = SO_DEBUG; return true;
        case W_SO_ACCEPTCONN: *hn = SO_ACCEPTCONN; return true;
        case W_SO_REUSEADDR: *hn = SO_REUSEADDR; return true;
        case W_SO_KEEPALIVE: *hn = SO_KEEPALIVE; return true;
        case W_SO_DONTROUTE: *hn = SO_DONTROUTE; return true;
        case W_SO_BROADCAST: *hn = SO_BROADCAST; return true;
        case W_SO_LINGER: *hn = SO_LINGER; return true;
        case W_SO_OOBINLINE: *hn = SO_OOBINLINE; return true;
        case W_SO_SNDBUF: *hn = SO_SNDBUF; return true;
        case W_SO_RCVBUF: *hn = SO_RCVBUF; return true;
        case W_SO_SNDTIMEO: *hn = SO_SNDTIMEO; return true;
        case W_SO_RCVTIMEO: *hn = SO_RCVTIMEO; return true;
        case W_SO_ERROR: *hn = SO_ERROR; return true;
        case W_SO_TYPE: *hn = SO_TYPE; return true;
        default: return false;
        }
    }
    if (level == IPPROTO_IPV6) {
        *hl = IPPROTO_IPV6;
        *hn = name == W_IPV6_V6ONLY ? IPV6_V6ONLY : name;
        return true;
    }
    if (level == IPPROTO_IP) {
        *hl = IPPROTO_IP;
        switch (name) {
        case W_IP_TTL: *hn = IP_TTL; return true;
        case W_IP_MULTICAST_IF: *hn = IP_MULTICAST_IF; return true;
        case W_IP_MULTICAST_TTL: *hn = IP_MULTICAST_TTL; return true;
        case W_IP_MULTICAST_LOOP: *hn = IP_MULTICAST_LOOP; return true;
        case W_IP_ADD_MEMBERSHIP: *hn = IP_ADD_MEMBERSHIP; return true;
        case W_IP_DROP_MEMBERSHIP: *hn = IP_DROP_MEMBERSHIP; return true;
        default: return false;
        }
    }
    *hl = level;
    *hn = name;
    return true;
}

static int WINAPI setsockopt_(SOCKET s, int level, int name, const char *val, int len)
{
    int fd = ws_fd(s);
    if (level == W_SOL_SOCKET && (name == W_SO_DONTLINGER || name == W_SO_EXCLUSIVEADDRUSE))
        return 0;
    int hl, hn;
    if (!option_in(level, name, &hl, &hn))
        return ws_fail(WSAENOPROTOOPT);
    if (hl == SOL_SOCKET && (hn == SO_RCVTIMEO || hn == SO_SNDTIMEO)) {
        DWORD ms = len >= 4 ? *(const DWORD *)val : 0;
        struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
        return setsockopt(fd, hl, hn, &tv, sizeof tv) ? errno_fail() : 0;
    }
    if (hl == SOL_SOCKET && hn == SO_LINGER) {
        const WLINGER *w = (const WLINGER *)val;
        struct linger l = { w->onoff, w->linger };
        return setsockopt(fd, hl, hn, &l, sizeof l) ? errno_fail() : 0;
    }
    int v = 0;
    if (len >= 4)
        v = *(const int *)val;
    else if (len >= 1)
        v = *(const unsigned char *)val;
    if (hl == IPPROTO_IP && (hn == IP_ADD_MEMBERSHIP || hn == IP_DROP_MEMBERSHIP))
        return setsockopt(fd, hl, hn, val, len) ? errno_fail() : 0;
    return setsockopt(fd, hl, hn, &v, sizeof v) ? errno_fail() : 0;
}

static int WINAPI getsockopt_(SOCKET s, int level, int name, char *val, int *len)
{
    int fd = ws_fd(s), hl, hn;
    if (!option_in(level, name, &hl, &hn))
        return ws_fail(WSAENOPROTOOPT);
    if (hl == SOL_SOCKET && (hn == SO_RCVTIMEO || hn == SO_SNDTIMEO)) {
        struct timeval tv;
        socklen_t tl = sizeof tv;
        if (getsockopt(fd, hl, hn, &tv, &tl))
            return errno_fail();
        *(DWORD *)val = tv.tv_sec * 1000 + tv.tv_usec / 1000;
        *len = 4;
        return 0;
    }
    if (hl == SOL_SOCKET && hn == SO_LINGER) {
        struct linger l;
        socklen_t ll = sizeof l;
        if (getsockopt(fd, hl, hn, &l, &ll))
            return errno_fail();
        *(WLINGER *)val = (WLINGER){ l.l_onoff, l.l_linger };
        *len = sizeof(WLINGER);
        return 0;
    }
    int v = 0;
    socklen_t vl = sizeof v;
    if (getsockopt(fd, hl, hn, &v, &vl))
        return errno_fail();
    if (hl == SOL_SOCKET && hn == SO_ERROR && v)
        v = ws_error_of(v);
    if (*len >= 4) {
        *(int *)val = v;
        *len = 4;
    } else if (*len >= 1) {
        *val = v;
        *len = 1;
    }
    return 0;
}

static int WINAPI ioctlsocket(SOCKET s, LONG cmd, ULONG *arg)
{
    int fd = ws_fd(s);
    switch ((uint32_t)cmd) {
    case W_FIONBIO: {
        int fl = fcntl(fd, F_GETFL);
        if (fl < 0 || fcntl(fd, F_SETFL, *arg ? fl | O_NONBLOCK : fl & ~O_NONBLOCK))
            return errno_fail();
        return 0;
    }
    case W_FIONREAD: {
        int n = 0;
        if (ioctl(fd, FIONREAD, &n))
            return errno_fail();
        *arg = n;
        return 0;
    }
    case W_SIOCATMARK: {
        int n = 0;
        if (ioctl(fd, SIOCATMARK, &n))
            return errno_fail();
        *arg = n;
        return 0;
    }
    default:
        return ws_fail(WSAEINVAL);
    }
}

static int collect(WFD_SET *set, struct pollfd *p, int n, short ev, SOCKET *map)
{
    if (!set)
        return n;
    for (unsigned i = 0; i < set->count && i < W_FD_SETSIZE; i++) {
        int j;
        for (j = 0; j < n; j++)
            if (map[j] == set->fds[i])
                break;
        if (j == n) {
            map[n] = set->fds[i];
            p[n].fd = ws_fd(set->fds[i]);
            p[n].events = 0;
            p[n].revents = 0;
            n++;
        }
        p[j].events |= ev;
    }
    return n;
}

static int filter(WFD_SET *set, struct pollfd *p, int n, SOCKET *map, short mask)
{
    if (!set)
        return 0;
    unsigned out = 0;
    for (unsigned i = 0; i < set->count && i < W_FD_SETSIZE; i++)
        for (int j = 0; j < n; j++)
            if (map[j] == set->fds[i] && (p[j].revents & mask)) {
                set->fds[out++] = set->fds[i];
                break;
            }
    set->count = out;
    return out;
}

static int WINAPI select_(int nfds, WFD_SET *rd, WFD_SET *wr, WFD_SET *ex, const WTIMEVAL *tv)
{
    struct pollfd p[W_FD_SETSIZE * 3];
    SOCKET map[W_FD_SETSIZE * 3];
    int n = 0;
    n = collect(rd, p, n, POLLIN, map);
    n = collect(wr, p, n, POLLOUT, map);
    n = collect(ex, p, n, POLLPRI, map);
    int ms = tv ? tv->sec * 1000 + tv->usec / 1000 : -1;
    int r;
    do
        r = poll(p, n, ms);
    while (r < 0 && errno == EINTR);
    if (r < 0)
        return errno_fail();
    int total = filter(rd, p, n, map, POLLIN | POLLHUP | POLLERR) +
                filter(wr, p, n, map, POLLOUT | POLLERR) + filter(ex, p, n, map, POLLPRI | POLLERR);
    return total;
}

static int WINAPI __WSAFDIsSet(SOCKET s, WFD_SET *set)
{
    for (unsigned i = 0; i < set->count && i < W_FD_SETSIZE; i++)
        if (set->fds[i] == s)
            return 1;
    return 0;
}

static int WINAPI WSAPoll(WPOLLFD *fds, ULONG n, int timeout)
{
    struct pollfd *p = calloc(n ? n : 1, sizeof *p);
    for (ULONG i = 0; i < n; i++) {
        p[i].fd = fds[i].fd == INVALID_SOCKET ? -1 : ws_fd(fds[i].fd);
        p[i].events = (fds[i].events & 0x0300 ? POLLIN : 0) | (fds[i].events & 0x0010 ? POLLOUT : 0) |
                      (fds[i].events & 0x0400 ? POLLPRI : 0);
    }
    int r;
    do
        r = poll(p, n, timeout);
    while (r < 0 && errno == EINTR);
    for (ULONG i = 0; i < n; i++) {
        short re = 0;
        if (p[i].revents & POLLIN)
            re |= 0x0100 | 0x0200;
        if (p[i].revents & POLLOUT)
            re |= 0x0010;
        if (p[i].revents & POLLPRI)
            re |= 0x0400;
        if (p[i].revents & POLLERR)
            re |= 0x0001;
        if (p[i].revents & POLLHUP)
            re |= 0x0002;
        if (p[i].revents & POLLNVAL)
            re |= 0x0004;
        fds[i].revents = re & (fds[i].events | 0x0007);
    }
    free(p);
    return r < 0 ? errno_fail() : r;
}

static int WINAPI WSASend(SOCKET s, WSABUF *bufs, DWORD count, DWORD *sent, DWORD flags, void *ov, void *cb)
{
    DWORD total = 0;
    for (DWORD i = 0; i < count; i++) {
        int n = send_(s, bufs[i].buf, bufs[i].len, flags);
        if (n < 0)
            return SOCKET_ERROR;
        total += n;
        if ((ULONG)n < bufs[i].len)
            break;
    }
    if (sent)
        *sent = total;
    return 0;
}

static int WINAPI WSARecv(SOCKET s, WSABUF *bufs, DWORD count, DWORD *got, DWORD *flags, void *ov, void *cb)
{
    DWORD total = 0;
    for (DWORD i = 0; i < count; i++) {
        int n = recv_(s, bufs[i].buf, bufs[i].len, flags ? *flags : 0);
        if (n < 0) {
            if (total)
                break;
            return SOCKET_ERROR;
        }
        total += n;
        if ((ULONG)n < bufs[i].len)
            break;
    }
    if (got)
        *got = total;
    if (flags)
        *flags = 0;
    return 0;
}

static uint16_t WINAPI htons_(uint16_t v) { return __builtin_bswap16(v); }
static uint32_t WINAPI htonl_(uint32_t v) { return __builtin_bswap32(v); }
static uint64_t WINAPI htonll_(uint64_t v) { return __builtin_bswap64(v); }

BIT_TABLE(ws_socket,
          BIT_FN(WSAStartup), BIT_FN(WSACleanup), BIT_FN(WSAGetLastError), BIT_FN(WSASetLastError),
          BIT_AS("socket", socket_), BIT_FN(WSASocketA), BIT_FN(WSASocketW), BIT_FN(closesocket),
          BIT_AS("bind", bind_), BIT_AS("listen", listen_), BIT_AS("accept", accept_),
          BIT_AS("connect", connect_), BIT_AS("send", send_), BIT_AS("recv", recv_),
          BIT_AS("sendto", sendto_), BIT_AS("recvfrom", recvfrom_), BIT_AS("shutdown", shutdown_),
          BIT_AS("getsockname", getsockname_), BIT_AS("getpeername", getpeername_),
          BIT_AS("setsockopt", setsockopt_), BIT_AS("getsockopt", getsockopt_), BIT_FN(ioctlsocket),
          BIT_AS("select", select_), BIT_FN(__WSAFDIsSet), BIT_FN(WSAPoll), BIT_FN(WSASend), BIT_FN(WSARecv),
          BIT_AS("htons", htons_), BIT_AS("ntohs", htons_), BIT_AS("htonl", htonl_), BIT_AS("ntohl", htonl_),
          BIT_AS("htonll", htonll_), BIT_AS("ntohll", htonll_));
