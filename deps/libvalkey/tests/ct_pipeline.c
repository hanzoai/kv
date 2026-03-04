#include "adapters/libevent.h"
#include "cluster.h"
#include "test_utils.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLUSTER_NODE "127.0.0.1:7000"

// Test of two pipelines using sync API
void test_pipeline(void) {
    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE;

    kvClusterContext *cc = kvClusterConnectWithOptions(&options);
    ASSERT_MSG(cc && cc->err == 0, cc ? cc->errstr : "OOM");

    int status;
    status = kvClusterAppendCommand(cc, "SET foo one");
    ASSERT_MSG(status == KV_OK, cc->errstr);
    status = kvClusterAppendCommand(cc, "SET bar two");
    ASSERT_MSG(status == KV_OK, cc->errstr);
    status = kvClusterAppendCommand(cc, "GET foo");
    ASSERT_MSG(status == KV_OK, cc->errstr);
    status = kvClusterAppendCommand(cc, "GET bar");
    ASSERT_MSG(status == KV_OK, cc->errstr);
    status = kvClusterAppendCommand(cc, "SUNION a b");
    ASSERT_MSG(status == KV_OK, cc->errstr);

    kvReply *reply;
    kvClusterGetReply(cc, (void *)&reply); // reply for: SET foo one
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    kvClusterGetReply(cc, (void *)&reply); // reply for: SET bar two
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    kvClusterGetReply(cc, (void *)&reply); // reply for: GET foo
    CHECK_REPLY_STR(cc, reply, "one");
    freeReplyObject(reply);

    kvClusterGetReply(cc, (void *)&reply); // reply for: GET bar
    CHECK_REPLY_STR(cc, reply, "two");
    freeReplyObject(reply);

    kvClusterGetReply(cc, (void *)&reply); // reply for: SUNION a b
    CHECK_REPLY_ERROR(cc, reply, "CROSSSLOT");
    freeReplyObject(reply);

    kvClusterFree(cc);
}

//------------------------------------------------------------------------------
// Async API
//------------------------------------------------------------------------------

typedef struct ExpectedResult {
    int type;
    const char *str;
    bool disconnect;
} ExpectedResult;

// Callback for KV connects and disconnects
void connectCallback(kvAsyncContext *ac, int status) {
    UNUSED(ac);
    assert(status == KV_OK);
}
void disconnectCallback(const kvAsyncContext *ac, int status) {
    UNUSED(ac);
    assert(status == KV_OK);
}

// Callback for async commands, verifies the kvReply
void commandCallback(kvClusterAsyncContext *cc, void *r, void *privdata) {
    kvReply *reply = (kvReply *)r;
    ExpectedResult *expect = (ExpectedResult *)privdata;
    assert(reply != NULL);
    assert(reply->type == expect->type);
    assert(strcmp(reply->str, expect->str) == 0);

    if (expect->disconnect) {
        kvClusterAsyncDisconnect(cc);
    }
}

// Test of two pipelines using async API
// In an asynchronous context, commands are automatically pipelined due to the
// nature of an event loop. Therefore, unlike the synchronous API, there is only
// a single way to send commands.
void test_async_pipeline(void) {
    struct event_base *base = event_base_new();

    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE;
    options.options = KV_OPT_BLOCKING_INITIAL_UPDATE;
    options.async_connect_callback = connectCallback;
    options.async_disconnect_callback = disconnectCallback;
    kvClusterOptionsUseLibevent(&options, base);

    kvClusterAsyncContext *acc = kvClusterAsyncConnectWithOptions(&options);
    ASSERT_MSG(acc && acc->err == 0, acc ? acc->errstr : "OOM");

    int status;
    ExpectedResult r1 = {.type = KV_REPLY_STATUS, .str = "OK"};
    status =
        kvClusterAsyncCommand(acc, commandCallback, &r1, "SET foo six");
    ASSERT_MSG(status == KV_OK, acc->errstr);

    ExpectedResult r2 = {.type = KV_REPLY_STATUS, .str = "OK"};
    status =
        kvClusterAsyncCommand(acc, commandCallback, &r2, "SET bar ten");
    ASSERT_MSG(status == KV_OK, acc->errstr);

    ExpectedResult r3 = {.type = KV_REPLY_STRING, .str = "six"};
    status = kvClusterAsyncCommand(acc, commandCallback, &r3, "GET foo");
    ASSERT_MSG(status == KV_OK, acc->errstr);

    ExpectedResult r4 = {.type = KV_REPLY_STRING, .str = "ten"};
    status = kvClusterAsyncCommand(acc, commandCallback, &r4, "GET bar");
    ASSERT_MSG(status == KV_OK, acc->errstr);

    ExpectedResult r5 = {
        .type = KV_REPLY_ERROR,
        .str = "CROSSSLOT Keys in request don't hash to the same slot",
        .disconnect = true};
    status = kvClusterAsyncCommand(acc, commandCallback, &r5, "SUNION a b");
    ASSERT_MSG(status == KV_OK, acc->errstr);

    event_base_dispatch(base);

    kvClusterAsyncFree(acc);
    event_base_free(base);
}

int main(void) {

    test_pipeline();

    test_async_pipeline();

    return 0;
}
