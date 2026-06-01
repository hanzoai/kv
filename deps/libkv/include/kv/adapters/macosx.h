/*
 * Copyright (c) 2015 Дмитрий Бахвалов (Dmitry Bakhvalov)
 *
 * Permission for license update:
 *   https://github.com/redis/hiredis/issues/1271#issuecomment-2258225227
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

#ifndef KV_ADAPTERS_MACOSX_H
#define KV_ADAPTERS_MACOSX_H

#include "../async.h"
#include "../cluster.h"
#include "../kv.h"

#include <CoreFoundation/CoreFoundation.h>

typedef struct {
    kvAsyncContext *context;
    CFSocketRef socketRef;
    CFRunLoopSourceRef sourceRef;
} KVRunLoop;

static int freeKVRunLoop(KVRunLoop *kvRunLoop) {
    if (kvRunLoop != NULL) {
        if (kvRunLoop->sourceRef != NULL) {
            CFRunLoopSourceInvalidate(kvRunLoop->sourceRef);
            CFRelease(kvRunLoop->sourceRef);
        }
        if (kvRunLoop->socketRef != NULL) {
            CFSocketInvalidate(kvRunLoop->socketRef);
            CFRelease(kvRunLoop->socketRef);
        }
        vk_free(kvRunLoop);
    }
    return KV_ERR;
}

static void kvMacOSAddRead(void *privdata) {
    KVRunLoop *kvRunLoop = (KVRunLoop *)privdata;
    CFSocketEnableCallBacks(kvRunLoop->socketRef, kCFSocketReadCallBack);
}

static void kvMacOSDelRead(void *privdata) {
    KVRunLoop *kvRunLoop = (KVRunLoop *)privdata;
    CFSocketDisableCallBacks(kvRunLoop->socketRef, kCFSocketReadCallBack);
}

static void kvMacOSAddWrite(void *privdata) {
    KVRunLoop *kvRunLoop = (KVRunLoop *)privdata;
    CFSocketEnableCallBacks(kvRunLoop->socketRef, kCFSocketWriteCallBack);
}

static void kvMacOSDelWrite(void *privdata) {
    KVRunLoop *kvRunLoop = (KVRunLoop *)privdata;
    CFSocketDisableCallBacks(kvRunLoop->socketRef, kCFSocketWriteCallBack);
}

static void kvMacOSCleanup(void *privdata) {
    KVRunLoop *kvRunLoop = (KVRunLoop *)privdata;
    freeKVRunLoop(kvRunLoop);
}

static void kvMacOSAsyncCallback(CFSocketRef __unused s, CFSocketCallBackType callbackType, CFDataRef __unused address, const void __unused *data, void *info) {
    kvAsyncContext *context = (kvAsyncContext *)info;

    switch (callbackType) {
    case kCFSocketReadCallBack:
        kvAsyncHandleRead(context);
        break;

    case kCFSocketWriteCallBack:
        kvAsyncHandleWrite(context);
        break;

    default:
        break;
    }
}

static int kvMacOSAttach(kvAsyncContext *kvAsyncCtx, CFRunLoopRef runLoop) {
    kvContext *kvCtx = &(kvAsyncCtx->c);

    /* Nothing should be attached when something is already attached */
    if (kvAsyncCtx->ev.data != NULL)
        return KV_ERR;

    KVRunLoop *kvRunLoop = (KVRunLoop *)vk_calloc(1, sizeof(KVRunLoop));
    if (kvRunLoop == NULL)
        return KV_ERR;

    /* Setup kv stuff */
    kvRunLoop->context = kvAsyncCtx;

    kvAsyncCtx->ev.addRead = kvMacOSAddRead;
    kvAsyncCtx->ev.delRead = kvMacOSDelRead;
    kvAsyncCtx->ev.addWrite = kvMacOSAddWrite;
    kvAsyncCtx->ev.delWrite = kvMacOSDelWrite;
    kvAsyncCtx->ev.cleanup = kvMacOSCleanup;
    kvAsyncCtx->ev.data = kvRunLoop;

    /* Initialize and install read/write events */
    CFSocketContext socketCtx = {0, kvAsyncCtx, NULL, NULL, NULL};

    kvRunLoop->socketRef = CFSocketCreateWithNative(NULL, kvCtx->fd,
                                                        kCFSocketReadCallBack | kCFSocketWriteCallBack,
                                                        kvMacOSAsyncCallback,
                                                        &socketCtx);
    if (!kvRunLoop->socketRef)
        return freeKVRunLoop(kvRunLoop);

    kvRunLoop->sourceRef = CFSocketCreateRunLoopSource(NULL, kvRunLoop->socketRef, 0);
    if (!kvRunLoop->sourceRef)
        return freeKVRunLoop(kvRunLoop);

    CFRunLoopAddSource(runLoop, kvRunLoop->sourceRef, kCFRunLoopDefaultMode);

    return KV_OK;
}

/* Internal adapter function with correct function signature. */
static int kvMacOSAttachAdapter(kvAsyncContext *ac, void *loop) {
    return kvMacOSAttach(ac, (CFRunLoopRef)loop);
}

KV_UNUSED
static int kvClusterOptionsUseMacOS(kvClusterOptions *options,
                                        CFRunLoopRef loop) {
    if (options == NULL || loop == NULL) {
        return KV_ERR;
    }

    options->attach_fn = kvMacOSAttachAdapter;
    options->attach_data = loop;
    return KV_OK;
}

#endif /* KV_ADAPTERS_MACOSX_H */
