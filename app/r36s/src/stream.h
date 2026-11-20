#ifndef ORBITA_STREAM_H
#define ORBITA_STREAM_H
#include <SDL.h>
#include "xc_proto.h"
#include <libavutil/frame.h>

typedef struct orb_stream orb_stream_t;
/* Main thread owns lifetime; renderer and textures stay on that thread. */
orb_stream_t *orb_stream_start(const char *title, const char *name, int f2p, int kbps);
void orb_stream_stop(orb_stream_t *s);
int orb_stream_done(orb_stream_t *s);
void orb_stream_free(orb_stream_t *s); /* after done; joins workers */
void orb_stream_status(orb_stream_t *s, char *code, size_t nc, char *text, size_t nt);
void orb_stream_answer_active(orb_stream_t *s, int resume);
void orb_stream_pad(orb_stream_t *s, const xcp_gamepad_t *pad);
/* Returns a ref to the newest decoded frame; caller av_frame_free(). */
AVFrame *orb_stream_frame(orb_stream_t *s);

/* Offline peer test uses the exact same UDP, decoder, audio and input path.
 * Signaling is exchanged with a local peer through files in this directory. */
orb_stream_t *orb_stream_test(const char *directory, int kbps);
#endif
