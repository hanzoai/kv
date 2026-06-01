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

#include "fmacros.h"

#include "alloc.h"
#include "read.h"
#include "kv.h"

#include <stdlib.h>
#include <string.h>
#ifndef _MSC_VER
#include <strings.h>
#endif
#include "win32.h"

#include "async.h"
#include "async_private.h"
#include "net.h"
#include "kv_private.h"

#include <dict.h>
#include <sds.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>

#ifdef NDEBUG
#undef assert
#define assert(e) (void)(e)
#endif

typedef struct {
    sds command;
    kvCallbackFn *user_callback;
    void *user_priv_data;
} ssubscribeCallbackData;

/* Forward declarations of kv.c functions */
int kvAppendCmdLen(kvContext *c, const char *cmd, size_t len);

/* Functions managing dictionary of callbacks for pub/sub. */
static uint64_t callbackHash(const void *key) {
    return dictGenHashFunction((const unsigned char *)key,
                               sdslen((const sds)key));
}

static int callbackKeyCompare(const void *key1, const void *key2) {
    int l1, l2;

    l1 = sdslen((const sds)key1);
    l2 = sdslen((const sds)key2);
    if (l1 != l2)
        return 0;
    return memcmp(key1, key2, l1) == 0;
}

static void callbackKeyDestructor(void *key) {
    sdsfree((sds)key);
}

static void callbackValDestructor(void *val) {
    vk_free(val);
}

static dictType callbackDict = {
    .hashFunction = callbackHash,
    .keyCompare = callbackKeyCompare,
    .keyDestructor = callbackKeyDestructor,
    .valDestructor = callbackValDestructor};

static kvAsyncContext *kvAsyncInitialize(kvContext *c) {
    kvAsyncContext *ac;
    dict *channels = NULL, *patterns = NULL, *schannels = NULL;

    channels = dictCreate(&callbackDict);
    if (channels == NULL)
        goto oom;

    patterns = dictCreate(&callbackDict);
    if (patterns == NULL)
        goto oom;

    schannels = dictCreate(&callbackDict);
    if (schannels == NULL)
        goto oom;

    ac = vk_realloc(c, sizeof(kvAsyncContext));
    if (ac == NULL)
        goto oom;

    c = &(ac->c);

    /* The regular connect functions will always set the flag KV_CONNECTED.
     * For the async API, we want to wait until the first write event is
     * received up before setting this flag, so reset it here. */
    c->flags &= ~KV_CONNECTED;

    ac->err = 0;
    ac->errstr = NULL;
    ac->data = NULL;
    ac->dataCleanup = NULL;

    ac->ev.data = NULL;
    ac->ev.addRead = NULL;
    ac->ev.delRead = NULL;
    ac->ev.addWrite = NULL;
    ac->ev.delWrite = NULL;
    ac->ev.cleanup = NULL;
    ac->ev.scheduleTimer = NULL;

    ac->onConnect = NULL;
    ac->onDisconnect = NULL;

    ac->replies.head = NULL;
    ac->replies.tail = NULL;
    ac->sub.replies.head = NULL;
    ac->sub.replies.tail = NULL;
    ac->sub.channels = channels;
    ac->sub.patterns = patterns;
    ac->sub.schannels = schannels;
    ac->sub.pending_unsubs = 0;

    return ac;
oom:
    dictRelease(channels);
    dictRelease(patterns);
    dictRelease(schannels);
    return NULL;
}

/* We want the error field to be accessible directly instead of requiring
 * an indirection to the kvContext struct. */
static void kvAsyncCopyError(kvAsyncContext *ac) {
    if (!ac)
        return;

    kvContext *c = &(ac->c);
    ac->err = c->err;
    ac->errstr = c->errstr;
}

kvAsyncContext *kvAsyncConnectWithOptions(const kvOptions *options) {
    kvOptions myOptions = *options;
    kvContext *c;
    kvAsyncContext *ac;

    /* Clear any erroneously set sync callback and flag that we don't want to
     * use freeReplyObject by default. */
    myOptions.push_cb = NULL;
    myOptions.options |= KV_OPT_NO_PUSH_AUTOFREE;

    myOptions.options |= KV_OPT_NONBLOCK;
    c = kvConnectWithOptions(&myOptions);
    if (c == NULL) {
        return NULL;
    }

    ac = kvAsyncInitialize(c);
    if (ac == NULL) {
        kvFree(c);
        return NULL;
    }

    /* Set any configured async push handler */
    kvAsyncSetPushCallback(ac, myOptions.async_push_cb);

    kvAsyncCopyError(ac);
    return ac;
}

kvAsyncContext *kvAsyncConnect(const char *ip, int port) {
    kvOptions options = {0};
    KV_OPTIONS_SET_TCP(&options, ip, port);
    return kvAsyncConnectWithOptions(&options);
}

kvAsyncContext *kvAsyncConnectBind(const char *ip, int port,
                                           const char *source_addr) {
    kvOptions options = {0};
    KV_OPTIONS_SET_TCP(&options, ip, port);
    options.endpoint.tcp.source_addr = source_addr;
    return kvAsyncConnectWithOptions(&options);
}

kvAsyncContext *kvAsyncConnectBindWithReuse(const char *ip, int port,
                                                    const char *source_addr) {
    kvOptions options = {0};
    KV_OPTIONS_SET_TCP(&options, ip, port);
    options.options |= KV_OPT_REUSEADDR;
    options.endpoint.tcp.source_addr = source_addr;
    return kvAsyncConnectWithOptions(&options);
}

kvAsyncContext *kvAsyncConnectUnix(const char *path) {
    kvOptions options = {0};
    KV_OPTIONS_SET_UNIX(&options, path);
    return kvAsyncConnectWithOptions(&options);
}

int kvAsyncSetConnectCallback(kvAsyncContext *ac, kvConnectCallback *fn) {
    /* If already set, this is an error */
    if (ac->onConnect)
        return KV_ERR;

    ac->onConnect = fn;

    /* The common way to detect an established connection is to wait for
     * the first write event to be fired. This assumes the related event
     * library functions are already set. */
    _EL_ADD_WRITE(ac);

    return KV_OK;
}

int kvAsyncSetDisconnectCallback(kvAsyncContext *ac, kvDisconnectCallback *fn) {
    if (ac->onDisconnect == NULL) {
        ac->onDisconnect = fn;
        return KV_OK;
    }
    return KV_ERR;
}

/* Helper functions to push/shift callbacks */
static int kvPushCallback(kvCallbackList *list, kvCallback *source) {
    kvCallback *cb;

    /* Copy callback from stack to heap */
    cb = vk_malloc(sizeof(*cb));
    if (cb == NULL)
        return KV_ERR_OOM;

    /* Otherwise cb will remain uninitialized but will be saved in the list */
    assert(source != NULL);
    if (source != NULL) {
        memcpy(cb, source, sizeof(*cb));
        cb->next = NULL;
    }

    /* Store callback in list */
    if (list->head == NULL)
        list->head = cb;
    if (list->tail != NULL)
        list->tail->next = cb;
    list->tail = cb;
    return KV_OK;
}

static int kvShiftCallback(kvCallbackList *list, kvCallback *target) {
    kvCallback *cb = list->head;
    if (cb != NULL) {
        list->head = cb->next;
        if (cb == list->tail)
            list->tail = NULL;

        /* Copy callback from heap to stack */
        if (target != NULL)
            memcpy(target, cb, sizeof(*cb));
        vk_free(cb);
        return KV_OK;
    }
    return KV_ERR;
}

static void kvRunCallback(kvAsyncContext *ac, kvCallback *cb, kvReply *reply) {
    kvContext *c = &(ac->c);
    if (cb->fn != NULL) {
        c->flags |= KV_IN_CALLBACK;
        cb->fn(ac, reply, cb->privdata);
        c->flags &= ~KV_IN_CALLBACK;
    }
}

static void kvRunPushCallback(kvAsyncContext *ac, kvReply *reply) {
    if (ac->push_cb != NULL) {
        ac->c.flags |= KV_IN_CALLBACK;
        ac->push_cb(ac, reply);
        ac->c.flags &= ~KV_IN_CALLBACK;
    }
}

static void kvRunConnectCallback(kvAsyncContext *ac, int status) {
    if (ac->onConnect == NULL)
        return;

    if (!(ac->c.flags & KV_IN_CALLBACK)) {
        ac->c.flags |= KV_IN_CALLBACK;
        ac->onConnect(ac, status);
        ac->c.flags &= ~KV_IN_CALLBACK;
    } else {
        /* already in callback */
        ac->onConnect(ac, status);
    }
}

static void kvRunDisconnectCallback(kvAsyncContext *ac, int status) {
    if (ac->onDisconnect) {
        if (!(ac->c.flags & KV_IN_CALLBACK)) {
            ac->c.flags |= KV_IN_CALLBACK;
            ac->onDisconnect(ac, status);
            ac->c.flags &= ~KV_IN_CALLBACK;
        } else {
            /* already in callback */
            ac->onDisconnect(ac, status);
        }
    }
}

/* Helper function to free the context. */
static void kvAsyncFreeInternal(kvAsyncContext *ac) {
    kvContext *c = &(ac->c);
    kvCallback cb;
    dictIterator it;
    dictEntry *de;

    /* Execute pending callbacks with NULL reply. */
    while (kvShiftCallback(&ac->replies, &cb) == KV_OK)
        kvRunCallback(ac, &cb, NULL);
    while (kvShiftCallback(&ac->sub.replies, &cb) == KV_OK)
        kvRunCallback(ac, &cb, NULL);

    /* Run subscription callbacks with NULL reply */
    if (ac->sub.channels) {
        dictInitIterator(&it, ac->sub.channels);
        while ((de = dictNext(&it)) != NULL)
            kvRunCallback(ac, dictGetVal(de), NULL);

        dictRelease(ac->sub.channels);
    }

    if (ac->sub.patterns) {
        dictInitIterator(&it, ac->sub.patterns);
        while ((de = dictNext(&it)) != NULL)
            kvRunCallback(ac, dictGetVal(de), NULL);

        dictRelease(ac->sub.patterns);
    }

    if (ac->sub.schannels) {
        dictInitIterator(&it, ac->sub.schannels);
        while ((de = dictNext(&it)) != NULL)
            kvRunCallback(ac, dictGetVal(de), NULL);

        dictRelease(ac->sub.schannels);
    }

    /* Signal event lib to clean up */
    _EL_CLEANUP(ac);

    /* Execute disconnect callback. When kvAsyncFree() initiated destroying
     * this context, the status will always be KV_OK. */
    if (c->flags & KV_CONNECTED) {
        int status = ac->err == 0 ? KV_OK : KV_ERR;
        if (c->flags & KV_FREEING)
            status = KV_OK;
        kvRunDisconnectCallback(ac, status);
    }

    if (ac->dataCleanup) {
        ac->dataCleanup(ac->data);
    }

    /* Cleanup self */
    kvFree(c);
}

/* Free the async context. When this function is called from a callback,
 * control needs to be returned to kvProcessCallbacks() before actual
 * free'ing. To do so, a flag is set on the context which is picked up by
 * kvProcessCallbacks(). Otherwise, the context is immediately free'd. */
void kvAsyncFree(kvAsyncContext *ac) {
    if (ac == NULL)
        return;

    kvContext *c = &(ac->c);

    c->flags |= KV_FREEING;
    if (!(c->flags & KV_IN_CALLBACK))
        kvAsyncFreeInternal(ac);
}

/* Helper function to make the disconnect happen and clean up. */
void kvAsyncDisconnectInternal(kvAsyncContext *ac) {
    kvContext *c = &(ac->c);

    /* Make sure error is accessible if there is any */
    kvAsyncCopyError(ac);

    if (ac->err == 0) {
        /* For clean disconnects, there should be no pending callbacks. */
        int ret = kvShiftCallback(&ac->replies, NULL);
        assert(ret == KV_ERR);
    } else {
        /* Disconnection is caused by an error, make sure that pending
         * callbacks cannot call new commands. */
        c->flags |= KV_DISCONNECTING;
    }

    /* cleanup event library on disconnect.
     * this is safe to call multiple times */
    _EL_CLEANUP(ac);

    /* For non-clean disconnects, kvAsyncFreeInternal() will execute pending
     * callbacks with a NULL-reply. */
    if (!(c->flags & KV_NO_AUTO_FREE)) {
        kvAsyncFreeInternal(ac);
    }
}

/* Tries to do a clean disconnect from the server, meaning it stops new commands
 * from being issued, but tries to flush the output buffer and execute
 * callbacks for all remaining replies. When this function is called from a
 * callback, there might be more replies and we can safely defer disconnecting
 * to kvProcessCallbacks(). Otherwise, we can only disconnect immediately
 * when there are no pending callbacks. */
void kvAsyncDisconnect(kvAsyncContext *ac) {
    kvContext *c = &(ac->c);
    c->flags |= KV_DISCONNECTING;

    /** unset the auto-free flag here, because disconnect undoes this */
    c->flags &= ~KV_NO_AUTO_FREE;
    if (!(c->flags & KV_IN_CALLBACK) && ac->replies.head == NULL)
        kvAsyncDisconnectInternal(ac);
}

static int kvIsShardedVariant(const char *cstr) {
    return !strncasecmp("sm", cstr, 2) || /* smessage */
           !strncasecmp("ss", cstr, 2) || /* ssubscribe */
           !strncasecmp("sun", cstr, 3);  /* sunsubscribe */
}

static int kvGetSubscribeCallback(kvAsyncContext *ac, kvReply *reply, kvCallback *dstcb) {
    kvContext *c = &(ac->c);
    dict *callbacks;
    kvCallback *cb = NULL;
    dictEntry *de;
    int pvariant, svariant;
    char *stype;
    sds sname = NULL;

    /* Match reply with the expected format of a pushed message.
     * The type and number of elements (3 to 4) are specified at:
     * https://kv.io/docs/topics/pubsub/#format-of-pushed-messages */
    if ((reply->type == KV_REPLY_ARRAY && !(c->flags & KV_SUPPORTS_PUSH) && reply->elements >= 3) ||
        reply->type == KV_REPLY_PUSH) {
        assert(reply->element[0]->type == KV_REPLY_STRING);
        stype = reply->element[0]->str;
        pvariant = (tolower(stype[0]) == 'p') ? 1 : 0;
        svariant = kvIsShardedVariant(stype);

        callbacks = pvariant ? ac->sub.patterns :
                    svariant ? ac->sub.schannels :
                               ac->sub.channels;

        /* Locate the right callback */
        if (reply->element[1]->type == KV_REPLY_STRING) {
            sname = sdsnewlen(reply->element[1]->str, reply->element[1]->len);
            if (sname == NULL)
                goto oom;

            if ((de = dictFind(callbacks, sname)) != NULL) {
                cb = dictGetVal(de);
                memcpy(dstcb, cb, sizeof(*dstcb));
            }
        }

        /* If this is a subscribe reply decrease pending counter. */
        if (strcasecmp(stype + pvariant + svariant, "subscribe") == 0) {
            assert(cb != NULL);
            cb->pending_subs -= 1;
            cb->subscribed = 1;
        } else if (strcasecmp(stype + pvariant + svariant, "unsubscribe") == 0) {
            if (cb == NULL)
                ac->sub.pending_unsubs -= 1;
            else if (cb->pending_subs == 0)
                dictDelete(callbacks, sname);

            /* If this was the last unsubscribe message, revert to
             * non-subscribe mode. */
            assert(reply->element[2]->type == KV_REPLY_INTEGER);

            /* Unset subscribed flag only when no pipelined pending subscribe
             * or pending unsubscribe replies. */
            if (reply->element[2]->integer == 0 &&
                dictSize(ac->sub.channels) == 0 &&
                dictSize(ac->sub.patterns) == 0 &&
                dictSize(ac->sub.schannels) == 0 &&
                ac->sub.pending_unsubs == 0) {
                c->flags &= ~KV_SUBSCRIBED;

                /* Move ongoing regular command callbacks. */
                kvCallback cb;
                while (kvShiftCallback(&ac->sub.replies, &cb) == KV_OK) {
                    kvPushCallback(&ac->replies, &cb);
                }
            }
        }
        sdsfree(sname);
    } else {
        /* Shift callback for pending command in subscribed context. */
        kvShiftCallback(&ac->sub.replies, dstcb);
    }
    return KV_OK;
oom:
    kvSetError(&(ac->c), KV_ERR_OOM, "Out of memory");
    kvAsyncCopyError(ac);
    return KV_ERR;
}

#define kvIsSpontaneousPushReply(r) \
    (kvIsPushReply(r) && !kvIsSubscribeReply(r))

static int kvIsSubscribeReply(kvReply *reply) {
    char *str;
    size_t len, off;

    /* We will always have at least one string with the subscribe/message type */
    if (reply->elements < 1 || reply->element[0]->type != KV_REPLY_STRING ||
        reply->element[0]->len < sizeof("message") - 1) {
        return 0;
    }

    /* Get the string/len moving past 'p' if needed */
    off = tolower(reply->element[0]->str[0]) == 'p' || kvIsShardedVariant(reply->element[0]->str);
    str = reply->element[0]->str + off;
    len = reply->element[0]->len - off;

    return !strncasecmp(str, "subscribe", len) ||
           !strncasecmp(str, "message", len) ||
           !strncasecmp(str, "unsubscribe", len);
}

void kvProcessCallbacks(kvAsyncContext *ac) {
    kvContext *c = &(ac->c);
    void *reply = NULL;
    int status;

    while ((status = kvGetReply(c, &reply)) == KV_OK) {
        if (reply == NULL) {
            /* When the connection is being disconnected and there are
             * no more replies, this is the cue to really disconnect. */
            if (c->flags & KV_DISCONNECTING && sdslen(c->obuf) == 0 && ac->replies.head == NULL) {
                kvAsyncDisconnectInternal(ac);
                return;
            }
            /* When the connection is not being disconnected, simply stop
             * trying to get replies and wait for the next loop tick. */
            break;
        }

        /* Keep track of push message support for subscribe handling */
        if (kvIsPushReply(reply))
            c->flags |= KV_SUPPORTS_PUSH;

        /* Send any non-subscribe related PUSH messages to our PUSH handler
         * while allowing subscribe related PUSH messages to pass through.
         * This allows existing code to be backward compatible and work in
         * either RESP2 or RESP3 mode. */
        if (kvIsSpontaneousPushReply(reply)) {
            kvRunPushCallback(ac, reply);
            c->reader->fn->freeObject(reply);
            continue;
        }

        /* Even if the context is subscribed, pending regular
         * callbacks will get a reply before pub/sub messages arrive. */
        kvCallback cb = {NULL, NULL, 0, 0, NULL};
        if (kvShiftCallback(&ac->replies, &cb) != KV_OK) {
            /*
             * A spontaneous reply in a not-subscribed context can be the error
             * reply that is sent when a new connection exceeds the maximum
             * number of allowed connections on the server side.
             *
             * This is seen as an error instead of a regular reply because the
             * server closes the connection after sending it.
             *
             * To prevent the error from being overwritten by an EOF error the
             * connection is closed here. See issue #43.
             *
             * Another possibility is that the server is loading its dataset.
             * In this case we also want to close the connection, and have the
             * user wait until the server is ready to take our request.
             */
            if (((kvReply *)reply)->type != KV_REPLY_ERROR) {
                /* No more regular callbacks and no errors, the context *must* be subscribed. */
                assert(c->flags & KV_SUBSCRIBED);
                if (c->flags & KV_SUBSCRIBED)
                    kvGetSubscribeCallback(ac, reply, &cb);
            } else if (
                (c->flags & KV_SUBSCRIBED) && (((kvReply *)reply)->type == KV_REPLY_ERROR) && (strncmp(((kvReply *)reply)->str, "MOVED", 5) == 0 || strncmp(((kvReply *)reply)->str, "CROSSSLOT", 9) == 0) && kvShiftCallback(&ac->sub.replies, &cb) == KV_OK) {
                /* Ssubscribe error */
            } else {
                c->err = KV_ERR_OTHER;
                snprintf(c->errstr, sizeof(c->errstr), "%s", ((kvReply *)reply)->str);
                c->reader->fn->freeObject(reply);
                kvAsyncDisconnectInternal(ac);
                return;
            }
        }

        if (cb.fn != NULL) {
            kvRunCallback(ac, &cb, reply);
            if (!(c->flags & KV_NO_AUTO_FREE_REPLIES)) {
                c->reader->fn->freeObject(reply);
            }

            /* Proceed with free'ing when kvAsyncFree() was called. */
            if (c->flags & KV_FREEING) {
                kvAsyncFreeInternal(ac);
                return;
            }
        } else {
            /* No callback for this reply. This can either be a NULL callback,
             * or there were no callbacks to begin with. Either way, don't
             * abort with an error, but simply ignore it because the client
             * doesn't know what the server will spit out over the wire. */
            c->reader->fn->freeObject(reply);
        }

        /* If in monitor mode, repush the callback */
        if (c->flags & KV_MONITORING) {
            kvPushCallback(&ac->replies, &cb);
        }
    }

    /* Disconnect when there was an error reading the reply */
    if (status != KV_OK)
        kvAsyncDisconnectInternal(ac);
}

static void kvAsyncHandleConnectFailure(kvAsyncContext *ac) {
    kvRunConnectCallback(ac, KV_ERR);
    kvAsyncDisconnectInternal(ac);
}

/* Internal helper function to detect socket status the first time a read or
 * write event fires. When connecting was not successful, the connect callback
 * is called with a KV_ERR status and the context is free'd. */
static int kvAsyncHandleConnect(kvAsyncContext *ac) {
    int completed = 0;
    kvContext *c = &(ac->c);

    if (kvCheckConnectDone(c, &completed) == KV_ERR) {
        /* Error! */
        if (kvCheckSocketError(c) == KV_ERR)
            kvAsyncCopyError(ac);
        kvAsyncHandleConnectFailure(ac);
        return KV_ERR;
    } else if (completed == 1) {
        /* connected! */
        if (c->connection_type == KV_CONN_TCP &&
            kvSetTcpNoDelay(c) == KV_ERR) {
            kvAsyncHandleConnectFailure(ac);
            return KV_ERR;
        }

        /* flag us as fully connect, but allow the callback
         * to disconnect.  For that reason, permit the function
         * to delete the context here after callback return.
         */
        c->flags |= KV_CONNECTED;
        kvRunConnectCallback(ac, KV_OK);
        if ((ac->c.flags & KV_DISCONNECTING)) {
            kvAsyncDisconnect(ac);
            return KV_ERR;
        } else if ((ac->c.flags & KV_FREEING)) {
            kvAsyncFree(ac);
            return KV_ERR;
        }
        return KV_OK;
    } else {
        return KV_OK;
    }
}

void kvAsyncRead(kvAsyncContext *ac) {
    kvContext *c = &(ac->c);

    if (kvBufferRead(c) == KV_ERR) {
        kvAsyncDisconnectInternal(ac);
    } else {
        /* Always re-schedule reads */
        _EL_ADD_READ(ac);
        kvProcessCallbacks(ac);
    }
}

/* This function should be called when the socket is readable.
 * It processes all replies that can be read and executes their callbacks.
 */
void kvAsyncHandleRead(kvAsyncContext *ac) {
    kvContext *c = &(ac->c);
    /* must not be called from a callback */
    assert(!(c->flags & KV_IN_CALLBACK));

    if (!(c->flags & KV_CONNECTED)) {
        /* Abort connect was not successful. */
        if (kvAsyncHandleConnect(ac) != KV_OK)
            return;
        /* Try again later when the context is still not connected. */
        if (!(c->flags & KV_CONNECTED))
            return;
    }

    c->funcs->async_read(ac);
}

void kvAsyncWrite(kvAsyncContext *ac) {
    kvContext *c = &(ac->c);
    int done = 0;

    if (kvBufferWrite(c, &done) == KV_ERR) {
        kvAsyncDisconnectInternal(ac);
    } else {
        /* Continue writing when not done, stop writing otherwise */
        if (!done)
            _EL_ADD_WRITE(ac);
        else
            _EL_DEL_WRITE(ac);

        /* Always schedule reads after writes */
        _EL_ADD_READ(ac);
    }
}

void kvAsyncHandleWrite(kvAsyncContext *ac) {
    kvContext *c = &(ac->c);
    /* must not be called from a callback */
    assert(!(c->flags & KV_IN_CALLBACK));

    if (!(c->flags & KV_CONNECTED)) {
        /* Abort connect was not successful. */
        if (kvAsyncHandleConnect(ac) != KV_OK)
            return;
        /* Try again later when the context is still not connected. */
        if (!(c->flags & KV_CONNECTED))
            return;
    }

    c->funcs->async_write(ac);
}

void kvAsyncHandleTimeout(kvAsyncContext *ac) {
    kvContext *c = &(ac->c);
    kvCallback cb;
    /* must not be called from a callback */
    assert(!(c->flags & KV_IN_CALLBACK));

    if ((c->flags & KV_CONNECTED)) {
        if (ac->replies.head == NULL && ac->sub.replies.head == NULL) {
            /* Nothing to do - just an idle timeout */
            return;
        }

        if (!ac->c.command_timeout ||
            (!ac->c.command_timeout->tv_sec && !ac->c.command_timeout->tv_usec)) {
            /* A belated connect timeout arriving, ignore */
            return;
        }
    }

    if (!c->err) {
        kvSetError(c, KV_ERR_TIMEOUT, "Timeout");
        kvAsyncCopyError(ac);
    }

    if (!(c->flags & KV_CONNECTED)) {
        kvRunConnectCallback(ac, KV_ERR);
    }

    while (kvShiftCallback(&ac->replies, &cb) == KV_OK) {
        kvRunCallback(ac, &cb, NULL);
    }

    /**
     * TODO: Don't automatically sever the connection,
     * rather, allow to ignore <x> responses before the queue is clear
     */
    kvAsyncDisconnectInternal(ac);
}

/* Sets a pointer to the first argument and its length starting at p. Returns
 * the number of bytes to skip to get to the following argument. */
static const char *nextArgument(const char *start, const char **str, size_t *len) {
    const char *p = start;
    if (p[0] != '$') {
        p = strchr(p, '$');
        if (p == NULL)
            return NULL;
    }

    *len = (int)strtol(p + 1, NULL, 10);
    p = strchr(p, '\r');
    assert(p);
    *str = p + 2;
    return p + 2 + (*len) + 2;
}

void kvSsubscribeCallback(struct kvAsyncContext *ac, void *reply, void *privdata) {
    /*
      This callback called on the first reply from ssubscribe:
      - on successful subscription:
          iterate over all channels specified in original ssubscribe command, assign them user provided callback and mark as subscribed, then call original user callback.
      - on failed ssubscribe:
          iterate over all channels specified in original ssubscribe command, reduce pending_subs and remove all not subscribed callbacks
    */
    kvReply *r = reply;
    ssubscribeCallbackData *data = privdata;
    size_t clen, alen;
    const char *p, *cstr, *astr;
    sds sname;
    kvCallback *cb = NULL;
    dictEntry *de;

    assert(data != NULL);
    assert(data->command != NULL);
    assert(r != NULL);
    if (r->type == KV_REPLY_ERROR) {
        /*/ On CROSSSLOT, MOVED and other errors */
        p = nextArgument(data->command, &cstr, &clen);
        while ((p = nextArgument(p, &astr, &alen)) != NULL) {
            sname = sdsnewlen(astr, alen);
            if (sname == NULL)
                goto oom;

            if ((de = dictFind(ac->sub.schannels, sname)) != NULL) {
                cb = dictGetVal(de);
                if (cb != NULL) {
                    cb->pending_subs -= 1;
                    if (cb->pending_subs == 0 && !cb->subscribed) {
                        dictDelete(ac->sub.schannels, sname);
                    }
                }
            }
            sdsfree(sname);
        }
    } else {
        if ((r->type == KV_REPLY_ARRAY || r->type == KV_REPLY_PUSH) && strncasecmp(r->element[0]->str, "ssubscribe", 10) == 0) {
            p = nextArgument(data->command, &cstr, &clen);
            while ((p = nextArgument(p, &astr, &alen)) != NULL) {
                sname = sdsnewlen(astr, alen);
                if (sname == NULL)
                    goto oom;

                if ((de = dictFind(ac->sub.schannels, sname)) != NULL) {
                    cb = dictGetVal(de);
                    if (cb != NULL) {
                        cb->subscribed = 1;
                        cb->fn = data->user_callback;
                    }
                }
                sdsfree(sname);
            }
        }

        kvCallback cb = {0};
        kvGetSubscribeCallback(ac, reply, &cb);
        kvRunCallback(ac, &cb, reply);
        vk_free(data->command);
        vk_free(privdata);
        return;
    }

    data->user_callback(ac, reply, data->user_priv_data);
    vk_free(data->command);
    vk_free(privdata);
    return;
oom:
    sdsfree(sname);
    vk_free(data->command);
    vk_free(privdata);
}

/* Helper function for the kvAsyncCommand* family of functions. Writes a
 * formatted command to the output buffer and registers the provided callback
 * function with the context. */
static int kvAsyncAppendCmdLen(kvAsyncContext *ac, kvCallbackFn *fn, void *privdata, const char *cmd, size_t len) {
    kvContext *c = &(ac->c);
    kvCallback cb;
    struct dict *cbdict;
    dictIterator it;
    dictEntry *de;
    kvCallback *existcb;
    int pvariant, hasnext, hasprefix, svariant;
    const char *cstr, *astr;
    size_t clen, alen;
    const char *p;
    sds sname = NULL;
    ssubscribeCallbackData *ssubscribe_data = NULL;

    /* Don't accept new commands when the connection is about to be closed. */
    if (c->flags & (KV_DISCONNECTING | KV_FREEING))
        return KV_ERR;

    /* Setup callback */
    cb.fn = fn;
    cb.privdata = privdata;
    cb.pending_subs = 1;
    cb.unsubscribe_sent = 0;
    cb.subscribed = 0;

    /* Find out which command will be appended. */
    p = nextArgument(cmd, &cstr, &clen);
    assert(p != NULL);
    hasnext = (p[0] == '$');
    pvariant = (tolower(cstr[0]) == 'p') ? 1 : 0;
    svariant = kvIsShardedVariant(cstr);
    hasprefix = svariant || pvariant;
    cstr += hasprefix;
    clen -= hasprefix;

    if (hasnext && strncasecmp(cstr, "subscribe\r\n", 11) == 0) {
        int was_subscribed = c->flags & KV_SUBSCRIBED;
        c->flags |= KV_SUBSCRIBED;

        /* Add every channel/pattern to the list of subscription callbacks. */
        while ((p = nextArgument(p, &astr, &alen)) != NULL) {
            sname = sdsnewlen(astr, alen);
            if (sname == NULL)
                goto oom;

            cbdict = pvariant ? ac->sub.patterns :
                     svariant ? ac->sub.schannels :
                                ac->sub.channels;
            if (svariant) {
                cb.fn = NULL;
            }

            if ((de = dictFind(cbdict, sname)) != NULL) {
                existcb = dictGetVal(de);
                cb.pending_subs = existcb->pending_subs + 1;
                cb.subscribed = existcb->subscribed;
                cb.fn = existcb->fn;
            }

            /* Create a duplicate to be stored in dict. */
            kvCallback *dup = vk_malloc(sizeof(*dup));
            if (dup == NULL)
                goto oom;
            memcpy(dup, &cb, sizeof(*dup));

            if (dictReplace(cbdict, sname, dup) == 0)
                sdsfree(sname);
        }

        if (svariant) {
            ssubscribe_data = vk_malloc(sizeof(*ssubscribe_data));
            if (ssubscribe_data == NULL)
                goto oom;

            /* copy command to iterate over all channels.
             * actual length of cmd is actually len + 1 (see kvvFormatCommand).
             * last byte important in nextArgument function.
             */
            ssubscribe_data->command = vk_malloc(len + 1);
            if (ssubscribe_data->command == NULL)
                goto oom;
            memcpy(ssubscribe_data->command, cmd, len + 1);

            ssubscribe_data->user_callback = fn;
            ssubscribe_data->user_priv_data = privdata;

            cb.fn = &kvSsubscribeCallback;
            cb.privdata = ssubscribe_data;
            cb.pending_subs = 1;
            cb.unsubscribe_sent = 0;
            cb.subscribed = 1;
            if (was_subscribed) {
                if (kvPushCallback(&ac->sub.replies, &cb) != KV_OK)
                    goto oom;
            } else {
                if (kvPushCallback(&ac->replies, &cb) != KV_OK)
                    goto oom;
            }
        }
    } else if (strncasecmp(cstr, "unsubscribe\r\n", 13) == 0) {
        /* It is only useful to call (P)UNSUBSCRIBE when the context is
         * subscribed to one or more channels or patterns. */
        if (!(c->flags & KV_SUBSCRIBED))
            return KV_ERR;

        cbdict = pvariant ? ac->sub.patterns :
                 svariant ? ac->sub.schannels :
                            ac->sub.channels;

        if (hasnext) {
            /* Send an unsubscribe with specific channels/patterns.
             * Bookkeeping the number of expected replies */
            while ((p = nextArgument(p, &astr, &alen)) != NULL) {
                sname = sdsnewlen(astr, alen);
                if (sname == NULL)
                    goto oom;

                de = dictFind(cbdict, sname);
                if (de != NULL) {
                    existcb = dictGetVal(de);
                    if (existcb->unsubscribe_sent == 0)
                        existcb->unsubscribe_sent = 1;
                    else
                        /* Already sent, reply to be ignored */
                        ac->sub.pending_unsubs += 1;
                } else {
                    /* Not subscribed to, reply to be ignored */
                    ac->sub.pending_unsubs += 1;
                }
                sdsfree(sname);
            }
        } else {
            /* Send an unsubscribe without specific channels/patterns.
             * Bookkeeping the number of expected replies */
            int no_subs = 1;
            dictInitIterator(&it, cbdict);
            while ((de = dictNext(&it)) != NULL) {
                existcb = dictGetVal(de);
                if (existcb->unsubscribe_sent == 0) {
                    existcb->unsubscribe_sent = 1;
                    no_subs = 0;
                }
            }
            /* Unsubscribing to all channels/patterns, where none is
             * subscribed to, results in a single reply to be ignored. */
            if (no_subs == 1)
                ac->sub.pending_unsubs += 1;
        }

        /* (P)UNSUBSCRIBE does not have its own response: every channel or
         * pattern that is unsubscribed will receive a message. This means we
         * should not append a callback function for this command. */
    } else if (strncasecmp(cstr, "monitor\r\n", 9) == 0) {
        /* Set monitor flag and push callback */
        c->flags |= KV_MONITORING;
        if (kvPushCallback(&ac->replies, &cb) != KV_OK)
            goto oom;
    } else {
        if (c->flags & KV_SUBSCRIBED) {
            if (kvPushCallback(&ac->sub.replies, &cb) != KV_OK)
                goto oom;
        } else {
            if (kvPushCallback(&ac->replies, &cb) != KV_OK)
                goto oom;
        }
    }

    kvAppendCmdLen(c, cmd, len);

    /* Always schedule a write when the write buffer is non-empty */
    _EL_ADD_WRITE(ac);

    return KV_OK;
oom:
    kvSetError(&(ac->c), KV_ERR_OOM, "Out of memory");
    kvAsyncCopyError(ac);
    if (ssubscribe_data) {
        vk_free(ssubscribe_data->command);
        vk_free(ssubscribe_data);
    }
    return KV_ERR;
}

int kvvAsyncCommand(kvAsyncContext *ac, kvCallbackFn *fn, void *privdata, const char *format, va_list ap) {
    char *cmd;
    int len;
    int status;
    len = kvvFormatCommand(&cmd, format, ap);

    /* We don't want to pass -1 or -2 to future functions as a length. */
    if (len < 0)
        return KV_ERR;

    status = kvAsyncAppendCmdLen(ac, fn, privdata, cmd, len);
    vk_free(cmd);
    return status;
}

int kvAsyncCommand(kvAsyncContext *ac, kvCallbackFn *fn, void *privdata, const char *format, ...) {
    va_list ap;
    int status;
    va_start(ap, format);
    status = kvvAsyncCommand(ac, fn, privdata, format, ap);
    va_end(ap);
    return status;
}

int kvAsyncCommandArgv(kvAsyncContext *ac, kvCallbackFn *fn, void *privdata, int argc, const char **argv, const size_t *argvlen) {
    sds cmd;
    long long len;
    int status;
    len = kvFormatSdsCommandArgv(&cmd, argc, argv, argvlen);
    if (len < 0)
        return KV_ERR;
    status = kvAsyncAppendCmdLen(ac, fn, privdata, cmd, len);
    sdsfree(cmd);
    return status;
}

int kvAsyncFormattedCommand(kvAsyncContext *ac, kvCallbackFn *fn, void *privdata, const char *cmd, size_t len) {
    int status = kvAsyncAppendCmdLen(ac, fn, privdata, cmd, len);
    return status;
}

kvAsyncPushFn *kvAsyncSetPushCallback(kvAsyncContext *ac, kvAsyncPushFn *fn) {
    kvAsyncPushFn *old = ac->push_cb;
    ac->push_cb = fn;
    return old;
}

int kvAsyncSetTimeout(kvAsyncContext *ac, struct timeval tv) {
    if (!ac->c.command_timeout) {
        ac->c.command_timeout = vk_calloc(1, sizeof(tv));
        if (ac->c.command_timeout == NULL) {
            kvSetError(&ac->c, KV_ERR_OOM, "Out of memory");
            kvAsyncCopyError(ac);
            return KV_ERR;
        }
    }

    if (tv.tv_sec != ac->c.command_timeout->tv_sec ||
        tv.tv_usec != ac->c.command_timeout->tv_usec) {
        *ac->c.command_timeout = tv;
    }

    return KV_OK;
}
