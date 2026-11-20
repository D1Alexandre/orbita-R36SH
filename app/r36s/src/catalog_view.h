#ifndef ORBITA_CATALOG_VIEW_H
#define ORBITA_CATALOG_VIEW_H

#include "xbox_auth.h"

#define ORB_QUERY_CAP 64

typedef struct {
    int *indices;
    int count, capacity, selected;
    char query[ORB_QUERY_CAP];
} orb_catalog_view_t;

const char *orb_catalog_label(const xa_title_t *game);
int orb_catalog_filter(orb_catalog_view_t *view, const xa_title_t *games, int n, const char *query);
int orb_catalog_index(const orb_catalog_view_t *view);
void orb_catalog_move(orb_catalog_view_t *view, int step);
void orb_catalog_destroy(orb_catalog_view_t *view);

#endif
