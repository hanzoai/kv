#include "kvmodule.h"
#include <string.h>

#define UNUSED(x) (void)(x)

void cluster_timer_handler(KVModuleCtx *ctx, void *data) {
    KVMODULE_NOT_USED(data);

    KVModuleCallReply *rep = KVModule_Call(ctx, "CLUSTER", "c", "SLOTS");

    if (rep) {
        if (KVModule_CallReplyType(rep) == KVMODULE_REPLY_ARRAY) {
            KVModule_Log(ctx, "notice", "Timer: CLUSTER SLOTS success");
        } else {
            KVModule_Log(ctx, "notice",
                             "Timer: CLUSTER SLOTS unexpected reply type %d",
                             KVModule_CallReplyType(rep));
        }
        KVModule_FreeCallReply(rep);
    } else {
        KVModule_Log(ctx, "warning", "Timer: CLUSTER SLOTS failed");
    }
}

int test_start_cluster_timer(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModule_CreateTimer(ctx, 1, cluster_timer_handler, NULL);

    return KVModule_ReplyWithSimpleString(ctx, "OK");
}


int test_cluster_slots(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    UNUSED(argv);

    if (argc != 1) return KVModule_WrongArity(ctx);

    KVModuleCallReply *rep = KVModule_Call(ctx, "CLUSTER", "c", "SLOTS");
    if (!rep) {
        KVModule_ReplyWithError(ctx, "ERR NULL reply returned");
    } else {
        KVModule_ReplyWithCallReply(ctx, rep);
        KVModule_FreeCallReply(rep);
    }

    return KVMODULE_OK;
}

int test_cluster_shards(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    UNUSED(argv);

    if (argc != 1) return KVModule_WrongArity(ctx);

    KVModuleCallReply *rep = KVModule_Call(ctx, "CLUSTER", "c", "SHARDS");
    if (!rep) {
        KVModule_ReplyWithError(ctx, "ERR NULL reply returned");
    } else {
        KVModule_ReplyWithCallReply(ctx, rep);
        KVModule_FreeCallReply(rep);
    }

    return KVMODULE_OK;
}

#define MSGTYPE_DING 1
#define MSGTYPE_DONG 2

/* test.pingall */
int PingallCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModule_SendClusterMessage(ctx, NULL, MSGTYPE_DING, "Hey", 3);
    return KVModule_ReplyWithSimpleString(ctx, "OK");
}

void DingReceiver(KVModuleCtx *ctx, const char *sender_id, uint8_t type, const unsigned char *payload, uint32_t len) {
    KVModule_Log(ctx, "notice", "DING (type %d) RECEIVED from %.*s: '%.*s'", type, KVMODULE_NODE_ID_LEN, sender_id, (int)len, payload);
    /* Ensure sender_id is null-terminated for cross-version compatibility */
    char null_terminated_sender_id[KVMODULE_NODE_ID_LEN + 1];
    memcpy(null_terminated_sender_id, sender_id, KVMODULE_NODE_ID_LEN);
    null_terminated_sender_id[KVMODULE_NODE_ID_LEN] = '\0';
    KVModule_SendClusterMessage(ctx, null_terminated_sender_id, MSGTYPE_DONG, "Message Received!", 17);
}

void DongReceiver(KVModuleCtx *ctx, const char *sender_id, uint8_t type, const unsigned char *payload, uint32_t len) {
    KVModule_Log(ctx, "notice", "DONG (type %d) RECEIVED from %.*s: '%.*s'", type, KVMODULE_NODE_ID_LEN, sender_id, (int)len, payload);
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx, "cluster", 1, KVMODULE_APIVER_1)== KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "test.pingall", PingallCommand, "readonly", 0, 0, 0) ==
        KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "test.cluster_slots", test_cluster_slots, "", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "test.cluster_shards", test_cluster_shards, "", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "test.start_cluster_timer", test_start_cluster_timer, "", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    /* Register our handlers for different message types. */
    KVModule_RegisterClusterMessageReceiver(ctx, MSGTYPE_DING, DingReceiver);
    KVModule_RegisterClusterMessageReceiver(ctx, MSGTYPE_DONG, DongReceiver);

    return KVMODULE_OK;
}
