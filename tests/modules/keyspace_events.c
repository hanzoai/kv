/* This module is used to test the server keyspace events API.
 *
 * -----------------------------------------------------------------------------
 *
 * Copyright (c) 2020, Meir Shpilraien <meir at redislabs dot com>
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

#define _BSD_SOURCE
#define _DEFAULT_SOURCE /* For usleep */

#include "kvmodule.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

ustime_t cached_time = 0;

/** stores all the keys on which we got 'loaded' keyspace notification **/
KVModuleDict *loaded_event_log = NULL;
/** stores all the keys on which we got 'module' keyspace notification **/
KVModuleDict *module_event_log = NULL;

/** Counts how many deleted KSN we got on keys with a prefix of "count_dels_" **/
static size_t dels = 0;

static int KeySpace_NotificationLoaded(KVModuleCtx *ctx, int type, const char *event, KVModuleString *key){
    KVMODULE_NOT_USED(ctx);
    KVMODULE_NOT_USED(type);

    if(strcmp(event, "loaded") == 0){
        const char* keyName = KVModule_StringPtrLen(key, NULL);
        int nokey;
        KVModule_DictGetC(loaded_event_log, (void*)keyName, strlen(keyName), &nokey);
        if(nokey){
            KVModule_DictSetC(loaded_event_log, (void*)keyName, strlen(keyName), KVModule_HoldString(ctx, key));
        }
    }

    return KVMODULE_OK;
}

static int KeySpace_NotificationGeneric(KVModuleCtx *ctx, int type, const char *event, KVModuleString *key) {
    KVMODULE_NOT_USED(type);
    const char *key_str = KVModule_StringPtrLen(key, NULL);
    if (strncmp(key_str, "count_dels_", 11) == 0 && strcmp(event, "del") == 0) {
        if (KVModule_GetContextFlags(ctx) & KVMODULE_CTX_FLAGS_PRIMARY) {
            dels++;
            KVModule_Replicate(ctx, "keyspace.incr_dels", "");
        }
        return KVMODULE_OK;
    }
    if (cached_time) {
        KVModule_Assert(cached_time == KVModule_CachedMicroseconds());
        usleep(1);
        KVModule_Assert(cached_time != KVModule_Microseconds());
    }

    if (strcmp(event, "del") == 0) {
        KVModuleString *copykey = KVModule_CreateStringPrintf(ctx, "%s_copy", KVModule_StringPtrLen(key, NULL));
        KVModuleCallReply* rep = KVModule_Call(ctx, "DEL", "s!", copykey);
        KVModule_FreeString(ctx, copykey);
        KVModule_FreeCallReply(rep);

        int ctx_flags = KVModule_GetContextFlags(ctx);
        if (ctx_flags & KVMODULE_CTX_FLAGS_LUA) {
            KVModuleCallReply* rep = KVModule_Call(ctx, "INCR", "c", "lua");
            KVModule_FreeCallReply(rep);
        }
        if (ctx_flags & KVMODULE_CTX_FLAGS_MULTI) {
            KVModuleCallReply* rep = KVModule_Call(ctx, "INCR", "c", "multi");
            KVModule_FreeCallReply(rep);
        }
    }

    return KVMODULE_OK;
}

static int KeySpace_NotificationExpired(KVModuleCtx *ctx, int type, const char *event, KVModuleString *key) {
    KVMODULE_NOT_USED(type);
    KVMODULE_NOT_USED(event);
    KVMODULE_NOT_USED(key);

    KVModuleCallReply* rep = KVModule_Call(ctx, "INCR", "c!", "testkeyspace:expired");
    KVModule_FreeCallReply(rep);

    return KVMODULE_OK;
}

/* This key miss notification handler is performing a write command inside the notification callback.
 * Notice, it is discourage and currently wrong to perform a write command inside key miss event.
 * It can cause read commands to be replicated to the replica/aof. This test is here temporary (for coverage and
 * verification that it's not crashing). */
static int KeySpace_NotificationModuleKeyMiss(KVModuleCtx *ctx, int type, const char *event, KVModuleString *key) {
    KVMODULE_NOT_USED(type);
    KVMODULE_NOT_USED(event);
    KVMODULE_NOT_USED(key);

    int flags = KVModule_GetContextFlags(ctx);
    if (!(flags & KVMODULE_CTX_FLAGS_PRIMARY)) {
        return KVMODULE_OK; // ignore the event on replica
    }

    KVModuleCallReply* rep = KVModule_Call(ctx, "incr", "!c", "missed");
    KVModule_FreeCallReply(rep);

    return KVMODULE_OK;
}

static int KeySpace_NotificationModuleString(KVModuleCtx *ctx, int type, const char *event, KVModuleString *key) {
    KVMODULE_NOT_USED(type);
    KVMODULE_NOT_USED(event);
    KVModuleKey *kv_key = KVModule_OpenKey(ctx, key, KVMODULE_READ);

    size_t len = 0;
    /* KVModule_StringDMA could change the data format and cause the old robj to be freed.
     * This code verifies that such format change will not cause any crashes.*/
    char *data = KVModule_StringDMA(kv_key, &len, KVMODULE_READ);
    int res = strncmp(data, "dummy", 5);
    KVMODULE_NOT_USED(res);

    KVModule_CloseKey(kv_key);

    return KVMODULE_OK;
}

static void KeySpace_PostNotificationStringFreePD(void *pd) {
    KVModule_FreeString(NULL, pd);
}

static void KeySpace_PostNotificationString(KVModuleCtx *ctx, void *pd) {
    KVMODULE_NOT_USED(ctx);
    KVModuleCallReply* rep = KVModule_Call(ctx, "incr", "!s", pd);
    KVModule_FreeCallReply(rep);
}

static int KeySpace_NotificationModuleStringPostNotificationJob(KVModuleCtx *ctx, int type, const char *event, KVModuleString *key) {
    KVMODULE_NOT_USED(ctx);
    KVMODULE_NOT_USED(type);
    KVMODULE_NOT_USED(event);

    const char *key_str = KVModule_StringPtrLen(key, NULL);

    if (strncmp(key_str, "string1_", 8) != 0) {
        return KVMODULE_OK;
    }

    KVModuleString *new_key = KVModule_CreateStringPrintf(NULL, "string_changed{%s}", key_str);
    KVModule_AddPostNotificationJob(ctx, KeySpace_PostNotificationString, new_key, KeySpace_PostNotificationStringFreePD);
    return KVMODULE_OK;
}

static int KeySpace_NotificationModule(KVModuleCtx *ctx, int type, const char *event, KVModuleString *key) {
    KVMODULE_NOT_USED(ctx);
    KVMODULE_NOT_USED(type);
    KVMODULE_NOT_USED(event);

    const char* keyName = KVModule_StringPtrLen(key, NULL);
    int nokey;
    KVModule_DictGetC(module_event_log, (void*)keyName, strlen(keyName), &nokey);
    if(nokey){
        KVModule_DictSetC(module_event_log, (void*)keyName, strlen(keyName), KVModule_HoldString(ctx, key));
    }
    return KVMODULE_OK;
}

static int cmdNotify(KVModuleCtx *ctx, KVModuleString **argv, int argc){
    if(argc != 2){
        return KVModule_WrongArity(ctx);
    }

    KVModule_NotifyKeyspaceEvent(ctx, KVMODULE_NOTIFY_MODULE, "notify", argv[1]);
    KVModule_ReplyWithNull(ctx);
    return KVMODULE_OK;
}

static int cmdIsModuleKeyNotified(KVModuleCtx *ctx, KVModuleString **argv, int argc){
    if(argc != 2){
        return KVModule_WrongArity(ctx);
    }

    const char* key  = KVModule_StringPtrLen(argv[1], NULL);

    int nokey;
    KVModuleString* keyStr = KVModule_DictGetC(module_event_log, (void*)key, strlen(key), &nokey);

    KVModule_ReplyWithArray(ctx, 2);
    KVModule_ReplyWithLongLong(ctx, !nokey);
    if(nokey){
        KVModule_ReplyWithNull(ctx);
    }else{
        KVModule_ReplyWithString(ctx, keyStr);
    }
    return KVMODULE_OK;
}

static int cmdIsKeyLoaded(KVModuleCtx *ctx, KVModuleString **argv, int argc){
    if(argc != 2){
        return KVModule_WrongArity(ctx);
    }

    const char* key  = KVModule_StringPtrLen(argv[1], NULL);

    int nokey;
    KVModuleString* keyStr = KVModule_DictGetC(loaded_event_log, (void*)key, strlen(key), &nokey);

    KVModule_ReplyWithArray(ctx, 2);
    KVModule_ReplyWithLongLong(ctx, !nokey);
    if(nokey){
        KVModule_ReplyWithNull(ctx);
    }else{
        KVModule_ReplyWithString(ctx, keyStr);
    }
    return KVMODULE_OK;
}

static int cmdDelKeyCopy(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2)
        return KVModule_WrongArity(ctx);

    cached_time = KVModule_CachedMicroseconds();

    KVModuleCallReply* rep = KVModule_Call(ctx, "DEL", "s!", argv[1]);
    if (!rep) {
        KVModule_ReplyWithError(ctx, "NULL reply returned");
    } else {
        KVModule_ReplyWithCallReply(ctx, rep);
        KVModule_FreeCallReply(rep);
    }
    cached_time = 0;
    return KVMODULE_OK;
}

/* Call INCR and propagate using RM_Call with `!`. */
static int cmdIncrCase1(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2)
        return KVModule_WrongArity(ctx);

    KVModuleCallReply* rep = KVModule_Call(ctx, "INCR", "s!", argv[1]);
    if (!rep) {
        KVModule_ReplyWithError(ctx, "NULL reply returned");
    } else {
        KVModule_ReplyWithCallReply(ctx, rep);
        KVModule_FreeCallReply(rep);
    }
    return KVMODULE_OK;
}

/* Call INCR and propagate using RM_Replicate. */
static int cmdIncrCase2(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2)
        return KVModule_WrongArity(ctx);

    KVModuleCallReply* rep = KVModule_Call(ctx, "INCR", "s", argv[1]);
    if (!rep) {
        KVModule_ReplyWithError(ctx, "NULL reply returned");
    } else {
        KVModule_ReplyWithCallReply(ctx, rep);
        KVModule_FreeCallReply(rep);
    }
    KVModule_Replicate(ctx, "INCR", "s", argv[1]);
    return KVMODULE_OK;
}

/* Call INCR and propagate using RM_ReplicateVerbatim. */
static int cmdIncrCase3(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2)
        return KVModule_WrongArity(ctx);

    KVModuleCallReply* rep = KVModule_Call(ctx, "INCR", "s", argv[1]);
    if (!rep) {
        KVModule_ReplyWithError(ctx, "NULL reply returned");
    } else {
        KVModule_ReplyWithCallReply(ctx, rep);
        KVModule_FreeCallReply(rep);
    }
    KVModule_ReplicateVerbatim(ctx);
    return KVMODULE_OK;
}

static int cmdIncrDels(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    dels++;
    return KVModule_ReplyWithSimpleString(ctx, "OK");
}

static int cmdGetDels(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    return KVModule_ReplyWithLongLong(ctx, dels);
}

/* This function must be present on each module. It is used in order to
 * register the commands into the server. */
int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (KVModule_Init(ctx,"testkeyspace",1,KVMODULE_APIVER_1) == KVMODULE_ERR){
        return KVMODULE_ERR;
    }

    loaded_event_log = KVModule_CreateDict(ctx);
    module_event_log = KVModule_CreateDict(ctx);

    int keySpaceAll = KVModule_GetKeyspaceNotificationFlagsAll();

    if (!(keySpaceAll & KVMODULE_NOTIFY_LOADED)) {
        // KVMODULE_NOTIFY_LOADED event are not supported we can not start
        return KVMODULE_ERR;
    }

    if(KVModule_SubscribeToKeyspaceEvents(ctx, KVMODULE_NOTIFY_LOADED, KeySpace_NotificationLoaded) != KVMODULE_OK){
        return KVMODULE_ERR;
    }

    if(KVModule_SubscribeToKeyspaceEvents(ctx, KVMODULE_NOTIFY_GENERIC, KeySpace_NotificationGeneric) != KVMODULE_OK){
        return KVMODULE_ERR;
    }

    if(KVModule_SubscribeToKeyspaceEvents(ctx, KVMODULE_NOTIFY_EXPIRED, KeySpace_NotificationExpired) != KVMODULE_OK){
        return KVMODULE_ERR;
    }

    if(KVModule_SubscribeToKeyspaceEvents(ctx, KVMODULE_NOTIFY_MODULE, KeySpace_NotificationModule) != KVMODULE_OK){
        return KVMODULE_ERR;
    }

    if(KVModule_SubscribeToKeyspaceEvents(ctx, KVMODULE_NOTIFY_KEY_MISS, KeySpace_NotificationModuleKeyMiss) != KVMODULE_OK){
        return KVMODULE_ERR;
    }

    if(KVModule_SubscribeToKeyspaceEvents(ctx, KVMODULE_NOTIFY_STRING, KeySpace_NotificationModuleString) != KVMODULE_OK){
        return KVMODULE_ERR;
    }

    if(KVModule_SubscribeToKeyspaceEvents(ctx, KVMODULE_NOTIFY_STRING, KeySpace_NotificationModuleStringPostNotificationJob) != KVMODULE_OK){
        return KVMODULE_ERR;
    }

    if (KVModule_CreateCommand(ctx,"keyspace.notify", cmdNotify,"",0,0,0) == KVMODULE_ERR){
        return KVMODULE_ERR;
    }

    if (KVModule_CreateCommand(ctx,"keyspace.is_module_key_notified", cmdIsModuleKeyNotified,"",0,0,0) == KVMODULE_ERR){
        return KVMODULE_ERR;
    }

    if (KVModule_CreateCommand(ctx,"keyspace.is_key_loaded", cmdIsKeyLoaded,"",0,0,0) == KVMODULE_ERR){
        return KVMODULE_ERR;
    }

    if (KVModule_CreateCommand(ctx, "keyspace.del_key_copy", cmdDelKeyCopy,
                                  "write", 0, 0, 0) == KVMODULE_ERR){
        return KVMODULE_ERR;
    }
    
    if (KVModule_CreateCommand(ctx, "keyspace.incr_case1", cmdIncrCase1,
                                  "write", 0, 0, 0) == KVMODULE_ERR){
        return KVMODULE_ERR;
    }
    
    if (KVModule_CreateCommand(ctx, "keyspace.incr_case2", cmdIncrCase2,
                                  "write", 0, 0, 0) == KVMODULE_ERR){
        return KVMODULE_ERR;
    }
    
    if (KVModule_CreateCommand(ctx, "keyspace.incr_case3", cmdIncrCase3,
                                  "write", 0, 0, 0) == KVMODULE_ERR){
        return KVMODULE_ERR;
    }

    if (KVModule_CreateCommand(ctx, "keyspace.incr_dels", cmdIncrDels,
                                  "write", 0, 0, 0) == KVMODULE_ERR){
        return KVMODULE_ERR;
    }

    if (KVModule_CreateCommand(ctx, "keyspace.get_dels", cmdGetDels,
                                  "readonly", 0, 0, 0) == KVMODULE_ERR){
        return KVMODULE_ERR;
    }

    if (argc == 1) {
        const char *ptr = KVModule_StringPtrLen(argv[0], NULL);
        if (!strcasecmp(ptr, "noload")) {
            /* This is a hint that we return ERR at the last moment of OnLoad. */
            KVModule_FreeDict(ctx, loaded_event_log);
            KVModule_FreeDict(ctx, module_event_log);
            return KVMODULE_ERR;
        }
    }

    return KVMODULE_OK;
}

int KVModule_OnUnload(KVModuleCtx *ctx) {
    KVModuleDictIter *iter = KVModule_DictIteratorStartC(loaded_event_log, "^", NULL, 0);
    char* key;
    size_t keyLen;
    KVModuleString* val;
    while((key = KVModule_DictNextC(iter, &keyLen, (void**)&val))){
        KVModule_FreeString(ctx, val);
    }
    KVModule_FreeDict(ctx, loaded_event_log);
    KVModule_DictIteratorStop(iter);
    loaded_event_log = NULL;

    iter = KVModule_DictIteratorStartC(module_event_log, "^", NULL, 0);
    while((key = KVModule_DictNextC(iter, &keyLen, (void**)&val))){
        KVModule_FreeString(ctx, val);
    }
    KVModule_FreeDict(ctx, module_event_log);
    KVModule_DictIteratorStop(iter);
    module_event_log = NULL;

    return KVMODULE_OK;
}
