#include <kv/async.h>

#include <kv/adapters/poll.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Put in the global scope, so that loop can be explicitly stopped */
static int exit_loop = 0;

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
        exit_loop = 1;
        return;
    }

    printf("Connected...\n");
}

void disconnectCallback(const kvAsyncContext *c, int status) {
    exit_loop = 1;
    if (status != KV_OK) {
        printf("Error: %s\n", c->errstr);
        return;
    }

    printf("Disconnected...\n");
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);

    kvAsyncContext *c = kvAsyncConnect("127.0.0.1", 6379);
    if (c->err) {
        /* Let *c leak for now... */
        printf("Error: %s\n", c->errstr);
        return 1;
    }

    kvPollAttach(c);
    kvAsyncSetConnectCallback(c, connectCallback);
    kvAsyncSetDisconnectCallback(c, disconnectCallback);
    kvAsyncCommand(
        c, NULL, NULL, "SET key %b", argv[argc - 1], strlen(argv[argc - 1]));
    kvAsyncCommand(c, getCallback, (char *)"end-1", "GET key");
    while (!exit_loop) {
        kvPollTick(c, 0.1);
    }
    return 0;
}
