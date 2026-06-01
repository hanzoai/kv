#include <kv/cluster.h>
#include <kv/tls.h>

#include <kv/adapters/libevent.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define CLUSTER_NODE_TLS "127.0.0.1:7300"

void getCallback(kvClusterAsyncContext *cc, void *r, void *privdata) {
    kvReply *reply = (kvReply *)r;
    if (reply == NULL) {
        if (cc->err) {
            printf("errstr: %s\n", cc->errstr);
        }
        return;
    }
    printf("privdata: %s reply: %s\n", (char *)privdata, reply->str);

    /* Disconnect after receiving the reply to GET */
    kvClusterAsyncDisconnect(cc);
}

void setCallback(kvClusterAsyncContext *cc, void *r, void *privdata) {
    kvReply *reply = (kvReply *)r;
    if (reply == NULL) {
        if (cc->err) {
            printf("errstr: %s\n", cc->errstr);
        }
        return;
    }
    printf("privdata: %s reply: %s\n", (char *)privdata, reply->str);
}

void connectCallback(kvAsyncContext *ac, int status) {
    if (status != KV_OK) {
        printf("Error: %s\n", ac->errstr);
        return;
    }

    printf("Connected to %s:%d\n", ac->c.tcp.host, ac->c.tcp.port);
}

void disconnectCallback(const kvAsyncContext *ac, int status) {
    if (status != KV_OK) {
        printf("Error: %s\n", ac->errstr);
        return;
    }
    printf("Disconnected from %s:%d\n", ac->c.tcp.host, ac->c.tcp.port);
}

int main(void) {
    kvTLSContext *tls;
    kvTLSContextError tls_error;

    kvInitOpenSSL();
    tls = kvCreateTLSContext("ca.crt", NULL, "client.crt", "client.key",
                                 NULL, &tls_error);
    if (!tls) {
        printf("TLS Context error: %s\n", kvTLSContextGetError(tls_error));
        exit(1);
    }

    struct event_base *base = event_base_new();

    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE_TLS;
    options.async_connect_callback = connectCallback;
    options.async_disconnect_callback = disconnectCallback;
    options.tls = tls;
    options.tls_init_fn = &kvInitiateTLSWithContext;
    kvClusterOptionsUseLibevent(&options, base);

    kvClusterAsyncContext *acc = kvClusterAsyncConnectWithOptions(&options);
    if (acc == NULL || acc->err != 0) {
        printf("Error: %s\n", acc ? acc->errstr : "OOM");
        exit(-1);
    }

    int status;
    status = kvClusterAsyncCommand(acc, setCallback, (char *)"THE_ID",
                                       "SET %s %s", "key", "value");
    if (status != KV_OK) {
        printf("error: err=%d errstr=%s\n", acc->err, acc->errstr);
    }

    status = kvClusterAsyncCommand(acc, getCallback, (char *)"THE_ID",
                                       "GET %s", "key");
    if (status != KV_OK) {
        printf("error: err=%d errstr=%s\n", acc->err, acc->errstr);
    }

    printf("Dispatch..\n");
    event_base_dispatch(base);

    printf("Done..\n");
    kvClusterAsyncFree(acc);
    kvFreeTLSContext(tls);
    event_base_free(base);
    return 0;
}
