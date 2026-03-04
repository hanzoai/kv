#ifndef KV_ADAPTERS_LIBHV_H
#define KV_ADAPTERS_LIBHV_H

#include "../async.h"
#include "../cluster.h"
#include "../kv.h"

#include <hv/hloop.h>

typedef struct kvLibhvEvents {
    hio_t *io;
    htimer_t *timer;
} kvLibhvEvents;

static void kvLibhvHandleEvents(hio_t *io) {
    kvAsyncContext *context = (kvAsyncContext *)hevent_userdata(io);
    int events = hio_events(io);
    int revents = hio_revents(io);
    if (context && (events & HV_READ) && (revents & HV_READ)) {
        kvAsyncHandleRead(context);
    }
    if (context && (events & HV_WRITE) && (revents & HV_WRITE)) {
        kvAsyncHandleWrite(context);
    }
}

static void kvLibhvAddRead(void *privdata) {
    kvLibhvEvents *events = (kvLibhvEvents *)privdata;
    hio_add(events->io, kvLibhvHandleEvents, HV_READ);
}

static void kvLibhvDelRead(void *privdata) {
    kvLibhvEvents *events = (kvLibhvEvents *)privdata;
    hio_del(events->io, HV_READ);
}

static void kvLibhvAddWrite(void *privdata) {
    kvLibhvEvents *events = (kvLibhvEvents *)privdata;
    hio_add(events->io, kvLibhvHandleEvents, HV_WRITE);
}

static void kvLibhvDelWrite(void *privdata) {
    kvLibhvEvents *events = (kvLibhvEvents *)privdata;
    hio_del(events->io, HV_WRITE);
}

static void kvLibhvCleanup(void *privdata) {
    kvLibhvEvents *events = (kvLibhvEvents *)privdata;

    if (events->timer)
        htimer_del(events->timer);

    hio_close(events->io);
    hevent_set_userdata(events->io, NULL);

    vk_free(events);
}

static void kvLibhvTimeout(htimer_t *timer) {
    hio_t *io = (hio_t *)hevent_userdata(timer);
    kvAsyncHandleTimeout((kvAsyncContext *)hevent_userdata(io));
}

static void kvLibhvSetTimeout(void *privdata, struct timeval tv) {
    kvLibhvEvents *events;
    uint32_t millis;
    hloop_t *loop;

    events = (kvLibhvEvents *)privdata;
    millis = tv.tv_sec * 1000 + tv.tv_usec / 1000;

    if (millis == 0) {
        /* Libhv disallows zero'd timers so treat this as a delete or NO OP */
        if (events->timer) {
            htimer_del(events->timer);
            events->timer = NULL;
        }
    } else if (events->timer == NULL) {
        /* Add new timer */
        loop = hevent_loop(events->io);
        events->timer = htimer_add(loop, kvLibhvTimeout, millis, 1);
        hevent_set_userdata(events->timer, events->io);
    } else {
        /* Update existing timer */
        htimer_reset(events->timer, millis);
    }
}

static int kvLibhvAttach(kvAsyncContext *ac, hloop_t *loop) {
    kvContext *c = &(ac->c);
    kvLibhvEvents *events;
    hio_t *io = NULL;

    if (ac->ev.data != NULL) {
        return KV_ERR;
    }

    /* Create container struct to keep track of our io and any timer */
    events = (kvLibhvEvents *)vk_malloc(sizeof(*events));
    if (events == NULL) {
        return KV_ERR;
    }

    io = hio_get(loop, c->fd);
    if (io == NULL) {
        vk_free(events);
        return KV_ERR;
    }

    hevent_set_userdata(io, ac);

    events->io = io;
    events->timer = NULL;

    ac->ev.addRead = kvLibhvAddRead;
    ac->ev.delRead = kvLibhvDelRead;
    ac->ev.addWrite = kvLibhvAddWrite;
    ac->ev.delWrite = kvLibhvDelWrite;
    ac->ev.cleanup = kvLibhvCleanup;
    ac->ev.scheduleTimer = kvLibhvSetTimeout;
    ac->ev.data = events;

    return KV_OK;
}

/* Internal adapter function with correct function signature. */
static int kvLibhvAttachAdapter(kvAsyncContext *ac, void *loop) {
    return kvLibhvAttach(ac, (hloop_t *)loop);
}

KV_UNUSED
static int kvClusterOptionsUseLibhv(kvClusterOptions *options,
                                        hloop_t *loop) {
    if (options == NULL || loop == NULL) {
        return KV_ERR;
    }

    options->attach_fn = kvLibhvAttachAdapter;
    options->attach_data = loop;
    return KV_OK;
}

#endif /* KV_ADAPTERS_LIBHV_H */
