#include "ws.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <unistd.h>

#define W_EAI_AGAIN WSATRY_AGAIN
#define W_EAI_BADFLAGS WSAEINVAL
#define W_EAI_FAIL WSANO_RECOVERY
#define W_EAI_FAMILY WSAEAFNOSUPPORT
#define W_EAI_MEMORY WSAENOBUFS
#define W_EAI_NONAME WSAHOST_NOT_FOUND
#define W_EAI_SERVICE 10109
#define W_EAI_SOCKTYPE 10044

struct waddrinfo {
    int flags;
    int family;
    int socktype;
    int protocol;
    size_t addrlen;
    char *canonname;
    void *addr;
    struct waddrinfo *next;
};

struct waddrinfo_w {
    int flags;
    int family;
    int socktype;
    int protocol;
    size_t addrlen;
    WCHAR *canonname;
    void *addr;
    struct waddrinfo_w *next;
};

struct whostent {
    char *name;
    char **aliases;
    short addrtype;
    short length;
    char **addr_list;
};

struct wservent {
    char *name;
    char **aliases;
    char *proto;
    short port;
};

struct wprotoent {
    char *name;
    char **aliases;
    short proto;
};

struct host_buf {
    struct whostent h;
    char name[256];
    char *aliases[1];
    char *list[17];
    char addrs[16][16];
};

static __thread struct host_buf host_store;
static __thread struct wservent serv_store;
static __thread char serv_name[64], serv_proto[16];
static __thread struct wprotoent proto_store;
static __thread char proto_name[32];
static __thread char ntoa_buf[16];

static int eai_error(int e)
{
    switch (e) {
    case EAI_AGAIN: return W_EAI_AGAIN;
    case EAI_BADFLAGS: return W_EAI_BADFLAGS;
    case EAI_FAIL: return W_EAI_FAIL;
    case EAI_FAMILY: return W_EAI_FAMILY;
    case EAI_MEMORY: return W_EAI_MEMORY;
    case EAI_SERVICE: return W_EAI_SERVICE;
    case EAI_SOCKTYPE: return W_EAI_SOCKTYPE;
    case EAI_SYSTEM: return ws_error_of(errno);
    default: return W_EAI_NONAME;
    }
}

static int lookup(const char *node, const char *service, const struct waddrinfo *hints, struct addrinfo **out)
{
    struct addrinfo h = { 0 };
    if (hints) {
        h.ai_flags = hints->flags & (AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST | AI_NUMERICSERV | AI_ADDRCONFIG);
        h.ai_family = family_in(hints->family);
        h.ai_socktype = hints->socktype;
        h.ai_protocol = hints->protocol;
    }
    int r = getaddrinfo(node, service, hints ? &h : NULL, out);
    return r ? eai_error(r) : 0;
}

static void *copy_addr(const struct addrinfo *a)
{
    void *p = malloc(a->ai_addrlen);
    memcpy(p, a->ai_addr, a->ai_addrlen);
    ((uint16_t *)p)[0] = family_out(a->ai_addr->sa_family);
    return p;
}

static int WINAPI getaddrinfo_(const char *node, const char *service, const struct waddrinfo *hints,
                               struct waddrinfo **out)
{
    struct addrinfo *res;
    int r = lookup(node, service, hints, &res);
    *out = NULL;
    if (r) {
        last_error_set(r);
        return r;
    }
    struct waddrinfo **tail = out;
    for (struct addrinfo *a = res; a; a = a->ai_next) {
        struct waddrinfo *w = calloc(1, sizeof *w);
        w->flags = a->ai_flags;
        w->family = family_out(a->ai_family);
        w->socktype = a->ai_socktype;
        w->protocol = a->ai_protocol;
        w->addrlen = a->ai_addrlen;
        w->canonname = a->ai_canonname ? strdup(a->ai_canonname) : NULL;
        w->addr = copy_addr(a);
        *tail = w;
        tail = &w->next;
    }
    freeaddrinfo(res);
    return 0;
}

static void WINAPI freeaddrinfo_(struct waddrinfo *a)
{
    while (a) {
        struct waddrinfo *n = a->next;
        free(a->canonname);
        free(a->addr);
        free(a);
        a = n;
    }
}

static int WINAPI GetAddrInfoW(const WCHAR *node, const WCHAR *service, const struct waddrinfo_w *hints,
                               struct waddrinfo_w **out)
{
    char *n = wide_to_host(node), *s = wide_to_host(service);
    struct waddrinfo h, *res;
    if (hints)
        h = (struct waddrinfo){ hints->flags, hints->family, hints->socktype, hints->protocol, 0, NULL, NULL, NULL };
    int r = getaddrinfo_(n, s, hints ? &h : NULL, &res);
    free(n);
    free(s);
    *out = NULL;
    if (r)
        return r;
    struct waddrinfo_w **tail = out;
    for (struct waddrinfo *a = res; a; a = a->next) {
        struct waddrinfo_w *w = calloc(1, sizeof *w);
        w->flags = a->flags;
        w->family = a->family;
        w->socktype = a->socktype;
        w->protocol = a->protocol;
        w->addrlen = a->addrlen;
        w->canonname = a->canonname ? host_to_wide(a->canonname) : NULL;
        w->addr = a->addr;
        a->addr = NULL;
        *tail = w;
        tail = &w->next;
    }
    freeaddrinfo_(res);
    return 0;
}

static void WINAPI FreeAddrInfoW(struct waddrinfo_w *a)
{
    while (a) {
        struct waddrinfo_w *n = a->next;
        free(a->canonname);
        free(a->addr);
        free(a);
        a = n;
    }
}

static int WINAPI getnameinfo_(const void *addr, int len, char *host, DWORD hostlen, char *serv, DWORD servlen,
                               int flags)
{
    struct sockaddr_storage h;
    socklen_t hl;
    if (addr_in(addr, len, &h, &hl))
        return WSAEFAULT;
    int r = getnameinfo((struct sockaddr *)&h, hl, host, hostlen, serv, servlen, flags & 0x1F);
    return r ? eai_error(r) : 0;
}

static struct whostent *fill_host(struct hostent *he)
{
    if (!he) {
        last_error_set(h_errno == TRY_AGAIN ? WSATRY_AGAIN : h_errno == NO_DATA ? WSANO_DATA : WSAHOST_NOT_FOUND);
        return NULL;
    }
    struct host_buf *b = &host_store;
    memset(b, 0, sizeof *b);
    snprintf(b->name, sizeof b->name, "%s", he->h_name ? he->h_name : "");
    int n = 0;
    for (; he->h_addr_list[n] && n < 16; n++) {
        memcpy(b->addrs[n], he->h_addr_list[n], he->h_length);
        b->list[n] = b->addrs[n];
    }
    b->h = (struct whostent){ b->name, b->aliases, family_out(he->h_addrtype), he->h_length, b->list };
    return &b->h;
}

static struct whostent *WINAPI gethostbyname_(const char *name)
{
    char self[256];
    if (!name || !*name) {
        gethostname(self, sizeof self);
        name = self;
    }
    struct hostent he, *res = NULL;
    char buf[4096];
    int err = 0;
    gethostbyname_r(name, &he, buf, sizeof buf, &res, &err);
    h_errno = err;
    return fill_host(res);
}

static struct whostent *WINAPI gethostbyaddr_(const char *addr, int len, int type)
{
    struct hostent he, *res = NULL;
    char buf[4096];
    int err = 0;
    gethostbyaddr_r(addr, len, family_in(type), &he, buf, sizeof buf, &res, &err);
    h_errno = err;
    return fill_host(res);
}

static int WINAPI gethostname_(char *name, int len)
{
    if (gethostname(name, len))
        return ws_fail(WSAEFAULT);
    return 0;
}

static int WINAPI GetHostNameW(WCHAR *name, int len)
{
    char buf[256];
    gethostname(buf, sizeof buf);
    if (utf8_to_utf16(buf, -1, name, len) < 0)
        return ws_fail(WSAEFAULT);
    return 0;
}

static struct wservent *WINAPI getservbyname_(const char *name, const char *proto)
{
    struct servent *s = getservbyname(name, proto);
    if (!s) {
        last_error_set(WSANO_DATA);
        return NULL;
    }
    snprintf(serv_name, sizeof serv_name, "%s", s->s_name);
    snprintf(serv_proto, sizeof serv_proto, "%s", s->s_proto);
    serv_store = (struct wservent){ serv_name, NULL, serv_proto, (short)s->s_port };
    return &serv_store;
}

static struct wservent *WINAPI getservbyport_(int port, const char *proto)
{
    struct servent *s = getservbyport(port, proto);
    if (!s) {
        last_error_set(WSANO_DATA);
        return NULL;
    }
    snprintf(serv_name, sizeof serv_name, "%s", s->s_name);
    snprintf(serv_proto, sizeof serv_proto, "%s", s->s_proto);
    serv_store = (struct wservent){ serv_name, NULL, serv_proto, (short)s->s_port };
    return &serv_store;
}

static struct wprotoent *WINAPI getprotobyname_(const char *name)
{
    struct protoent *p = getprotobyname(name);
    if (!p) {
        last_error_set(WSANO_DATA);
        return NULL;
    }
    snprintf(proto_name, sizeof proto_name, "%s", p->p_name);
    proto_store = (struct wprotoent){ proto_name, NULL, (short)p->p_proto };
    return &proto_store;
}

static ULONG WINAPI inet_addr_(const char *s)
{
    struct in_addr a;
    return s && inet_aton(s, &a) ? a.s_addr : 0xFFFFFFFFu;
}

static char *WINAPI inet_ntoa_(struct in_addr a)
{
    inet_ntop(AF_INET, &a, ntoa_buf, sizeof ntoa_buf);
    return ntoa_buf;
}

static int WINAPI inet_pton_(int af, const char *src, void *dst)
{
    int r = inet_pton(family_in(af), src, dst);
    if (r < 0)
        ws_fail(WSAEAFNOSUPPORT);
    return r;
}

static const char *WINAPI inet_ntop_(int af, const void *src, char *dst, size_t len)
{
    const char *r = inet_ntop(family_in(af), src, dst, len);
    if (!r)
        ws_fail(ws_error_of(errno));
    return r;
}

static int WINAPI InetPtonW(int af, const WCHAR *src, void *dst)
{
    char *s = wide_to_host(src);
    int r = inet_pton_(af, s, dst);
    free(s);
    return r;
}

static const WCHAR *WINAPI InetNtopW(int af, const void *src, WCHAR *dst, size_t len)
{
    char buf[64];
    if (!inet_ntop_(af, src, buf, sizeof buf))
        return NULL;
    return utf8_to_utf16(buf, -1, dst, len) < 0 ? NULL : dst;
}

BIT_TABLE(ws_resolve,
          BIT_AS("getaddrinfo", getaddrinfo_), BIT_AS("freeaddrinfo", freeaddrinfo_),
          BIT_FN(GetAddrInfoW), BIT_FN(FreeAddrInfoW), BIT_AS("getnameinfo", getnameinfo_),
          BIT_AS("gethostbyname", gethostbyname_), BIT_AS("gethostbyaddr", gethostbyaddr_),
          BIT_AS("gethostname", gethostname_), BIT_FN(GetHostNameW),
          BIT_AS("getservbyname", getservbyname_), BIT_AS("getservbyport", getservbyport_),
          BIT_AS("getprotobyname", getprotobyname_),
          BIT_AS("inet_addr", inet_addr_), BIT_AS("inet_ntoa", inet_ntoa_),
          BIT_AS("inet_pton", inet_pton_), BIT_AS("InetPtonA", inet_pton_), BIT_FN(InetPtonW),
          BIT_AS("inet_ntop", inet_ntop_), BIT_AS("InetNtopA", inet_ntop_), BIT_FN(InetNtopW));

BIT_DEFINE(bit_ws2_32, "ws2_32.dll", &ws_socket, &ws_resolve);
