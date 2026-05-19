<<<<<<< HEAD
#include "kvmodule.h"
=======
#include "valkeymodule.h"
>>>>>>> v9.0.4
#include <string.h>

#define UNUSED(x) (void)(x)

<<<<<<< HEAD
void cluster_timer_handler(KVModuleCtx *ctx, void *data) {
    KVMODULE_NOT_USED(data);
=======
void cluster_timer_handler(ValkeyModuleCtx *ctx, void *data) {
    VALKEYMODULE_NOT_USED(data);

    ValkeyModuleCallReply *rep = ValkeyModule_Call(ctx, "CLUSTER", "c", "SLOTS");

    if (rep) {
        if (ValkeyModule_CallReplyType(rep) == VALKEYMODULE_REPLY_ARRAY) {
            ValkeyModule_Log(ctx, "notice", "Timer: CLUSTER SLOTS success");
        } else {
            ValkeyModule_Log(ctx, "notice",
                             "Timer: CLUSTER SLOTS unexpected reply type %d",
                             ValkeyModule_CallReplyType(rep));
        }
        ValkeyModule_FreeCallReply(rep);
    } else {
        ValkeyModule_Log(ctx, "warning", "Timer: CLUSTER SLOTS failed");
    }
}

int test_start_cluster_timer(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    VALKEYMODULE_NOT_USED(argv);
    VALKEYMODULE_NOT_USED(argc);

    ValkeyModule_CreateTimer(ctx, 1, cluster_timer_handler, NULL);

    return ValkeyModule_ReplyWithSimpleString(ctx, "OK");
}


int test_cluster_slots(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    UNUSED(argv);
>>>>>>> v9.0.4

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

<<<<<<< HEAD
void DingReceiver(KVModuleCtx *ctx, const char *sender_id, uint8_t type, const unsigned char *payload, uint32_t len) {
    KVModule_Log(ctx, "notice", "DING (type %d) RECEIVED from %.*s: '%.*s'", type, KVMODULE_NODE_ID_LEN, sender_id, (int)len, payload);
    /* Ensure sender_id is null-terminated for cross-version compatibility */
    char null_terminated_sender_id[KVMODULE_NODE_ID_LEN + 1];
    memcpy(null_terminated_sender_id, sender_id, KVMODULE_NODE_ID_LEN);
    null_terminated_sender_id[KVMODULE_NODE_ID_LEN] = '\0';
    KVModule_SendClusterMessage(ctx, null_terminated_sender_id, MSGTYPE_DONG, "Message Received!", 17);
=======
void DingReceiver(ValkeyModuleCtx *ctx, const char *sender_id, uint8_t type, const unsigned char *payload, uint32_t len) {
    ValkeyModule_Log(ctx, "notice", "DING (type %d) RECEIVED from %.*s: '%.*s'", type, VALKEYMODULE_NODE_ID_LEN, sender_id, (int)len, payload);
    /* Ensure sender_id is null-terminated for cross-version compatibility */
    char null_terminated_sender_id[VALKEYMODULE_NODE_ID_LEN + 1];
    memcpy(null_terminated_sender_id, sender_id, VALKEYMODULE_NODE_ID_LEN);
    null_terminated_sender_id[VALKEYMODULE_NODE_ID_LEN] = '\0';
    ValkeyModule_SendClusterMessage(ctx, null_terminated_sender_id, MSGTYPE_DONG, "Message Received!", 17);
>>>>>>> v9.0.4
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

    if (ValkeyModule_CreateCommand(ctx, "test.start_cluster_timer", test_start_cluster_timer, "", 0, 0, 0) == VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    /* Register our handlers for different message types. */
<<<<<<< HEAD
    KVModule_RegisterClusterMessageReceiver(ctx, MSGTYPE_DING, DingReceiver);
    KVModule_RegisterClusterMessageReceiver(ctx, MSGTYPE_DONG, DongReceiver);

    return KVMODULE_OK;
=======
    ValkeyModule_RegisterClusterMessageReceiver(ctx, MSGTYPE_DING, DingReceiver);
    ValkeyModule_RegisterClusterMessageReceiver(ctx, MSGTYPE_DONG, DongReceiver);

    return VALKEYMODULE_OK;
>>>>>>> v9.0.4
}
