#include <kv/cluster.h>
#include <kv/tls.h>

#include <stdio.h>
#include <stdlib.h>

#define CLUSTER_NODE_TLS "127.0.0.1:7301"

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

    struct timeval timeout = {1, 500000}; // 1.5s

    kvClusterOptions options = {0};
    options.initial_nodes = CLUSTER_NODE_TLS;
    options.connect_timeout = &timeout;
    options.tls = tls;
    options.tls_init_fn = &kvInitiateTLSWithContext;

    kvClusterContext *cc = kvClusterConnectWithOptions(&options);
    if (!cc) {
        printf("Error: Allocation failure\n");
        exit(-1);
    } else if (cc->err) {
        printf("Error: %s\n", cc->errstr);
        // handle error
        exit(-1);
    }

    kvReply *reply = kvClusterCommand(cc, "SET %s %s", "key", "value");
    if (!reply) {
        printf("Reply missing: %s\n", cc->errstr);
        exit(-1);
    }
    printf("SET: %s\n", reply->str);
    freeReplyObject(reply);

    kvReply *reply2 = kvClusterCommand(cc, "GET %s", "key");
    if (!reply2) {
        printf("Reply missing: %s\n", cc->errstr);
        exit(-1);
    }
    printf("GET: %s\n", reply2->str);
    freeReplyObject(reply2);

    kvClusterFree(cc);
    kvFreeTLSContext(tls);
    return 0;
}
