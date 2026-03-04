/* This module contains four tests :
 * 1- test.sanity    : Basic tests for argument validation mostly.
 * 2- test.sendbytes : Creates a pipe and registers its fds to the event loop,
 *                     one end of the pipe for read events and the other end for
 *                     the write events. On writable event, data is written. On
 *                     readable event data is read. Repeated until all data is
 *                     received.
 * 3- test.iteration : A test for BEFORE_SLEEP and AFTER_SLEEP callbacks.
 *                     Counters are incremented each time these events are
 *                     fired. They should be equal and increment monotonically.
 * 4- test.oneshot   : Test for oneshot API
 */

#include "kvmodule.h"
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <memory.h>
#include <errno.h>

int fds[2];
long long buf_size;
char *src;
long long src_offset;
char *dst;
long long dst_offset;

KVModuleBlockedClient *bc;
KVModuleCtx *reply_ctx;

void onReadable(int fd, void *user_data, int mask) {
    KVMODULE_NOT_USED(mask);

    KVModule_Assert(strcmp(user_data, "userdataread") == 0);

    while (1) {
        int rd = read(fd, dst + dst_offset, buf_size - dst_offset);
        if (rd <= 0)
            return;
        dst_offset += rd;

        /* Received all bytes */
        if (dst_offset == buf_size) {
            if (memcmp(src, dst, buf_size) == 0)
                KVModule_ReplyWithSimpleString(reply_ctx, "OK");
            else
                KVModule_ReplyWithError(reply_ctx, "ERR bytes mismatch");

            KVModule_EventLoopDel(fds[0], KVMODULE_EVENTLOOP_READABLE);
            KVModule_EventLoopDel(fds[1], KVMODULE_EVENTLOOP_WRITABLE);
            KVModule_Free(src);
            KVModule_Free(dst);
            close(fds[0]);
            close(fds[1]);

            KVModule_FreeThreadSafeContext(reply_ctx);
            KVModule_UnblockClient(bc, NULL);
            return;
        }
    };
}

void onWritable(int fd, void *user_data, int mask) {
    KVMODULE_NOT_USED(user_data);
    KVMODULE_NOT_USED(mask);

    KVModule_Assert(strcmp(user_data, "userdatawrite") == 0);

    while (1) {
        /* Check if we sent all data */
        if (src_offset >= buf_size)
            return;
        int written = write(fd, src + src_offset, buf_size - src_offset);
        if (written <= 0) {
            return;
        }

        src_offset += written;
    };
}

/* Create a pipe(), register pipe fds to the event loop and send/receive data
 * using them. */
int sendbytes(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    if (KVModule_StringToLongLong(argv[1], &buf_size) != KVMODULE_OK ||
        buf_size == 0) {
        KVModule_ReplyWithError(ctx, "Invalid integer value");
        return KVMODULE_OK;
    }

    bc = KVModule_BlockClient(ctx, NULL, NULL, NULL, 0);
    reply_ctx = KVModule_GetThreadSafeContext(bc);

    /* Allocate source buffer and write some random data */
    src = KVModule_Calloc(1,buf_size);
    src_offset = 0;
    memset(src, rand() % 0xFF, buf_size);
    memcpy(src, "randomtestdata", strlen("randomtestdata"));

    dst = KVModule_Calloc(1,buf_size);
    dst_offset = 0;

    /* Create a pipe and register it to the event loop. */
    if (pipe(fds) < 0) return KVMODULE_ERR;
    if (fcntl(fds[0], F_SETFL, O_NONBLOCK) < 0) return KVMODULE_ERR;
    if (fcntl(fds[1], F_SETFL, O_NONBLOCK) < 0) return KVMODULE_ERR;

    if (KVModule_EventLoopAdd(fds[0], KVMODULE_EVENTLOOP_READABLE,
        onReadable, "userdataread") != KVMODULE_OK) return KVMODULE_ERR;
    if (KVModule_EventLoopAdd(fds[1], KVMODULE_EVENTLOOP_WRITABLE,
        onWritable, "userdatawrite") != KVMODULE_OK) return KVMODULE_ERR;
    return KVMODULE_OK;
}

int sanity(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (pipe(fds) < 0) return KVMODULE_ERR;

    if (KVModule_EventLoopAdd(fds[0], 9999999, onReadable, NULL)
        == KVMODULE_OK || errno != EINVAL) {
        KVModule_ReplyWithError(ctx, "ERR non-existing event type should fail");
        goto out;
    }
    if (KVModule_EventLoopAdd(-1, KVMODULE_EVENTLOOP_READABLE, onReadable, NULL)
        == KVMODULE_OK || errno != ERANGE) {
        KVModule_ReplyWithError(ctx, "ERR out of range fd should fail");
        goto out;
    }
    if (KVModule_EventLoopAdd(99999999, KVMODULE_EVENTLOOP_READABLE, onReadable, NULL)
        == KVMODULE_OK || errno != ERANGE) {
        KVModule_ReplyWithError(ctx, "ERR out of range fd should fail");
        goto out;
    }
    if (KVModule_EventLoopAdd(fds[0], KVMODULE_EVENTLOOP_READABLE, NULL, NULL)
        == KVMODULE_OK || errno != EINVAL) {
        KVModule_ReplyWithError(ctx, "ERR null callback should fail");
        goto out;
    }
    if (KVModule_EventLoopAdd(fds[0], 9999999, onReadable, NULL)
        == KVMODULE_OK || errno != EINVAL) {
        KVModule_ReplyWithError(ctx, "ERR non-existing event type should fail");
        goto out;
    }
    if (KVModule_EventLoopDel(fds[0], KVMODULE_EVENTLOOP_READABLE)
        != KVMODULE_OK || errno != 0) {
        KVModule_ReplyWithError(ctx, "ERR del on non-registered fd should not fail");
        goto out;
    }
    if (KVModule_EventLoopDel(fds[0], 9999999) == KVMODULE_OK ||
        errno != EINVAL) {
        KVModule_ReplyWithError(ctx, "ERR non-existing event type should fail");
        goto out;
    }
    if (KVModule_EventLoopDel(-1, KVMODULE_EVENTLOOP_READABLE)
        == KVMODULE_OK || errno != ERANGE) {
        KVModule_ReplyWithError(ctx, "ERR out of range fd should fail");
        goto out;
    }
    if (KVModule_EventLoopDel(99999999, KVMODULE_EVENTLOOP_READABLE)
        == KVMODULE_OK || errno != ERANGE) {
        KVModule_ReplyWithError(ctx, "ERR out of range fd should fail");
        goto out;
    }
    if (KVModule_EventLoopAdd(fds[0], KVMODULE_EVENTLOOP_READABLE, onReadable, NULL)
        != KVMODULE_OK || errno != 0) {
        KVModule_ReplyWithError(ctx, "ERR Add failed");
        goto out;
    }
    if (KVModule_EventLoopAdd(fds[0], KVMODULE_EVENTLOOP_READABLE, onReadable, NULL)
        != KVMODULE_OK || errno != 0) {
        KVModule_ReplyWithError(ctx, "ERR Adding same fd twice failed");
        goto out;
    }
    if (KVModule_EventLoopDel(fds[0], KVMODULE_EVENTLOOP_READABLE)
        != KVMODULE_OK || errno != 0) {
        KVModule_ReplyWithError(ctx, "ERR Del failed");
        goto out;
    }
    if (KVModule_EventLoopAddOneShot(NULL, NULL) == KVMODULE_OK || errno != EINVAL) {
        KVModule_ReplyWithError(ctx, "ERR null callback should fail");
        goto out;
    }

    KVModule_ReplyWithSimpleString(ctx, "OK");
out:
    close(fds[0]);
    close(fds[1]);
    return KVMODULE_OK;
}

static long long beforeSleepCount;
static long long afterSleepCount;

int iteration(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    /* On each event loop iteration, eventloopCallback() is called. We increment
     * beforeSleepCount and afterSleepCount, so these two should be equal.
     * We reply with iteration count, caller can test if iteration count
     * increments monotonically */
    KVModule_Assert(beforeSleepCount == afterSleepCount);
    KVModule_ReplyWithLongLong(ctx, beforeSleepCount);
    return KVMODULE_OK;
}

void oneshotCallback(void* arg)
{
    KVModule_Assert(strcmp(arg, "userdata") == 0);
    KVModule_ReplyWithSimpleString(reply_ctx, "OK");
    KVModule_FreeThreadSafeContext(reply_ctx);
    KVModule_UnblockClient(bc, NULL);
}

int oneshot(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    bc = KVModule_BlockClient(ctx, NULL, NULL, NULL, 0);
    reply_ctx = KVModule_GetThreadSafeContext(bc);

    if (KVModule_EventLoopAddOneShot(oneshotCallback, "userdata") != KVMODULE_OK) {
        KVModule_ReplyWithError(ctx, "ERR oneshot failed");
        KVModule_FreeThreadSafeContext(reply_ctx);
        KVModule_UnblockClient(bc, NULL);
    }
    return KVMODULE_OK;
}

void eventloopCallback(struct KVModuleCtx *ctx, KVModuleEvent eid, uint64_t subevent, void *data) {
    KVMODULE_NOT_USED(ctx);
    KVMODULE_NOT_USED(eid);
    KVMODULE_NOT_USED(subevent);
    KVMODULE_NOT_USED(data);

    KVModule_Assert(eid.id == KVMODULE_EVENT_EVENTLOOP);
    if (subevent == KVMODULE_SUBEVENT_EVENTLOOP_BEFORE_SLEEP)
        beforeSleepCount++;
    else if (subevent == KVMODULE_SUBEVENT_EVENTLOOP_AFTER_SLEEP)
        afterSleepCount++;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx,"eventloop",1,KVMODULE_APIVER_1)
        == KVMODULE_ERR) return KVMODULE_ERR;

    /* Test basics. */
    if (KVModule_CreateCommand(ctx, "test.sanity", sanity, "", 0, 0, 0)
        == KVMODULE_ERR) return KVMODULE_ERR;

    /* Register a command to create a pipe() and send data through it by using
     * event loop API. */
    if (KVModule_CreateCommand(ctx, "test.sendbytes", sendbytes, "", 0, 0, 0)
        == KVMODULE_ERR) return KVMODULE_ERR;

    /* Register a command to return event loop iteration count. */
    if (KVModule_CreateCommand(ctx, "test.iteration", iteration, "", 0, 0, 0)
        == KVMODULE_ERR) return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "test.oneshot", oneshot, "", 0, 0, 0)
        == KVMODULE_ERR) return KVMODULE_ERR;

    if (KVModule_SubscribeToServerEvent(ctx, KVModuleEvent_EventLoop,
        eventloopCallback) != KVMODULE_OK) return KVMODULE_ERR;

    return KVMODULE_OK;
}
