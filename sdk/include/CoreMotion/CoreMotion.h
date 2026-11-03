/* iosrt mini-SDK: CoreMotion accelerometer subset. */
#import <Foundation/Foundation.h>
@class NSOperationQueue;
typedef struct { double x, y, z; } CMAcceleration;
@interface CMLogItem : NSObject
@property (readonly) NSTimeInterval timestamp;
@end
@interface CMAccelerometerData : CMLogItem
@property (readonly) CMAcceleration acceleration;
@end
typedef void (^CMAccelerometerHandler)(CMAccelerometerData *data, NSError *error);
@interface CMMotionManager : NSObject
@property NSTimeInterval accelerometerUpdateInterval;
@property (readonly, getter=isAccelerometerAvailable) BOOL accelerometerAvailable;
@property (readonly, getter=isAccelerometerActive) BOOL accelerometerActive;
@property (readonly, getter=isGyroAvailable) BOOL gyroAvailable;
@property (readonly) CMAccelerometerData *accelerometerData;
- (void)startAccelerometerUpdates;
- (void)startAccelerometerUpdatesToQueue:(NSOperationQueue *)queue withHandler:(CMAccelerometerHandler)handler;
- (void)stopAccelerometerUpdates;
@end
