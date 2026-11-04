#pragma once
#import <Foundation/Foundation.h>
@interface AVAudioSession : NSObject
+ (AVAudioSession *)sharedInstance;
- (BOOL)setCategory:(NSString *)c error:(NSError **)e;
- (BOOL)setActive:(BOOL)a error:(NSError **)e;
- (BOOL)setPreferredSampleRate:(double)r error:(NSError **)e;
@property (readonly) double sampleRate;
@property (readonly) NSTimeInterval IOBufferDuration;
@end
#ifdef __cplusplus
extern "C" {
#endif
extern NSString *const AVAudioSessionCategoryPlayback;
extern NSString *const AVAudioSessionCategoryPlayAndRecord;
#ifdef __cplusplus
}
#endif
@class AVAudioPlayer;
@protocol AVAudioPlayerDelegate <NSObject>
@optional
- (void)audioPlayerDidFinishPlaying:(AVAudioPlayer *)player successfully:(BOOL)flag;
@end
@interface AVAudioPlayer : NSObject
- (instancetype)initWithContentsOfURL:(NSURL *)url error:(NSError **)error;
- (instancetype)initWithData:(NSData *)data error:(NSError **)error;
- (BOOL)prepareToPlay;
- (BOOL)play;
- (void)pause;
- (void)stop;
@property (readonly, getter=isPlaying) BOOL playing;
@property float volume;
@property float pan;
@property NSInteger numberOfLoops;
@property NSTimeInterval currentTime;
@property (readonly) NSTimeInterval duration;
@property (readonly) NSUInteger numberOfChannels;
@property (weak) id<AVAudioPlayerDelegate> delegate;
@property (getter=isMeteringEnabled) BOOL meteringEnabled;
- (void)updateMeters;
- (float)peakPowerForChannel:(NSUInteger)ch;
@end

