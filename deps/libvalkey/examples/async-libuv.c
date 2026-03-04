#define _XOPEN_SOURCE 600 /* Required by libuv (pthread_rwlock_t) */
#include <kv/async.h>
#include <kv/kv.h>

#include <kv/adapters/libuv.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void debugCallback(kvAsyncContext *c, void *r, void *privdata) {
    (void)privdata; //unused
    kvReply *reply = r;
    if (reply == NULL) {
        /* The DEBUG SLEEP command will almost always fail, because we have set a 1 second timeout */
        printf("`DEBUG SLEEP` error: %s\n", c->errstr ? c->errstr : "unknown error");
        return;
    }
    /* Disconnect after receiving the reply of DEBUG SLEEP (which will not)*/
    kvAsyncDisconnect(c);
}

void getCallback(kvAsyncContext *c, void *r, void *privdata) {
    kvReply *reply = r;
    if (reply == NULL) {
        printf("`GET key` error: %s\n", c->errstr ? c->errstr : "unknown error");
        return;
    }
    printf("`GET key` result: argv[%s]: %s\n", (char *)privdata, reply->str);

    /* start another request that demonstrate timeout */
    kvAsyncCommand(c, debugCallback, NULL, "DEBUG SLEEP %f", 1.5);
}

void connectCallback(kvAsyncContext *c, int status) {
    if (status != KV_OK) {
        printf("connect error: %s\n", c->errstr);
        return;
    }
    printf("Connected...\n");
}

void disconnectCallback(const kvAsyncContext *c, int status) {
    if (status != KV_OK) {
        printf("disconnect because of error: %s\n", c->errstr);
        return;
    }
    printf("Disconnected...\n");
}

int main(int argc, char **argv) {
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    uv_loop_t *loop = uv_default_loop();

    kvAsyncContext *c = kvAsyncConnect("127.0.0.1", 6379);
    if (c->err) {
        /* Let *c leak for now... */
        printf("Error: %s\n", c->errstr);
        return 1;
    }

    kvLibuvAttach(c, loop);
    kvAsyncSetConnectCallback(c, connectCallback);
    kvAsyncSetDisconnectCallback(c, disconnectCallback);
    kvAsyncSetTimeout(c, (struct timeval){.tv_sec = 1, .tv_usec = 0});

    /*
    In this demo, we first `set key`, then `get key` to demonstrate the basic usage of libuv adapter.
    Then in `getCallback`, we start a `debug sleep` command to create 1.5 second long request.
    Because we have set a 1 second timeout to the connection, the command will always fail with a
    timeout error, which is shown in the `debugCallback`.
    */

    kvAsyncCommand(
        c, NULL, NULL, "SET key %b", argv[argc - 1], strlen(argv[argc - 1]));
    kvAsyncCommand(c, getCallback, (char *)"end-1", "GET key");

    uv_run(loop, UV_RUN_DEFAULT);
    return 0;
}
