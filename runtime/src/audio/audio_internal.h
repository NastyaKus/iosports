/* Shared audio types (the AudioToolbox structures as the app lays them out)
 * and the decoded-file representation used by ExtAudioFile, AudioFile,
 * AVAudioPlayer and system sounds. */
#pragma once
#include <stdint.h>

struct asbd { double rate; uint32_t fmt, flags, bytesPerPacket, framesPerPacket, bytesPerFrame, channels, bits, reserved; };
struct audio_buffer { uint32_t channels, bytes; void *data; };
struct abl { uint32_t n; uint32_t pad; struct audio_buffer b[8]; };
struct audio_timestamp { double sampleTime; uint64_t hostTime; double rateScalar; uint64_t wordClock; uint8_t smpte[24]; uint32_t flags, reserved; };
struct render_cb { void *proc; void *refcon; };

/* a decoded audio file: 16-bit interleaved PCM in memory */
struct rt_pcm {
    double rate; uint32_t ch; uint64_t frames; int16_t *s;
    struct asbd src;        /* the file's own data format */
    uint32_t ftype;         /* AudioFileTypeID ('caff', 'WAVE', 'AIFF', 'm4af') */
};
struct rt_pcm *rt_pcm_load(const char *path);   /* NULL if the format cannot be decoded here */
void rt_pcm_free(struct rt_pcm *p);
float rt_pcm_get(const struct asbd *f, const struct abl *l, uint32_t frame, uint32_t ch);
void rt_pcm_put(const struct asbd *f, struct abl *l, uint32_t frame, uint32_t ch, float x);
char *rt_url_path(id url);                      /* NSURL/CFURL or NSString -> malloc'd path */

/* audio.c: output units used internally */
int32_t iosrt_AudioComponentInstanceNew(void *comp, void **out);
int32_t iosrt_AudioComponentInstanceDispose(void *u);
int32_t iosrt_AudioUnitSetProperty(void *u, uint32_t prop, uint32_t scope, uint32_t elem, const void *data, uint32_t size);
int32_t iosrt_AudioOutputUnitStart(void *u);
int32_t iosrt_AudioOutputUnitStop(void *u);
