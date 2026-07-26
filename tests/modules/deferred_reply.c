/* Test module to verify that addReplyDeferredLen / setDeferredReply work
 * correctly when the deferred reply buffer is active.
 *
 * The module subscribes to NOTIFY_STRING keyspace notifications.  When armed,
 * the notification callback blocks the client with a reply callback that
 * builds a nested array using KVMODULE_POSTPONED_LEN (which internally
 * calls addReplyDeferredLen).
 *
 * Without the fix in networking.c the placeholder node for the inner array
 * ends up in c->reply while the outer array header and elements live in
 * c->deferred_reply, producing a malformed response after
 * commitDeferredReplyBuffer joins the two lists. */

#include "kvmodule.h"
#include <pthread.h>
#include <unistd.h>

static int armed = 0;

static void *UnblockThread(void *arg) {
    KVModuleBlockedClient *bc = arg;
    usleep(100000); /* 100 ms */
    KVModule_UnblockClient(bc, NULL);
    return NULL;
}

/* Reply callback – builds a two-element array where the second element is
 * itself an array whose length is set via POSTPONED_LEN.
 * Expected response: ["first", ["a", "b"]] */
static int ReplyCallback(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(ctx);
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModule_ReplyWithArray(ctx, 2);
    KVModule_ReplyWithSimpleString(ctx, "first");
    KVModule_ReplyWithArray(ctx, KVMODULE_POSTPONED_LEN);
    KVModule_ReplyWithSimpleString(ctx, "a");
    KVModule_ReplyWithSimpleString(ctx, "b");
    KVModule_ReplySetArrayLength(ctx, 2);
    return KVMODULE_OK;
}

static int OnKeyspaceNotification(KVModuleCtx *ctx, int type,
                                  const char *event, KVModuleString *key) {
    KVMODULE_NOT_USED(type);
    KVMODULE_NOT_USED(event);
    KVMODULE_NOT_USED(key);

    if (!armed) return KVMODULE_OK;
    armed = 0;

    KVModuleBlockedClient *bc =
        KVModule_BlockClient(ctx, ReplyCallback, NULL, NULL, 0);
    if (!bc) return KVMODULE_OK;

    pthread_t tid;
    if (pthread_create(&tid, NULL, UnblockThread, bc) != 0) {
        KVModule_UnblockClient(bc, NULL);
    } else {
        pthread_detach(tid);
    }
    return KVMODULE_OK;
}

/* DEFERRED_REPLY.ARM – arms the notification handler so the next
 * NOTIFY_STRING event blocks the client. */
static int CmdArm(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    armed = 1;
    KVModule_ReplyWithSimpleString(ctx, "OK");
    return KVMODULE_OK;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx, "deferred_reply", 1, KVMODULE_APIVER_1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_SubscribeToKeyspaceEvents(ctx, KVMODULE_NOTIFY_STRING,
                                               OnKeyspaceNotification) != KVMODULE_OK)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "deferred_reply.arm", CmdArm, "", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}
