/* This module is used to test the server events hooks API.
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
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <assert.h>

/* We need to store events to be able to test and see what we got, and we can't
 * store them in the key-space since that would mess up rdb loading (duplicates)
 * and be lost of flushdb. */
KVModuleDict *event_log = NULL;
/* stores all the keys on which we got 'removed' event */
KVModuleDict *removed_event_log = NULL;
/* stores all the subevent on which we got 'removed' event */
KVModuleDict *removed_subevent_type = NULL;
/* stores all the keys on which we got 'removed' event with expiry information */
KVModuleDict *removed_expiry_log = NULL;

typedef struct EventElement {
    long count;
    KVModuleString *last_val_string;
    long last_val_int;
} EventElement;

void LogStringEvent(KVModuleCtx *ctx, const char* keyname, const char* data) {
    EventElement *event = KVModule_DictGetC(event_log, (void*)keyname, strlen(keyname), NULL);
    if (!event) {
        event = KVModule_Alloc(sizeof(EventElement));
        memset(event, 0, sizeof(EventElement));
        KVModule_DictSetC(event_log, (void*)keyname, strlen(keyname), event);
    }
    if (event->last_val_string) KVModule_FreeString(ctx, event->last_val_string);
    event->last_val_string = KVModule_CreateString(ctx, data, strlen(data));
    event->count++;
}

void LogNumericEvent(KVModuleCtx *ctx, const char* keyname, long data) {
    KVMODULE_NOT_USED(ctx);
    EventElement *event = KVModule_DictGetC(event_log, (void*)keyname, strlen(keyname), NULL);
    if (!event) {
        event = KVModule_Alloc(sizeof(EventElement));
        memset(event, 0, sizeof(EventElement));
        KVModule_DictSetC(event_log, (void*)keyname, strlen(keyname), event);
    }
    event->last_val_int = data;
    event->count++;
}

void FreeEvent(KVModuleCtx *ctx, EventElement *event) {
    if (event->last_val_string)
        KVModule_FreeString(ctx, event->last_val_string);
    KVModule_Free(event);
}

int cmdEventCount(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    if (argc != 2){
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    EventElement *event = KVModule_DictGet(event_log, argv[1], NULL);
    KVModule_ReplyWithLongLong(ctx, event? event->count: 0);
    return KVMODULE_OK;
}

int cmdEventLast(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    if (argc != 2){
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    EventElement *event = KVModule_DictGet(event_log, argv[1], NULL);
    if (event && event->last_val_string)
        KVModule_ReplyWithString(ctx, event->last_val_string);
    else if (event)
        KVModule_ReplyWithLongLong(ctx, event->last_val_int);
    else
        KVModule_ReplyWithNull(ctx);
    return KVMODULE_OK;
}

void clearEvents(KVModuleCtx *ctx)
{
    KVModuleString *key;
    EventElement *event;
    KVModuleDictIter *iter = KVModule_DictIteratorStartC(event_log, "^", NULL, 0);
    while((key = KVModule_DictNext(ctx, iter, (void**)&event)) != NULL) {
        event->count = 0;
        event->last_val_int = 0;
        if (event->last_val_string) KVModule_FreeString(ctx, event->last_val_string);
        event->last_val_string = NULL;
        KVModule_DictDel(event_log, key, NULL);
        KVModule_Free(event);
        KVModule_DictIteratorReseek(iter, ">=", key);
        KVModule_FreeString(ctx, key);
    }
    KVModule_DictIteratorStop(iter);
}

int cmdEventsClear(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    KVMODULE_NOT_USED(argc);
    KVMODULE_NOT_USED(argv);
    clearEvents(ctx);
    return KVMODULE_OK;
}

/* Client state change callback. */
void clientChangeCallback(KVModuleCtx *ctx, KVModuleEvent e, uint64_t sub, void *data)
{
    KVMODULE_NOT_USED(e);

    KVModuleClientInfo *ci = data;
    char *keyname = (sub == KVMODULE_SUBEVENT_CLIENT_CHANGE_CONNECTED) ?
        "client-connected" : "client-disconnected";
    LogNumericEvent(ctx, keyname, ci->id);
}

void flushdbCallback(KVModuleCtx *ctx, KVModuleEvent e, uint64_t sub, void *data)
{
    KVMODULE_NOT_USED(e);

    KVModuleFlushInfo *fi = data;
    char *keyname = (sub == KVMODULE_SUBEVENT_FLUSHDB_START) ?
        "flush-start" : "flush-end";
    LogNumericEvent(ctx, keyname, fi->dbnum);
}

void roleChangeCallback(KVModuleCtx *ctx, KVModuleEvent e, uint64_t sub, void *data)
{
    KVMODULE_NOT_USED(e);
    KVMODULE_NOT_USED(data);

    KVModuleReplicationInfo *ri = data;
    char *keyname = (sub == KVMODULE_EVENT_REPLROLECHANGED_NOW_PRIMARY) ?
        "role-master" : "role-replica";
    LogStringEvent(ctx, keyname, ri->primary_host);
}

void replicationChangeCallback(KVModuleCtx *ctx, KVModuleEvent e, uint64_t sub, void *data)
{
    KVMODULE_NOT_USED(e);
    KVMODULE_NOT_USED(data);

    char *keyname = (sub == KVMODULE_SUBEVENT_REPLICA_CHANGE_ONLINE) ?
        "replica-online" : "replica-offline";
    LogNumericEvent(ctx, keyname, 0);
}

void rasterLinkChangeCallback(KVModuleCtx *ctx, KVModuleEvent e, uint64_t sub, void *data)
{
    KVMODULE_NOT_USED(e);
    KVMODULE_NOT_USED(data);

    char *keyname = (sub == KVMODULE_SUBEVENT_PRIMARY_LINK_UP) ?
        "masterlink-up" : "masterlink-down";
    LogNumericEvent(ctx, keyname, 0);
}

void persistenceCallback(KVModuleCtx *ctx, KVModuleEvent e, uint64_t sub, void *data)
{
    KVMODULE_NOT_USED(e);
    KVMODULE_NOT_USED(data);

    char *keyname = NULL;
    switch (sub) {
        case KVMODULE_SUBEVENT_PERSISTENCE_RDB_START: keyname = "persistence-rdb-start"; break;
        case KVMODULE_SUBEVENT_PERSISTENCE_AOF_START: keyname = "persistence-aof-start"; break;
        case KVMODULE_SUBEVENT_PERSISTENCE_SYNC_AOF_START: keyname = "persistence-syncaof-start"; break;
        case KVMODULE_SUBEVENT_PERSISTENCE_SYNC_RDB_START: keyname = "persistence-syncrdb-start"; break;
        case KVMODULE_SUBEVENT_PERSISTENCE_ENDED: keyname = "persistence-end"; break;
        case KVMODULE_SUBEVENT_PERSISTENCE_FAILED: keyname = "persistence-failed"; break;
    }
    /* modifying the keyspace from the fork child is not an option, using log instead */
    KVModule_Log(ctx, "warning", "module-event-%s", keyname);
    if (sub == KVMODULE_SUBEVENT_PERSISTENCE_SYNC_RDB_START ||
        sub == KVMODULE_SUBEVENT_PERSISTENCE_SYNC_AOF_START) 
    {
        LogNumericEvent(ctx, keyname, 0);
    }
}

void loadingCallback(KVModuleCtx *ctx, KVModuleEvent e, uint64_t sub, void *data)
{
    KVMODULE_NOT_USED(e);
    KVMODULE_NOT_USED(data);

    char *keyname = NULL;
    switch (sub) {
        case KVMODULE_SUBEVENT_LOADING_RDB_START: keyname = "loading-rdb-start"; break;
        case KVMODULE_SUBEVENT_LOADING_AOF_START: keyname = "loading-aof-start"; break;
        case KVMODULE_SUBEVENT_LOADING_REPL_START: keyname = "loading-repl-start"; break;
        case KVMODULE_SUBEVENT_LOADING_ENDED: keyname = "loading-end"; break;
        case KVMODULE_SUBEVENT_LOADING_FAILED: keyname = "loading-failed"; break;
    }
    LogNumericEvent(ctx, keyname, 0);
}

void loadingProgressCallback(KVModuleCtx *ctx, KVModuleEvent e, uint64_t sub, void *data)
{
    KVMODULE_NOT_USED(e);

    KVModuleLoadingProgress *ei = data;
    char *keyname = (sub == KVMODULE_SUBEVENT_LOADING_PROGRESS_RDB) ?
        "loading-progress-rdb" : "loading-progress-aof";
    LogNumericEvent(ctx, keyname, ei->progress);
}

void shutdownCallback(KVModuleCtx *ctx, KVModuleEvent e, uint64_t sub, void *data)
{
    KVMODULE_NOT_USED(e);
    KVMODULE_NOT_USED(data);
    KVMODULE_NOT_USED(sub);

    KVModule_Log(ctx, "warning", "module-event-%s", "shutdown");
}

void cronLoopCallback(KVModuleCtx *ctx, KVModuleEvent e, uint64_t sub, void *data)
{
    KVMODULE_NOT_USED(e);
    KVMODULE_NOT_USED(sub);

    KVModuleCronLoop *ei = data;
    LogNumericEvent(ctx, "cron-loop", ei->hz);
}

void moduleChangeCallback(KVModuleCtx *ctx, KVModuleEvent e, uint64_t sub, void *data)
{
    KVMODULE_NOT_USED(e);

    KVModuleModuleChange *ei = data;
    char *keyname = (sub == KVMODULE_SUBEVENT_MODULE_LOADED) ?
        "module-loaded" : "module-unloaded";
    LogStringEvent(ctx, keyname, ei->module_name);
}

void swapDbCallback(KVModuleCtx *ctx, KVModuleEvent e, uint64_t sub, void *data)
{
    KVMODULE_NOT_USED(e);
    KVMODULE_NOT_USED(sub);

    KVModuleSwapDbInfo *ei = data;
    LogNumericEvent(ctx, "swapdb-first", ei->dbnum_first);
    LogNumericEvent(ctx, "swapdb-second", ei->dbnum_second);
}

void configChangeCallback(KVModuleCtx *ctx, KVModuleEvent e, uint64_t sub, void *data)
{
    KVMODULE_NOT_USED(e);
    if (sub != KVMODULE_SUBEVENT_CONFIG_CHANGE) {
        return;
    }

    KVModuleConfigChangeV1 *ei = data;
    LogNumericEvent(ctx, "config-change-count", ei->num_changes);
    LogStringEvent(ctx, "config-change-first", ei->config_names[0]);
}

void keyInfoCallback(KVModuleCtx *ctx, KVModuleEvent e, uint64_t sub, void *data)
{
    KVMODULE_NOT_USED(e);

    KVModuleKeyInfoV1 *ei = data;
    KVModuleKey *kp = ei->key;
    KVModuleString *key = (KVModuleString *) KVModule_GetKeyNameFromModuleKey(kp);
    const char *keyname = KVModule_StringPtrLen(key, NULL);
    KVModuleString *event_keyname = KVModule_CreateStringPrintf(ctx, "key-info-%s", keyname);
    LogStringEvent(ctx, KVModule_StringPtrLen(event_keyname, NULL), keyname);
    KVModule_FreeString(ctx, event_keyname);

    /* Despite getting a key object from the callback, we also try to re-open it
     * to make sure the callback is called before it is actually removed from the keyspace. */
    KVModuleKey *kp_open = KVModule_OpenKey(ctx, key, KVMODULE_READ);
    assert(KVModule_ValueLength(kp) == KVModule_ValueLength(kp_open));
    KVModule_CloseKey(kp_open);

    /* We also try to RM_Call a command that accesses that key, also to make sure it's still in the keyspace. */
    char *size_command = NULL;
    int key_type = KVModule_KeyType(kp);
    if (key_type == KVMODULE_KEYTYPE_STRING) {
        size_command = "STRLEN";
    } else if (key_type == KVMODULE_KEYTYPE_LIST) {
        size_command = "LLEN";
    } else if (key_type == KVMODULE_KEYTYPE_HASH) {
        size_command = "HLEN";
    } else if (key_type == KVMODULE_KEYTYPE_SET) {
        size_command = "SCARD";
    } else if (key_type == KVMODULE_KEYTYPE_ZSET) {
        size_command = "ZCARD";
    } else if (key_type == KVMODULE_KEYTYPE_STREAM) {
        size_command = "XLEN";
    }
    if (size_command != NULL) {
        KVModuleCallReply *reply = KVModule_Call(ctx, size_command, "s", key);
        assert(reply != NULL);
        assert(KVModule_ValueLength(kp) == (size_t) KVModule_CallReplyInteger(reply));
        KVModule_FreeCallReply(reply);
    }

    /* Now use the key object we got from the callback for various validations. */
    KVModuleString *prev = KVModule_DictGetC(removed_event_log, (void*)keyname, strlen(keyname), NULL);
    /* We keep object length */
    KVModuleString *v = KVModule_CreateStringPrintf(ctx, "%zd", KVModule_ValueLength(kp));
    /* For string type, we keep value instead of length */
    if (KVModule_KeyType(kp) == KVMODULE_KEYTYPE_STRING) {
        KVModule_FreeString(ctx, v);
        size_t len;
        /* We need to access the string value with KVModule_StringDMA.
         * KVModule_StringDMA may call dbUnshareStringValue to free the origin object,
         * so we also can test it. */
        char *s = KVModule_StringDMA(kp, &len, KVMODULE_READ);
        v = KVModule_CreateString(ctx, s, len);
    }
    KVModule_DictReplaceC(removed_event_log, (void*)keyname, strlen(keyname), v);
    if (prev != NULL) {
        KVModule_FreeString(ctx, prev);
    }

    const char *subevent = "deleted";
    if (sub == KVMODULE_SUBEVENT_KEY_EXPIRED) {
        subevent = "expired";
    } else if (sub == KVMODULE_SUBEVENT_KEY_EVICTED) {
        subevent = "evicted";
    } else if (sub == KVMODULE_SUBEVENT_KEY_OVERWRITTEN) {
        subevent = "overwritten";
    }
    KVModule_DictReplaceC(removed_subevent_type, (void*)keyname, strlen(keyname), (void *)subevent);

    KVModuleString *prevexpire = KVModule_DictGetC(removed_expiry_log, (void*)keyname, strlen(keyname), NULL);
    KVModuleString *expire = KVModule_CreateStringPrintf(ctx, "%lld", KVModule_GetAbsExpire(kp));
    KVModule_DictReplaceC(removed_expiry_log, (void*)keyname, strlen(keyname), (void *)expire);
    if (prevexpire != NULL) {
        KVModule_FreeString(ctx, prevexpire);
    }
}

void authAttemptCallback(KVModuleCtx *ctx, KVModuleEvent e, uint64_t sub, void *data)
{
    KVMODULE_NOT_USED(e);
    KVMODULE_NOT_USED(sub);

    KVModuleAuthenticationInfo *ai = data;
    LogStringEvent(ctx, "auth-attempt", ai->username);
    if (ai->module_name) {
        LogStringEvent(ctx, "auth-attempt-module", ai->module_name);
    }
    LogNumericEvent(ctx, "auth-attempt-success", ai->result == KVMODULE_AUTH_RESULT_GRANTED);
}

void logAtomicSlotMigrationInfo(KVModuleCtx *ctx, const char *prefix, KVModuleAtomicSlotMigrationInfo *asmi) {
    KVModuleString *job_keyname = KVModule_CreateStringPrintf(ctx, "%s-jobname", prefix);
    LogStringEvent(ctx, KVModule_StringPtrLen(job_keyname, NULL), asmi->job_name);
    KVModule_FreeString(ctx, job_keyname);

    KVModuleString *numslotranges_keyname = KVModule_CreateStringPrintf(ctx, "%s-numslotranges", prefix);
    LogNumericEvent(ctx, KVModule_StringPtrLen(numslotranges_keyname, NULL), asmi->num_slot_ranges);
    KVModule_FreeString(ctx, numslotranges_keyname);

    KVModuleString *joined_range_str = NULL;
    for (size_t i = 0; i < asmi->num_slot_ranges; i++) {
        KVModuleString *range_str = KVModule_CreateStringPrintf(ctx, "%d-%d",
            asmi->slot_ranges[i].start, asmi->slot_ranges[i].end);
        if (joined_range_str) {
            KVModule_StringAppendBuffer(ctx, range_str, " ", 1);
            size_t range_str_len;
            const char *range_buf = KVModule_StringPtrLen(range_str, &range_str_len);
            KVModule_StringAppendBuffer(ctx, joined_range_str, range_buf, range_str_len);
            KVModule_FreeString(ctx, range_str);
        } else {
            joined_range_str = range_str;
        }
    }
    if (!joined_range_str) {
        joined_range_str = KVModule_CreateString(ctx, "", 0);
    }
    KVModuleString *slotranges_keyname = KVModule_CreateStringPrintf(ctx, "%s-slotranges", prefix);
    LogStringEvent(ctx, KVModule_StringPtrLen(slotranges_keyname, NULL), KVModule_StringPtrLen(joined_range_str, NULL));
    KVModule_FreeString(ctx, slotranges_keyname);
    KVModule_FreeString(ctx, joined_range_str);
}

void atomicSlotMigrationCallback(KVModuleCtx *ctx, KVModuleEvent e, uint64_t sub, void *data)
{
    KVMODULE_NOT_USED(e);

    KVModuleAtomicSlotMigrationInfo *asmi = data;
    switch (sub) {
        case KVMODULE_SUBEVENT_ATOMIC_SLOT_MIGRATION_IMPORT_STARTED:
            logAtomicSlotMigrationInfo(ctx, "atomic-slot-migration-import-start", asmi);
            break;
        case KVMODULE_SUBEVENT_ATOMIC_SLOT_MIGRATION_IMPORT_COMPLETED:
            logAtomicSlotMigrationInfo(ctx, "atomic-slot-migration-import-complete", asmi);
            break;
        case KVMODULE_SUBEVENT_ATOMIC_SLOT_MIGRATION_IMPORT_ABORTED:
            logAtomicSlotMigrationInfo(ctx, "atomic-slot-migration-import-abort", asmi);
            break;
        case KVMODULE_SUBEVENT_ATOMIC_SLOT_MIGRATION_EXPORT_STARTED:
            logAtomicSlotMigrationInfo(ctx, "atomic-slot-migration-export-start", asmi);
            break;
        case KVMODULE_SUBEVENT_ATOMIC_SLOT_MIGRATION_EXPORT_COMPLETED:
            logAtomicSlotMigrationInfo(ctx, "atomic-slot-migration-export-complete", asmi);
            break;
        case KVMODULE_SUBEVENT_ATOMIC_SLOT_MIGRATION_EXPORT_ABORTED:
            logAtomicSlotMigrationInfo(ctx, "atomic-slot-migration-export-abort", asmi);
            break;
    }
}

static int cmdIsKeyRemoved(KVModuleCtx *ctx, KVModuleString **argv, int argc){
    if(argc != 2){
        return KVModule_WrongArity(ctx);
    }

    const char *key  = KVModule_StringPtrLen(argv[1], NULL);

    KVModuleString *value = KVModule_DictGetC(removed_event_log, (void*)key, strlen(key), NULL);

    if (value == NULL) {
        return KVModule_ReplyWithError(ctx, "ERR Key was not removed");
    }

    const char *subevent = KVModule_DictGetC(removed_subevent_type, (void*)key, strlen(key), NULL);
    KVModule_ReplyWithArray(ctx, 2);
    KVModule_ReplyWithString(ctx, value);
    KVModule_ReplyWithSimpleString(ctx, subevent);

    return KVMODULE_OK;
}

static int cmdKeyExpiry(KVModuleCtx *ctx, KVModuleString **argv, int argc){
    if(argc != 2){
        return KVModule_WrongArity(ctx);
    }

    const char* key  = KVModule_StringPtrLen(argv[1], NULL);
    KVModuleString *expire = KVModule_DictGetC(removed_expiry_log, (void*)key, strlen(key), NULL);
    if (expire == NULL) {
        return KVModule_ReplyWithError(ctx, "ERR Key was not removed");
    }
    KVModule_ReplyWithString(ctx, expire);
    return KVMODULE_OK;
}

/* This function must be present on each module. It is used in order to
 * register the commands into the server. */
int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
#define VerifySubEventSupported(e, s) \
    if (!KVModule_IsSubEventSupported(e, s)) { \
        return KVMODULE_ERR; \
    }

    if (KVModule_Init(ctx,"testhook",1,KVMODULE_APIVER_1)
        == KVMODULE_ERR) return KVMODULE_ERR;

    /* Example on how to check if a server sub event is supported */
    if (!KVModule_IsSubEventSupported(KVModuleEvent_ReplicationRoleChanged, KVMODULE_EVENT_REPLROLECHANGED_NOW_PRIMARY)) {
        return KVMODULE_ERR;
    }

    /* replication related hooks */
    KVModule_SubscribeToServerEvent(ctx,
        KVModuleEvent_ReplicationRoleChanged, roleChangeCallback);
    KVModule_SubscribeToServerEvent(ctx,
        KVModuleEvent_ReplicaChange, replicationChangeCallback);
    KVModule_SubscribeToServerEvent(ctx,
        KVModuleEvent_PrimaryLinkChange, rasterLinkChangeCallback);

    /* persistence related hooks */
    KVModule_SubscribeToServerEvent(ctx,
        KVModuleEvent_Persistence, persistenceCallback);
    KVModule_SubscribeToServerEvent(ctx,
        KVModuleEvent_Loading, loadingCallback);
    KVModule_SubscribeToServerEvent(ctx,
        KVModuleEvent_LoadingProgress, loadingProgressCallback);

    /* other hooks */
    KVModule_SubscribeToServerEvent(ctx,
        KVModuleEvent_ClientChange, clientChangeCallback);
    KVModule_SubscribeToServerEvent(ctx,
        KVModuleEvent_FlushDB, flushdbCallback);
    KVModule_SubscribeToServerEvent(ctx,
        KVModuleEvent_Shutdown, shutdownCallback);
    KVModule_SubscribeToServerEvent(ctx,
        KVModuleEvent_CronLoop, cronLoopCallback);

    KVModule_SubscribeToServerEvent(ctx,
        KVModuleEvent_ModuleChange, moduleChangeCallback);
    KVModule_SubscribeToServerEvent(ctx,
        KVModuleEvent_SwapDB, swapDbCallback);

    KVModule_SubscribeToServerEvent(ctx,
        KVModuleEvent_Config, configChangeCallback);

    KVModule_SubscribeToServerEvent(ctx,
        KVModuleEvent_Key, keyInfoCallback);

    KVModule_SubscribeToServerEvent(ctx,
        KVModuleEvent_AuthenticationAttempt, authAttemptCallback);

    KVModule_SubscribeToServerEvent(ctx,
        KVModuleEvent_AtomicSlotMigration, atomicSlotMigrationCallback);

    event_log = KVModule_CreateDict(ctx);
    removed_event_log = KVModule_CreateDict(ctx);
    removed_subevent_type = KVModule_CreateDict(ctx);
    removed_expiry_log = KVModule_CreateDict(ctx);

    if (KVModule_CreateCommand(ctx,"hooks.event_count", cmdEventCount,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"hooks.event_last", cmdEventLast,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"hooks.clear", cmdEventsClear,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"hooks.is_key_removed", cmdIsKeyRemoved,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"hooks.pexpireat", cmdKeyExpiry,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (argc == 1) {
        const char *ptr = KVModule_StringPtrLen(argv[0], NULL);
        if (!strcasecmp(ptr, "noload")) {
            /* This is a hint that we return ERR at the last moment of OnLoad. */
            KVModule_FreeDict(ctx, event_log);
            KVModule_FreeDict(ctx, removed_event_log);
            KVModule_FreeDict(ctx, removed_subevent_type);
            KVModule_FreeDict(ctx, removed_expiry_log);
            return KVMODULE_ERR;
        }
    }

    KVModule_SetModuleOptions(ctx, KVMODULE_OPTIONS_HANDLE_ATOMIC_SLOT_MIGRATION);

    return KVMODULE_OK;
}

int KVModule_OnUnload(KVModuleCtx *ctx) {
    clearEvents(ctx);
    KVModule_FreeDict(ctx, event_log);
    event_log = NULL;

    KVModuleDictIter *iter = KVModule_DictIteratorStartC(removed_event_log, "^", NULL, 0);
    char* key;
    size_t keyLen;
    KVModuleString* val;
    while((key = KVModule_DictNextC(iter, &keyLen, (void**)&val))){
        KVModule_FreeString(ctx, val);
    }
    KVModule_FreeDict(ctx, removed_event_log);
    KVModule_DictIteratorStop(iter);
    removed_event_log = NULL;

    KVModule_FreeDict(ctx, removed_subevent_type);
    removed_subevent_type = NULL;

    iter = KVModule_DictIteratorStartC(removed_expiry_log, "^", NULL, 0);
    while((key = KVModule_DictNextC(iter, &keyLen, (void**)&val))){
        KVModule_FreeString(ctx, val);
    }
    KVModule_FreeDict(ctx, removed_expiry_log);
    KVModule_DictIteratorStop(iter);
    removed_expiry_log = NULL;

    return KVMODULE_OK;
}

