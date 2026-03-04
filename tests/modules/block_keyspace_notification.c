/* This module is used to test blocking the client during a keyspace event. */
#define _BSD_SOURCE
#define _DEFAULT_SOURCE /* For usleep */

#include "kvmodule.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define EVENT_LOG_MAX_SIZE 1024

static pthread_mutex_t event_log_mutex = PTHREAD_MUTEX_INITIALIZER;
typedef struct KeyspaceEventData {
    KVModuleString *key;
    KVModuleString *event;
} KeyspaceEventData;

typedef struct KeyspaceEventLog {
    KeyspaceEventData *log[EVENT_LOG_MAX_SIZE];
    size_t next_index;
} KeyspaceEventLog;

KeyspaceEventLog *event_log = NULL;
int unloaded = 0;

typedef struct BackgroundThreadData {
    KeyspaceEventData *event;
    KVModuleBlockedClient *bc;
} BackgroundThreadData;

static void *GenericEvent_BackgroundWork(void *arg) {
    BackgroundThreadData *data = (BackgroundThreadData *)arg;
    // Sleep for 1 second
    sleep(1);
    pthread_mutex_lock(&event_log_mutex);
    if (!unloaded && event_log->next_index < EVENT_LOG_MAX_SIZE) {
        event_log->log[event_log->next_index] = data->event;
        event_log->next_index++;
    }
    pthread_mutex_unlock(&event_log_mutex);
    if (data->bc) {
        KVModule_UnblockClient(data->bc, NULL);
    }
    KVModule_Free(data);
    pthread_exit(NULL);
}

static int KeySpace_NotificationGeneric(KVModuleCtx *ctx, int type,
                                        const char *event,
                                        KVModuleString *key) {
    KVMODULE_NOT_USED(ctx);
    KVMODULE_NOT_USED(type);
    KVModuleString *retained_key = KVModule_HoldString(ctx, key);
    KVModuleBlockedClient *bc =
        KVModule_BlockClient(ctx, NULL, NULL, NULL, 0);
    if (bc == NULL) {
        KVModule_Log(ctx, KVMODULE_LOGLEVEL_NOTICE,
                         "Failed to block for event %s on %s!", event,
                         KVModule_StringPtrLen(key, NULL));
    }
    BackgroundThreadData *data =
        KVModule_Alloc(sizeof(BackgroundThreadData));
    data->bc = bc;
    KeyspaceEventData *event_data =
        KVModule_Alloc(sizeof(KeyspaceEventData));
    event_data->key = retained_key;
    event_data->event = KVModule_CreateString(ctx, event, strlen(event));
    data->event = event_data;
    pthread_t tid;
    pthread_create(&tid, NULL, GenericEvent_BackgroundWork, (void *)data);
    return KVMODULE_OK;
}

static int cmdGetEvents(KVModuleCtx *ctx, KVModuleString **argv,
                        int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    pthread_mutex_lock(&event_log_mutex);
    KVModule_ReplyWithArray(ctx, event_log->next_index);
    for (size_t i = 0; i < event_log->next_index; i++) {
        KVModule_ReplyWithArray(ctx, 4);
        KVModule_ReplyWithStringBuffer(ctx, "event", 5);
        KVModule_ReplyWithString(ctx, event_log->log[i]->event);
        KVModule_ReplyWithStringBuffer(ctx, "key", 3);
        KVModule_ReplyWithString(ctx, event_log->log[i]->key);
    }
    pthread_mutex_unlock(&event_log_mutex);
    return KVMODULE_OK;
}

static int cmdClearEvents(KVModuleCtx *ctx, KVModuleString **argv,
                          int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    pthread_mutex_lock(&event_log_mutex);
    for (size_t i = 0; i < event_log->next_index; i++) {
        KeyspaceEventData *data = event_log->log[i];
        KVModule_FreeString(ctx, data->event);
        KVModule_FreeString(ctx, data->key);
        KVModule_Free(data);
    }
    event_log->next_index = 0;
    KVModule_ReplyWithSimpleString(ctx, "OK");
    pthread_mutex_unlock(&event_log_mutex);
    return KVMODULE_OK;
}

/* This function must be present on each KV module. It is used in order to
 * register the commands into the KV server. */
int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv,
                        int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    if (KVModule_Init(ctx, "testblockingkeyspacenotif", 1,
                          KVMODULE_APIVER_1) == KVMODULE_ERR) {
        return KVMODULE_ERR;
    }
    event_log = KVModule_Alloc(sizeof(KeyspaceEventLog));
    event_log->next_index = 0;
    int keySpaceAll = KVModule_GetKeyspaceNotificationFlagsAll();
    if (!(keySpaceAll & KVMODULE_NOTIFY_LOADED)) {
        // KVMODULE_NOTIFY_LOADED event are not supported we can not start
        return KVMODULE_ERR;
    }
    if (KVModule_SubscribeToKeyspaceEvents(ctx, KVMODULE_NOTIFY_LOADED,
                                               KeySpace_NotificationGeneric) !=
            KVMODULE_OK ||
        KVModule_SubscribeToKeyspaceEvents(ctx, KVMODULE_NOTIFY_GENERIC,
                                               KeySpace_NotificationGeneric) !=
            KVMODULE_OK ||
        KVModule_SubscribeToKeyspaceEvents(ctx, KVMODULE_NOTIFY_EXPIRED,
                                               KeySpace_NotificationGeneric) !=
            KVMODULE_OK ||
        KVModule_SubscribeToKeyspaceEvents(ctx, KVMODULE_NOTIFY_MODULE,
                                               KeySpace_NotificationGeneric) !=
            KVMODULE_OK ||
        KVModule_SubscribeToKeyspaceEvents(
            ctx, KVMODULE_NOTIFY_KEY_MISS, KeySpace_NotificationGeneric) !=
            KVMODULE_OK ||
        KVModule_SubscribeToKeyspaceEvents(ctx, KVMODULE_NOTIFY_STRING,
                                               KeySpace_NotificationGeneric) !=
            KVMODULE_OK ||
        KVModule_SubscribeToKeyspaceEvents(ctx, KVMODULE_NOTIFY_HASH,
                                               KeySpace_NotificationGeneric) !=
            KVMODULE_OK ||
        KVModule_CreateCommand(ctx, "b_keyspace.events", cmdGetEvents, "",
                                   0, 0, 0) == KVMODULE_ERR ||
        KVModule_CreateCommand(ctx, "b_keyspace.clear", cmdClearEvents, "",
                                   0, 0, 0) == KVMODULE_ERR) {
        return KVMODULE_ERR;
    }
    return KVMODULE_OK;
}

int KVModule_OnUnload(KVModuleCtx *ctx) {
    pthread_mutex_lock(&event_log_mutex);
    unloaded = 1;
    for (size_t i = 0; i < event_log->next_index; i++) {
        KeyspaceEventData *data = event_log->log[i];
        KVModule_FreeString(ctx, data->event);
        KVModule_FreeString(ctx, data->key);
        KVModule_Free(data);
    }
    KVModule_Free(event_log);
    pthread_mutex_unlock(&event_log_mutex);
    return KVMODULE_OK;
}
