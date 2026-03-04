#ifndef KV_ADAPTERS_GLIB_H
#define KV_ADAPTERS_GLIB_H

#include "../async.h"
#include "../cluster.h"
#include "../kv.h"

#include <glib.h>

typedef struct
{
    GSource source;
    kvAsyncContext *ac;
    GPollFD poll_fd;
} KVSource;

static void
kv_source_add_read(gpointer data) {
    KVSource *source = (KVSource *)data;
    g_return_if_fail(source);
    source->poll_fd.events |= G_IO_IN;
    g_main_context_wakeup(g_source_get_context((GSource *)data));
}

static void
kv_source_del_read(gpointer data) {
    KVSource *source = (KVSource *)data;
    g_return_if_fail(source);
    source->poll_fd.events &= ~G_IO_IN;
    g_main_context_wakeup(g_source_get_context((GSource *)data));
}

static void
kv_source_add_write(gpointer data) {
    KVSource *source = (KVSource *)data;
    g_return_if_fail(source);
    source->poll_fd.events |= G_IO_OUT;
    g_main_context_wakeup(g_source_get_context((GSource *)data));
}

static void
kv_source_del_write(gpointer data) {
    KVSource *source = (KVSource *)data;
    g_return_if_fail(source);
    source->poll_fd.events &= ~G_IO_OUT;
    g_main_context_wakeup(g_source_get_context((GSource *)data));
}

static void
kv_source_cleanup(gpointer data) {
    KVSource *source = (KVSource *)data;

    g_return_if_fail(source);

    kv_source_del_read(source);
    kv_source_del_write(source);
    /*
     * It is not our responsibility to remove ourself from the
     * current main loop. However, we will remove the GPollFD.
     */
    if (source->poll_fd.fd >= 0) {
        g_source_remove_poll((GSource *)data, &source->poll_fd);
        source->poll_fd.fd = -1;
    }
}

static gboolean
kv_source_prepare(GSource *source,
                      gint *timeout_) {
    KVSource *kv = (KVSource *)source;
    *timeout_ = -1;
    return !!(kv->poll_fd.events & kv->poll_fd.revents);
}

static gboolean
kv_source_check(GSource *source) {
    KVSource *kv = (KVSource *)source;
    return !!(kv->poll_fd.events & kv->poll_fd.revents);
}

static gboolean
kv_source_dispatch(GSource *source,
                       GSourceFunc callback,
                       gpointer user_data) {
    KVSource *kv = (KVSource *)source;

    if ((kv->poll_fd.revents & G_IO_OUT)) {
        kvAsyncHandleWrite(kv->ac);
        kv->poll_fd.revents &= ~G_IO_OUT;
    }

    if ((kv->poll_fd.revents & G_IO_IN)) {
        kvAsyncHandleRead(kv->ac);
        kv->poll_fd.revents &= ~G_IO_IN;
    }

    if (callback) {
        return callback(user_data);
    }

    return TRUE;
}

static void
kv_source_finalize(GSource *source) {
    KVSource *kv = (KVSource *)source;

    if (kv->poll_fd.fd >= 0) {
        g_source_remove_poll(source, &kv->poll_fd);
        kv->poll_fd.fd = -1;
    }
}

static GSource *
kv_source_new(kvAsyncContext *ac) {
    static GSourceFuncs source_funcs = {
        .prepare = kv_source_prepare,
        .check = kv_source_check,
        .dispatch = kv_source_dispatch,
        .finalize = kv_source_finalize,
    };
    kvContext *c = &ac->c;
    KVSource *source;

    g_return_val_if_fail(ac != NULL, NULL);

    source = (KVSource *)g_source_new(&source_funcs, sizeof *source);
    if (source == NULL)
        return NULL;

    source->ac = ac;
    source->poll_fd.fd = c->fd;
    source->poll_fd.events = 0;
    source->poll_fd.revents = 0;
    g_source_add_poll((GSource *)source, &source->poll_fd);

    ac->ev.addRead = kv_source_add_read;
    ac->ev.delRead = kv_source_del_read;
    ac->ev.addWrite = kv_source_add_write;
    ac->ev.delWrite = kv_source_del_write;
    ac->ev.cleanup = kv_source_cleanup;
    ac->ev.data = source;

    return (GSource *)source;
}

/* Internal adapter function with correct function signature. */
static int kvGlibAttachAdapter(kvAsyncContext *ac, void *context) {
    if (g_source_attach(kv_source_new(ac), (GMainContext *)context) > 0) {
        return KV_OK;
    }
    return KV_ERR;
}

KV_UNUSED
static int kvClusterOptionsUseGlib(kvClusterOptions *options,
                                       GMainContext *context) {
    if (options == NULL) { // A NULL context is accepted.
        return KV_ERR;
    }

    options->attach_fn = kvGlibAttachAdapter;
    options->attach_data = context;
    return KV_OK;
}

#endif /* KV_ADAPTERS_GLIB_H */
