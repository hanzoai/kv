#include "adapters/glib.h"
#include "cluster.h"
#include "test_utils.h"

#include <assert.h>

#define CLUSTER_NODE "127.0.0.1:7000"

static GMainLoop *mainloop;

void setCallback(kvClusterAsyncContext *acc, void *r, void *privdata) {
    UNUSED(privdata);
    kvReply *reply = (kvReply *)r;
    ASSERT_MSG(reply != NULL, acc->errstr);
}

void getCallback(kvClusterAsyncContext *acc, void *r, void *privdata) {
    UNUSED(privdata);
    kvReply *reply = (kvReply *)r;
    ASSERT_MSG(reply != NULL, acc->errstr);

    /* Disconnect after receiving the first reply to GET */
    kvClusterAsyncDisconnect(acc);
    g_main_loop_quit(mainloop);
}

void connectCallback(kvAsyncContext *ac, int status) {
    ASSERT_MSG(status == KV_OK, ac->errstr);
    printf("Connected to %s:%d\n", ac->c.tcp.host, ac->c.tcp.port);
}

void disconnectCallback(const kvAsyncContext *ac, int status) {
    ASSERT_MSG(status == KV_OK, ac->errstr);
    printf("Disconnected from %s:%d\n", ac->c.tcp.host, ac->c.tcp.port);
}

int main(int argc, char **argv) {
    UNUSED(argc);
    UNUSED(argv);

    GMainContext *context = NULL;
    mainloop = g_main_loop_new(context, FALSE);

    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE;
    options.options = KV_OPT_BLOCKING_INITIAL_UPDATE;
    options.async_connect_callback = connectCallback;
    options.async_disconnect_callback = disconnectCallback;
    kvClusterOptionsUseGlib(&options, context);

    kvClusterAsyncContext *acc = kvClusterAsyncConnectWithOptions(&options);
    assert(acc);
    ASSERT_MSG(acc->err == 0, acc->errstr);

    int status;
    status = kvClusterAsyncCommand(acc, setCallback, (char *)"id", "SET key value");
    ASSERT_MSG(status == KV_OK, acc->errstr);

    status = kvClusterAsyncCommand(acc, getCallback, (char *)"id", "GET key");
    ASSERT_MSG(status == KV_OK, acc->errstr);

    g_main_loop_run(mainloop);

    kvClusterAsyncFree(acc);
    return 0;
}
