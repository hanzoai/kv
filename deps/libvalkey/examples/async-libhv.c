#include <kv/async.h>
#include <kv/kv.h>

#include <kv/adapters/libhv.h>

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

void debugCallback(kvAsyncContext *c, void *r, void *privdata) {
    (void)privdata;
    kvReply *reply = r;

    if (reply == NULL) {
        printf("`DEBUG SLEEP` error: %s\n", c->errstr ? c->errstr : "unknown error");
        return;
    }

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

    kvAsyncContext *c = kvAsyncConnect("127.0.0.1", 6379);
    if (c->err) {
        /* Let *c leak for now... */
        printf("Error: %s\n", c->errstr);
        return 1;
    }

    hloop_t *loop = hloop_new(HLOOP_FLAG_QUIT_WHEN_NO_ACTIVE_EVENTS);
    kvLibhvAttach(c, loop);
    kvAsyncSetTimeout(c, (struct timeval){.tv_sec = 0, .tv_usec = 500000});
    kvAsyncSetConnectCallback(c, connectCallback);
    kvAsyncSetDisconnectCallback(c, disconnectCallback);
    kvAsyncCommand(
        c, NULL, NULL, "SET key %b", argv[argc - 1], strlen(argv[argc - 1]));
    kvAsyncCommand(c, getCallback, (char *)"end-1", "GET key");
    kvAsyncCommand(c, debugCallback, NULL, "DEBUG SLEEP %d", 1);
    hloop_run(loop);
    hloop_free(&loop);
    return 0;
}
