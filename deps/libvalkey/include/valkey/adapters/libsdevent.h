#ifndef KV_ADAPTERS_LIBSDEVENT_H
#define KV_ADAPTERS_LIBSDEVENT_H
#include "../async.h"
#include "../cluster.h"
#include "../kv.h"

#include <systemd/sd-event.h>

#define KV_LIBSDEVENT_DELETED 0x01
#define KV_LIBSDEVENT_ENTERED 0x02

typedef struct kvLibsdeventEvents {
    kvAsyncContext *context;
    struct sd_event *event;
    struct sd_event_source *fdSource;
    struct sd_event_source *timerSource;
    int fd;
    short flags;
    short state;
} kvLibsdeventEvents;

static void kvLibsdeventDestroy(kvLibsdeventEvents *e) {
    if (e->fdSource) {
        e->fdSource = sd_event_source_disable_unref(e->fdSource);
    }
    if (e->timerSource) {
        e->timerSource = sd_event_source_disable_unref(e->timerSource);
    }
    sd_event_unref(e->event);
    vk_free(e);
}

static int kvLibsdeventTimeoutHandler(sd_event_source *s, uint64_t usec, void *userdata) {
    ((void)s);
    ((void)usec);
    kvLibsdeventEvents *e = (kvLibsdeventEvents *)userdata;
    kvAsyncHandleTimeout(e->context);
    return 0;
}

static int kvLibsdeventHandler(sd_event_source *s, int fd, uint32_t event, void *userdata) {
    ((void)s);
    ((void)fd);
    kvLibsdeventEvents *e = (kvLibsdeventEvents *)userdata;
    e->state |= KV_LIBSDEVENT_ENTERED;

#define CHECK_DELETED()                         \
    if (e->state & KV_LIBSDEVENT_DELETED) { \
        kvLibsdeventDestroy(e);             \
        return 0;                               \
    }

    if ((event & EPOLLIN) && e->context && (e->state & KV_LIBSDEVENT_DELETED) == 0) {
        kvAsyncHandleRead(e->context);
        CHECK_DELETED();
    }

    if ((event & EPOLLOUT) && e->context && (e->state & KV_LIBSDEVENT_DELETED) == 0) {
        kvAsyncHandleWrite(e->context);
        CHECK_DELETED();
    }

    e->state &= ~KV_LIBSDEVENT_ENTERED;
#undef CHECK_DELETED

    return 0;
}

static void kvLibsdeventAddRead(void *userdata) {
    kvLibsdeventEvents *e = (kvLibsdeventEvents *)userdata;

    if (e->flags & EPOLLIN) {
        return;
    }

    e->flags |= EPOLLIN;

    if (e->flags & EPOLLOUT) {
        sd_event_source_set_io_events(e->fdSource, e->flags);
    } else {
        sd_event_add_io(e->event, &e->fdSource, e->fd, e->flags, kvLibsdeventHandler, e);
    }
}

static void kvLibsdeventDelRead(void *userdata) {
    kvLibsdeventEvents *e = (kvLibsdeventEvents *)userdata;

    e->flags &= ~EPOLLIN;

    if (e->flags) {
        sd_event_source_set_io_events(e->fdSource, e->flags);
    } else {
        e->fdSource = sd_event_source_disable_unref(e->fdSource);
    }
}

static void kvLibsdeventAddWrite(void *userdata) {
    kvLibsdeventEvents *e = (kvLibsdeventEvents *)userdata;

    if (e->flags & EPOLLOUT) {
        return;
    }

    e->flags |= EPOLLOUT;

    if (e->flags & EPOLLIN) {
        sd_event_source_set_io_events(e->fdSource, e->flags);
    } else {
        sd_event_add_io(e->event, &e->fdSource, e->fd, e->flags, kvLibsdeventHandler, e);
    }
}

static void kvLibsdeventDelWrite(void *userdata) {
    kvLibsdeventEvents *e = (kvLibsdeventEvents *)userdata;

    e->flags &= ~EPOLLOUT;

    if (e->flags) {
        sd_event_source_set_io_events(e->fdSource, e->flags);
    } else {
        e->fdSource = sd_event_source_disable_unref(e->fdSource);
    }
}

static void kvLibsdeventCleanup(void *userdata) {
    kvLibsdeventEvents *e = (kvLibsdeventEvents *)userdata;

    if (!e) {
        return;
    }

    if (e->state & KV_LIBSDEVENT_ENTERED) {
        e->state |= KV_LIBSDEVENT_DELETED;
    } else {
        kvLibsdeventDestroy(e);
    }
}

static void kvLibsdeventSetTimeout(void *userdata, struct timeval tv) {
    kvLibsdeventEvents *e = (kvLibsdeventEvents *)userdata;

    uint64_t usec = tv.tv_sec * 1000000 + tv.tv_usec;
    if (!e->timerSource) {
        sd_event_add_time_relative(e->event, &e->timerSource, CLOCK_MONOTONIC, usec, 1, kvLibsdeventTimeoutHandler, e);
    } else {
        sd_event_source_set_time_relative(e->timerSource, usec);
    }
}

static int kvLibsdeventAttach(kvAsyncContext *ac, struct sd_event *event) {
    kvContext *c = &(ac->c);
    kvLibsdeventEvents *e;

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return KV_ERR;

    /* Create container for context and r/w events */
    e = (kvLibsdeventEvents *)vk_calloc(1, sizeof(*e));
    if (e == NULL)
        return KV_ERR;

    /* Initialize and increase event refcount */
    e->context = ac;
    e->event = event;
    e->fd = c->fd;
    sd_event_ref(event);

    /* Register functions to start/stop listening for events */
    ac->ev.addRead = kvLibsdeventAddRead;
    ac->ev.delRead = kvLibsdeventDelRead;
    ac->ev.addWrite = kvLibsdeventAddWrite;
    ac->ev.delWrite = kvLibsdeventDelWrite;
    ac->ev.cleanup = kvLibsdeventCleanup;
    ac->ev.scheduleTimer = kvLibsdeventSetTimeout;
    ac->ev.data = e;

    return KV_OK;
}

/* Internal adapter function with correct function signature. */
static int kvLibsdeventAttachAdapter(kvAsyncContext *ac, void *event) {
    return kvLibsdeventAttach(ac, (struct sd_event *)event);
}

KV_UNUSED
static int kvClusterOptionsUseLibsdevent(kvClusterOptions *options,
                                             struct sd_event *event) {
    if (options == NULL || event == NULL) {
        return KV_ERR;
    }

    options->attach_fn = kvLibsdeventAttachAdapter;
    options->attach_data = event;
    return KV_OK;
}

#endif /* KV_ADAPTERS_LIBSDEVENT_H */
