/* This module is used to test the propagation (replication + AOF) of
 * commands, via the KVModule_Replicate() interface, in asynchronous
 * contexts, such as callbacks not implementing commands, and thread safe
 * contexts.
 *
 * We create a timer callback and a threads using a thread safe context.
 * Using both we try to propagate counters increments, and later we check
 * if the replica contains the changes as expected.
 *
 * -----------------------------------------------------------------------------
 *
 * Copyright (c) 2019, Redis Ltd.
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

#include "kvmodule.h"
#include <pthread.h>
#include <errno.h>

#define UNUSED(V) ((void) V)

KVModuleCtx *detached_ctx = NULL;

static int KeySpace_NotificationGeneric(KVModuleCtx *ctx, int type, const char *event, KVModuleString *key) {
    KVMODULE_NOT_USED(type);
    KVMODULE_NOT_USED(event);
    KVMODULE_NOT_USED(key);

    KVModuleCallReply* rep = KVModule_Call(ctx, "INCR", "c!", "notifications");
    KVModule_FreeCallReply(rep);

    return KVMODULE_OK;
}

/* Timer callback. */
void timerHandler(KVModuleCtx *ctx, void *data) {
    KVMODULE_NOT_USED(ctx);
    KVMODULE_NOT_USED(data);

    static int times = 0;

    KVModule_Replicate(ctx,"INCR","c","timer");
    times++;

    if (times < 3)
        KVModule_CreateTimer(ctx,100,timerHandler,NULL);
    else
        times = 0;
}

int propagateTestTimerCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModuleTimerID timer_id =
        KVModule_CreateTimer(ctx,100,timerHandler,NULL);
    KVMODULE_NOT_USED(timer_id);

    KVModule_ReplyWithSimpleString(ctx,"OK");
    return KVMODULE_OK;
}

/* Timer callback. */
void timerNestedHandler(KVModuleCtx *ctx, void *data) {
    int repl = (long long)data;

    /* The goal is the trigger a module command that calls RM_Replicate
     * in order to test MULTI/EXEC structure */
    KVModule_Replicate(ctx,"INCRBY","cc","timer-nested-start","1");
    KVModuleCallReply *reply = KVModule_Call(ctx,"propagate-test.nested", repl? "!" : "");
    KVModule_FreeCallReply(reply);
    reply = KVModule_Call(ctx, "INCR", repl? "c!" : "c", "timer-nested-middle");
    KVModule_FreeCallReply(reply);
    KVModule_Replicate(ctx,"INCRBY","cc","timer-nested-end","1");
}

int propagateTestTimerNestedCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModuleTimerID timer_id =
        KVModule_CreateTimer(ctx,100,timerNestedHandler,(void*)0);
    KVMODULE_NOT_USED(timer_id);

    KVModule_ReplyWithSimpleString(ctx,"OK");
    return KVMODULE_OK;
}

int propagateTestTimerNestedReplCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModuleTimerID timer_id =
        KVModule_CreateTimer(ctx,100,timerNestedHandler,(void*)1);
    KVMODULE_NOT_USED(timer_id);

    KVModule_ReplyWithSimpleString(ctx,"OK");
    return KVMODULE_OK;
}

void timerHandlerMaxmemory(KVModuleCtx *ctx, void *data) {
    KVMODULE_NOT_USED(ctx);
    KVMODULE_NOT_USED(data);

    KVModuleCallReply *reply = KVModule_Call(ctx,"SETEX","ccc!","timer-maxmemory-volatile-start","100","1");
    KVModule_FreeCallReply(reply);
    reply = KVModule_Call(ctx, "CONFIG", "ccc!", "SET", "maxmemory", "1");
    KVModule_FreeCallReply(reply);

    KVModule_Replicate(ctx, "INCR", "c", "timer-maxmemory-middle");

    reply = KVModule_Call(ctx,"SETEX","ccc!","timer-maxmemory-volatile-end","100","1");
    KVModule_FreeCallReply(reply);
}

int propagateTestTimerMaxmemoryCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModuleTimerID timer_id =
        KVModule_CreateTimer(ctx,100,timerHandlerMaxmemory,(void*)1);
    KVMODULE_NOT_USED(timer_id);

    KVModule_ReplyWithSimpleString(ctx,"OK");
    return KVMODULE_OK;
}

void timerHandlerEval(KVModuleCtx *ctx, void *data) {
    KVMODULE_NOT_USED(ctx);
    KVMODULE_NOT_USED(data);

    KVModuleCallReply *reply = KVModule_Call(ctx,"INCRBY","cc!","timer-eval-start","1");
    KVModule_FreeCallReply(reply);
    reply = KVModule_Call(ctx, "EVAL", "cccc!", "redis.call('set',KEYS[1],ARGV[1])", "1", "foo", "bar");
    KVModule_FreeCallReply(reply);

    KVModule_Replicate(ctx, "INCR", "c", "timer-eval-middle");

    reply = KVModule_Call(ctx,"INCRBY","cc!","timer-eval-end","1");
    KVModule_FreeCallReply(reply);
}

int propagateTestTimerEvalCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModuleTimerID timer_id =
        KVModule_CreateTimer(ctx,100,timerHandlerEval,(void*)1);
    KVMODULE_NOT_USED(timer_id);

    KVModule_ReplyWithSimpleString(ctx,"OK");
    return KVMODULE_OK;
}

/* The thread entry point. */
void *threadMain(void *arg) {
    KVMODULE_NOT_USED(arg);
    KVModuleCtx *ctx = KVModule_GetThreadSafeContext(NULL);
    KVModule_SelectDb(ctx,9); /* Tests ran in database number 9. */
    for (int i = 0; i < 3; i++) {
        KVModule_ThreadSafeContextLock(ctx);
        KVModule_Replicate(ctx,"INCR","c","a-from-thread");
        KVModuleCallReply *reply = KVModule_Call(ctx,"INCR","c!","thread-call");
        KVModule_FreeCallReply(reply);
        KVModule_Replicate(ctx,"INCR","c","b-from-thread");
        KVModule_ThreadSafeContextUnlock(ctx);
    }
    KVModule_FreeThreadSafeContext(ctx);
    return NULL;
}

int propagateTestThreadCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    pthread_t tid;
    if (pthread_create(&tid,NULL,threadMain,NULL) != 0)
        return KVModule_ReplyWithError(ctx,"-ERR Can't start thread");
    KVMODULE_NOT_USED(tid);

    KVModule_ReplyWithSimpleString(ctx,"OK");
    return KVMODULE_OK;
}

/* The thread entry point. */
void *threadDetachedMain(void *arg) {
    KVMODULE_NOT_USED(arg);
    KVModule_SelectDb(detached_ctx,9); /* Tests ran in database number 9. */

    KVModule_ThreadSafeContextLock(detached_ctx);
    KVModule_Replicate(detached_ctx,"INCR","c","thread-detached-before");
    KVModuleCallReply *reply = KVModule_Call(detached_ctx,"INCR","c!","thread-detached-1");
    KVModule_FreeCallReply(reply);
    reply = KVModule_Call(detached_ctx,"INCR","c!","thread-detached-2");
    KVModule_FreeCallReply(reply);
    KVModule_Replicate(detached_ctx,"INCR","c","thread-detached-after");
    KVModule_ThreadSafeContextUnlock(detached_ctx);

    return NULL;
}

int propagateTestDetachedThreadCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    pthread_t tid;
    if (pthread_create(&tid,NULL,threadDetachedMain,NULL) != 0)
        return KVModule_ReplyWithError(ctx,"-ERR Can't start thread");
    KVMODULE_NOT_USED(tid);

    KVModule_ReplyWithSimpleString(ctx,"OK");
    return KVMODULE_OK;
}

int propagateTestSimpleCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    /* Replicate two commands to test MULTI/EXEC wrapping. */
    KVModule_Replicate(ctx, "INCR", "c", "counter-1");
    KVModule_Replicate(ctx, "INCR", "c", "counter-2");
    KVModule_ReplyWithSimpleString(ctx,"OK");
    return KVMODULE_OK;
}

int propagateTestMixedCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    KVModuleCallReply *reply;

    /* This test mixes multiple propagation systems. */
    reply = KVModule_Call(ctx, "INCR", "c!", "using-call");
    KVModule_FreeCallReply(reply);

    KVModule_Replicate(ctx, "INCR", "c", "counter-1");
    KVModule_Replicate(ctx, "INCR", "c", "counter-2");

    reply = KVModule_Call(ctx, "INCR", "c!", "after-call");
    KVModule_FreeCallReply(reply);

    KVModule_ReplyWithSimpleString(ctx,"OK");
    return KVMODULE_OK;
}

int propagateTestNestedCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    KVModuleCallReply *reply;

    /* This test mixes multiple propagation systems. */
    reply = KVModule_Call(ctx, "INCR", "c!", "using-call");
    KVModule_FreeCallReply(reply);

    reply = KVModule_Call(ctx,"propagate-test.simple", "!");
    KVModule_FreeCallReply(reply);

    KVModule_Replicate(ctx,"INCR","c","counter-3");
    KVModule_Replicate(ctx,"INCR","c","counter-4");

    reply = KVModule_Call(ctx, "INCR", "c!", "after-call");
    KVModule_FreeCallReply(reply);

    reply = KVModule_Call(ctx, "INCR", "c!", "before-call-2");
    KVModule_FreeCallReply(reply);

    reply = KVModule_Call(ctx, "keyspace.incr_case1", "c!", "asdf"); /* Propagates INCR */
    KVModule_FreeCallReply(reply);

    reply = KVModule_Call(ctx, "keyspace.del_key_copy", "c!", "asdf"); /* Propagates DEL */
    KVModule_FreeCallReply(reply);

    reply = KVModule_Call(ctx, "INCR", "c!", "after-call-2");
    KVModule_FreeCallReply(reply);

    KVModule_ReplyWithSimpleString(ctx,"OK");
    return KVMODULE_OK;
}

/* Counter to track "propagate-test.incr" commands which were obeyed (due to being replicated or processed from AOF). */
static long long obeyed_cmds = 0;

/* Handles the "propagate-test.obeyed" command to return the `obeyed_cmds` count. */
int propagateTestObeyed(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    KVModule_ReplyWithLongLong(ctx, obeyed_cmds);
    return KVMODULE_OK;
}

int propagateTestIncr(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    KVMODULE_NOT_USED(argc);
    KVModuleCallReply *reply;

    /* Track the number of commands which are "obeyed". */
    if (KVModule_MustObeyClient(ctx)) {
        obeyed_cmds += 1;
    }
    /* This test propagates the module command, not the INCR it executes. */
    reply = KVModule_Call(ctx, "INCR", "s", argv[1]);
    KVModule_ReplyWithCallReply(ctx,reply);
    KVModule_FreeCallReply(reply);
    KVModule_ReplicateVerbatim(ctx);
    return KVMODULE_OK;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx,"propagate-test",1,KVMODULE_APIVER_1)
            == KVMODULE_ERR) return KVMODULE_ERR;

    detached_ctx = KVModule_GetDetachedThreadSafeContext(ctx);

    /* This option tests skip command validation for KVModule_Replicate */
    KVModule_SetModuleOptions(ctx, KVMODULE_OPTIONS_SKIP_COMMAND_VALIDATION);

    if (KVModule_SubscribeToKeyspaceEvents(ctx, KVMODULE_NOTIFY_ALL, KeySpace_NotificationGeneric) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"propagate-test.timer",
                propagateTestTimerCommand,
                "",1,1,1) == KVMODULE_ERR)
            return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"propagate-test.timer-nested",
                propagateTestTimerNestedCommand,
                "",1,1,1) == KVMODULE_ERR)
            return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"propagate-test.timer-nested-repl",
                propagateTestTimerNestedReplCommand,
                "",1,1,1) == KVMODULE_ERR)
            return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"propagate-test.timer-maxmemory",
                propagateTestTimerMaxmemoryCommand,
                "",1,1,1) == KVMODULE_ERR)
            return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"propagate-test.timer-eval",
                propagateTestTimerEvalCommand,
                "",1,1,1) == KVMODULE_ERR)
            return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"propagate-test.thread",
                propagateTestThreadCommand,
                "",1,1,1) == KVMODULE_ERR)
            return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"propagate-test.detached-thread",
                propagateTestDetachedThreadCommand,
                "",1,1,1) == KVMODULE_ERR)
            return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"propagate-test.simple",
                propagateTestSimpleCommand,
                "",1,1,1) == KVMODULE_ERR)
            return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"propagate-test.mixed",
                propagateTestMixedCommand,
                "write",1,1,1) == KVMODULE_ERR)
            return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"propagate-test.nested",
                propagateTestNestedCommand,
                "write",1,1,1) == KVMODULE_ERR)
            return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"propagate-test.incr",
                propagateTestIncr,
                "write",1,1,1) == KVMODULE_ERR)
            return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"propagate-test.obeyed",
                propagateTestObeyed,
                "",1,1,1) == KVMODULE_ERR)
            return KVMODULE_ERR;

    return KVMODULE_OK;
}

int KVModule_OnUnload(KVModuleCtx *ctx) {
    UNUSED(ctx);

    if (detached_ctx)
        KVModule_FreeThreadSafeContext(detached_ctx);

    return KVMODULE_OK;
}
