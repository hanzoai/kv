/* ACL API example - An example for performing custom synchronous and
 * asynchronous password authentication.
 *
 * -----------------------------------------------------------------------------
 *
 * Copyright 2019 Amazon.com, Inc. or its affiliates.
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

#include "../kvmodule.h"
#include <pthread.h>
#include <unistd.h>

// A simple global user
static KVModuleUser *global;
static uint64_t global_auth_client_id = 0;

/* HELLOACL.REVOKE
 * Synchronously revoke access from a user. */
int RevokeCommand_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (global_auth_client_id) {
        KVModule_DeauthenticateAndCloseClient(ctx, global_auth_client_id);
        return KVModule_ReplyWithSimpleString(ctx, "OK");
    } else {
        return KVModule_ReplyWithError(ctx, "Global user currently not used");
    }
}

/* HELLOACL.RESET
 * Synchronously delete and re-create a module user. */
int ResetCommand_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModule_FreeModuleUser(global);
    global = KVModule_CreateModuleUser("global");
    KVModule_SetModuleUserACL(global, "allcommands");
    KVModule_SetModuleUserACL(global, "allkeys");
    KVModule_SetModuleUserACL(global, "on");

    return KVModule_ReplyWithSimpleString(ctx, "OK");
}

/* Callback handler for user changes, use this to notify a module of
 * changes to users authenticated by the module */
void HelloACL_UserChanged(uint64_t client_id, void *privdata) {
    KVMODULE_NOT_USED(privdata);
    KVMODULE_NOT_USED(client_id);
    global_auth_client_id = 0;
}

/* HELLOACL.AUTHGLOBAL
 * Synchronously assigns a module user to the current context. */
int AuthGlobalCommand_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (global_auth_client_id) {
        return KVModule_ReplyWithError(ctx, "Global user currently used");
    }

    KVModule_AuthenticateClientWithUser(ctx, global, HelloACL_UserChanged, NULL, &global_auth_client_id);

    return KVModule_ReplyWithSimpleString(ctx, "OK");
}

#define TIMEOUT_TIME 1000

/* Reply callback for auth command HELLOACL.AUTHASYNC */
int HelloACL_Reply(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    size_t length;

    KVModuleString *user_string = KVModule_GetBlockedClientPrivateData(ctx);
    const char *name = KVModule_StringPtrLen(user_string, &length);

    if (KVModule_AuthenticateClientWithACLUser(ctx, name, length, NULL, NULL, NULL) == KVMODULE_ERR) {
        return KVModule_ReplyWithError(ctx, "Invalid Username or password");
    }
    return KVModule_ReplyWithSimpleString(ctx, "OK");
}

/* Timeout callback for auth command HELLOACL.AUTHASYNC */
int HelloACL_Timeout(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    return KVModule_ReplyWithSimpleString(ctx, "Request timedout");
}

/* Private data frees data for HELLOACL.AUTHASYNC command. */
void HelloACL_FreeData(KVModuleCtx *ctx, void *privdata) {
    KVMODULE_NOT_USED(ctx);
    KVModule_FreeString(NULL, privdata);
}

/* Background authentication can happen here. */
void *HelloACL_ThreadMain(void *args) {
    void **targs = args;
    KVModuleBlockedClient *bc = targs[0];
    KVModuleString *user = targs[1];
    KVModule_Free(targs);

    KVModule_UnblockClient(bc, user);
    return NULL;
}

/* HELLOACL.AUTHASYNC
 * Asynchronously assigns an ACL user to the current context. */
int AuthAsyncCommand_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) return KVModule_WrongArity(ctx);

    pthread_t tid;
    KVModuleBlockedClient *bc =
        KVModule_BlockClient(ctx, HelloACL_Reply, HelloACL_Timeout, HelloACL_FreeData, TIMEOUT_TIME);


    void **targs = KVModule_Alloc(sizeof(void *) * 2);
    targs[0] = bc;
    targs[1] = KVModule_CreateStringFromString(NULL, argv[1]);

    if (pthread_create(&tid, NULL, HelloACL_ThreadMain, targs) != 0) {
        KVModule_AbortBlock(bc);
        return KVModule_ReplyWithError(ctx, "-ERR Can't start thread");
    }

    return KVMODULE_OK;
}

/* This function must be present on each module. It is used in order to
 * register the commands into the server. */
int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx, "helloacl", 1, KVMODULE_APIVER_1) == KVMODULE_ERR) return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "helloacl.reset", ResetCommand_KVCommand, "", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "helloacl.revoke", RevokeCommand_KVCommand, "", 0, 0, 0) ==
        KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "helloacl.authglobal", AuthGlobalCommand_KVCommand, "no-auth", 0, 0, 0) ==
        KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "helloacl.authasync", AuthAsyncCommand_KVCommand, "no-auth", 0, 0, 0) ==
        KVMODULE_ERR)
        return KVMODULE_ERR;

    global = KVModule_CreateModuleUser("global");
    KVModule_SetModuleUserACL(global, "allcommands");
    KVModule_SetModuleUserACL(global, "allkeys");
    KVModule_SetModuleUserACL(global, "on");

    global_auth_client_id = 0;

    return KVMODULE_OK;
}
