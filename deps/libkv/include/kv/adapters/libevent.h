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

#ifndef KV_ADAPTERS_LIBEVENT_H
#define KV_ADAPTERS_LIBEVENT_H
#include "../async.h"
#include "../cluster.h"
#include "../kv.h"

#include <event2/event.h>

#define KV_LIBEVENT_DELETED 0x01
#define KV_LIBEVENT_ENTERED 0x02

typedef struct kvLibeventEvents {
    kvAsyncContext *context;
    struct event *ev;
    struct event_base *base;
    struct timeval tv;
    short flags;
    short state;
} kvLibeventEvents;

static void kvLibeventDestroy(kvLibeventEvents *e) {
    vk_free(e);
}

static void kvLibeventHandler(evutil_socket_t fd, short event, void *arg) {
    ((void)fd);
    kvLibeventEvents *e = (kvLibeventEvents *)arg;
    e->state |= KV_LIBEVENT_ENTERED;

#define CHECK_DELETED()                       \
    if (e->state & KV_LIBEVENT_DELETED) { \
        kvLibeventDestroy(e);             \
        return;                               \
    }

    if ((event & EV_TIMEOUT) && (e->state & KV_LIBEVENT_DELETED) == 0) {
        kvAsyncHandleTimeout(e->context);
        CHECK_DELETED();
    }

    if ((event & EV_READ) && e->context && (e->state & KV_LIBEVENT_DELETED) == 0) {
        kvAsyncHandleRead(e->context);
        CHECK_DELETED();
    }

    if ((event & EV_WRITE) && e->context && (e->state & KV_LIBEVENT_DELETED) == 0) {
        kvAsyncHandleWrite(e->context);
        CHECK_DELETED();
    }

    e->state &= ~KV_LIBEVENT_ENTERED;
#undef CHECK_DELETED
}

static void kvLibeventUpdate(void *privdata, short flag, int isRemove) {
    kvLibeventEvents *e = (kvLibeventEvents *)privdata;
    const struct timeval *tv = e->tv.tv_sec || e->tv.tv_usec ? &e->tv : NULL;

    if (isRemove) {
        if ((e->flags & flag) == 0) {
            return;
        } else {
            e->flags &= ~flag;
        }
    } else {
        if (e->flags & flag) {
            return;
        } else {
            e->flags |= flag;
        }
    }

    event_del(e->ev);
    event_assign(e->ev, e->base, e->context->c.fd, e->flags | EV_PERSIST,
                 kvLibeventHandler, privdata);
    event_add(e->ev, tv);
}

static void kvLibeventAddRead(void *privdata) {
    kvLibeventUpdate(privdata, EV_READ, 0);
}

static void kvLibeventDelRead(void *privdata) {
    kvLibeventUpdate(privdata, EV_READ, 1);
}

static void kvLibeventAddWrite(void *privdata) {
    kvLibeventUpdate(privdata, EV_WRITE, 0);
}

static void kvLibeventDelWrite(void *privdata) {
    kvLibeventUpdate(privdata, EV_WRITE, 1);
}

static void kvLibeventCleanup(void *privdata) {
    kvLibeventEvents *e = (kvLibeventEvents *)privdata;
    if (!e) {
        return;
    }
    event_del(e->ev);
    event_free(e->ev);
    e->ev = NULL;

    if (e->state & KV_LIBEVENT_ENTERED) {
        e->state |= KV_LIBEVENT_DELETED;
    } else {
        kvLibeventDestroy(e);
    }
}

static void kvLibeventSetTimeout(void *privdata, struct timeval tv) {
    kvLibeventEvents *e = (kvLibeventEvents *)privdata;
    short flags = e->flags;
    e->flags = 0;
    e->tv = tv;
    kvLibeventUpdate(e, flags, 0);
}

static int kvLibeventAttach(kvAsyncContext *ac, struct event_base *base) {
    kvContext *c = &(ac->c);
    kvLibeventEvents *e;

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return KV_ERR;

    /* Create container for context and r/w events */
    e = (kvLibeventEvents *)vk_calloc(1, sizeof(*e));
    if (e == NULL)
        return KV_ERR;

    e->context = ac;

    /* Register functions to start/stop listening for events */
    ac->ev.addRead = kvLibeventAddRead;
    ac->ev.delRead = kvLibeventDelRead;
    ac->ev.addWrite = kvLibeventAddWrite;
    ac->ev.delWrite = kvLibeventDelWrite;
    ac->ev.cleanup = kvLibeventCleanup;
    ac->ev.scheduleTimer = kvLibeventSetTimeout;
    ac->ev.data = e;

    /* Initialize and install read/write events */
    e->ev = event_new(base, c->fd, EV_READ | EV_WRITE, kvLibeventHandler, e);
    e->base = base;
    return KV_OK;
}

/* Internal adapter function with correct function signature. */
static int kvLibeventAttachAdapter(kvAsyncContext *ac, void *base) {
    return kvLibeventAttach(ac, (struct event_base *)base);
}

KV_UNUSED
static int kvClusterOptionsUseLibevent(kvClusterOptions *options,
                                           struct event_base *base) {
    if (options == NULL || base == NULL) {
        return KV_ERR;
    }

    options->attach_fn = kvLibeventAttachAdapter;
    options->attach_data = base;
    return KV_OK;
}

#endif /* KV_ADAPTERS_LIBEVENT_H */
