#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

static SOCKET listener;
static unsigned short port;

static DWORD WINAPI server(void *arg)
{
    SOCKET c = accept(listener, NULL, NULL);
    char buf[64];
    int n = recv(c, buf, sizeof buf - 1, 0);
    buf[n > 0 ? n : 0] = 0;
    char reply[96];
    snprintf(reply, sizeof reply, "echo:%s", buf);
    send(c, reply, (int)strlen(reply), 0);
    closesocket(c);
    return 0;
}

int main(void)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) {
        printf("startup failed\n");
        return 1;
    }
    printf("winsock %d.%d\n", LOBYTE(wsa.wVersion), HIBYTE(wsa.wVersion));

    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(listener, (struct sockaddr *)&a, sizeof a);
    int len = sizeof a;
    getsockname(listener, (struct sockaddr *)&a, &len);
    port = ntohs(a.sin_port);
    listen(listener, 1);
    HANDLE t = CreateThread(NULL, 0, server, NULL, 0, NULL);

    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(s, (struct sockaddr *)&a, sizeof a)) {
        printf("connect failed %d\n", WSAGetLastError());
        return 1;
    }
    send(s, "ping", 4, 0);
    char buf[64] = "";
    int n = recv(s, buf, sizeof buf - 1, 0);
    buf[n > 0 ? n : 0] = 0;
    printf("tcp loopback: %s\n", buf);
    closesocket(s);
    WaitForSingleObject(t, INFINITE);
    closesocket(listener);

    SOCKET bad = socket(AF_INET, SOCK_STREAM, 0);
    a.sin_port = htons(1);
    printf("refused: %s\n", connect(bad, (struct sockaddr *)&a, sizeof a) && WSAGetLastError() == WSAECONNREFUSED ? "WSAECONNREFUSED" : "unexpected");
    closesocket(bad);

    struct addrinfo hints = { 0 }, *res = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int r = getaddrinfo("localhost", "80", &hints, &res);
    int v4 = 0, v6 = 0;
    for (struct addrinfo *p = res; p; p = p->ai_next) {
        if (p->ai_family == AF_INET) v4++;
        if (p->ai_family == AF_INET6) v6++;
    }
    printf("getaddrinfo localhost: r=%d has_v4=%d\n", r, v4 > 0);
    freeaddrinfo(res);

    struct in6_addr six;
    char text[64];
    inet_pton(AF_INET6, "2001:db8::1", &six);
    inet_ntop(AF_INET6, &six, text, sizeof text);
    printf("inet6: %s\n", text);
    WSACleanup();
    return 0;
}
