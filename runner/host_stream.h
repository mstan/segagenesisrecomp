#ifndef GENESIS_HOST_STREAM_H
#define GENESIS_HOST_STREAM_H
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct GenesisHostStream GenesisHostStream;
/* One producer owns the borrowed FILE until destroy completes. Two fixed
 * buffers bound storage; a full pair applies backpressure without dropping.
 * Worker inputs are copied before return and immutable until completion.
 * NULL on allocation failure lets callers keep their original stdio path. */
GenesisHostStream *genesis_host_stream_create(FILE *, size_t capacity);
int genesis_host_stream_write(GenesisHostStream *, const void *, size_t);
int genesis_host_stream_printf(GenesisHostStream *, const char *, ...);
int genesis_host_stream_vprintf(GenesisHostStream *, const char *, va_list);
/* Same frozen formatting, with ordinary stdio fallback when creation failed. */
int genesis_host_stream_fprintf(GenesisHostStream *, FILE *, const char *, ...);
/* Publish and flush on the worker. The next publication applies backpressure
 * to the previous one; live readers see each published row on completion.
 * Acceptance is asynchronous; flush/destroy still acknowledge completion. */
int genesis_host_stream_flush_async(GenesisHostStream *);
/* Producer-side capture completion boundary, including function-static sinks.
 * Does not close borrowed FILEs or unregister still-active captures. */
int genesis_host_stream_flush_all(void);
/* Flush/destroy acknowledge actual completion/errors, not just acceptance.
 * Destroy drains but does not close the caller's FILE. */
int genesis_host_stream_flush(GenesisHostStream *);
int genesis_host_stream_destroy(GenesisHostStream *);
#ifdef __cplusplus
}
#endif
#endif
