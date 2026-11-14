/*
 * iosrt C++ support, part 2: the pieces of Apple's C++ ABI that the libc++
 * build cannot provide, implemented in C on raw object layouts and exported
 * under their exact Itanium mangled names (asm labels), plus the old GNU
 * libstdc++ ABI.
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#define MANGLED(sym) __asm__(sym)
_Noreturn void rt_cxx_fatal(const char *what) { rt_guest_exit(134, what); }

/* std::__1 itself (strings, streams, locale, threads, shared_ptr...) is libc++
 * built with Apple's configuration: runtime/third_party/libcxx, tools/build_libcxx.sh */

/* ---- random_device: Apple's uses arc4random and keeps no state in the object
 * (libc++'s Android build would store a file descriptor in it) */
void rd_ctor(void *self, const void *token) MANGLED("_ZNSt3__113random_deviceC1ERKNS_12basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEE");
void rd_ctor(void *self, const void *token) { (void)self; (void)token; }
void rd_dtor(void *self) MANGLED("_ZNSt3__113random_deviceD1Ev");
void rd_dtor(void *self) { (void)self; }
uint32_t iosrt_arc4random(void);
unsigned rd_call(void *self) MANGLED("_ZNSt3__113random_deviceclEv");
unsigned rd_call(void *self) { (void)self; return iosrt_arc4random(); }
double rd_entropy(void *self) MANGLED("_ZNKSt3__113random_device7entropyEv");
double rd_entropy(void *self) { (void)self; return 32; }

/* ---- exceptions: no unwinding through Mach-O frames yet; describe what was thrown */
extern char *__cxa_demangle(const char *, char *, size_t *, int *);
extern char _ZTVN10__cxxabiv117__class_type_infoE[], _ZTVN10__cxxabiv120__si_class_type_infoE[], _ZTVN10__cxxabiv121__vmi_class_type_infoE[];
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
