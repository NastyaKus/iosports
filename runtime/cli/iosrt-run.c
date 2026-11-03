/* iosrt-run: command-line launcher for Mach-O arm64 iOS executables / .app bundles. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct rt_run_opts { int argc; char **argv; int dry_run; int verbose; };
int iosrt_run(const char *path, const struct rt_run_opts *o);
char *iosrt_report(const char *path);
void iosrt_set_screen(float w, float h, float scale);

/* bionic static executables need a TLS segment aligned to 64 bytes on arm64 */
__thread char iosrt_tls_align_pad[64] __attribute__((aligned(64), used));

static void usage(void) {
    fprintf(stderr,
            "usage: iosrt-run [--report] [--screen WxH] <app.app | mach-o> [args...]\n"
            "env: IOSRT_VERBOSE=1  IOSRT_LOG_PLAIN=1  IOSRT_HEADLESS_FRAMES=n  IOSRT_SCRIPT=...\n"
            "     IOSRT_DUMP_FRAMES=1  IOSRT_AUDIO_WAV=out.wav  IOSRT_HOME=dir\n");
}

int main(int argc, char **argv) {
    iosrt_tls_align_pad[0] = 0;
    int i = 1, report = 0;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if (!strcmp(argv[i], "--report")) report = 1;
        else if (!strcmp(argv[i], "--screen") && i + 1 < argc) { float w, h; if (sscanf(argv[++i], "%fx%f", &w, &h) == 2) iosrt_set_screen(w, h, 2); }
        else { usage(); return 2; }
    }
    if (i >= argc) { usage(); return 2; }
    if (report) {
        char *r = iosrt_report(argv[i]);
        fputs(r, stdout);
        int bad = strstr(r, "NOT LOADABLE") != NULL;
        free(r);
        return bad;
    }
    struct rt_run_opts o = { argc - i, argv + i, 0, 0 };
    return iosrt_run(argv[i], &o);
}
