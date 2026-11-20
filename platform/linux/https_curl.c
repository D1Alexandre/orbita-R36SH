/* Linux transport for the existing Microsoft login/catalog core.
 * libcurl uses ArkOS's DNS, system CA bundle and clock. No beta/site calls.
 */
#define _GNU_SOURCE
#include "https.h"
#include "xc_port.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_once_t once = PTHREAD_ONCE_INIT;
static CURLcode init_rc;
static void curl_init_once(void) { init_rc = curl_global_init(CURL_GLOBAL_DEFAULT); }

int https_init(xc_err_t *e) {
    pthread_once(&once, curl_init_once);
    if (init_rc == CURLE_OK) return 0;
    if (e) { e->code = HTTPS_EINIT; snprintf(e->msg, sizeof(e->msg), "Falha ao iniciar HTTPS"); }
    return HTTPS_EINIT;
}

typedef struct {
    char *body;
    size_t len, limit;
    int too_big;
} buffer_t;

static size_t receive(char *data, size_t size, size_t count, void *ctx) {
    buffer_t *b = ctx;
    if (size && count > SIZE_MAX / size) return 0;
    size_t n = size * count;
    if (n > b->limit - b->len) { b->too_big = 1; return 0; }
    char *next = realloc(b->body, b->len + n + 1);
    if (!next) return 0;
    b->body = next;
    memcpy(b->body + b->len, data, n);
    b->len += n;
    b->body[b->len] = 0;
    return n;
}

static int progress(void *ctx, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
    (void)ctx; (void)a; (void)b; (void)c; (void)d;
    return xcn_cancelled();
}

static int error_code(CURLcode rc) {
    switch (rc) {
    case CURLE_ABORTED_BY_CALLBACK: return HTTPS_ECANCEL;
    case CURLE_OPERATION_TIMEDOUT: return HTTPS_ETIMEOUT;
    case CURLE_COULDNT_RESOLVE_HOST: return HTTPS_EDNS;
    case CURLE_COULDNT_CONNECT: return HTTPS_ETCP;
    case CURLE_PEER_FAILED_VERIFICATION: case CURLE_SSL_CACERT_BADFILE: return HTTPS_ECERT;
    case CURLE_SSL_CONNECT_ERROR: return HTTPS_ETLS;
    default: return HTTPS_EPROTO;
    }
}

static int request_with_curl(CURL *curl, const char *step, const char *method, const char *host,
                  const char *path, const char *headers, const char *body,
                  size_t body_len, size_t max_body, https_resp_t *r, xc_err_t *e) {
    memset(r, 0, sizeof(*r));
    if (e) memset(e, 0, sizeof(*e));
    if (https_init(e)) return HTTPS_EINIT;
    if (!host || !*host || strpbrk(host, "/?#@\\\r\n") || !path || *path != '/' ||
        strpbrk(path, "\r\n") || !method || strpbrk(method, " \r\n")) return HTTPS_EPROTO;
    if (xcn_cancelled()) { if (e) e->code = HTTPS_ECANCEL; return HTTPS_ECANCEL; }
    size_t url_len = strlen(host) + strlen(path) + 9;
    char *url = malloc(url_len);
    if (!url) return HTTPS_EINIT;
    snprintf(url, url_len, "https://%s%s", host, path);
    /* Reset options, retaining the easy handle's DNS/TLS/connection caches. */
    curl_easy_reset(curl);
    struct curl_slist *list = NULL;
    char *copy = headers ? strdup(headers) : NULL;
    char *save = NULL;
    for (char *line = copy ? strtok_r(copy, "\r\n", &save) : NULL;
         line; line = strtok_r(NULL, "\r\n", &save)) {
        struct curl_slist *next = curl_slist_append(list, line);
        if (!next) { curl_slist_free_all(list); free(copy); free(url); return HTTPS_EINIT; }
        list = next;
    }
    buffer_t b = { .limit = max_body };
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 15000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 45000L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "orbita-r36s/0.1");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    if (body) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body_len);
    }
    /* Test harness adds a CA, without disabling verification or changing URLs. */
#ifdef ORBITA_TRANSPORT_TEST
    const char *ca = getenv("ORBITA_TEST_CA");
    if (ca) curl_easy_setopt(curl, CURLOPT_CAINFO, ca);
    curl_easy_setopt(curl, CURLOPT_PROXY, "");
#endif
    CURLcode rc = curl_easy_perform(curl);
    int result = 0;
    if (rc == CURLE_OK) {
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        r->status = (int)status;
        r->body = b.body ? b.body : calloc(1, 1);
        r->len = b.len;
        if (!r->body) result = HTTPS_EINIT;
    } else {
        result = b.too_big ? HTTPS_ETOOBIG : error_code(rc);
        free(b.body);
    }
    if (result && e) {
        e->code = result;
        /* No URL, body, credential headers, or raw provider response in logs. */
        snprintf(e->msg, sizeof(e->msg), "HTTPS em %s: %s", step ? step : "rede",
                 b.too_big ? "resposta grande demais" : curl_easy_strerror(rc));
    }
    curl_slist_free_all(list);
    /* The caller owns the handle. No pointer to request-local memory survives. */
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, NULL);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
    free(copy);
    free(url);
    return result;
}

int https_request(const char *step, const char *method, const char *host,
                  const char *path, const char *headers, const char *body,
                  size_t body_len, size_t max_body, https_resp_t *r, xc_err_t *e) {
    memset(r, 0, sizeof(*r));
    if (https_init(e)) return HTTPS_EINIT;
    CURL *curl = curl_easy_init();
    if (!curl) return HTTPS_EINIT;
    int rc = request_with_curl(curl, step, method, host, path, headers, body, body_len, max_body, r, e);
    curl_easy_cleanup(curl);
    return rc;
}

/* Linux signaling runs on a dedicated thread, independently of the UDP/media
 * pump. It uses synchronous keep-alive; CPU handoff APIs explicitly decline
 * asynchronous work instead of ever blocking the WebRTC thread. */
struct https_conn {
    char *host;
    CURL *curl;
    int open;
    https_conn_stats_t stats;
};
https_conn_t *https_conn_new(const char *host) {
    if (!host || https_init(NULL)) return NULL;
    https_conn_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->host = strdup(host);
    c->curl = curl_easy_init();
    if (!c->host || !c->curl) { https_conn_free(c); return NULL; }
    return c;
}
void https_conn_free(https_conn_t *c) {
    if (!c) return;
    if (c->curl) curl_easy_cleanup(c->curl);
    free(c->host);
    free(c);
}
const char *https_conn_host(const https_conn_t *c) { return c ? c->host : ""; }
int https_conn_is_open(const https_conn_t *c) { return c && c->open; }
int https_conn_busy(const https_conn_t *c) { (void)c; return 0; }
int https_conn_request(https_conn_t *c, const char *step, const char *method,
                       const char *path, const char *headers, const char *body,
                       size_t body_len, size_t max_body, https_resp_t *r, xc_err_t *e) {
    if (!c) { memset(r, 0, sizeof(*r)); return HTTPS_EINIT; }
    int rc = request_with_curl(c->curl, step, method, c->host, path, headers, body, body_len, max_body, r, e);
    c->open = rc == 0;
    if (!rc) {
        long connections = 0;
        curl_easy_getinfo(c->curl, CURLINFO_NUM_CONNECTS, &connections);
        c->stats.requests++;
        c->stats.full += (unsigned)connections;
        c->stats.reused += connections == 0;
    }
    return rc;
}
int https_conn_start(https_conn_t *c, const char *step, const char *method,
                     const char *path, const char *headers, const char *body,
                     size_t body_len, size_t max_body, xc_err_t *e) {
    (void)step; (void)method; (void)path; (void)headers; (void)body; (void)body_len; (void)max_body;
    if (c) c->stats.needcpu++;
    if (e) { memset(e, 0, sizeof(*e)); e->code = HTTPS_ENEEDCPU; }
    return HTTPS_ENEEDCPU;
}
int https_conn_poll(https_conn_t *c, https_resp_t *r, xc_err_t *e) {
    (void)c;
    memset(r, 0, sizeof(*r));
    if (e) { memset(e, 0, sizeof(*e)); e->code = HTTPS_ENEEDCPU; }
    return HTTPS_ENEEDCPU;
}
void https_conn_abort(https_conn_t *c) {
    if (c) { curl_easy_reset(c->curl); c->open = 0; }
}
void https_conn_get_stats(const https_conn_t *c, https_conn_stats_t *s) {
    if (c) *s = c->stats;
    else memset(s, 0, sizeof(*s));
}

void https_free(https_resp_t *r) {
    if (!r) return;
    if (r->body) {
        volatile char *p = r->body;
        for (size_t i = 0; i < r->len; ++i) p[i] = 0;
        free(r->body);
    }
    memset(r, 0, sizeof(*r));
}

int https_random(unsigned char *out, size_t n) {
    return xc_seed_material(out, n) == n ? 0 : -1;
}

int https_probe(const char *host, xc_err_t *e) {
    https_resp_t r;
    int rc = https_request("conexao", "HEAD", host, "/", NULL, NULL, 0, 0, &r, e);
    https_free(&r);
    return rc;
}
