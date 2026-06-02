/* define macros for having usleep */
#define _BSD_SOURCE
#define _DEFAULT_SOURCE
#include <string.h>
#include <unistd.h>

#include "kvmodule.h"
#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <strings.h>

#define UNUSED(V) ((void) V)

/* Forward declarations of module API functions not publicly exposed */
extern int VM_CallArgv(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc, int flags, const ValkeyModuleReplyHandlers *resp_handlers, void *reply_ctx);
extern int VM_CallArgvAbort(ValkeyModuleCallArgvBlockedHandle *handle);
extern int VM_ReplyRaw(ValkeyModuleCtx *ctx, const char *proto, size_t proto_len);
#define ValkeyModule_CallArgv VM_CallArgv
#define ValkeyModule_CallArgvAbort VM_CallArgvAbort
#define ValkeyModule_ReplyRaw VM_ReplyRaw

/* used to test processing events during slow bg operation */
static volatile int g_slow_bg_operation = 0;
static volatile int g_is_in_slow_bg_operation = 0;

void *sub_worker(void *arg) {
    // Get module context
    KVModuleCtx *ctx = (KVModuleCtx *)arg;

    // Try acquiring GIL
    int res = KVModule_ThreadSafeContextTryLock(ctx);

    // GIL is already taken by the calling thread expecting to fail.
    assert(res != KVMODULE_OK);

    return NULL;
}

void *worker(void *arg) {
    // Retrieve blocked client
    KVModuleBlockedClient *bc = (KVModuleBlockedClient *)arg;

    // Get module context
    KVModuleCtx *ctx = KVModule_GetThreadSafeContext(bc);

    // Acquire GIL
    KVModule_ThreadSafeContextLock(ctx);

    // Create another thread which will try to acquire the GIL
    pthread_t tid;
    int res = pthread_create(&tid, NULL, sub_worker, ctx);
    assert(res == 0);

    // Wait for thread
    pthread_join(tid, NULL);

    // Release GIL
    KVModule_ThreadSafeContextUnlock(ctx);

    // Reply to client
    KVModule_ReplyWithSimpleString(ctx, "OK");

    // Unblock client
    KVModule_UnblockClient(bc, NULL);

    // Free the module context
    KVModule_FreeThreadSafeContext(ctx);

    return NULL;
}

int acquire_gil(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    UNUSED(argv);
    UNUSED(argc);

    int flags = KVModule_GetContextFlags(ctx);
    int allFlags = KVModule_GetContextFlagsAll();
    if ((allFlags & KVMODULE_CTX_FLAGS_MULTI) &&
        (flags & KVMODULE_CTX_FLAGS_MULTI)) {
        KVModule_ReplyWithSimpleString(ctx, "Blocked client is not supported inside multi");
        return KVMODULE_OK;
    }

    if ((allFlags & KVMODULE_CTX_FLAGS_DENY_BLOCKING) &&
        (flags & KVMODULE_CTX_FLAGS_DENY_BLOCKING)) {
        KVModule_ReplyWithSimpleString(ctx, "Blocked client is not allowed");
        return KVMODULE_OK;
    }

    /* This command handler tries to acquire the GIL twice
     * once in the worker thread using "KVModule_ThreadSafeContextLock"
     * second in the sub-worker thread
     * using "KVModule_ThreadSafeContextTryLock"
     * as the GIL is already locked. */
    KVModuleBlockedClient *bc = KVModule_BlockClient(ctx, NULL, NULL, NULL, 0);

    pthread_t tid;
    int res = pthread_create(&tid, NULL, worker, bc);
    assert(res == 0);

    return KVMODULE_OK;
}

typedef struct {
    KVModuleString **argv;
    int argc;
    KVModuleBlockedClient *bc;
} bg_call_data;

void *bg_call_worker(void *arg) {
    bg_call_data *bg = arg;
    KVModuleBlockedClient *bc = bg->bc;

    // Get module context
    KVModuleCtx *ctx = KVModule_GetThreadSafeContext(bg->bc);

    // Acquire GIL
    KVModule_ThreadSafeContextLock(ctx);

    // Test slow operation yielding
    if (g_slow_bg_operation) {
        g_is_in_slow_bg_operation = 1;
        while (g_slow_bg_operation) {
            KVModule_Yield(ctx, KVMODULE_YIELD_FLAG_CLIENTS, "Slow module operation");
            usleep(1000);
        }
        g_is_in_slow_bg_operation = 0;
    }

    // Call the command
    const char *module_cmd = KVModule_StringPtrLen(bg->argv[0], NULL);
    int cmd_pos = 1;
    KVModuleString *format_kv_str = KVModule_CreateString(NULL, "v", 1);
    if (!strcasecmp(module_cmd, "do_bg_rm_call_format")) {
        cmd_pos = 2;
        size_t format_len;
        const char *format = KVModule_StringPtrLen(bg->argv[1], &format_len);
        KVModule_StringAppendBuffer(NULL, format_kv_str, format, format_len);
        KVModule_StringAppendBuffer(NULL, format_kv_str, "E", 1);
    }
    const char *format = KVModule_StringPtrLen(format_kv_str, NULL);
    const char *cmd = KVModule_StringPtrLen(bg->argv[cmd_pos], NULL);
    KVModuleCallReply *rep = KVModule_Call(ctx, cmd, format, bg->argv + cmd_pos + 1, (size_t)bg->argc - cmd_pos - 1);
    KVModule_FreeString(NULL, format_kv_str);

    /* Free the arguments within GIL to prevent simultaneous freeing in main thread. */
    for (int i=0; i<bg->argc; i++)
        KVModule_FreeString(ctx, bg->argv[i]);
    KVModule_Free(bg->argv);
    KVModule_Free(bg);

    // Release GIL
    KVModule_ThreadSafeContextUnlock(ctx);

    // Reply to client
    if (!rep) {
        KVModule_ReplyWithError(ctx, "NULL reply returned");
    } else {
        KVModule_ReplyWithCallReply(ctx, rep);
        KVModule_FreeCallReply(rep);
    }

    // Unblock client
    KVModule_UnblockClient(bc, NULL);

    // Free the module context
    KVModule_FreeThreadSafeContext(ctx);

    return NULL;
}

int do_bg_rm_call(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    UNUSED(argv);
    UNUSED(argc);

    /* Make sure we're not trying to block a client when we shouldn't */
    int flags = KVModule_GetContextFlags(ctx);
    int allFlags = KVModule_GetContextFlagsAll();
    if ((allFlags & KVMODULE_CTX_FLAGS_MULTI) &&
        (flags & KVMODULE_CTX_FLAGS_MULTI)) {
        KVModule_ReplyWithSimpleString(ctx, "Blocked client is not supported inside multi");
        return KVMODULE_OK;
    }
    if ((allFlags & KVMODULE_CTX_FLAGS_DENY_BLOCKING) &&
        (flags & KVMODULE_CTX_FLAGS_DENY_BLOCKING)) {
        KVModule_ReplyWithSimpleString(ctx, "Blocked client is not allowed");
        return KVMODULE_OK;
    }

    /* Make a copy of the arguments and pass them to the thread. */
    bg_call_data *bg = KVModule_Alloc(sizeof(bg_call_data));
    bg->argv = KVModule_Alloc(sizeof(KVModuleString*)*argc);
    bg->argc = argc;
    for (int i=0; i<argc; i++)
        bg->argv[i] = KVModule_HoldString(ctx, argv[i]);

    /* Block the client */
    bg->bc = KVModule_BlockClient(ctx, NULL, NULL, NULL, 0);

    /* Start a thread to handle the request */
    pthread_t tid;
    int res = pthread_create(&tid, NULL, bg_call_worker, bg);
    assert(res == 0);

    return KVMODULE_OK;
}

int do_rm_call(KVModuleCtx *ctx, KVModuleString **argv, int argc){
    UNUSED(argv);
    UNUSED(argc);

    if(argc < 2){
        return KVModule_WrongArity(ctx);
    }

    const char* cmd = KVModule_StringPtrLen(argv[1], NULL);

    KVModuleCallReply* rep = KVModule_Call(ctx, cmd, "Ev", argv + 2, (size_t)argc - 2);
    if(!rep){
        KVModule_ReplyWithError(ctx, "NULL reply returned");
    }else{
        KVModule_ReplyWithCallReply(ctx, rep);
        KVModule_FreeCallReply(rep);
    }

    return KVMODULE_OK;
}

static void rm_call_async_send_reply(KVModuleCtx *ctx, KVModuleCallReply *reply) {
    KVModule_ReplyWithCallReply(ctx, reply);
    KVModule_FreeCallReply(reply);
}

/* Called when the command that was blocked on 'RM_Call' gets unblocked
 * and send the reply to the blocked client. */
static void rm_call_async_on_unblocked(KVModuleCtx *ctx, KVModuleCallReply *reply, void *private_data) {
    UNUSED(ctx);
    KVModuleBlockedClient *bc = private_data;
    KVModuleCtx *bctx = KVModule_GetThreadSafeContext(bc);
    rm_call_async_send_reply(bctx, reply);
    KVModule_FreeThreadSafeContext(bctx);
    KVModule_UnblockClient(bc, KVModule_BlockClientGetPrivateData(bc));
}

int do_rm_call_async_fire_and_forget(KVModuleCtx *ctx, KVModuleString **argv, int argc){
    UNUSED(argv);
    UNUSED(argc);

    if(argc < 2){
        return KVModule_WrongArity(ctx);
    }
    const char* cmd = KVModule_StringPtrLen(argv[1], NULL);

    KVModuleCallReply* rep = KVModule_Call(ctx, cmd, "!KEv", argv + 2, (size_t)argc - 2);

    if(KVModule_CallReplyType(rep) != KVMODULE_REPLY_PROMISE) {
        KVModule_ReplyWithCallReply(ctx, rep);
    } else {
        KVModule_ReplyWithSimpleString(ctx, "Blocked");
    }
    KVModule_FreeCallReply(rep);

    return KVMODULE_OK;
}

static void do_rm_call_async_free_pd(KVModuleCtx * ctx, void *pd) {
    UNUSED(ctx);
    KVModule_FreeCallReply(pd);
}

static void do_rm_call_async_disconnect(KVModuleCtx *ctx, struct KVModuleBlockedClient *bc) {
    UNUSED(ctx);
    KVModuleCallReply* rep = KVModule_BlockClientGetPrivateData(bc);
    KVModule_CallReplyPromiseAbort(rep, NULL);
    KVModule_FreeCallReply(rep);
    KVModule_AbortBlock(bc);
}

/*
 * Callback for do_rm_call_async / do_rm_call_async_script_mode
 * Gets the command to invoke as the first argument to the command and runs it,
 * passing the rest of the arguments to the command invocation.
 * If the command got blocked, blocks the client and unblock it when the command gets unblocked,
 * this allows check the K (allow blocking) argument to RM_Call.
 */
int do_rm_call_async(KVModuleCtx *ctx, KVModuleString **argv, int argc){
    UNUSED(argv);
    UNUSED(argc);

    if(argc < 2){
        return KVModule_WrongArity(ctx);
    }

    size_t format_len = 0;
    char format[6] = {0};

    if (!(KVModule_GetContextFlags(ctx) & KVMODULE_CTX_FLAGS_DENY_BLOCKING)) {
        /* We are allowed to block the client so we can allow RM_Call to also block us */
        format[format_len++] = 'K';
    }

    const char* invoked_cmd = KVModule_StringPtrLen(argv[0], NULL);
    if (strcasecmp(invoked_cmd, "do_rm_call_async_script_mode") == 0) {
        format[format_len++] = 'S';
    }

    format[format_len++] = 'E';
    format[format_len++] = 'v';
    if (strcasecmp(invoked_cmd, "do_rm_call_async_no_replicate") != 0) {
        /* Notice, without the '!' flag we will have inconsistency between master and replica.
         * This is used only to check '!' flag correctness on blocked commands. */
        format[format_len++] = '!';
    }

    const char* cmd = KVModule_StringPtrLen(argv[1], NULL);

    KVModuleCallReply* rep = KVModule_Call(ctx, cmd, format, argv + 2, (size_t)argc - 2);

    if(KVModule_CallReplyType(rep) != KVMODULE_REPLY_PROMISE) {
        rm_call_async_send_reply(ctx, rep);
    } else {
        KVModuleBlockedClient *bc = KVModule_BlockClient(ctx, NULL, NULL, do_rm_call_async_free_pd, 0);
        KVModule_SetDisconnectCallback(bc, do_rm_call_async_disconnect);
        KVModule_BlockClientSetPrivateData(bc, rep);
        KVModule_CallReplyPromiseSetUnblockHandler(rep, rm_call_async_on_unblocked, bc);
    }

    return KVMODULE_OK;
}

typedef struct ThreadedAsyncRMCallCtx{
    KVModuleBlockedClient *bc;
    KVModuleCallReply *reply;
} ThreadedAsyncRMCallCtx;

void *send_async_reply(void *arg) {
    ThreadedAsyncRMCallCtx *ta_rm_call_ctx = arg;
    rm_call_async_on_unblocked(NULL, ta_rm_call_ctx->reply, ta_rm_call_ctx->bc);
    KVModule_Free(ta_rm_call_ctx);
    return NULL;
}

/* Called when the command that was blocked on 'RM_Call' gets unblocked
 * and schedule a thread to send the reply to the blocked client. */
static void rm_call_async_reply_on_thread(KVModuleCtx *ctx, KVModuleCallReply *reply, void *private_data) {
    UNUSED(ctx);
    ThreadedAsyncRMCallCtx *ta_rm_call_ctx = KVModule_Alloc(sizeof(*ta_rm_call_ctx));
    ta_rm_call_ctx->bc = private_data;
    ta_rm_call_ctx->reply = reply;
    pthread_t tid;
    int res = pthread_create(&tid, NULL, send_async_reply, ta_rm_call_ctx);
    assert(res == 0);
}

/*
 * Callback for do_rm_call_async_on_thread.
 * Gets the command to invoke as the first argument to the command and runs it,
 * passing the rest of the arguments to the command invocation.
 * If the command got blocked, blocks the client and unblock on a background thread.
 * this allows check the K (allow blocking) argument to RM_Call, and make sure that the reply
 * that passes to unblock handler is owned by the handler and are not attached to any
 * context that might be freed after the callback ends.
 */
int do_rm_call_async_on_thread(KVModuleCtx *ctx, KVModuleString **argv, int argc){
    UNUSED(argv);
    UNUSED(argc);

    if(argc < 2){
        return KVModule_WrongArity(ctx);
    }

    const char* cmd = KVModule_StringPtrLen(argv[1], NULL);

    KVModuleCallReply* rep = KVModule_Call(ctx, cmd, "KEv", argv + 2, (size_t)argc - 2);

    if(KVModule_CallReplyType(rep) != KVMODULE_REPLY_PROMISE) {
        rm_call_async_send_reply(ctx, rep);
    } else {
        KVModuleBlockedClient *bc = KVModule_BlockClient(ctx, NULL, NULL, NULL, 0);
        KVModule_CallReplyPromiseSetUnblockHandler(rep, rm_call_async_reply_on_thread, bc);
        KVModule_FreeCallReply(rep);
    }

    return KVMODULE_OK;
}

/* Private data for wait_and_do_rm_call_async that holds information about:
 * 1. the block client, to unblock when done.
 * 2. the arguments, contains the command to run using RM_Call */
typedef struct WaitAndDoRMCallCtx {
    KVModuleBlockedClient *bc;
    KVModuleString **argv;
    int argc;
} WaitAndDoRMCallCtx;

/*
 * This callback will be called when the 'wait' command invoke on 'wait_and_do_rm_call_async' will finish.
 * This callback will continue the execution flow just like 'do_rm_call_async' command.
 */
static void wait_and_do_rm_call_async_on_unblocked(KVModuleCtx *ctx, KVModuleCallReply *reply, void *private_data) {
    WaitAndDoRMCallCtx *wctx = private_data;
    if (KVModule_CallReplyType(reply) != KVMODULE_REPLY_INTEGER) {
        goto done;
    }

    if (KVModule_CallReplyInteger(reply) != 1) {
        goto done;
    }

    KVModule_FreeCallReply(reply);
    reply = NULL;

    const char* cmd = KVModule_StringPtrLen(wctx->argv[0], NULL);
    reply = KVModule_Call(ctx, cmd, "!EKv", wctx->argv + 1, (size_t)wctx->argc - 1);

done:
    if(KVModule_CallReplyType(reply) != KVMODULE_REPLY_PROMISE) {
        KVModuleCtx *bctx = KVModule_GetThreadSafeContext(wctx->bc);
        rm_call_async_send_reply(bctx, reply);
        KVModule_FreeThreadSafeContext(bctx);
        KVModule_UnblockClient(wctx->bc, NULL);
    } else {
        KVModule_CallReplyPromiseSetUnblockHandler(reply, rm_call_async_on_unblocked, wctx->bc);
        KVModule_FreeCallReply(reply);
    }
    for (int i = 0 ; i < wctx->argc ; ++i) {
        KVModule_FreeString(NULL, wctx->argv[i]);
    }
    KVModule_Free(wctx->argv);
    KVModule_Free(wctx);
}

/*
 * Callback for wait_and_do_rm_call
 * Gets the command to invoke as the first argument, runs 'wait'
 * command (using the K flag to RM_Call). Once the wait finished, runs the
 * command that was given (just like 'do_rm_call_async').
 */
int wait_and_do_rm_call_async(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);

    if(argc < 2){
        return KVModule_WrongArity(ctx);
    }

    int flags = KVModule_GetContextFlags(ctx);
    if (flags & KVMODULE_CTX_FLAGS_DENY_BLOCKING) {
        return KVModule_ReplyWithError(ctx, "Err can not run wait, blocking is not allowed.");
    }

    KVModuleCallReply* rep = KVModule_Call(ctx, "wait", "!EKcc", "1", "0");
    if(KVModule_CallReplyType(rep) != KVMODULE_REPLY_PROMISE) {
        rm_call_async_send_reply(ctx, rep);
    } else {
        KVModuleBlockedClient *bc = KVModule_BlockClient(ctx, NULL, NULL, NULL, 0);
        WaitAndDoRMCallCtx *wctx = KVModule_Alloc(sizeof(*wctx));
        *wctx = (WaitAndDoRMCallCtx){
                .bc = bc,
                .argv = KVModule_Alloc((argc - 1) * sizeof(KVModuleString*)),
                .argc = argc - 1,
        };

        for (int i = 1 ; i < argc ; ++i) {
            wctx->argv[i - 1] = KVModule_HoldString(NULL, argv[i]);
        }
        KVModule_CallReplyPromiseSetUnblockHandler(rep, wait_and_do_rm_call_async_on_unblocked, wctx);
        KVModule_FreeCallReply(rep);
    }

    return KVMODULE_OK;
}

static void blpop_and_set_multiple_keys_on_unblocked(KVModuleCtx *ctx, KVModuleCallReply *reply, void *private_data) {
    /* ignore the reply */
    KVModule_FreeCallReply(reply);
    WaitAndDoRMCallCtx *wctx = private_data;
    for (int i = 0 ; i < wctx->argc ; i += 2) {
        KVModuleCallReply* rep = KVModule_Call(ctx, "set", "!ss", wctx->argv[i], wctx->argv[i + 1]);
        KVModule_FreeCallReply(rep);
    }

    KVModuleCtx *bctx = KVModule_GetThreadSafeContext(wctx->bc);
    KVModule_ReplyWithSimpleString(bctx, "OK");
    KVModule_FreeThreadSafeContext(bctx);
    KVModule_UnblockClient(wctx->bc, NULL);

    for (int i = 0 ; i < wctx->argc ; ++i) {
        KVModule_FreeString(NULL, wctx->argv[i]);
    }
    KVModule_Free(wctx->argv);
    KVModule_Free(wctx);

}

/*
 * Performs a blpop command on a given list and when unblocked set multiple string keys.
 * This command allows checking that the unblock callback is performed as a unit
 * and its effect are replicated to the replica and AOF wrapped with multi exec.
 */
int blpop_and_set_multiple_keys(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);

    if(argc < 2 || argc % 2 != 0){
        return KVModule_WrongArity(ctx);
    }

    int flags = KVModule_GetContextFlags(ctx);
    if (flags & KVMODULE_CTX_FLAGS_DENY_BLOCKING) {
        return KVModule_ReplyWithError(ctx, "Err can not run wait, blocking is not allowed.");
    }

    KVModuleCallReply* rep = KVModule_Call(ctx, "blpop", "!EKsc", argv[1], "0");
    if(KVModule_CallReplyType(rep) != KVMODULE_REPLY_PROMISE) {
        rm_call_async_send_reply(ctx, rep);
    } else {
        KVModuleBlockedClient *bc = KVModule_BlockClient(ctx, NULL, NULL, NULL, 0);
        WaitAndDoRMCallCtx *wctx = KVModule_Alloc(sizeof(*wctx));
        *wctx = (WaitAndDoRMCallCtx){
                .bc = bc,
                .argv = KVModule_Alloc((argc - 2) * sizeof(KVModuleString*)),
                .argc = argc - 2,
        };

        for (int i = 0 ; i < argc - 2 ; ++i) {
            wctx->argv[i] = KVModule_HoldString(NULL, argv[i + 2]);
        }
        KVModule_CallReplyPromiseSetUnblockHandler(rep, blpop_and_set_multiple_keys_on_unblocked, wctx);
        KVModule_FreeCallReply(rep);
    }

    return KVMODULE_OK;
}

struct CallArgvAsyncContext {
    int is_async;
    ValkeyModuleCallArgvBlockedHandle *abort_handle;
    ValkeyModuleBlockedClient *bc;
};

static int call_argv_raw_resp_handler(void *ctx, ValkeyModuleCtx *mctx, const char *proto, size_t proto_len) {
    UNUSED(proto_len);

    struct CallArgvAsyncContext *actx = (struct CallArgvAsyncContext *)(void *)ctx;

    if (actx->bc) {
        ValkeyModuleCtx *bctx = ValkeyModule_GetThreadSafeContext(actx->bc);
        ValkeyModule_ReplyRaw(bctx, proto, proto_len);
        ValkeyModule_FreeThreadSafeContext(bctx);
        ValkeyModule_UnblockClient(actx->bc, ValkeyModule_BlockClientGetPrivateData(actx->bc));
    } else if (!actx->is_async) {
        ValkeyModule_ReplyRaw(mctx, proto, proto_len);
    } else {
        /* The client disconnected */
    }

    ValkeyModule_Free(actx);
    return 0; /* continue parsing */
}

static void do_vm_call_argv_async_disconnect(ValkeyModuleCtx *ctx, struct ValkeyModuleBlockedClient *bc) {
    UNUSED(ctx);
    struct CallArgvAsyncContext *actx = ValkeyModule_BlockClientGetPrivateData(bc);
    ValkeyModule_CallArgvAbort(actx->abort_handle);
    ValkeyModule_AbortBlock(bc);
    actx->bc = NULL;
    actx->abort_handle = NULL;
    ValkeyModule_Free(actx);
}

static void call_argv_deferred_reply_handle(void *ctx, ValkeyModuleCtx *mctx,
                                             ValkeyModuleCallArgvBlockedHandle *abort_handle) {
    struct CallArgvAsyncContext *actx = (struct CallArgvAsyncContext *)(void *)ctx;
    actx->is_async = 1;
    actx->abort_handle = abort_handle;

    if (!actx->bc) {
        ValkeyModuleBlockedClient *bc = ValkeyModule_BlockClient(mctx, NULL, NULL, NULL, 0);
        ValkeyModule_SetDisconnectCallback(bc, do_vm_call_argv_async_disconnect);
        ValkeyModule_BlockClientSetPrivateData(bc, ctx);
        actx->bc = bc;
    }
}

int do_vm_call_argv_async_aux(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc, int flags, ValkeyModuleBlockedClient *bc) {
    if(argc < 1){
        return ValkeyModule_WrongArity(ctx);
    }

    struct CallArgvAsyncContext *actx = ValkeyModule_Alloc(sizeof(*actx));
    *actx = (struct CallArgvAsyncContext){
        .is_async = 0,
        .abort_handle = NULL,
        .bc = bc,
    };

    ValkeyModuleReplyHandlers resp_handlers = {
        .version = VALKEYMODULE_REPLY_HANDLERS_VERSION,
        .onBlocked = call_argv_deferred_reply_handle,
        .onRespAvailable = call_argv_raw_resp_handler,
    };

     ValkeyModule_CallArgv(ctx, argv, (size_t)argc, flags, &resp_handlers, (void *)actx);
     return VALKEYMODULE_OK;
}


/*
 * Callback for do_vm_call_argv_async
 * Gets the command to invoke as the first argument to the command and runs it,
 * passing the rest of the arguments to the command invocation.
 * If the command got blocked, blocks the client and unblock it when the command gets unblocked,
 * this allows check the VALKEYMODULE_CALL_ARGV_ALLOW_BLOCK flag to VM_CallArgv.
 */
int do_vm_call_argv_async(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    int flags = VALKEYMODULE_CALL_ARGV_ERRORS_AS_REPLIES;

    const char *invoked_cmd = ValkeyModule_StringPtrLen(argv[0], NULL);
    if (strcasecmp(invoked_cmd, "do_vm_call_argv_async_no_replicate") != 0) {
        /* Notice, without the REPLICATE flag we will have inconsistency between
         * primary and replica. This is used only to test the flag's correctness
         * on blocked commands. */
        flags |= VALKEYMODULE_CALL_ARGV_REPLICATE;
    }

    if (strcasecmp(invoked_cmd, "do_vm_call_argv_async_script_mode") == 0) {
        flags |= VALKEYMODULE_CALL_ARGV_SCRIPT_MODE;
    }

    if (!(ValkeyModule_GetContextFlags(ctx) & VALKEYMODULE_CTX_FLAGS_DENY_BLOCKING)) {
        /* We are allowed to block the client so we can allow VM_CallArgv to also block us */
        flags |= VALKEYMODULE_CALL_ARGV_ALLOW_BLOCK;
    }

    return do_vm_call_argv_async_aux(ctx, argv + 1, argc - 1, flags, NULL);
}

int do_vm_call_argv_async_script_mode_available_reply_handle(void *ctx, ValkeyModuleCtx *mctx,
                                                             const char *proto, size_t proto_len) {
    UNUSED(ctx);
    ValkeyModule_ReplyRaw(mctx, proto, proto_len);
    return 0; /* continue parsing */
}

void do_vm_call_argv_async_script_mode_deferred_reply_handle(void *ctx, ValkeyModuleCtx *mctx,
                                                             ValkeyModuleCallArgvBlockedHandle *abort_handle) {
    UNUSED(ctx);
    UNUSED(abort_handle);
    ValkeyModule_ReplyWithSimpleString(mctx, "Blocked");
}

int do_vm_call_argv_async_fire_and_forget(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc){
    if(argc < 2){
        return ValkeyModule_WrongArity(ctx);
    }

    int flags = VALKEYMODULE_CALL_ARGV_ERRORS_AS_REPLIES |
        VALKEYMODULE_CALL_ARGV_REPLICATE |
        VALKEYMODULE_CALL_ARGV_ALLOW_BLOCK;

    ValkeyModuleReplyHandlers resp_handlers = {
        .version = VALKEYMODULE_REPLY_HANDLERS_VERSION,
        .onRespAvailable = do_vm_call_argv_async_script_mode_available_reply_handle,
        .onBlocked = do_vm_call_argv_async_script_mode_deferred_reply_handle,
    };

    ValkeyModule_CallArgv(ctx, argv + 1, (size_t)argc - 1, flags, &resp_handlers, NULL);
    return VALKEYMODULE_OK;
}

typedef struct ThreadedAsyncCallArgvCtx{
    ValkeyModuleBlockedClient *bc;
    char *proto;
    size_t proto_len;
} ThreadedAsyncCallArgvCtx;

void *send_call_argv_async_reply(void *arg) {
    ThreadedAsyncCallArgvCtx *ta_call_ctx = arg;
    ValkeyModuleCtx *bctx = ValkeyModule_GetThreadSafeContext(ta_call_ctx->bc);
    ValkeyModule_ReplyRaw(bctx, ta_call_ctx->proto, ta_call_ctx->proto_len);
    ValkeyModule_FreeThreadSafeContext(bctx);
    ValkeyModule_UnblockClient(ta_call_ctx->bc, ValkeyModule_BlockClientGetPrivateData(ta_call_ctx->bc));
    ValkeyModule_Free(ta_call_ctx->proto);
    ValkeyModule_Free(ta_call_ctx);
    return NULL;
}

static int call_argv_async_on_thread_available_handler(void *ctx, ValkeyModuleCtx *mctx, const char *proto, size_t proto_len) {
    UNUSED(proto_len);

    struct CallArgvAsyncContext *actx = (struct CallArgvAsyncContext *)(void *)ctx;

    if (actx->bc) {
        ThreadedAsyncCallArgvCtx *ta_call_ctx = ValkeyModule_Alloc(sizeof(*ta_call_ctx));
        ta_call_ctx->bc = actx->bc;
        ta_call_ctx->proto = ValkeyModule_Alloc(proto_len);
        strncpy(ta_call_ctx->proto, proto, proto_len);
        ta_call_ctx->proto_len = proto_len;
        pthread_t tid;
        int res = pthread_create(&tid, NULL, send_call_argv_async_reply, ta_call_ctx);
        assert(res == 0);
    } else if (!actx->is_async) {
        ValkeyModule_ReplyRaw(mctx, proto, proto_len);
    } else {
        /* The client disconnected */
    }

    ValkeyModule_Free(actx);
    return 0; /* continue parsing */
}

int do_vm_call_argv_async_on_thread(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    if(argc < 2){
        return ValkeyModule_WrongArity(ctx);
    }

    int flags = VALKEYMODULE_CALL_ARGV_ERRORS_AS_REPLIES |
        VALKEYMODULE_CALL_ARGV_ALLOW_BLOCK;

    struct CallArgvAsyncContext *actx = ValkeyModule_Alloc(sizeof(*actx));
    *actx = (struct CallArgvAsyncContext){
        .is_async = 0,
        .abort_handle = NULL,
        .bc = NULL,
    };

    ValkeyModuleReplyHandlers resp_handlers = {
        .version = VALKEYMODULE_REPLY_HANDLERS_VERSION,
        .onBlocked = call_argv_deferred_reply_handle,
        .onRespAvailable = call_argv_async_on_thread_available_handler,
    };

    ValkeyModule_CallArgv(ctx, argv + 1, (size_t)argc - 1, flags, &resp_handlers, actx);
    return VALKEYMODULE_OK;
}

struct WaitAndCallArgvAsyncContext {
    int is_async;
    ValkeyModuleBlockedClient *bc;
    ValkeyModuleString **argv;
    int argc;
};

int wait_and_call_argv_raw_resp_handler(void *ctx, ValkeyModuleCtx *mctx, const char *proto, size_t proto_len) {
    struct WaitAndCallArgvAsyncContext *actx = (struct WaitAndCallArgvAsyncContext *)ctx;

    if (actx->is_async) {
        int flags = VALKEYMODULE_CALL_ARGV_ALLOW_BLOCK |
            VALKEYMODULE_CALL_ARGV_ERRORS_AS_REPLIES |
            VALKEYMODULE_CALL_ARGV_REPLICATE;
        do_vm_call_argv_async_aux(mctx, actx->argv, actx->argc, flags, actx->bc);
    } else {
        ValkeyModule_ReplyRaw(mctx, proto, proto_len);
    }

    ValkeyModule_Free(actx);
    return 0; /* stop parsing */
}

void wait_and_call_argv_deferred_reply_handle(void *ctx, ValkeyModuleCtx *mctx,
                                               ValkeyModuleCallArgvBlockedHandle *abort_handle) {
    UNUSED(abort_handle);
    struct WaitAndCallArgvAsyncContext *actx = (struct WaitAndCallArgvAsyncContext *)ctx;
    ValkeyModuleBlockedClient *bc = ValkeyModule_BlockClient(mctx, NULL, NULL, NULL, 0);
    actx->is_async = 1;
    actx->bc = bc;
}

static void forward_call_to_unblock_handler(ValkeyModuleCtx *ctx,
                                            ValkeyModuleString **argv,
                                            int argc,
                                            ValkeyModuleString **fw_args,
                                            int fw_argc,
                                            int flags,
                                            const ValkeyModuleReplyHandlers *resp_handlers) {

    struct WaitAndCallArgvAsyncContext *actx = ValkeyModule_Alloc(sizeof(struct WaitAndCallArgvAsyncContext));
    *actx = (struct WaitAndCallArgvAsyncContext){
        .is_async = 0,
        .bc = NULL,
        .argv = fw_args,
        .argc = fw_argc,
    };

    ValkeyModule_CallArgv(ctx, argv, argc, flags, resp_handlers, actx);
}

int wait_and_do_vm_call_argv_async(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    if(argc < 2){
        return ValkeyModule_WrongArity(ctx);
    }

    ValkeyModuleString *wait_argv[3];
    wait_argv[0] = ValkeyModule_CreateString(NULL, "wait", 4);
    wait_argv[1] = ValkeyModule_CreateString(NULL, "1", 1);
    wait_argv[2] = ValkeyModule_CreateString(NULL, "0", 1);

    int flags = ValkeyModule_GetContextFlags(ctx);
    if (flags & VALKEYMODULE_CTX_FLAGS_DENY_BLOCKING) {
        return ValkeyModule_ReplyWithError(ctx, "Err can not run wait, blocking is not allowed.");
    }

    ValkeyModuleReplyHandlers resp_handlers = {
        .version = VALKEYMODULE_REPLY_HANDLERS_VERSION,
        .onBlocked = wait_and_call_argv_deferred_reply_handle,
        .onRespAvailable = wait_and_call_argv_raw_resp_handler,
    };

    flags = VALKEYMODULE_CALL_ARGV_ALLOW_BLOCK |
            VALKEYMODULE_CALL_ARGV_ERRORS_AS_REPLIES |
            VALKEYMODULE_CALL_ARGV_REPLICATE;

    forward_call_to_unblock_handler(
        ctx, (ValkeyModuleString **)wait_argv, 3, argv + 1, argc - 1, flags, &resp_handlers);

    ValkeyModule_FreeString(NULL, wait_argv[0]);
    ValkeyModule_FreeString(NULL, wait_argv[1]);
    ValkeyModule_FreeString(NULL, wait_argv[2]);

    return VALKEYMODULE_OK;
}

static int blpop_and_set_multiple_keys_on_available(void *ctx, ValkeyModuleCtx *mctx, const char *proto, size_t proto_len) {
    struct WaitAndCallArgvAsyncContext *actx = (struct WaitAndCallArgvAsyncContext *)(void *)ctx;

    if (actx->is_async) {
        int flags = VALKEYMODULE_CALL_ARGV_REPLICATE;

        ValkeyModuleString *set_argv[3];
        set_argv[0] = ValkeyModule_CreateString(NULL, "set", 3);

        for (int i = 0 ; i < actx->argc ; i += 2) {
            set_argv[1] = actx->argv[i];
            set_argv[2] = actx->argv[i + 1];
            ValkeyModule_CallArgv(mctx, set_argv, 3, flags, NULL, NULL);
        }

        ValkeyModule_FreeString(NULL, set_argv[0]);

        ValkeyModuleCtx *bctx = ValkeyModule_GetThreadSafeContext(actx->bc);
        ValkeyModule_ReplyWithSimpleString(bctx, "OK");
        ValkeyModule_FreeThreadSafeContext(bctx);
        ValkeyModule_UnblockClient(actx->bc, NULL);

    } else {
        ValkeyModule_ReplyRaw(mctx, proto, proto_len);
    }

    for (int i = 0 ; i < actx->argc ; i++) {
        ValkeyModule_FreeString(NULL, actx->argv[i]);
    }
    ValkeyModule_Free(actx->argv);
    ValkeyModule_Free(actx);
    return 0; /* stop parsing */
}

int blpop_and_set_multiple_keys_with_vm_call_argv(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    if(argc < 2){
        return ValkeyModule_WrongArity(ctx);
    }

    ValkeyModuleString *blpop_argv[3];
    blpop_argv[0] = ValkeyModule_CreateString(NULL, "blpop", 5);
    blpop_argv[1] = argv[1];
    blpop_argv[2] = ValkeyModule_CreateString(NULL, "0", 1);

    int flags = ValkeyModule_GetContextFlags(ctx);
    if (flags & VALKEYMODULE_CTX_FLAGS_DENY_BLOCKING) {
        return ValkeyModule_ReplyWithError(ctx, "Err can not run wait, blocking is not allowed.");
    }

    ValkeyModuleReplyHandlers resp_handlers = {
        .version = VALKEYMODULE_REPLY_HANDLERS_VERSION,
        .onBlocked = wait_and_call_argv_deferred_reply_handle,
        .onRespAvailable = blpop_and_set_multiple_keys_on_available,
    };

    flags = VALKEYMODULE_CALL_ARGV_ALLOW_BLOCK |
            VALKEYMODULE_CALL_ARGV_ERRORS_AS_REPLIES |
            VALKEYMODULE_CALL_ARGV_REPLICATE;

    ValkeyModuleString **fw_argv = ValkeyModule_Alloc((argc - 2) * sizeof(ValkeyModuleString*));
    for (int i = 0 ; i < argc - 2 ; ++i) {
        fw_argv[i] = ValkeyModule_HoldString(NULL, argv[i + 2]);
    }

    forward_call_to_unblock_handler(
        ctx, (ValkeyModuleString **)blpop_argv, 3, fw_argv, argc - 2, flags, &resp_handlers);

    ValkeyModule_FreeString(NULL, blpop_argv[0]);
    ValkeyModule_FreeString(NULL, blpop_argv[2]);

    return VALKEYMODULE_OK;
}

/* simulate a blocked client replying to a thread safe context without creating a thread */
int do_fake_bg_true(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);

    KVModuleBlockedClient *bc = KVModule_BlockClient(ctx, NULL, NULL, NULL, 0);
    KVModuleCtx *bctx = KVModule_GetThreadSafeContext(bc);

    KVModule_ReplyWithBool(bctx, 1);

    KVModule_FreeThreadSafeContext(bctx);
    KVModule_UnblockClient(bc, NULL);

    return KVMODULE_OK;
}


/* this flag is used to work with busy commands, that might take a while
 * and ability to stop the busy work with a different command*/
static volatile int abort_flag = 0;

int slow_fg_command(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }
    long long block_time = 0;
    if (KVModule_StringToLongLong(argv[1], &block_time) != KVMODULE_OK) {
        KVModule_ReplyWithError(ctx, "Invalid integer value");
        return KVMODULE_OK;
    }

    uint64_t start_time = KVModule_MonotonicMicroseconds();
    /* when not blocking indefinitely, we don't process client commands in this test. */
    int yield_flags = block_time? KVMODULE_YIELD_FLAG_NONE: KVMODULE_YIELD_FLAG_CLIENTS;
    while (!abort_flag) {
        KVModule_Yield(ctx, yield_flags, "Slow module operation");
        usleep(1000);
        if (block_time && KVModule_MonotonicMicroseconds() - start_time > (uint64_t)block_time)
            break;
    }

    abort_flag = 0;
    KVModule_ReplyWithLongLong(ctx, 1);
    return KVMODULE_OK;
}

int stop_slow_fg_command(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    abort_flag = 1;
    KVModule_ReplyWithLongLong(ctx, 1);
    return KVMODULE_OK;
}

/* used to enable or disable slow operation in do_bg_rm_call */
static int set_slow_bg_operation(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }
    long long ll;
    if (KVModule_StringToLongLong(argv[1], &ll) != KVMODULE_OK) {
        KVModule_ReplyWithError(ctx, "Invalid integer value");
        return KVMODULE_OK;
    }
    g_slow_bg_operation = ll;
    KVModule_ReplyWithSimpleString(ctx, "OK");
    return KVMODULE_OK;
}

/* used to test if we reached the slow operation in do_bg_rm_call */
static int is_in_slow_bg_operation(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    UNUSED(argv);
    if (argc != 1) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    KVModule_ReplyWithLongLong(ctx, g_is_in_slow_bg_operation);
    return KVMODULE_OK;
}

static void timer_callback(KVModuleCtx *ctx, void *data)
{
    UNUSED(ctx);

    KVModuleBlockedClient *bc = data;

    // Get module context
    KVModuleCtx *reply_ctx = KVModule_GetThreadSafeContext(bc);

    // Reply to client
    KVModule_ReplyWithSimpleString(reply_ctx, "OK");

    // Unblock client
    KVModule_UnblockClient(bc, NULL);

    // Free the module context
    KVModule_FreeThreadSafeContext(reply_ctx);
}

/* unblock_by_timer <period_ms> <timeout_ms>
 * period_ms is the period of the timer.
 * timeout_ms is the blocking timeout. */
int unblock_by_timer(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    if (argc != 3)
        return KVModule_WrongArity(ctx);

    long long period;
    long long timeout;
    if (KVModule_StringToLongLong(argv[1],&period) != KVMODULE_OK)
        return KVModule_ReplyWithError(ctx,"ERR invalid period");
    if (KVModule_StringToLongLong(argv[2],&timeout) != KVMODULE_OK) {
        return KVModule_ReplyWithError(ctx,"ERR invalid timeout");
    }

    KVModuleBlockedClient *bc = KVModule_BlockClient(ctx, NULL, NULL, NULL, timeout);
    KVModule_CreateTimer(ctx, period, timer_callback, bc);
    return KVMODULE_OK;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx, "blockedclient", 1, KVMODULE_APIVER_1)== KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "acquire_gil", acquire_gil, "", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "do_rm_call", do_rm_call,
                                  "write", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "do_rm_call_async", do_rm_call_async,
                                  "write", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "do_rm_call_async_on_thread", do_rm_call_async_on_thread,
                                      "write", 0, 0, 0) == KVMODULE_ERR)
            return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "do_rm_call_async_script_mode", do_rm_call_async,
                                  "write", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "do_rm_call_async_no_replicate", do_rm_call_async,
                                  "write", 0, 0, 0) == KVMODULE_ERR)
            return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "do_rm_call_fire_and_forget", do_rm_call_async_fire_and_forget,
                                  "write", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "wait_and_do_rm_call", wait_and_do_rm_call_async,
                                  "write", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "blpop_and_set_multiple_keys", blpop_and_set_multiple_keys,
                                      "write", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "do_bg_rm_call", do_bg_rm_call, "", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "do_bg_rm_call_format", do_bg_rm_call, "", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "do_fake_bg_true", do_fake_bg_true, "", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "slow_fg_command", slow_fg_command,"", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "stop_slow_fg_command", stop_slow_fg_command,"allow-busy", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "set_slow_bg_operation", set_slow_bg_operation, "allow-busy", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "is_in_slow_bg_operation", is_in_slow_bg_operation, "allow-busy", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "unblock_by_timer", unblock_by_timer, "", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}
