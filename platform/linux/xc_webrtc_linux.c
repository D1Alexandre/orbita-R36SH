#define _GNU_SOURCE
#include "xcw_port.h"
#include "xc_port.h"
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t session_lock = PTHREAD_MUTEX_INITIALIZER;
void xs_lock(void) { pthread_mutex_lock(&session_lock); }
void xs_unlock(void) { pthread_mutex_unlock(&session_lock); }

int xcw_udp_open(uint16_t port, uint16_t *bound_port) {
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    int bytes = 4 * 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes));
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port) };
    socklen_t n = sizeof(a);
    if (bind(fd, (struct sockaddr *)&a, n) || getsockname(fd, (struct sockaddr *)&a, &n)) {
        close(fd); return -1;
    }
    *bound_port = ntohs(a.sin_port);
    return fd;
}
void xcw_udp_close(int fd) { if (fd >= 0) close(fd); }
int xcw_udp_send(int fd, uint32_t ip, uint16_t port, const void *buf, int len) {
    if (len < 0) return -1;
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port),
                             .sin_addr.s_addr = htonl(ip) };
    return (int)sendto(fd, buf, (size_t)len, MSG_DONTWAIT, (struct sockaddr *)&a, sizeof(a));
}
int xcw_udp_recv(int fd, uint32_t *ip, uint16_t *port, void *buf, int cap) {
    if (cap <= 0) return -1;
    struct sockaddr_in a;
    socklen_t n = sizeof(a);
    ssize_t r = recvfrom(fd, buf, (size_t)cap, MSG_DONTWAIT | MSG_TRUNC, (struct sockaddr *)&a, &n);
    if (r < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
    if (r > cap) return 0; /* Never pass a truncated SRTP datagram onward. */
    *ip = ntohl(a.sin_addr.s_addr);
    *port = ntohs(a.sin_port);
    return (int)r;
}
int xcw_local_ipv4(uint32_t *ip) {
    struct in_addr chosen;
    const char *override = getenv("ORBITA_LOCAL_IP");
    if (override) {
        if (inet_pton(AF_INET, override, &chosen) != 1) return -1;
        *ip = ntohl(chosen.s_addr); return 0;
    }
    /* UDP connect asks the kernel for its route; it sends no datagram. */
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    struct sockaddr_in route = { .sin_family = AF_INET, .sin_port = htons(53) };
    inet_pton(AF_INET, "1.1.1.1", &route.sin_addr);
    socklen_t len = sizeof(route);
    if (fd >= 0) {
        int ok = !connect(fd, (struct sockaddr *)&route, len) &&
                 !getsockname(fd, (struct sockaddr *)&route, &len);
        close(fd);
        if (ok && route.sin_addr.s_addr) { *ip = ntohl(route.sin_addr.s_addr); return 0; }
    }
    struct ifaddrs *all = NULL;
    if (getifaddrs(&all)) return -1;
    int result = -1;
    for (struct ifaddrs *p = all; p; p = p->ifa_next) {
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET ||
            !(p->ifa_flags & IFF_UP) || (p->ifa_flags & IFF_LOOPBACK)) continue;
        *ip = ntohl(((struct sockaddr_in *)p->ifa_addr)->sin_addr.s_addr);
        result = 0; break;
    }
    freeifaddrs(all);
    return result;
}
int xcw_resolve(const char *host, uint32_t *ip) {
    struct addrinfo hint = { .ai_family = AF_INET, .ai_socktype = SOCK_DGRAM }, *result;
    if (getaddrinfo(host, NULL, &hint, &result)) return -1;
    *ip = ntohl(((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr);
    freeaddrinfo(result); return 0;
}
uint64_t xcw_now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000 + (uint64_t)t.tv_nsec / 1000;
}
uint64_t xcw_now_ms(void) { return xcw_now_us() / 1000; }
void xcw_idle(int ms) { xcn_sleep_ms(ms > 0 ? (unsigned)ms : 0); }
void xcw_random(void *buf, size_t n) {
    if (xc_seed_material(buf, n) != n) { fputs("WebRTC: falha de entropia\n", stderr); abort(); }
}
void xcw_log(const char *line) {
    /* Data-channel payloads/SDP can contain identifiers or authorization data.
     * Keep state/codec/transport diagnostics, never their raw contents. */
    if (strstr(line, "[dc] recv") || strstr(line, "[dc] send [") ||
        strstr(line, "[sdp] offer") || strstr(line, "[sdp] answer") ||
        strstr(line, "[ice] +remote") || strstr(line, "ice-ufrag") ||
        strstr(line, "ice-pwd") || strstr(line, "a=fingerprint")) return;
    xc_log("[rtc] %s", line);
}
int mbedtls_hardware_poll(void *unused, unsigned char *out, size_t n, size_t *written) {
    (void)unused;
    *written = xc_seed_material(out, n);
    return *written == n ? 0 : -1;
}
