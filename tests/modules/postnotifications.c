/* This module is used to test the server post keyspace jobs API.
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

/* This module allow to verify 'KVModule_AddPostNotificationJob' by registering to 3
 * key space event:
 * * STRINGS - the module register to all strings notifications and set post notification job
 *             that increase a counter indicating how many times the string key was changed.
 *             In addition, it increase another counter that counts the total changes that
 *             was made on all strings keys.
 * * EXPIRED - the module register to expired event and set post notification job that that
 *             counts the total number of expired events.
 * * EVICTED - the module register to evicted event and set post notification job that that
 *             counts the total number of evicted events.
 *
 * In addition, the module register a new command, 'postnotification.async_set', that performs a set
 * command from a background thread. This allows to check the 'KVModule_AddPostNotificationJob' on
 * notifications that was triggered on a background thread. */

#define _BSD_SOURCE
#define _DEFAULT_SOURCE /* For usleep */

#include "kvmodule.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

static void KeySpace_PostNotificationStringFreePD(void *pd) {
    KVModule_FreeString(NULL, pd);
}

static void KeySpace_PostNotificationReadKey(KVModuleCtx *ctx, void *pd) {
    KVModuleCallReply* rep = KVModule_Call(ctx, "get", "!s", pd);
    KVModule_FreeCallReply(rep);
}

static void KeySpace_PostNotificationString(KVModuleCtx *ctx, void *pd) {
    KVMODULE_NOT_USED(ctx);
    KVModuleCallReply* rep = KVModule_Call(ctx, "incr", "!s", pd);
    KVModule_FreeCallReply(rep);
}

static int KeySpace_NotificationExpired(KVModuleCtx *ctx, int type, const char *event, KVModuleString *key){
    KVMODULE_NOT_USED(type);
    KVMODULE_NOT_USED(event);
    KVMODULE_NOT_USED(key);

    KVModuleString *new_key = KVModule_CreateString(NULL, "expired", 7);
    int res = KVModule_AddPostNotificationJob(ctx, KeySpace_PostNotificationString, new_key, KeySpace_PostNotificationStringFreePD);
    if (res == KVMODULE_ERR) KeySpace_PostNotificationStringFreePD(new_key);
    return KVMODULE_OK;
}

static int KeySpace_NotificationEvicted(KVModuleCtx *ctx, int type, const char *event, KVModuleString *key){
    KVMODULE_NOT_USED(type);
    KVMODULE_NOT_USED(event);
    KVMODULE_NOT_USED(key);

    const char *key_str = KVModule_StringPtrLen(key, NULL);

    if (strncmp(key_str, "evicted", 7) == 0) {
        return KVMODULE_OK; /* do not count the evicted key */
    }

    if (strncmp(key_str, "before_evicted", 14) == 0) {
        return KVMODULE_OK; /* do not count the before_evicted key */
    }

    KVModuleString *new_key = KVModule_CreateString(NULL, "evicted", 7);
    int res = KVModule_AddPostNotificationJob(ctx, KeySpace_PostNotificationString, new_key, KeySpace_PostNotificationStringFreePD);
    if (res == KVMODULE_ERR) KeySpace_PostNotificationStringFreePD(new_key);
    return KVMODULE_OK;
}

static int KeySpace_NotificationString(KVModuleCtx *ctx, int type, const char *event, KVModuleString *key){
    KVMODULE_NOT_USED(ctx);
    KVMODULE_NOT_USED(type);
    KVMODULE_NOT_USED(event);

    const char *key_str = KVModule_StringPtrLen(key, NULL);

    if (strncmp(key_str, "string_", 7) != 0) {
        return KVMODULE_OK;
    }

    if (strcmp(key_str, "string_total") == 0) {
        return KVMODULE_OK;
    }

    KVModuleString *new_key;
    if (strncmp(key_str, "string_changed{", 15) == 0) {
        new_key = KVModule_CreateString(NULL, "string_total", 12);
    } else {
        new_key = KVModule_CreateStringPrintf(NULL, "string_changed{%s}", key_str);
    }

    int res = KVModule_AddPostNotificationJob(ctx, KeySpace_PostNotificationString, new_key, KeySpace_PostNotificationStringFreePD);
    if (res == KVMODULE_ERR) KeySpace_PostNotificationStringFreePD(new_key);
    return KVMODULE_OK;
}

static int KeySpace_LazyExpireInsidePostNotificationJob(KVModuleCtx *ctx, int type, const char *event, KVModuleString *key){
    KVMODULE_NOT_USED(ctx);
    KVMODULE_NOT_USED(type);
    KVMODULE_NOT_USED(event);

    const char *key_str = KVModule_StringPtrLen(key, NULL);

    if (strncmp(key_str, "read_", 5) != 0) {
        return KVMODULE_OK;
    }

    KVModuleString *new_key = KVModule_CreateString(NULL, key_str + 5, strlen(key_str) - 5);;
    int res = KVModule_AddPostNotificationJob(ctx, KeySpace_PostNotificationReadKey, new_key, KeySpace_PostNotificationStringFreePD);
    if (res == KVMODULE_ERR) KeySpace_PostNotificationStringFreePD(new_key);
    return KVMODULE_OK;
}

static int KeySpace_NestedNotification(KVModuleCtx *ctx, int type, const char *event, KVModuleString *key){
    KVMODULE_NOT_USED(ctx);
    KVMODULE_NOT_USED(type);
    KVMODULE_NOT_USED(event);

    const char *key_str = KVModule_StringPtrLen(key, NULL);

    if (strncmp(key_str, "write_sync_", 11) != 0) {
        return KVMODULE_OK;
    }

    /* This test was only meant to check KVMODULE_OPTIONS_ALLOW_NESTED_KEYSPACE_NOTIFICATIONS.
     * In general it is wrong and discourage to perform any writes inside a notification callback.  */
    KVModuleString *new_key = KVModule_CreateString(NULL, key_str + 11, strlen(key_str) - 11);;
    KVModuleCallReply* rep = KVModule_Call(ctx, "set", "!sc", new_key, "1");
    KVModule_FreeCallReply(rep);
    KVModule_FreeString(NULL, new_key);
    return KVMODULE_OK;
}

static void *KeySpace_PostNotificationsAsyncSetInner(void *arg) {
    KVModuleBlockedClient *bc = arg;
    KVModuleCtx *ctx = KVModule_GetThreadSafeContext(bc);
    KVModule_ThreadSafeContextLock(ctx);
    KVModuleCallReply* rep = KVModule_Call(ctx, "set", "!cc", "string_x", "1");
    KVModule_ThreadSafeContextUnlock(ctx);
    KVModule_ReplyWithCallReply(ctx, rep);
    KVModule_FreeCallReply(rep);

    KVModule_UnblockClient(bc, NULL);
    KVModule_FreeThreadSafeContext(ctx);
    return NULL;
}

static int KeySpace_PostNotificationsAsyncSet(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    if (argc != 1)
        return KVModule_WrongArity(ctx);

    pthread_t tid;
    KVModuleBlockedClient *bc = KVModule_BlockClient(ctx,NULL,NULL,NULL,0);

    if (pthread_create(&tid,NULL,KeySpace_PostNotificationsAsyncSetInner,bc) != 0) {
        KVModule_AbortBlock(bc);
        return KVModule_ReplyWithError(ctx,"-ERR Can't start thread");
    }
    return KVMODULE_OK;
}

typedef struct KeySpace_EventPostNotificationCtx {
    KVModuleString *triggered_on;
    KVModuleString *new_key;
} KeySpace_EventPostNotificationCtx;

static void KeySpace_ServerEventPostNotificationFree(void *pd) {
    KeySpace_EventPostNotificationCtx *pn_ctx = pd;
    KVModule_FreeString(NULL, pn_ctx->new_key);
    KVModule_FreeString(NULL, pn_ctx->triggered_on);
    KVModule_Free(pn_ctx);
}

static void KeySpace_ServerEventPostNotification(KVModuleCtx *ctx, void *pd) {
    KVMODULE_NOT_USED(ctx);
    KeySpace_EventPostNotificationCtx *pn_ctx = pd;
    KVModuleCallReply* rep = KVModule_Call(ctx, "lpush", "!ss", pn_ctx->new_key, pn_ctx->triggered_on);
    KVModule_FreeCallReply(rep);
}

static void KeySpace_ServerEventCallback(KVModuleCtx *ctx, KVModuleEvent eid, uint64_t subevent, void *data) {
    KVMODULE_NOT_USED(eid);
    KVMODULE_NOT_USED(data);
    if (subevent > 3) {
        KVModule_Log(ctx, "warning", "Got an unexpected subevent '%llu'", (unsigned long long)subevent);
        return;
    }
    static const char* events[] = {
            "before_deleted",
            "before_expired",
            "before_evicted",
            "before_overwritten",
    };

    const KVModuleString *key_name = KVModule_GetKeyNameFromModuleKey(((KVModuleKeyInfo*)data)->key);
    const char *key_str = KVModule_StringPtrLen(key_name, NULL);

    for (int i = 0 ; i < 4 ; ++i) {
        const char *event = events[i];
        if (strncmp(key_str, event , strlen(event)) == 0) {
            return; /* don't log any event on our tracking keys */
        }
    }

    KeySpace_EventPostNotificationCtx *pn_ctx = KVModule_Alloc(sizeof(*pn_ctx));
    pn_ctx->triggered_on = KVModule_HoldString(NULL, (KVModuleString*)key_name);
    pn_ctx->new_key = KVModule_CreateString(NULL, events[subevent], strlen(events[subevent]));
    int res = KVModule_AddPostNotificationJob(ctx, KeySpace_ServerEventPostNotification, pn_ctx, KeySpace_ServerEventPostNotificationFree);
    if (res == KVMODULE_ERR) KeySpace_ServerEventPostNotificationFree(pn_ctx);
}

/* This function must be present on each module. It is used in order to
 * register the commands into the server. */
int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx,"postnotifications",1,KVMODULE_APIVER_1) == KVMODULE_ERR){
        return KVMODULE_ERR;
    }

    if (!(KVModule_GetModuleOptionsAll() & KVMODULE_OPTIONS_ALLOW_NESTED_KEYSPACE_NOTIFICATIONS)) {
        return KVMODULE_ERR;
    }

    int with_key_events = 0;
    if (argc >= 1) {
        const char *arg = KVModule_StringPtrLen(argv[0], 0);
        if (strcmp(arg, "with_key_events") == 0) {
            with_key_events = 1;
        }
    }

    KVModule_SetModuleOptions(ctx, KVMODULE_OPTIONS_ALLOW_NESTED_KEYSPACE_NOTIFICATIONS);

    if(KVModule_SubscribeToKeyspaceEvents(ctx, KVMODULE_NOTIFY_STRING, KeySpace_NotificationString) != KVMODULE_OK){
        return KVMODULE_ERR;
    }

    if(KVModule_SubscribeToKeyspaceEvents(ctx, KVMODULE_NOTIFY_STRING, KeySpace_LazyExpireInsidePostNotificationJob) != KVMODULE_OK){
        return KVMODULE_ERR;
    }

    if(KVModule_SubscribeToKeyspaceEvents(ctx, KVMODULE_NOTIFY_STRING, KeySpace_NestedNotification) != KVMODULE_OK){
        return KVMODULE_ERR;
    }

    if(KVModule_SubscribeToKeyspaceEvents(ctx, KVMODULE_NOTIFY_EXPIRED, KeySpace_NotificationExpired) != KVMODULE_OK){
        return KVMODULE_ERR;
    }

    if(KVModule_SubscribeToKeyspaceEvents(ctx, KVMODULE_NOTIFY_EVICTED, KeySpace_NotificationEvicted) != KVMODULE_OK){
        return KVMODULE_ERR;
    }

    if (with_key_events) {
        if(KVModule_SubscribeToServerEvent(ctx, KVModuleEvent_Key, KeySpace_ServerEventCallback) != KVMODULE_OK){
            return KVMODULE_ERR;
        }
    }

    if (KVModule_CreateCommand(ctx, "postnotification.async_set", KeySpace_PostNotificationsAsyncSet,
                                      "write", 0, 0, 0) == KVMODULE_ERR){
        return KVMODULE_ERR;
    }

    return KVMODULE_OK;
}

int KVModule_OnUnload(KVModuleCtx *ctx) {
    KVMODULE_NOT_USED(ctx);
    return KVMODULE_OK;
}
