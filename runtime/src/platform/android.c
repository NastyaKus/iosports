/*
 * Android platform layer + JNI bridge for iosrt.
 *   - guest stdout/stderr/NSLog -> logcat + org.iosrt.NativeBridge.onLog
 *   - rendered UIKit frames     -> NativeBridge.onFrame (RGBA bitmap, drawn by IosView)
 *   - RemoteIO output           -> AAudio low-latency stream
 *   - libc++ imports (std::__1) -> remapped to the NDK's std::__ndk1 in libc++_shared.so
 */
#include "../internal.h"
#include <jni.h>
#include <android/log.h>
#include <aaudio/AAudio.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

static JavaVM *g_vm;
static jclass g_bridge;
static jmethodID m_onLog, m_onFrame, m_onExit;
static pthread_key_t detach_key;

static void detach_thread(void *p) { (void)p; if (g_vm) (*g_vm)->DetachCurrentThread(g_vm); }
static JNIEnv *env_for_thread(void) {
    JNIEnv *env = NULL;
    if ((*g_vm)->GetEnv(g_vm, (void **)&env, JNI_VERSION_1_6) == JNI_OK) return env;
    if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) return NULL;
    pthread_setspecific(detach_key, env);       /* detach automatically when the thread exits */
    return env;
}

static void a_write(int fd, const char *buf, size_t len) {
    char tmp[1024];
    size_t n = len < sizeof tmp - 1 ? len : sizeof tmp - 1;
    memcpy(tmp, buf, n); tmp[n] = 0;
    __android_log_write(fd == 2 ? ANDROID_LOG_WARN : ANDROID_LOG_INFO, "iosrt-guest", tmp);
    JNIEnv *env = env_for_thread();
    if (!env) return;
    jbyteArray arr = (*env)->NewByteArray(env, (jsize)len);
    (*env)->SetByteArrayRegion(env, arr, 0, (jsize)len, (const jbyte *)buf);
    (*env)->CallStaticVoidMethod(env, g_bridge, m_onLog, (jint)fd, arr);
    (*env)->DeleteLocalRef(env, arr);
}

static void a_present(const uint32_t *px, int w, int h, int stride) {
    JNIEnv *env = env_for_thread();
    if (!env) return;
    /* rows are contiguous in the screen surface; the Java side copies synchronously */
    const void *data = px;
    void *tmp = NULL;
    if (stride != w) {
        tmp = malloc((size_t)w * h * 4);
        for (int y = 0; y < h; y++) memcpy((uint32_t *)tmp + (size_t)y * w, px + (size_t)y * stride, (size_t)w * 4);
        data = tmp;
    }
    jobject buf = (*env)->NewDirectByteBuffer(env, (void *)data, (jlong)w * h * 4);
    (*env)->CallStaticVoidMethod(env, g_bridge, m_onFrame, buf, (jint)w, (jint)h);
    (*env)->DeleteLocalRef(env, buf);
    free(tmp);
}

/* ---------------------------------------------------------------- AAudio */
struct aaudio_unit { void *unit; AAudioStream *stream; uint32_t ch; };
static struct aaudio_unit streams[8];
static aaudio_data_callback_result_t a_cb(AAudioStream *s, void *ud, void *data, int32_t frames) {
    (void)s;
    struct aaudio_unit *u = ud;
    rt_audio_render(u->unit, (uint32_t)frames, data, u->ch);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
static int a_audio_start(void *unit) {
    struct aaudio_unit *slot = NULL;
    for (int i = 0; i < 8; i++) if (!streams[i].unit) { slot = &streams[i]; break; }
    if (!slot) return -1;
    AAudioStreamBuilder *b;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return -1;
    slot->unit = unit;
    slot->ch = rt_audio_unit_channels(unit); if (slot->ch > 2) slot->ch = 2;
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(b, (int32_t)slot->ch);
    AAudioStreamBuilder_setSampleRate(b, (int32_t)rt_audio_unit_rate(unit));
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(b, a_cb, slot);
    aaudio_result_t r = AAudioStreamBuilder_openStream(b, &slot->stream);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK) { slot->unit = NULL; rt_warn("AAudio open failed: %s", AAudio_convertResultToText(r)); return -1; }
    AAudioStream_requestStart(slot->stream);
    rt_log("AAudio stream started: %u ch @ %d Hz", slot->ch, AAudioStream_getSampleRate(slot->stream));
    return 0;
}
static void a_audio_stop(void *unit) {
    for (int i = 0; i < 8; i++) if (streams[i].unit == unit) {
        AAudioStream_requestStop(streams[i].stream);
        AAudioStream_close(streams[i].stream);
        streams[i].unit = NULL; streams[i].stream = NULL;
    }
}

static void a_on_exit(int code, const char *reason) {
    JNIEnv *env = env_for_thread();
    if (!env) return;
    jstring r = reason ? (*env)->NewStringUTF(env, reason) : NULL;
    (*env)->CallStaticVoidMethod(env, g_bridge, m_onExit, (jint)code, r);
}

static struct rt_platform android_platform = { "android", a_write, a_present, a_audio_start, a_audio_stop, a_on_exit };

/* std::__1 (Apple libc++) -> std::__ndk1 (NDK libc++): identical libc++ codebase, different inline namespace */
void *rt_platform_dlsym_fallback(const char *name) {
    if (!strstr(name, "NSt3__1")) return NULL;
    static void *libcxx;
    if (!libcxx) libcxx = dlopen("libc++_shared.so", RTLD_NOW);
    if (!libcxx) return NULL;
    char buf[1024]; size_t o = 0;
    const char *p = name[0] == '_' ? name + 1 : name;    /* strip Mach-O underscore */
    while (*p && o < sizeof buf - 8) {
        if (!strncmp(p, "NSt3__1", 7)) { memcpy(buf + o, "NSt6__ndk1", 10); o += 10; p += 7; }
        else buf[o++] = *p++;
    }
    buf[o] = 0;
    return dlsym(libcxx, buf);
}

/* ------------------------------------------------------------------- JNI */
RT_EXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)reserved;
    g_vm = vm;
    JNIEnv *env;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) return -1;
    jclass c = (*env)->FindClass(env, "org/iosrt/NativeBridge");
    g_bridge = (*env)->NewGlobalRef(env, c);
    m_onLog = (*env)->GetStaticMethodID(env, c, "onLog", "(I[B)V");
    m_onFrame = (*env)->GetStaticMethodID(env, c, "onFrame", "(Ljava/nio/ByteBuffer;II)V");
    m_onExit = (*env)->GetStaticMethodID(env, c, "onExit", "(ILjava/lang/String;)V");
    pthread_key_create(&detach_key, detach_thread);
    rt_plat = &android_platform;
    return JNI_VERSION_1_6;
}

void iosrt_init(void);
JNIEXPORT jstring JNICALL Java_org_iosrt_NativeBridge_nativeReport(JNIEnv *env, jclass cls, jstring jpath) {
    (void)cls;
    const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
    struct rt_platform *saved = rt_plat;
    rt_platform_set_host();          /* no JNI callbacks while building the report */
    char *r = iosrt_report(path);
    rt_plat = saved;
    (*env)->ReleaseStringUTFChars(env, jpath, path);
    jstring s = (*env)->NewStringUTF(env, r);
    free(r);
    return s;
}

JNIEXPORT jint JNICALL Java_org_iosrt_NativeBridge_nativeRun(JNIEnv *env, jclass cls, jstring jpath, jstring jhome,
                                                            jfloat w, jfloat h, jfloat scale) {
    (void)cls;
    const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
    const char *home = (*env)->GetStringUTFChars(env, jhome, NULL);
    setenv("IOSRT_HOME", home, 1);
    iosrt_set_screen(w, h, scale);
    char *p = rt_xstrdup(path);
    char *argv[2] = { p, NULL };
    struct rt_run_opts o = { 1, argv, 0, 0 };
    (*env)->ReleaseStringUTFChars(env, jhome, home);
    (*env)->ReleaseStringUTFChars(env, jpath, path);
    return iosrt_run(p, &o);
}

JNIEXPORT void JNICALL Java_org_iosrt_NativeBridge_nativeTouch(JNIEnv *env, jclass cls, jint phase, jfloat x, jfloat y) {
    (void)env; (void)cls; iosrt_push_touch(phase, x, y);
}
JNIEXPORT void JNICALL Java_org_iosrt_NativeBridge_nativeQuit(JNIEnv *env, jclass cls) { (void)env; (void)cls; iosrt_request_quit(); }
