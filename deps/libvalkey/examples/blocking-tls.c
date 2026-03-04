#include <kv/tls.h>
#include <kv/kv.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#include <winsock2.h> /* For struct timeval */
#endif

int main(int argc, char **argv) {
    unsigned int j;
    kvTLSContext *tls;
    kvTLSContextError tls_error = KV_TLS_CTX_NONE;
    kvContext *c;
    kvReply *reply;
    if (argc < 4) {
        printf("Usage: %s <host> <port> <cert> <key> [ca]\n", argv[0]);
        exit(1);
    }
    const char *hostname = (argc > 1) ? argv[1] : "127.0.0.1";
    int port = atoi(argv[2]);
    const char *cert = argv[3];
    const char *key = argv[4];
    const char *ca = argc > 4 ? argv[5] : NULL;

    kvInitOpenSSL();
    tls = kvCreateTLSContext(ca, NULL, cert, key, NULL, &tls_error);
    if (!tls || tls_error != KV_TLS_CTX_NONE) {
        printf("TLS Context error: %s\n", kvTLSContextGetError(tls_error));
        exit(1);
    }

    struct timeval tv = {1, 500000}; // 1.5 seconds
    kvOptions options = {0};
    KV_OPTIONS_SET_TCP(&options, hostname, port);
    options.connect_timeout = &tv;
    c = kvConnectWithOptions(&options);

    if (c == NULL || c->err) {
        if (c) {
            printf("Connection error: %s\n", c->errstr);
            kvFree(c);
        } else {
            printf("Connection error: can't allocate kv context\n");
        }
        exit(1);
    }

    if (kvInitiateTLSWithContext(c, tls) != KV_OK) {
        printf("Couldn't initialize TLS!\n");
        printf("Error: %s\n", c->errstr);
        kvFree(c);
        exit(1);
    }

    /* PING server */
    reply = kvCommand(c, "PING");
    printf("PING: %s\n", reply->str);
    freeReplyObject(reply);

    /* Set a key */
    reply = kvCommand(c, "SET %s %s", "foo", "hello world");
    printf("SET: %s\n", reply->str);
    freeReplyObject(reply);

    /* Set a key using binary safe API */
    reply = kvCommand(c, "SET %b %b", "bar", (size_t)3, "hello", (size_t)5);
    printf("SET (binary API): %s\n", reply->str);
    freeReplyObject(reply);

    /* Try a GET and two INCR */
    reply = kvCommand(c, "GET foo");
    printf("GET foo: %s\n", reply->str);
    freeReplyObject(reply);

    reply = kvCommand(c, "INCR counter");
    printf("INCR counter: %lld\n", reply->integer);
    freeReplyObject(reply);
    /* again ... */
    reply = kvCommand(c, "INCR counter");
    printf("INCR counter: %lld\n", reply->integer);
    freeReplyObject(reply);

    /* Create a list of numbers, from 0 to 9 */
    reply = kvCommand(c, "DEL mylist");
    freeReplyObject(reply);
    for (j = 0; j < 10; j++) {
        char buf[64];

        snprintf(buf, 64, "%u", j);
        reply = kvCommand(c, "LPUSH mylist element-%s", buf);
        freeReplyObject(reply);
    }

    /* Let's check what we have inside the list */
    reply = kvCommand(c, "LRANGE mylist 0 -1");
    if (reply->type == KV_REPLY_ARRAY) {
        for (j = 0; j < reply->elements; j++) {
            printf("%u) %s\n", j, reply->element[j]->str);
        }
    }
    freeReplyObject(reply);

    /* Disconnects and frees the context */
    kvFree(c);

    kvFreeTLSContext(tls);

    return 0;
}
