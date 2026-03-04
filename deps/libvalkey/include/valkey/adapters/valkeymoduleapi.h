#ifndef KV_ADAPTERS_KVMODULEAPI_H
#define KV_ADAPTERS_KVMODULEAPI_H

#include "../async.h"
#include "../kv.h"
#include "kvmodule.h"

#include <sys/types.h>

typedef struct kvModuleEvents {
    kvAsyncContext *context;
    KVModuleCtx *module_ctx;
    int fd;
    int reading, writing;
    int timer_active;
    KVModuleTimerID timer_id;
} kvModuleEvents;

static inline void kvModuleReadEvent(int fd, void *privdata, int mask) {
    (void)fd;
    (void)mask;

    kvModuleEvents *e = (kvModuleEvents *)privdata;
    kvAsyncHandleRead(e->context);
}

static inline void kvModuleWriteEvent(int fd, void *privdata, int mask) {
    (void)fd;
    (void)mask;

    kvModuleEvents *e = (kvModuleEvents *)privdata;
    kvAsyncHandleWrite(e->context);
}

static inline void kvModuleAddRead(void *privdata) {
    kvModuleEvents *e = (kvModuleEvents *)privdata;
    if (!e->reading) {
        e->reading = 1;
        KVModule_EventLoopAdd(e->fd, KVMODULE_EVENTLOOP_READABLE, kvModuleReadEvent, e);
    }
}

static inline void kvModuleDelRead(void *privdata) {
    kvModuleEvents *e = (kvModuleEvents *)privdata;
    if (e->reading) {
        e->reading = 0;
        KVModule_EventLoopDel(e->fd, KVMODULE_EVENTLOOP_READABLE);
    }
}

static inline void kvModuleAddWrite(void *privdata) {
    kvModuleEvents *e = (kvModuleEvents *)privdata;
    if (!e->writing) {
        e->writing = 1;
        KVModule_EventLoopAdd(e->fd, KVMODULE_EVENTLOOP_WRITABLE, kvModuleWriteEvent, e);
    }
}

static inline void kvModuleDelWrite(void *privdata) {
    kvModuleEvents *e = (kvModuleEvents *)privdata;
    if (e->writing) {
        e->writing = 0;
        KVModule_EventLoopDel(e->fd, KVMODULE_EVENTLOOP_WRITABLE);
    }
}

static inline void kvModuleStopTimer(void *privdata) {
    kvModuleEvents *e = (kvModuleEvents *)privdata;
    if (e->timer_active) {
        KVModule_StopTimer(e->module_ctx, e->timer_id, NULL);
    }
    e->timer_active = 0;
}

static inline void kvModuleCleanup(void *privdata) {
    kvModuleEvents *e = (kvModuleEvents *)privdata;
    kvModuleDelRead(privdata);
    kvModuleDelWrite(privdata);
    kvModuleStopTimer(privdata);
    vk_free(e);
}

static inline void kvModuleTimeout(KVModuleCtx *ctx, void *privdata) {
    (void)ctx;

    kvModuleEvents *e = (kvModuleEvents *)privdata;
    e->timer_active = 0;
    kvAsyncHandleTimeout(e->context);
}

static inline void kvModuleSetTimeout(void *privdata, struct timeval tv) {
    kvModuleEvents *e = (kvModuleEvents *)privdata;

    kvModuleStopTimer(privdata);

    mstime_t millis = tv.tv_sec * 1000 + tv.tv_usec / 1000.0;
    e->timer_id = KVModule_CreateTimer(e->module_ctx, millis, kvModuleTimeout, e);
    e->timer_active = 1;
}

/* Check if Redis version is compatible with the adapter. */
static inline int kvModuleCompatibilityCheck(void) {
    if (!KVModule_EventLoopAdd ||
        !KVModule_EventLoopDel ||
        !KVModule_CreateTimer ||
        !KVModule_StopTimer) {
        return KV_ERR;
    }
    return KV_OK;
}

static inline int kvModuleAttach(kvAsyncContext *ac, KVModuleCtx *module_ctx) {
    kvContext *c = &(ac->c);
    kvModuleEvents *e;

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return KV_ERR;

    /* Create container for context and r/w events */
    e = (kvModuleEvents *)vk_malloc(sizeof(*e));
    if (e == NULL)
        return KV_ERR;

    e->context = ac;
    e->module_ctx = module_ctx;
    e->fd = c->fd;
    e->reading = e->writing = 0;
    e->timer_active = 0;

    /* Register functions to start/stop listening for events */
    ac->ev.addRead = kvModuleAddRead;
    ac->ev.delRead = kvModuleDelRead;
    ac->ev.addWrite = kvModuleAddWrite;
    ac->ev.delWrite = kvModuleDelWrite;
    ac->ev.cleanup = kvModuleCleanup;
    ac->ev.scheduleTimer = kvModuleSetTimeout;
    ac->ev.data = e;

    return KV_OK;
}

#endif /* KV_ADAPTERS_KVMODULEAPI_H */
