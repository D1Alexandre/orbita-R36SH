/* Linux session, WebRTC and bounded media queues. SDL rendering stays in main. */
#include "stream.h"
#include "xc_webrtc_xsm.h"
#include "xcw_port.h"
#include "xc_port.h"
#include <libavcodec/avcodec.h>
#include <opus/opus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIDEO_QUEUE 8
#define AUDIO_RATE 48000
#define AUDIO_CHANNELS 2
#define AUDIO_FRAME 960

typedef struct { uint8_t *data; int len, reset; unsigned generation; } au_t;
struct orb_stream {
    xsm_t machine;
    xcws_t rtc;
    SDL_mutex *mu;
    SDL_cond *video_ready;
    SDL_Thread *session_thread, *network_thread, *decoder_thread;
    SDL_atomic_t stop, session_done, network_done, decoder_done, keyframe;
    SDL_atomic_t decoded, audio_packets, audio_errors, queue_drops, failed;
    xcp_gamepad_t pad;
    AVCodecContext *decoder;
    AVFrame *latest;
    OpusDecoder *opus;
    SDL_AudioDeviceID audio;
    au_t queue[VIDEO_QUEUE];
    int qread, qcount, gate, reset, audio_seq_valid;
    uint16_t audio_seq;
    int kbps, test;
    unsigned generation;
    uint64_t last_video_ms;
    char directory[1024], code[24], text[160];
};

static void clear_queue(orb_stream_t *s) {
    while (s->qcount) {
        free(s->queue[s->qread].data);
        memset(&s->queue[s->qread], 0, sizeof(au_t));
        s->qread = (s->qread + 1) % VIDEO_QUEUE;
        --s->qcount;
    }
}

static void on_video(void *user, const uint8_t *data, size_t len, const xcw_au_info_t *info) {
    orb_stream_t *s = user;
    if (!len || len > 2 * 1024 * 1024 || SDL_AtomicGet(&s->stop)) return;
    s->last_video_ms = xcw_now_ms(); /* callback and watchdog share the network thread */
    SDL_LockMutex(s->mu);
    if (s->qcount == VIDEO_QUEUE) {
        clear_queue(s);
        ++s->generation;
        s->gate = s->reset = 1;
        SDL_AtomicAdd(&s->queue_drops, 1);
        SDL_AtomicSet(&s->keyframe, 1);
    }
    if (s->gate && !info->idr) { SDL_UnlockMutex(s->mu); return; }
    au_t *au = &s->queue[(s->qread + s->qcount) % VIDEO_QUEUE];
    au->data = calloc(1, len + AV_INPUT_BUFFER_PADDING_SIZE);
    if (au->data) {
        memcpy(au->data, data, len);
        au->len = (int)len;
        au->reset = s->reset;
        au->generation = s->generation;
        s->gate = s->reset = 0;
        ++s->qcount;
        SDL_CondSignal(s->video_ready);
    } else SDL_AtomicSet(&s->keyframe, 1);
    SDL_UnlockMutex(s->mu);
}

static void queue_audio(orb_stream_t *s, const int16_t *pcm, int frames) {
    if (!s->audio || frames <= 0 || SDL_AtomicGet(&s->stop)) return;
    /* Cap sound latency at ~120 ms instead of accumulating stale audio. */
    if (SDL_GetQueuedAudioSize(s->audio) > AUDIO_FRAME * AUDIO_CHANNELS * sizeof(int16_t) * 6)
        SDL_ClearQueuedAudio(s->audio);
    SDL_QueueAudio(s->audio, pcm, (Uint32)(frames * AUDIO_CHANNELS * sizeof(int16_t)));
}
static void on_audio(void *user, uint16_t seq, uint32_t ts, const uint8_t *data, size_t len) {
    (void)ts;
    orb_stream_t *s = user;
    if (!s->opus || !len || len > 4096 || SDL_AtomicGet(&s->stop)) return;
    int16_t pcm[5760 * AUDIO_CHANNELS];
    if (s->audio_seq_valid) {
        int delta = (int16_t)(seq - s->audio_seq);
        if (delta <= 0) return; /* duplicate / late packet */
        if (delta > 1 && delta <= 4) {
            for (int i = 1; i < delta; ++i)
                queue_audio(s, pcm, opus_decode(s->opus, NULL, 0, pcm, AUDIO_FRAME, 0));
        }
    }
    s->audio_seq = seq;
    s->audio_seq_valid = 1;
    int frames = opus_decode(s->opus, data, (opus_int32)len, pcm, 5760, 0);
    if (frames > 0) { queue_audio(s, pcm, frames); SDL_AtomicAdd(&s->audio_packets, 1); }
    else SDL_AtomicAdd(&s->audio_errors, 1);
}

static int decode_worker(void *user) {
    orb_stream_t *s = user;
    AVFrame *frame = av_frame_alloc();
    if (!frame) { orb_stream_stop(s); SDL_AtomicSet(&s->decoder_done, 1); return 1; }
    for (;;) {
        SDL_LockMutex(s->mu);
        while (!s->qcount && !SDL_AtomicGet(&s->network_done)) SDL_CondWait(s->video_ready, s->mu);
        if (!s->qcount && SDL_AtomicGet(&s->network_done)) { SDL_UnlockMutex(s->mu); break; }
        au_t au = s->queue[s->qread];
        memset(&s->queue[s->qread], 0, sizeof(au_t));
        s->qread = (s->qread + 1) % VIDEO_QUEUE;
        --s->qcount;
        SDL_UnlockMutex(s->mu);
        if (au.reset) avcodec_flush_buffers(s->decoder);
        AVPacket packet = {0};
        packet.data = au.data;
        packet.size = au.len;
        int rc = avcodec_send_packet(s->decoder, &packet);
        free(au.data);
        if (rc < 0) {
            SDL_LockMutex(s->mu);
            clear_queue(s);
            ++s->generation;
            s->gate = s->reset = 1;
            SDL_UnlockMutex(s->mu);
            SDL_AtomicSet(&s->keyframe, 1);
            continue;
        }
        while ((rc = avcodec_receive_frame(s->decoder, frame)) == 0) {
            AVFrame *next = av_frame_clone(frame);
            if (next) {
                SDL_LockMutex(s->mu);
                if (au.generation == s->generation) {
                    av_frame_free(&s->latest);
                    s->latest = next;
                } else av_frame_free(&next);
                SDL_UnlockMutex(s->mu);
                if (SDL_AtomicAdd(&s->decoded, 1) == 0)
                    xc_log("[video] primeiro frame decodificado: %dx%d formato=%d", frame->width, frame->height, frame->format);
            }
            av_frame_unref(frame);
        }
        if (rc != AVERROR(EAGAIN) && rc != AVERROR_EOF) SDL_AtomicSet(&s->keyframe, 1);
    }
    av_frame_free(&frame);
    SDL_AtomicSet(&s->decoder_done, 1);
    return 0;
}

static void on_state(void *user, xcw_state_t state) {
    orb_stream_t *s = user;
    xc_log("[stream] WebRTC=%s", xcw_state_name(state));
    if (s->test) {
        SDL_LockMutex(s->mu);
        snprintf(s->code, sizeof(s->code), "%s", state == XCW_STREAMING ? "STREAMING" : "STARTING");
        snprintf(s->text, sizeof(s->text), "Peer local: %s", xcw_state_name(state));
        SDL_UnlockMutex(s->mu);
    }
}
static int session_worker(void *user) {
    orb_stream_t *s = user;
    xsm_thread_run(&s->machine);
    SDL_AtomicSet(&s->session_done, 1);
    return 0;
}

static char *read_signal(orb_stream_t *s, const char *name) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s", s->directory, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *text = calloc(1, 65537);
    if (!text) { fclose(f); return NULL; }
    size_t n = fread(text, 1, 65537, f);
    int ok = n <= 65536 && !ferror(f);
    fclose(f);
    if (!ok) { free(text); return NULL; }
    text[n] = 0;
    return text;
}
static int write_signal(orb_stream_t *s, const char *name, const char *text) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s", s->directory, name);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fputs(text, f) >= 0;
    if (fclose(f)) ok = 0;
    return ok ? 0 : -1;
}

static int network_worker(void *user) {
    orb_stream_t *s = user;
    xcw_cfg_t cfg;
    xcw_default_cfg(&cfg);
    cfg.video_kbps = s->kbps;
    xc_log("[video] pedido=%dx%d fps=%d bitrate_kbps=%d", cfg.video_w, cfg.video_h, cfg.video_fps, cfg.video_kbps);
    cfg.stun_url = s->test ? NULL : "stun:stun.l.google.com:19302";
    cfg.on_video_au = on_video;
    cfg.on_audio = on_audio;
    cfg.on_state = on_state;
    cfg.user = s;
    xcw_t *test_pc = NULL;
    int initialized = xcw_global_init() == 0, answered = 0;
    if (!initialized) goto finished;
    if (s->test) {
        test_pc = xcw_create(&cfg);
        const char *offer = test_pc ? xcw_create_offer(test_pc) : NULL;
        if (!offer || write_signal(s, "offer.sdp", offer)) goto finished;
        char locals[8192] = "";
        for (int i = 0; i < xcw_local_candidate_count(test_pc); ++i) {
            const char *candidate = xcw_local_candidate(test_pc, i);
            if (strlen(locals) + strlen(candidate) + 2 >= sizeof(locals)) goto finished;
            strcat(locals, candidate); strcat(locals, "\n");
        }
        if (write_signal(s, "local.cands", locals)) goto finished;
    } else xcws_init(&s->rtc, &s->machine, &cfg);
    uint64_t next_log = xcw_now_ms() + 2000;
    unsigned generation = 0;
    while (!(s->test ? SDL_AtomicGet(&s->stop) : SDL_AtomicGet(&s->session_done))) {
        if (s->test && !answered) {
            char *answer = read_signal(s, "answer.sdp");
            char *remotes = read_signal(s, "remote.cands");
            if (answer && remotes) {
                char *save = NULL;
                for (char *p = strtok_r(remotes, "\r\n", &save); p; p = strtok_r(NULL, "\r\n", &save))
                    xcw_add_remote_candidate(test_pc, p);
                if (xcw_set_answer(test_pc, answer)) { free(answer); free(remotes); goto finished; }
                answered = 1;
            }
            free(answer); free(remotes);
        }
        if (s->test) xcw_poll(test_pc);
        else xcws_step(&s->rtc);
        xcw_t *pc = s->test ? test_pc : xcws_pc(&s->rtc);
        if (!s->test && generation != s->rtc.gen) {
            generation = s->rtc.gen;
            s->last_video_ms = 0;
            SDL_LockMutex(s->mu);
            clear_queue(s);
            ++s->generation;
            av_frame_free(&s->latest);
            s->gate = s->reset = 1;
            SDL_UnlockMutex(s->mu);
            opus_decoder_ctl(s->opus, OPUS_RESET_STATE);
            s->audio_seq_valid = 0;
            if (s->audio) SDL_ClearQueuedAudio(s->audio);
        }
        if (pc) {
            xcp_gamepad_t pad;
            SDL_LockMutex(s->mu); pad = s->pad; SDL_UnlockMutex(s->mu);
            if (SDL_AtomicGet(&s->stop)) memset(&pad, 0, sizeof(pad));
            xcw_set_gamepad(pc, &pad);
            if (SDL_AtomicSet(&s->keyframe, 0)) xcw_resync_video(pc);
            if (!s->test && !SDL_AtomicGet(&s->stop) && s->last_video_ms &&
                xcw_now_ms() - s->last_video_ms >= 5000 && xcw_state(pc) == XCW_STREAMING) {
                xc_log("[stream] video sem frames por 5s; renegociando a mesma sessao");
                xsm_rtc_report(&s->machine, s->rtc.gen, XSM_RTC_DROPPED);
                s->last_video_ms = 0;
            }
            if (xcw_now_ms() >= next_log) {
                xcw_stats_t stats;
                xcw_get_stats(pc, &stats);
                xc_log("[stream] STAT rtc=%s video_au=%u decoded=%d audio=%d audio_errors=%d input=%u nack=%u queue_drops=%d srtp_errors=%u rtp=%u wait_idr=%u keyframes=%u jitter_drops=%u",
                       xcw_state_name(stats.state), stats.aus, SDL_AtomicGet(&s->decoded), SDL_AtomicGet(&s->audio_packets),
                       SDL_AtomicGet(&s->audio_errors), stats.input_sent, stats.nacks_sent,
                       SDL_AtomicGet(&s->queue_drops), stats.srtp_errors, stats.rtp_packets,
                       stats.waiting_keyframe, stats.keyframe_requests, stats.jit_dropped);
                next_log = xcw_now_ms() + 2000;
            }
        }
        SDL_Delay(1);
    }
finished:
    if (!initialized || (s->test && (!test_pc || !answered))) {
        SDL_LockMutex(s->mu);
        SDL_AtomicSet(&s->failed, 1);
        snprintf(s->code, sizeof(s->code), "ERROR");
        snprintf(s->text, sizeof(s->text), "Não foi possível iniciar o WebRTC.");
        SDL_UnlockMutex(s->mu);
        if (!s->test) xsm_request_stop(&s->machine);
    }
    if (test_pc) xcw_destroy(test_pc);
    if (!s->test) xcws_shutdown(&s->rtc);
    if (initialized) xcw_global_deinit();
    if (s->audio) SDL_ClearQueuedAudio(s->audio);
    SDL_LockMutex(s->mu);
    clear_queue(s);
    SDL_AtomicSet(&s->network_done, 1);
    SDL_CondSignal(s->video_ready);
    SDL_UnlockMutex(s->mu);
    return 0;
}

static orb_stream_t *start(const char *title, const char *name, int f2p, int kbps, const char *directory) {
    orb_stream_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->kbps = kbps;
    s->test = directory != NULL;
    s->gate = s->reset = 1;
    s->mu = SDL_CreateMutex();
    s->video_ready = SDL_CreateCond();
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    s->decoder = codec ? avcodec_alloc_context3(codec) : NULL;
    int error = 0;
    s->opus = opus_decoder_create(AUDIO_RATE, AUDIO_CHANNELS, &error);
    if (!s->mu || !s->video_ready || !s->decoder || !s->opus || error != OPUS_OK) goto fail;
    s->decoder->thread_count = 3;
    /* xCloud can send a single slice per frame. Slice-only threading then
     * leaves most RK3326 cores idle; frame threading pipelines three frames. */
    s->decoder->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    s->decoder->flags |= AV_CODEC_FLAG_LOW_DELAY;
    if (avcodec_open2(s->decoder, codec, NULL) < 0) goto fail;
    xc_log("[video] decoder=%s threads=%d active_thread_type=%d queue=%d",
           codec->name, s->decoder->thread_count, s->decoder->active_thread_type, VIDEO_QUEUE);
    SDL_AudioSpec desired = { .freq = AUDIO_RATE, .format = AUDIO_S16SYS,
        .channels = AUDIO_CHANNELS, .samples = 960 };
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0)
        s->audio = SDL_OpenAudioDevice(NULL, 0, &desired, NULL, 0);
    if (s->audio) SDL_PauseAudioDevice(s->audio, 0);
    else xc_log("[audio] sem saída SDL: %s", SDL_GetError());
    if (s->test) {
        if (strlen(directory) >= sizeof(s->directory)) goto fail;
        snprintf(s->directory, sizeof(s->directory), "%s", directory);
        SDL_AtomicSet(&s->session_done, 1);
    } else {
        xsm_cfg_t config;
        xsm_cfg_defaults(&config, title, name);
        config.f2p = f2p;
        config.active_policy = XSM_ACTIVE_ASK;
        config.net_live = 0; /* dedicated signaling thread; no Xenon CPU leases */
        xsm_init(&s->machine, &config, NULL);
    }
    s->decoder_thread = SDL_CreateThread(decode_worker, "h264-decode", s);
    if (!s->decoder_thread) goto fail;
    s->network_thread = SDL_CreateThread(network_worker, "webrtc", s);
    if (!s->network_thread) goto fail;
    if (!s->test) {
        s->session_thread = SDL_CreateThread(session_worker, "xcloud-session", s);
        if (!s->session_thread) goto fail;
    }
    return s;
fail:
    SDL_AtomicSet(&s->session_done, 1);
    SDL_AtomicSet(&s->stop, 1);
    if (!s->network_thread) {
        SDL_AtomicSet(&s->network_done, 1);
        if (s->video_ready) SDL_CondSignal(s->video_ready);
    }
    orb_stream_free(s);
    return NULL;
}
orb_stream_t *orb_stream_start(const char *title, const char *name, int f2p, int kbps) {
    return start(title, name, f2p, kbps, NULL);
}
orb_stream_t *orb_stream_test(const char *directory, int kbps) { return start(NULL, NULL, 0, kbps, directory); }
void orb_stream_stop(orb_stream_t *s) {
    if (!s) return;
    SDL_AtomicSet(&s->stop, 1);
    if (!s->test) xsm_request_stop(&s->machine);
}
int orb_stream_done(orb_stream_t *s) {
    return SDL_AtomicGet(&s->session_done) && SDL_AtomicGet(&s->network_done) && SDL_AtomicGet(&s->decoder_done);
}
void orb_stream_free(orb_stream_t *s) {
    if (!s) return;
    if (s->session_thread) SDL_WaitThread(s->session_thread, NULL);
    if (s->network_thread) SDL_WaitThread(s->network_thread, NULL);
    if (s->decoder_thread) SDL_WaitThread(s->decoder_thread, NULL);
    xc_log("[stream] terminou decoded=%d audio=%d audio_errors=%d queue_drops=%d",
           SDL_AtomicGet(&s->decoded), SDL_AtomicGet(&s->audio_packets),
           SDL_AtomicGet(&s->audio_errors), SDL_AtomicGet(&s->queue_drops));
    if (s->test && s->directory[0]) write_signal(s, "recv.done", "done\n");
    if (!s->test) xsm_free(&s->machine);
    if (s->audio) SDL_CloseAudioDevice(s->audio);
    if (s->opus) opus_decoder_destroy(s->opus);
    av_frame_free(&s->latest);
    avcodec_free_context(&s->decoder);
    clear_queue(s);
    SDL_DestroyCond(s->video_ready);
    SDL_DestroyMutex(s->mu);
    free(s);
}
void orb_stream_status(orb_stream_t *s, char *code, size_t nc, char *text, size_t nt) {
    if (!s->test && !SDL_AtomicGet(&s->failed)) xsm_snapshot(&s->machine, code, nc, text, nt);
    else {
        SDL_LockMutex(s->mu);
        snprintf(code, nc, "%s", s->code);
        snprintf(text, nt, "%s", s->text);
        SDL_UnlockMutex(s->mu);
    }
    if (SDL_AtomicGet(&s->stop)) {
        snprintf(code, nc, "STOPPING");
        snprintf(text, nt, "Encerrando a sessão...");
    }
}
void orb_stream_answer_active(orb_stream_t *s, int resume) { if (!s->test) xsm_answer_active(&s->machine, resume); }
void orb_stream_pad(orb_stream_t *s, const xcp_gamepad_t *pad) {
    SDL_LockMutex(s->mu); s->pad = *pad; SDL_UnlockMutex(s->mu);
}
AVFrame *orb_stream_frame(orb_stream_t *s) {
    SDL_LockMutex(s->mu);
    AVFrame *frame = s->latest;
    s->latest = NULL;
    SDL_UnlockMutex(s->mu);
    return frame;
}
