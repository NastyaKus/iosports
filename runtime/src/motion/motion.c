/*
 * Accelerometer: UIKit's UIAccelerometer/UIAcceleration and CoreMotion's
 * CMMotionManager/CMAccelerometerData.
 *
 * On Android the readings come from the NDK sensor API (libandroid's
 * ASensorManager, opened at run time) on a small sensor thread. iOS reports
 * g units with gravity pointing away from the screen when the device lies
 * flat (z = -1); Android reports m/s^2 of the opposing force (z = +9.81), so
 * every axis is scaled by -1/9.81. Elsewhere the device "lies flat on a
 * table" (0, 0, -1), or IOSRT_ACCEL="x,y,z" sets a fixed reading.
 * Deliveries happen on the main thread at the requested interval.
 */
#include "../internal.h"
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#define IM(c, s, f) rt_native_method(c, s, (void *)(f), "")
#define CM(c, s, f) rt_native_cmethod(c, s, (void *)(f), "")
RT_DECLARE_CLASS(NSObject);
RT_DEFINE_CLASS(UIAccelerometer);
RT_DEFINE_CLASS(UIAcceleration);
RT_DEFINE_CLASS(CMMotionManager);
RT_DEFINE_CLASS(CMAccelerometerData);
RT_DEFINE_CLASS(CMLogItem);
void rt_after(double delay, void (*fn)(void *), void *ctx);
double rt_now(void);

/* ------------------------------------------------------------------ the sensor */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static double ax, ay, az = -1, ats;
static int sensor_state;   /* 0 not started, 1 device sensor, 2 fixed reading */

typedef struct { int32_t version, sensor, type, reserved0; int64_t timestamp; float data[16]; uint32_t flags; int32_t reserved1[3]; } asensor_event;
static void *sensor_thread(void *p) {
    void *lib = p;
    void *(*getInstance)(void) = dlsym(lib, "ASensorManager_getInstance");
    const void *(*getDefault)(void *, int) = dlsym(lib, "ASensorManager_getDefaultSensor");
    void *(*prepare)(int) = dlsym(lib, "ALooper_prepare");
    void *(*createQueue)(void *, void *, int, void *, void *) = dlsym(lib, "ASensorManager_createEventQueue");
    int (*enable)(void *, const void *) = dlsym(lib, "ASensorEventQueue_enableSensor");
    int (*setRate)(void *, const void *, int32_t) = dlsym(lib, "ASensorEventQueue_setEventRate");
    int (*poll)(int, int *, int *, void **) = dlsym(lib, "ALooper_pollOnce");
    long (*getEvents)(void *, asensor_event *, size_t) = dlsym(lib, "ASensorEventQueue_getEvents");
    if (!getInstance || !getDefault || !prepare || !createQueue || !enable || !poll || !getEvents) return NULL;
    void *mgr = getInstance(); const void *acc = mgr ? getDefault(mgr, 1 /* ASENSOR_TYPE_ACCELEROMETER */) : NULL;
    if (!acc) { rt_log("accelerometer: the device has none"); return NULL; }
    void *looper = prepare(1 /* ALOOPER_PREPARE_ALLOW_NON_CALLBACKS */);
    void *queue = createQueue(mgr, looper, 3, NULL, NULL);
    if (!queue || enable(queue, acc) < 0) return NULL;
    if (setRate) setRate(queue, acc, 16667);
    for (;;) {
        int ev, fd; void *data;
        if (poll(500, &fd, &ev, &data) < 0) continue;
        asensor_event e[8]; long n;
        while ((n = getEvents(queue, e, 8)) > 0) {
            asensor_event *last = &e[n - 1];
            pthread_mutex_lock(&lock);
            ax = -last->data[0] / 9.80665; ay = -last->data[1] / 9.80665; az = -last->data[2] / 9.80665; ats = rt_now();
            pthread_mutex_unlock(&lock);
        }
    }
    return NULL;
}
static void sensor_start(void) {
    pthread_mutex_lock(&lock);
    if (sensor_state) { pthread_mutex_unlock(&lock); return; }
    const char *fixed = getenv("IOSRT_ACCEL");
    void *lib = fixed ? NULL : dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (lib) {
        pthread_t t; sensor_state = pthread_create(&t, NULL, sensor_thread, lib) == 0 ? 1 : 2;
        if (sensor_state == 1) pthread_detach(t);
    } else {
        sensor_state = 2;
        if (fixed) sscanf(fixed, "%lf,%lf,%lf", &ax, &ay, &az);
    }
    pthread_mutex_unlock(&lock);
}
static void sensor_read(double *x, double *y, double *z, double *t) {
    sensor_start();
    pthread_mutex_lock(&lock);
    *x = ax; *y = ay; *z = az; *t = ats > 0 ? ats : rt_now();
    pthread_mutex_unlock(&lock);
}
int rt_accel_available(void) { return 1; }

/* ------------------------------------------------------------------ UIAcceleration / CMAccelerometerData */
struct uiacc { Class isa; double x, y, z, ts; };
static id acc_new(Class c, double x, double y, double z, double ts) {
    id o = rt_alloc_instance(c, 0); struct uiacc *a = (struct uiacc *)o; a->x = x; a->y = y; a->z = z; a->ts = ts; return objc_autorelease(o);
}
static double acc_x(id self, SEL s) { (void)s; return ((struct uiacc *)self)->x; }
static double acc_y(id self, SEL s) { (void)s; return ((struct uiacc *)self)->y; }
static double acc_z(id self, SEL s) { (void)s; return ((struct uiacc *)self)->z; }
static double acc_ts(id self, SEL s) { (void)s; return ((struct uiacc *)self)->ts; }
typedef struct { double x, y, z; } cmacc;
static cmacc acc_vec(id self, SEL s) { (void)s; struct uiacc *a = (struct uiacc *)self; return (cmacc){ a->x, a->y, a->z }; }
static id acc_desc(id self, SEL s) {
    (void)s; struct uiacc *a = (struct uiacc *)self; char b[96];
    snprintf(b, sizeof b, "x %f y %f z %f @ %f", a->x, a->y, a->z, a->ts); return rt_str(b);
}

/* ------------------------------------------------------------------ UIAccelerometer */
struct uiaccel { Class isa; id delegate; double interval; int ticking; };
static id g_shared;
static void accel_tick(void *p) {
    struct uiaccel *a = p;
    if (!a->delegate || !(a->interval > 0)) { a->ticking = 0; return; }
    double x, y, z, t; sensor_read(&x, &y, &z, &t);
    void *pool = objc_autoreleasePoolPush();
    id d = a->delegate;
    if (rt_responds(d, rt_sel("accelerometer:didAccelerate:")))
        RT_SEND2(void, d, "accelerometer:didAccelerate:", id, (id)a, id, acc_new(RT_CLASS(UIAcceleration), x, y, z, t));
    objc_autoreleasePoolPop(pool);
    rt_after(a->interval, accel_tick, a);
}
static id accel_shared(Class c, SEL s) {
    (void)s;
    if (!g_shared) { g_shared = rt_alloc_instance(c, 0); ((struct uiaccel *)g_shared)->interval = 0.1; }
    return g_shared;
}
static id accel_delegate(id self, SEL s) { (void)s; return ((struct uiaccel *)self)->delegate; }
static void accel_kick(struct uiaccel *a) { if (a->delegate && !a->ticking) { a->ticking = 1; sensor_start(); rt_after(a->interval > 0 ? a->interval : 0.1, accel_tick, a); } }
static void accel_setDelegate(id self, SEL s, id d) { (void)s; struct uiaccel *a = (struct uiaccel *)self; a->delegate = d; accel_kick(a); }
static double accel_interval(id self, SEL s) { (void)s; return ((struct uiaccel *)self)->interval; }
static void accel_setInterval(id self, SEL s, double v) { (void)s; struct uiaccel *a = (struct uiaccel *)self; a->interval = v < 0.01 ? 0.01 : v; accel_kick(a); }

/* ------------------------------------------------------------------ CMMotionManager */
struct cmmm { Class isa; double interval; int active; id handler, queue; long gen; };
struct cmtick { id mgr; long gen; };
static void call_handler(id handler, id data) {
    struct rt_block *b = (struct rt_block *)handler;
    ((void (*)(void *, id, id))b->invoke)(b, data, NULL);
}
static void mm_tick(void *p) {
    struct cmtick *t = p; struct cmmm *m = (struct cmmm *)t->mgr;
    if (!m->active || m->gen != t->gen) { objc_release(t->mgr); free(t); return; }
    if (m->handler) {
        double x, y, z, ts; sensor_read(&x, &y, &z, &ts);
        void *pool = objc_autoreleasePoolPush();
        call_handler(m->handler, acc_new(RT_CLASS(CMAccelerometerData), x, y, z, ts));
        objc_autoreleasePoolPop(pool);
    }
    rt_after(m->interval > 0 ? m->interval : 0.1, mm_tick, t);
}
static id mm_init(id self, SEL s) { (void)s; ((struct cmmm *)self)->interval = 0.1; return self; }
static void mm_dealloc(id self, SEL s) { (void)s; struct cmmm *m = (struct cmmm *)self; if (m->handler) _Block_release(m->handler); objc_release(m->queue); rt_dealloc_instance(self); }
static rt_bool mm_yes(id self, SEL s) { (void)self; (void)s; return 1; }
static rt_bool mm_no(id self, SEL s) { (void)self; (void)s; return 0; }
static rt_bool mm_active(id self, SEL s) { (void)s; return ((struct cmmm *)self)->active != 0; }
static double mm_interval(id self, SEL s) { (void)s; return ((struct cmmm *)self)->interval; }
static void mm_setInterval(id self, SEL s, double v) { (void)s; ((struct cmmm *)self)->interval = v < 0.01 ? 0.01 : v; }
static void mm_start(id self, SEL s) { (void)s; ((struct cmmm *)self)->active = 1; sensor_start(); }
static void mm_startQ(id self, SEL s, id queue, id handler) {
    (void)s; struct cmmm *m = (struct cmmm *)self;
    if (m->handler) _Block_release(m->handler);
    m->handler = handler ? _Block_copy(handler) : NULL;
    objc_retain(queue); objc_release(m->queue); m->queue = queue;
    m->active = 1; m->gen++; sensor_start();
    struct cmtick *t = calloc(1, sizeof *t); t->mgr = objc_retain(self); t->gen = m->gen;
    rt_after(m->interval, mm_tick, t);
}
static void mm_stop(id self, SEL s) { (void)s; struct cmmm *m = (struct cmmm *)self; m->active = 0; m->gen++; }
static id mm_data(id self, SEL s) {
    (void)s; if (!((struct cmmm *)self)->active) return NULL;
    double x, y, z, t; sensor_read(&x, &y, &z, &t);
    return acc_new(RT_CLASS(CMAccelerometerData), x, y, z, t);
}
static id mm_nil(id self, SEL s) { (void)self; (void)s; return NULL; }
static void mm_noop(id self, SEL s) { (void)self; (void)s; }
static void mm_noopQ(id self, SEL s, id q, id h) { (void)self; (void)s; (void)q; (void)h; }

void rt_motion_init(void) {
    Class o = RT_CLASS(NSObject);
    rt_native_class(RT_CLASS(UIAcceleration), &iosrt_meta_UIAcceleration, "UIAcceleration", o, sizeof(struct uiacc) - sizeof(void *));
    Class ua = RT_CLASS(UIAcceleration);
    IM(ua, "x", acc_x); IM(ua, "y", acc_y); IM(ua, "z", acc_z); IM(ua, "timestamp", acc_ts); IM(ua, "description", acc_desc);
    rt_native_class(RT_CLASS(UIAccelerometer), &iosrt_meta_UIAccelerometer, "UIAccelerometer", o, sizeof(struct uiaccel) - sizeof(void *));
    Class u = RT_CLASS(UIAccelerometer);
    CM(u, "sharedAccelerometer", accel_shared);
    IM(u, "delegate", accel_delegate); IM(u, "setDelegate:", accel_setDelegate);
    IM(u, "updateInterval", accel_interval); IM(u, "setUpdateInterval:", accel_setInterval);
    rt_native_class(RT_CLASS(CMLogItem), &iosrt_meta_CMLogItem, "CMLogItem", o, sizeof(struct uiacc) - sizeof(void *));
    IM(RT_CLASS(CMLogItem), "timestamp", acc_ts);
    rt_native_class(RT_CLASS(CMAccelerometerData), &iosrt_meta_CMAccelerometerData, "CMAccelerometerData", RT_CLASS(CMLogItem), 0);
    Class cd = RT_CLASS(CMAccelerometerData);
    IM(cd, "acceleration", acc_vec); IM(cd, "description", acc_desc);
    rt_native_class(RT_CLASS(CMMotionManager), &iosrt_meta_CMMotionManager, "CMMotionManager", o, sizeof(struct cmmm) - sizeof(void *));
    Class m = RT_CLASS(CMMotionManager);
    IM(m, "init", mm_init); IM(m, "dealloc", mm_dealloc);
    IM(m, "isAccelerometerAvailable", mm_yes); IM(m, "isAccelerometerActive", mm_active);
    IM(m, "accelerometerUpdateInterval", mm_interval); IM(m, "setAccelerometerUpdateInterval:", mm_setInterval);
    IM(m, "startAccelerometerUpdates", mm_start); IM(m, "startAccelerometerUpdatesToQueue:withHandler:", mm_startQ);
    IM(m, "stopAccelerometerUpdates", mm_stop); IM(m, "accelerometerData", mm_data);
    /* gyroscope, magnetometer and fused device motion: not available */
    IM(m, "isGyroAvailable", mm_no); IM(m, "isGyroActive", mm_no); IM(m, "isMagnetometerAvailable", mm_no); IM(m, "isMagnetometerActive", mm_no);
    IM(m, "isDeviceMotionAvailable", mm_no); IM(m, "isDeviceMotionActive", mm_no);
    IM(m, "gyroData", mm_nil); IM(m, "magnetometerData", mm_nil); IM(m, "deviceMotion", mm_nil);
    IM(m, "startGyroUpdates", mm_noop); IM(m, "stopGyroUpdates", mm_noop); IM(m, "startDeviceMotionUpdates", mm_noop); IM(m, "stopDeviceMotionUpdates", mm_noop);
    IM(m, "startMagnetometerUpdates", mm_noop); IM(m, "stopMagnetometerUpdates", mm_noop);
    IM(m, "startGyroUpdatesToQueue:withHandler:", mm_noopQ); IM(m, "startDeviceMotionUpdatesToQueue:withHandler:", mm_noopQ);
    IM(m, "setGyroUpdateInterval:", mm_setInterval); IM(m, "setDeviceMotionUpdateInterval:", mm_setInterval); IM(m, "setMagnetometerUpdateInterval:", mm_setInterval);
}
