#include "adapters/libevent.h"
#include "cluster.h"
#include "test_utils.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLUSTER_NODE "127.0.0.1:7000"
#define CLUSTER_NODE_WITH_PASSWORD "127.0.0.1:7100"
#define CLUSTER_USERNAME "default"
#define CLUSTER_PASSWORD "secretword"

int connect_success_counter;
int connect_failure_counter;
void connect_callback(const kvContext *c, int status) {
    (void)c;
    if (status == KV_OK)
        connect_success_counter++;
    else
        connect_failure_counter++;
}
void reset_counters(void) {
    connect_success_counter = connect_failure_counter = 0;
}

/* Creating a context using unsupported client options should give errors */
void test_unsupported_option(void) {
    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE;
    options.options = KV_OPT_PREFER_IPV4;
    options.options |= KV_OPT_NONBLOCK; /* unsupported in cluster */

    kvClusterContext *cc = kvClusterConnectWithOptions(&options);
    assert(cc);
    assert(strcmp(cc->errstr, "Unsupported options") == 0);

    kvClusterFree(cc);
}

// Connecting to a password protected cluster and
// providing a correct password.
void test_password_ok(void) {
    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE_WITH_PASSWORD;
    options.password = CLUSTER_PASSWORD;
    options.connect_callback = connect_callback;

    kvClusterContext *cc = kvClusterConnectWithOptions(&options);
    ASSERT_MSG(cc && cc->err == 0, cc ? cc->errstr : "OOM");
    assert(connect_success_counter == 1); // for CLUSTER SLOTS
    load_kv_version(cc);
    // Check that the initial slotmap update connection is reused.
    assert(connect_success_counter == 1);

    // Test connection
    kvReply *reply;
    reply = kvClusterCommand(cc, "SET key1 Hello");
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);
    kvClusterFree(cc);

    // Check counters incremented by connect callback
    assert(connect_success_counter == 2); // for SET (to a different node)
    assert(connect_failure_counter == 0);
    reset_counters();
}

// Connecting to a password protected cluster and
// providing wrong password.
void test_password_wrong(void) {
    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE_WITH_PASSWORD;
    options.password = "faultypass";

    kvClusterContext *cc = kvClusterConnectWithOptions(&options);
    assert(cc);

    assert(cc->err == KV_ERR_OTHER);
    if (kv_version_less_than(6, 0))
        assert(strcmp(cc->errstr, "ERR invalid password") == 0);
    else
        assert(strncmp(cc->errstr, "WRONGPASS", 9) == 0);

    kvClusterFree(cc);
}

// Connecting to a password protected cluster and
// not providing any password.
void test_password_missing(void) {
    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE_WITH_PASSWORD;
    // No password set.

    kvClusterContext *cc = kvClusterConnectWithOptions(&options);
    assert(cc);

    assert(cc->err == KV_ERR_OTHER);
    assert(strncmp(cc->errstr, "NOAUTH", 6) == 0);

    kvClusterFree(cc);
}

// Connect to a cluster and authenticate using username and password,
// i.e. 'AUTH <username> <password>'
void test_username_ok(void) {
    if (kv_version_less_than(6, 0))
        return;

    // Connect to the cluster using username and password
    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE_WITH_PASSWORD;
    options.username = CLUSTER_USERNAME;
    options.password = CLUSTER_PASSWORD;

    kvClusterContext *cc = kvClusterConnectWithOptions(&options);
    ASSERT_MSG(cc && cc->err == 0, cc ? cc->errstr : "OOM");

    // Test connection
    kvReply *reply = kvClusterCommand(cc, "SET key1 Hello");
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    kvClusterFree(cc);
}

// Connect and handle two clusters simultaneously
void test_multicluster(void) {
    kvReply *reply;

    // Connect to first cluster
    kvClusterContext *cc1 = kvClusterConnect(CLUSTER_NODE);
    assert(cc1);
    ASSERT_MSG(cc1->err == 0, cc1->errstr);

    // Connect to second cluster
    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE_WITH_PASSWORD;
    options.password = CLUSTER_PASSWORD;
    kvClusterContext *cc2 = kvClusterConnectWithOptions(&options);
    assert(cc2);
    ASSERT_MSG(cc2->err == 0, cc2->errstr);

    // Set keys differently in clusters
    reply = kvClusterCommand(cc1, "SET key Hello1");
    CHECK_REPLY_OK(cc1, reply);
    freeReplyObject(reply);

    reply = kvClusterCommand(cc2, "SET key Hello2");
    CHECK_REPLY_OK(cc2, reply);
    freeReplyObject(reply);

    // Verify keys in clusters
    reply = kvClusterCommand(cc1, "GET key");
    CHECK_REPLY_STR(cc1, reply, "Hello1");
    freeReplyObject(reply);

    reply = kvClusterCommand(cc2, "GET key");
    CHECK_REPLY_STR(cc2, reply, "Hello2");
    freeReplyObject(reply);

    // Disconnect from first cluster
    kvClusterFree(cc1);

    // Verify that key is still accessible in connected cluster
    reply = kvClusterCommand(cc2, "GET key");
    CHECK_REPLY_STR(cc2, reply, "Hello2");
    freeReplyObject(reply);

    kvClusterFree(cc2);
}

/* Connect to a non-routable address which results in a connection timeout. */
void test_connect_timeout(void) {
    struct timeval timeout = {0, 200000};

    /* Configure a non-routable IP address and a timeout */
    kvClusterOptions options = {0};
    options.initial_nodes = "192.168.0.0:7000";
    options.connect_timeout = &timeout;
    options.connect_callback = connect_callback;

    kvClusterContext *cc = kvClusterConnectWithOptions(&options);
    assert(cc);
    assert(cc->err == KV_ERR_IO);
    assert(strcmp(cc->errstr, "Connection timed out") == 0);
    assert(connect_success_counter == 0);
    assert(connect_failure_counter == 1);
    reset_counters();

    kvClusterFree(cc);
}

/* Connect using a pre-configured command timeout */
void test_command_timeout(void) {
    struct timeval timeout = {0, 10000};

    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE;
    options.command_timeout = &timeout;

    kvClusterContext *cc = kvClusterConnectWithOptions(&options);
    ASSERT_MSG(cc && cc->err == 0, cc ? cc->errstr : "OOM");

    kvClusterNodeIterator ni;
    kvClusterInitNodeIterator(&ni, cc);
    kvClusterNode *node = kvClusterNodeNext(&ni);
    assert(node);

    /* Simulate a command timeout */
    kvReply *reply;
    reply = kvClusterCommandToNode(cc, node, "DEBUG SLEEP 0.2");
    assert(reply == NULL);
    assert(cc->err == KV_ERR_IO);

    /* Make sure debug sleep is done before leaving testcase */
    for (int i = 0; i < 20; ++i) {
        reply = kvClusterCommandToNode(cc, node, "SET key1 Hello");
        if (reply && reply->type == KV_REPLY_STATUS)
            break;
    }
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    kvClusterFree(cc);
}

/* Connect and configure a command timeout while connected. */
void test_command_timeout_set_while_connected(void) {
    kvClusterContext *cc = kvClusterConnect(CLUSTER_NODE);
    ASSERT_MSG(cc && cc->err == 0, cc ? cc->errstr : "OOM");

    kvClusterNodeIterator ni;
    kvClusterInitNodeIterator(&ni, cc);
    kvClusterNode *node = kvClusterNodeNext(&ni);
    assert(node);

    kvReply *reply;
    reply = kvClusterCommandToNode(cc, node, "DEBUG SLEEP 0.2");
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    /* Set command timeout while connected */
    struct timeval timeout = {0, 10000};
    kvClusterSetOptionTimeout(cc, timeout);

    reply = kvClusterCommandToNode(cc, node, "DEBUG SLEEP 0.2");
    assert(reply == NULL);
    assert(cc->err == KV_ERR_IO);

    /* Make sure debug sleep is done before leaving testcase */
    for (int i = 0; i < 20; ++i) {
        reply = kvClusterCommandToNode(cc, node, "SET key1 Hello");
        if (reply && reply->type == KV_REPLY_STATUS)
            break;
    }
    CHECK_REPLY_OK(cc, reply);
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
    bool noreply;
    const char *errstr;
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
    if (expect->noreply) {
        assert(reply == NULL);
        assert(strcmp(cc->errstr, expect->errstr) == 0);
    } else {
        assert(reply != NULL);
        assert(reply->type == expect->type);
        if (reply->type == KV_REPLY_ERROR ||
            reply->type == KV_REPLY_STATUS ||
            reply->type == KV_REPLY_STRING ||
            reply->type == KV_REPLY_DOUBLE ||
            reply->type == KV_REPLY_VERB) {
            assert(strcmp(reply->str, expect->str) == 0);
        }
    }
    if (expect->disconnect)
        kvClusterAsyncDisconnect(cc);
}

// Connecting to a password protected cluster using
// the async API, providing correct password.
void test_async_password_ok(void) {
    struct event_base *base = event_base_new();

    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE_WITH_PASSWORD;
    options.options = KV_OPT_BLOCKING_INITIAL_UPDATE;
    options.password = CLUSTER_PASSWORD;
    options.async_connect_callback = connectCallback;
    options.async_disconnect_callback = disconnectCallback;
    kvClusterOptionsUseLibevent(&options, base);

    kvClusterAsyncContext *acc = kvClusterAsyncConnectWithOptions(&options);
    ASSERT_MSG(acc && acc->err == 0, acc ? acc->errstr : "OOM");

    // Test connection
    ExpectedResult r = {
        .type = KV_REPLY_STATUS, .str = "OK", .disconnect = true};
    int ret = kvClusterAsyncCommand(acc, commandCallback, &r, "SET key1 Hello");
    assert(ret == KV_OK);

    event_base_dispatch(base);

    kvClusterAsyncFree(acc);
    event_base_free(base);
}

/* Connect to a password protected cluster using the wrong password.
   An eventloop is not attached since it is not needed is this case. */
void test_async_password_wrong(void) {
    struct event_base *base = event_base_new();

    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE_WITH_PASSWORD;
    options.options = KV_OPT_BLOCKING_INITIAL_UPDATE;
    options.password = "faultypass";
    kvClusterOptionsUseLibevent(&options, base);

    kvClusterAsyncContext *acc = kvClusterAsyncConnectWithOptions(&options);
    assert(acc);
    assert(acc->err == KV_ERR_OTHER);
    if (kv_version_less_than(6, 0))
        assert(strcmp(acc->errstr, "ERR invalid password") == 0);
    else
        assert(strncmp(acc->errstr, "WRONGPASS", 9) == 0);

    // No connection
    ExpectedResult r;
    int ret = kvClusterAsyncCommand(acc, commandCallback, &r, "SET key1 Hello");
    assert(ret == KV_ERR);
    assert(acc->err == KV_ERR_OTHER);
    assert(strcmp(acc->errstr, "slotmap not available") == 0);

    kvClusterAsyncFree(acc);
    event_base_free(base);
}

/* Connect to a password protected cluster without providing a password.
   An eventloop is not attached since it is not needed is this case. */
void test_async_password_missing(void) {
    struct event_base *base = event_base_new();

    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE_WITH_PASSWORD;
    options.options = KV_OPT_BLOCKING_INITIAL_UPDATE;
    options.async_connect_callback = connectCallback;
    options.async_disconnect_callback = disconnectCallback;
    kvClusterOptionsUseLibevent(&options, base);
    // Password not configured

    kvClusterAsyncContext *acc = kvClusterAsyncConnectWithOptions(&options);
    assert(acc);
    assert(acc->err == KV_ERR_OTHER);
    assert(strncmp(acc->errstr, "NOAUTH", 6) == 0);

    // No connection
    ExpectedResult r;
    int ret = kvClusterAsyncCommand(acc, commandCallback, &r, "SET key1 Hello");
    assert(ret == KV_ERR);
    assert(acc->err == KV_ERR_OTHER);
    assert(strcmp(acc->errstr, "slotmap not available") == 0);

    kvClusterAsyncFree(acc);
    event_base_free(base);
}

// Connect to a cluster and authenticate using username and password
void test_async_username_ok(void) {
    if (kv_version_less_than(6, 0))
        return;
    struct event_base *base = event_base_new();

    // Connect to the cluster using username and password
    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE_WITH_PASSWORD;
    options.options = KV_OPT_BLOCKING_INITIAL_UPDATE;
    options.async_connect_callback = connectCallback;
    options.async_disconnect_callback = disconnectCallback;
    options.username = "missing-user";
    options.password = CLUSTER_PASSWORD;
    kvClusterOptionsUseLibevent(&options, base);

    // Connect using wrong username should fail
    kvClusterAsyncContext *acc = kvClusterAsyncConnectWithOptions(&options);
    assert(acc);
    assert(acc->err == KV_ERR_OTHER);
    assert(strncmp(acc->errstr, "WRONGPASS invalid username-password pair",
                   40) == 0);
    kvClusterAsyncFree(acc);

    // Set correct username
    options.username = CLUSTER_USERNAME;

    // Connect using correct username should pass
    acc = kvClusterAsyncConnectWithOptions(&options);
    assert(acc);
    assert(acc->err == 0);

    // Test connection
    ExpectedResult r = {
        .type = KV_REPLY_STATUS, .str = "OK", .disconnect = true};
    int ret = kvClusterAsyncCommand(acc, commandCallback, &r, "SET key1 Hello");
    assert(ret == KV_OK);

    event_base_dispatch(base);

    kvClusterAsyncFree(acc);
    event_base_free(base);
}

// Connect and handle two clusters simultaneously using the async API
void test_async_multicluster(void) {
    struct event_base *base = event_base_new();

    kvClusterOptions options1 = {0};
    options1.initial_nodes = CLUSTER_NODE;
    options1.options = KV_OPT_BLOCKING_INITIAL_UPDATE;
    options1.async_connect_callback = connectCallback;
    options1.async_disconnect_callback = disconnectCallback;
    kvClusterOptionsUseLibevent(&options1, base);

    // Connect to first cluster
    kvClusterAsyncContext *acc1 = kvClusterAsyncConnectWithOptions(&options1);
    ASSERT_MSG(acc1 && acc1->err == 0, acc1 ? acc1->errstr : "OOM");

    kvClusterOptions options2 = {0};
    options2.initial_nodes = CLUSTER_NODE_WITH_PASSWORD;
    options2.options = KV_OPT_BLOCKING_INITIAL_UPDATE;
    options2.password = CLUSTER_PASSWORD;
    options2.async_connect_callback = connectCallback;
    options2.async_disconnect_callback = disconnectCallback;
    kvClusterOptionsUseLibevent(&options2, base);

    // Connect to second cluster
    kvClusterAsyncContext *acc2 = kvClusterAsyncConnectWithOptions(&options2);
    ASSERT_MSG(acc2 && acc2->err == 0, acc2 ? acc2->errstr : "OOM");

    // Set keys differently in clusters
    ExpectedResult r1 = {.type = KV_REPLY_STATUS, .str = "OK"};
    int ret = kvClusterAsyncCommand(acc1, commandCallback, &r1, "SET key A");
    assert(ret == KV_OK);

    ExpectedResult r2 = {.type = KV_REPLY_STATUS, .str = "OK"};
    ret = kvClusterAsyncCommand(acc2, commandCallback, &r2, "SET key B");
    assert(ret == KV_OK);

    // Verify key in first cluster
    ExpectedResult r3 = {.type = KV_REPLY_STRING, .str = "A"};
    ret = kvClusterAsyncCommand(acc1, commandCallback, &r3, "GET key");
    assert(ret == KV_OK);

    // Verify key in second cluster and disconnect
    ExpectedResult r4 = {
        .type = KV_REPLY_STRING, .str = "B", .disconnect = true};
    ret = kvClusterAsyncCommand(acc2, commandCallback, &r4, "GET key");
    assert(ret == KV_OK);

    // Verify that key is still accessible in connected cluster
    ExpectedResult r5 = {
        .type = KV_REPLY_STRING, .str = "A", .disconnect = true};
    ret = kvClusterAsyncCommand(acc1, commandCallback, &r5, "GET key");
    assert(ret == KV_OK);

    event_base_dispatch(base);

    kvClusterAsyncFree(acc1);
    kvClusterAsyncFree(acc2);
    event_base_free(base);
}

/* Connect to a non-routable address which results in a connection timeout. */
void test_async_connect_timeout(void) {
    struct event_base *base = event_base_new();
    struct timeval timeout = {0, 200000};

    kvClusterOptions options = {0};
    /* Configure a non-routable IP address and a timeout */
    options.initial_nodes = "192.168.0.0:7000";
    options.options = KV_OPT_BLOCKING_INITIAL_UPDATE;
    options.connect_timeout = &timeout;
    kvClusterOptionsUseLibevent(&options, base);

    kvClusterAsyncContext *acc = kvClusterAsyncConnectWithOptions(&options);
    assert(acc);
    assert(acc->err == KV_ERR_IO);
    assert(strcmp(acc->errstr, "Connection timed out") == 0);

    event_base_dispatch(base);

    kvClusterAsyncFree(acc);
    event_base_free(base);
}

/* Connect using a pre-configured command timeout */
void test_async_command_timeout(void) {
    struct event_base *base = event_base_new();
    struct timeval timeout = {0, 10000};

    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE;
    options.options = KV_OPT_BLOCKING_INITIAL_UPDATE;
    options.command_timeout = &timeout;
    kvClusterOptionsUseLibevent(&options, base);

    kvClusterAsyncContext *acc = kvClusterAsyncConnectWithOptions(&options);
    ASSERT_MSG(acc && acc->err == 0, acc ? acc->errstr : "OOM");

    kvClusterNodeIterator ni;
    kvClusterInitNodeIterator(&ni, &acc->cc);
    kvClusterNode *node = kvClusterNodeNext(&ni);
    assert(node);

    /* Simulate a command timeout and expect a timeout error */
    ExpectedResult r = {
        .noreply = true, .errstr = "Timeout", .disconnect = true};
    int status = kvClusterAsyncCommandToNode(acc, node, commandCallback, &r,
                                                 "DEBUG SLEEP 0.2");
    assert(status == KV_OK);

    event_base_dispatch(base);

    kvClusterAsyncFree(acc);
    event_base_free(base);
}

int main(void) {

    test_unsupported_option();

    test_password_ok();
    test_password_wrong();
    test_password_missing();
    test_username_ok();
    test_multicluster();
    test_connect_timeout();
    test_command_timeout();
    test_command_timeout_set_while_connected();

    test_async_password_ok();
    test_async_password_wrong();
    test_async_password_missing();
    test_async_username_ok();
    test_async_multicluster();
    test_async_connect_timeout();
    test_async_command_timeout();

    return 0;
}
