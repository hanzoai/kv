#define _XOPEN_SOURCE 600 /* For strdup() */
#include "cluster.h"
#include "test_utils.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLUSTER_NODE "127.0.0.1:7000"

void test_exists(kvClusterContext *cc) {
    kvReply *reply;
    reply = kvClusterCommand(cc, "SET {key}1 Hello");
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    reply = kvClusterCommand(cc, "EXISTS {key}1");
    CHECK_REPLY_INT(cc, reply, 1);
    freeReplyObject(reply);

    reply = kvClusterCommand(cc, "EXISTS nosuch{key}");
    CHECK_REPLY_INT(cc, reply, 0);
    freeReplyObject(reply);

    reply = kvClusterCommand(cc, "SET {key}2 World");
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    reply = kvClusterCommand(cc, "EXISTS {key}1 {key}2 nosuch{key}");
    CHECK_REPLY_INT(cc, reply, 2);
    freeReplyObject(reply);
}

void test_bitfield(kvClusterContext *cc) {
    kvReply *reply;

    reply = (kvReply *)kvClusterCommand(
        cc, "BITFIELD bkey1 SET u32 #0 255 GET u32 #0");
    CHECK_REPLY_ARRAY(cc, reply, 2);
    CHECK_REPLY_INT(cc, reply->element[1], 255);
    freeReplyObject(reply);
}

void test_bitfield_ro(kvClusterContext *cc) {
    if (kv_version_less_than(6, 0))
        return; /* Skip test, command not available. */

    kvReply *reply;

    reply = (kvReply *)kvClusterCommand(cc, "SET bkey2 a"); // 97
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    reply =
        (kvReply *)kvClusterCommand(cc, "BITFIELD_RO bkey2 GET u8 #0");
    CHECK_REPLY_ARRAY(cc, reply, 1);
    CHECK_REPLY_INT(cc, reply->element[0], 97);
    freeReplyObject(reply);
}

void test_mset(kvClusterContext *cc) {
    kvReply *reply;
    reply =
        kvClusterCommand(cc, "MSET {key}1 mset1 {key}2 mset2 {key}3 mset3");
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    reply = kvClusterCommand(cc, "GET {key}1");
    CHECK_REPLY_STR(cc, reply, "mset1");
    freeReplyObject(reply);

    reply = kvClusterCommand(cc, "GET {key}2");
    CHECK_REPLY_STR(cc, reply, "mset2");
    freeReplyObject(reply);

    reply = kvClusterCommand(cc, "GET {key}3");
    CHECK_REPLY_STR(cc, reply, "mset3");
    freeReplyObject(reply);
}

void test_mget(kvClusterContext *cc) {
    kvReply *reply;
    reply = kvClusterCommand(cc, "SET {key}1 mget1");
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    reply = kvClusterCommand(cc, "SET {key}2 mget2");
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    reply = kvClusterCommand(cc, "SET {key}3 mget3");
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    reply = kvClusterCommand(cc, "MGET {key}1 {key}2 {key}3");
    CHECK_REPLY_ARRAY(cc, reply, 3);
    CHECK_REPLY_STR(cc, reply->element[0], "mget1");
    CHECK_REPLY_STR(cc, reply->element[1], "mget2");
    CHECK_REPLY_STR(cc, reply->element[2], "mget3");
    freeReplyObject(reply);
}

void test_hset_hget_hdel_hexists(kvClusterContext *cc) {
    kvReply *reply;

    // Prepare
    reply = (kvReply *)kvClusterCommand(cc, "HDEL myhash field1");
    CHECK_REPLY(cc, reply);
    freeReplyObject(reply);
    reply = (kvReply *)kvClusterCommand(cc, "HDEL myhash field2");
    CHECK_REPLY(cc, reply);
    freeReplyObject(reply);

    // Set hash field
    reply =
        (kvReply *)kvClusterCommand(cc, "HSET myhash field1 hsetvalue");
    CHECK_REPLY_INT(cc, reply, 1); // Set 1 field
    freeReplyObject(reply);

    // Set second hash field
    reply = (kvReply *)kvClusterCommand(
        cc, "HSET myhash field3 hsetvalue3");
    CHECK_REPLY_INT(cc, reply, 1); // Set 1 field
    freeReplyObject(reply);

    // Get field value
    reply = (kvReply *)kvClusterCommand(cc, "HGET myhash field1");
    CHECK_REPLY_STR(cc, reply, "hsetvalue");
    freeReplyObject(reply);

    // Get field that is not present
    reply = (kvReply *)kvClusterCommand(cc, "HGET myhash field2");
    CHECK_REPLY_NIL(cc, reply);
    freeReplyObject(reply);

    // Delete a field
    reply = (kvReply *)kvClusterCommand(cc, "HDEL myhash field1");
    CHECK_REPLY_INT(cc, reply, 1); // Delete 1 field
    freeReplyObject(reply);

    // Delete a field that is not present
    reply = (kvReply *)kvClusterCommand(cc, "HDEL myhash field2");
    CHECK_REPLY_INT(cc, reply, 0); // Nothing to delete
    freeReplyObject(reply);

    // Check if field exists
    reply = (kvReply *)kvClusterCommand(cc, "HEXISTS myhash field3");
    CHECK_REPLY_INT(cc, reply, 1); // exists
    freeReplyObject(reply);

    // Delete multiple fields at once
    reply = (kvReply *)kvClusterCommand(
        cc, "HDEL myhash field1 field2 field3");
    CHECK_REPLY_INT(cc, reply, 1); // field3 deleted
    freeReplyObject(reply);

    // Make sure field3 is deleted now
    reply = (kvReply *)kvClusterCommand(cc, "HEXISTS myhash field3");
    CHECK_REPLY_INT(cc, reply, 0); // no field
    freeReplyObject(reply);

    // Set multiple fields at once
    reply = (kvReply *)kvClusterCommand(
        cc, "HSET myhash field1 hsetvalue1 field2 hsetvalue2");
    CHECK_REPLY_INT(cc, reply, 2);
    freeReplyObject(reply);
}

// Command layout:
// eval <script> <no of keys> <keys..> <args..>
void test_eval(kvClusterContext *cc) {
    kvReply *reply;

    // Single key
    reply = (kvReply *)kvClusterCommand(
        cc, "eval %s 1 %s", "return redis.call('set',KEYS[1],'bar')", "foo");
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    // Single key, string response
    reply = (kvReply *)kvClusterCommand(cc, "eval %s 1 %s",
                                                "return KEYS[1]", "key1");
    CHECK_REPLY_STR(cc, reply, "key1");
    freeReplyObject(reply);

    // Single key with single argument
    reply = (kvReply *)kvClusterCommand(
        cc, "eval %s 1 %s %s", "return {KEYS[1],ARGV[1]}", "key1", "first");
    CHECK_REPLY_ARRAY(cc, reply, 2);
    CHECK_REPLY_STR(cc, reply->element[0], "key1");
    CHECK_REPLY_STR(cc, reply->element[1], "first");
    freeReplyObject(reply);

    // Multi key, but handled by same instance
    reply = (kvReply *)kvClusterCommand(
        cc, "eval %s 2 %s %s", "return {KEYS[1],KEYS[2]}", "key1", "key1");
    CHECK_REPLY_ARRAY(cc, reply, 2);
    CHECK_REPLY_STR(cc, reply->element[0], "key1");
    CHECK_REPLY_STR(cc, reply->element[1], "key1");
    freeReplyObject(reply);

    // Error handling in EVAL

    // Request without key, fails due to no key given.
    reply = (kvReply *)kvClusterCommand(
        cc, "eval %s 0", "return kv.call('set','foo','bar')");
    assert(reply == NULL);

    // Prepare a key to be a list-type, then run a script the attempts
    // to access it as a simple type.
    reply = (kvReply *)kvClusterCommand(cc, "del foo");
    CHECK_REPLY_INT(cc, reply, 1);
    freeReplyObject(reply);

    reply = (kvReply *)kvClusterCommand(cc, "lpush foo a");
    CHECK_REPLY_INT(cc, reply, 1);
    freeReplyObject(reply);

    reply = (kvReply *)kvClusterCommand(
        cc, "eval %s 1 %s", "return redis.call('get',KEYS[1])", "foo");
    if (kv_version_less_than(7, 0)) {
        CHECK_REPLY_ERROR(cc, reply, "ERR Error running script");
    } else {
        CHECK_REPLY_ERROR(cc, reply, "WRONGTYPE");
    }
    freeReplyObject(reply);

    // Two keys handled by different instances,
    // will fail due to CROSSSLOT.
    reply = (kvReply *)kvClusterCommand(
        cc, "eval %s 2 %s %s %s %s", "return {KEYS[1],KEYS[2],ARGV[1],ARGV[2]}",
        "key1", "key2", "first", "second");
    CHECK_REPLY_ERROR(cc, reply, "CROSSSLOT");
    freeReplyObject(reply);
}

void test_xack(kvClusterContext *cc) {
    kvReply *r;

    /* Prepare a stream and group */
    r = kvClusterCommand(cc, "XADD mystream * field1 value1");
    CHECK_REPLY_TYPE(r, KV_REPLY_STRING);
    freeReplyObject(r);
    r = kvClusterCommand(cc, "XGROUP DESTROY mystream mygroup");
    CHECK_REPLY_TYPE(r, KV_REPLY_INTEGER);
    freeReplyObject(r);
    r = kvClusterCommand(cc, "XGROUP CREATE mystream mygroup 0");
    CHECK_REPLY_OK(cc, r);
    freeReplyObject(r);

    r = kvClusterCommand(cc, "XACK mystream mygroup 1526569495631-0");
    CHECK_REPLY_TYPE(r, KV_REPLY_INTEGER);
    freeReplyObject(r);
}

void test_xadd(kvClusterContext *cc) {
    kvReply *r;

    r = kvClusterCommand(cc, "XADD mystream * field1 value1");
    CHECK_REPLY_TYPE(r, KV_REPLY_STRING);
    freeReplyObject(r);

    r = kvClusterCommand(cc, "XADD mystream * field1 value1 field2 value2");
    CHECK_REPLY_TYPE(r, KV_REPLY_STRING);
    freeReplyObject(r);

    r = kvClusterCommand(
        cc, "XADD mystream * field1 value1 field2 value2 field3 value3");
    CHECK_REPLY_TYPE(r, KV_REPLY_STRING);
    freeReplyObject(r);
}

void test_xautoclaim(kvClusterContext *cc) {
    if (kv_version_less_than(6, 2))
        return; /* Skip test, command not available. */

    kvReply *r;

    r = kvClusterCommand(
        cc, "XAUTOCLAIM mystream mygroup Alice 3600000 0-0 COUNT 25");
    CHECK_REPLY_TYPE(r, KV_REPLY_ARRAY);
    freeReplyObject(r);
}

void test_xclaim(kvClusterContext *cc) {
    kvReply *r;

    r = kvClusterCommand(
        cc, "XCLAIM mystream mygroup Alice 3600000 1526569498055-0");
    CHECK_REPLY_TYPE(r, KV_REPLY_ARRAY);
    freeReplyObject(r);
}

void test_xdel(kvClusterContext *cc) {
    kvReply *r;
    char *id;

    r = kvClusterCommand(cc, "XADD mystream * field value");
    CHECK_REPLY_TYPE(r, KV_REPLY_STRING);
    id = strdup(r->str); /* Keep the id */
    freeReplyObject(r);

    r = kvClusterCommand(cc, "XDEL mystream %s", id);
    CHECK_REPLY_TYPE(r, KV_REPLY_INTEGER);
    freeReplyObject(r);

    /* Verify client handling of multiple id values / arguments */
    r = kvClusterCommand(cc, "XDEL mystream %s %s", id, id);
    CHECK_REPLY_TYPE(r, KV_REPLY_INTEGER);
    freeReplyObject(r);

    free(id);
}

void test_xgroup(kvClusterContext *cc) {
    kvReply *r;

    /* Test of missing subcommand */
    r = kvClusterCommand(cc, "XGROUP");
    assert(r == NULL);

    /* Test of missing stream/key */
    r = kvClusterCommand(cc, "XGROUP CREATE");
    assert(r == NULL);

    /* Test the destroy command first as preparation */
    r = kvClusterCommand(cc, "XGROUP DESTROY mystream consumer-group-name");
    CHECK_REPLY_TYPE(r, KV_REPLY_INTEGER);
    freeReplyObject(r);

    r = kvClusterCommand(cc,
                             "XGROUP CREATE mystream consumer-group-name 0");
    CHECK_REPLY_OK(cc, r);
    freeReplyObject(r);

    /* Attempting to create an already existing group gives error */
    r = kvClusterCommand(cc,
                             "XGROUP CREATE mystream consumer-group-name 0");
    CHECK_REPLY_ERROR(cc, r, "BUSYGROUP");
    freeReplyObject(r);

    if (!kv_version_less_than(6, 2)) {
        /* Test of subcommand CREATECONSUMER when available. */
        r = kvClusterCommand(
            cc,
            "XGROUP CREATECONSUMER mystream consumer-group-name myconsumer123");
        CHECK_REPLY_INT(cc, r, 1);
        freeReplyObject(r);
    }

    r = kvClusterCommand(
        cc, "XGROUP DELCONSUMER mystream consumer-group-name myconsumer123");
    CHECK_REPLY_TYPE(r, KV_REPLY_INTEGER);
    freeReplyObject(r);

    r = kvClusterCommand(cc, "XGROUP SETID mystream consumer-group-name 0");
    CHECK_REPLY_OK(cc, r);
    freeReplyObject(r);
}

void test_xinfo(kvClusterContext *cc) {
    kvReply *r;

    /* Test of missing subcommand */
    r = kvClusterCommand(cc, "XINFO");
    assert(r == NULL);

    /* Test of missing stream/key */
    r = kvClusterCommand(cc, "XINFO STREAM");
    assert(r == NULL);

    r = kvClusterCommand(cc, "XINFO STREAM mystream");
    CHECK_REPLY_TYPE(r, KV_REPLY_ARRAY);
    freeReplyObject(r);

    if (!kv_version_less_than(6, 0)) {
        /* Test of subcommand STREAM with arguments when available. */
        r = kvClusterCommand(cc, "XINFO STREAM mystream FULL COUNT 1");
        CHECK_REPLY_TYPE(r, KV_REPLY_ARRAY);
        freeReplyObject(r);
    }

    r = kvClusterCommand(cc, "XINFO GROUPS mystream");
    CHECK_REPLY_TYPE(r, KV_REPLY_ARRAY);
    freeReplyObject(r);

    r = kvClusterCommand(cc,
                             "XINFO CONSUMERS mystream consumer-group-name");
    CHECK_REPLY_TYPE(r, KV_REPLY_ARRAY);
    freeReplyObject(r);
}

void test_xlen(kvClusterContext *cc) {
    kvReply *r;

    r = (kvReply *)kvClusterCommand(cc, "XLEN mystream");
    CHECK_REPLY_TYPE(r, KV_REPLY_INTEGER);
    freeReplyObject(r);
}

void test_xpending(kvClusterContext *cc) {
    kvReply *r;

    r = kvClusterCommand(cc, "XPENDING mystream mygroup");
    CHECK_REPLY_TYPE(r, KV_REPLY_ARRAY);
    freeReplyObject(r);

    r = kvClusterCommand(cc, "XPENDING mystream mygroup - + 10");
    CHECK_REPLY_TYPE(r, KV_REPLY_ARRAY);
    freeReplyObject(r);
}

void test_xrange(kvClusterContext *cc) {
    kvReply *r;

    r = kvClusterCommand(cc, "XRANGE mystream 0 0");
    CHECK_REPLY_ARRAY(cc, r, 0); /* No entries due to 0-range */
    freeReplyObject(r);

    r = kvClusterCommand(cc, "XRANGE mystream - + COUNT 1");
    CHECK_REPLY_ARRAY(cc, r, 1); /* Single entry due to count argument */
    freeReplyObject(r);
}

void test_xrevrange(kvClusterContext *cc) {
    kvReply *r;

    r = kvClusterCommand(cc, "XREVRANGE mystream 0 0");
    CHECK_REPLY_ARRAY(cc, r, 0); /* No entries due to 0-range */
    freeReplyObject(r);

    r = kvClusterCommand(cc, "XREVRANGE mystream + - COUNT 1");
    CHECK_REPLY_ARRAY(cc, r, 1); /* Single entry due to count argument */
    freeReplyObject(r);
}

void test_xtrim(kvClusterContext *cc) {
    kvReply *r;

    r = kvClusterCommand(cc, "XTRIM mystream MAXLEN 200");
    CHECK_REPLY_TYPE(r, KV_REPLY_INTEGER);
    freeReplyObject(r);

    r = kvClusterCommand(cc, "XTRIM mystream MAXLEN ~ 100");
    CHECK_REPLY_TYPE(r, KV_REPLY_INTEGER);
    freeReplyObject(r);
}

void test_multi(kvClusterContext *cc) {

    /* Since the slot lookup is currently not handled for this command
     * the regular API is expected to fail. This command requires the
     * ..ToNode() APIs for sending a command to a specific node.
     * See ct_specific_nodes.c for these tests.
     */

    kvReply *r = kvClusterCommand(cc, "MULTI");
    assert(r == NULL);
}

int main(void) {
    struct timeval timeout = {0, 500000};

    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE;
    options.connect_timeout = &timeout;

    kvClusterContext *cc = kvClusterConnectWithOptions(&options);
    ASSERT_MSG(cc && cc->err == 0, cc ? cc->errstr : "OOM");
    load_kv_version(cc);

    test_bitfield(cc);
    test_bitfield_ro(cc);
    test_eval(cc);
    test_exists(cc);
    test_hset_hget_hdel_hexists(cc);
    test_mget(cc);
    test_mset(cc);
    test_multi(cc);
    test_xack(cc);
    test_xadd(cc);
    test_xautoclaim(cc);
    test_xclaim(cc);
    test_xdel(cc);
    test_xgroup(cc);
    test_xinfo(cc);
    test_xlen(cc);
    test_xpending(cc);
    test_xrange(cc);
    test_xrevrange(cc);
    test_xtrim(cc);

    kvClusterFree(cc);
    return 0;
}
