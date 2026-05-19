/*
 * Copyright (c) 2009-2011, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2011, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef KV_ASYNC_H
#define KV_ASYNC_H
#include "kv.h"
#include "visibility.h"

#ifdef __cplusplus
extern "C" {
#endif

/* For the async cluster attach functions. */
#if defined(__GNUC__) || defined(__clang__)
#define KV_UNUSED __attribute__((unused))
#else
#define KV_UNUSED
#endif

struct kvAsyncContext; /* need forward declaration of kvAsyncContext */
struct dict;               /* dictionary header is included in async.c */

/* Reply callback prototype and container */
typedef void(kvCallbackFn)(struct kvAsyncContext *, void *, void *);
typedef struct kvCallback {
    struct kvCallback *next; /* simple singly linked list */
    kvCallbackFn *fn;
    int pending_subs;
    int unsubscribe_sent;
    void *privdata;
    int subscribed;
} kvCallback;

/* List of callbacks for either regular replies or pub/sub */
typedef struct kvCallbackList {
    kvCallback *head, *tail;
} kvCallbackList;

/* Connection callback prototypes */
typedef void(kvDisconnectCallback)(const struct kvAsyncContext *, int status);
typedef void(kvConnectCallback)(struct kvAsyncContext *, int status);
typedef void(kvTimerCallback)(void *timer, void *privdata);

/* Context for an async connection to KV */
typedef struct kvAsyncContext {
    /* Hold the regular context, so it can be realloc'ed. */
    kvContext c;

    /* Setup error flags so they can be used directly. */
    int err;
    char *errstr;

    /* Not used by libkv */
    void *data;
    void (*dataCleanup)(void *privdata);

    /* Event library data and hooks */
    struct {
        void *data;

        /* Hooks that are called when the library expects to start
         * reading/writing. These functions should be idempotent. */
        void (*addRead)(void *privdata);
        void (*delRead)(void *privdata);
        void (*addWrite)(void *privdata);
        void (*delWrite)(void *privdata);
        void (*cleanup)(void *privdata);
        void (*scheduleTimer)(void *privdata, struct timeval tv);
    } ev;

    /* Called when either the connection is terminated due to an error or per
     * user request. The status is set accordingly (KV_OK, KV_ERR). */
    kvDisconnectCallback *onDisconnect;

    /* Called when the first write event was received. */
    kvConnectCallback *onConnect;

    /* Regular command callbacks */
    kvCallbackList replies;

    /* Address used for connect() */
    struct sockaddr *saddr;
    size_t addrlen;

    /* Subscription callbacks */
    struct {
        kvCallbackList replies;
        struct dict *channels;
        struct dict *patterns;
        struct dict *schannels;
        int pending_unsubs;
    } sub;

    /* Any configured RESP3 PUSH handler */
    kvAsyncPushFn *push_cb;
} kvAsyncContext;

LIBKV_API kvAsyncContext *kvAsyncConnectWithOptions(const kvOptions *options);
LIBKV_API kvAsyncContext *kvAsyncConnect(const char *ip, int port);
LIBKV_API kvAsyncContext *kvAsyncConnectBind(const char *ip, int port, const char *source_addr);
LIBKV_API kvAsyncContext *kvAsyncConnectBindWithReuse(const char *ip, int port,
                                                                  const char *source_addr);
LIBKV_API kvAsyncContext *kvAsyncConnectUnix(const char *path);
LIBKV_API int kvAsyncSetConnectCallback(kvAsyncContext *ac, kvConnectCallback *fn);
LIBKV_API int kvAsyncSetDisconnectCallback(kvAsyncContext *ac, kvDisconnectCallback *fn);

LIBKV_API kvAsyncPushFn *kvAsyncSetPushCallback(kvAsyncContext *ac, kvAsyncPushFn *fn);
LIBKV_API int kvAsyncSetTimeout(kvAsyncContext *ac, struct timeval tv);
LIBKV_API void kvAsyncDisconnect(kvAsyncContext *ac);
LIBKV_API void kvAsyncFree(kvAsyncContext *ac);

/* Handle read/write events */
LIBKV_API void kvAsyncHandleRead(kvAsyncContext *ac);
LIBKV_API void kvAsyncHandleWrite(kvAsyncContext *ac);
LIBKV_API void kvAsyncHandleTimeout(kvAsyncContext *ac);
LIBKV_API void kvAsyncRead(kvAsyncContext *ac);
LIBKV_API void kvAsyncWrite(kvAsyncContext *ac);

/* Command functions for an async context. Write the command to the
 * output buffer and register the provided callback. */
LIBKV_API int kvvAsyncCommand(kvAsyncContext *ac, kvCallbackFn *fn, void *privdata, const char *format, va_list ap);
LIBKV_API int kvAsyncCommand(kvAsyncContext *ac, kvCallbackFn *fn, void *privdata, const char *format, ...);
LIBKV_API int kvAsyncCommandArgv(kvAsyncContext *ac, kvCallbackFn *fn, void *privdata, int argc, const char **argv, const size_t *argvlen);
LIBKV_API int kvAsyncFormattedCommand(kvAsyncContext *ac, kvCallbackFn *fn, void *privdata, const char *cmd, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* KV_ASYNC_H */
