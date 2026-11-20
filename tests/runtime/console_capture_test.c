/* console_capture_test -- launch history, live lines, partial-line drain,
 * ring wrap, rotation and the no-capture fallback of runner/console_capture.c. */
#include "console_capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
static void sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
#endif

/* Normally defined by crash_report.c. */
void (*crash_report_pre_exit_hook)(void);

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *b;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (char *)malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    if (b) b[n] = 0;
    fclose(f);
    return b;
}

static int wait_recorded(unsigned long long want)
{
    for (int i = 0; i < 1000; ++i) {
        if (console_capture_bytes_recorded() >= want) return 1;
        sleep_ms(5);
    }
    return 0;
}

int main(int argc, char **argv)
{
    char prev[1100], *text;
    unsigned long long before;
    CHECK(argc == 2);
    snprintf(prev, sizeof prev, "%s.previous", argv[1]);
    remove(argv[1]);
    remove(prev);

    CHECK(console_capture_install() == 1);
    CHECK(console_capture_active());
    CHECK(console_capture_install() == 1);   /* idempotent */
    CHECK(console_capture_file_path()[0] == 0);

    /* 1. Printed before any file exists: must come back as launch history. */
    fprintf(stderr, "early-stderr-line\n");
    printf("early-stdout-line\n");
    fflush(stdout);
    CHECK(wait_recorded(2 * 20));

    /* 2. Ring wrap: well over 1 MiB of history keeps the newest lines. */
    {
        /* Recorded size per line: "[%10.3f] " (13 bytes) + text + '\n'. Wait
         * for every byte so the newest line is history, not live. */
        static const char filler[] = "filler %06d ................................................................................\n";
        char one[256];
        unsigned long long want;
        int n = snprintf(one, sizeof one, filler, 0);
        before = console_capture_bytes_recorded();
        want = before + 12000ull * (unsigned long long)(13 + n) + 13 + strlen("newest-history-line\n");
        for (int i = 0; i < 12000; ++i) fprintf(stderr, filler, i);
        fprintf(stderr, "newest-history-line\n");
        CHECK(wait_recorded(want));
    }
    CHECK(console_capture_open_file(argv[1], "HEADER-LINE\n"));
    CHECK(!strcmp(console_capture_file_path(), argv[1]));

    /* 3. Live lines, and a final partial line drained at shutdown. */
    fprintf(stderr, "live-stderr-line\n");
    printf("live-stdout-line\n");
    fflush(stdout);
#if defined(_WIN32)
    const char *home = getenv("USERPROFILE");
#else
    const char *home = getenv("HOME");
#endif
    int check_home = home && strlen(home) >= 4 && strlen(home) < 400;
    if (check_home) {
        fprintf(stderr, "saved to %s/save.srm\n", home);
        fprintf(stderr, "sibling %sX2/keep\n", home);   /* another folder: kept */
    }
    fprintf(stderr, "partial-no-newline");
    /* The fatal path's lock-free drain (normal exit drains through
     * console_capture_shutdown; covered by the end-to-end runs). */
    CHECK(crash_report_pre_exit_hook != NULL);
    crash_report_pre_exit_hook();
    CHECK(!console_capture_active());
    console_capture_shutdown();               /* after a drain: no-op */
    console_capture_close_file();
    CHECK(console_capture_file_path()[0] == 0);

    text = slurp(argv[1]);
    CHECK(text);
    {
        static const char head[] = "HEADER-LINE\n---- console output since launch ----\n";
        CHECK(!strncmp(text, head, sizeof head - 1));
    }
    CHECK(strstr(text, "earlier console output was dropped"));
    CHECK(!strstr(text, "early-stderr-line"));        /* wrapped out */
    CHECK(!strstr(text, "filler 000000"));
    CHECK(strstr(text, "filler 011999"));
    CHECK(strstr(text, "] newest-history-line\n"));
    CHECK(strstr(text, "---- live ----\n"));
    CHECK(strstr(text, "] live-stderr-line\n"));
    CHECK(strstr(text, "] live-stdout-line\n"));
    CHECK(strstr(text, "] partial-no-newline\n"));
    CHECK(strstr(text, "newest-history-line") < strstr(text, "---- live ----"));
    CHECK(strstr(text, "---- live ----") < strstr(text, "live-stderr-line"));
    if (check_home) {   /* the player's home folder never reaches the file */
        char sibling[512];
        CHECK(strstr(text, "] saved to <home>/save.srm\n"));
        snprintf(sibling, sizeof sibling, "] sibling %sX2/keep\n", home);
        CHECK(strstr(text, sibling));                    /* prefix of a longer name */
        CHECK(!strstr(text, "<home>X2"));
    }
    free(text);

    /* 4. Fallback after shutdown (as on a platform without pipes): the file
     * still receives output; the previous file is rotated, not lost. */
    CHECK(console_capture_open_file(argv[1], "SECOND\n"));
    fprintf(stderr, "fallback-line\n");
    console_capture_close_file();
    fprintf(stderr, "after-close-line (console only)\n");
    text = slurp(argv[1]);
    CHECK(text);
    CHECK(!strncmp(text, "SECOND\n", 7));
    CHECK(strstr(text, "no launch history"));
    CHECK(strstr(text, "fallback-line\n"));
    CHECK(!strstr(text, "after-close-line"));
    free(text);
    text = slurp(prev);
    CHECK(text && !strncmp(text, "HEADER-LINE\n", 12));
    free(text);

    CHECK(remove(argv[1]) == 0);
    CHECK(remove(prev) == 0);
    puts("PASS launch history, ring wrap, live stdout/stderr, partial-line drain, rotation, fallback");
    return 0;
}
