/* Helloworld cluster -- A ping/pong cluster API example.
 *
 * -----------------------------------------------------------------------------
 *
 * Copyright (c) 2018, Redis Ltd.
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

#define MSGTYPE_PING 1
#define MSGTYPE_PONG 2

/* HELLOCLUSTER.PINGALL */
int PingallCommand_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModule_SendClusterMessage(ctx, NULL, MSGTYPE_PING, "Hey", 3);
    return KVModule_ReplyWithSimpleString(ctx, "OK");
}

/* HELLOCLUSTER.LIST */
int ListCommand_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    size_t numnodes;
    char **ids = KVModule_GetClusterNodesList(ctx, &numnodes);
    if (ids == NULL) {
        return KVModule_ReplyWithError(ctx, "Cluster not enabled");
    }

    KVModule_ReplyWithArray(ctx, numnodes);
    for (size_t j = 0; j < numnodes; j++) {
        int port;
        KVModule_GetClusterNodeInfo(ctx, ids[j], NULL, NULL, &port, NULL);
        KVModule_ReplyWithArray(ctx, 2);
        KVModule_ReplyWithStringBuffer(ctx, ids[j], KVMODULE_NODE_ID_LEN);
        KVModule_ReplyWithLongLong(ctx, port);
    }
    KVModule_FreeClusterNodesList(ids);
    return KVMODULE_OK;
}

/* Callback for message MSGTYPE_PING */
void PingReceiver(KVModuleCtx *ctx,
                  const char *sender_id,
                  uint8_t type,
                  const unsigned char *payload,
                  uint32_t len) {
    KVModule_Log(ctx, "notice", "PING (type %d) RECEIVED from %.*s: '%.*s'", type, KVMODULE_NODE_ID_LEN,
                     sender_id, (int)len, payload);
    KVModule_SendClusterMessage(ctx, NULL, MSGTYPE_PONG, "Ohi!", 4);
    KVModuleCallReply *reply = KVModule_Call(ctx, "INCR", "c", "pings_received");
    KVModule_FreeCallReply(reply);
}

/* Callback for message MSGTYPE_PONG. */
void PongReceiver(KVModuleCtx *ctx,
                  const char *sender_id,
                  uint8_t type,
                  const unsigned char *payload,
                  uint32_t len) {
    KVModule_Log(ctx, "notice", "PONG (type %d) RECEIVED from %.*s: '%.*s'", type, KVMODULE_NODE_ID_LEN,
                     sender_id, (int)len, payload);
}

/* This function must be present on each module. It is used in order to
 * register the commands into the server. */
int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx, "hellocluster", 1, KVMODULE_APIVER_1) == KVMODULE_ERR) return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hellocluster.pingall", PingallCommand_KVCommand, "readonly", 0, 0, 0) ==
        KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hellocluster.list", ListCommand_KVCommand, "readonly", 0, 0, 0) ==
        KVMODULE_ERR)
        return KVMODULE_ERR;

    /* Disable Cluster sharding and redirections. This way every node
     * will be able to access every possible key, regardless of the hash slot.
     * This way the PING message handler will be able to increment a specific
     * variable. Normally you do that in order for the distributed system
     * you create as a module to have total freedom in the keyspace
     * manipulation. */
    KVModule_SetClusterFlags(ctx, KVMODULE_CLUSTER_FLAG_NO_REDIRECTION);

    /* Register our handlers for different message types. */
    KVModule_RegisterClusterMessageReceiver(ctx, MSGTYPE_PING, PingReceiver);
    KVModule_RegisterClusterMessageReceiver(ctx, MSGTYPE_PONG, PongReceiver);
    return KVMODULE_OK;
}
