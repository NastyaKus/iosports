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
