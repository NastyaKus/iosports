// iosrt sample: audio files the way apps use them:
//  - ExtAudioFile writes a 22.05 kHz mono 16-bit CAF of a 440 Hz tone,
//    then reads it back as 44.1 kHz stereo float (rate + channel conversion);
//  - AudioFile reports the format, packet count and duration;
//  - AudioConverter turns the 16-bit mono samples into 48 kHz float stereo
//    through an input callback;
//  - AVAudioPlayer plays the file and tells its delegate when it is done;
//  - a system sound plays with a completion block.
#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#include <AudioToolbox/AudioToolbox.h>
#include <math.h>

static const double kRate = 22050;
static const UInt32 kFrames = 11025;   /* 0.5 s */

typedef struct { const SInt16 *pcm; UInt32 left; } Feed;
static OSStatus feed(AudioConverterRef c, UInt32 *ioPackets, AudioBufferList *io, AudioStreamPacketDescription **pd, void *user) {
    Feed *f = user;
    UInt32 n = *ioPackets < f->left ? *ioPackets : f->left;
    io->mNumberBuffers = 1; io->mBuffers[0].mNumberChannels = 1;
    io->mBuffers[0].mData = (void *)f->pcm; io->mBuffers[0].mDataByteSize = n * 2;
    f->pcm += n; f->left -= n; *ioPackets = n;
    return noErr;
}

@interface AudioController : UIViewController <AVAudioPlayerDelegate>
@property (nonatomic, strong) AVAudioPlayer *player;
@end
@implementation AudioController
- (void)viewDidLoad {
    [super viewDidLoad];
    NSString *path = [NSTemporaryDirectory() stringByAppendingPathComponent:@"tone.caf"];
    NSURL *url = [NSURL fileURLWithPath:path];
    SInt16 *pcm = malloc(kFrames * 2);
    for (UInt32 i = 0; i < kFrames; i++) pcm[i] = (SInt16)lrint(0.5 * 32767 * sin(2 * M_PI * 440 * i / kRate));

    /* write */
    AudioStreamBasicDescription mono16 = { kRate, kAudioFormatLinearPCM, kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked, 2, 1, 2, 1, 16, 0 };
    ExtAudioFileRef wf = NULL;
    OSStatus err = ExtAudioFileCreateWithURL((__bridge const void *)url, kAudioFileCAFType, &mono16, NULL, kAudioFileFlags_EraseFile, &wf);
    AudioBufferList wl = { 1, { { 1, kFrames * 2, pcm } } };
    OSStatus werr = ExtAudioFileWrite(wf, kFrames, &wl);
    ExtAudioFileDispose(wf);
    NSLog(@"ExtAudioFile write: create %d, write %d, file exists %d", (int)err, (int)werr, [[NSFileManager defaultManager] fileExistsAtPath:path]);

    /* read back with a different client format */
    ExtAudioFileRef rf = NULL;
    err = ExtAudioFileOpenURL((__bridge const void *)url, &rf);
    AudioStreamBasicDescription ff; UInt32 sz = sizeof ff;
    ExtAudioFileGetProperty(rf, kExtAudioFileProperty_FileDataFormat, &sz, &ff);
    SInt64 len = 0; sz = sizeof len; ExtAudioFileGetProperty(rf, kExtAudioFileProperty_FileLengthFrames, &sz, &len);
    AudioStreamBasicDescription client = { 44100, kAudioFormatLinearPCM, kAudioFormatFlagsNativeFloatPacked, 8, 1, 8, 2, 32, 0 };
    ExtAudioFileSetProperty(rf, kExtAudioFileProperty_ClientDataFormat, sizeof client, &client);
    float *st = calloc(30000 * 2, sizeof(float)); UInt32 total = 0;
    for (;;) {
        UInt32 n = 4096; AudioBufferList rl = { 1, { { 2, n * 8, st + total * 2 } } };
        if (ExtAudioFileRead(rf, &n, &rl) != noErr || n == 0) break;
        total += n;
    }
    ExtAudioFileDispose(rf);
    float peak = 0; for (UInt32 i = 0; i < total * 2; i++) peak = fmaxf(peak, fabsf(st[i]));
    NSLog(@"ExtAudioFile read: open %d, file %.0f Hz %u ch %u bit, %lld frames; as 44.1 kHz stereo float %u frames (about twice %d), peak %.2f, channels equal %d",
          (int)err, ff.mSampleRate, (unsigned)ff.mChannelsPerFrame, (unsigned)ff.mBitsPerChannel, len, (unsigned)total, abs((int)total - 2 * (int)kFrames) < 4, peak, st[1000] == st[1001]);

    /* AudioFile */
    AudioFileID af = NULL;
    err = AudioFileOpenURL((__bridge const void *)url, kAudioFileReadPermission, 0, &af);
    UInt64 packets = 0; sz = sizeof packets; AudioFileGetProperty(af, kAudioFilePropertyAudioDataPacketCount, &sz, &packets);
    Float64 dur = 0; sz = sizeof dur; AudioFileGetProperty(af, kAudioFilePropertyEstimatedDuration, &sz, &dur);
    AudioFileClose(af);
    NSLog(@"AudioFile: open %d, %llu packets, %.2f s", (int)err, packets, dur);

    /* AudioConverter through an input callback */
    AudioStreamBasicDescription out48 = { 48000, kAudioFormatLinearPCM, kAudioFormatFlagsNativeFloatPacked, 8, 1, 8, 2, 32, 0 };
    AudioConverterRef conv = NULL;
    err = AudioConverterNew(&mono16, &out48, &conv);
    Feed fd = { pcm, kFrames };
    float *o = calloc(30000 * 2, sizeof(float)); UInt32 got = 0;
    for (;;) {
        UInt32 n = 2048; AudioBufferList ol = { 1, { { 2, n * 8, o + got * 2 } } };
        if (AudioConverterFillComplexBuffer(conv, feed, &fd, &n, &ol, NULL) != noErr || n == 0) break;
        got += n;
    }
    AudioConverterDispose(conv);
    float cpeak = 0; for (UInt32 i = 0; i < got * 2; i++) cpeak = fmaxf(cpeak, fabsf(o[i]));
    NSLog(@"AudioConverter: new %d, 22.05 kHz mono int16 -> 48 kHz stereo float: %u frames (expected about %u), peak %.2f", (int)err, (unsigned)got, (unsigned)(kFrames * 48000 / kRate), cpeak);
    free(o); free(st); free(pcm);

    /* AVAudioPlayer */
    NSError *e = nil;
    self.player = [[AVAudioPlayer alloc] initWithContentsOfURL:url error:&e];
    self.player.delegate = self;
    self.player.volume = 0.8f;
    NSLog(@"AVAudioPlayer: created %d (error %d), duration %.2f s, %lu channel, volume %.1f, prepared %d, playing %d",
          self.player != nil, e != nil, self.player.duration, (unsigned long)self.player.numberOfChannels, self.player.volume, [self.player prepareToPlay], [self.player play]);
    AVAudioPlayer *bad = [[AVAudioPlayer alloc] initWithContentsOfURL:[NSURL fileURLWithPath:@"/nonexistent.caf"] error:&e];
    NSLog(@"missing file: player %d, error set %d", bad != nil, e != nil);

    /* system sound */
    SystemSoundID sid = 0;
    err = AudioServicesCreateSystemSoundID((__bridge const void *)url, &sid);
    NSLog(@"system sound: create %d, id set %d", (int)err, sid != 0);
    AudioServicesPlaySystemSoundWithCompletion(sid, ^{ NSLog(@"system sound finished"); });
}
- (void)audioPlayerDidFinishPlaying:(AVAudioPlayer *)player successfully:(BOOL)flag {
    NSLog(@"AVAudioPlayer finished: successfully %d, playing %d", flag, player.playing);
}
@end

@interface AudioAppDelegate : UIResponder <UIApplicationDelegate>
@property (nonatomic, strong) UIWindow *window;
@end
@implementation AudioAppDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)opts {
    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.window.rootViewController = [AudioController new];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass([AudioAppDelegate class])); }
}
