/*
 * console_capture.h -- always-on capture of the process's console output.
 *
 * Every line written to stdout or stderr (runner, recomp-net, recomp-ui,
 * libraries) is timestamped and kept in a bounded in-memory ring from the
 * moment console_capture_install() runs, while still reaching the original
 * console unchanged. A diagnostics file can be attached at any time: it gets
 * the caller's header, the ring's backlog (so lines printed before the file
 * was opened are not lost), then every later line as it arrives.
 *
 * Mechanism: fds 1 and 2 are redirected into one pipe each; a reader thread
 * per pipe echoes raw bytes to the saved original fd and appends complete
 * lines to the ring and the file. Windows (UCRT _pipe/_dup2) and POSIX
 * (pipe/dup2) take this path.
 *
 * Portable fallback: where the pipes or threads cannot be set up (or
 * GENESIS_CONSOLE_CAPTURE=0), nothing is redirected and no ring exists;
 * console_capture_open_file() then points fds 1 and 2 straight at the file
 * (lines lose the console echo and timestamps but are still recorded) and
 * console_capture_close_file() restores them. Same interface, same file.
 */
#ifndef RUNNER_CONSOLE_CAPTURE_H
#define RUNNER_CONSOLE_CAPTURE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Install as early as possible in main(). Idempotent. Returns 1 when the
 * pipe capture is active, 0 when the fallback applies. Registers an atexit
 * drain so output printed up to exit reaches the file. */
int  console_capture_install(void);
int  console_capture_active(void);

/* Attach `path` (created or truncated; an existing file is first kept as
 * "<path>.previous"). `header` is written verbatim first, then the ring
 * backlog, then live lines. Replaces any file already attached. 1 = ok. */
int  console_capture_open_file(const char *path, const char *header);
/* Detach and close the file (if any). The ring keeps recording. */
void console_capture_close_file(void);
/* The attached file's path, or "" when none. */
const char *console_capture_file_path(void);

/* Drain and detach (also run from atexit). Safe to call more than once. */
void console_capture_shutdown(void);

/* Seconds since install (monotonic); 0 before install. */
double console_capture_uptime(void);
/* Bytes of timestamped lines recorded so far (tests wait on this). */
unsigned long long console_capture_bytes_recorded(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNNER_CONSOLE_CAPTURE_H */
