#include <adapters/kvmoduleapi.h>
#include <async.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kv.h>

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
        if (c->errstr) {
            printf("errstr: %s\n", c->errstr);
        }
        return;
    }
    printf("argv[%s]: %s\n", (char *)privdata, reply->str);

    /* start another request that demonstrate timeout */
    kvAsyncCommand(c, debugCallback, NULL, "DEBUG SLEEP %f", 1.5);
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

/*
 * 1- Compile this file as a shared library. Directory of "kvmodule.h" must
 *    be in the include path.
 *       gcc -fPIC -shared -I../../kv/src/ -I.. example-kvmoduleapi.c -o example-kvmoduleapi.so
 *
 * 2- Load module:
 *       kv-server --loadmodule ./example-kvmoduleapi.so value
 */
int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    int ret = KVModule_Init(ctx, "example-kvmoduleapi", 1, KVMODULE_APIVER_1);
    if (ret != KVMODULE_OK) {
        printf("error module init \n");
        return KVMODULE_ERR;
    }

    kvAsyncContext *c = kvAsyncConnect("127.0.0.1", 6379);
    if (c->err) {
        /* Let *c leak for now... */
        printf("Error: %s\n", c->errstr);
        return 1;
    }

    size_t len;
    const char *val = KVModule_StringPtrLen(argv[argc - 1], &len);

    KVModuleCtx *module_ctx = KVModule_GetDetachedThreadSafeContext(ctx);
    kvModuleAttach(c, module_ctx);
    kvAsyncSetConnectCallback(c, connectCallback);
    kvAsyncSetDisconnectCallback(c, disconnectCallback);
    kvAsyncSetTimeout(c, (struct timeval){.tv_sec = 1, .tv_usec = 0});

    /*
    In this demo, we first `set key`, then `get key` to demonstrate the basic usage of the adapter.
    Then in `getCallback`, we start a `debug sleep` command to create 1.5 second long request.
    Because we have set a 1 second timeout to the connection, the command will always fail with a
    timeout error, which is shown in the `debugCallback`.
    */

    kvAsyncCommand(c, NULL, NULL, "SET key %b", val, len);
    kvAsyncCommand(c, getCallback, (char *)"end-1", "GET key");
    return 0;
}
