/* iosrt mini-SDK: AudioToolbox subset (RemoteIO output). */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef int32_t OSStatus;
typedef uint32_t UInt32;
typedef int32_t SInt32;
typedef double Float64;
typedef float Float32;
typedef int16_t SInt16;
typedef uint64_t UInt64;
typedef uint32_t OSType;
typedef uint32_t AudioUnitPropertyID, AudioUnitScope, AudioUnitElement, AudioUnitRenderActionFlags, AudioFormatID, AudioFormatFlags;
typedef struct OpaqueAudioComponent *AudioComponent;
typedef struct ComponentInstanceRecord *AudioComponentInstance;
typedef AudioComponentInstance AudioUnit;
#define noErr 0
typedef struct AudioComponentDescription { OSType componentType, componentSubType, componentManufacturer; UInt32 componentFlags, componentFlagsMask; } AudioComponentDescription;
typedef struct AudioStreamBasicDescription {
    Float64 mSampleRate; AudioFormatID mFormatID; AudioFormatFlags mFormatFlags; UInt32 mBytesPerPacket, mFramesPerPacket,
    mBytesPerFrame, mChannelsPerFrame, mBitsPerChannel, mReserved;
} AudioStreamBasicDescription;
typedef struct AudioBuffer { UInt32 mNumberChannels; UInt32 mDataByteSize; void *mData; } AudioBuffer;
typedef struct AudioBufferList { UInt32 mNumberBuffers; AudioBuffer mBuffers[1]; } AudioBufferList;
typedef struct SMPTETime { int16_t mSubframes, mSubframeDivisor; UInt32 mCounter, mType, mFlags; int16_t mHours, mMinutes, mSeconds, mFrames; } SMPTETime;
typedef struct AudioTimeStamp { Float64 mSampleTime; UInt64 mHostTime; Float64 mRateScalar; UInt64 mWordClockTime; SMPTETime mSMPTETime; UInt32 mFlags, mReserved; } AudioTimeStamp;
typedef OSStatus (*AURenderCallback)(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags, const AudioTimeStamp *inTimeStamp,
                                     UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList *ioData);
typedef struct AURenderCallbackStruct { AURenderCallback inputProc; void *inputProcRefCon; } AURenderCallbackStruct;
enum { kAudioUnitType_Output = 'auou', kAudioUnitSubType_RemoteIO = 'rioc', kAudioUnitManufacturer_Apple = 'appl' };
enum { kAudioFormatLinearPCM = 'lpcm' };
enum { kAudioFormatFlagIsFloat = 1, kAudioFormatFlagIsSignedInteger = 4, kAudioFormatFlagIsPacked = 8, kAudioFormatFlagIsNonInterleaved = 32 };
enum { kAudioUnitScope_Global = 0, kAudioUnitScope_Input = 1, kAudioUnitScope_Output = 2 };
enum { kAudioUnitProperty_StreamFormat = 8, kAudioUnitProperty_MaximumFramesPerSlice = 14, kAudioUnitProperty_SetRenderCallback = 23,
       kAudioOutputUnitProperty_EnableIO = 2003 };
AudioComponent AudioComponentFindNext(AudioComponent after, const AudioComponentDescription *desc);
OSStatus AudioComponentInstanceNew(AudioComponent c, AudioComponentInstance *out);
OSStatus AudioComponentInstanceDispose(AudioComponentInstance u);
OSStatus AudioUnitSetProperty(AudioUnit u, AudioUnitPropertyID p, AudioUnitScope s, AudioUnitElement e, const void *d, UInt32 sz);
OSStatus AudioUnitGetProperty(AudioUnit u, AudioUnitPropertyID p, AudioUnitScope s, AudioUnitElement e, void *d, UInt32 *sz);
OSStatus AudioUnitInitialize(AudioUnit u);
OSStatus AudioUnitUninitialize(AudioUnit u);
OSStatus AudioOutputUnitStart(AudioUnit u);
OSStatus AudioOutputUnitStop(AudioUnit u);
#ifdef __cplusplus
}
#endif
