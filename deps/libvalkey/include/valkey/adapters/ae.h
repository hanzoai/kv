/*
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

#ifndef KV_ADAPTERS_AE_H
#define KV_ADAPTERS_AE_H
#include "../async.h"
#include "../cluster.h"
#include "../kv.h"

#include <ae.h>
#include <sys/types.h>

typedef struct kvAeEvents {
    kvAsyncContext *context;
    aeEventLoop *loop;
    int fd;
    int reading, writing;
} kvAeEvents;

static void kvAeReadEvent(aeEventLoop *el, int fd, void *privdata, int mask) {
    ((void)el);
    ((void)fd);
    ((void)mask);

    kvAeEvents *e = (kvAeEvents *)privdata;
    kvAsyncHandleRead(e->context);
}

static void kvAeWriteEvent(aeEventLoop *el, int fd, void *privdata, int mask) {
    ((void)el);
    ((void)fd);
    ((void)mask);

    kvAeEvents *e = (kvAeEvents *)privdata;
    kvAsyncHandleWrite(e->context);
}

static void kvAeAddRead(void *privdata) {
    kvAeEvents *e = (kvAeEvents *)privdata;
    aeEventLoop *loop = e->loop;
    if (!e->reading) {
        e->reading = 1;
        aeCreateFileEvent(loop, e->fd, AE_READABLE, kvAeReadEvent, e);
    }
}

static void kvAeDelRead(void *privdata) {
    kvAeEvents *e = (kvAeEvents *)privdata;
    aeEventLoop *loop = e->loop;
    if (e->reading) {
        e->reading = 0;
        aeDeleteFileEvent(loop, e->fd, AE_READABLE);
    }
}

static void kvAeAddWrite(void *privdata) {
    kvAeEvents *e = (kvAeEvents *)privdata;
    aeEventLoop *loop = e->loop;
    if (!e->writing) {
        e->writing = 1;
        aeCreateFileEvent(loop, e->fd, AE_WRITABLE, kvAeWriteEvent, e);
    }
}

static void kvAeDelWrite(void *privdata) {
    kvAeEvents *e = (kvAeEvents *)privdata;
    aeEventLoop *loop = e->loop;
    if (e->writing) {
        e->writing = 0;
        aeDeleteFileEvent(loop, e->fd, AE_WRITABLE);
    }
}

static void kvAeCleanup(void *privdata) {
    kvAeEvents *e = (kvAeEvents *)privdata;
    kvAeDelRead(privdata);
    kvAeDelWrite(privdata);
    vk_free(e);
}

static int kvAeAttach(aeEventLoop *loop, kvAsyncContext *ac) {
    kvContext *c = &(ac->c);
    kvAeEvents *e;

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return KV_ERR;

    /* Create container for context and r/w events */
    e = (kvAeEvents *)vk_malloc(sizeof(*e));
    if (e == NULL)
        return KV_ERR;

    e->context = ac;
    e->loop = loop;
    e->fd = c->fd;
    e->reading = e->writing = 0;

    /* Register functions to start/stop listening for events */
    ac->ev.addRead = kvAeAddRead;
    ac->ev.delRead = kvAeDelRead;
    ac->ev.addWrite = kvAeAddWrite;
    ac->ev.delWrite = kvAeDelWrite;
    ac->ev.cleanup = kvAeCleanup;
    ac->ev.data = e;

    return KV_OK;
}

/* Internal adapter function with correct function signature. */
static int kvAeAttachAdapter(kvAsyncContext *ac, void *loop) {
    return kvAeAttach((aeEventLoop *)loop, ac);
}

KV_UNUSED
static int kvClusterOptionsUseAe(kvClusterOptions *options,
                                     aeEventLoop *loop) {
    if (options == NULL || loop == NULL) {
        return KV_ERR;
    }

    options->attach_fn = kvAeAttachAdapter;
    options->attach_data = loop;
    return KV_OK;
}

#endif /* KV_ADAPTERS_AE_H */
