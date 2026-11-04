/*
 * Playing audio files: AVAudioPlayer (AVFoundation) and system sounds
 * (AudioServices). A file decodes once (file.c); each playing sound is a
 * "voice" on its own internal RemoteIO unit (AAudio on Android, the paced
 * writer on the host) that renders the PCM with volume, pan and looping.
 * End-of-sound callbacks run on the main thread.
 */
#include "../internal.h"
#include "audio_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#define IM(c, s, f) rt_native_method(c, s, (void *)(f), "")
#define CM(c, s, f) rt_native_cmethod(c, s, (void *)(f), "")
#define FCC(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
RT_DECLARE_CLASS(NSObject);
RT_DEFINE_CLASS(AVAudioPlayer);
void rt_after(double delay, void (*fn)(void *), void *ctx);

/* ------------------------------------------------------------------ voices */
struct voice {
    struct rt_pcm *pcm; void *unit;
    double pos; float vol, pan; long loops;
    volatile int playing, ended; int started;
    float peak[2], avg[2];
    void (*on_end)(struct voice *); void *owner;
};
static void voice_finished(void *p) {
    struct voice *v = p;
    if (v->unit && v->started) { iosrt_AudioOutputUnitStop(v->unit); v->started = 0; }
    if (v->on_end) v->on_end(v);
}
static int32_t voice_render(void *refcon, uint32_t *flags, const struct audio_timestamp *ts, uint32_t bus, uint32_t frames, struct abl *io) {
    (void)ts; (void)bus;
    struct voice *v = refcon; float *out = io->n ? io->b[0].data : NULL;
    if (!out) return 0;
    struct rt_pcm *p = v->pcm;
    float lg = v->vol * (v->pan > 0 ? 1 - v->pan : 1), rg = v->vol * (v->pan < 0 ? 1 + v->pan : 1);
    float pk[2] = { 0, 0 }; double sq[2] = { 0, 0 };
    uint32_t i = 0;
    if (v->playing && p && p->frames) {
        for (; i < frames; i++) {
            uint64_t f = (uint64_t)v->pos;
            if (f >= p->frames) {
                if (v->loops != 0) { if (v->loops > 0) v->loops--; v->pos = 0; f = 0; }
                else { v->playing = 0; if (!v->ended) { v->ended = 1; rt_after(0, voice_finished, v); } break; }
            }
            float l = p->s[f * p->ch] / 32768.0f, r = p->ch > 1 ? p->s[f * p->ch + 1] / 32768.0f : l;
            l *= lg; r *= rg;
            out[i * 2] = l; out[i * 2 + 1] = r;
            float al = fabsf(l), ar = fabsf(r); if (al > pk[0]) pk[0] = al; if (ar > pk[1]) pk[1] = ar; sq[0] += l * l; sq[1] += r * r;
            v->pos += 1;
        }
    }
    for (; i < frames; i++) { out[i * 2] = 0; out[i * 2 + 1] = 0; }
    v->peak[0] = pk[0]; v->peak[1] = pk[1];
    v->avg[0] = (float)sqrt(sq[0] / (frames ? frames : 1)); v->avg[1] = (float)sqrt(sq[1] / (frames ? frames : 1));
    if (flags && !v->playing) *flags |= 16;
    return 0;
}
static struct voice *voice_new(struct rt_pcm *pcm) {
    struct voice *v = rt_xcalloc(1, sizeof *v);
    v->pcm = pcm; v->vol = 1;
    static uint32_t rio[5] = { FCC('a', 'u', 'o', 'u'), FCC('r', 'i', 'o', 'c'), FCC('a', 'p', 'p', 'l'), 0, 0 };
    if (iosrt_AudioComponentInstanceNew(rio, &v->unit) != 0) { free(v); return NULL; }
    struct asbd f = { pcm->rate, FCC('l', 'p', 'c', 'm'), 1 | 8, 8, 1, 8, 2, 32, 0 };
    iosrt_AudioUnitSetProperty(v->unit, 8, 1, 0, &f, sizeof f);
    struct render_cb cb = { (void *)voice_render, v };
    iosrt_AudioUnitSetProperty(v->unit, 23, 1, 0, &cb, sizeof cb);
    return v;
}
static void voice_play(struct voice *v) {
    if (!v) return;
    if (v->pcm && v->pos >= v->pcm->frames) v->pos = 0;
    v->ended = 0; v->playing = 1;
    if (!v->started) { v->started = 1; iosrt_AudioOutputUnitStart(v->unit); }
}
static void voice_stop(struct voice *v) { if (!v) return; v->playing = 0; if (v->started) { iosrt_AudioOutputUnitStop(v->unit); v->started = 0; } }
static void voice_free(struct voice *v) { if (!v) return; voice_stop(v); iosrt_AudioComponentInstanceDispose(v->unit); free(v); }

/* ------------------------------------------------------------------ AVAudioPlayer */
struct avplayer { Class isa; struct voice *v; struct rt_pcm *pcm; id delegate, url, data; long meter, rateOn; float rate; long loops; char *tmp; };
#define P(x) ((struct avplayer *)(x))
static id make_error(long code, const char *desc) {
    id info = RT_SEND2(id, (id)rt_class_named("NSDictionary"), "dictionaryWithObject:forKey:", id, rt_str(desc), id, rt_str("NSLocalizedDescription"));
    return RT_SEND3(id, (id)rt_class_named("NSError"), "errorWithDomain:code:userInfo:", id, rt_str("NSOSStatusErrorDomain"), long, code, id, info);
}
static void player_ended(struct voice *v) {
    id self = v->owner; if (!self) return;
    id d = P(self)->delegate;
    v->pos = 0;
    if (d && rt_responds(d, rt_sel("audioPlayerDidFinishPlaying:successfully:")))
        RT_SEND2(void, d, "audioPlayerDidFinishPlaying:successfully:", id, self, long, 1);
}
static id player_setup(id self, struct rt_pcm *pcm, id *err) {
    if (!pcm) {
        if (err) *err = make_error(FCC('t', 'y', 'p', '?'), "The operation couldn't be completed: the audio file cannot be decoded");
        objc_release(self); return NULL;
    }
    P(self)->pcm = pcm; P(self)->v = voice_new(pcm); P(self)->rate = 1;
    if (!P(self)->v) { if (err) *err = make_error(-50, "no audio output"); objc_release(self); return NULL; }
    P(self)->v->owner = self; P(self)->v->on_end = player_ended;
    if (err) *err = NULL;
    return self;
}
static id pl_initURL(id self, SEL s, id url, id *err) {
    (void)s; char *path = rt_url_path(url);
    struct rt_pcm *p = path ? rt_pcm_load(path) : NULL; free(path);
    P(self)->url = objc_retain(url);
    return player_setup(self, p, err);
}
static id pl_initURLHint(id self, SEL s, id url, id hint, id *err) { (void)hint; return pl_initURL(self, s, url, err); }
static id pl_initData(id self, SEL s, id data, id *err) {
    (void)s;
    size_t n = data ? RT_SEND0(unsigned long, data, "length") : 0;
    const void *b = n ? RT_SEND0(const void *, data, "bytes") : NULL;
    const char *tmpdir = getenv("TMPDIR"); char path[512];
    static int seq; snprintf(path, sizeof path, "%s/iosrt-avplayer-%d-%d", tmpdir && *tmpdir ? tmpdir : "/tmp", (int)getpid(), ++seq);
    FILE *f = fopen(path, "wb"); struct rt_pcm *p = NULL;
    if (f) { if (n) fwrite(b, 1, n, f); fclose(f); p = rt_pcm_load(path); }
    P(self)->tmp = strdup(path);
    P(self)->data = objc_retain(data);
    return player_setup(self, p, err);
}
static id pl_initDataHint(id self, SEL s, id data, id hint, id *err) { (void)hint; return pl_initData(self, s, data, err); }
static void pl_dealloc(id self, SEL s) {
    (void)s; struct avplayer *p = P(self);
    if (p->v) { p->v->owner = NULL; voice_free(p->v); }
    rt_pcm_free(p->pcm);
    if (p->tmp) { unlink(p->tmp); free(p->tmp); }
    objc_release(p->url); objc_release(p->data);
    rt_dealloc_instance(self);
}
static rt_bool pl_prepare(id self, SEL s) { (void)self; (void)s; return 1; }
static rt_bool pl_play(id self, SEL s) { (void)s; if (!P(self)->v) return 0; P(self)->v->loops = P(self)->loops; voice_play(P(self)->v); return 1; }
static rt_bool pl_playAt(id self, SEL s, double t) { (void)t; return pl_play(self, s); }
static void pl_pause(id self, SEL s) { (void)s; if (P(self)->v) P(self)->v->playing = 0; }
static void pl_stop(id self, SEL s) { (void)s; voice_stop(P(self)->v); }
static rt_bool pl_playing(id self, SEL s) { (void)s; return P(self)->v && P(self)->v->playing; }
static float pl_volume(id self, SEL s) { (void)s; return P(self)->v ? P(self)->v->vol : 0; }
static void pl_setVolume(id self, SEL s, float v) { (void)s; if (P(self)->v) P(self)->v->vol = v < 0 ? 0 : v > 1 ? 1 : v; }
static void pl_setVolumeFade(id self, SEL s, float v, double d) { (void)d; pl_setVolume(self, s, v); }
static float pl_pan(id self, SEL s) { (void)s; return P(self)->v ? P(self)->v->pan : 0; }
static void pl_setPan(id self, SEL s, float v) { (void)s; if (P(self)->v) P(self)->v->pan = v < -1 ? -1 : v > 1 ? 1 : v; }
static long pl_loops(id self, SEL s) { (void)s; return P(self)->loops; }
static void pl_setLoops(id self, SEL s, long n) { (void)s; P(self)->loops = n; if (P(self)->v) P(self)->v->loops = n; }
static double pl_time(id self, SEL s) { (void)s; struct avplayer *p = P(self); return p->v && p->pcm ? p->v->pos / p->pcm->rate : 0; }
static void pl_setTime(id self, SEL s, double t) { (void)s; struct avplayer *p = P(self); if (p->v && p->pcm) { double f = t * p->pcm->rate; if (f < 0) f = 0; if (f > (double)p->pcm->frames) f = (double)p->pcm->frames; p->v->pos = f; } }
static double pl_duration(id self, SEL s) { (void)s; struct avplayer *p = P(self); return p->pcm ? p->pcm->frames / p->pcm->rate : 0; }
static unsigned long pl_channels(id self, SEL s) { (void)s; return P(self)->pcm ? P(self)->pcm->ch : 0; }
static id pl_url(id self, SEL s) { (void)s; return P(self)->url; }
static id pl_data(id self, SEL s) { (void)s; return P(self)->data; }
static id pl_delegate(id self, SEL s) { (void)s; return P(self)->delegate; }
static void pl_setDelegate(id self, SEL s, id d) { (void)s; P(self)->delegate = d; }
static rt_bool pl_meter(id self, SEL s) { (void)s; return P(self)->meter != 0; }
static void pl_setMeter(id self, SEL s, long v) { (void)s; P(self)->meter = v & 0xff; }
static void pl_update(id self, SEL s) { (void)self; (void)s; }
static float db(float a) { return a > 1e-6f ? 20 * log10f(a) : -160; }
static float pl_avg(id self, SEL s, unsigned long ch) { (void)s; return P(self)->v ? db(P(self)->v->avg[ch ? 1 : 0]) : -160; }
static float pl_peak(id self, SEL s, unsigned long ch) { (void)s; return P(self)->v ? db(P(self)->v->peak[ch ? 1 : 0]) : -160; }
static rt_bool pl_rateOn(id self, SEL s) { (void)s; return P(self)->rateOn != 0; }
static void pl_setRateOn(id self, SEL s, long v) { (void)s; P(self)->rateOn = v & 0xff; }
static float pl_rate(id self, SEL s) { (void)s; return P(self)->rate; }
static void pl_setRate(id self, SEL s, float r) { (void)s; P(self)->rate = r; }
static double pl_deviceTime(id self, SEL s) { (void)self; (void)s; struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static id pl_settings(id self, SEL s) { (void)self; (void)s; return NULL; }

/* ------------------------------------------------------------------ system sounds */
struct sysound { struct rt_pcm *pcm; void *proc, *user; };
static struct sysound sounds[256]; static int nsounds;
static pthread_mutex_t snd_lock = PTHREAD_MUTEX_INITIALIZER;
int32_t iosrt_AudioServicesCreateSystemSoundID(id url, uint32_t *out) {
    if (!out) return -50;
    char *path = rt_url_path(url); struct rt_pcm *p = path ? rt_pcm_load(path) : NULL; free(path);
    if (!p) { *out = 0; return (int32_t)FCC('t', 'y', 'p', '?'); }
    pthread_mutex_lock(&snd_lock);
    int i = 0; while (i < nsounds && sounds[i].pcm) i++;
    if (i == nsounds && nsounds < 256) nsounds++;
    if (i >= 256) { pthread_mutex_unlock(&snd_lock); rt_pcm_free(p); return -50; }
    sounds[i] = (struct sysound){ p, NULL, NULL };
    pthread_mutex_unlock(&snd_lock);
    *out = 0x1000u + (uint32_t)i;
    return 0;
}
static struct sysound *sound_of(uint32_t id_) { uint32_t i = id_ - 0x1000u; return id_ >= 0x1000u && i < (uint32_t)nsounds && sounds[i].pcm ? &sounds[i] : NULL; }
int32_t iosrt_AudioServicesDisposeSystemSoundID(uint32_t id_) {
    pthread_mutex_lock(&snd_lock);
    struct sysound *s = sound_of(id_);
    if (s) { /* playing voices keep a reference only while active; disposal waits for them */ s->proc = NULL; }
    pthread_mutex_unlock(&snd_lock);
    return s ? 0 : -50;
}
struct oneshot { uint32_t id; id block; };
static void sound_ended(struct voice *v) {
    struct oneshot *o = v->owner;
    struct sysound *s = sound_of(o->id);
    if (s && s->proc) ((void (*)(uint32_t, void *))s->proc)(o->id, s->user);
    if (o->block) { struct rt_block *b = (struct rt_block *)o->block; ((void (*)(void *))b->invoke)(b); _Block_release(o->block); }
    free(o);
    voice_free(v);
}
static void play_sound(uint32_t id_, id block) {
    if (id_ == 4095 || id_ == 0x00000FFF) { if (block) { struct rt_block *b = (struct rt_block *)block; ((void (*)(void *))b->invoke)(b); } return; }   /* vibrate */
    struct sysound *s = sound_of(id_);
    if (!s) return;
    struct voice *v = voice_new(s->pcm); if (!v) return;
    struct oneshot *o = rt_xcalloc(1, sizeof *o); o->id = id_; o->block = block ? _Block_copy(block) : NULL;
    v->owner = o; v->on_end = sound_ended;
    voice_play(v);
}
void iosrt_AudioServicesPlaySystemSound(uint32_t id_) { play_sound(id_, NULL); }
void iosrt_AudioServicesPlayAlertSound(uint32_t id_) { play_sound(id_, NULL); }
void iosrt_AudioServicesPlaySystemSoundWithCompletion(uint32_t id_, id block) { play_sound(id_, block); }
void iosrt_AudioServicesPlayAlertSoundWithCompletion(uint32_t id_, id block) { play_sound(id_, block); }
int32_t iosrt_AudioServicesAddSystemSoundCompletion(uint32_t id_, void *rl, void *mode, void *proc, void *user) {
    (void)rl; (void)mode; struct sysound *s = sound_of(id_); if (!s) return -50; s->proc = proc; s->user = user; return 0;
}
void iosrt_AudioServicesRemoveSystemSoundCompletion(uint32_t id_) { struct sysound *s = sound_of(id_); if (s) s->proc = NULL; }
int32_t iosrt_AudioServicesGetProperty(uint32_t prop, uint32_t specsize, const void *spec, uint32_t *size, void *data) {
    (void)prop; (void)specsize; (void)spec; if (size && data && *size >= 4) { *(uint32_t *)data = 0; *size = 4; } return 0;
}
int32_t iosrt_AudioServicesSetProperty(uint32_t prop, uint32_t specsize, const void *spec, uint32_t size, const void *data) { (void)prop; (void)specsize; (void)spec; (void)size; (void)data; return 0; }
int32_t iosrt_AudioServicesGetPropertyInfo(uint32_t prop, uint32_t specsize, const void *spec, uint32_t *size, unsigned char *w) { (void)prop; (void)specsize; (void)spec; if (size) *size = 4; if (w) *w = 1; return 0; }

void rt_player_init(void) {
    rt_native_class(RT_CLASS(AVAudioPlayer), &iosrt_meta_AVAudioPlayer, "AVAudioPlayer", RT_CLASS(NSObject), sizeof(struct avplayer) - sizeof(void *));
    Class c = RT_CLASS(AVAudioPlayer);
    IM(c, "initWithContentsOfURL:error:", pl_initURL); IM(c, "initWithContentsOfURL:fileTypeHint:error:", pl_initURLHint);
    IM(c, "initWithData:error:", pl_initData); IM(c, "initWithData:fileTypeHint:error:", pl_initDataHint);
    IM(c, "dealloc", pl_dealloc);
    IM(c, "prepareToPlay", pl_prepare); IM(c, "play", pl_play); IM(c, "playAtTime:", pl_playAt); IM(c, "pause", pl_pause); IM(c, "stop", pl_stop);
    IM(c, "isPlaying", pl_playing); IM(c, "volume", pl_volume); IM(c, "setVolume:", pl_setVolume); IM(c, "setVolume:fadeDuration:", pl_setVolumeFade);
    IM(c, "pan", pl_pan); IM(c, "setPan:", pl_setPan); IM(c, "numberOfLoops", pl_loops); IM(c, "setNumberOfLoops:", pl_setLoops);
    IM(c, "currentTime", pl_time); IM(c, "setCurrentTime:", pl_setTime); IM(c, "duration", pl_duration); IM(c, "numberOfChannels", pl_channels);
    IM(c, "url", pl_url); IM(c, "data", pl_data); IM(c, "delegate", pl_delegate); IM(c, "setDelegate:", pl_setDelegate);
    IM(c, "isMeteringEnabled", pl_meter); IM(c, "setMeteringEnabled:", pl_setMeter); IM(c, "updateMeters", pl_update);
    IM(c, "averagePowerForChannel:", pl_avg); IM(c, "peakPowerForChannel:", pl_peak);
    IM(c, "enableRate", pl_rateOn); IM(c, "setEnableRate:", pl_setRateOn); IM(c, "rate", pl_rate); IM(c, "setRate:", pl_setRate);
    IM(c, "deviceCurrentTime", pl_deviceTime); IM(c, "settings", pl_settings);
}
