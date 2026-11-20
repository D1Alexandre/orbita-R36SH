#include "https.h"
#include "xc_port.h"
#include "xc_port_linux.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *cancel_later(void *unused) {
    (void)unused;
    xcn_sleep_ms(150);
    orb_linux_cancel(1);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 5) return 2;
    const char *mode = argv[1];
    int expected = atoi(argv[4]);
    https_resp_t r;
    xc_err_t e;
    pthread_t thread;
    int async_cancel = !strcmp(mode, "cancel-running");
    orb_linux_cancel(!strcmp(mode, "cancel"));
    if (!strcmp(mode, "persistent")) {
        https_conn_t *conn = https_conn_new(argv[2]);
        if (!conn) return 1;
        int ok = 1;
        for (int i = 0; i < 3; ++i) {
            const char *payload = i == 1 ? "fake-request" : NULL;
            int rc = https_conn_request(conn, "keep-alive", payload ? "POST" : "GET", argv[3],
                        payload ? "Content-Type: text/plain\r\nX-Test: contract\r\n" : NULL,
                        payload, payload ? strlen(payload) : 0, 4096, &r, &e);
            ok &= rc == 0 && r.status == 200 && r.body && !strcmp(r.body, payload ? payload : "hello");
            https_free(&r);
        }
        https_conn_stats_t stats;
        https_conn_get_stats(conn, &stats);
        ok &= stats.requests == 3 && stats.full == 1 && stats.reused == 2;
        printf("persistent requests=%u connections=%u reused=%u ok=%d\n", stats.requests, stats.full, stats.reused, ok);
        https_conn_free(conn);
        return ok ? 0 : 1;
    }
    if (async_cancel && pthread_create(&thread, NULL, cancel_later, NULL)) return 2;
    const char *body = !strcmp(mode, "post") ? "fake-request" : NULL;
    int rc = https_request("test", body ? "POST" : "GET", argv[2], argv[3],
                           body ? "Content-Type: text/plain\r\nX-Test: contract\r\n" : NULL,
                           body, body ? strlen(body) : 0, !strcmp(mode, "large") ? 8 : 4096, &r, &e);
    if (async_cancel) pthread_join(thread, NULL);
    int ok;
    if (!strcmp(mode, "cert")) ok = rc == HTTPS_ECERT;
    else if (!strcmp(mode, "large")) ok = rc == HTTPS_ETOOBIG;
    else if (!strncmp(mode, "cancel", 6)) ok = rc == HTTPS_ECANCEL;
    else if (!strcmp(mode, "invalid")) ok = rc == HTTPS_EPROTO;
    else ok = rc == 0 && r.status == expected &&
              (expected != 200 || (r.body && !strcmp(r.body, body ? "fake-request" : "hello")));
    printf("mode=%s rc=%d status=%d length=%zu ok=%d\n", mode, rc, r.status, r.len, ok);
    https_free(&r);
    return ok ? 0 : 1;
}
