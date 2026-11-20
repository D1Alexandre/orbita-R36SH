#include "catalog_view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

const char *orb_catalog_label(const xa_title_t *game) {
    if (game->name[0]) return game->name;
    /* A label fallback only: the title ID and account grants still come from
     * Microsoft's catalog. Never invent a title or an entitlement. */
    if (!strcasecmp(game->product, "BT5P2X999VH2")) return "Fortnite";
    return game->id;
}

static void search_key(char *out, size_t cap, const char *text) {
    char folded[256];
    xa_ascii_fold(folded, sizeof(folded), text);
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)folded; *p && used + 1 < cap; ++p) {
        unsigned c = *p;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[used++] = (char)c;
    }
    out[used] = 0;
}

int orb_catalog_filter(orb_catalog_view_t *view, const xa_title_t *games, int n, const char *query) {
    if (n < 0 || (n && !games)) return -1;
    if (n > view->capacity) {
        int *indices = realloc(view->indices, (size_t)n * sizeof(*indices));
        if (!indices) return -1;
        view->indices = indices;
        view->capacity = n;
    }
    char saved[ORB_QUERY_CAP] = {0}, key[256];
    snprintf(saved, sizeof(saved), "%s", query ? query : "");
    search_key(key, sizeof(key), saved);
    view->count = view->selected = 0;
    for (int i = 0; i < n; ++i) {
        char name[256], id[256], product[256];
        search_key(name, sizeof(name), orb_catalog_label(&games[i]));
        search_key(id, sizeof(id), games[i].id);
        search_key(product, sizeof(product), games[i].product);
        if (!key[0] || strstr(name, key) || strstr(id, key) || strstr(product, key))
            view->indices[view->count++] = i;
    }
    memcpy(view->query, saved, sizeof(saved));
    return 0;
}

int orb_catalog_index(const orb_catalog_view_t *view) {
    return view->selected >= 0 && view->selected < view->count ? view->indices[view->selected] : -1;
}

void orb_catalog_move(orb_catalog_view_t *view, int step) {
    if (!view->count) { view->selected = 0; return; }
    if (step < -view->selected) view->selected = 0;
    else if (step >= view->count - view->selected) view->selected = view->count - 1;
    else view->selected += step;
}

void orb_catalog_destroy(orb_catalog_view_t *view) {
    free(view->indices);
    memset(view, 0, sizeof(*view));
}
