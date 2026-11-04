// iosrt sample: AudioQueue output, the way games stream audio: three 16-bit
// stereo buffers primed before start and refilled from the output callback
// (on the queue's own thread) with a 440 Hz sine for 1.5 s, then stop.
#import <Foundation/Foundation.h>
#include <AudioToolbox/AudioToolbox.h>
#include <math.h>
#include <unistd.h>

typedef struct { double phase; UInt32 refills; UInt64 frames; Boolean done; } Synth;
static const double kRate = 44100;

static void fill(Synth *s, AudioQueueBufferRef b) {
    SInt16 *p = b->mAudioData;
    UInt32 frames = b->mAudioDataBytesCapacity / 4;
    for (UInt32 i = 0; i < frames; i++) {
        SInt16 v = (SInt16)lrint(0.5 * 32767 * sin(s->phase));
        p[2 * i] = v; p[2 * i + 1] = v;
        s->phase = fmod(s->phase + 2 * M_PI * 440 / kRate, 2 * M_PI);
    }
    b->mAudioDataByteSize = frames * 4;
    s->frames += frames;
}
static void callback(void *user, AudioQueueRef q, AudioQueueBufferRef b) {
    Synth *s = user;
    if (s->done) return;
    s->refills++;
    fill(s, b);
    AudioQueueEnqueueBuffer(q, b, 0, NULL);
}

int main(void) {
    @autoreleasepool {
        AudioStreamBasicDescription f = { kRate, kAudioFormatLinearPCM, kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked, 4, 1, 4, 2, 16, 0 };
        Synth s = { 0 };
        AudioQueueRef q = NULL;
        OSStatus err = AudioQueueNewOutput(&f, callback, &s, NULL, NULL, 0, &q);
        NSLog(@"new output: %d, queue %d", (int)err, q != NULL);
        for (int i = 0; i < 3; i++) {
            AudioQueueBufferRef b = NULL;
            AudioQueueAllocateBuffer(q, 2048 * 4, &b);
            fill(&s, b);
            AudioQueueEnqueueBuffer(q, b, 0, NULL);
        }
        AudioQueueSetParameter(q, kAudioQueueParam_Volume, 1.0f);
        Float32 vol = 0; AudioQueueGetParameter(q, kAudioQueueParam_Volume, &vol);
        err = AudioQueueStart(q, NULL);
        UInt32 running = 0, size = sizeof running;
        AudioQueueGetProperty(q, kAudioQueueProperty_IsRunning, &running, &size);
        NSLog(@"start: %d, running %u, volume %.1f", (int)err, running, vol);
        usleep(1500 * 1000);
        s.done = 1;
        AudioTimeStamp ts; AudioQueueGetCurrentTime(q, NULL, &ts, NULL);
        AudioQueueStop(q, true);
        NSLog(@"stopped: refilled from the callback %d, played about 1.5 s %d", s.refills > 20, ts.mSampleTime > kRate && ts.mSampleTime < 3 * kRate);
        AudioQueueDispose(q, true);
    }
    return 0;
}
