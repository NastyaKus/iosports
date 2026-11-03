/*
 * AudioQueue output queues (AudioToolbox).
 *
 * Each queue drives an internal RemoteIO unit (the same backend: AAudio on
 * Android, a paced writer on the host) whose render callback plays the
 * enqueued buffers in order. When a buffer has been consumed, the app's
 * output callback gets it back to refill, on the queue's own thread or, if
 * the app named a run loop, on the main thread. Linear PCM (integer or
 * float, interleaved) plays as is; other formats play as silence while the
 * buffers still cycle, so apps keep running.
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#define FOURCC(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
struct asbd { double rate; uint32_t fmt, flags, bytesPerPacket, framesPerPacket, bytesPerFrame, channels, bits, reserved; };
struct acdesc { uint32_t type, subtype, manufacturer, flags, mask; };
struct render_cb { void *proc; void *refcon; };
struct audio_timestamp { double sampleTime; uint64_t hostTime; double rateScalar; uint64_t wordClock; uint8_t smpte[24]; uint32_t flags, reserved; };
struct audio_buffer { uint32_t channels, bytes; void *data; };
struct abl { uint32_t n; uint32_t pad; struct audio_buffer b[8]; };
struct packet_desc { int64_t start; uint32_t frames, bytes; };

/* AudioQueueBuffer, as the app sees it */
struct aq_buffer {
    uint32_t capacity; uint32_t pad0; void *data; uint32_t size; uint32_t pad1; void *user;
    uint32_t pd_capacity; uint32_t pad2; struct packet_desc *pds; uint32_t pd_count; uint32_t pad3;
};

int32_t iosrt_AudioComponentInstanceNew(void *comp, void **out);
int32_t iosrt_AudioComponentInstanceDispose(void *u);
int32_t iosrt_AudioUnitSetProperty(void *u, uint32_t prop, uint32_t scope, uint32_t elem, const void *data, uint32_t size);
int32_t iosrt_AudioOutputUnitStart(void *u);
int32_t iosrt_AudioOutputUnitStop(void *u);
void rt_after(double delay, void (*fn)(void *), void *ctx);

#define AQ_MAGIC 0x41517565
#define MAXQ 64
typedef void (*aq_output_cb)(void *user, void *queue, struct aq_buffer *buf);
struct listener { uint32_t prop; void *proc, *user; };
struct aqueue {
    uint32_t magic; struct asbd fmt; aq_output_cb cb; void *user; int on_main;
    void *unit;
    pthread_mutex_t lock;
    struct aq_buffer *fifo[MAXQ]; int head, count; uint32_t offset;   /* offset into fifo[head] */
    struct aq_buffer *done[MAXQ]; int ndone;                          /* consumed, waiting for the callback */
    struct aq_buffer *owned[256]; int nowned;
    int running, paused, pcm, stopping;
    float volume;
    double sample_time;
    struct listener ls[8]; int nls;
};
static int valid(struct aqueue *q) { return q && q->magic == AQ_MAGIC; }

/* hands consumed buffers back to the app */
static void deliver(struct aqueue *q) {
    for (;;) {
        pthread_mutex_lock(&q->lock);
        if (!q->ndone) { pthread_mutex_unlock(&q->lock); return; }
        struct aq_buffer *b = q->done[0];
        memmove(q->done, q->done + 1, (size_t)(--q->ndone) * sizeof *q->done);
        pthread_mutex_unlock(&q->lock);
        if (q->cb && !q->stopping) q->cb(q->user, q, b);
    }
}
static void deliver_main(void *p) { struct aqueue *q = p; if (valid(q)) deliver(q); }

/* the internal unit's render callback: copy from the queued buffers */
static int32_t render(void *refcon, uint32_t *flags, const struct audio_timestamp *ts, uint32_t bus, uint32_t frames, struct abl *io) {
    (void)ts; (void)bus;
    struct aqueue *q = refcon;
    uint8_t *out = io->n ? io->b[0].data : NULL;
    uint32_t want = io->n ? io->b[0].bytes : 0, got = 0;
    uint32_t bpf = q->fmt.bytesPerFrame ? q->fmt.bytesPerFrame : 4;
    int any_done = 0;
    pthread_mutex_lock(&q->lock);
    if (!q->paused) {
        while (got < want && q->count) {
            struct aq_buffer *b = q->fifo[q->head];
            uint32_t left = b->size > q->offset ? b->size - q->offset : 0;
            uint32_t n = left < want - got ? left : want - got;
            if (q->pcm && n) {
                if (q->volume >= 0.999f) memcpy(out + got, (uint8_t *)b->data + q->offset, n);
                else if (q->fmt.flags & 1) { const float *s = (const float *)((uint8_t *)b->data + q->offset); float *d = (float *)(out + got); for (uint32_t i = 0; i < n / 4; i++) d[i] = s[i] * q->volume; }
                else if (q->fmt.bits == 16) { const int16_t *s = (const int16_t *)((uint8_t *)b->data + q->offset); int16_t *d = (int16_t *)(out + got); for (uint32_t i = 0; i < n / 2; i++) d[i] = (int16_t)(s[i] * q->volume); }
                else memcpy(out + got, (uint8_t *)b->data + q->offset, n);
            } else if (n) memset(out + got, 0, n);
            got += n; q->offset += n;
            if (q->offset >= b->size) {
                q->head = (q->head + 1) % MAXQ; q->count--; q->offset = 0;
                if (q->ndone < MAXQ) q->done[q->ndone++] = b;
                any_done = 1;
            }
            if (!q->pcm && got < want) {   /* compressed data: time passes per packet; approximate by bytes */
                memset(out + got, 0, want - got); got = want;
            }
        }
    }
    q->sample_time += frames;
    pthread_mutex_unlock(&q->lock);
    if (got < want) memset(out + got, 0, want - got);
    if (!got && flags) *flags |= 16;   /* OutputIsSilence */
    (void)bpf;
    if (any_done) { if (q->on_main) rt_after(0, deliver_main, q); else deliver(q); }
    return 0;
}

static int is_pcm(const struct asbd *f) { return f->fmt == FOURCC('l', 'p', 'c', 'm') && !(f->flags & 32) && (f->bits == 16 || f->bits == 32); }

int32_t iosrt_AudioQueueNewOutput(const struct asbd *fmt, aq_output_cb cb, void *user, void *runloop, void *mode, uint32_t flags, struct aqueue **out) {
    (void)mode; (void)flags;
    if (!fmt || !out) return -50;
    struct aqueue *q = rt_xcalloc(1, sizeof *q);
    q->magic = AQ_MAGIC; q->fmt = *fmt; q->cb = cb; q->user = user; q->on_main = runloop != NULL; q->volume = 1;
    pthread_mutex_init(&q->lock, NULL);
    q->pcm = is_pcm(fmt);
    static struct acdesc rio = { FOURCC('a', 'u', 'o', 'u'), FOURCC('r', 'i', 'o', 'c'), FOURCC('a', 'p', 'p', 'l'), 0, 0 };
    if (iosrt_AudioComponentInstanceNew(&rio, &q->unit) != 0) { free(q); return -50; }
    struct asbd uf = *fmt;
    if (!q->pcm) {   /* the unit plays 16-bit PCM silence in place of compressed data */
        memset(&uf, 0, sizeof uf); uf.rate = fmt->rate > 0 ? fmt->rate : 44100; uf.fmt = FOURCC('l', 'p', 'c', 'm'); uf.flags = 4 | 8;
        uf.channels = fmt->channels ? fmt->channels : 2; uf.bits = 16; uf.bytesPerFrame = uf.bytesPerPacket = 2 * uf.channels; uf.framesPerPacket = 1;
        rt_warn("AudioQueue: format '%c%c%c%c' is not decoded yet; the queue plays silence", fmt->fmt >> 24, fmt->fmt >> 16 & 255, fmt->fmt >> 8 & 255, fmt->fmt & 255);
    }
    q->fmt.bytesPerFrame = uf.bytesPerFrame;
    iosrt_AudioUnitSetProperty(q->unit, 8, 1, 0, &uf, sizeof uf);
    struct render_cb rcb = { (void *)render, q };
    iosrt_AudioUnitSetProperty(q->unit, 23, 1, 0, &rcb, sizeof rcb);
    *out = q;
    return 0;
}
int32_t iosrt_AudioQueueAllocateBufferWithPacketDescriptions(struct aqueue *q, uint32_t size, uint32_t npd, struct aq_buffer **out) {
    if (!valid(q) || !out) return -50;
    struct aq_buffer *b = rt_xcalloc(1, sizeof *b);
    b->capacity = size; b->data = rt_xcalloc(1, size ? size : 1);
    if (npd) { b->pd_capacity = npd; b->pds = rt_xcalloc(npd, sizeof *b->pds); }
    pthread_mutex_lock(&q->lock);
    if (q->nowned < 256) q->owned[q->nowned++] = b;
    pthread_mutex_unlock(&q->lock);
    *out = b;
    return 0;
}
int32_t iosrt_AudioQueueAllocateBuffer(struct aqueue *q, uint32_t size, struct aq_buffer **out) { return iosrt_AudioQueueAllocateBufferWithPacketDescriptions(q, size, 0, out); }
static void free_buffer(struct aq_buffer *b) { free(b->data); free(b->pds); free(b); }
int32_t iosrt_AudioQueueFreeBuffer(struct aqueue *q, struct aq_buffer *b) {
    if (!valid(q) || !b) return -50;
    pthread_mutex_lock(&q->lock);
    for (int i = 0; i < q->nowned; i++) if (q->owned[i] == b) { q->owned[i] = q->owned[--q->nowned]; break; }
    pthread_mutex_unlock(&q->lock);
    free_buffer(b);
    return 0;
}
int32_t iosrt_AudioQueueEnqueueBuffer(struct aqueue *q, struct aq_buffer *b, uint32_t npd, const struct packet_desc *pds) {
    if (!valid(q) || !b) return -50;
    if (npd && pds && b->pds && b != NULL) { uint32_t n = npd < b->pd_capacity ? npd : b->pd_capacity; memcpy(b->pds, pds, n * sizeof *pds); b->pd_count = n; }
    pthread_mutex_lock(&q->lock);
    if (q->count >= MAXQ) { pthread_mutex_unlock(&q->lock); return -66686; }   /* kAudioQueueErr_BufferEnqueuedTwice-ish: queue full */
    q->fifo[(q->head + q->count) % MAXQ] = b; q->count++;
    pthread_mutex_unlock(&q->lock);
    return 0;
}
int32_t iosrt_AudioQueueEnqueueBufferWithParameters(struct aqueue *q, struct aq_buffer *b, uint32_t npd, const struct packet_desc *pds,
                                                     uint32_t trimStart, uint32_t trimEnd, uint32_t nparams, const void *params, const void *start, void *actual) {
    (void)trimStart; (void)trimEnd; (void)nparams; (void)params; (void)start; (void)actual;
    return iosrt_AudioQueueEnqueueBuffer(q, b, npd, pds);
}
static void notify_running(struct aqueue *q) {
    for (int i = 0; i < q->nls; i++) if (q->ls[i].prop == FOURCC('a', 'q', 'r', 'n'))
        ((void (*)(void *, void *, uint32_t))q->ls[i].proc)(q->ls[i].user, q, q->ls[i].prop);
}
int32_t iosrt_AudioQueueStart(struct aqueue *q, const void *start) {
    (void)start;
    if (!valid(q)) return -50;
    q->paused = 0; q->stopping = 0;
    if (!q->running) { q->running = 1; iosrt_AudioOutputUnitStart(q->unit); notify_running(q); }
    return 0;
}
int32_t iosrt_AudioQueuePrime(struct aqueue *q, uint32_t frames, uint32_t *prepared) { (void)frames; if (prepared) *prepared = 0; return valid(q) ? 0 : -50; }
int32_t iosrt_AudioQueuePause(struct aqueue *q) { if (!valid(q)) return -50; q->paused = 1; return 0; }
int32_t iosrt_AudioQueueFlush(struct aqueue *q) { return valid(q) ? 0 : -50; }
/* drops everything queued; the app gets the buffers back */
static void reset_buffers(struct aqueue *q, int call_back) {
    pthread_mutex_lock(&q->lock);
    while (q->count) { struct aq_buffer *b = q->fifo[q->head]; q->head = (q->head + 1) % MAXQ; q->count--; if (q->ndone < MAXQ) q->done[q->ndone++] = b; }
    q->offset = 0;
    if (!call_back) q->ndone = 0;
    pthread_mutex_unlock(&q->lock);
    if (call_back) deliver(q);
}
int32_t iosrt_AudioQueueStop(struct aqueue *q, long immediate) {
    (void)immediate;
    if (!valid(q)) return -50;
    if (q->running) { q->running = 0; iosrt_AudioOutputUnitStop(q->unit); }
    q->stopping = 1;     /* no refill callbacks while stopping, as on iOS */
    reset_buffers(q, 0);
    q->sample_time = 0;
    notify_running(q);
    return 0;
}
int32_t iosrt_AudioQueueReset(struct aqueue *q) { if (!valid(q)) return -50; reset_buffers(q, 1); return 0; }
int32_t iosrt_AudioQueueDispose(struct aqueue *q, long immediate) {
    (void)immediate;
    if (!valid(q)) return -50;
    if (q->running) { q->running = 0; iosrt_AudioOutputUnitStop(q->unit); }
    iosrt_AudioComponentInstanceDispose(q->unit);
    q->magic = 0;
    for (int i = 0; i < q->nowned; i++) free_buffer(q->owned[i]);
    pthread_mutex_destroy(&q->lock);
    free(q);
    return 0;
}
int32_t iosrt_AudioQueueSetParameter(struct aqueue *q, uint32_t param, float v) {
    if (!valid(q)) return -50;
    if (param == 1) q->volume = v < 0 ? 0 : v > 1 ? 1 : v;   /* kAudioQueueParam_Volume */
    return 0;
}
int32_t iosrt_AudioQueueGetParameter(struct aqueue *q, uint32_t param, float *v) {
    if (!valid(q) || !v) return -50;
    *v = param == 1 ? q->volume : param == 2 ? 1.0f : 0;     /* Volume, PlayRate */
    return 0;
}
int32_t iosrt_AudioQueueGetCurrentTime(struct aqueue *q, void *timeline, struct audio_timestamp *ts, unsigned char *disc) {
    (void)timeline;
    if (!valid(q)) return -50;
    if (ts) {
        memset(ts, 0, sizeof *ts);
        ts->sampleTime = q->sample_time; ts->rateScalar = 1; ts->flags = 1 | 2;
        struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); ts->hostTime = (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
    }
    if (disc) *disc = 0;
    return 0;
}
int32_t iosrt_AudioQueueCreateTimeline(struct aqueue *q, void **out) { if (out) *out = q; return valid(q) ? 0 : -50; }
int32_t iosrt_AudioQueueDisposeTimeline(struct aqueue *q, void *t) { (void)t; return valid(q) ? 0 : -50; }
int32_t iosrt_AudioQueueGetProperty(struct aqueue *q, uint32_t prop, void *data, uint32_t *size) {
    if (!valid(q)) return -50;
    if (prop == FOURCC('a', 'q', 'r', 'n') && data) { *(uint32_t *)data = (uint32_t)q->running; if (size) *size = 4; return 0; }
    if (prop == FOURCC('a', 'q', 'f', 't') && data) { memcpy(data, &q->fmt, sizeof q->fmt); if (size) *size = sizeof q->fmt; return 0; }   /* StreamDescription */
    if (prop == FOURCC('d', 'e', 'c', 'r') && data) { *(double *)data = q->fmt.rate; if (size) *size = 8; return 0; }    /* DeviceSampleRate */
    if (prop == FOURCC('d', 'c', 'h', 'n') && data) { *(uint32_t *)data = 2; if (size) *size = 4; return 0; }           /* DeviceNumberChannels */
    if (size) *size = 0;
    return 0;
}
int32_t iosrt_AudioQueueGetPropertySize(struct aqueue *q, uint32_t prop, uint32_t *size) {
    if (!valid(q) || !size) return -50;
    *size = prop == FOURCC('a', 'q', 'f', 't') ? (uint32_t)sizeof(struct asbd) : prop == FOURCC('d', 'e', 'c', 'r') ? 8 : 4;
    return 0;
}
int32_t iosrt_AudioQueueSetProperty(struct aqueue *q, uint32_t prop, const void *data, uint32_t size) { (void)prop; (void)data; (void)size; return valid(q) ? 0 : -50; }
int32_t iosrt_AudioQueueAddPropertyListener(struct aqueue *q, uint32_t prop, void *proc, void *user) {
    if (!valid(q)) return -50;
    if (q->nls < 8) q->ls[q->nls++] = (struct listener){ prop, proc, user };
    return 0;
}
int32_t iosrt_AudioQueueRemovePropertyListener(struct aqueue *q, uint32_t prop, void *proc, void *user) {
    if (!valid(q)) return -50;
    for (int i = 0; i < q->nls; i++) if (q->ls[i].prop == prop && q->ls[i].proc == proc && q->ls[i].user == user) { q->ls[i] = q->ls[--q->nls]; break; }
    return 0;
}
/* input queues (recording): not available yet, fail like a device without a microphone */
int32_t iosrt_AudioQueueNewInput(const struct asbd *fmt, void *cb, void *user, void *rl, void *mode, uint32_t flags, void **out) {
    (void)fmt; (void)cb; (void)user; (void)rl; (void)mode; (void)flags;
    if (out) *out = NULL;
    rt_warn("AudioQueueNewInput: recording is not available yet");
    return FOURCC('!', 'd', 'e', 'v');   /* kAudioQueueErr_InvalidDevice-like */
}
