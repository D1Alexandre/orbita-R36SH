/* Exercise the real menu handlers and renderer offline, including filtered
 * selection. No Microsoft login, token file, or streaming session is opened. */
#define main orbita_program_main
#include "../src/main.c"
#undef main

#include <limits.h>

static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); ++failures; } } while (0)

static void press(int button) { CHECK(action(button, 0) == 0); }

static void type_on_pad(const char *value) {
    while (*value) {
        const char *found = strchr(search_keys, *value++);
        CHECK(found != NULL);
        if (!found) return;
        int key = (int)(found - search_keys);
        while (app.search_key / 10 != key / 10) press(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        while (app.search_key % 10 != key % 10) press(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
        press(SDL_CONTROLLER_BUTTON_A);
    }
}

static void capture(xui_canvas_t *canvas, const char *directory, const char *name) {
    draw(canvas, 0, 0, "");
    if (!directory) return;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.bmp", directory, name);
    SDL_Surface *surface = SDL_CreateRGBSurfaceFrom(canvas->px, WIDTH, HEIGHT, 32, WIDTH * 4,
                                                    0xff0000, 0xff00, 0xff, 0);
    CHECK(surface != NULL);
    if (surface) { CHECK(SDL_SaveBMP(surface, path) == 0); SDL_FreeSurface(surface); }
}

int main(int argc, char **argv) {
    CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) == 0);
    app.mu = SDL_CreateMutex();
    CHECK(app.mu != NULL);
    app.n = 2813;
    app.games = calloc((size_t)app.n, sizeof(*app.games));
    CHECK(app.games != NULL);
    if (!app.mu || !app.games) return 1;
    for (int i = 0; i < app.n; ++i) {
        snprintf(app.games[i].id, sizeof(app.games[i].id), "TITLE%04d", i);
        snprintf(app.games[i].name, sizeof(app.games[i].name), "Game %04d", i);
        app.games[i].flags = i % 2 ? XA_F_SUB : XA_F_OWNED;
    }
    const int fortnite = 1733;
    app.games[fortnite].name[0] = 0;
    snprintf(app.games[fortnite].product, sizeof(app.games[fortnite].product), "BT5P2X999VH2");
    snprintf(app.games[51].name, sizeof(app.games[51].name), "Ação: Corrida Épica");
    snprintf(app.games[52].name, sizeof(app.games[52].name), "HOT WHEELS - Unleashed");
    xa_title_t *before = malloc((size_t)app.n * sizeof(*before));
    CHECK(before != NULL);
    if (!before) return 1;
    memcpy(before, app.games, (size_t)app.n * sizeof(*before));
    CHECK(orb_catalog_filter(&app.view, app.games, app.n, "") == 0);
    CHECK(app.view.count == app.n);
    orb_catalog_move(&app.view, INT_MAX);
    CHECK(app.view.selected == app.n - 1);
    orb_catalog_move(&app.view, INT_MIN);
    CHECK(app.view.selected == 0);
    CHECK(orb_catalog_filter(&app.view, app.games, app.n, "aCAo corrida epica") == 0);
    CHECK(app.view.count == 1 && orb_catalog_index(&app.view) == 51);
    CHECK(orb_catalog_filter(&app.view, app.games, app.n, "hotwheels") == 0);
    CHECK(app.view.count == 1 && orb_catalog_index(&app.view) == 52);
    CHECK(orb_catalog_filter(&app.view, app.games, app.n, "title1733") == 0);
    CHECK(app.view.count == 1 && orb_catalog_index(&app.view) == fortnite);
    CHECK(orb_catalog_filter(&app.view, app.games, app.n, "bt5p2x999vh2") == 0);
    CHECK(app.view.count == 1 && orb_catalog_index(&app.view) == fortnite);
    CHECK(!strcmp(orb_catalog_label(&app.games[fortnite]), "Fortnite"));
    CHECK(orb_catalog_filter(&app.view, app.games, app.n, "") == 0);
    press(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    CHECK(app.view.selected == 50);
    press(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    CHECK(app.view.selected == 56);
    press(SDL_CONTROLLER_BUTTON_X);
    CHECK(app.searching && SDL_IsTextInputActive());
    press(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    CHECK(app.search_key == 9);
    press(SDL_CONTROLLER_BUTTON_DPAD_UP);
    CHECK(app.search_key == 39);
    type_on_pad("FORTNITE");
    CHECK(!strcmp(app.draft, "FORTNITE"));
    CHECK(app.view.count == 1 && orb_catalog_index(&app.view) == fortnite);
    uint32_t *pixels = calloc(WIDTH * HEIGHT, sizeof(*pixels));
    CHECK(pixels != NULL);
    xui_canvas_t canvas;
    if (!pixels) return 1;
    xui_canvas_init(&canvas, pixels, WIDTH, HEIGHT, WIDTH);
    const char *directory = argc > 1 ? argv[1] : NULL;
    capture(&canvas, directory, "search-fortnite");
    press(SDL_CONTROLLER_BUTTON_START);
    CHECK(!app.searching && !SDL_IsTextInputActive());
    CHECK(app.view.count == 1 && orb_catalog_index(&app.view) == fortnite);
    capture(&canvas, directory, "fortnite-result");
    press(SDL_CONTROLLER_BUTTON_X);
    press(SDL_CONTROLLER_BUTTON_Y); /* Clears search, never the user's login. */
    CHECK(!app.draft[0] && app.view.count == app.n && app.worker == NULL);
    type_on_pad("ZZZZ");
    CHECK(app.view.count == 0 && orb_catalog_index(&app.view) == -1);
    capture(&canvas, directory, "search-empty");
    press(SDL_CONTROLLER_BUTTON_START);
    capture(&canvas, directory, "no-results");
    press(SDL_CONTROLLER_BUTTON_A); /* No selection must not start a session. */
    CHECK(!stream);
    press(SDL_CONTROLLER_BUTTON_B);
    CHECK(app.view.count == app.n && !app.view.query[0]);
    press(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    press(SDL_CONTROLLER_BUTTON_X);
    type_on_pad("FOR");
    press(SDL_CONTROLLER_BUTTON_B); /* Cancel restores query and selection. */
    CHECK(!app.searching && app.view.count == app.n && app.view.selected == 50);
    press(SDL_CONTROLLER_BUTTON_BACK); /* Fortnite shortcut never quits. */
    CHECK(app.view.count == 1 && orb_catalog_index(&app.view) == fortnite);
    press(SDL_CONTROLLER_BUTTON_X);
    press(SDL_CONTROLLER_BUTTON_Y);
    search_append("ação");
    CHECK(app.view.count == 1 && orb_catalog_index(&app.view) == 51);
    search_erase();
    CHECK(!strcmp(app.draft, "açã"));
    search_erase();
    CHECK(!strcmp(app.draft, "aç"));
    press(SDL_CONTROLLER_BUTTON_Y);
    char long_query[ORB_QUERY_CAP];
    memset(long_query, 'A', sizeof(long_query) - 1);
    long_query[sizeof(long_query) - 1] = 0;
    search_append(long_query);
    search_append("B");
    CHECK(strlen(app.draft) == ORB_QUERY_CAP - 1);
    press(SDL_CONTROLLER_BUTTON_B);
    CHECK(!strcmp(app.view.query, "Fortnite"));
    press(SDL_CONTROLLER_BUTTON_B);
    CHECK(action(SDL_CONTROLLER_BUTTON_B, 0) == 1); /* B exits only in full list. */
    CHECK(memcmp(before, app.games, (size_t)app.n * sizeof(*before)) == 0);
    show_stream_error("HTTP 400 em play (NoEntitlement)");
    CHECK(strstr(app.error, "xbox.com/play") != NULL);
    CHECK(strstr(app.error, "HTTP 400") == NULL);
    show_stream_error("Conexão interrompida");
    CHECK(!strcmp(app.error, "Conexão interrompida"));
    CHECK(xui_text_width(XUI_F_LABEL, "A: jogar   X: buscar   Start: atualizar   B: voltar/sair", 0) <= 592);
    CHECK(xui_text_width(XUI_F_LABEL, "Direcional: escolher · A: digitar · X: apagar · Y: limpar", 0) <= 592);
    for (int i = 0; i < 40; ++i) {
        char label[2];
        CHECK(xui_text_width(XUI_F_LABEL, search_key_label(i, label), 0) <= 54);
    }
    CHECK(orb_catalog_filter(&app.view, NULL, 0, "Fortnite") == 0);
    CHECK(app.view.count == 0 && orb_catalog_index(&app.view) == -1);
    orb_catalog_move(&app.view, 50);
    CHECK(app.view.selected == 0);
    CHECK(orb_catalog_filter(&app.view, NULL, 1, "") == -1);
    orb_catalog_destroy(&app.view);
    free(before); free(app.games); free(pixels);
    SDL_DestroyMutex(app.mu); SDL_Quit();
    printf("Menu: %d checks, %d failures; search, accents, controller keyboard, cancel, selection and grants\n", checks, failures);
    return failures ? 1 : 0;
}
