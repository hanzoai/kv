#include "test_utils.h"

#include "cluster.h"

#include <stdlib.h>
#include <string.h>

static int server_version_major;
static int server_version_minor;

/* Helper to extract version information. */
#define KV_VERSION_FIELD "kv_version:"
#define REDIS_VERSION_FIELD "redis_version:"
void load_kv_version(kvClusterContext *cc) {
    kvClusterNodeIterator ni;
    kvClusterNode *node;
    char *eptr, *s, *e;
    kvReply *reply = NULL;

    kvClusterInitNodeIterator(&ni, cc);
    if ((node = kvClusterNodeNext(&ni)) == NULL)
        goto abort;

    reply = kvClusterCommandToNode(cc, node, "INFO");
    if (reply == NULL || cc->err || reply->type != KV_REPLY_STRING)
        goto abort;
    if ((s = strstr(reply->str, KV_VERSION_FIELD)) != NULL)
        s += strlen(KV_VERSION_FIELD);
    else if ((s = strstr(reply->str, REDIS_VERSION_FIELD)) != NULL)
        s += strlen(REDIS_VERSION_FIELD);
    else
        goto abort;

    /* We need a field terminator and at least 'x.y.z' (5) bytes of data */
    if ((e = strstr(s, "\r\n")) == NULL || (e - s) < 5)
        goto abort;

    /* Extract version info */
    server_version_major = strtol(s, &eptr, 10);
    if (*eptr != '.')
        goto abort;
    server_version_minor = strtol(eptr + 1, NULL, 10);

    freeReplyObject(reply);
    return;

abort:
    freeReplyObject(reply);
    fprintf(stderr, "Error: Cannot get KV version, aborting..\n");
    exit(1);
}

/* Helper to verify KV version information. */
int kv_version_less_than(int major, int minor) {
    if (server_version_major == 0) {
        fprintf(stderr, "Error: KV version not loaded, aborting..\n");
        exit(1);
    }

    if (server_version_major < major)
        return 1;
    if (server_version_major == major && server_version_minor < minor)
        return 1;
    return 0;
}
