#include "kvmodule.h"
#include <pthread.h>
#include <assert.h>

#define UNUSED(V) ((void) V)

KVModuleUser *user = NULL;

int call_without_user(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc < 2) {
        return KVModule_WrongArity(ctx);
    }

    const char *cmd = KVModule_StringPtrLen(argv[1], NULL);

    KVModuleCallReply *rep = KVModule_Call(ctx, cmd, "Ev", argv + 2, (size_t)argc - 2);
    if (!rep) {
        KVModule_ReplyWithError(ctx, "NULL reply returned");
    } else {
        KVModule_ReplyWithCallReply(ctx, rep);
        KVModule_FreeCallReply(rep);
    }
    return KVMODULE_OK;
}

int call_with_user_flag(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc < 3) {
        return KVModule_WrongArity(ctx);
    }

    KVModule_SetContextUser(ctx, user);

    /* Append Ev to the provided flags. */
    KVModuleString *flags = KVModule_CreateStringFromString(ctx, argv[1]);
    KVModule_StringAppendBuffer(ctx, flags, "Ev", 2);

    const char* flg = KVModule_StringPtrLen(flags, NULL);
    const char* cmd = KVModule_StringPtrLen(argv[2], NULL);

    KVModuleCallReply* rep = KVModule_Call(ctx, cmd, flg, argv + 3, (size_t)argc - 3);
    if (!rep) {
        KVModule_ReplyWithError(ctx, "NULL reply returned");
    } else {
        KVModule_ReplyWithCallReply(ctx, rep);
        KVModule_FreeCallReply(rep);
    }
    KVModule_FreeString(ctx, flags);

    return KVMODULE_OK;
}

int add_to_acl(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) {
        return KVModule_WrongArity(ctx);
    }

    size_t acl_len;
    const char *acl = KVModule_StringPtrLen(argv[1], &acl_len);

    KVModuleString *error;
    int ret = KVModule_SetModuleUserACLString(ctx, user, acl, &error);
    if (ret) {
        size_t len;
        const char * e = KVModule_StringPtrLen(error, &len);
        KVModule_ReplyWithError(ctx, e);
        return KVMODULE_OK;
    }

    KVModule_ReplyWithSimpleString(ctx, "OK");

    return KVMODULE_OK;
}

int get_acl(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);

    if (argc != 1) {
        return KVModule_WrongArity(ctx);
    }

    KVModule_Assert(user != NULL);

    KVModuleString *acl = KVModule_GetModuleUserACLString(user);

    KVModule_ReplyWithString(ctx, acl);

    KVModule_FreeString(NULL, acl);

    return KVMODULE_OK;
}

int reset_user(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);

    if (argc != 1) {
        return KVModule_WrongArity(ctx);
    }

    if (user != NULL) {
        KVModule_FreeModuleUser(user);
    }

    user = KVModule_CreateModuleUser("module_user");

    KVModule_ReplyWithSimpleString(ctx, "OK");

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

    // Set user
    KVModule_SetContextUser(ctx, user);

    // Call the command
    size_t format_len;
    KVModuleString *format_kv_str = KVModule_CreateString(NULL, "v", 1);
    const char *format = KVModule_StringPtrLen(bg->argv[1], &format_len);
    KVModule_StringAppendBuffer(NULL, format_kv_str, format, format_len);
    KVModule_StringAppendBuffer(NULL, format_kv_str, "E", 1);
    format = KVModule_StringPtrLen(format_kv_str, NULL);
    const char *cmd = KVModule_StringPtrLen(bg->argv[2], NULL);
    KVModuleCallReply *rep = KVModule_Call(ctx, cmd, format, bg->argv + 3, (size_t)bg->argc - 3);
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

int call_with_user_bg(KVModuleCtx *ctx, KVModuleString **argv, int argc)
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

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx,"usercall",1,KVMODULE_APIVER_1)== KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"usercall.call_without_user", call_without_user,"write",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"usercall.call_with_user_flag", call_with_user_flag,"write",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "usercall.call_with_user_bg", call_with_user_bg, "write", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "usercall.add_to_acl", add_to_acl, "write",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"usercall.reset_user", reset_user,"write",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"usercall.get_acl", get_acl,"write",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}

int KVModule_OnUnload(KVModuleCtx *ctx) {
    KVMODULE_NOT_USED(ctx);

    if (user != NULL) {
        KVModule_FreeModuleUser(user);
        user = NULL;
    }

    return KVMODULE_OK;
}
