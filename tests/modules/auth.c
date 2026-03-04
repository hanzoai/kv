/* define macros for having usleep */
#define _BSD_SOURCE
#define _DEFAULT_SOURCE

#include "kvmodule.h"

#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define UNUSED(V) ((void) V)

// A simple global user
static KVModuleUser *global = NULL;
static long long client_change_delta = 0;

void UserChangedCallback(uint64_t client_id, void *privdata) {
    KVMODULE_NOT_USED(privdata);
    KVMODULE_NOT_USED(client_id);
    client_change_delta++;
}

int Auth_CreateModuleUser(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (global) {
        KVModule_FreeModuleUser(global);
    }

    global = KVModule_CreateModuleUser("global");
    KVModule_SetModuleUserACL(global, "allcommands");
    KVModule_SetModuleUserACL(global, "allkeys");
    KVModule_SetModuleUserACL(global, "on");

    return KVModule_ReplyWithSimpleString(ctx, "OK");
}

int Auth_AuthModuleUser(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    uint64_t client_id;
    KVModule_AuthenticateClientWithUser(ctx, global, UserChangedCallback, NULL, &client_id);

    return KVModule_ReplyWithLongLong(ctx, (uint64_t) client_id);
}

int Auth_AuthRealUser(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) return KVModule_WrongArity(ctx);

    size_t length;
    uint64_t client_id;

    KVModuleString *user_string = argv[1];
    const char *name = KVModule_StringPtrLen(user_string, &length);

    if (KVModule_AuthenticateClientWithACLUser(ctx, name, length, 
            UserChangedCallback, NULL, &client_id) == KVMODULE_ERR) {
        return KVModule_ReplyWithError(ctx, "Invalid user");   
    }

    return KVModule_ReplyWithLongLong(ctx, (uint64_t) client_id);
}

/* This command redacts every other arguments and returns OK */
int Auth_RedactedAPI(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    for(int i = argc - 1; i > 0; i -= 2) {
        int result = KVModule_RedactClientCommandArgument(ctx, i);
        KVModule_Assert(result == KVMODULE_OK);
    }
    return KVModule_ReplyWithSimpleString(ctx, "OK"); 
}

int Auth_ChangeCount(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    long long result = client_change_delta;
    client_change_delta = 0;
    return KVModule_ReplyWithLongLong(ctx, result);
}

/* The Module functionality below validates that module authentication callbacks can be registered
 * to support both non-blocking and blocking module based authentication. */

/* Non Blocking Module Auth callback / implementation. */
int auth_cb(KVModuleCtx *ctx, KVModuleString *username, KVModuleString *password, KVModuleString **err) {
    const char *user = KVModule_StringPtrLen(username, NULL);
    const char *pwd = KVModule_StringPtrLen(password, NULL);
    if (!strcmp(user,"foo") && !strcmp(pwd,"allow")) {
        KVModule_AuthenticateClientWithACLUser(ctx, "foo", 3, NULL, NULL, NULL);
        return KVMODULE_AUTH_HANDLED;
    }
    else if (!strcmp(user,"foo") && !strcmp(pwd,"deny")) {
        KVModuleString *log = KVModule_CreateString(ctx, "Module Auth", 11);
        KVModule_ACLAddLogEntryByUserName(ctx, username, log, KVMODULE_ACL_LOG_AUTH);
        KVModule_FreeString(ctx, log);
        const char *err_msg = "Auth denied by Misc Module.";
        *err = KVModule_CreateString(ctx, err_msg, strlen(err_msg));
        return KVMODULE_AUTH_HANDLED;
    }
    return KVMODULE_AUTH_NOT_HANDLED;
}

int test_rm_register_auth_cb(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    KVModule_RegisterAuthCallback(ctx, auth_cb);
    KVModule_ReplyWithSimpleString(ctx, "OK");
    return KVMODULE_OK;
}

/*
 * The thread entry point that actually executes the blocking part of the AUTH command.
 * This function sleeps for 0.5 seconds and then unblocks the client which will later call
 * `AuthBlock_Reply`.
 * `arg` is expected to contain the KVModuleBlockedClient, username, and password.
 */
void *AuthBlock_ThreadMain(void *arg) {
    usleep(500000);
    void **targ = arg;
    KVModuleBlockedClient *bc = targ[0];
    int result = 2;
    const char *user = KVModule_StringPtrLen(targ[1], NULL);
    const char *pwd = KVModule_StringPtrLen(targ[2], NULL);
    if (!strcmp(user,"foo") && !strcmp(pwd,"block_allow")) {
        result = 1;
    }
    else if (!strcmp(user,"foo") && !strcmp(pwd,"block_deny")) {
        result = 0;
    }
    else if (!strcmp(user,"foo") && !strcmp(pwd,"block_abort")) {
        KVModule_BlockedClientMeasureTimeEnd(bc);
        KVModule_AbortBlock(bc);
        goto cleanup;
    }
    /* Provide the result to the blocking reply cb. */
    void **replyarg = KVModule_Alloc(sizeof(void*));
    replyarg[0] = (void *) (uintptr_t) result;
    KVModule_BlockedClientMeasureTimeEnd(bc);
    KVModule_UnblockClient(bc, replyarg);
cleanup:
    /* Free the username and password and thread / arg data. */
    KVModule_FreeString(NULL, targ[1]);
    KVModule_FreeString(NULL, targ[2]);
    KVModule_Free(targ);
    return NULL;
}

/*
 * Reply callback for a blocking AUTH command. This is called when the client is unblocked.
 */
int AuthBlock_Reply(KVModuleCtx *ctx, KVModuleString *username, KVModuleString *password, KVModuleString **err) {
    KVMODULE_NOT_USED(password);
    void **targ = KVModule_GetBlockedClientPrivateData(ctx);
    int result = (uintptr_t) targ[0];
    size_t userlen = 0;
    const char *user = KVModule_StringPtrLen(username, &userlen);
    /* Handle the success case by authenticating. */
    if (result == 1) {
        KVModule_AuthenticateClientWithACLUser(ctx, user, userlen, NULL, NULL, NULL);
        return KVMODULE_AUTH_HANDLED;
    }
    /* Handle the Error case by denying auth */
    else if (result == 0) {
        KVModuleString *log = KVModule_CreateString(ctx, "Module Auth", 11);
        KVModule_ACLAddLogEntryByUserName(ctx, username, log, KVMODULE_ACL_LOG_AUTH);
        KVModule_FreeString(ctx, log);
        const char *err_msg = "Auth denied by Misc Module.";
        *err = KVModule_CreateString(ctx, err_msg, strlen(err_msg));
        return KVMODULE_AUTH_HANDLED;
    }
    /* "Skip" Authentication */
    return KVMODULE_AUTH_NOT_HANDLED;
}

/* Private data freeing callback for Module Auth. */
void AuthBlock_FreeData(KVModuleCtx *ctx, void *privdata) {
    KVMODULE_NOT_USED(ctx);
    KVModule_Free(privdata);
}

/* Callback triggered when the engine attempts module auth
 * Return code here is one of the following: Auth succeeded, Auth denied,
 * Auth not handled, Auth blocked.
 * The Module can have auth succeed / denied here itself, but this is an example
 * of blocking module auth.
 */
int blocking_auth_cb(KVModuleCtx *ctx, KVModuleString *username, KVModuleString *password, KVModuleString **err) {
    KVMODULE_NOT_USED(err);
    /* Block the client from the Module. */
    KVModuleBlockedClient *bc = KVModule_BlockClientOnAuth(ctx, AuthBlock_Reply, AuthBlock_FreeData);
    int ctx_flags = KVModule_GetContextFlags(ctx);
    if (ctx_flags & KVMODULE_CTX_FLAGS_MULTI || ctx_flags & KVMODULE_CTX_FLAGS_LUA) {
        /* Clean up by using KVModule_UnblockClient since we attempted blocking the client. */
        KVModule_UnblockClient(bc, NULL);
        return KVMODULE_AUTH_HANDLED;
    }
    KVModule_BlockedClientMeasureTimeStart(bc);
    pthread_t tid;
    /* Allocate memory for information needed. */
    void **targ = KVModule_Alloc(sizeof(void*)*3);
    targ[0] = bc;
    targ[1] = KVModule_CreateStringFromString(NULL, username);
    targ[2] = KVModule_CreateStringFromString(NULL, password);
    /* Create bg thread and pass the blockedclient, username and password to it. */
    if (pthread_create(&tid, NULL, AuthBlock_ThreadMain, targ) != 0) {
        KVModule_AbortBlock(bc);
    }
    return KVMODULE_AUTH_HANDLED;
}

int test_rm_register_blocking_auth_cb(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    KVModule_RegisterAuthCallback(ctx, blocking_auth_cb);
    KVModule_ReplyWithSimpleString(ctx, "OK");
    return KVMODULE_OK;
}

/* This function must be present on each module. It is used in order to
 * register the commands into the server. */
int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx,"testacl",1,KVMODULE_APIVER_1)
        == KVMODULE_ERR) return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"auth.authrealuser",
        Auth_AuthRealUser,"no-auth",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"auth.createmoduleuser",
        Auth_CreateModuleUser,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"auth.authmoduleuser",
        Auth_AuthModuleUser,"no-auth",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"auth.changecount",
        Auth_ChangeCount,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"auth.redact",
        Auth_RedactedAPI,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"testmoduleone.rm_register_auth_cb",
        test_rm_register_auth_cb,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"testmoduleone.rm_register_blocking_auth_cb",
        test_rm_register_blocking_auth_cb,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}

int KVModule_OnUnload(KVModuleCtx *ctx) {
    UNUSED(ctx);

    if (global)
        KVModule_FreeModuleUser(global);

    return KVMODULE_OK;
}
