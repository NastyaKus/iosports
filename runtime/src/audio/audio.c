/*
 * AudioToolbox / AVAudioSession HLE.
 *
 * RemoteIO ('auou'/'rioc') output units are emulated: the guest configures
 * the stream format and render callback exactly as on iOS; on start the
 * platform backend (AAudio on Android, a paced WAV writer on the host) pulls
 * audio by calling the guest render callback with a correctly laid out
 * AudioBufferList/AudioTimeStamp and converts the result to float.
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

#define IM(c, s, f) rt_native_method(c, s, (void *)(f), "")
#define CM(c, s, f) rt_native_cmethod(c, s, (void *)(f), "")
RT_DECLARE_CLASS(NSObject);
RT_DEFINE_CLASS(AVAudioSession);

#define FOURCC(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
#define kAudioFormatFlagIsFloat 1u
#define kAudioFormatFlagIsSignedInteger 4u
#define kAudioFormatFlagIsNonInterleaved 32u

struct asbd { double rate; uint32_t fmt, flags, bytesPerPacket, framesPerPacket, bytesPerFrame, channels, bits, reserved; };
struct acdesc { uint32_t type, subtype, manufacturer, flags, mask; };
struct render_cb { void *proc; void *refcon; };
struct audio_timestamp { double sampleTime; uint64_t hostTime; double rateScalar; uint64_t wordClock; uint8_t smpte[24]; uint32_t flags, reserved; };
struct audio_buffer { uint32_t channels, bytes; void *data; };
struct abl { uint32_t n; uint32_t pad; struct audio_buffer b[8]; };

#define UNIT_MAGIC 0x4155696f
#define MAX_BUS 32
enum { UK_IO, UK_MIXER, UK_CONVERTER, UK_OTHER };
struct audio_unit;
struct unit_input { struct render_cb cb; struct audio_unit *src; uint32_t src_bus; float volume; int enabled; };
struct audio_unit {
    uint32_t magic; struct acdesc desc; int kind;
    struct unit_input in[MAX_BUS]; uint32_t nbus; float master_volume;
    float *mixbuf; size_t mixcap;
    struct asbd out_fmt, in_fmt;
    struct render_cb cb, input_cb;
    uint32_t enable_out, enable_in, max_frames;
    volatile int running;
    double sample_time;
    pthread_t thread; int has_thread;
    void *backend;
    uint8_t *scratch; size_t scratch_size;
};
static struct acdesc g_rio = { FOURCC('a', 'u', 'o', 'u'), FOURCC('r', 'i', 'o', 'c'), FOURCC('a', 'p', 'p', 'l'), 0, 0 };
/* components handed out for other unit types: one descriptor slot per distinct request */
static struct acdesc g_comps[32]; static int g_ncomps;
static int unit_kind(const struct acdesc *d) {
    if (d->type == FOURCC('a', 'u', 'o', 'u')) return UK_IO;
    if (d->type == FOURCC('a', 'u', 'm', 'x')) return UK_MIXER;
    if (d->type == FOURCC('a', 'u', 'f', 'c')) return UK_CONVERTER;
    return UK_OTHER;
}

static void default_format(struct asbd *f) {
    memset(f, 0, sizeof *f);
    f->rate = 44100; f->fmt = FOURCC('l', 'p', 'c', 'm');
    f->flags = kAudioFormatFlagIsFloat | 8 | kAudioFormatFlagIsNonInterleaved;
    f->bytesPerPacket = 4; f->framesPerPacket = 1; f->bytesPerFrame = 4; f->channels = 2; f->bits = 32;
}

void *iosrt_AudioComponentFindNext(void *after, const struct acdesc *d) {
    if (after) return NULL;
    if (d && d->type == g_rio.type && (d->subtype == g_rio.subtype || d->subtype == FOURCC('v', 'p', 'i', 'o'))) return &g_rio;
    if (d && (d->type == FOURCC('a', 'u', 'm', 'x') || d->type == FOURCC('a', 'u', 'f', 'c') || d->type == FOURCC('a', 'u', 'o', 'u'))) {
        for (int i = 0; i < g_ncomps; i++) if (g_comps[i].type == d->type && g_comps[i].subtype == d->subtype) return &g_comps[i];
        if (g_ncomps < 32) { g_comps[g_ncomps] = *d; return &g_comps[g_ncomps++]; }
    }
    if (d) {
        char t[5] = { (char)(d->type >> 24), (char)(d->type >> 16), (char)(d->type >> 8), (char)d->type, 0 };
        char s[5] = { (char)(d->subtype >> 24), (char)(d->subtype >> 16), (char)(d->subtype >> 8), (char)d->subtype, 0 };
        rt_warn("AudioComponentFindNext: component '%s'/'%s' not available (only RemoteIO output is emulated)", t, s);
    }
    return NULL;
}
int32_t iosrt_AudioComponentGetDescription(void *comp, struct acdesc *out) { if (!comp) return -50; *out = *(struct acdesc *)comp; return 0; }
int32_t iosrt_AudioComponentInstanceNew(void *comp, struct audio_unit **out) {
    if (!comp) return -50;
    struct audio_unit *u = rt_xcalloc(1, sizeof *u);
    u->magic = UNIT_MAGIC; u->desc = *(struct acdesc *)comp; u->enable_out = 1; u->max_frames = 4096;
    u->kind = unit_kind(&u->desc); u->nbus = u->kind == UK_MIXER ? 8 : 1; u->master_volume = 1;
    for (int i = 0; i < MAX_BUS; i++) { u->in[i].volume = 1; u->in[i].enabled = 1; }
    default_format(&u->out_fmt); default_format(&u->in_fmt);
    *out = u;
    return 0;
}
static int valid(struct audio_unit *u) { return u && u->magic == UNIT_MAGIC; }
/* the component handle is just a description; the instance keeps its own copy */
void *iosrt_AudioComponentInstanceGetComponent(struct audio_unit *u) { return valid(u) ? (void *)&u->desc : NULL; }
int32_t iosrt_AudioOutputUnitStop(struct audio_unit *u);
int32_t iosrt_AudioComponentInstanceDispose(struct audio_unit *u) {
    if (!valid(u)) return -50;
    iosrt_AudioOutputUnitStop(u);
    u->magic = 0; free(u->scratch); free(u);
    return 0;
}
int32_t iosrt_AudioUnitSetProperty(struct audio_unit *u, uint32_t prop, uint32_t scope, uint32_t elem, const void *data, uint32_t size) {
    if (!valid(u)) return -50;
    switch (prop) {
    case 8:   /* StreamFormat */
        if (size < sizeof(struct asbd)) return -50;
        if (scope == 1 && elem == 0) memcpy(&u->out_fmt, data, sizeof u->out_fmt);
        else if (scope == 2 && elem == 1) memcpy(&u->in_fmt, data, sizeof u->in_fmt);
        else if (scope == 2 && elem == 0) { /* hardware side: ignore */ }
        return 0;
    case 23:  /* SetRenderCallback (per input bus) */
        if (size < sizeof(struct render_cb)) return -50;
        if (elem < MAX_BUS) { memcpy(&u->in[elem].cb, data, sizeof(struct render_cb)); u->in[elem].src = NULL; }
        if (elem == 0) memcpy(&u->cb, data, sizeof u->cb);
        return 0;
    case 11:  /* ElementCount */
        if (scope == 1) { uint32_t n = *(const uint32_t *)data; u->nbus = n > MAX_BUS ? MAX_BUS : n; }
        return 0;
    case 1:   /* MakeConnection */
        if (size >= 12) { const uint32_t *c = data; struct audio_unit *src = *(struct audio_unit *const *)data; (void)c; (void)src; }
        return 0;
    case 2005: /* SetInputCallback */
        memcpy(&u->input_cb, data, sizeof u->input_cb); return 0;
    case 2003: /* EnableIO */
        if (elem == 1) u->enable_in = *(const uint32_t *)data; else u->enable_out = *(const uint32_t *)data;
        if (elem == 1 && u->enable_in) rt_warn("RemoteIO input (microphone) is not implemented yet: AudioUnitRender on bus 1 returns silence");
        return 0;
    case 14:  /* MaximumFramesPerSlice */
        u->max_frames = *(const uint32_t *)data; return 0;
    default:
        return 0;
    }
}
int32_t iosrt_AudioUnitGetProperty(struct audio_unit *u, uint32_t prop, uint32_t scope, uint32_t elem, void *data, uint32_t *size) {
    if (!valid(u)) return -50;
    switch (prop) {
    case 8: {
        const struct asbd *f = (scope == 2 && elem == 1) ? &u->in_fmt : &u->out_fmt;
        if (*size < sizeof *f) return -50;
        memcpy(data, f, sizeof *f); *size = sizeof *f; return 0;
    }
    case 14: *(uint32_t *)data = u->max_frames; *size = 4; return 0;
    case 2: { double r = u->out_fmt.rate; memcpy(data, &r, 8); *size = 8; return 0; }
    case 2003: *(uint32_t *)data = elem == 1 ? u->enable_in : u->enable_out; *size = 4; return 0;
    case 11: *(uint32_t *)data = u->nbus; *size = 4; return 0;
    case 12: { double l = 0; memcpy(data, &l, 8); *size = 8; return 0; }        /* Latency */
    case 2006: *(uint32_t *)data = 1; *size = 4; return 0;                        /* IsRunning-ish */
    case 2001: *(uint32_t *)data = u->running ? 1 : 0; *size = 4; return 0;        /* kAudioOutputUnitProperty_IsRunning */
    case 101:  *(uint32_t *)data = 0; *size = 4; return 0;                        /* IsInterAppConnected: never */
    default: return -10879;  /* kAudioUnitErr_InvalidProperty */
    }
}
int32_t iosrt_AudioUnitGetPropertyInfo(struct audio_unit *u, uint32_t prop, uint32_t scope, uint32_t elem, uint32_t *size, long *writable) {
    (void)u; (void)scope; (void)elem;
    if (size) *size = prop == 8 ? sizeof(struct asbd) : prop == 2 ? 8 : 4;
    if (writable) *(char *)writable = 1;
    return 0;
}
int32_t iosrt_AudioUnitInitialize(struct audio_unit *u) { return valid(u) ? 0 : -50; }
int32_t iosrt_AudioUnitUninitialize(struct audio_unit *u) { return valid(u) ? 0 : -50; }
int32_t iosrt_AudioUnitReset(struct audio_unit *u, uint32_t s, uint32_t e) { (void)s; (void)e; return valid(u) ? 0 : -50; }
/* multichannel mixer: param 0 = volume, 1 = enable; scope 1 input bus, 2 output (master) */
int32_t iosrt_AudioUnitSetParameter(struct audio_unit *u, uint32_t id, uint32_t s, uint32_t e, float v, uint32_t off) {
    (void)off; if (!valid(u)) return -50;
    if (id == 0) { if (s == 1 && e < MAX_BUS) u->in[e].volume = v; else if (s == 2) u->master_volume = v; }
    else if (id == 1 && s == 1 && e < MAX_BUS) u->in[e].enabled = v != 0;
    return 0;
}
int32_t iosrt_AudioUnitGetParameter(struct audio_unit *u, uint32_t id, uint32_t s, uint32_t e, float *v) {
    if (!valid(u) || !v) return -50;
    *v = id == 0 ? (s == 1 && e < MAX_BUS ? u->in[e].volume : u->master_volume) : id == 1 && e < MAX_BUS ? (float)u->in[e].enabled : 0;
    return 0;
}
int32_t iosrt_AudioUnitAddPropertyListener(struct audio_unit *u, uint32_t p, void *proc, void *ud) { (void)u; (void)p; (void)proc; (void)ud; return 0; }
int32_t iosrt_AudioUnitRemovePropertyListenerWithUserData(struct audio_unit *u, uint32_t p, void *proc, void *ud) { (void)u; (void)p; (void)proc; (void)ud; return 0; }
int32_t iosrt_AudioUnitAddRenderNotify(struct audio_unit *u, void *proc, void *ud) { (void)u; (void)proc; (void)ud; return 0; }
int32_t iosrt_AudioUnitRemoveRenderNotify(struct audio_unit *u, void *proc, void *ud) { (void)u; (void)proc; (void)ud; return 0; }
/* Pull `frames` of a unit's output into `io` (same format the consumer asked for).
 * IO unit: its single input (callback or connected node). Mixer: sum of enabled inputs
 * (float32 samples assumed, as with the canonical/TAAE formats). Converter/other: pass-through. */
static int32_t pull_input(struct audio_unit *u, uint32_t bus, uint32_t *flags, const struct audio_timestamp *ts, uint32_t frames, struct abl *io);
static int32_t unit_render(struct audio_unit *u, uint32_t *flags, const struct audio_timestamp *ts, uint32_t frames, struct abl *io) {
    if (u->kind != UK_MIXER) return pull_input(u, 0, flags, ts, frames, io);
    for (uint32_t i = 0; i < io->n && i < 8; i++) if (io->b[i].data) memset(io->b[i].data, 0, io->b[i].bytes);
    for (uint32_t bus = 0; bus < u->nbus; bus++) {
        struct unit_input *in = &u->in[bus];
        if (!in->enabled || (!in->cb.proc && !in->src)) continue;
        /* temp buffers mirroring io's layout */
        struct abl tmp; memcpy(&tmp, io, sizeof tmp);
        size_t total = 0; for (uint32_t i = 0; i < io->n && i < 8; i++) total += io->b[i].bytes;
        if (total > u->mixcap) { free(u->mixbuf); u->mixbuf = rt_xmalloc(total); u->mixcap = total; }
        size_t off = 0;
        for (uint32_t i = 0; i < io->n && i < 8; i++) { tmp.b[i].data = (uint8_t *)u->mixbuf + off; off += io->b[i].bytes; memset(tmp.b[i].data, 0, io->b[i].bytes); }
        uint32_t f = 0;
        if (pull_input(u, bus, &f, ts, frames, &tmp) != 0 || (f & 16)) continue;
        float vol = in->volume * u->master_volume;
        for (uint32_t i = 0; i < io->n && i < 8; i++) {
            float *d = io->b[i].data; const float *sp = tmp.b[i].data;
            for (uint32_t k = 0; k < io->b[i].bytes / 4; k++) d[k] += sp[k] * vol;
        }
    }
    return 0;
}
static int32_t pull_input(struct audio_unit *u, uint32_t bus, uint32_t *flags, const struct audio_timestamp *ts, uint32_t frames, struct abl *io) {
    struct unit_input *in = bus < MAX_BUS ? &u->in[bus] : NULL;
    if (in && in->src) return unit_render(in->src, flags, ts, frames, io);
    if (in && in->cb.proc)
        return ((int32_t (*)(void *, uint32_t *, const struct audio_timestamp *, uint32_t, uint32_t, struct abl *))in->cb.proc)(in->cb.refcon, flags, ts, bus, frames, io);
    for (uint32_t i = 0; i < io->n && i < 8; i++) if (io->b[i].data) memset(io->b[i].data, 0, io->b[i].bytes);
    if (flags) *flags |= 16;
    return 0;
}
int32_t iosrt_AudioUnitRender(struct audio_unit *u, uint32_t *flags, const void *ts, uint32_t bus, uint32_t frames, struct abl *io) {
    if (!valid(u)) return -50;
    if (u->kind == UK_IO && bus == 1) {   /* microphone: silence until input is implemented */
        for (uint32_t i = 0; i < io->n && i < 8; i++) if (io->b[i].data) memset(io->b[i].data, 0, io->b[i].bytes);
        return 0;
    }
    return unit_render(u, flags, ts, frames, io);
}

/* ------------------------------------------------------------ rendering */
uint32_t rt_audio_unit_channels(void *unit) { struct audio_unit *u = unit; return u->out_fmt.channels ? u->out_fmt.channels : 2; }
double rt_audio_unit_rate(void *unit) { struct audio_unit *u = unit; return u->out_fmt.rate > 0 ? u->out_fmt.rate : 44100; }
int rt_audio_render(void *unit, uint32_t frames, float *out, uint32_t out_ch) {
    struct audio_unit *u = unit;
    const struct asbd *f = &u->out_fmt;
    uint32_t ch = f->channels ? f->channels : 2;
    int nonint = (f->flags & kAudioFormatFlagIsNonInterleaved) != 0;
    uint32_t bpf = f->bytesPerFrame ? f->bytesPerFrame : 4;          /* per channel if non-interleaved */
    uint32_t nbuf = nonint ? ch : 1;
    if (nbuf > 8) nbuf = 8;
    size_t per = (size_t)frames * bpf;
    size_t need = per * nbuf;
    if (need > u->scratch_size) { free(u->scratch); u->scratch = rt_xmalloc(need); u->scratch_size = need; }
    memset(u->scratch, 0, need);
    struct abl list; memset(&list, 0, sizeof list);
    list.n = nbuf;
    for (uint32_t i = 0; i < nbuf; i++) { list.b[i].channels = nonint ? 1 : ch; list.b[i].bytes = (uint32_t)per; list.b[i].data = u->scratch + i * per; }
    struct audio_timestamp ts; memset(&ts, 0, sizeof ts);
    ts.sampleTime = u->sample_time; ts.rateScalar = 1;
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); ts.hostTime = (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
    ts.flags = 3;
    uint32_t aflags = 0;
    int32_t err = 0;
    err = pull_input(u, 0, &aflags, &ts, frames, &list);
    u->sample_time += frames;
    int isfloat = (f->flags & kAudioFormatFlagIsFloat) != 0;
    uint32_t frac = (f->flags >> 7) & 0x3f;   /* 8.24 fixed point canonical format */
    uint32_t bits = f->bits ? f->bits : 32;
    for (uint32_t fr = 0; fr < frames; fr++) {
        for (uint32_t c = 0; c < out_ch; c++) {
            uint32_t sc = c < ch ? c : ch - 1;
            const uint8_t *p = nonint ? (const uint8_t *)list.b[sc].data + (size_t)fr * bpf
                                      : (const uint8_t *)list.b[0].data + (size_t)fr * bpf + (size_t)sc * (bits / 8);
            float v;
            if (isfloat) memcpy(&v, p, 4);
            else if (bits == 16) { int16_t s; memcpy(&s, p, 2); v = s / 32768.0f; }
            else { int32_t s; memcpy(&s, p, 4); v = frac ? (float)s / (float)(1u << frac) : (float)s / 2147483648.0f; }
            if (aflags & 16) v = 0;     /* kAudioUnitRenderAction_OutputIsSilence */
            out[(size_t)fr * out_ch + c] = v;
        }
    }
    return err;
}

/* ------------------------------------------------- host backend: paced WAV */
struct wav_backend { FILE *f; uint32_t frames; uint32_t ch; uint32_t rate; };
static void wav_header(FILE *f, uint32_t ch, uint32_t rate, uint32_t frames) {
    uint32_t data = frames * ch * 2, riff = 36 + data, br = rate * ch * 2, fmtlen = 16;
    uint16_t pcm = 1, chn = (uint16_t)ch, ba = (uint16_t)(ch * 2), bps = 16;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmtlen, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&chn, 2, 1, f); fwrite(&rate, 4, 1, f);
    fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bps, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
}
static void *host_audio_thread(void *p) {
    struct audio_unit *u = p;
    const char *path = getenv("IOSRT_AUDIO_WAV");
    struct wav_backend wb = { 0 };
    wb.ch = rt_audio_unit_channels(u); if (wb.ch > 2) wb.ch = 2;
    wb.rate = (uint32_t)rt_audio_unit_rate(u);
    if (path) { wb.f = fopen(path, "wb"); if (wb.f) wav_header(wb.f, wb.ch, wb.rate, 0); }
    const uint32_t block = 512;
    float buf[512 * 2]; int16_t pcm[512 * 2];
    struct timespec start; clock_gettime(CLOCK_MONOTONIC, &start);
    uint64_t done = 0;
    while (u->running) {
        rt_audio_render(u, block, buf, wb.ch);
        if (wb.f) {
            for (uint32_t i = 0; i < block * wb.ch; i++) { float v = buf[i]; if (v > 1) v = 1; if (v < -1) v = -1; pcm[i] = (int16_t)lrintf(v * 32767.0f); }
            fwrite(pcm, 2, block * wb.ch, wb.f);
            wb.frames += block;
        }
        done += block;
        /* real-time pacing */
        double target = (double)done / wb.rate;
        struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
        double el = (double)(now.tv_sec - start.tv_sec) + (now.tv_nsec - start.tv_nsec) / 1e9;
        if (target > el) usleep((useconds_t)((target - el) * 1e6));
    }
    if (wb.f) { wav_header(wb.f, wb.ch, wb.rate, wb.frames); fclose(wb.f); }
    return NULL;
}
int32_t iosrt_AudioOutputUnitStart(struct audio_unit *u) {
    if (!valid(u)) return -50;
    if (u->running) return 0;
    u->running = 1;
    if (rt_plat->audio_start) return rt_plat->audio_start(u) ? -1 : 0;
    u->has_thread = pthread_create(&u->thread, NULL, host_audio_thread, u) == 0;
    return 0;
}
int32_t iosrt_AudioOutputUnitStop(struct audio_unit *u) {
    if (!valid(u) || !u->running) return 0;
    u->running = 0;
    if (rt_plat->audio_stop) rt_plat->audio_stop(u);
    else if (u->has_thread) { pthread_join(u->thread, NULL); u->has_thread = 0; }
    return 0;
}
int32_t iosrt_AudioOutputUnitPublish(const void *d, id name, uint32_t ver, void *unit) { (void)d; (void)name; (void)ver; (void)unit; return 0; }

/* ------------------------------------------------------------------ AUGraph */
struct augraph { struct audio_unit *nodes[64]; struct acdesc descs[64]; int n; int open, init, running; };
int32_t iosrt_NewAUGraph(struct augraph **g) { *g = rt_xcalloc(1, sizeof **g); return 0; }
int32_t iosrt_DisposeAUGraph(struct augraph *g) {
    if (!g) return -50;
    for (int i = 0; i < g->n; i++) if (g->nodes[i]) iosrt_AudioComponentInstanceDispose(g->nodes[i]);
    free(g); return 0;
}
static int32_t graph_instantiate(struct augraph *g, int i) {
    if (g->nodes[i]) return 0;
    void *c = iosrt_AudioComponentFindNext(NULL, &g->descs[i]);
    if (!c) { static struct acdesc any; any = g->descs[i]; c = &any; }
    return iosrt_AudioComponentInstanceNew(c, &g->nodes[i]);
}
int32_t iosrt_AUGraphAddNode(struct augraph *g, const struct acdesc *d, int32_t *node) {
    if (!g || g->n >= 64) return -50;
    g->descs[g->n] = *d; *node = g->n + 1; g->n++;
    if (g->open) graph_instantiate(g, g->n - 1);
    return 0;
}
int32_t iosrt_AUGraphRemoveNode(struct augraph *g, int32_t node) { (void)g; (void)node; return 0; }
int32_t iosrt_AUGraphOpen(struct augraph *g) { if (!g) return -50; g->open = 1; for (int i = 0; i < g->n; i++) graph_instantiate(g, i); return 0; }
int32_t iosrt_AUGraphClose(struct augraph *g) { if (g) g->open = 0; return 0; }
int32_t iosrt_AUGraphNodeInfo(struct augraph *g, int32_t node, struct acdesc *d, struct audio_unit **unit) {
    if (!g || node < 1 || node > g->n) return -10866;   /* kAUGraphErr_InvalidNode */
    if (d) *d = g->descs[node - 1];
    if (unit) { graph_instantiate(g, node - 1); *unit = g->nodes[node - 1]; }
    return 0;
}
int32_t iosrt_AUGraphConnectNodeInput(struct augraph *g, int32_t src, uint32_t sbus, int32_t dst, uint32_t dbus) {
    if (!g || src < 1 || src > g->n || dst < 1 || dst > g->n || dbus >= MAX_BUS) return -10866;
    graph_instantiate(g, src - 1); graph_instantiate(g, dst - 1);
    struct audio_unit *d = g->nodes[dst - 1];
    d->in[dbus].src = g->nodes[src - 1]; d->in[dbus].src_bus = sbus; d->in[dbus].cb.proc = NULL;
    return 0;
}
int32_t iosrt_AUGraphDisconnectNodeInput(struct augraph *g, int32_t dst, uint32_t dbus) {
    if (!g || dst < 1 || dst > g->n || dbus >= MAX_BUS) return -10866;
    if (g->nodes[dst - 1]) { g->nodes[dst - 1]->in[dbus].src = NULL; g->nodes[dst - 1]->in[dbus].cb.proc = NULL; }
    return 0;
}
int32_t iosrt_AUGraphSetNodeInputCallback(struct augraph *g, int32_t node, uint32_t bus, const struct render_cb *cb) {
    if (!g || node < 1 || node > g->n || bus >= MAX_BUS) return -10866;
    graph_instantiate(g, node - 1);
    struct audio_unit *u = g->nodes[node - 1];
    u->in[bus].cb = *cb; u->in[bus].src = NULL;
    if (bus == 0) u->cb = *cb;
    return 0;
}
int32_t iosrt_AUGraphClearConnections(struct augraph *g) {
    for (int i = 0; g && i < g->n; i++) if (g->nodes[i]) for (int b = 0; b < MAX_BUS; b++) { g->nodes[i]->in[b].src = NULL; g->nodes[i]->in[b].cb.proc = NULL; }
    return 0;
}
int32_t iosrt_AUGraphGetNodeInteractions(struct augraph *g, int32_t node, uint32_t *count, void *out) { (void)g; (void)node; (void)out; if (count) *count = 0; return 0; }
int32_t iosrt_AUGraphGetNodeCount(struct augraph *g, uint32_t *n) { if (n) *n = g ? (uint32_t)g->n : 0; return 0; }
int32_t iosrt_AUGraphGetIndNode(struct augraph *g, uint32_t i, int32_t *node) { if (!g || i >= (uint32_t)g->n) return -50; *node = (int32_t)i + 1; return 0; }
int32_t iosrt_AUGraphInitialize(struct augraph *g) { if (!g) return -50; if (!g->open) iosrt_AUGraphOpen(g); g->init = 1; return 0; }
int32_t iosrt_AUGraphUninitialize(struct augraph *g) { if (g) g->init = 0; return 0; }
int32_t iosrt_AUGraphIsInitialized(struct augraph *g, long *out) { if (out) *(char *)out = g && g->init; return 0; }
int32_t iosrt_AUGraphIsRunning(struct augraph *g, long *out) { if (out) *(char *)out = g && g->running; return 0; }
int32_t iosrt_AUGraphIsOpen(struct augraph *g, long *out) { if (out) *(char *)out = g && g->open; return 0; }
int32_t iosrt_AUGraphUpdate(struct augraph *g, long *done) { (void)g; if (done) *(char *)done = 1; return 0; }
int32_t iosrt_AUGraphStart(struct augraph *g) {
    if (!g) return -50;
    if (!g->init) iosrt_AUGraphInitialize(g);
    for (int i = 0; i < g->n; i++) if (g->nodes[i] && g->nodes[i]->kind == UK_IO) iosrt_AudioOutputUnitStart(g->nodes[i]);
    g->running = 1; return 0;
}
int32_t iosrt_AUGraphStop(struct augraph *g) {
    if (!g) return -50;
    for (int i = 0; i < g->n; i++) if (g->nodes[i] && g->nodes[i]->kind == UK_IO) iosrt_AudioOutputUnitStop(g->nodes[i]);
    g->running = 0; return 0;
}
int32_t iosrt_AudioComponentInstantiate(void *comp, uint32_t opts, void *block) {
    struct audio_unit *u = NULL; int32_t e = iosrt_AudioComponentInstanceNew(comp, &u); (void)opts;
    if (block) ((void (*)(void *, void *, int32_t))((struct rt_block *)block)->invoke)(block, u, e);
    return 0;
}
int32_t iosrt_MusicDeviceMIDIEvent(void *u, uint32_t st, uint32_t d1, uint32_t d2, uint32_t off) { (void)u; (void)st; (void)d1; (void)d2; (void)off; return 0; }

/* Legacy AudioSession C API and system sounds: accepted, no effect */
int32_t iosrt_AudioSessionInitialize(void *rl, void *mode, void *proc, void *ud) { (void)rl; (void)mode; (void)proc; (void)ud; return 0; }
int32_t iosrt_AudioSessionSetActive(long a) { (void)a; return 0; }
int32_t iosrt_AudioSessionSetProperty(uint32_t id, uint32_t sz, const void *d) { (void)id; (void)sz; (void)d; return 0; }
int32_t iosrt_AudioSessionGetProperty(uint32_t id, uint32_t *sz, void *d) {
    if (id == FOURCC('c', 'h', 's', 'r') && *sz >= 8) { double r = 48000; memcpy(d, &r, 8); return 0; }
    if (d && sz) memset(d, 0, *sz);
    return 0;
}
int32_t iosrt_AudioServicesCreateSystemSoundID(id url, uint32_t *sid) { (void)url; if (sid) *sid = 1; return 0; }
void iosrt_AudioServicesPlaySystemSound(uint32_t sid) { (void)sid; }

/* ---------------------------------------------------------- AVAudioSession */
static id g_session;
static id as_shared(Class c, SEL s) { (void)s; if (!g_session) g_session = rt_alloc_instance(c, 0); return g_session; }
static rt_bool as_yes_err(id self, SEL s, id a, id *err) { (void)self; (void)s; (void)a; if (err) *err = NULL; return 1; }
static rt_bool as_yes_err2(id self, SEL s, id a, long b, id *err) { (void)self; (void)s; (void)a; (void)b; if (err) *err = NULL; return 1; }
static rt_bool as_setActive(id self, SEL s, long a, id *err) { (void)self; (void)s; (void)a; if (err) *err = NULL; return 1; }
static rt_bool as_setDouble(id self, SEL s, double v, id *err) { (void)self; (void)s; (void)v; if (err) *err = NULL; return 1; }
static id as_emptyArr(id self, SEL s) { (void)self; (void)s; return objc_autorelease(rt_array_new(NULL, 0)); }
static long as_zero_l(id self, SEL s) { (void)self; (void)s; return 0; }
static double as_rate(id self, SEL s) { (void)self; (void)s; return 48000; }
static double as_iobuf(id self, SEL s) { (void)self; (void)s; return 256.0 / 48000; }
static double as_latency(id self, SEL s) { (void)self; (void)s; return 0.01; }
static rt_bool as_inputAvailable(id self, SEL s) { (void)self; (void)s; return 1; }
static long as_channels(id self, SEL s) { (void)self; (void)s; return 2; }
static id as_category(id self, SEL s) { (void)self; (void)s; return rt_str("AVAudioSessionCategoryPlayAndRecord"); }
static void as_requestRecordPermission(id self, SEL s, void *blk) { (void)self; (void)s; if (blk) ((void (*)(void *, long))((struct rt_block *)blk)->invoke)(blk, 1); }
RT_DEFINE_CLASS(AVAudioSessionRouteDescription);
RT_DEFINE_CLASS(AVAudioSessionPortDescription);
static id g_route;
static id port_type(id self, SEL s) { (void)s; return rt_str(*(long *)((uint8_t *)self + 8) ? "MicrophoneBuiltIn" : "Speaker"); }
static id port_name(id self, SEL s) { (void)s; return rt_str(*(long *)((uint8_t *)self + 8) ? "iPhone Microphone" : "Speaker"); }
static id route_ports(id self, SEL s, long input) {
    (void)self; (void)s;
    id p = rt_alloc_instance(RT_CLASS(AVAudioSessionPortDescription), 8); *(long *)((uint8_t *)p + 8) = input;
    id a = rt_array_new(&p, 1); objc_release(p); return objc_autorelease(a);
}
static id route_outputs(id self, SEL s) { return route_ports(self, s, 0); }
static id route_inputs(id self, SEL s) { return route_ports(self, s, 1); }
static id as_currentRoute(id self, SEL s) { (void)self; (void)s; if (!g_route) g_route = rt_alloc_instance(RT_CLASS(AVAudioSessionRouteDescription), 0); return g_route; }
static rt_bool as_setFloat(id self, SEL s, float v, id *err) { (void)self; (void)s; (void)v; if (err) *err = NULL; return 1; }
static float as_inputGain(id self, SEL s) { (void)self; (void)s; return 1.0f; }
static rt_bool as_setLong(id self, SEL s, long v, id *err) { (void)self; (void)s; (void)v; if (err) *err = NULL; return 1; }
static id as_availableInputs(id self, SEL s) { return route_ports(self, s, 1); }
static unsigned long as_recordPermission(id self, SEL s) { (void)self; (void)s; return FOURCC('g', 'r', 'n', 't'); }

id iosrt_AVAudioSessionCategoryPlayback, iosrt_AVAudioSessionCategoryPlayAndRecord, iosrt_AVAudioSessionCategoryRecord,
   iosrt_AVAudioSessionCategoryAmbient, iosrt_AVAudioSessionCategorySoloAmbient, iosrt_AVAudioSessionModeDefault,
   iosrt_AVAudioSessionModeMeasurement, iosrt_AVAudioSessionInterruptionNotification, iosrt_AVAudioSessionRouteChangeNotification,
   iosrt_AVAudioSessionMediaServicesWereResetNotification, iosrt_AVAudioSessionInterruptionTypeKey, iosrt_AVAudioSessionRouteChangeReasonKey;

void rt_audio_init(void) {
    rt_native_class(RT_CLASS(AVAudioSession), &iosrt_meta_AVAudioSession, "AVAudioSession", RT_CLASS(NSObject), 0);
    Class a = RT_CLASS(AVAudioSession);
    CM(a, "sharedInstance", as_shared);
    IM(a, "setCategory:error:", as_yes_err); IM(a, "setCategory:withOptions:error:", as_yes_err2);
    IM(a, "setMode:error:", as_yes_err); IM(a, "setActive:error:", as_setActive); IM(a, "setActive:withOptions:error:", as_yes_err2);
    IM(a, "setPreferredSampleRate:error:", as_setDouble); IM(a, "setPreferredIOBufferDuration:error:", as_setDouble);
    IM(a, "sampleRate", as_rate); IM(a, "preferredSampleRate", as_rate); IM(a, "IOBufferDuration", as_iobuf);
    IM(a, "outputLatency", as_latency); IM(a, "inputLatency", as_latency); IM(a, "isInputAvailable", as_inputAvailable);
    IM(a, "inputAvailable", as_inputAvailable); IM(a, "outputNumberOfChannels", as_channels); IM(a, "inputNumberOfChannels", as_channels);
    IM(a, "category", as_category); IM(a, "currentRoute", as_currentRoute); IM(a, "inputDataSources", as_emptyArr); IM(a, "outputDataSources", as_emptyArr); IM(a, "availableInputs", as_availableInputs);
    IM(a, "setInputGain:error:", as_setFloat); IM(a, "inputGain", as_inputGain); IM(a, "isInputGainSettable", as_inputAvailable);
    IM(a, "setPreferredInputNumberOfChannels:error:", as_setLong); IM(a, "setPreferredOutputNumberOfChannels:error:", as_setLong);
    IM(a, "overrideOutputAudioPort:error:", as_setLong); IM(a, "setPreferredInput:error:", as_yes_err);
    IM(a, "otherAudioPlaying", as_zero_l); IM(a, "isOtherAudioPlaying", as_zero_l); IM(a, "mode", as_category);
    rt_native_class(RT_CLASS(AVAudioSessionRouteDescription), &iosrt_meta_AVAudioSessionRouteDescription, "AVAudioSessionRouteDescription", RT_CLASS(NSObject), 0);
    rt_native_class(RT_CLASS(AVAudioSessionPortDescription), &iosrt_meta_AVAudioSessionPortDescription, "AVAudioSessionPortDescription", RT_CLASS(NSObject), 8);
    rt_native_method(RT_CLASS(AVAudioSessionRouteDescription), "outputs", (void *)route_outputs, "");
    rt_native_method(RT_CLASS(AVAudioSessionRouteDescription), "inputs", (void *)route_inputs, "");
    rt_native_method(RT_CLASS(AVAudioSessionPortDescription), "portType", (void *)port_type, "");
    rt_native_method(RT_CLASS(AVAudioSessionPortDescription), "portName", (void *)port_name, ""); IM(a, "requestRecordPermission:", as_requestRecordPermission); IM(a, "recordPermission", as_recordPermission);
#define S(v, t) v = rt_str_new(t)
    S(iosrt_AVAudioSessionCategoryPlayback, "AVAudioSessionCategoryPlayback");
    S(iosrt_AVAudioSessionCategoryPlayAndRecord, "AVAudioSessionCategoryPlayAndRecord");
    S(iosrt_AVAudioSessionCategoryRecord, "AVAudioSessionCategoryRecord");
    S(iosrt_AVAudioSessionCategoryAmbient, "AVAudioSessionCategoryAmbient");
    S(iosrt_AVAudioSessionCategorySoloAmbient, "AVAudioSessionCategorySoloAmbient");
    S(iosrt_AVAudioSessionModeDefault, "AVAudioSessionModeDefault");
    S(iosrt_AVAudioSessionModeMeasurement, "AVAudioSessionModeMeasurement");
    S(iosrt_AVAudioSessionInterruptionNotification, "AVAudioSessionInterruptionNotification");
    S(iosrt_AVAudioSessionRouteChangeNotification, "AVAudioSessionRouteChangeNotification");
    S(iosrt_AVAudioSessionMediaServicesWereResetNotification, "AVAudioSessionMediaServicesWereResetNotification");
    S(iosrt_AVAudioSessionInterruptionTypeKey, "AVAudioSessionInterruptionTypeKey");
    S(iosrt_AVAudioSessionRouteChangeReasonKey, "AVAudioSessionRouteChangeReasonKey");
}
