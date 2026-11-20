/* Local H.264 bring-up: measure decode throughput separately from cloud/network. */
#include <SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1000000000.0;
}

int main(int argc, char **argv) {
    const char *file = NULL, *decoder_name = NULL;
    int show = 0, max_frames = 600;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--show")) show = 1;
        else if (!strcmp(argv[i], "--decoder") && i + 1 < argc) decoder_name = argv[++i];
        else if (!strcmp(argv[i], "--max-frames") && i + 1 < argc) {
            char *end;
            long n = strtol(argv[++i], &end, 10);
            if (*end || n < 1 || n > 1000000) return 2;
            max_frames = (int)n;
        } else if (!file && argv[i][0] != '-') file = argv[i];
        else { fprintf(stderr, "Argumento inválido: %s\n", argv[i]); return 2; }
    }
    if (!file) { fprintf(stderr, "Uso: orbita-video-test ARQUIVO.h264 [--show] [--decoder NOME] [--max-frames N]\n"); return 2; }
    AVFormatContext *format = NULL;
    AVDictionary *options = NULL;
    av_dict_set(&options, "protocol_whitelist", "file", 0);
    int rc = avformat_open_input(&format, file, NULL, &options);
    av_dict_free(&options);
    if (rc < 0 || avformat_find_stream_info(format, NULL) < 0) {
        fprintf(stderr, "Não foi possível abrir o vídeo local.\n");
        avformat_close_input(&format); return 1;
    }
    int stream = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (stream < 0 || format->streams[stream]->codecpar->codec_id != AV_CODEC_ID_H264) {
        fprintf(stderr, "O teste exige vídeo H.264.\n"); avformat_close_input(&format); return 1;
    }
    const AVCodec *codec = decoder_name ? avcodec_find_decoder_by_name(decoder_name) : avcodec_find_decoder(AV_CODEC_ID_H264);
    AVCodecContext *ctx = codec ? avcodec_alloc_context3(codec) : NULL;
    if (!ctx || codec->id != AV_CODEC_ID_H264 || avcodec_parameters_to_context(ctx, format->streams[stream]->codecpar) < 0) {
        fprintf(stderr, "Decoder H.264 indisponível.\n"); avcodec_free_context(&ctx); avformat_close_input(&format); return 1;
    }
    ctx->thread_count = 4;
    if (avcodec_open2(ctx, codec, NULL) < 0) {
        fprintf(stderr, "Não foi possível iniciar %s.\n", codec->name);
        avcodec_free_context(&ctx); avformat_close_input(&format); return 1;
    }
    SDL_Window *win = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    struct SwsContext *sws = NULL;
    AVFrame *frame = av_frame_alloc(), *yuv = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();
    int allocated = frame && yuv && packet;
    if (show && !SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER)) {
        win = SDL_CreateWindow("orbita · teste H.264", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480, SDL_WINDOW_FULLSCREEN_DESKTOP);
        if (win) renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (win && !renderer) renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }
    int frames = 0, failed = !allocated || (show && !renderer), ended = 0, eof = 0;
    int width = 0, height = 0;
    double start = now();
    while (!failed && !ended && frames < max_frames) {
        rc = avcodec_receive_frame(ctx, frame);
        if (rc == AVERROR_EOF) break;
        if (rc == AVERROR(EAGAIN)) {
            if (eof) { failed = 1; break; }
            int read_rc;
            do {
                av_packet_unref(packet);
                read_rc = av_read_frame(format, packet);
            } while (read_rc >= 0 && packet->stream_index != stream);
            if (read_rc < 0 && read_rc != AVERROR_EOF) { failed = 1; break; }
            eof = read_rc == AVERROR_EOF;
            if (avcodec_send_packet(ctx, eof ? NULL : packet) < 0) failed = 1;
            continue;
        }
        if (rc < 0) { failed = 1; break; }
        ++frames;
        if (show) {
            if (frame->width != width || frame->height != height) {
                SDL_DestroyTexture(texture);
                texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, frame->width, frame->height);
                av_frame_unref(yuv);
                yuv->format = AV_PIX_FMT_YUV420P;
                yuv->width = frame->width; yuv->height = frame->height;
                if (!texture || av_frame_get_buffer(yuv, 32) < 0) { failed = 1; break; }
            }
            AVFrame *display = frame;
            if (frame->format != AV_PIX_FMT_YUV420P) {
                sws = sws_getCachedContext(sws, frame->width, frame->height, frame->format,
                                           frame->width, frame->height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, NULL, NULL, NULL);
                if (!sws || sws_scale(sws, (const uint8_t * const *)frame->data, frame->linesize, 0,
                                      frame->height, yuv->data, yuv->linesize) <= 0) { failed = 1; break; }
                display = yuv;
            }
            SDL_RenderSetLogicalSize(renderer, frame->width, frame->height);
            if (SDL_UpdateYUVTexture(texture, NULL, display->data[0], display->linesize[0],
                                    display->data[1], display->linesize[1], display->data[2], display->linesize[2])) {
                failed = 1; break;
            }
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer); SDL_RenderCopy(renderer, texture, NULL, NULL); SDL_RenderPresent(renderer);
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) ||
                    (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == SDL_CONTROLLER_BUTTON_B)) ended = 1;
            }
        }
        width = frame->width; height = frame->height;
        av_frame_unref(frame);
    }
    double elapsed = now() - start;
    printf("decoder=%s\nframes=%d\nwidth=%d\nheight=%d\nelapsed_s=%.3f\nfps=%.2f\ndisplay=%s\n",
           codec->name, frames, width, height, elapsed, elapsed > 0 ? frames / elapsed : 0, show ? "yes" : "no");
    sws_freeContext(sws);
    av_packet_free(&packet); av_frame_free(&frame); av_frame_free(&yuv);
    avcodec_free_context(&ctx); avformat_close_input(&format);
    SDL_DestroyTexture(texture); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(win);
    if (show) SDL_Quit();
    return failed || frames == 0 ? 1 : 0;
}
