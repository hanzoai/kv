#define _XOPEN_SOURCE 600 /* For strdup() */
#include "adapters/libevent.h"
#include "cluster.h"
#include "test_utils.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLUSTER_NODE "127.0.0.1:7000"

void test_command_to_single_node(kvClusterContext *cc) {
    kvReply *reply;

    kvClusterNodeIterator ni;
    kvClusterInitNodeIterator(&ni, cc);
    kvClusterNode *node = kvClusterNodeNext(&ni);
    assert(node);

    reply = kvClusterCommandToNode(cc, node, "DBSIZE");
    CHECK_REPLY(cc, reply);
    CHECK_REPLY_TYPE(reply, KV_REPLY_INTEGER);
    freeReplyObject(reply);
}

void test_command_to_all_nodes(kvClusterContext *cc) {

    kvClusterNodeIterator ni;
    kvClusterInitNodeIterator(&ni, cc);

    kvClusterNode *node;
    while ((node = kvClusterNodeNext(&ni)) != NULL) {

        kvReply *reply;
        reply = kvClusterCommandToNode(cc, node, "DBSIZE");
        CHECK_REPLY(cc, reply);
        CHECK_REPLY_TYPE(reply, KV_REPLY_INTEGER);
        freeReplyObject(reply);
    }
}

void test_transaction(kvClusterContext *cc) {

    kvClusterNode *node = kvClusterGetNodeByKey(cc, (char *)"foo");
    assert(node);

    kvReply *reply;
    reply = kvClusterCommandToNode(cc, node, "MULTI");
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    reply = kvClusterCommandToNode(cc, node, "SET foo 99");
    CHECK_REPLY_QUEUED(cc, reply);
    freeReplyObject(reply);

    reply = kvClusterCommandToNode(cc, node, "INCR foo");
    CHECK_REPLY_QUEUED(cc, reply);
    freeReplyObject(reply);

    reply = kvClusterCommandToNode(cc, node, "EXEC");
    CHECK_REPLY_ARRAY(cc, reply, 2);
    CHECK_REPLY_OK(cc, reply->element[0]);
    CHECK_REPLY_INT(cc, reply->element[1], 100);
    freeReplyObject(reply);
}

void test_streams(kvClusterContext *cc) {
    kvReply *reply;
    char *id;

    /* Get the node that handles given stream */
    kvClusterNode *node = kvClusterGetNodeByKey(cc, (char *)"mystream");
    assert(node);

    /* Preparation: remove old stream/key */
    reply = kvClusterCommandToNode(cc, node, "DEL mystream");
    CHECK_REPLY_TYPE(reply, KV_REPLY_INTEGER);
    freeReplyObject(reply);

    /* Query wrong node */
    kvClusterNode *wrongNode = kvClusterGetNodeByKey(cc, (char *)"otherstream");
    assert(node != wrongNode);
    reply = kvClusterCommandToNode(cc, wrongNode, "XLEN mystream");
    CHECK_REPLY_ERROR(cc, reply, "MOVED");
    freeReplyObject(reply);

    /* Verify stream length before adding entries */
    reply = kvClusterCommandToNode(cc, node, "XLEN mystream");
    CHECK_REPLY_INT(cc, reply, 0);
    freeReplyObject(reply);

    /* Add entries to a created stream */
    reply = kvClusterCommandToNode(cc, node, "XADD mystream * name t800");
    CHECK_REPLY_TYPE(reply, KV_REPLY_STRING);
    freeReplyObject(reply);

    reply = kvClusterCommandToNode(
        cc, node, "XADD mystream * name Sara surname OConnor");
    CHECK_REPLY_TYPE(reply, KV_REPLY_STRING);
    id = strdup(reply->str); /* Keep this id for later inspections */
    freeReplyObject(reply);

    /* Verify stream length after adding entries */
    reply = kvClusterCommandToNode(cc, node, "XLEN mystream");
    CHECK_REPLY_INT(cc, reply, 2);
    freeReplyObject(reply);

    /* Modify the stream */
    reply = kvClusterCommandToNode(cc, node, "XTRIM mystream MAXLEN 1");
    CHECK_REPLY_INT(cc, reply, 1);
    freeReplyObject(reply);

    /* Verify stream length after modifying the stream */
    reply = kvClusterCommandToNode(cc, node, "XLEN mystream");
    CHECK_REPLY_INT(cc, reply, 1); /* 1 entry left */
    freeReplyObject(reply);

    /* Read from the stream */
    reply = kvClusterCommandToNode(cc, node,
                                       "XREAD COUNT 2 STREAMS mystream 0");
    CHECK_REPLY_ARRAY(cc, reply, 1); /* Reply from a single stream */

    /* Verify the reply from stream */
    CHECK_REPLY_ARRAY(cc, reply->element[0], 2);
    CHECK_REPLY_STR(cc, reply->element[0]->element[0], "mystream");
    CHECK_REPLY_ARRAY(cc, reply->element[0]->element[1], 1); /* single entry */

    /* Verify the entry, an array of id and field+value elements */
    kvReply *entry = reply->element[0]->element[1]->element[0];
    CHECK_REPLY_ARRAY(cc, entry, 2);
    CHECK_REPLY_STR(cc, entry->element[0], id);

    CHECK_REPLY_ARRAY(cc, entry->element[1], 4);
    CHECK_REPLY_STR(cc, entry->element[1]->element[0], "name");
    CHECK_REPLY_STR(cc, entry->element[1]->element[1], "Sara");
    CHECK_REPLY_STR(cc, entry->element[1]->element[2], "surname");
    CHECK_REPLY_STR(cc, entry->element[1]->element[3], "OConnor");
    freeReplyObject(reply);

    /* Delete the entry in stream */
    reply = kvClusterCommandToNode(cc, node, "XDEL mystream %s", id);
    CHECK_REPLY_INT(cc, reply, 1);
    freeReplyObject(reply);

    /* Blocking read of stream */
    reply = kvClusterCommandToNode(
        cc, node, "XREAD COUNT 2 BLOCK 200 STREAMS mystream 0");
    CHECK_REPLY_NIL(cc, reply);
    freeReplyObject(reply);

    /* Create a consumer group */
    reply = kvClusterCommandToNode(cc, node,
                                       "XGROUP CREATE mystream mygroup1 0");
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    if (!kv_version_less_than(6, 2)) {
        /* Create a consumer */
        reply = kvClusterCommandToNode(
            cc, node, "XGROUP CREATECONSUMER mystream mygroup1 myconsumer123");
        CHECK_REPLY_INT(cc, reply, 1);
        freeReplyObject(reply);
    }

    /* Blocking read of consumer group */
    reply =
        kvClusterCommandToNode(cc, node,
                                   "XREADGROUP GROUP mygroup1 myconsumer123 "
                                   "COUNT 2 BLOCK 200 STREAMS mystream 0");
    CHECK_REPLY_TYPE(reply, KV_REPLY_ARRAY);
    freeReplyObject(reply);

    free(id);
}

void test_pipeline_to_single_node(kvClusterContext *cc) {
    int status;
    kvReply *reply;

    kvClusterNodeIterator ni;
    kvClusterInitNodeIterator(&ni, cc);
    kvClusterNode *node = kvClusterNodeNext(&ni);
    assert(node);

    status = kvClusterAppendCommandToNode(cc, node, "DBSIZE");
    ASSERT_MSG(status == KV_OK, cc->errstr);

    // Trigger send of pipeline commands
    kvClusterGetReply(cc, (void *)&reply);
    CHECK_REPLY(cc, reply);
    CHECK_REPLY_TYPE(reply, KV_REPLY_INTEGER);
    freeReplyObject(reply);
}

void test_pipeline_to_all_nodes(kvClusterContext *cc) {

    kvClusterNodeIterator ni;
    kvClusterInitNodeIterator(&ni, cc);

    kvClusterNode *node;
    while ((node = kvClusterNodeNext(&ni)) != NULL) {
        int status = kvClusterAppendCommandToNode(cc, node, "DBSIZE");
        ASSERT_MSG(status == KV_OK, cc->errstr);
    }

    // Get replies from 3 node cluster
    kvReply *reply;
    kvClusterGetReply(cc, (void *)&reply);
    CHECK_REPLY(cc, reply);
    CHECK_REPLY_TYPE(reply, KV_REPLY_INTEGER);
    freeReplyObject(reply);

    kvClusterGetReply(cc, (void *)&reply);
    CHECK_REPLY(cc, reply);
    CHECK_REPLY_TYPE(reply, KV_REPLY_INTEGER);
    freeReplyObject(reply);

    kvClusterGetReply(cc, (void *)&reply);
    CHECK_REPLY(cc, reply);
    CHECK_REPLY_TYPE(reply, KV_REPLY_INTEGER);
    freeReplyObject(reply);

    kvClusterGetReply(cc, (void *)&reply);
    assert(reply == NULL);
}

void test_pipeline_transaction(kvClusterContext *cc) {
    int status;
    kvReply *reply;

    kvClusterNode *node = kvClusterGetNodeByKey(cc, (char *)"foo");
    assert(node);

    status = kvClusterAppendCommandToNode(cc, node, "MULTI");
    ASSERT_MSG(status == KV_OK, cc->errstr);
    status = kvClusterAppendCommandToNode(cc, node, "SET foo 199");
    ASSERT_MSG(status == KV_OK, cc->errstr);
    status = kvClusterAppendCommandToNode(cc, node, "INCR foo");
    ASSERT_MSG(status == KV_OK, cc->errstr);
    status = kvClusterAppendCommandToNode(cc, node, "EXEC");
    ASSERT_MSG(status == KV_OK, cc->errstr);

    // Trigger send of pipeline commands
    {
        kvClusterGetReply(cc, (void *)&reply); // MULTI
        CHECK_REPLY_OK(cc, reply);
        freeReplyObject(reply);

        kvClusterGetReply(cc, (void *)&reply); // SET
        CHECK_REPLY_QUEUED(cc, reply);
        freeReplyObject(reply);

        kvClusterGetReply(cc, (void *)&reply); // INCR
        CHECK_REPLY_QUEUED(cc, reply);
        freeReplyObject(reply);

        kvClusterGetReply(cc, (void *)&reply); // EXEC
        CHECK_REPLY_ARRAY(cc, reply, 2);
        CHECK_REPLY_OK(cc, reply->element[0]);
        CHECK_REPLY_INT(cc, reply->element[1], 200);
        freeReplyObject(reply);
    }
}

//------------------------------------------------------------------------------
// Async API
//------------------------------------------------------------------------------
typedef struct ExpectedResult {
    int type;
    const char *str;
    size_t elements;
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
        switch (reply->type) {
        case KV_REPLY_ARRAY:
            assert(reply->elements == expect->elements);
            assert(reply->str == NULL);
            break;
        case KV_REPLY_INTEGER:
            assert(reply->elements == 0);
            assert(reply->str == NULL);
            break;
        case KV_REPLY_STATUS:
            assert(strcmp(reply->str, expect->str) == 0);
            assert(reply->elements == 0);
            break;
        default:
            assert(0);
        }
    }
    if (expect->disconnect)
        kvClusterAsyncDisconnect(cc);
}

void test_async_to_single_node(void) {
    struct event_base *base = event_base_new();

    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE;
    options.options = KV_OPT_BLOCKING_INITIAL_UPDATE;
    options.max_retry = 1;
    options.async_connect_callback = connectCallback;
    options.async_disconnect_callback = disconnectCallback;
    kvClusterOptionsUseLibevent(&options, base);

    kvClusterAsyncContext *acc = kvClusterAsyncConnectWithOptions(&options);
    ASSERT_MSG(acc && acc->err == 0, acc ? acc->errstr : "OOM");

    kvClusterNodeIterator ni;
    kvClusterInitNodeIterator(&ni, &acc->cc);
    kvClusterNode *node = kvClusterNodeNext(&ni);
    assert(node);

    int status;
    ExpectedResult r1 = {.type = KV_REPLY_INTEGER, .disconnect = true};
    status = kvClusterAsyncCommandToNode(acc, node, commandCallback, &r1,
                                             "DBSIZE");
    ASSERT_MSG(status == KV_OK, acc->errstr);

    event_base_dispatch(base);

    kvClusterAsyncFree(acc);
    event_base_free(base);
}

void test_async_formatted_to_single_node(void) {
    struct event_base *base = event_base_new();

    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE;
    options.options = KV_OPT_BLOCKING_INITIAL_UPDATE;
    options.max_retry = 1;
    options.async_connect_callback = connectCallback;
    options.async_disconnect_callback = disconnectCallback;
    kvClusterOptionsUseLibevent(&options, base);

    kvClusterAsyncContext *acc = kvClusterAsyncConnectWithOptions(&options);
    ASSERT_MSG(acc && acc->err == 0, acc ? acc->errstr : "OOM");

    kvClusterNodeIterator ni;
    kvClusterInitNodeIterator(&ni, &acc->cc);
    kvClusterNode *node = kvClusterNodeNext(&ni);
    assert(node);

    int status;
    ExpectedResult r1 = {.type = KV_REPLY_INTEGER, .disconnect = true};
    char command[] = "*1\r\n$6\r\nDBSIZE\r\n";
    status = kvClusterAsyncFormattedCommandToNode(
        acc, node, commandCallback, &r1, command, strlen(command));
    ASSERT_MSG(status == KV_OK, acc->errstr);

    event_base_dispatch(base);

    kvClusterAsyncFree(acc);
    event_base_free(base);
}

void test_async_command_argv_to_single_node(void) {
    struct event_base *base = event_base_new();

    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE;
    options.options = KV_OPT_BLOCKING_INITIAL_UPDATE;
    options.max_retry = 1;
    options.async_connect_callback = connectCallback;
    options.async_disconnect_callback = disconnectCallback;
    kvClusterOptionsUseLibevent(&options, base);

    kvClusterAsyncContext *acc = kvClusterAsyncConnectWithOptions(&options);
    ASSERT_MSG(acc && acc->err == 0, acc ? acc->errstr : "OOM");

    kvClusterNodeIterator ni;
    kvClusterInitNodeIterator(&ni, &acc->cc);
    kvClusterNode *node = kvClusterNodeNext(&ni);
    assert(node);

    int status;
    ExpectedResult r1 = {.type = KV_REPLY_INTEGER, .disconnect = true};
    status = kvClusterAsyncCommandArgvToNode(
        acc, node, commandCallback, &r1, 1, (const char *[]){"DBSIZE"},
        (size_t[]){6});
    ASSERT_MSG(status == KV_OK, acc->errstr);

    event_base_dispatch(base);

    kvClusterAsyncFree(acc);
    event_base_free(base);
}

void test_async_to_all_nodes(void) {
    struct event_base *base = event_base_new();

    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE;
    options.options = KV_OPT_BLOCKING_INITIAL_UPDATE;
    options.max_retry = 1;
    options.async_connect_callback = connectCallback;
    options.async_disconnect_callback = disconnectCallback;
    kvClusterOptionsUseLibevent(&options, base);

    kvClusterAsyncContext *acc = kvClusterAsyncConnectWithOptions(&options);
    ASSERT_MSG(acc && acc->err == 0, acc ? acc->errstr : "OOM");

    kvClusterNodeIterator ni;
    kvClusterInitNodeIterator(&ni, &acc->cc);

    int status;
    ExpectedResult r1 = {.type = KV_REPLY_INTEGER};

    kvClusterNode *node;
    while ((node = kvClusterNodeNext(&ni)) != NULL) {

        status = kvClusterAsyncCommandToNode(acc, node, commandCallback,
                                                 &r1, "DBSIZE");
        ASSERT_MSG(status == KV_OK, acc->errstr);
    }

    // Normal command to trigger disconnect
    ExpectedResult r2 = {
        .type = KV_REPLY_STATUS, .str = "OK", .disconnect = true};
    status =
        kvClusterAsyncCommand(acc, commandCallback, &r2, "SET foo bar");

    event_base_dispatch(base);

    kvClusterAsyncFree(acc);
    event_base_free(base);
}

void test_async_transaction(void) {
    struct event_base *base = event_base_new();

    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE;
    options.options = KV_OPT_BLOCKING_INITIAL_UPDATE;
    options.max_retry = 1;
    options.async_connect_callback = connectCallback;
    options.async_disconnect_callback = disconnectCallback;
    kvClusterOptionsUseLibevent(&options, base);

    kvClusterAsyncContext *acc = kvClusterAsyncConnectWithOptions(&options);
    ASSERT_MSG(acc && acc->err == 0, acc ? acc->errstr : "OOM");

    kvClusterNode *node = kvClusterGetNodeByKey(&acc->cc, (char *)"foo");
    assert(node);

    int status;
    ExpectedResult r1 = {.type = KV_REPLY_STATUS, .str = "OK"};
    status = kvClusterAsyncCommandToNode(acc, node, commandCallback, &r1,
                                             "MULTI");
    ASSERT_MSG(status == KV_OK, acc->errstr);

    ExpectedResult r2 = {.type = KV_REPLY_STATUS, .str = "QUEUED"};
    status = kvClusterAsyncCommandToNode(acc, node, commandCallback, &r2,
                                             "SET foo 99");
    ASSERT_MSG(status == KV_OK, acc->errstr);

    ExpectedResult r3 = {.type = KV_REPLY_STATUS, .str = "QUEUED"};
    status = kvClusterAsyncCommandToNode(acc, node, commandCallback, &r3,
                                             "INCR foo");
    ASSERT_MSG(status == KV_OK, acc->errstr);

    /* The EXEC command will return an array with result from 2 queued commands. */
    ExpectedResult r4 = {
        .type = KV_REPLY_ARRAY, .elements = 2, .disconnect = true};
    status = kvClusterAsyncCommandToNode(acc, node, commandCallback, &r4,
                                             "EXEC ");
    ASSERT_MSG(status == KV_OK, acc->errstr);

    event_base_dispatch(base);

    kvClusterAsyncFree(acc);
    event_base_free(base);
}

int main(void) {
    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE;
    options.max_retry = 1;

    kvClusterContext *cc = kvClusterConnectWithOptions(&options);
    ASSERT_MSG(cc && cc->err == 0, cc ? cc->errstr : "OOM");
    load_kv_version(cc);

    // Synchronous API
    test_command_to_single_node(cc);
    test_command_to_all_nodes(cc);
    test_transaction(cc);
    test_streams(cc);

    // Pipeline API
    test_pipeline_to_single_node(cc);
    test_pipeline_to_all_nodes(cc);
    test_pipeline_transaction(cc);

    kvClusterFree(cc);

    // Asynchronous API
    test_async_to_single_node();
    test_async_formatted_to_single_node();
    test_async_command_argv_to_single_node();
    test_async_to_all_nodes();
    test_async_transaction();

    return 0;
}
