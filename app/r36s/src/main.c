#include <SDL.h>
#include <curl/curl.h>
#include <sys/utsname.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "login_flow.h"
#include "xc_port.h"
#include "xc_port_linux.h"
#include "xui.h"
#include "stream.h"
#include "catalog_view.h"

#define WIDTH 640
#define HEIGHT 480

typedef struct {
    SDL_mutex *mu;
    SDL_Thread *worker;
    int busy, result, n;
    xa_title_t *games;
    orb_catalog_view_t view;
    int searching, search_key;
    char draft[ORB_QUERY_CAP];
    char status[160], uri[128], code[32], error[160];
} app_t;
static app_t app;
static orb_stream_t *stream;
static char stream_code[24], stream_text[160];
static int bitrate_kbps = 1000;

static void show_stream_error(const char *message) {
    if (strstr(message, "NoEntitlement"))
        snprintf(app.error, sizeof(app.error), "A Microsoft não liberou este jogo para a sessão. "
                 "Teste em xbox.com/play com a mesma conta e depois tente novamente aqui.");
    else snprintf(app.error, sizeof(app.error), "%s", message);
}

static void play_selected(void) {
    int index = orb_catalog_index(&app.view);
    if (stream || app.busy || index < 0) return;
    if (app.worker) { SDL_WaitThread(app.worker, NULL); app.worker = NULL; }
    const xa_title_t *title = &app.games[index];
    orb_linux_cancel(0);
    stream = orb_stream_start(title->id, orb_catalog_label(title), !!(title->flags & XA_F_ADPLAY), bitrate_kbps);
    if (!stream) snprintf(app.error, sizeof(app.error), "Não foi possível iniciar vídeo e áudio.");
    else {
        snprintf(stream_code, sizeof(stream_code), "STARTING");
        snprintf(stream_text, sizeof(stream_text), "Iniciando %s...", orb_catalog_label(title));
    }
}

static void status_changed(const char *status) {
    SDL_LockMutex(app.mu);
    snprintf(app.status, sizeof(app.status), "%s", status ? status : "");
    SDL_UnlockMutex(app.mu);
}
static void code_changed(const char *uri, const char *code) {
    SDL_LockMutex(app.mu);
    snprintf(app.uri, sizeof(app.uri), "%s", uri ? uri : "");
    snprintf(app.code, sizeof(app.code), "%s", code ? code : "");
    SDL_UnlockMutex(app.mu);
}
static int login_worker(void *unused) {
    (void)unused;
    const lf_ui_t ui = { status_changed, code_changed };
    xa_title_t *games = NULL;
    int n = 0;
    xc_err_t e = {0};
    int rc = lf_run_catalog(&ui, &games, &n, &e, 1);
    int available = 0;
    for (int i = 0; i < n; ++i) available += xa_playable(&games[i]);
    if (rc) xc_log("[login] falhou rc=%d code=%d: %s", rc, e.code, e.msg);
    SDL_LockMutex(app.mu);
    app.games = games;
    app.n = n;
    app.result = rc;
    if (!rc && orb_catalog_filter(&app.view, games, n, app.view.query)) {
        app.result = rc = -1;
        snprintf(e.msg, sizeof(e.msg), "Não há memória para abrir a lista de jogos.");
    }
    app.busy = 0;
    app.code[0] = app.uri[0] = 0;
    if (rc) snprintf(app.error, sizeof(app.error), "%s", e.msg[0] ? e.msg : "Não foi possível entrar. Tente novamente.");
    else snprintf(app.status, sizeof(app.status), "%d jogos · %d com acesso", n, available);
    SDL_UnlockMutex(app.mu);
    return 0;
}
static int start_login(void) {
    if (app.worker) { SDL_WaitThread(app.worker, NULL); app.worker = NULL; }
    free(app.games);
    app.games = NULL;
    app.n = 0;
    app.view.count = app.view.selected = 0;
    app.searching = 0;
    SDL_StopTextInput();
    app.error[0] = app.code[0] = app.uri[0] = 0;
    app.busy = 1;
    app.result = 0;
    orb_linux_cancel(0);
    snprintf(app.status, sizeof(app.status), "Preparando login Microsoft...");
    app.worker = SDL_CreateThread(login_worker, "microsoft-login", NULL);
    if (!app.worker) {
        app.busy = 0;
        snprintf(app.error, sizeof(app.error), "Não foi possível iniciar o login.");
        return -1;
    }
    return 0;
}

static void text(xui_canvas_t *c, int f, int x, int y, uint32_t color, const char *s) {
    xui_text(c, f, x, y, color, s, 0, WIDTH - x - 24);
}

/* Ten columns, including actions reachable without a physical keyboard. */
static const char search_keys[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static const char *search_key_label(int key, char label[2]) {
    if (key < 36) { label[0] = search_keys[key]; label[1] = 0; return label; }
    return key == 36 ? "Esp." : key == 37 ? "Apag." : key == 38 ? "Limpar" : "Buscar";
}

static void search_update(void) {
    if (orb_catalog_filter(&app.view, app.games, app.n, app.draft))
        snprintf(app.error, sizeof(app.error), "Não há memória para buscar jogos.");
}

static void search_append(const char *value) {
    size_t used = strlen(app.draft), extra = strlen(value);
    if (used + extra >= sizeof(app.draft)) return;
    memcpy(app.draft + used, value, extra + 1);
    search_update();
}

static void search_erase(void) {
    size_t used = strlen(app.draft);
    if (!used) return;
    do { --used; } while (used && ((unsigned char)app.draft[used] & 0xc0) == 0x80);
    app.draft[used] = 0;
    search_update();
}

static char search_saved[ORB_QUERY_CAP];
static int search_saved_selection;
static void search_open(void) {
    snprintf(search_saved, sizeof(search_saved), "%s", app.view.query);
    snprintf(app.draft, sizeof(app.draft), "%s", app.view.query);
    search_saved_selection = app.view.selected;
    app.searching = 1;
    app.search_key = 0;
    SDL_StartTextInput();
}

static void search_close(int cancel) {
    if (cancel) {
        orb_catalog_filter(&app.view, app.games, app.n, search_saved);
        orb_catalog_move(&app.view, search_saved_selection);
    }
    app.searching = 0;
    SDL_StopTextInput();
}

static void draw_search(xui_canvas_t *c) {
    char line[160];
    text(c, XUI_F_ROW, 24, 132, XUI_C_TEXT, "Buscar jogos");
    xui_round_rect(c, 20, 146, 600, 44, 6, XUI_C_PANEL, 255);
    snprintf(line, sizeof(line), "%s_", app.draft);
    text(c, XUI_F_BODY, 31, 176, XUI_C_ACCENT, line);
    snprintf(line, sizeof(line), "%d resultado%s", app.view.count, app.view.count == 1 ? "" : "s");
    text(c, XUI_F_LABEL, 24, 215, XUI_C_DIM, line);
    if (app.view.count) text(c, XUI_F_LABEL, 180, 215, XUI_C_TEXT,
                             orb_catalog_label(&app.games[app.view.indices[0]]));
    for (int i = 0; i < 40; ++i) {
        int x = 24 + (i % 10) * 60, y = 232 + (i / 10) * 40;
        char label[2];
        xui_round_rect(c, x, y, 54, 34, 5, i == app.search_key ? XUI_C_ACCENT : XUI_C_PANEL, 255);
        xui_text_center(c, XUI_F_LABEL, x, 54, y + 24, i == app.search_key ? XUI_C_BG : XUI_C_TEXT,
                        search_key_label(i, label), 0);
    }
    text(c, XUI_F_LABEL, 24, 420, XUI_C_DIM, "Direcional: escolher · A: digitar · X: apagar · Y: limpar");
    text(c, XUI_F_BODY, 24, 455, XUI_C_DIM, "Start: ver resultados   B: cancelar");
}

static void draw(xui_canvas_t *c, int input_test, int unmapped, const char *input_line) {
    xui_fill(c, 0, 0, WIDTH, HEIGHT, XUI_C_BG);
    xui_round_rect(c, 24, 24, 8, 36, 4, XUI_C_ACCENT, 255);
    text(c, XUI_F_TITLE, 44, 60, XUI_C_TEXT, "orbita");
    text(c, XUI_F_LABEL, 418, 48, XUI_C_DIM, "R36S/H · " ORBITA_VERSION);
    text(c, XUI_F_LABEL, 24, 91, XUI_C_FLARE, "STREAMING EXPERIMENTAL · ARKOS");
    if (stream) {
        text(c, XUI_F_H2, 24, 155, XUI_C_TEXT, "Preparando o jogo");
        xui_text_wrap(c, XUI_F_BODY, 24, 194, 592, 25, 6, 0, XUI_C_DIM, stream_text);
        text(c, XUI_F_BODY, 24, 455, XUI_C_DIM, "B ou Esc: cancelar e voltar ao catálogo");
        return;
    }
    if (input_test) {
        text(c, XUI_F_H2, 24, 155, XUI_C_TEXT, "Teste dos controles");
        text(c, XUI_F_BODY, 24, 197, XUI_C_DIM, "Pressione os botões e mova os analógicos.");
        text(c, XUI_F_ROW, 24, 261, XUI_C_ACCENT, input_line);
        text(c, XUI_F_BODY, 24, 455, XUI_C_DIM,
             unmapped ? "Dois botões juntos ou Esc: sair" : "Select + Start ou Esc: sair");
        return;
    }
    SDL_LockMutex(app.mu);
    if (app.searching) {
        draw_search(c);
        SDL_UnlockMutex(app.mu);
        return;
    } else if (app.code[0]) {
        text(c, XUI_F_H2, 24, 147, XUI_C_TEXT, "Entre com sua conta Microsoft");
        text(c, XUI_F_BODY, 24, 189, XUI_C_DIM, "No celular ou computador, abra:");
        text(c, XUI_F_ROW, 24, 230, XUI_C_ACCENT, app.uri);
        text(c, XUI_F_CODE, 24, 320, XUI_C_TEXT, app.code);
        text(c, XUI_F_BODY, 24, 365, XUI_C_DIM, "Digite o código e autorize o acesso.");
    } else if (app.error[0]) {
        text(c, XUI_F_H2, 24, 159, XUI_C_TEXT, "Não foi possível continuar");
        xui_text_wrap(c, XUI_F_BODY, 24, 194, 592, 25, 4, 0, XUI_C_DIM, app.error);
        text(c, XUI_F_BODY, 24, 353, XUI_C_ACCENT,
             app.n ? "B: voltar ao catálogo   X: atualizar" : "X: tentar novamente");
    } else if (app.busy) {
        text(c, XUI_F_H2, 24, 190, XUI_C_TEXT, "Conectando à Microsoft");
        text(c, XUI_F_BODY, 24, 242, XUI_C_DIM, app.status);
    } else {
        char summary[128];
        snprintf(summary, sizeof(summary), app.view.query[0] ? "Busca: %s" : "X: buscar jogos · %s",
                 app.view.query[0] ? app.view.query : app.status);
        text(c, XUI_F_LABEL, 24, 137, XUI_C_TEXT, summary);
        int top = app.view.selected > 5 ? app.view.selected - 5 : 0;
        for (int i = top; i < app.view.count && i < top + 6; ++i) {
            const xa_title_t *game = &app.games[app.view.indices[i]];
            int y = 153 + (i - top) * 37;
            if (i == app.view.selected) xui_round_rect(c, 20, y, 600, 35, 6, XUI_C_PANEL, 255);
            text(c, XUI_F_BODY, 31, y + 25, i == app.view.selected ? XUI_C_ACCENT :
                 xa_playable(game) ? XUI_C_TEXT : XUI_C_DIM, orb_catalog_label(game));
        }
        int index = orb_catalog_index(&app.view);
        if (index >= 0) {
            char selected[128];
            snprintf(selected, sizeof(selected), "%d/%d · %s", app.view.selected + 1, app.view.count,
                     xa_playable(&app.games[index]) ? "Acesso confirmado pela conta" : "Acesso não confirmado · A: tentar iniciar");
            text(c, XUI_F_LABEL, 24, 399, XUI_C_FLARE, selected);
        } else {
            text(c, XUI_F_BODY, 24, 211, XUI_C_TEXT, "Nenhum jogo encontrado");
            text(c, XUI_F_BODY, 24, 253, XUI_C_DIM, "X: alterar a busca · B: mostrar todos");
        }
        text(c, XUI_F_LABEL, 24, 422, XUI_C_DIM, "Select: Fortnite · L1/R1: 50 jogos · Y: sair da conta");
    }
    text(c, XUI_F_LABEL, 24, 455, XUI_C_DIM, "A: jogar   X: buscar   Start: atualizar   B: voltar/sair");
    SDL_UnlockMutex(app.mu);
}

static int action(int button, int input_test) {
    if (input_test) return 0;
    if (stream) {
        if (strstr(stream_text, "Sessao ja ativa")) {
            if (button == SDL_CONTROLLER_BUTTON_A) orb_stream_answer_active(stream, 1);
            if (button == SDL_CONTROLLER_BUTTON_X) orb_stream_answer_active(stream, 0);
        }
        if (strcmp(stream_code, "STREAMING") && button == SDL_CONTROLLER_BUTTON_B) orb_stream_stop(stream);
        return 0;
    }
    SDL_LockMutex(app.mu);
    int busy = app.busy;
    if (!busy && app.searching) {
        if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) app.search_key = app.search_key / 10 * 10 + (app.search_key + 9) % 10;
        if (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) app.search_key = app.search_key / 10 * 10 + (app.search_key + 1) % 10;
        if (button == SDL_CONTROLLER_BUTTON_DPAD_UP) app.search_key = (app.search_key + 30) % 40;
        if (button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) app.search_key = (app.search_key + 10) % 40;
        if (button == SDL_CONTROLLER_BUTTON_A) {
            if (app.search_key < 36) { char value[2] = { search_keys[app.search_key], 0 }; search_append(value); }
            else if (app.search_key == 36) search_append(" ");
            else if (app.search_key == 37) search_erase();
            else if (app.search_key == 38) { app.draft[0] = 0; search_update(); }
            else search_close(0);
        }
        if (button == SDL_CONTROLLER_BUTTON_X) search_erase();
        if (button == SDL_CONTROLLER_BUTTON_Y) { app.draft[0] = 0; search_update(); }
        if (button == SDL_CONTROLLER_BUTTON_START) search_close(0);
        if (button == SDL_CONTROLLER_BUTTON_B || button == SDL_CONTROLLER_BUTTON_BACK) search_close(1);
        SDL_UnlockMutex(app.mu);
        return 0;
    }
    if (!busy && app.n && app.error[0] && button == SDL_CONTROLLER_BUTTON_B) {
        app.error[0] = 0;
        SDL_UnlockMutex(app.mu);
        return 0;
    }
    if (!busy && !app.error[0]) {
        int step = button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : button == SDL_CONTROLLER_BUTTON_DPAD_DOWN ? 1 :
                   button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? -6 : button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT ? 6 :
                   button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ? -50 : button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER ? 50 : 0;
        orb_catalog_move(&app.view, step);
        if (app.n && button == SDL_CONTROLLER_BUTTON_X) search_open();
        if (app.n && button == SDL_CONTROLLER_BUTTON_BACK) {
            orb_catalog_filter(&app.view, app.games, app.n, "Fortnite");
            SDL_UnlockMutex(app.mu);
            return 0;
        }
        if (button == SDL_CONTROLLER_BUTTON_B && app.view.query[0]) {
            orb_catalog_filter(&app.view, app.games, app.n, "");
            SDL_UnlockMutex(app.mu);
            return 0;
        }
    }
    SDL_UnlockMutex(app.mu);
    if (button == SDL_CONTROLLER_BUTTON_B || button == SDL_CONTROLLER_BUTTON_BACK) return 1;
    if (!busy && button == SDL_CONTROLLER_BUTTON_A) play_selected();
    if (!busy && (button == SDL_CONTROLLER_BUTTON_START || (button == SDL_CONTROLLER_BUTTON_X && app.error[0]))) start_login();
    if (!busy && button == SDL_CONTROLLER_BUTTON_Y) {
        if (xc_store_remove(LF_TOKEN_FILE) != 0) {
            SDL_LockMutex(app.mu);
            snprintf(app.error, sizeof(app.error), "Não foi possível remover o login salvo.");
            SDL_UnlockMutex(app.mu);
        } else start_login();
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *data_dir = getenv("ORBITA_DATA_DIR");
    const char *stream_test_dir = NULL;
    const char *play_name = NULL;
    int test_seconds = 12;
    int smoke = 0, input_test = 0, windowed = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--smoke-test")) smoke = 1;
        else if (!strcmp(argv[i], "--input-test")) input_test = 1;
        else if (!strcmp(argv[i], "--windowed")) windowed = 1;
        else if (!strcmp(argv[i], "--stream-test-dir") && i + 1 < argc) stream_test_dir = argv[++i];
        else if (!strcmp(argv[i], "--play-name") && i + 1 < argc) play_name = argv[++i];
        else if ((!strcmp(argv[i], "--bitrate-kbps") || !strcmp(argv[i], "--test-seconds")) && i + 1 < argc) {
            int bitrate = !strcmp(argv[i], "--bitrate-kbps");
            char *end;
            long value = strtol(argv[++i], &end, 10);
            if (*end || value < (bitrate ? 500 : 1) || value > (bitrate ? 8000 : 120)) return 2;
            if (bitrate) bitrate_kbps = (int)value;
            else test_seconds = (int)value;
        }
        else if (!strcmp(argv[i], "--data-dir") && i + 1 < argc) data_dir = argv[++i];
        else if (!strcmp(argv[i], "--diagnose")) {
            struct utsname u;
            SDL_version s;
            SDL_GetVersion(&s);
            if (uname(&u)) return 1;
            printf("orbita=%s\narch=%s\nkernel=%s\nsdl=%u.%u.%u\n%s\nbeta_gate=disabled\nstreaming=experimental-webrtc\n",
                   ORBITA_VERSION, u.machine, u.release, s.major, s.minor, s.patch, curl_version());
            printf("sdl_build=%u.%u.%u\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
            for (int d = 0; d < SDL_GetNumVideoDrivers(); ++d)
                printf("video_driver_%d=%s\n", d, SDL_GetVideoDriver(d));
            if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) == 0) {
                int count = SDL_NumJoysticks();
                printf("joysticks=%d\n", count);
                for (int j = 0; j < count; ++j) {
                    SDL_Joystick *joy = SDL_JoystickOpen(j);
                    if (!joy) continue;
                    char guid[64];
                    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joy), guid, sizeof(guid));
                    printf("joystick_%d=%s\nguid_%d=%s\naxes_%d=%d\nbuttons_%d=%d\nhats_%d=%d\ncontroller_%d=%d\n",
                           j, SDL_JoystickName(joy), j, guid, j, SDL_JoystickNumAxes(joy),
                           j, SDL_JoystickNumButtons(joy), j, SDL_JoystickNumHats(joy), j, SDL_IsGameController(j));
                    if (SDL_IsGameController(j)) {
                        SDL_GameController *pad = SDL_GameControllerOpen(j);
                        if (pad) {
                            char *mapping = SDL_GameControllerMapping(pad);
                            if (mapping) { printf("mapping_%d=%s\n", j, mapping); SDL_free(mapping); }
                            SDL_GameControllerClose(pad);
                        }
                    }
                    SDL_JoystickClose(joy);
                }
                SDL_Quit();
            } else fprintf(stderr, "input_probe_error=%s\n", SDL_GetError());
            return 0;
        } else {
            fprintf(stderr, "Uso: orbita [--data-dir PASTA] [--windowed] [--input-test] [--diagnose] [--smoke-test] [--bitrate-kbps 500..8000] [--stream-test-dir PASTA --test-seconds 1..120]\n");
            return !strcmp(argv[i], "--help") ? 0 : 2;
        }
    }
    if ((stream_test_dir && (input_test || smoke)) || (play_name && (input_test || smoke || stream_test_dir))) return 2;
    /* Smoke test is fully offline and does not touch the user's saved login. */
    fprintf(stderr, "app_start version=%s mode=%s\n", ORBITA_VERSION,
            smoke ? "smoke-test" : input_test ? "input-test" : stream_test_dir ? "local-peer" : "microsoft-login");
    if (!smoke && !stream_test_dir && orb_linux_init(data_dir)) { fprintf(stderr, "Não foi possível abrir a pasta de dados.\n"); return 1; }
    fprintf(stderr, "stage=sdl_init\n");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS)) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError()); orb_linux_shutdown(); return 1;
    }
    fprintf(stderr, "stage=window_create video_driver=%s\n", SDL_GetCurrentVideoDriver());
    app.mu = SDL_CreateMutex();
    SDL_Window *win = SDL_CreateWindow("orbita R36S/H", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                      WIDTH, HEIGHT, windowed || smoke ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer *renderer = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (!renderer && win) renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    SDL_Texture *texture = renderer ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888,
                                                         SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT) : NULL;
    uint32_t *pixels = calloc(WIDTH * HEIGHT, sizeof(*pixels));
    if (!app.mu || !win || !renderer || !texture || !pixels) {
        fprintf(stderr, "Tela: %s\n", SDL_GetError());
        free(pixels); SDL_DestroyTexture(texture); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(win);
        if (app.mu) SDL_DestroyMutex(app.mu);
        SDL_Quit(); orb_linux_shutdown(); return 1;
    }
    SDL_RendererInfo renderer_info;
    memset(&renderer_info, 0, sizeof(renderer_info));
    SDL_GetRendererInfo(renderer, &renderer_info);
    fprintf(stderr, "display_ready renderer=%s size=%dx%d\n",
            renderer_info.name ? renderer_info.name : "unknown", WIDTH, HEIGHT);
    SDL_RenderSetLogicalSize(renderer, WIDTH, HEIGHT);
    SDL_ShowCursor(SDL_DISABLE);
    SDL_GameController *controller = NULL;
    SDL_Joystick *joystick = NULL;
    for (int i = 0; i < SDL_NumJoysticks() && !controller && !joystick; ++i) {
        if (SDL_IsGameController(i)) controller = SDL_GameControllerOpen(i);
        else joystick = SDL_JoystickOpen(i);
    }
    fprintf(stderr, "controller=%s mapped=%s\n",
            controller ? SDL_GameControllerName(controller) : joystick ? SDL_JoystickName(joystick) : "none",
            controller ? "yes" : "no");
    xui_canvas_t canvas;
    xui_canvas_init(&canvas, pixels, WIDTH, HEIGHT, WIDTH);
    int unmapped = joystick && !controller;
    if (unmapped && !input_test) {
        snprintf(app.error, sizeof(app.error), "Controle sem mapeamento SDL. Inicie pelo launcher do PortMaster "
                 "ou use --input-test para identificar os botões. Pressione qualquer botão para sair.");
    } else if (stream_test_dir) {
        stream = orb_stream_test(stream_test_dir, bitrate_kbps);
        if (!stream) { fprintf(stderr, "Falha ao iniciar o teste de streaming.\n"); SDL_Quit(); return 1; }
    } else if (!smoke && !input_test) start_login();
    char input_line[128] = "Aguardando entrada...";
    int quit = 0, frames = 0, injected = 0, render_failed = 0, video_presented = 0;
    int leaving = 0, video_rendered = 0, video_readback = 0;
    uint64_t started = xcn_now_ms(), exit_combo_since = 0;
    SDL_Texture *video_texture = NULL;
    int video_width = 0, video_height = 0;
    while (!quit) {
        if (play_name && !stream) {
            int ready, found = 0;
            SDL_LockMutex(app.mu);
            ready = !app.busy && !app.result;
            if (ready) {
                for (int i = 0; i < app.n; ++i) if (!strcmp(app.games[i].name, play_name)) {
                    app.view.selected = i; found = 1; break;
                }
            }
            SDL_UnlockMutex(app.mu);
            if (ready) {
                if (found) play_selected();
                else snprintf(app.error, sizeof(app.error), "Jogo não encontrado no catálogo desta conta.");
                play_name = NULL; /* exactly one attempt, even after returning to the catalog */
            }
        }
        if (stream) orb_stream_status(stream, stream_code, sizeof(stream_code), stream_text, sizeof(stream_text));
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { if (stream) { leaving = 1; orb_stream_stop(stream); } else quit = 1; }
            else if (ev.type == SDL_KEYDOWN) {
                int key = ev.key.keysym.sym;
                if (key == SDLK_ESCAPE) { if (stream) orb_stream_stop(stream); else if (input_test) quit = 1; else quit |= action(SDL_CONTROLLER_BUTTON_B, input_test); }
                else if (key == SDLK_BACKSPACE && app.searching) search_erase();
                else if (app.searching && key != SDLK_RETURN && key != SDLK_UP && key != SDLK_DOWN && key != SDLK_LEFT && key != SDLK_RIGHT) { /* SDL_TEXTINPUT handles typed characters. */ }
                else if (key == SDLK_RETURN) quit |= action(app.searching ? SDL_CONTROLLER_BUTTON_START : SDL_CONTROLLER_BUTTON_A, input_test);
                else if (key == SDLK_UP) quit |= action(SDL_CONTROLLER_BUTTON_DPAD_UP, input_test);
                else if (key == SDLK_DOWN) quit |= action(SDL_CONTROLLER_BUTTON_DPAD_DOWN, input_test);
                else if (key == SDLK_LEFT) quit |= action(SDL_CONTROLLER_BUTTON_DPAD_LEFT, input_test);
                else if (key == SDLK_RIGHT) quit |= action(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, input_test);
                else if (key == SDLK_PAGEUP) quit |= action(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, input_test);
                else if (key == SDLK_PAGEDOWN) quit |= action(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, input_test);
                else if (key == SDLK_x) quit |= action(SDL_CONTROLLER_BUTTON_X, input_test);
                else if (key == SDLK_y) quit |= action(SDL_CONTROLLER_BUTTON_Y, input_test);
                else if (key == SDLK_b) quit |= action(SDL_CONTROLLER_BUTTON_B, input_test);
                else if (key == SDLK_F5) quit |= action(SDL_CONTROLLER_BUTTON_START, input_test);
                else if (key == SDLK_f) quit |= action(SDL_CONTROLLER_BUTTON_BACK, input_test);
            } else if (ev.type == SDL_TEXTINPUT && app.searching && !stream) {
                search_append(ev.text.text);
            } else if (ev.type == SDL_CONTROLLERBUTTONDOWN) {
                snprintf(input_line, sizeof(input_line), "Botão SDL: %u", ev.cbutton.button);
                if (smoke) { injected = 1; quit = 1; }
                else quit |= action(ev.cbutton.button, input_test);
            } else if (ev.type == SDL_JOYBUTTONDOWN && !controller) {
                snprintf(input_line, sizeof(input_line), "Botão bruto: %u", ev.jbutton.button);
                if (unmapped && !input_test) quit = 1;
            } else if (ev.type == SDL_JOYAXISMOTION) {
                snprintf(input_line, sizeof(input_line), "Analógico %u: %d", ev.jaxis.axis, ev.jaxis.value);
            } else if (ev.type == SDL_JOYHATMOTION && !controller && ev.jhat.value != SDL_HAT_CENTERED) {
                if (ev.jhat.value & SDL_HAT_UP) quit |= action(SDL_CONTROLLER_BUTTON_DPAD_UP, input_test);
                if (ev.jhat.value & SDL_HAT_DOWN) quit |= action(SDL_CONTROLLER_BUTTON_DPAD_DOWN, input_test);
            }
        }
        if (stream) {
            xcp_gamepad_t pad = {0};
            if (controller) {
#define PAD_BUTTON(field, name) pad.field = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_##name)
                PAD_BUTTON(a, A); PAD_BUTTON(b, B); PAD_BUTTON(x, X); PAD_BUTTON(y, Y);
                PAD_BUTTON(menu, START); PAD_BUTTON(view, BACK); PAD_BUTTON(nexus, GUIDE);
                PAD_BUTTON(dpad_up, DPAD_UP); PAD_BUTTON(dpad_down, DPAD_DOWN);
                PAD_BUTTON(dpad_left, DPAD_LEFT); PAD_BUTTON(dpad_right, DPAD_RIGHT);
                PAD_BUTTON(left_shoulder, LEFTSHOULDER); PAD_BUTTON(right_shoulder, RIGHTSHOULDER);
                PAD_BUTTON(left_thumb, LEFTSTICK); PAD_BUTTON(right_thumb, RIGHTSTICK);
#undef PAD_BUTTON
                pad.left_x = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX) / 32768.0f;
                pad.left_y = -SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY) / 32768.0f;
                pad.right_x = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX) / 32768.0f;
                pad.right_y = -SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY) / 32768.0f;
                pad.left_trigger = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 32767.0f;
                pad.right_trigger = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.0f;
                if (pad.left_trigger < 0) pad.left_trigger = 0;
                if (pad.right_trigger < 0) pad.right_trigger = 0;
            } else if (stream_test_dir) {
                unsigned t = (unsigned)(xcn_now_ms() - started);
                pad.a = (t / 200) % 2;
                pad.left_x = (t / 400) % 2 ? 0.75f : -0.75f;
            } else {
                const Uint8 *keys = SDL_GetKeyboardState(NULL);
                pad.a = keys[SDL_SCANCODE_RETURN]; pad.b = keys[SDL_SCANCODE_B];
                pad.x = keys[SDL_SCANCODE_X]; pad.y = keys[SDL_SCANCODE_Y];
                pad.dpad_up = keys[SDL_SCANCODE_UP]; pad.dpad_down = keys[SDL_SCANCODE_DOWN];
                pad.dpad_left = keys[SDL_SCANCODE_LEFT]; pad.dpad_right = keys[SDL_SCANCODE_RIGHT];
            }
            if (pad.menu && pad.view) {
                memset(&pad, 0, sizeof(pad));
                if (!exit_combo_since) exit_combo_since = xcn_now_ms();
                if (xcn_now_ms() - exit_combo_since >= 700) orb_stream_stop(stream);
            } else exit_combo_since = 0;
            orb_stream_pad(stream, &pad);
            if (stream_test_dir && xcn_now_ms() - started >= (uint64_t)test_seconds * 1000) {
                leaving = 1;
                orb_stream_stop(stream);
            }
            AVFrame *frame = orb_stream_frame(stream);
            if (frame) {
                if (frame->width != video_width || frame->height != video_height) {
                    SDL_DestroyTexture(video_texture);
                    video_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
                                                      frame->width, frame->height);
                    video_width = frame->width; video_height = frame->height;
                }
                if (!video_texture || (frame->format != AV_PIX_FMT_YUV420P && frame->format != AV_PIX_FMT_YUVJ420P) ||
                    SDL_UpdateYUVTexture(video_texture, NULL, frame->data[0], frame->linesize[0],
                        frame->data[1], frame->linesize[1], frame->data[2], frame->linesize[2])) {
                    fprintf(stderr, "Falha ao preparar o frame de vídeo: %s\n", SDL_GetError());
                    render_failed = leaving = 1;
                    orb_stream_stop(stream);
                } else {
                    if (!video_presented) fprintf(stderr, "[video] primeiro frame pronto para apresentação: %dx%d\n", video_width, video_height);
                    ++video_presented;
                }
                av_frame_free(&frame);
            }
            if (orb_stream_done(stream)) {
                orb_stream_status(stream, stream_code, sizeof(stream_code), stream_text, sizeof(stream_text));
                if (!strcmp(stream_code, "ERROR")) show_stream_error(stream_text);
                orb_stream_free(stream); stream = NULL;
                SDL_DestroyTexture(video_texture); video_texture = NULL;
                video_width = video_height = 0;
                if (stream_test_dir && !video_presented) render_failed = 1;
                if (leaving || stream_test_dir) quit = 1;
                video_presented = 0;
            }
        }
        if (input_test && controller && SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK) &&
            SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START)) quit = 1;
        if (input_test && unmapped) {
            int pressed = 0;
            for (int i = 0; i < SDL_JoystickNumButtons(joystick); ++i) pressed += !!SDL_JoystickGetButton(joystick, i);
            if (pressed >= 2) quit = 1;
        }
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        if (SDL_RenderClear(renderer)) { render_failed = 1; break; }
        if (stream && video_texture && video_presented && !strcmp(stream_code, "STREAMING")) {
            SDL_Rect dst = {0, 0, WIDTH, HEIGHT};
            if ((int64_t)video_width * HEIGHT > (int64_t)video_height * WIDTH) {
                dst.h = video_height * WIDTH / video_width; dst.y = (HEIGHT - dst.h) / 2;
            } else { dst.w = video_width * HEIGHT / video_height; dst.x = (WIDTH - dst.w) / 2; }
            if (SDL_RenderCopy(renderer, video_texture, NULL, &dst)) { render_failed = 1; break; }
        } else {
            draw(&canvas, input_test, unmapped, input_line);
            if (SDL_UpdateTexture(texture, NULL, pixels, WIDTH * (int)sizeof(*pixels)) ||
                SDL_RenderCopy(renderer, texture, NULL, NULL)) { render_failed = 1; break; }
        }
        if (smoke) {
            SDL_Rect probe = { 28, 40, 1, 1 };
            uint32_t color = 0;
            if (SDL_RenderReadPixels(renderer, &probe, SDL_PIXELFORMAT_RGB888, &color, sizeof(color)) ||
                (color & 0x00ffffff) != XUI_C_ACCENT) { render_failed = 1; break; }
        }
        if (stream && video_texture && video_presented && !strcmp(stream_code, "STREAMING")) {
            if (++video_rendered == 1) fprintf(stderr, "[video] primeiro frame apresentado\n");
            if (stream_test_dir && !video_readback) {
                /* Fullscreen output can be larger than the logical 640x480
                 * canvas (e.g. the dummy driver's 1024x768 desktop). */
                SDL_Rect probe = {0, 0, WIDTH, HEIGHT};
                if (SDL_RenderReadPixels(renderer, &probe, SDL_PIXELFORMAT_RGB888, pixels, WIDTH * (int)sizeof(*pixels))) {
                    render_failed = 1; break;
                }
                for (int i = 0; i < WIDTH * HEIGHT; ++i) if (pixels[i] & 0x00ffffff) { video_readback = 1; break; }
            }
        }
        SDL_RenderPresent(renderer);
        ++frames;
        if (frames == 1) fprintf(stderr, "first_frame_presented=yes\n");
        if (smoke && frames == 2) {
            SDL_Event button = {0};
            button.type = SDL_CONTROLLERBUTTONDOWN;
            button.cbutton.button = SDL_CONTROLLER_BUTTON_B;
            SDL_PushEvent(&button);
        }
        if (smoke && frames > 5) quit = 1;
        SDL_Delay(16);
    }
    if (render_failed) fprintf(stderr, "Falha ao apresentar a tela: %s\n", SDL_GetError());
    if (stream) { orb_stream_stop(stream); orb_stream_free(stream); stream = NULL; }
    fprintf(stderr, "stage=shutdown frames=%d render_failed=%d\n", frames, render_failed);
    orb_linux_cancel(1);
    if (app.worker) SDL_WaitThread(app.worker, NULL);
    orb_catalog_destroy(&app.view);
    free(app.games); free(pixels);
    if (controller) SDL_GameControllerClose(controller);
    if (joystick) SDL_JoystickClose(joystick);
    fprintf(stderr, "stage=display_cleanup\n");
    SDL_DestroyTexture(video_texture); SDL_DestroyTexture(texture); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(win);
    fprintf(stderr, "stage=sdl_cleanup\n");
    SDL_DestroyMutex(app.mu); SDL_Quit(); orb_linux_shutdown();
    fprintf(stderr, "stage=exit\n");
    if (smoke) printf("SDL smoke: %d frames, controller_event=%d, network=disabled\n", frames, injected);
    if (stream_test_dir) {
        fprintf(stderr, "stream_test rendered=%d video_readback=%d\n", video_rendered, video_readback);
        if (!video_rendered || !video_readback) render_failed = 1;
    }
    return render_failed || (smoke && !injected) ? 1 : 0;
}
