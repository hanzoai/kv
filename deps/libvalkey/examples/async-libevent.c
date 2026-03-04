#include <kv/async.h>
#include <kv/kv.h>

#include <kv/adapters/libevent.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void getCallback(kvAsyncContext *c, void *r, void *privdata) {
    kvReply *reply = r;
    if (reply == NULL) {
        if (c->errstr) {
            printf("errstr: %s\n", c->errstr);
        }
        return;
    }
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
    kvOptions options = {0};
    KV_OPTIONS_SET_TCP(&options, "127.0.0.1", 6379);
    struct timeval tv = {0};
    tv.tv_sec = 1;
    options.connect_timeout = &tv;

    kvAsyncContext *c = kvAsyncConnectWithOptions(&options);
    if (c->err) {
        /* Let *c leak for now... */
        printf("Error: %s\n", c->errstr);
        return 1;
    }

    kvLibeventAttach(c, base);
    kvAsyncSetConnectCallback(c, connectCallback);
    kvAsyncSetDisconnectCallback(c, disconnectCallback);
    kvAsyncCommand(
        c, NULL, NULL, "SET key %b", argv[argc - 1], strlen(argv[argc - 1]));
    kvAsyncCommand(c, getCallback, (char *)"end-1", "GET key");
    event_base_dispatch(base);
    return 0;
}
