#include <kv/async.h>
#include <kv/tls.h>
#include <kv/kv.h>

#include <kv/adapters/libevent.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void getCallback(kvAsyncContext *c, void *r, void *privdata) {
    kvReply *reply = r;
    if (reply == NULL)
        return;
    printf("argv[%s]: %s\n", (char *)privdata, reply->str);

    /* Disconnect after receiving the reply to GET */
    kvAsyncDisconnect(c);
}

void connectCallback(kvAsyncContext *c, int status) {
    if (status != KV_OK) {
        printf("Error: %s\n", c->errstr);
        return;
    }
    printf("Connected...\n");
}

void disconnectCallback(const kvAsyncContext *c, int status) {
    if (status != KV_OK) {
        printf("Error: %s\n", c->errstr);
        return;
    }
    printf("Disconnected...\n");
}

int main(int argc, char **argv) {
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    struct event_base *base = event_base_new();
    if (argc < 5) {
        fprintf(stderr,
                "Usage: %s <key> <host> <port> <cert> <certKey> [ca]\n", argv[0]);
        exit(1);
    }

    const char *value = argv[1];
    size_t nvalue = strlen(value);

    const char *hostname = argv[2];
    int port = atoi(argv[3]);

    const char *cert = argv[4];
    const char *certKey = argv[5];
    const char *caCert = argc > 5 ? argv[6] : NULL;

    kvTLSContext *tls;
    kvTLSContextError tls_error = KV_TLS_CTX_NONE;

    kvInitOpenSSL();

    tls = kvCreateTLSContext(caCert, NULL,
                                 cert, certKey, NULL, &tls_error);
    if (!tls) {
        printf("Error: %s\n", kvTLSContextGetError(tls_error));
        return 1;
    }

    kvAsyncContext *c = kvAsyncConnect(hostname, port);
    if (c->err) {
        /* Let *c leak for now... */
        printf("Error: %s\n", c->errstr);
        return 1;
    }
    if (kvInitiateTLSWithContext(&c->c, tls) != KV_OK) {
        printf("TLS Error!\n");
        exit(1);
    }

    kvLibeventAttach(c, base);
    kvAsyncSetConnectCallback(c, connectCallback);
    kvAsyncSetDisconnectCallback(c, disconnectCallback);
    kvAsyncCommand(c, NULL, NULL, "SET key %b", value, nvalue);
    kvAsyncCommand(c, getCallback, (char *)"end-1", "GET key");
    event_base_dispatch(base);

    kvFreeTLSContext(tls);
    return 0;
}
