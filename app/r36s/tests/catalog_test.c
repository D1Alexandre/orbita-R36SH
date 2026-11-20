/* Catalog policy regression: real auth/catalog logic with synthetic responses.
 * No network, account credentials, or assumptions about the user's subscription. */
#include "login_flow.h"
#include "mock_port.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks, names_calls, fail_br, fail_names;
static char *titles;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); ++failures; } } while (0)

static int respond(void *unused, const mock_req_t *q, int *status, char **body) {
    (void)unused;
    *status = 200;
    const char *reply = NULL;
    if (!strcmp(q->host, "login.microsoftonline.com")) reply = "{\"access_token\":\"test-access\"}";
    else if (!strcmp(q->host, "user.auth.xboxlive.com") || !strcmp(q->host, "xsts.auth.xboxlive.com"))
        reply = "{\"Token\":\"test-xbox\"}";
    else if (!strcmp(q->path, "/v2/login/user")) {
        reply = strstr(q->host, "xgpuwebf2p.") ?
            "{\"gsToken\":\"test-cloud\",\"offeringSettings\":{\"regions\":[{\"isDefault\":true,\"baseUri\":\"https://f2p.invalid\"}]}}" :
            "{\"gsToken\":\"test-cloud\",\"offeringSettings\":{\"regions\":[{\"isDefault\":true,\"baseUri\":\"https://cloud.invalid\"}]}}";
    } else if (!strcmp(q->path, "/v2/titles")) reply = !strcmp(q->host, "cloud.invalid") ? titles : "{\"results\":[]}";
    else if (!strcmp(q->host, "displaycatalog.mp.microsoft.com")) {
        ++names_calls;
        CHECK(strstr(q->path, "fieldsTemplate=Browse") != NULL);
        int br = strstr(q->path, "market=BR") != NULL;
        if (fail_names || (fail_br && br)) { *status = 503; reply = "{}"; }
        else {
            char ids[256];
            const char *start = strstr(q->path, "bigIds=");
            CHECK(start != NULL);
            snprintf(ids, sizeof(ids), "%s", start ? start + 7 : "");
            char *end = strchr(ids, '&');
            if (end) *end = 0;
            size_t cap = 8192, pos = 0;
            *body = malloc(cap);
            pos += (size_t)snprintf(*body, cap, "{\"Products\":[");
            int count = 0;
            for (char *id = strtok(ids, ","); id; id = strtok(NULL, ",")) {
                pos += (size_t)snprintf(*body + pos, cap - pos,
                    "%s{\"ProductId\":\"%s\",\"LocalizedProperties\":[{\"ProductTitle\":\"Game %s\"}]}",
                    count++ ? "," : "", id, id);
            }
            snprintf(*body + pos, cap - pos, "]}");
            return 0;
        }
    } else { CHECK(0); *status = 501; reply = "{}"; }
    *body = strdup(reply);
    return 0;
}

static void setup(void) {
    mock_reset();
    mock_set_token("synthetic-refresh-token-long-enough");
    g_mock_handler = respond;
    names_calls = fail_br = fail_names = 0;
}

int main(void) {
    /* Mirrors the observed count, with only two confirmed grants. The other
     * titles must remain visible without gaining an entitlement flag. */
    const int total = 2813;
    size_t cap = (size_t)total * 220, pos = 0;
    titles = malloc(cap);
    pos += (size_t)snprintf(titles, cap, "{\"results\":[");
    for (int i = 0; i < total; ++i) pos += (size_t)snprintf(titles + pos, cap - pos,
        "%s{\"titleId\":\"T%04d\",\"details\":{\"productId\":\"P%04d\",\"hasEntitlement\":%s,"
        "\"programs\":[\"GPULTIMATE\"],\"userSubscriptions\":[]}}", i ? "," : "", i, i, i < 2 ? "true" : "false");
    snprintf(titles + pos, cap - pos, "]}");

    xa_title_t *games = NULL;
    int n = 0;
    xc_err_t error = {0};
    setup();
    CHECK(lf_run_catalog(NULL, &games, &n, &error, 1) == 0);
    CHECK(n == total);
    CHECK(names_calls == (total + 19) / 20);
    int playable = 0;
    for (int i = 0; i < n; ++i) {
        CHECK(games[i].name[0] != 0);
        CHECK(games[i].flags == (unsigned)(XA_F_SUB | (i < 2 ? XA_F_OWNED : 0)));
        if (i) CHECK(strcmp(games[i - 1].name, games[i].name) < 0);
        playable += xa_playable(&games[i]);
    }
    CHECK(playable == 2);
    CHECK(n && !strcmp(games[n - 1].id, "T2812"));
    char *cache = xa_names_to_cache(games, n);
    CHECK(cache != NULL);
    free(games);
    games = NULL;
    CHECK(xa_parse_titles(titles, &games, &n) == 0);
    CHECK(cache && xa_names_from_cache(cache, games, n) == total);
    names_calls = 0;
    CHECK(xa_fetch_names_all(games, n, NULL, &error) == 0 && names_calls == 0);
    free(cache);
    free(games);

    setup();
    CHECK(lf_run(NULL, &games, &n, &error) == 0);
    CHECK(n == 2 && names_calls == 1);
    free(games);

    /* A metadata outage must not remove titles or grant access. */
    setup(); fail_names = 1;
    CHECK(lf_run_catalog(NULL, &games, &n, &error, 1) == 0 && n == total);
    CHECK(names_calls == 6);
    CHECK(n && games[0].name[0] == 0 && xa_playable(&games[0]));
    CHECK(n > 2 && !xa_playable(&games[2]));
    free(games);

    /* BR metadata unavailable: en-US fallback resolves names for locked games. */
    setup(); fail_br = 1;
    CHECK(xa_parse_titles(titles, &games, &n) == 0);
    CHECK(xa_fetch_names_all(games, 3, NULL, &error) == 3);
    CHECK(names_calls == 2 && games[2].name[0] && !xa_playable(&games[2]));
    free(games);
    free(titles);
    mock_reset();
    printf("Catalog: %d checks, %d failures; full list, grants, legacy filter, name cache and outages\n", checks, failures);
    return failures ? 1 : 0;
}
