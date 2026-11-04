/*
 * Audio files and format conversion (AudioToolbox):
 *   - decoding: WAV (PCM, float), AIFF/AIFC (PCM, 'sowt', 'fl32'), CAF
 *     ('lpcm', 'ima4') natively; everything else (AAC/M4A, MP3, ...) through
 *     Android's MediaExtractor/MediaCodec (libmediandk, opened at run time);
 *     a file decodes once into 16-bit PCM in memory (rt_pcm_load);
 *   - ExtAudioFile: open/read/seek/tell with any linear PCM client format
 *     (sample format, channels, rate), create/write (PCM written as CAF,
 *     WAV or AIFF; compressed targets fall back to 16-bit PCM CAF);
 *   - AudioFile: open, properties, packet/byte reads of the decoded PCM;
 *   - AudioConverter: linear PCM to linear PCM, with channel mapping and
 *     sample-rate conversion (FillComplexBuffer pulls through the app's
 *     input callback), ConvertBuffer, ConvertComplexBuffer;
 *   - AudioFormatGetProperty: FormatInfo.
 */
#include "../internal.h"
#include "audio_internal.h"
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#define FCC(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

/* ------------------------------------------------------------------ helpers */
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[1] << 8 | p[0]); }
static uint64_t be64(const uint8_t *p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }
static double bedouble(const uint8_t *p) { uint64_t v = be64(p); double d; memcpy(&d, &v, 8); return d; }
static double ext80(const uint8_t *p) {   /* AIFF sample rate: 80-bit extended */
    int e = ((p[0] & 0x7f) << 8 | p[1]) - 16383; uint64_t m = be64(p + 2);
    return m ? ldexp((double)m, e - 63) : 0;
}
static int16_t clip16(float v) { v *= 32768.0f; if (v > 32767) v = 32767; if (v < -32768) v = -32768; return (int16_t)lrintf(v); }

/* one sample of a PCM buffer described by an ASBD, as float */
float rt_pcm_get(const struct asbd *f, const struct abl *l, uint32_t frame, uint32_t ch) {
    int nonint = (f->flags & 32) != 0;
    uint32_t bits = f->bits ? f->bits : 16, bps = bits / 8, chs = f->channels ? f->channels : 1;
    if (ch >= chs) ch = chs - 1;
    const uint8_t *p;
    if (nonint) { if (ch >= l->n || !l->b[ch].data) return 0; p = (const uint8_t *)l->b[ch].data + (size_t)frame * bps; }
    else { if (!l->n || !l->b[0].data) return 0; uint32_t bpf = f->bytesPerFrame ? f->bytesPerFrame : bps * chs; p = (const uint8_t *)l->b[0].data + (size_t)frame * bpf + (size_t)ch * bps; }
    int big = (f->flags & 2) != 0;
    if (f->flags & 1) {
        if (bits == 64) { uint64_t v; memcpy(&v, p, 8); if (big) v = __builtin_bswap64(v); double d; memcpy(&d, &v, 8); return (float)d; }
        uint32_t v; memcpy(&v, p, 4); if (big) v = __builtin_bswap32(v); float x; memcpy(&x, &v, 4); return x;
    }
    int32_t s;
    switch (bits) {
    case 8: s = (f->flags & 4) ? (int8_t)p[0] : (int)p[0] - 128; return s / 128.0f;
    case 16: { uint16_t v; memcpy(&v, p, 2); if (big) v = __builtin_bswap16(v); return (int16_t)v / 32768.0f; }
    case 24: { int32_t v = big ? (p[0] << 24 | p[1] << 16 | p[2] << 8) : (p[2] << 24 | p[1] << 16 | p[0] << 8); return v / 2147483648.0f; }
    default: { uint32_t v; memcpy(&v, p, 4); if (big) v = __builtin_bswap32(v); s = (int32_t)v;
        uint32_t frac = (f->flags >> 7) & 0x3f; return frac ? (float)s / (float)(1u << frac) : (float)s / 2147483648.0f; }
    }
}
void rt_pcm_put(const struct asbd *f, struct abl *l, uint32_t frame, uint32_t ch, float x) {
    int nonint = (f->flags & 32) != 0;
    uint32_t bits = f->bits ? f->bits : 16, bps = bits / 8, chs = f->channels ? f->channels : 1;
    if (ch >= chs) return;
    uint8_t *p;
    if (nonint) { if (ch >= l->n || !l->b[ch].data) return; p = (uint8_t *)l->b[ch].data + (size_t)frame * bps; }
    else { if (!l->n || !l->b[0].data) return; uint32_t bpf = f->bytesPerFrame ? f->bytesPerFrame : bps * chs; p = (uint8_t *)l->b[0].data + (size_t)frame * bpf + (size_t)ch * bps; }
    int big = (f->flags & 2) != 0;
    if (f->flags & 1) {
        if (bits == 64) { double d = x; uint64_t v; memcpy(&v, &d, 8); if (big) v = __builtin_bswap64(v); memcpy(p, &v, 8); return; }
        uint32_t v; memcpy(&v, &x, 4); if (big) v = __builtin_bswap32(v); memcpy(p, &v, 4); return;
    }
    if (x > 1) x = 1; if (x < -1) x = -1;
    switch (bits) {
    case 8: { int v = (int)lrintf(x * 127); p[0] = (f->flags & 4) ? (uint8_t)(int8_t)v : (uint8_t)(v + 128); return; }
    case 16: { uint16_t v = (uint16_t)clip16(x); if (big) v = __builtin_bswap16(v); memcpy(p, &v, 2); return; }
    case 24: { int32_t v = (int32_t)lrint(x * 8388607.0); if (big) { p[0] = (uint8_t)(v >> 16); p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)v; } else { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); } return; }
    default: {
        uint32_t frac = (f->flags >> 7) & 0x3f; int32_t s;
        if (frac) s = (int32_t)lrintf(x * (float)(1u << frac)); else s = (int32_t)llrint((double)x * 2147483647.0);
        uint32_t v = (uint32_t)s; if (big) v = __builtin_bswap32(v); memcpy(p, &v, 4); return; }
    }
}
static int is_lpcm(const struct asbd *f) { return f && f->fmt == FCC('l', 'p', 'c', 'm'); }
static void fill_pcm_asbd(struct asbd *f) {
    if (!is_lpcm(f)) return;
    if (!f->bits) f->bits = 16;
    if (!f->channels) f->channels = 1;
    uint32_t bps = f->bits / 8;
    if (!f->bytesPerFrame) f->bytesPerFrame = (f->flags & 32) ? bps : bps * f->channels;
    if (!f->framesPerPacket) f->framesPerPacket = 1;
    if (!f->bytesPerPacket) f->bytesPerPacket = f->bytesPerFrame;
}

/* ------------------------------------------------------------------ decoders */
static struct rt_pcm *pcm_new(double rate, uint32_t ch, uint64_t frames) {
    struct rt_pcm *p = rt_xcalloc(1, sizeof *p);
    p->rate = rate > 0 ? rate : 44100; p->ch = ch ? ch : 1; p->frames = frames;
    p->s = rt_xcalloc((size_t)frames * p->ch + 1, sizeof(int16_t));
    return p;
}
void rt_pcm_free(struct rt_pcm *p) { if (p) { free(p->s); free(p); } }
/* integer/float PCM bytes into the 16-bit store */
static void pcm_from_bytes(struct rt_pcm *p, const uint8_t *d, size_t n, const struct asbd *f) {
    struct abl l = { 1, 0, { { f->channels, (uint32_t)n, (void *)d } } };
    uint64_t frames = n / (f->bytesPerFrame ? f->bytesPerFrame : 2);
    if (frames > p->frames) frames = p->frames;
    for (uint64_t i = 0; i < frames; i++) for (uint32_t c = 0; c < p->ch; c++) p->s[i * p->ch + c] = clip16(rt_pcm_get(f, &l, (uint32_t)i, c));
}
static const int ima_steps[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499,
    2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767 };
static const int ima_index[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
/* Apple IMA4: per channel 34-byte blocks of 64 frames (2-byte header: predictor and step index) */
static struct rt_pcm *decode_ima4(const uint8_t *d, size_t n, double rate, uint32_t ch) {
    size_t pk = 34 * ch, packets = n / pk;
    struct rt_pcm *p = pcm_new(rate, ch, packets * 64);
    for (size_t k = 0; k < packets; k++) for (uint32_t c = 0; c < ch; c++) {
        const uint8_t *b = d + k * pk + c * 34;
        uint16_t h = be16(b); int pred = (int16_t)(h & 0xff80), idx = h & 0x7f; if (idx > 88) idx = 88;
        for (int i = 0; i < 64; i++) {
            int nib = (b[2 + i / 2] >> ((i & 1) * 4)) & 15, step = ima_steps[idx], diff = step >> 3;
            if (nib & 1) diff += step >> 2; if (nib & 2) diff += step >> 1; if (nib & 4) diff += step; if (nib & 8) diff = -diff;
            pred += diff; if (pred > 32767) pred = 32767; if (pred < -32768) pred = -32768;
            idx += ima_index[nib]; if (idx < 0) idx = 0; if (idx > 88) idx = 88;
            p->s[(k * 64 + i) * ch + c] = (int16_t)pred;
        }
    }
    return p;
}
static struct rt_pcm *decode_wav(const uint8_t *d, size_t n) {
    struct asbd f = { 0 }; const uint8_t *data = NULL; size_t dlen = 0; int have_fmt = 0;
    for (size_t i = 12; i + 8 <= n;) {
        uint32_t len = le32(d + i + 4); const uint8_t *c = d + i + 8;
        if (!memcmp(d + i, "fmt ", 4) && len >= 16 && i + 8 + 16 <= n) {
            uint16_t tag = le16(c); if (tag == 0xfffe && len >= 26) tag = le16(c + 24);
            f.channels = le16(c + 2); f.rate = le32(c + 4); f.bits = le16(c + 14); f.fmt = FCC('l', 'p', 'c', 'm');
            f.flags = tag == 3 ? 1 | 8 : (f.bits > 8 ? 4 | 8 : 8);
            f.bytesPerFrame = le16(c + 12);
            if (tag != 1 && tag != 3) { rt_warn("WAV: unsupported codec 0x%x", tag); return NULL; }
            have_fmt = 1;
        } else if (!memcmp(d + i, "data", 4)) { data = c; dlen = len; if (dlen > n - (size_t)(c - d)) dlen = n - (size_t)(c - d); }
        i += 8 + len + (len & 1);
    }
    if (!have_fmt || !data || !f.bytesPerFrame) return NULL;
    struct rt_pcm *p = pcm_new(f.rate, f.channels, dlen / f.bytesPerFrame);
    pcm_from_bytes(p, data, dlen, &f);
    p->src = f; p->ftype = FCC('W', 'A', 'V', 'E');
    return p;
}
static struct rt_pcm *decode_aiff(const uint8_t *d, size_t n) {
    int aifc = !memcmp(d + 8, "AIFC", 4);
    struct asbd f = { 0 }; const uint8_t *data = NULL; size_t dlen = 0; uint32_t comp = FCC('N', 'O', 'N', 'E');
    for (size_t i = 12; i + 8 <= n;) {
        uint32_t len = be32(d + i + 4); const uint8_t *c = d + i + 8;
        if (!memcmp(d + i, "COMM", 4) && len >= 18) {
            f.channels = be16(c); f.bits = be16(c + 6); f.rate = ext80(c + 8);
            if (aifc && len >= 22) comp = be32(c + 18);
        } else if (!memcmp(d + i, "SSND", 4) && len >= 8) { uint32_t off = be32(c); data = c + 8 + off; dlen = len - 8 - off; if (dlen > n - (size_t)(data - d)) dlen = n - (size_t)(data - d); }
        i += 8 + len + (len & 1);
    }
    if (!f.channels || !data) return NULL;
    f.fmt = FCC('l', 'p', 'c', 'm');
    if (comp == FCC('N', 'O', 'N', 'E') || comp == FCC('t', 'w', 'o', 's')) f.flags = 2 | 4 | 8;
    else if (comp == FCC('s', 'o', 'w', 't')) f.flags = 4 | 8;
    else if (comp == FCC('f', 'l', '3', '2') || comp == FCC('F', 'L', '3', '2')) { f.flags = 1 | 2 | 8; f.bits = 32; }
    else if (comp == FCC('i', 'm', 'a', '4')) return decode_ima4(data, dlen, f.rate, f.channels);
    else { rt_warn("AIFC: unsupported compression %.4s", (const char *)&comp); return NULL; }
    f.bytesPerFrame = (f.bits + 7) / 8 * f.channels;
    struct rt_pcm *p = pcm_new(f.rate, f.channels, dlen / f.bytesPerFrame);
    pcm_from_bytes(p, data, dlen, &f);
    p->src = f; p->ftype = FCC('A', 'I', 'F', 'F');
    return p;
}
static struct rt_pcm *decode_caf(const uint8_t *d, size_t n) {
    struct asbd f = { 0 }; const uint8_t *data = NULL; size_t dlen = 0;
    for (size_t i = 8; i + 12 <= n;) {
        int64_t len = (int64_t)be64(d + i + 4); const uint8_t *c = d + i + 12;
        if (len < 0 || (uint64_t)len > n - i - 12) len = (int64_t)(n - i - 12);   /* data chunk of unknown size runs to the end */
        if (!memcmp(d + i, "desc", 4) && len >= 32) {
            f.rate = bedouble(c); f.fmt = be32(c + 8); f.flags = be32(c + 12); f.bytesPerPacket = be32(c + 16); f.framesPerPacket = be32(c + 20); f.channels = be32(c + 24); f.bits = be32(c + 28);
        } else if (!memcmp(d + i, "data", 4) && len >= 4) { data = c + 4; dlen = (size_t)len - 4; }
        i += 12 + (size_t)len;
    }
    if (!data || !f.channels) return NULL;
    struct rt_pcm *p = NULL;
    if (f.fmt == FCC('l', 'p', 'c', 'm')) {
        /* CAF flags: bit 0 float, bit 1 little endian (kCAFLinearPCMFormatFlagIsLittleEndian) */
        struct asbd g = f; g.flags = (f.flags & 1 ? 1 : 4) | 8 | (f.flags & 2 ? 0 : 2);
        g.bytesPerFrame = f.bytesPerPacket ? f.bytesPerPacket : (f.bits / 8) * f.channels;
        p = pcm_new(f.rate, f.channels, dlen / g.bytesPerFrame);
        pcm_from_bytes(p, data, dlen, &g);
        f = g;
    } else if (f.fmt == FCC('i', 'm', 'a', '4')) p = decode_ima4(data, dlen, f.rate, f.channels);
    if (p) { p->src = f; p->ftype = FCC('c', 'a', 'f', 'f'); }
    return p;
}

/* Android MediaCodec for everything else */
struct mbinfo { int32_t offset, size; int64_t pts; uint32_t flags; };
static struct rt_pcm *decode_mediacodec(const char *path) {
    static void *lib; static int tried;
    if (!tried) { tried = 1; lib = dlopen("libmediandk.so", RTLD_NOW | RTLD_LOCAL); }
    if (!lib) return NULL;
    #define F(ret, name, args) ret (*name)args = (ret (*)args)dlsym(lib, #name)
    F(void *, AMediaExtractor_new, (void)); F(int, AMediaExtractor_delete, (void *)); F(int, AMediaExtractor_setDataSourceFd, (void *, int, int64_t, int64_t));
    F(size_t, AMediaExtractor_getTrackCount, (void *)); F(void *, AMediaExtractor_getTrackFormat, (void *, size_t)); F(int, AMediaExtractor_selectTrack, (void *, size_t));
    F(ssize_t, AMediaExtractor_readSampleData, (void *, uint8_t *, size_t)); F(int64_t, AMediaExtractor_getSampleTime, (void *)); F(int, AMediaExtractor_advance, (void *));
    F(int, AMediaFormat_getString, (void *, const char *, const char **)); F(int, AMediaFormat_getInt32, (void *, const char *, int32_t *));
    F(int, AMediaFormat_getInt64, (void *, const char *, int64_t *)); F(int, AMediaFormat_delete, (void *));
    F(void *, AMediaCodec_createDecoderByType, (const char *)); F(int, AMediaCodec_configure, (void *, void *, void *, void *, uint32_t));
    F(int, AMediaCodec_start, (void *)); F(int, AMediaCodec_stop, (void *)); F(int, AMediaCodec_delete, (void *));
    F(ssize_t, AMediaCodec_dequeueInputBuffer, (void *, int64_t)); F(uint8_t *, AMediaCodec_getInputBuffer, (void *, size_t, size_t *));
    F(int, AMediaCodec_queueInputBuffer, (void *, size_t, int64_t, size_t, uint64_t, uint32_t));
    F(ssize_t, AMediaCodec_dequeueOutputBuffer, (void *, struct mbinfo *, int64_t)); F(uint8_t *, AMediaCodec_getOutputBuffer, (void *, size_t, size_t *));
    F(int, AMediaCodec_releaseOutputBuffer, (void *, size_t, int)); F(void *, AMediaCodec_getOutputFormat, (void *));
    #undef F
    if (!AMediaExtractor_new || !AMediaCodec_createDecoderByType || !AMediaCodec_dequeueOutputBuffer) return NULL;
    int fd = open(path, O_RDONLY); if (fd < 0) return NULL;
    struct stat st; fstat(fd, &st);
    void *ex = AMediaExtractor_new();
    struct rt_pcm *p = NULL;
    if (AMediaExtractor_setDataSourceFd(ex, fd, 0, st.st_size) != 0) goto out;
    size_t nt = AMediaExtractor_getTrackCount(ex), track = (size_t)-1; void *fmt = NULL; const char *mime = NULL; char mimebuf[64];
    for (size_t i = 0; i < nt; i++) {
        void *f = AMediaExtractor_getTrackFormat(ex, i); const char *m = NULL;
        if (f && AMediaFormat_getString(f, "mime", &m) && m && !strncmp(m, "audio/", 6)) { track = i; fmt = f; snprintf(mimebuf, sizeof mimebuf, "%s", m); mime = mimebuf; break; }
        if (f) AMediaFormat_delete(f);
    }
    if (track == (size_t)-1) goto out;
    int32_t rate = 44100, ch = 2; int64_t dur = 0;
    AMediaFormat_getInt32(fmt, "sample-rate", &rate); AMediaFormat_getInt32(fmt, "channel-count", &ch); AMediaFormat_getInt64(fmt, "durationUs", &dur);
    AMediaExtractor_selectTrack(ex, track);
    void *codec = AMediaCodec_createDecoderByType(mime);
    if (!codec || AMediaCodec_configure(codec, fmt, NULL, NULL, 0) != 0 || AMediaCodec_start(codec) != 0) { if (codec) AMediaCodec_delete(codec); AMediaFormat_delete(fmt); goto out; }
    size_t cap = (size_t)((dur > 0 ? dur / 1e6 + 1 : 10) * rate * ch) + 4096, used = 0;
    int16_t *s = malloc(cap * 2); int in_done = 0, enc = 2, idle = 0;
    for (int guard = 0; guard < 2000000; guard++) {
        if (!in_done) {
            ssize_t ib = AMediaCodec_dequeueInputBuffer(codec, 5000);
            if (ib >= 0) {
                size_t bcap = 0; uint8_t *buf = AMediaCodec_getInputBuffer(codec, (size_t)ib, &bcap);
                ssize_t got = buf ? AMediaExtractor_readSampleData(ex, buf, bcap) : -1;
                if (got < 0) { AMediaCodec_queueInputBuffer(codec, (size_t)ib, 0, 0, 0, 4 /* END_OF_STREAM */); in_done = 1; }
                else { AMediaCodec_queueInputBuffer(codec, (size_t)ib, 0, (size_t)got, (uint64_t)AMediaExtractor_getSampleTime(ex), 0); AMediaExtractor_advance(ex); }
            }
        }
        struct mbinfo info; ssize_t ob = AMediaCodec_dequeueOutputBuffer(codec, &info, 5000);
        if (ob >= 0) {
            size_t osz = 0; uint8_t *o = AMediaCodec_getOutputBuffer(codec, (size_t)ob, &osz);
            if (o && info.size > 0) {
                size_t ns = enc == 4 ? (size_t)info.size / 4 : (size_t)info.size / 2;
                if (used + ns > cap) { cap = (used + ns) * 2; s = realloc(s, cap * 2); }
                if (enc == 4) { const float *fp = (const float *)(o + info.offset); for (size_t i = 0; i < ns; i++) s[used + i] = clip16(fp[i]); }
                else memcpy(s + used, o + info.offset, ns * 2);
                used += ns;
            }
            AMediaCodec_releaseOutputBuffer(codec, (size_t)ob, 0);
            if (info.flags & 4) break;
        } else if (ob == -2) {   /* output format changed */
            void *of = AMediaCodec_getOutputFormat(codec);
            if (of) { AMediaFormat_getInt32(of, "sample-rate", &rate); AMediaFormat_getInt32(of, "channel-count", &ch); int32_t e = 2; if (AMediaFormat_getInt32(of, "pcm-encoding", &e)) enc = e; AMediaFormat_delete(of); }
        } else if (in_done && ob == -1) { if (++idle > 400) break; }
    }
    AMediaCodec_stop(codec); AMediaCodec_delete(codec); AMediaFormat_delete(fmt);
    if (ch < 1) ch = 1;
    p = rt_xcalloc(1, sizeof *p); p->rate = rate; p->ch = (uint32_t)ch; p->frames = used / (size_t)ch; p->s = s;
    p->src.rate = rate; p->src.channels = (uint32_t)ch; p->src.framesPerPacket = 1024; p->ftype = FCC('m', '4', 'a', 'f');
    p->src.fmt = strstr(mime, "mpeg") ? FCC('.', 'm', 'p', '3') : strstr(mime, "mp4a") ? FCC('a', 'a', 'c', ' ') : FCC('?', '?', '?', '?');
    if (!strstr(mime, "mp4a") && !strstr(mime, "mpeg")) p->src.fmt = FCC('l', 'p', 'c', 'm');
out:
    AMediaExtractor_delete(ex); close(fd);
    return p;
}

struct rt_pcm *rt_pcm_load(const char *path) {
    int fd = open(path, O_RDONLY); if (fd < 0) return NULL;
    struct stat st; if (fstat(fd, &st) != 0 || st.st_size < 12 || st.st_size > (256 << 20)) { close(fd); return NULL; }
    uint8_t *d = malloc((size_t)st.st_size); ssize_t got = read(fd, d, (size_t)st.st_size); close(fd);
    struct rt_pcm *p = NULL;
    if (got == st.st_size) {
        size_t n = (size_t)got;
        if (!memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WAVE", 4)) p = decode_wav(d, n);
        else if (!memcmp(d, "FORM", 4) && (!memcmp(d + 8, "AIFF", 4) || !memcmp(d + 8, "AIFC", 4))) p = decode_aiff(d, n);
        else if (!memcmp(d, "caff", 4)) p = decode_caf(d, n);
    }
    free(d);
    if (!p) p = decode_mediacodec(path);
    if (p && !p->src.fmt) { p->src.rate = p->rate; p->src.channels = p->ch; p->src.fmt = FCC('l', 'p', 'c', 'm'); p->src.bits = 16; p->src.flags = 4 | 8; fill_pcm_asbd(&p->src); }
    if (p && !is_lpcm(&p->src)) { /* compressed source: its frames-per-packet stays, bytes are variable */ }
    else if (p) fill_pcm_asbd(&p->src);
    if (!p) rt_warn("audio: cannot decode %s", path);
    return p;
}
char *rt_url_path(id url) {
    if (!url) return NULL;
    id path = rt_responds(url, rt_sel("path")) ? RT_SEND0(id, url, "path") : url;
    return path ? strdup(rt_str_utf8(path)) : NULL;
}

/* ------------------------------------------------------------------ writing PCM files */
static void put_be32(FILE *f, uint32_t v) { uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v }; fwrite(b, 1, 4, f); }
static void put_be64(FILE *f, uint64_t v) { put_be32(f, (uint32_t)(v >> 32)); put_be32(f, (uint32_t)v); }
static void put_le32(FILE *f, uint32_t v) { uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; fwrite(b, 1, 4, f); }
static void put_le16(FILE *f, uint16_t v) { uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; fwrite(b, 1, 2, f); }
/* float frames -> file, in the file's PCM format (compressed targets: 16-bit PCM CAF) */
static int write_pcm_file(const char *path, uint32_t type, const struct asbd *ff, const float *x, uint64_t frames, uint32_t ch) {
    struct asbd f = *ff;
    if (!is_lpcm(&f)) { memset(&f, 0, sizeof f); f.rate = ff->rate > 0 ? ff->rate : 44100; f.fmt = FCC('l', 'p', 'c', 'm'); f.channels = ch; f.bits = 16; f.flags = 4 | 8; type = FCC('c', 'a', 'f', 'f');
                         rt_warn("ExtAudioFile: no %.4s encoder here; %s is written as 16-bit PCM CAF", (const char *)&ff->fmt, path); }
    f.flags &= ~32u; f.channels = ch; f.bytesPerFrame = 0; f.bytesPerPacket = 0;
    if (type == FCC('W', 'A', 'V', 'E')) f.flags &= ~2u;             /* WAV is little endian */
    if (type == FCC('A', 'I', 'F', 'F')) f.flags = (f.flags | 2) & ~1u;
    fill_pcm_asbd(&f);
    size_t dbytes = (size_t)frames * f.bytesPerFrame;
    uint8_t *buf = calloc(1, dbytes + 1);
    struct abl l = { 1, 0, { { ch, (uint32_t)dbytes, buf } } };
    for (uint64_t i = 0; i < frames; i++) for (uint32_t c = 0; c < ch; c++) rt_pcm_put(&f, &l, (uint32_t)i, c, x[i * ch + c]);
    FILE *o = fopen(path, "wb"); if (!o) { free(buf); return -1; }
    if (type == FCC('W', 'A', 'V', 'E')) {
        fwrite("RIFF", 1, 4, o); put_le32(o, (uint32_t)(36 + dbytes)); fwrite("WAVEfmt ", 1, 8, o); put_le32(o, 16);
        put_le16(o, f.flags & 1 ? 3 : 1); put_le16(o, (uint16_t)ch); put_le32(o, (uint32_t)f.rate); put_le32(o, (uint32_t)(f.rate * f.bytesPerFrame));
        put_le16(o, (uint16_t)f.bytesPerFrame); put_le16(o, (uint16_t)f.bits); fwrite("data", 1, 4, o); put_le32(o, (uint32_t)dbytes);
    } else if (type == FCC('A', 'I', 'F', 'F')) {
        fwrite("FORM", 1, 4, o); put_be32(o, (uint32_t)(4 + 26 + 16 + dbytes)); fwrite("AIFFCOMM", 1, 8, o); put_be32(o, 18);
        uint8_t c2[2] = { (uint8_t)(ch >> 8), (uint8_t)ch }; fwrite(c2, 1, 2, o); put_be32(o, (uint32_t)frames);
        uint8_t b2[2] = { 0, (uint8_t)f.bits }; fwrite(b2, 1, 2, o);
        /* 80-bit extended sample rate */
        double r = f.rate; int e = 0; double m = frexp(r, &e); uint64_t mant = (uint64_t)ldexp(m, 64); int ex = e - 1 + 16383;
        uint8_t ext[10] = { (uint8_t)(ex >> 8), (uint8_t)ex }; for (int i = 0; i < 8; i++) ext[2 + i] = (uint8_t)(mant >> (56 - 8 * i)); fwrite(ext, 1, 10, o);
        fwrite("SSND", 1, 4, o); put_be32(o, (uint32_t)(8 + dbytes)); put_be32(o, 0); put_be32(o, 0);
    } else {   /* CAF */
        fwrite("caff", 1, 4, o); uint8_t v[4] = { 0, 1, 0, 0 }; fwrite(v, 1, 4, o);
        fwrite("desc", 1, 4, o); put_be64(o, 32);
        uint64_t rb; memcpy(&rb, &f.rate, 8); put_be64(o, rb); put_be32(o, FCC('l', 'p', 'c', 'm'));
        put_be32(o, (f.flags & 1 ? 1u : 0u) | (f.flags & 2 ? 0u : 2u)); put_be32(o, f.bytesPerFrame); put_be32(o, 1); put_be32(o, ch); put_be32(o, f.bits);
        fwrite("data", 1, 4, o); put_be64(o, dbytes + 4); put_be32(o, 0);
    }
    fwrite(buf, 1, dbytes, o); fclose(o); free(buf);
    return 0;
}

/* ------------------------------------------------------------------ ExtAudioFile */
#define EAF_MAGIC 0x45414631
struct extfile {
    uint32_t magic; struct rt_pcm *pcm; struct asbd file_fmt, client; int has_client;
    double pos;                       /* read position in file frames */
    /* writing */
    char *wpath; uint32_t wtype; float *w; uint64_t wframes, wcap; uint32_t wch;
};
static int ef_valid(struct extfile *e) { return e && e->magic == EAF_MAGIC; }
int32_t iosrt_ExtAudioFileOpenURL(id url, struct extfile **out) {
    if (!out) return -50;
    char *path = rt_url_path(url); *out = NULL;
    if (!path) return -43;
    struct stat st; if (stat(path, &st) != 0) { free(path); return -43; }   /* fnfErr */
    struct rt_pcm *p = rt_pcm_load(path); free(path);
    if (!p) return (int32_t)FCC('t', 'y', 'p', '?');
    struct extfile *e = rt_xcalloc(1, sizeof *e); e->magic = EAF_MAGIC; e->pcm = p; e->file_fmt = p->src;
    *out = e;
    return 0;
}
int32_t iosrt_ExtAudioFileCreateWithURL(id url, uint32_t type, const struct asbd *fmt, const void *layout, uint32_t flags, struct extfile **out) {
    (void)layout; (void)flags;
    if (!out || !fmt) return -50;
    char *path = rt_url_path(url); if (!path) return -50;
    struct extfile *e = rt_xcalloc(1, sizeof *e); e->magic = EAF_MAGIC; e->file_fmt = *fmt; fill_pcm_asbd(&e->file_fmt);
    e->wpath = path; e->wtype = type; e->wch = fmt->channels ? fmt->channels : 2;
    if (is_lpcm(fmt)) { e->client = e->file_fmt; e->has_client = 1; }
    *out = e;
    return 0;
}
int32_t iosrt_ExtAudioFileDispose(struct extfile *e) {
    if (!ef_valid(e)) return -50;
    if (e->wpath) { write_pcm_file(e->wpath, e->wtype, &e->file_fmt, e->w, e->wframes, e->wch); free(e->wpath); free(e->w); }
    rt_pcm_free(e->pcm);
    e->magic = 0; free(e);
    return 0;
}
static void layout_for(uint32_t ch, uint32_t *out3) { out3[0] = ch == 1 ? (100u << 16 | 1) : (101u << 16 | 2); out3[1] = 0; out3[2] = 0; }
int32_t iosrt_ExtAudioFileGetProperty(struct extfile *e, uint32_t prop, uint32_t *size, void *data) {
    if (!ef_valid(e) || !size) return -50;
    switch (prop) {
    case FCC('f', 'f', 'm', 't'): if (*size < sizeof(struct asbd)) return -50; memcpy(data, &e->file_fmt, sizeof e->file_fmt); *size = sizeof e->file_fmt; return 0;
    case FCC('c', 'f', 'm', 't'): if (*size < sizeof(struct asbd)) return -50; memcpy(data, e->has_client ? &e->client : &e->file_fmt, sizeof(struct asbd)); *size = sizeof(struct asbd); return 0;
    case FCC('#', 'f', 'r', 'm'): { int64_t n = e->pcm ? (int64_t)e->pcm->frames : (int64_t)e->wframes; if (*size < 8) return -50; memcpy(data, &n, 8); *size = 8; return 0; }
    case FCC('f', 'c', 'l', 'o'): case FCC('c', 'c', 'l', 'o'): { if (*size < 12) return -50; layout_for(e->pcm ? e->pcm->ch : e->wch, data); *size = 12; return 0; }
    case FCC('a', 'f', 'i', 'l'): case FCC('a', 'c', 'n', 'v'): if (*size >= 8) { memset(data, 0, 8); *size = 8; } return 0;
    case FCC('i', 'o', 'b', 's'): if (*size >= 4) { *(uint32_t *)data = 32768; *size = 4; } return 0;
    case FCC('f', 'm', 'p', 's'): case FCC('c', 'm', 'p', 's'): if (*size >= 4) { *(uint32_t *)data = e->file_fmt.bytesPerPacket ? e->file_fmt.bytesPerPacket : 4096; *size = 4; } return 0;
    }
    return (int32_t)FCC('p', 'r', 'o', 'p');
}
int32_t iosrt_ExtAudioFileGetPropertyInfo(struct extfile *e, uint32_t prop, uint32_t *size, unsigned char *writable) {
    if (!ef_valid(e)) return -50;
    uint32_t s = prop == FCC('f', 'f', 'm', 't') || prop == FCC('c', 'f', 'm', 't') ? (uint32_t)sizeof(struct asbd) : prop == FCC('#', 'f', 'r', 'm') ? 8 : prop == FCC('f', 'c', 'l', 'o') || prop == FCC('c', 'c', 'l', 'o') ? 12 : 8;
    if (size) *size = s;
    if (writable) *writable = prop == FCC('c', 'f', 'm', 't') || prop == FCC('c', 'c', 'l', 'o');
    return 0;
}
int32_t iosrt_ExtAudioFileSetProperty(struct extfile *e, uint32_t prop, uint32_t size, const void *data) {
    if (!ef_valid(e)) return -50;
    if (prop == FCC('c', 'f', 'm', 't')) {
        if (size < sizeof(struct asbd) || !is_lpcm(data)) return (int32_t)FCC('f', 'm', 't', '?');
        memcpy(&e->client, data, sizeof e->client); fill_pcm_asbd(&e->client); e->has_client = 1;
        return 0;
    }
    return 0;   /* channel layouts, converter settings: accepted */
}
int32_t iosrt_ExtAudioFileRead(struct extfile *e, uint32_t *frames, struct abl *io) {
    if (!ef_valid(e) || !frames || !io) return -50;
    struct rt_pcm *p = e->pcm; if (!p) { *frames = 0; return 0; }
    const struct asbd *cf = e->has_client ? &e->client : &p->src;
    if (!is_lpcm(cf)) { *frames = 0; return (int32_t)FCC('f', 'm', 't', '?'); }
    double crate = cf->rate > 0 ? cf->rate : p->rate, step = p->rate / crate;
    uint32_t cch = cf->channels ? cf->channels : p->ch, want = *frames, done = 0;
    /* the buffer list's capacity limits the frames */
    uint32_t bpf = (cf->flags & 32) ? cf->bits / 8 : cf->bytesPerFrame;
    if (bpf && io->n && io->b[0].bytes / bpf < want) want = io->b[0].bytes / bpf;
    for (; done < want; done++) {
        double pos = e->pos; uint64_t i0 = (uint64_t)pos;
        if (i0 >= p->frames) break;
        double fr = pos - (double)i0; uint64_t i1 = i0 + 1 < p->frames ? i0 + 1 : i0;
        for (uint32_t c = 0; c < cch; c++) {
            uint32_t sc = p->ch == 1 ? 0 : c < p->ch ? c : p->ch - 1;
            float a = p->s[i0 * p->ch + sc] / 32768.0f, b = p->s[i1 * p->ch + sc] / 32768.0f;
            if (p->ch == 2 && cch == 1) { a = (p->s[i0 * 2] + p->s[i0 * 2 + 1]) / 65536.0f; b = (p->s[i1 * 2] + p->s[i1 * 2 + 1]) / 65536.0f; }
            rt_pcm_put(cf, io, done, c, (float)(a + (b - a) * fr));
        }
        e->pos += step;
    }
    *frames = done;
    for (uint32_t i = 0; i < io->n && i < 8; i++) io->b[i].bytes = done * bpf;
    return 0;
}
int32_t iosrt_ExtAudioFileSeek(struct extfile *e, int64_t frame) { if (!ef_valid(e)) return -50; e->pos = frame < 0 ? 0 : (double)frame; return 0; }
int32_t iosrt_ExtAudioFileTell(struct extfile *e, int64_t *frame) { if (!ef_valid(e) || !frame) return -50; *frame = (int64_t)e->pos; return 0; }
int32_t iosrt_ExtAudioFileWrite(struct extfile *e, uint32_t frames, const struct abl *io) {
    if (!ef_valid(e) || !e->wpath) return -50;
    const struct asbd *cf = e->has_client ? &e->client : &e->file_fmt;
    if (!is_lpcm(cf)) return (int32_t)FCC('f', 'm', 't', '?');
    double step = (cf->rate > 0 && e->file_fmt.rate > 0) ? cf->rate / e->file_fmt.rate : 1;
    uint64_t outn = (uint64_t)(frames / step + 0.5);
    if (e->wframes + outn > e->wcap) { e->wcap = (e->wframes + outn) * 2 + 4096; e->w = realloc(e->w, e->wcap * e->wch * sizeof(float)); }
    for (uint64_t i = 0; i < outn; i++) {
        uint32_t src = (uint32_t)(i * step); if (src >= frames) src = frames - 1;
        for (uint32_t c = 0; c < e->wch; c++) e->w[(e->wframes + i) * e->wch + c] = rt_pcm_get(cf, io, src, c);
    }
    e->wframes += outn;
    return 0;
}
int32_t iosrt_ExtAudioFileWriteAsync(struct extfile *e, uint32_t frames, const struct abl *io) { return io ? iosrt_ExtAudioFileWrite(e, frames, io) : 0; }

/* ------------------------------------------------------------------ AudioFile (reads of the decoded PCM) */
#define AF_MAGIC 0x41466c31
struct afile { uint32_t magic; struct rt_pcm *pcm; struct asbd fmt; };
static int af_valid(struct afile *a) { return a && a->magic == AF_MAGIC; }
int32_t iosrt_AudioFileOpenURL(id url, int8_t perm, uint32_t hint, struct afile **out) {
    (void)perm; (void)hint;
    if (!out) return -50;
    char *path = rt_url_path(url); *out = NULL;
    if (!path) return -43;
    struct stat st; if (stat(path, &st) != 0) { free(path); return -43; }
    struct rt_pcm *p = rt_pcm_load(path); free(path);
    if (!p) return (int32_t)FCC('t', 'y', 'p', '?');
    struct afile *a = rt_xcalloc(1, sizeof *a); a->magic = AF_MAGIC; a->pcm = p;
    /* the app sees the decoded data: 16-bit native-endian interleaved PCM */
    a->fmt.rate = p->rate; a->fmt.fmt = FCC('l', 'p', 'c', 'm'); a->fmt.flags = 4 | 8; a->fmt.channels = p->ch; a->fmt.bits = 16; fill_pcm_asbd(&a->fmt);
    *out = a;
    return 0;
}
int32_t iosrt_AudioFileClose(struct afile *a) { if (!af_valid(a)) return -50; rt_pcm_free(a->pcm); a->magic = 0; free(a); return 0; }
int32_t iosrt_AudioFileGetProperty(struct afile *a, uint32_t prop, uint32_t *size, void *data) {
    if (!af_valid(a) || !size) return -50;
    uint64_t frames = a->pcm->frames, bytes = frames * a->fmt.bytesPerFrame;
    switch (prop) {
    case FCC('d', 'f', 'm', 't'): if (*size < sizeof(struct asbd)) return -50; memcpy(data, &a->fmt, sizeof a->fmt); *size = sizeof a->fmt; return 0;
    case FCC('p', 'c', 'n', 't'): if (*size < 8) return -50; memcpy(data, &frames, 8); *size = 8; return 0;          /* AudioDataPacketCount */
    case FCC('b', 'c', 'n', 't'): if (*size < 8) return -50; memcpy(data, &bytes, 8); *size = 8; return 0;           /* AudioDataByteCount */
    case FCC('e', 'd', 'u', 'r'): { double d = frames / a->pcm->rate; if (*size < 8) return -50; memcpy(data, &d, 8); *size = 8; return 0; }
    case FCC('p', 's', 'z', 'e'): case FCC('p', 's', 'z', 'u'): if (*size < 4) return -50; *(uint32_t *)data = a->fmt.bytesPerPacket; *size = 4; return 0;
    case FCC('f', 'f', 'm', 't'): if (*size < 4) return -50; *(uint32_t *)data = a->pcm->ftype; *size = 4; return 0;
    case FCC('d', 'o', 'f', 'f'): if (*size < 8) return -50; memset(data, 0, 8); *size = 8; return 0;
    case FCC('c', 'm', 'a', 'p'): case FCC('c', 'l', 'a', 'y'): if (*size < 12) return -50; layout_for(a->pcm->ch, data); *size = 12; return 0;
    case FCC('m', 'g', 'i', 'c'): *size = 0; return 0;   /* no magic cookie for PCM */
    }
    return (int32_t)FCC('p', 't', 'y', '?');
}
int32_t iosrt_AudioFileGetPropertyInfo(struct afile *a, uint32_t prop, uint32_t *size, uint32_t *writable) {
    if (!af_valid(a)) return -50;
    if (size) *size = prop == FCC('d', 'f', 'm', 't') ? (uint32_t)sizeof(struct asbd) : prop == FCC('m', 'g', 'i', 'c') ? 0 : prop == FCC('c', 'm', 'a', 'p') ? 12 : 8;
    if (writable) *writable = 0;
    return 0;
}
int32_t iosrt_AudioFileReadBytes(struct afile *a, unsigned char cache, int64_t start, uint32_t *n, void *out) {
    (void)cache;
    if (!af_valid(a) || !n) return -50;
    uint64_t total = a->pcm->frames * a->fmt.bytesPerFrame;
    if (start < 0 || (uint64_t)start >= total) { *n = 0; return -39; }   /* eofErr */
    uint64_t left = total - (uint64_t)start; if (*n > left) *n = (uint32_t)left;
    memcpy(out, (uint8_t *)a->pcm->s + start, *n);
    return 0;
}
int32_t iosrt_AudioFileReadPacketData(struct afile *a, unsigned char cache, uint32_t *nbytes, void *pds, int64_t start, uint32_t *npk, void *out) {
    (void)cache; (void)pds;
    if (!af_valid(a) || !npk) return -50;
    uint64_t frames = a->pcm->frames;
    if (start < 0 || (uint64_t)start >= frames) { *npk = 0; if (nbytes) *nbytes = 0; return 0; }
    uint64_t left = frames - (uint64_t)start; uint32_t k = *npk;
    if (k > left) k = (uint32_t)left;
    if (nbytes && *nbytes / a->fmt.bytesPerFrame < k) k = *nbytes / a->fmt.bytesPerFrame;
    memcpy(out, a->pcm->s + (size_t)start * a->pcm->ch, (size_t)k * a->fmt.bytesPerFrame);
    *npk = k; if (nbytes) *nbytes = k * a->fmt.bytesPerFrame;
    return 0;
}
int32_t iosrt_AudioFileReadPackets(struct afile *a, unsigned char cache, uint32_t *nbytes, void *pds, int64_t start, uint32_t *npk, void *out) {
    uint32_t big = 0xffffffffu; if (nbytes && *nbytes == 0) nbytes = &big;
    return iosrt_AudioFileReadPacketData(a, cache, nbytes == &big ? NULL : nbytes, pds, start, npk, out);
}

/* ------------------------------------------------------------------ AudioConverter (PCM <-> PCM) */
#define AC_MAGIC 0x41436e76
struct aconv { uint32_t magic; struct asbd in, out; float *buf; size_t nbuf, capbuf; double pos; };
static int ac_valid(struct aconv *c) { return c && c->magic == AC_MAGIC; }
int32_t iosrt_AudioConverterNew(const struct asbd *in, const struct asbd *out, struct aconv **res) {
    if (!in || !out || !res) return -50;
    if (!is_lpcm(in) || !is_lpcm(out)) { *res = NULL; rt_warn("AudioConverter: only linear PCM is converted here (%.4s -> %.4s)", (const char *)&in->fmt, (const char *)&out->fmt); return (int32_t)FCC('f', 'm', 't', '?'); }
    struct aconv *c = rt_xcalloc(1, sizeof *c); c->magic = AC_MAGIC; c->in = *in; c->out = *out; fill_pcm_asbd(&c->in); fill_pcm_asbd(&c->out);
    *res = c;
    return 0;
}
int32_t iosrt_AudioConverterNewSpecific(const struct asbd *in, const struct asbd *out, uint32_t n, const void *desc, struct aconv **res) { (void)n; (void)desc; return iosrt_AudioConverterNew(in, out, res); }
int32_t iosrt_AudioConverterDispose(struct aconv *c) { if (!ac_valid(c)) return -50; c->magic = 0; free(c->buf); free(c); return 0; }
int32_t iosrt_AudioConverterReset(struct aconv *c) { if (!ac_valid(c)) return -50; c->nbuf = 0; c->pos = 0; return 0; }
int32_t iosrt_AudioConverterGetProperty(struct aconv *c, uint32_t prop, uint32_t *size, void *data) {
    if (!ac_valid(c) || !size) return -50;
    switch (prop) {
    case FCC('a', 'c', 'i', 'd'): if (*size < sizeof(struct asbd)) return -50; memcpy(data, &c->in, sizeof c->in); *size = sizeof c->in; return 0;
    case FCC('a', 'c', 'o', 'd'): if (*size < sizeof(struct asbd)) return -50; memcpy(data, &c->out, sizeof c->out); *size = sizeof c->out; return 0;
    case FCC('x', 'o', 'p', 's'): if (*size < 4) return -50; *(uint32_t *)data = c->out.bytesPerPacket; *size = 4; return 0;
    case FCC('x', 'i', 'p', 's'): if (*size < 4) return -50; *(uint32_t *)data = c->in.bytesPerPacket; *size = 4; return 0;
    case FCC('m', 'i', 'b', 's'): case FCC('m', 'o', 'b', 's'): if (*size < 4) return -50; *(uint32_t *)data = 4; *size = 4; return 0;
    }
    if (*size >= 4) { memset(data, 0, *size < 8 ? *size : 8); }
    return 0;
}
int32_t iosrt_AudioConverterGetPropertyInfo(struct aconv *c, uint32_t prop, uint32_t *size, unsigned char *writable) {
    if (!ac_valid(c)) return -50;
    if (size) *size = prop == FCC('a', 'c', 'i', 'd') || prop == FCC('a', 'c', 'o', 'd') ? (uint32_t)sizeof(struct asbd) : 4;
    if (writable) *writable = 1;
    return 0;
}
int32_t iosrt_AudioConverterSetProperty(struct aconv *c, uint32_t prop, uint32_t size, const void *data) {
    if (!ac_valid(c)) return -50;
    if (prop == FCC('a', 'c', 'i', 'd') && size >= sizeof(struct asbd)) { memcpy(&c->in, data, sizeof c->in); fill_pcm_asbd(&c->in); }
    if (prop == FCC('a', 'c', 'o', 'd') && size >= sizeof(struct asbd)) { memcpy(&c->out, data, sizeof c->out); fill_pcm_asbd(&c->out); }
    return 0;
}
/* frames from an input buffer list into the converter's float buffer */
static void ac_append(struct aconv *c, const struct abl *l, uint32_t frames) {
    uint32_t ch = c->in.channels ? c->in.channels : 1;
    if (c->nbuf + frames > c->capbuf) { c->capbuf = (c->nbuf + frames) * 2 + 1024; c->buf = realloc(c->buf, c->capbuf * ch * sizeof(float)); }
    for (uint32_t i = 0; i < frames; i++) for (uint32_t k = 0; k < ch; k++) c->buf[(c->nbuf + i) * ch + k] = rt_pcm_get(&c->in, l, i, k);
    c->nbuf += frames;
}
/* converts from the float buffer into out, resampling; returns output frames */
static uint32_t ac_emit(struct aconv *c, struct abl *out, uint32_t want, int final) {
    uint32_t ich = c->in.channels ? c->in.channels : 1, och = c->out.channels ? c->out.channels : 1;
    double step = (c->in.rate > 0 && c->out.rate > 0) ? c->in.rate / c->out.rate : 1;
    uint32_t n = 0;
    while (n < want) {
        size_t i0 = (size_t)c->pos;
        if (i0 + (step == 1 ? 0 : 1) >= c->nbuf && !(final && i0 < c->nbuf)) break;
        size_t i1 = i0 + 1 < c->nbuf ? i0 + 1 : i0; double fr = c->pos - (double)i0;
        for (uint32_t k = 0; k < och; k++) {
            float a, b;
            if (ich == 2 && och == 1) { a = (c->buf[i0 * 2] + c->buf[i0 * 2 + 1]) * 0.5f; b = (c->buf[i1 * 2] + c->buf[i1 * 2 + 1]) * 0.5f; }
            else { uint32_t sk = k < ich ? k : ich - 1; a = c->buf[i0 * ich + sk]; b = c->buf[i1 * ich + sk]; }
            rt_pcm_put(&c->out, out, n, k, (float)(a + (b - a) * fr));
        }
        n++; c->pos += step;
    }
    /* drop consumed input */
    size_t used = (size_t)c->pos;
    if (used) { if (used > c->nbuf) used = c->nbuf; memmove(c->buf, c->buf + used * ich, (c->nbuf - used) * ich * sizeof(float)); c->nbuf -= used; c->pos -= (double)used; }
    return n;
}
typedef int32_t (*ac_input_proc)(struct aconv *, uint32_t *, struct abl *, void **, void *);
int32_t iosrt_AudioConverterFillComplexBuffer(struct aconv *c, ac_input_proc proc, void *user, uint32_t *packets, struct abl *out, void *pds) {
    (void)pds;
    if (!ac_valid(c) || !packets || !out) return -50;
    uint32_t want = *packets, got = 0; int32_t err = 0; int ended = 0;
    double step = (c->in.rate > 0 && c->out.rate > 0) ? c->in.rate / c->out.rate : 1;
    for (int guard = 0; got < want && guard < 1000; guard++) {
        struct abl part = *out;
        uint32_t obpf = (c->out.flags & 32) ? c->out.bits / 8 : c->out.bytesPerFrame;
        for (uint32_t i = 0; i < part.n && i < 8; i++) part.b[i].data = (uint8_t *)out->b[i].data + (size_t)got * obpf;
        got += ac_emit(c, &part, want - got, ended);
        if (got >= want || ended) break;
        uint32_t need = (uint32_t)((want - got) * step) + 2;
        struct abl in; memset(&in, 0, sizeof in);
        in.n = (c->in.flags & 32) ? (c->in.channels ? c->in.channels : 1) : 1; if (in.n > 8) in.n = 8;
        void *pd = NULL; uint32_t np = need;
        err = proc ? proc(c, &np, &in, &pd, user) : -50;
        if (err || np == 0) { ended = 1; if (err && !got) break; continue; }
        ac_append(c, &in, np);
    }
    *packets = got;
    uint32_t obpf = (c->out.flags & 32) ? c->out.bits / 8 : c->out.bytesPerFrame;
    for (uint32_t i = 0; i < out->n && i < 8; i++) out->b[i].bytes = got * obpf;
    return got ? 0 : err;
}
int32_t iosrt_AudioConverterConvertComplexBuffer(struct aconv *c, uint32_t frames, const struct abl *in, struct abl *out) {
    if (!ac_valid(c) || !in || !out) return -50;
    uint32_t och = c->out.channels ? c->out.channels : 1, ich = c->in.channels ? c->in.channels : 1;
    for (uint32_t i = 0; i < frames; i++) for (uint32_t k = 0; k < och; k++) {
        float v = ich == 2 && och == 1 ? (rt_pcm_get(&c->in, in, i, 0) + rt_pcm_get(&c->in, in, i, 1)) * 0.5f : rt_pcm_get(&c->in, in, i, k < ich ? k : ich - 1);
        rt_pcm_put(&c->out, out, i, k, v);
    }
    return 0;
}
int32_t iosrt_AudioConverterConvertBuffer(struct aconv *c, uint32_t insize, const void *in, uint32_t *outsize, void *out) {
    if (!ac_valid(c) || !outsize) return -50;
    uint32_t ibpf = c->in.bytesPerFrame ? c->in.bytesPerFrame : 2, obpf = c->out.bytesPerFrame ? c->out.bytesPerFrame : 2;
    uint32_t frames = insize / ibpf; if (frames > *outsize / obpf) frames = *outsize / obpf;
    struct abl li = { 1, 0, { { c->in.channels, insize, (void *)in } } }, lo = { 1, 0, { { c->out.channels, *outsize, out } } };
    iosrt_AudioConverterConvertComplexBuffer(c, frames, &li, &lo);
    *outsize = frames * obpf;
    return 0;
}

/* ------------------------------------------------------------------ AudioFormat */
int32_t iosrt_AudioFormatGetPropertyInfo(uint32_t prop, uint32_t specsize, const void *spec, uint32_t *size) {
    (void)specsize; (void)spec;
    if (!size) return -50;
    if (prop == FCC('f', 'm', 't', 'i')) { *size = sizeof(struct asbd); return 0; }
    if (prop == FCC('f', 'n', 'a', 'm') || prop == FCC('l', 'n', 'a', 'm')) { *size = 8; return 0; }
    *size = 0; return (int32_t)FCC('p', 'r', 'o', 'p');
}
int32_t iosrt_AudioFormatGetProperty(uint32_t prop, uint32_t specsize, const void *spec, uint32_t *size, void *data) {
    if (prop == FCC('f', 'm', 't', 'i') && data && size && *size >= sizeof(struct asbd)) {   /* FormatInfo: complete an ASBD */
        struct asbd *f = data;
        if (spec && specsize >= sizeof(struct asbd) && spec != data) memcpy(f, spec, sizeof *f);
        if (is_lpcm(f)) fill_pcm_asbd(f);
        else if (f->fmt == FCC('a', 'a', 'c', ' ')) { if (!f->framesPerPacket) f->framesPerPacket = 1024; }
        else if (f->fmt == FCC('i', 'm', 'a', '4')) { f->framesPerPacket = 64; f->bytesPerPacket = 34 * (f->channels ? f->channels : 1); }
        *size = sizeof *f;
        return 0;
    }
    return (int32_t)FCC('p', 'r', 'o', 'p');
}
