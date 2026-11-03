// iosrt sample: RemoteIO output with a render callback (440 Hz sine, 1.5 s).
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#include <AudioToolbox/AudioToolbox.h>
#include <math.h>

typedef struct { double phase, freq, rate; UInt32 calls; } SineState;

static OSStatus render(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts,
                       UInt32 bus, UInt32 frames, AudioBufferList *io) {
    SineState *s = ref;
    for (UInt32 b = 0; b < io->mNumberBuffers; b++) {
        float *out = io->mBuffers[b].mData;
        double ph = s->phase;
        for (UInt32 i = 0; i < frames; i++) {
            out[i] = (float)(0.5 * sin(ph));
            ph += 2 * M_PI * s->freq / s->rate;
        }
    }
    s->phase = fmod(s->phase + 2 * M_PI * s->freq / s->rate * frames, 2 * M_PI);
    s->calls++;
    return noErr;
}

int main(void) {
    @autoreleasepool {
        [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayback error:nil];
        [[AVAudioSession sharedInstance] setActive:YES error:nil];
        AudioComponentDescription d = { kAudioUnitType_Output, kAudioUnitSubType_RemoteIO, kAudioUnitManufacturer_Apple, 0, 0 };
        AudioComponent comp = AudioComponentFindNext(NULL, &d);
        AudioUnit unit;
        OSStatus err = AudioComponentInstanceNew(comp, &unit);
        AudioStreamBasicDescription f = { 0 };
        f.mSampleRate = 44100; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
        err |= AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
        static SineState st = { 0, 440, 44100, 0 };
        AURenderCallbackStruct cb = { render, &st };
        err |= AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
        err |= AudioUnitInitialize(unit);
        err |= AudioOutputUnitStart(unit);
        NSLog(@"RemoteIO started (status %d), session rate %.0f", (int)err, [AVAudioSession sharedInstance].sampleRate);
        [NSThread sleepForTimeInterval:1.5];
        AudioOutputUnitStop(unit);
        AudioComponentInstanceDispose(unit);
        NSLog(@"stopped after %s render callbacks", st.calls > 50 ? "many" : "too few");
    }
    return 0;
}
