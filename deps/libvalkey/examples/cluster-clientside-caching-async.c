/*
 * Simple example how to enable client tracking to implement client side caching.
 * Tracking can be enabled via a registered connect callback and invalidation
 * messages are received via the registered push callback.
 * The disconnect callback should also be used as an indication of invalidation.
 */
#include <kv/cluster.h>

#include <kv/adapters/libevent.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLUSTER_NODE "127.0.0.1:7000"
#define KEY "key:1"

void pushCallback(kvAsyncContext *ac, void *r);
void setCallback(kvClusterAsyncContext *acc, void *r, void *privdata);
void getCallback1(kvClusterAsyncContext *acc, void *r, void *privdata);
void getCallback2(kvClusterAsyncContext *acc, void *r, void *privdata);
void modifyKey(const char *key, const char *value);

/* The connect callback enables RESP3 and client tracking,
 * and sets the push callback in the libkv context. */
void connectCallback(kvAsyncContext *ac, int status) {
    assert(status == KV_OK);
    kvAsyncSetPushCallback(ac, pushCallback);
    kvAsyncCommand(ac, NULL, NULL, "HELLO 3");
    kvAsyncCommand(ac, NULL, NULL, "CLIENT TRACKING ON");
    printf("Connected to %s:%d\n", ac->c.tcp.host, ac->c.tcp.port);
}

/* The event callback issues a 'SET' command when the client is ready to accept
   commands. A reply is expected via a call to 'setCallback()' */
void eventCallback(const kvClusterContext *cc, int event, void *privdata) {
    (void)cc;
    (void)privdata;
    /* Get the async context by a simple cast since in the Async API a
     * kvClusterAsyncContext is an extension of the kvClusterContext. */
    kvClusterAsyncContext *acc = (kvClusterAsyncContext *)cc;

    /* We send our commands when the client is ready to accept commands. */
    if (event == KVCLUSTER_EVENT_READY) {
        printf("Client is ready to accept commands\n");

        int status =
            kvClusterAsyncCommand(acc, setCallback, NULL, "SET %s 1", KEY);
        assert(status == KV_OK);
    }
}

/* Message callback for 'SET' commands. Issues a 'GET' command and a reply is
   expected as a call to 'getCallback1()' */
void setCallback(kvClusterAsyncContext *acc, void *r, void *privdata) {
    (void)privdata;
    kvReply *reply = (kvReply *)r;
    assert(reply != NULL);
    printf("Callback for 'SET', reply: %s\n", reply->str);

    int status =
        kvClusterAsyncCommand(acc, getCallback1, NULL, "GET %s", KEY);
    assert(status == KV_OK);
}

/* Message callback for the first 'GET' command. Modifies the key to
   trigger KV to send a key invalidation message and then sends another
   'GET' command. The invalidation message is received via the registered
   push callback. */
void getCallback1(kvClusterAsyncContext *acc, void *r, void *privdata) {
    (void)privdata;
    kvReply *reply = (kvReply *)r;
    assert(reply != NULL);

    printf("Callback for first 'GET', reply: %s\n", reply->str);

    /* Modify the key from another client which will invalidate a cached value.
       KV will send an invalidation message via a push message. */
    modifyKey(KEY, "99");

    int status =
        kvClusterAsyncCommand(acc, getCallback2, NULL, "GET %s", KEY);
    assert(status == KV_OK);
}

/* Push message callback handling invalidation messages. */
void pushCallback(kvAsyncContext *ac, void *r) {
    (void)ac;
    kvReply *reply = r;
    if (!(reply->type == KV_REPLY_PUSH && reply->elements == 2 &&
          reply->element[0]->type == KV_REPLY_STRING &&
          !strncmp(reply->element[0]->str, "invalidate", 10) &&
          reply->element[1]->type == KV_REPLY_ARRAY)) {
        /* Not an 'invalidate' message. Ignore. */
        return;
    }
    kvReply *payload = reply->element[1];
    size_t i;
    for (i = 0; i < payload->elements; i++) {
        kvReply *key = payload->element[i];
        if (key->type == KV_REPLY_STRING)
            printf("Invalidate key '%.*s'\n", (int)key->len, key->str);
        else if (key->type == KV_REPLY_NIL)
            printf("Invalidate all\n");
    }
}

/* Message callback for 'GET' commands. Exits program. */
void getCallback2(kvClusterAsyncContext *acc, void *r, void *privdata) {
    (void)privdata;
    kvReply *reply = (kvReply *)r;
    assert(reply != NULL);

    printf("Callback for second 'GET', reply: %s\n", reply->str);

    /* Exit the eventloop after a couple of sent commands. */
    kvClusterAsyncDisconnect(acc);
}

/* A disconnect callback should invalidate all cached keys. */
void disconnectCallback(const kvAsyncContext *ac, int status) {
    assert(status == KV_OK);
    printf("Disconnected from %s:%d\n", ac->c.tcp.host, ac->c.tcp.port);

    printf("Invalidate all\n");
}

/* Helper to modify keys using a separate client. */
void modifyKey(const char *key, const char *value) {
    printf("Modify key: '%s'\n", key);
    kvClusterContext *cc = kvClusterConnect(CLUSTER_NODE);
    assert(cc);

    kvReply *reply = kvClusterCommand(cc, "SET %s %s", key, value);
    assert(reply != NULL);
    freeReplyObject(reply);

    kvClusterFree(cc);
}

int main(void) {
    struct event_base *base = event_base_new();

    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE;
    options.async_connect_callback = connectCallback;
    options.async_disconnect_callback = disconnectCallback;
    options.event_callback = eventCallback;
    kvClusterOptionsUseLibevent(&options, base);

    kvClusterAsyncContext *acc = kvClusterAsyncConnectWithOptions(&options);
    if (acc == NULL || acc->err != 0) {
        printf("Connect error: %s\n", acc ? acc->errstr : "OOM");
        exit(2);
    }

    event_base_dispatch(base);

    kvClusterAsyncFree(acc);
    event_base_free(base);
    return 0;
}
