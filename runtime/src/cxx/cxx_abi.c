/*
 * iosrt C++ support, part 2: non-template parts of Apple's libc++ ABI,
 * implemented in C on raw object layouts and exported under their exact
 * Itanium mangled names (asm labels). Guest objects are laid out by Apple's
 * headers, so nothing here may assume the NDK's layouts.
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#define MANGLED(sym) __asm__(sym)
_Noreturn void rt_cxx_fatal(const char *what) { rt_guest_exit(134, what); }

/* ---- std::__1::__shared_weak_count: { vptr; long shared_owners; long weak_owners; } */
struct sws { void **vptr; long shared, weak; };
typedef void (*vfn)(void *);
void sws_add_shared(struct sws *s) MANGLED("_ZNSt3__119__shared_weak_count12__add_sharedEv");
void sws_add_shared(struct sws *s) { __atomic_add_fetch(&s->shared, 1, __ATOMIC_RELAXED); }
void sws_add_weak(struct sws *s) MANGLED("_ZNSt3__119__shared_weak_count10__add_weakEv");
void sws_add_weak(struct sws *s) { __atomic_add_fetch(&s->weak, 1, __ATOMIC_RELAXED); }
void sws_release_weak(struct sws *s) MANGLED("_ZNSt3__119__shared_weak_count14__release_weakEv");
void sws_release_weak(struct sws *s) {
    /* vtable: [0] D1 [1] D0 [2] __on_zero_shared [3] __get_deleter [4] __on_zero_shared_weak */
    if (__atomic_load_n(&s->weak, __ATOMIC_ACQUIRE) == 0 || __atomic_sub_fetch(&s->weak, 1, __ATOMIC_ACQ_REL) == -1)
        ((vfn)s->vptr[4])(s);
}
void sws_release_shared(struct sws *s) MANGLED("_ZNSt3__119__shared_weak_count16__release_sharedEv");
void sws_release_shared(struct sws *s) {
    if (__atomic_sub_fetch(&s->shared, 1, __ATOMIC_ACQ_REL) == -1) {
        ((vfn)s->vptr[2])(s);
        sws_release_weak(s);
    }
}
struct sws *sws_lock(struct sws *s) MANGLED("_ZNSt3__119__shared_weak_count4lockEv");
struct sws *sws_lock(struct sws *s) {
    long c = __atomic_load_n(&s->shared, __ATOMIC_RELAXED);
    while (c != -1) {
        if (__atomic_compare_exchange_n(&s->shared, &c, c + 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) return s;
    }
    return NULL;
}
const void *sws_get_deleter(const struct sws *s, const void *ti) MANGLED("_ZNKSt3__119__shared_weak_count13__get_deleterERKSt9type_info");
const void *sws_get_deleter(const struct sws *s, const void *ti) { (void)s; (void)ti; return NULL; }
void sws_dtor(struct sws *s) MANGLED("_ZNSt3__119__shared_weak_countD2Ev");
void sws_dtor(struct sws *s) { (void)s; }
void sws_dtor1(struct sws *s) MANGLED("_ZNSt3__119__shared_weak_countD1Ev");
void sws_dtor1(struct sws *s) { (void)s; }

/* type_info objects for libc++ classes guests derive from / throw.
 * Layout: { vptr (-> __class_type_info vtable + 16), name, [base type_info] } */
extern char _ZTVN10__cxxabiv117__class_type_infoE[], _ZTVN10__cxxabiv120__si_class_type_infoE[];
extern char _ZTISt9exception[];
struct ti_class { const void *vptr; const char *name; };
struct ti_si { const void *vptr; const char *name; const void *base; };
static struct ti_class ti_shared_count = { _ZTVN10__cxxabiv117__class_type_infoE + 16, "NSt3__114__shared_countE" };
struct ti_si iosrt_ti_shared_weak_count MANGLED("_ZTINSt3__119__shared_weak_countE") =
    { _ZTVN10__cxxabiv120__si_class_type_infoE + 16, "NSt3__119__shared_weak_countE", &ti_shared_count };
struct ti_si iosrt_ti_bad_weak_ptr MANGLED("_ZTINSt3__112bad_weak_ptrE") =
    { _ZTVN10__cxxabiv120__si_class_type_infoE + 16, "NSt3__112bad_weak_ptrE", _ZTISt9exception };
/* std::__1::bad_weak_ptr : std::exception */
extern void _ZNSt9exceptionD2Ev(void *);
static const char *bwp_what(const void *self) { (void)self; return "bad_weak_ptr"; }
void bwp_dtor(void *self) MANGLED("_ZNSt3__112bad_weak_ptrD1Ev");
void bwp_dtor(void *self) { _ZNSt9exceptionD2Ev(self); }
void bwp_dtor2(void *self) MANGLED("_ZNSt3__112bad_weak_ptrD2Ev");
void bwp_dtor2(void *self) { _ZNSt9exceptionD2Ev(self); }
static void bwp_deleting_dtor(void *self) { bwp_dtor(self); free(self); }
struct { long offset; const void *ti; const void *fns[3]; } iosrt_vt_bad_weak_ptr MANGLED("_ZTVNSt3__112bad_weak_ptrE") =
    { 0, &iosrt_ti_bad_weak_ptr, { (const void *)bwp_dtor, (const void *)bwp_deleting_dtor, (const void *)bwp_what } };

/* ---- std::__1::mutex: wraps a Darwin pthread_mutex_t (may hold the static initializer) */
int iosrt_pthread_mutex_lock(pthread_mutex_t *m);
int iosrt_pthread_mutex_unlock(pthread_mutex_t *m);
int iosrt_pthread_mutex_trylock(pthread_mutex_t *m);
int iosrt_pthread_mutex_destroy(pthread_mutex_t *m);
void mtx_lock(void *m) MANGLED("_ZNSt3__15mutex4lockEv");
void mtx_lock(void *m) { int e = iosrt_pthread_mutex_lock(m); if (e) rt_cxx_fatal("std::mutex::lock failed"); }
void mtx_unlock(void *m) MANGLED("_ZNSt3__15mutex6unlockEv");
void mtx_unlock(void *m) { iosrt_pthread_mutex_unlock(m); }
long mtx_try_lock(void *m) MANGLED("_ZNSt3__15mutex8try_lockEv");
long mtx_try_lock(void *m) { return iosrt_pthread_mutex_trylock(m) == 0; }
void mtx_dtor(void *m) MANGLED("_ZNSt3__15mutexD1Ev");
void mtx_dtor(void *m) { iosrt_pthread_mutex_destroy(m); }
void mtx_dtor2(void *m) MANGLED("_ZNSt3__15mutexD2Ev");
void mtx_dtor2(void *m) { iosrt_pthread_mutex_destroy(m); }
/* recursive_mutex: constructed out-of-line */
void rmtx_ctor(pthread_mutex_t *m) MANGLED("_ZNSt3__115recursive_mutexC1Ev");
void rmtx_ctor(pthread_mutex_t *m) { pthread_mutexattr_t a; pthread_mutexattr_init(&a); pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE); pthread_mutex_init(m, &a); }
void rmtx_dtor(pthread_mutex_t *m) MANGLED("_ZNSt3__115recursive_mutexD1Ev");
void rmtx_dtor(pthread_mutex_t *m) { pthread_mutex_destroy(m); }
void rmtx_lock(pthread_mutex_t *m) MANGLED("_ZNSt3__115recursive_mutex4lockEv");
void rmtx_lock(pthread_mutex_t *m) { pthread_mutex_lock(m); }
void rmtx_unlock(pthread_mutex_t *m) MANGLED("_ZNSt3__115recursive_mutex6unlockEv");
void rmtx_unlock(pthread_mutex_t *m) { pthread_mutex_unlock(m); }

/* ---- std::__1::thread: { pthread_t __t_; } */
void thr_join(pthread_t *t) MANGLED("_ZNSt3__16thread4joinEv");
void thr_join(pthread_t *t) { if (!*t) rt_cxx_fatal("std::thread::join on a non-joinable thread"); pthread_join(*t, NULL); *t = 0; }
void thr_detach(pthread_t *t) MANGLED("_ZNSt3__16thread6detachEv");
void thr_detach(pthread_t *t) { if (*t) pthread_detach(*t); *t = 0; }
void thr_dtor(pthread_t *t) MANGLED("_ZNSt3__16threadD1Ev");
void thr_dtor(pthread_t *t) { if (*t) rt_cxx_fatal("std::thread destroyed while joinable (std::terminate)"); }
unsigned thr_hw(void) MANGLED("_ZNSt3__16thread20hardware_concurrencyEv");
unsigned thr_hw(void) { return (unsigned)sysconf(_SC_NPROCESSORS_ONLN); }
/* __thread_struct { __thread_struct_imp* }, __thread_local_data() -> __thread_specific_ptr { pthread_key_t } */
void ts_ctor(void **p) MANGLED("_ZNSt3__115__thread_structC1Ev");
void ts_ctor(void **p) { *p = rt_xcalloc(1, 64); }
void ts_dtor(void **p) MANGLED("_ZNSt3__115__thread_structD1Ev");
void ts_dtor(void **p) { free(*p); *p = NULL; }
static struct { unsigned long key; } tld;
static pthread_once_t tld_once = PTHREAD_ONCE_INIT;
static void tld_free(void *p) { if (p) { free(*(void **)p); free(p); } }
static void tld_init(void) { pthread_key_t k; pthread_key_create(&k, tld_free); tld.key = (unsigned long)k; }
void *tld_get(void) MANGLED("_ZNSt3__119__thread_local_dataEv");
void *tld_get(void) { pthread_once(&tld_once, tld_init); return &tld; }

/* ---- random_device (Apple: arc4random, object holds no state we may touch) */
void rd_ctor(void *self, const void *token) MANGLED("_ZNSt3__113random_deviceC1ERKNS_12basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEE");
void rd_ctor(void *self, const void *token) { (void)self; (void)token; }
void rd_dtor(void *self) MANGLED("_ZNSt3__113random_deviceD1Ev");
void rd_dtor(void *self) { (void)self; }
uint32_t iosrt_arc4random(void);
unsigned rd_call(void *self) MANGLED("_ZNSt3__113random_deviceclEv");
unsigned rd_call(void *self) { (void)self; return iosrt_arc4random(); }
double rd_entropy(void *self) MANGLED("_ZNKSt3__113random_device7entropyEv");
double rd_entropy(void *self) { (void)self; return 32; }

/* ---- chrono */
long long sc_now(void) MANGLED("_ZNSt3__16chrono12system_clock3nowEv");
long long sc_now(void) { struct timeval tv; gettimeofday(&tv, NULL); return (long long)tv.tv_sec * 1000000LL + tv.tv_usec; }
long long st_now(void) MANGLED("_ZNSt3__16chrono12steady_clock3nowEv");
long long st_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (long long)t.tv_sec * 1000000000LL + t.tv_nsec; }

/* ---- error helpers referenced from inline code of older libc++ */
void vbc_len(const void *self) MANGLED("_ZNKSt3__120__vector_base_commonILb1EE20__throw_length_errorEv");
void vbc_len(const void *self) { (void)self; rt_cxx_fatal("std::length_error: vector"); }
void vbc_range(const void *self) MANGLED("_ZNKSt3__120__vector_base_commonILb1EE20__throw_out_of_rangeEv");
void vbc_range(const void *self) { (void)self; rt_cxx_fatal("std::out_of_range: vector"); }
void bsc_len(const void *self) MANGLED("_ZNKSt3__121__basic_string_commonILb1EE20__throw_length_errorEv");
void bsc_len(const void *self) { (void)self; rt_cxx_fatal("std::length_error: basic_string"); }
void bsc_range(const void *self) MANGLED("_ZNKSt3__121__basic_string_commonILb1EE20__throw_out_of_rangeEv");
void bsc_range(const void *self) { (void)self; rt_cxx_fatal("std::out_of_range: basic_string"); }

/* ---- exceptions: no unwinding through Mach-O frames yet; describe what was thrown */
extern char *__cxa_demangle(const char *, char *, size_t *, int *);
extern char _ZTVN10__cxxabiv121__vmi_class_type_infoE[];
void iosrt_cxx_throw(void *obj, void *tinfo, void (*dest)(void *)) {
    (void)dest;
    const char *mangled = tinfo ? ((const char **)tinfo)[1] : "?";
    mangled = (const char *)((uintptr_t)mangled & ~(1ULL << 63));      /* Apple "non-unique RTTI" bit */
    int st = 0; char *dm = __cxa_demangle(mangled, NULL, NULL, &st);
    const char *what = NULL;
    const void *tvp = tinfo ? *(const void **)tinfo : NULL;
    int is_class = tvp == _ZTVN10__cxxabiv117__class_type_infoE + 16 || tvp == _ZTVN10__cxxabiv120__si_class_type_infoE + 16 ||
                   tvp == _ZTVN10__cxxabiv121__vmi_class_type_infoE + 16;
    if (obj && is_class && (strstr(mangled, "exception") || strstr(mangled, "error"))) {
        void **vt = *(void ***)obj;          /* std::exception vtable: D1, D0, what */
        if (vt) what = ((const char *(*)(void *))vt[2])(obj);
    }
    char msg[768];
    snprintf(msg, sizeof msg, "uncaught C++ exception of type %s%s%s (exception unwinding is not supported yet)",
             dm ? dm : mangled, what ? ": " : "", what ? what : "");
    free(dm);
    rt_guest_exit(72, msg);
}

/* ===================================================================
 * Old GNU libstdc++ ABI (apps linked against /usr/lib/libstdc++.6 before
 * iOS 7). Its containers are header templates except for a few node
 * helpers, implemented here from the algorithms (red-black tree as in the
 * classic STL design, doubly linked list hooks), with libstdc++'s node
 * layouts: _List_node_base { next, prev }, _Rb_tree_node_base { color
 * (0 red, 1 black), parent, left, right }; the tree header keeps root in
 * parent, leftmost in left and rightmost in right.
 * =================================================================== */
struct gnu_list { struct gnu_list *next, *prev; };
void gnu_list_hook(struct gnu_list *self, struct gnu_list *pos) __asm__("_ZNSt15_List_node_base4hookEPS_");
void gnu_list_hook(struct gnu_list *self, struct gnu_list *pos) { self->next = pos; self->prev = pos->prev; pos->prev->next = self; pos->prev = self; }
void gnu_list_unhook(struct gnu_list *self) __asm__("_ZNSt15_List_node_base6unhookEv");
void gnu_list_unhook(struct gnu_list *self) { self->prev->next = self->next; self->next->prev = self->prev; }
void gnu_list_reverse(struct gnu_list *self) __asm__("_ZNSt15_List_node_base7reverseEv");
void gnu_list_reverse(struct gnu_list *self) {
    struct gnu_list *n = self;
    do { struct gnu_list *t = n->next; n->next = n->prev; n->prev = t; n = t; } while (n != self);
}
/* move [first, last) before self */
void gnu_list_transfer(struct gnu_list *self, struct gnu_list *first, struct gnu_list *last) __asm__("_ZNSt15_List_node_base8transferEPS_S0_");
void gnu_list_transfer(struct gnu_list *self, struct gnu_list *first, struct gnu_list *last) {
    if (self == last || first == last) return;
    struct gnu_list *lastp = last->prev;
    first->prev->next = last; last->prev = first->prev;          /* unlink */
    struct gnu_list *before = self->prev;
    before->next = first; first->prev = before; lastp->next = self; self->prev = lastp;
}
void gnu_list_swap(struct gnu_list *a, struct gnu_list *b) __asm__("_ZNSt15_List_node_base4swapERS_S0_");
void gnu_list_swap(struct gnu_list *a, struct gnu_list *b) {
    /* swap the contents of two list headers (each possibly empty: pointing to itself) */
    struct gnu_list ta = *a, tb = *b;
    if (tb.next == b) { a->next = a->prev = a; } else { a->next = tb.next; a->prev = tb.prev; a->next->prev = a; a->prev->next = a; }
    if (ta.next == a) { b->next = b->prev = b; } else { b->next = ta.next; b->prev = ta.prev; b->next->prev = b; b->prev->next = b; }
}

enum { RB_RED = 0, RB_BLACK = 1 };
struct rbn { int color; struct rbn *parent, *left, *right; };
static struct rbn *rb_min(struct rbn *x) { while (x->left) x = x->left; return x; }
static struct rbn *rb_max(struct rbn *x) { while (x->right) x = x->right; return x; }
struct rbn *gnu_rb_increment(struct rbn *x) __asm__("_ZSt18_Rb_tree_incrementPSt18_Rb_tree_node_base");
struct rbn *gnu_rb_increment(struct rbn *x) {
    if (x->right) return rb_min(x->right);
    struct rbn *y = x->parent;
    while (x == y->right) { x = y; y = y->parent; }
    return x->right != y ? y : x;   /* the header's special case */
}
const struct rbn *gnu_rb_increment_c(const struct rbn *x) __asm__("_ZSt18_Rb_tree_incrementPKSt18_Rb_tree_node_base");
const struct rbn *gnu_rb_increment_c(const struct rbn *x) { return gnu_rb_increment((struct rbn *)x); }
struct rbn *gnu_rb_decrement(struct rbn *x) __asm__("_ZSt18_Rb_tree_decrementPSt18_Rb_tree_node_base");
struct rbn *gnu_rb_decrement(struct rbn *x) {
    if (x->color == RB_RED && x->parent->parent == x) return x->right;   /* end() -> rightmost */
    if (x->left) return rb_max(x->left);
    struct rbn *y = x->parent;
    while (x == y->left) { x = y; y = y->parent; }
    return y;
}
const struct rbn *gnu_rb_decrement_c(const struct rbn *x) __asm__("_ZSt18_Rb_tree_decrementPKSt18_Rb_tree_node_base");
const struct rbn *gnu_rb_decrement_c(const struct rbn *x) { return gnu_rb_decrement((struct rbn *)x); }
static void rb_rotate_left(struct rbn *x, struct rbn **root) {
    struct rbn *y = x->right;
    x->right = y->left; if (y->left) y->left->parent = x;
    y->parent = x->parent;
    if (x == *root) *root = y; else if (x == x->parent->left) x->parent->left = y; else x->parent->right = y;
    y->left = x; x->parent = y;
}
static void rb_rotate_right(struct rbn *x, struct rbn **root) {
    struct rbn *y = x->left;
    x->left = y->right; if (y->right) y->right->parent = x;
    y->parent = x->parent;
    if (x == *root) *root = y; else if (x == x->parent->right) x->parent->right = y; else x->parent->left = y;
    y->right = x; x->parent = y;
}
void gnu_rb_insert(_Bool left, struct rbn *x, struct rbn *p, struct rbn *header) __asm__("_ZSt29_Rb_tree_insert_and_rebalancebPSt18_Rb_tree_node_baseS0_RS_");
void gnu_rb_insert(_Bool left, struct rbn *x, struct rbn *p, struct rbn *header) {
    struct rbn **root = &header->parent;
    x->parent = p; x->left = x->right = NULL; x->color = RB_RED;
    if (left) {
        p->left = x;                       /* also sets leftmost when p is the header */
        if (p == header) { header->parent = x; header->right = x; }
        else if (p == header->left) header->left = x;
    } else {
        p->right = x;
        if (p == header->right) header->right = x;
    }
    while (x != *root && x->parent->color == RB_RED) {
        struct rbn *xpp = x->parent->parent;
        if (x->parent == xpp->left) {
            struct rbn *y = xpp->right;
            if (y && y->color == RB_RED) { x->parent->color = RB_BLACK; y->color = RB_BLACK; xpp->color = RB_RED; x = xpp; }
            else {
                if (x == x->parent->right) { x = x->parent; rb_rotate_left(x, root); }
                x->parent->color = RB_BLACK; xpp->color = RB_RED; rb_rotate_right(xpp, root);
            }
        } else {
            struct rbn *y = xpp->left;
            if (y && y->color == RB_RED) { x->parent->color = RB_BLACK; y->color = RB_BLACK; xpp->color = RB_RED; x = xpp; }
            else {
                if (x == x->parent->left) { x = x->parent; rb_rotate_right(x, root); }
                x->parent->color = RB_BLACK; xpp->color = RB_RED; rb_rotate_left(xpp, root);
            }
        }
    }
    (*root)->color = RB_BLACK;
}
/* unlink z and rebalance; returns the node to destroy (z) */
struct rbn *gnu_rb_erase(struct rbn *z, struct rbn *header) __asm__("_ZSt28_Rb_tree_rebalance_for_erasePSt18_Rb_tree_node_baseRS_");
struct rbn *gnu_rb_erase(struct rbn *z, struct rbn *header) {
    struct rbn **root = &header->parent, **leftmost = &header->left, **rightmost = &header->right;
    struct rbn *y = z, *x, *xp;
    if (!y->left) x = y->right;
    else if (!y->right) x = y->left;
    else { y = rb_min(y->right); x = y->right; }
    if (y != z) {   /* z has two children: y (its successor) takes z's place */
        z->left->parent = y; y->left = z->left;
        if (y != z->right) {
            xp = y->parent; if (x) x->parent = y->parent;
            y->parent->left = x;
            y->right = z->right; z->right->parent = y;
        } else xp = y;
        if (*root == z) *root = y; else if (z->parent->left == z) z->parent->left = y; else z->parent->right = y;
        y->parent = z->parent;
        int c = y->color; y->color = z->color; z->color = c;
        y = z;
    } else {
        xp = y->parent; if (x) x->parent = y->parent;
        if (*root == z) *root = x; else if (z->parent->left == z) z->parent->left = x; else z->parent->right = x;
        if (*leftmost == z) *leftmost = z->right ? rb_min(x) : z->parent;
        if (*rightmost == z) *rightmost = z->left ? rb_max(x) : z->parent;
    }
    if (y->color != RB_RED) {
        while (x != *root && (!x || x->color == RB_BLACK)) {
            if (x == xp->left) {
                struct rbn *w = xp->right;
                if (w->color == RB_RED) { w->color = RB_BLACK; xp->color = RB_RED; rb_rotate_left(xp, root); w = xp->right; }
                if ((!w->left || w->left->color == RB_BLACK) && (!w->right || w->right->color == RB_BLACK)) { w->color = RB_RED; x = xp; xp = xp->parent; }
                else {
                    if (!w->right || w->right->color == RB_BLACK) { w->left->color = RB_BLACK; w->color = RB_RED; rb_rotate_right(w, root); w = xp->right; }
                    w->color = xp->color; xp->color = RB_BLACK; if (w->right) w->right->color = RB_BLACK;
                    rb_rotate_left(xp, root); break;
                }
            } else {
                struct rbn *w = xp->left;
                if (w->color == RB_RED) { w->color = RB_BLACK; xp->color = RB_RED; rb_rotate_right(xp, root); w = xp->left; }
                if ((!w->right || w->right->color == RB_BLACK) && (!w->left || w->left->color == RB_BLACK)) { w->color = RB_RED; x = xp; xp = xp->parent; }
                else {
                    if (!w->left || w->left->color == RB_BLACK) { w->right->color = RB_BLACK; w->color = RB_RED; rb_rotate_left(w, root); w = xp->left; }
                    w->color = xp->color; xp->color = RB_BLACK; if (w->left) w->left->color = RB_BLACK;
                    rb_rotate_right(xp, root); break;
                }
            }
        }
        if (x) x->color = RB_BLACK;
    }
    return y;
}
unsigned gnu_rb_black_count(const struct rbn *node, const struct rbn *root) __asm__("_ZSt20_Rb_tree_black_countPKSt18_Rb_tree_node_baseS1_");
unsigned gnu_rb_black_count(const struct rbn *node, const struct rbn *root) {
    unsigned n = 0;
    for (; node; node = node->parent) { if (node->color == RB_BLACK) n++; if (node == root) break; }
    return n;
}
/* the throw helpers end the app with the message (no unwinding yet) */
void iosrt_gnu_throw_bad_alloc(void);   /* exported by hand in cxx.def: the NDK's libc++ defines the same name */
void iosrt_gnu_throw_bad_alloc(void) { rt_cxx_fatal("std::bad_alloc"); }
void gnu_throw_length_error(const char *m) __asm__("_ZSt20__throw_length_errorPKc");
void gnu_throw_length_error(const char *m) { char b[300]; snprintf(b, sizeof b, "std::length_error: %s", m ? m : ""); rt_cxx_fatal(b); }
void gnu_throw_out_of_range(const char *m) __asm__("_ZSt20__throw_out_of_rangePKc");
void gnu_throw_out_of_range(const char *m) { char b[300]; snprintf(b, sizeof b, "std::out_of_range: %s", m ? m : ""); rt_cxx_fatal(b); }
void gnu_throw_logic_error(const char *m) __asm__("_ZSt19__throw_logic_errorPKc");
void gnu_throw_logic_error(const char *m) { char b[300]; snprintf(b, sizeof b, "std::logic_error: %s", m ? m : ""); rt_cxx_fatal(b); }
void gnu_throw_invalid_argument(const char *m) __asm__("_ZSt24__throw_invalid_argumentPKc");
void gnu_throw_invalid_argument(const char *m) { char b[300]; snprintf(b, sizeof b, "std::invalid_argument: %s", m ? m : ""); rt_cxx_fatal(b); }
/* std::ios_base::Init of the old ABI: as for libc++, the streams are not provided */
void gnu_ios_init(void *self) __asm__("_ZNSt8ios_base4InitC1Ev");
void gnu_ios_init(void *self) { (void)self; }
void gnu_ios_fini(void *self) __asm__("_ZNSt8ios_base4InitD1Ev");
void gnu_ios_fini(void *self) { (void)self; }
