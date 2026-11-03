/*
 * CoreMIDI HLE. There is no MIDI hardware or network behind it yet: clients,
 * ports and the app's own virtual endpoints are real objects with properties,
 * the system reports no sources/destinations, and MIDISend goes nowhere.
 * MIDIPacketList helpers follow the iOS arm64 layout (#pragma pack(4)).
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

RT_DECLARE_CLASS(NSObject);
RT_DEFINE_CLASS(MIDINetworkSession);
RT_DEFINE_CLASS(MIDINetworkHost);
RT_DEFINE_CLASS(MIDINetworkConnection);

typedef uint32_t MIDIObjectRef;
enum { MO_CLIENT = 1, MO_PORT_IN, MO_PORT_OUT, MO_SOURCE, MO_DEST };
#define MAX_INT_PROPS 8
struct midi_obj {
    int kind; id name; void *proc; void *refcon; void *block;
    struct { id key; int32_t val; } props[MAX_INT_PROPS]; int nprops;
};
static struct midi_obj *objs[1024];
static pthread_mutex_t midi_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t uid_seed = 0x1a2b3c;

id iosrt_kMIDIPropertyName, iosrt_kMIDIPropertyDisplayName, iosrt_kMIDIPropertyUniqueID, iosrt_kMIDIPropertyPrivate,
   iosrt_kMIDIPropertyAdvanceScheduleTimeMuSec, iosrt_kMIDIPropertyManufacturer, iosrt_kMIDIPropertyModel,
   iosrt_kMIDIPropertyOffline, iosrt_MIDINetworkBonjourServiceType;

static MIDIObjectRef obj_new(int kind, id name, void *proc, void *refcon) {
    pthread_mutex_lock(&midi_lock);
    MIDIObjectRef r = 0;
    for (uint32_t i = 1; i < 1024; i++) if (!objs[i]) { r = i; break; }
    if (r) {
        struct midi_obj *o = rt_xcalloc(1, sizeof *o);
        o->kind = kind; o->name = name ? RT_SEND0(id, name, "copy") : NULL; o->proc = proc; o->refcon = refcon;
        o->props[0].key = iosrt_kMIDIPropertyUniqueID; o->props[0].val = (int32_t)(uid_seed += 0x9e37); o->nprops = 1;
        objs[r] = o;
    }
    pthread_mutex_unlock(&midi_lock);
    return r;
}
static struct midi_obj *obj_get(MIDIObjectRef r) { return r && r < 1024 ? objs[r] : NULL; }
static int32_t obj_dispose(MIDIObjectRef r) {
    pthread_mutex_lock(&midi_lock);
    struct midi_obj *o = obj_get(r);
    if (o) { objs[r] = NULL; objc_release(o->name); if (o->block) _Block_release(o->block); free(o); }
    pthread_mutex_unlock(&midi_lock);
    return o ? 0 : -10830;   /* kMIDIObjectNotFound */
}
static int same_key(id a, id b) { return a == b || (a && b && (RT_SEND1(long, a, "isEqualToString:", id, b) & 0xff)); }

int32_t iosrt_MIDIClientCreate(id name, void *notify, void *refcon, MIDIObjectRef *out) { *out = obj_new(MO_CLIENT, name, notify, refcon); return *out ? 0 : -10844; }
int32_t iosrt_MIDIClientCreateWithBlock(id name, MIDIObjectRef *out, void *blk) { *out = obj_new(MO_CLIENT, name, NULL, NULL); if (*out && blk) objs[*out]->block = _Block_copy(blk); return *out ? 0 : -10844; }
int32_t iosrt_MIDIClientDispose(MIDIObjectRef c) { return obj_dispose(c); }
int32_t iosrt_MIDIInputPortCreate(MIDIObjectRef c, id name, void *proc, void *refcon, MIDIObjectRef *out) { (void)c; *out = obj_new(MO_PORT_IN, name, proc, refcon); return *out ? 0 : -10844; }
int32_t iosrt_MIDIOutputPortCreate(MIDIObjectRef c, id name, MIDIObjectRef *out) { (void)c; *out = obj_new(MO_PORT_OUT, name, NULL, NULL); return *out ? 0 : -10844; }
int32_t iosrt_MIDIPortDispose(MIDIObjectRef p) { return obj_dispose(p); }
int32_t iosrt_MIDIPortConnectSource(MIDIObjectRef p, MIDIObjectRef src, void *refcon) { (void)p; (void)refcon; return obj_get(src) ? 0 : -10830; }
int32_t iosrt_MIDIPortDisconnectSource(MIDIObjectRef p, MIDIObjectRef src) { (void)p; (void)src; return 0; }
int32_t iosrt_MIDISourceCreate(MIDIObjectRef c, id name, MIDIObjectRef *out) { (void)c; *out = obj_new(MO_SOURCE, name, NULL, NULL); return *out ? 0 : -10844; }
int32_t iosrt_MIDIDestinationCreate(MIDIObjectRef c, id name, void *proc, void *refcon, MIDIObjectRef *out) { (void)c; *out = obj_new(MO_DEST, name, proc, refcon); return *out ? 0 : -10844; }
int32_t iosrt_MIDIEndpointDispose(MIDIObjectRef e) { return obj_dispose(e); }
unsigned long iosrt_MIDIGetNumberOfSources(void) { return 0; }
unsigned long iosrt_MIDIGetNumberOfDestinations(void) { return 0; }
unsigned long iosrt_MIDIGetNumberOfDevices(void) { return 0; }
MIDIObjectRef iosrt_MIDIGetSource(unsigned long i) { (void)i; return 0; }
MIDIObjectRef iosrt_MIDIGetDestination(unsigned long i) { (void)i; return 0; }
int32_t iosrt_MIDISend(MIDIObjectRef port, MIDIObjectRef dest, const void *pl) { (void)port; (void)pl; return obj_get(dest) ? 0 : -10830; }
int32_t iosrt_MIDIReceived(MIDIObjectRef src, const void *pl) { (void)pl; return obj_get(src) ? 0 : -10830; }
int32_t iosrt_MIDIFlushOutput(MIDIObjectRef dest) { (void)dest; return 0; }
int32_t iosrt_MIDIRestart(void) { return 0; }

int32_t iosrt_MIDIObjectGetIntegerProperty(MIDIObjectRef r, id key, int32_t *out) {
    pthread_mutex_lock(&midi_lock);
    struct midi_obj *o = obj_get(r); int32_t err = -10835;   /* kMIDIUnknownProperty */
    if (!o) err = -10830;
    else for (int i = 0; i < o->nprops; i++) if (same_key(o->props[i].key, key)) { *out = o->props[i].val; err = 0; break; }
    pthread_mutex_unlock(&midi_lock);
    return err;
}
int32_t iosrt_MIDIObjectSetIntegerProperty(MIDIObjectRef r, id key, int32_t v) {
    pthread_mutex_lock(&midi_lock);
    struct midi_obj *o = obj_get(r); int32_t err = 0;
    if (!o) err = -10830;
    else {
        int i; for (i = 0; i < o->nprops; i++) if (same_key(o->props[i].key, key)) break;
        if (i == o->nprops) { if (o->nprops < MAX_INT_PROPS) { o->props[i].key = objc_retain(key); o->nprops++; } else err = -10844; }
        if (!err) {
            /* uniqueness is enforced like CoreMIDI: kMIDIIDNotUnique */
            if (same_key(key, iosrt_kMIDIPropertyUniqueID))
                for (uint32_t k = 1; k < 1024 && !err; k++) if (k != r && objs[k] && objs[k]->props[0].val == v) err = -10843;
            if (!err) o->props[i].val = v;
        }
    }
    pthread_mutex_unlock(&midi_lock);
    return err;
}
int32_t iosrt_MIDIObjectGetStringProperty(MIDIObjectRef r, id key, id *out) {
    struct midi_obj *o = obj_get(r);
    if (!o) return -10830;
    if (same_key(key, iosrt_kMIDIPropertyName) || same_key(key, iosrt_kMIDIPropertyDisplayName)) { *out = objc_retain(o->name); return 0; }
    return -10835;
}
int32_t iosrt_MIDIObjectSetStringProperty(MIDIObjectRef r, id key, id v) {
    struct midi_obj *o = obj_get(r);
    if (!o) return -10830;
    if (same_key(key, iosrt_kMIDIPropertyName) || same_key(key, iosrt_kMIDIPropertyDisplayName)) { id old = o->name; o->name = v ? RT_SEND0(id, v, "copy") : NULL; objc_release(old); }
    return 0;
}

/* MIDIPacketList: { UInt32 numPackets; MIDIPacket packet[]; } with
 * MIDIPacket { UInt64 timeStamp; UInt16 length; Byte data[256]; }, packed to 4 */
#define PKT_HDR 10
static uint8_t *pkt_next(uint8_t *p) { uint16_t len; memcpy(&len, p + 8, 2); return (uint8_t *)(((uintptr_t)(p + PKT_HDR + len) + 3) & ~(uintptr_t)3); }
void *iosrt_MIDIPacketListInit(uint8_t *pl) { memset(pl, 0, 4); return pl + 4; }
void *iosrt_MIDIPacketListAdd(uint8_t *pl, unsigned long size, uint8_t *cur, uint64_t time, unsigned long n, const uint8_t *data) {
    uint32_t count; memcpy(&count, pl, 4);
    uint8_t *p = count ? pkt_next(cur) : pl + 4;
    if (p + PKT_HDR + n > pl + size || n > 0xffff) return NULL;
    uint16_t len = (uint16_t)n;
    memcpy(p, &time, 8); memcpy(p + 8, &len, 2); memcpy(p + PKT_HDR, data, n);
    count++; memcpy(pl, &count, 4);
    return p;
}

/* ---- MIDINetworkSession & co: a disabled session with no peers */
struct mns { Class isa; long enabled; unsigned long policy; };
static id g_session;
static id mns_default(Class c, SEL s) { (void)s; if (!g_session) g_session = rt_alloc_instance(c, 0); return g_session; }
static rt_bool mns_enabled(id self, SEL s) { (void)s; return ((struct mns *)self)->enabled != 0; }
static void mns_setEnabled(id self, SEL s, long v) { (void)s; ((struct mns *)self)->enabled = v & 0xff; }
static unsigned long mns_policy(id self, SEL s) { (void)s; return ((struct mns *)self)->policy; }
static void mns_setPolicy(id self, SEL s, unsigned long v) { (void)s; ((struct mns *)self)->policy = v; }
static id mns_emptySet(id self, SEL s) { (void)self; (void)s; return RT_SEND0(id, (id)rt_class_named("NSSet"), "set"); }
static id mns_name(id self, SEL s) { (void)self; (void)s; return rt_str("iosrt"); }
static MIDIObjectRef mns_endpoint(id self, SEL s) { (void)self; (void)s; return 0; }
static rt_bool mns_no(id self, SEL s, id a) { (void)self; (void)s; (void)a; return 0; }
struct mnh { Class isa; id name, address; long port; };
static id mnh_with(Class c, SEL s, id name, id addr, long port) { (void)s; id h = rt_alloc_instance(c, 0); struct mnh *x = (struct mnh *)h; x->name = objc_retain(name); x->address = objc_retain(addr); x->port = port; return objc_autorelease(h); }
static id mnh_withService(Class c, SEL s, id name, id svc) { (void)svc; return mnh_with(c, s, name, NULL, 0); }
static id mnh_name(id self, SEL s) { (void)s; return ((struct mnh *)self)->name; }
static id mnh_address(id self, SEL s) { (void)s; return ((struct mnh *)self)->address; }
static long mnh_port(id self, SEL s) { (void)s; return ((struct mnh *)self)->port; }
struct mnc { Class isa; id host; };
static id mnc_with(Class c, SEL s, id host) { (void)s; id o = rt_alloc_instance(c, 0); ((struct mnc *)o)->host = objc_retain(host); return objc_autorelease(o); }
static id mnc_host(id self, SEL s) { (void)s; return ((struct mnc *)self)->host; }

#define IM(c, s, f) rt_native_method(c, s, (void *)(f), "")
#define CM(c, s, f) rt_native_cmethod(c, s, (void *)(f), "")
void rt_midi_init(void) {
    iosrt_kMIDIPropertyName = rt_str_new("name");
    iosrt_kMIDIPropertyDisplayName = rt_str_new("displayName");
    iosrt_kMIDIPropertyUniqueID = rt_str_new("uniqueID");
    iosrt_kMIDIPropertyPrivate = rt_str_new("private");
    iosrt_kMIDIPropertyAdvanceScheduleTimeMuSec = rt_str_new("advanceScheduleTimeMuSec");
    iosrt_kMIDIPropertyManufacturer = rt_str_new("manufacturer");
    iosrt_kMIDIPropertyModel = rt_str_new("model");
    iosrt_kMIDIPropertyOffline = rt_str_new("offline");
    iosrt_MIDINetworkBonjourServiceType = rt_str_new("_apple-midi._udp");
    Class o = RT_CLASS(NSObject);
    rt_native_class(RT_CLASS(MIDINetworkSession), &iosrt_meta_MIDINetworkSession, "MIDINetworkSession", o, sizeof(struct mns) - sizeof(void *));
    Class ms = RT_CLASS(MIDINetworkSession);
    CM(ms, "defaultSession", mns_default);
    IM(ms, "isEnabled", mns_enabled); IM(ms, "setEnabled:", mns_setEnabled);
    IM(ms, "connectionPolicy", mns_policy); IM(ms, "setConnectionPolicy:", mns_setPolicy);
    IM(ms, "contacts", mns_emptySet); IM(ms, "connections", mns_emptySet);
    IM(ms, "networkName", mns_name); IM(ms, "localName", mns_name);
    IM(ms, "sourceEndpoint", mns_endpoint); IM(ms, "destinationEndpoint", mns_endpoint);
    IM(ms, "addContact:", mns_no); IM(ms, "removeContact:", mns_no); IM(ms, "addConnection:", mns_no); IM(ms, "removeConnection:", mns_no);
    rt_native_class(RT_CLASS(MIDINetworkHost), &iosrt_meta_MIDINetworkHost, "MIDINetworkHost", o, sizeof(struct mnh) - sizeof(void *));
    Class mh = RT_CLASS(MIDINetworkHost);
    CM(mh, "hostWithName:address:port:", mnh_with); CM(mh, "hostWithName:netService:", mnh_withService);
    IM(mh, "name", mnh_name); IM(mh, "address", mnh_address); IM(mh, "port", mnh_port);
    rt_native_class(RT_CLASS(MIDINetworkConnection), &iosrt_meta_MIDINetworkConnection, "MIDINetworkConnection", o, sizeof(struct mnc) - sizeof(void *));
    CM(RT_CLASS(MIDINetworkConnection), "connectionWithHost:", mnc_with); IM(RT_CLASS(MIDINetworkConnection), "host", mnc_host);
}
