#ifndef KV_ADAPTERS_IVYKIS_H
#define KV_ADAPTERS_IVYKIS_H
#include "../async.h"
#include "../cluster.h"
#include "../kv.h"

#include <iv.h>

typedef struct kvIvykisEvents {
    kvAsyncContext *context;
    struct iv_fd fd;
} kvIvykisEvents;

static void kvIvykisReadEvent(void *arg) {
    kvAsyncContext *context = (kvAsyncContext *)arg;
    kvAsyncHandleRead(context);
}

static void kvIvykisWriteEvent(void *arg) {
    kvAsyncContext *context = (kvAsyncContext *)arg;
    kvAsyncHandleWrite(context);
}

static void kvIvykisAddRead(void *privdata) {
    kvIvykisEvents *e = (kvIvykisEvents *)privdata;
    iv_fd_set_handler_in(&e->fd, kvIvykisReadEvent);
}

static void kvIvykisDelRead(void *privdata) {
    kvIvykisEvents *e = (kvIvykisEvents *)privdata;
    iv_fd_set_handler_in(&e->fd, NULL);
}

static void kvIvykisAddWrite(void *privdata) {
    kvIvykisEvents *e = (kvIvykisEvents *)privdata;
    iv_fd_set_handler_out(&e->fd, kvIvykisWriteEvent);
}

static void kvIvykisDelWrite(void *privdata) {
    kvIvykisEvents *e = (kvIvykisEvents *)privdata;
    iv_fd_set_handler_out(&e->fd, NULL);
}

static void kvIvykisCleanup(void *privdata) {
    kvIvykisEvents *e = (kvIvykisEvents *)privdata;

    iv_fd_unregister(&e->fd);
    vk_free(e);
}

static int kvIvykisAttach(kvAsyncContext *ac) {
    kvContext *c = &(ac->c);
    kvIvykisEvents *e;

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return KV_ERR;

    /* Create container for context and r/w events */
    e = (kvIvykisEvents *)vk_malloc(sizeof(*e));
    if (e == NULL)
        return KV_ERR;

    e->context = ac;

    /* Register functions to start/stop listening for events */
    ac->ev.addRead = kvIvykisAddRead;
    ac->ev.delRead = kvIvykisDelRead;
    ac->ev.addWrite = kvIvykisAddWrite;
    ac->ev.delWrite = kvIvykisDelWrite;
    ac->ev.cleanup = kvIvykisCleanup;
    ac->ev.data = e;

    /* Initialize and install read/write events */
    IV_FD_INIT(&e->fd);
    e->fd.fd = c->fd;
    e->fd.handler_in = kvIvykisReadEvent;
    e->fd.handler_out = kvIvykisWriteEvent;
    e->fd.handler_err = NULL;
    e->fd.cookie = e->context;

    iv_fd_register(&e->fd);

    return KV_OK;
}

/* Internal adapter function with correct function signature. */
static int kvIvykisAttachAdapter(kvAsyncContext *ac, KV_UNUSED void *) {
    return kvIvykisAttach(ac);
}

KV_UNUSED
static int kvClusterOptionsUseIvykis(kvClusterOptions *options) {
    if (options == NULL) {
        return KV_ERR;
    }

    options->attach_fn = kvIvykisAttachAdapter;
    return KV_OK;
}

#endif /* KV_ADAPTERS_IVYKIS_H */
