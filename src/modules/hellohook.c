/* Server hooks API example
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

#include "../kvmodule.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

/* Client state change callback. */
void clientChangeCallback(KVModuleCtx *ctx, KVModuleEvent e, uint64_t sub, void *data) {
    KVMODULE_NOT_USED(ctx);
    KVMODULE_NOT_USED(e);

    KVModuleClientInfo *ci = data;
    printf("Client %s event for client #%llu %s:%d\n",
           (sub == KVMODULE_SUBEVENT_CLIENT_CHANGE_CONNECTED) ? "connection" : "disconnection",
           (unsigned long long)ci->id, ci->addr, ci->port);
}

void flushdbCallback(KVModuleCtx *ctx, KVModuleEvent e, uint64_t sub, void *data) {
    KVMODULE_NOT_USED(ctx);
    KVMODULE_NOT_USED(e);

    KVModuleFlushInfo *fi = data;
    if (sub == KVMODULE_SUBEVENT_FLUSHDB_START) {
        if (fi->dbnum != -1) {
            KVModuleCallReply *reply;
            reply = KVModule_Call(ctx, "DBSIZE", "");
            long long numkeys = KVModule_CallReplyInteger(reply);
            printf("FLUSHDB event of database %d started (%lld keys in DB)\n", fi->dbnum, numkeys);
            KVModule_FreeCallReply(reply);
        } else {
            printf("FLUSHALL event started\n");
        }
    } else {
        if (fi->dbnum != -1) {
            printf("FLUSHDB event of database %d ended\n", fi->dbnum);
        } else {
            printf("FLUSHALL event ended\n");
        }
    }
}

void authenticationAttemptCallback(KVModuleCtx *ctx, KVModuleEvent e, uint64_t sub, void *data) {
    KVMODULE_NOT_USED(ctx);
    KVMODULE_NOT_USED(e);

    KVModuleAuthenticationInfo *ai = data;
    printf("Authentication attempt for client #%llu with username=%s module=%s success=%d\n",
           (unsigned long long)ai->client_id, ai->username, ai->module_name, ai->result == KVMODULE_AUTH_RESULT_GRANTED);
}

/* This function must be present on each module. It is used in order to
 * register the commands into the server. */
int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx, "hellohook", 1, KVMODULE_APIVER_1) == KVMODULE_ERR) return KVMODULE_ERR;

    KVModule_SubscribeToServerEvent(ctx, KVModuleEvent_ClientChange, clientChangeCallback);
    KVModule_SubscribeToServerEvent(ctx, KVModuleEvent_FlushDB, flushdbCallback);
    KVModule_SubscribeToServerEvent(ctx, KVModuleEvent_AuthenticationAttempt, authenticationAttemptCallback);
    return KVMODULE_OK;
}
