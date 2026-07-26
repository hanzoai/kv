/*
 * Copyright (c) 2015-2017, Ieshen Zheng <ieshen.zheng at 163 dot com>
 * Copyright (c) 2020, Nick <heronr1 at gmail dot com>
 * Copyright (c) 2020-2021, Bjorn Svensson <bjorn.a.svensson at est dot tech>
 * Copyright (c) 2021, Red Hat
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef KV_CLUSTER_H
#define KV_CLUSTER_H

#include "async.h"
#include "kv.h"
#include "visibility.h"

#define KVCLUSTER_SLOTS 16384

#define KV_ROLE_UNKNOWN 0
#define KV_ROLE_PRIMARY 1
#define KV_ROLE_REPLICA 2

/* Events, for event_callback in kvClusterOptions. */
#define KVCLUSTER_EVENT_SLOTMAP_UPDATED 1
#define KVCLUSTER_EVENT_READY 2
#define KVCLUSTER_EVENT_FREE_CONTEXT 3

#ifdef __cplusplus
extern "C" {
#endif

struct dict;
struct hilist;
struct kvClusterAsyncContext;
struct kvTLSContext;

typedef void(kvClusterCallbackFn)(struct kvClusterAsyncContext *,
                                      void *, void *);
typedef struct kvClusterNode {
    char *name;
    char *addr;
    char *host;
    uint16_t port;
    uint8_t role;
    uint8_t pad;
    int failure_count; /* consecutive failing attempts in async */
    kvContext *con;
    kvAsyncContext *acon;
    int64_t lastConnectionAttempt; /* Timestamp */
    struct hilist *slots;
    struct hilist *replicas;
} kvClusterNode;

typedef struct cluster_slot {
    uint32_t start;
    uint32_t end;
    kvClusterNode *node; /* Owner of slot region. */
} cluster_slot;

/* Context for accessing a KV Cluster */
typedef struct kvClusterContext {
    int err;          /* Error flags, 0 when there is no error */
    char errstr[128]; /* String representation of error when applicable */

    /* Configurations */
    int options;                     /* Client configuration */
    int flags;                       /* Config and state flags */
    struct timeval *connect_timeout; /* TCP connect timeout */
    struct timeval *command_timeout; /* Receive and send timeout */
    int max_retry_count;             /* Allowed retry attempts */
    char *username;                  /* Authenticate using user */
    char *password;                  /* Authentication password */

    struct dict *nodes;        /* Known kvClusterNode's */
    uint64_t route_version;    /* Increased when the node lookup table changes */
    kvClusterNode **table; /* kvClusterNode lookup table */

    struct hilist *requests; /* Outstanding commands (Pipelining) */

    int retry_count;       /* Current number of failing attempts */
    int need_update_route; /* Indicator for kvClusterReset() (Pipel.) */

    void *tls; /* Pointer to a kvTLSContext when using TLS. */
    int (*tls_init_fn)(struct kvContext *, struct kvTLSContext *);

    void (*on_connect)(const struct kvContext *c, int status);
    void (*event_callback)(const struct kvClusterContext *cc, int event,
                           void *privdata);
    void *event_privdata;

} kvClusterContext;

/* Context for accessing a KV Cluster asynchronously */
typedef struct kvClusterAsyncContext {
    /* Hold the regular context. */
    kvClusterContext cc;

    int err;      /* Error flag, 0 when there is no error,
                   * a copy of cc->err for convenience. */
    char *errstr; /* String representation of error when applicable,
                   * always pointing to cc->errstr[] */

    int64_t lastSlotmapUpdateAttempt; /* Timestamp */

    /* Attach function for an async library. */
    int (*attach_fn)(kvAsyncContext *ac, void *attach_data);
    void *attach_data;

    /* Called when either the connection is terminated due to an error or per
     * user request. The status is set accordingly (KV_OK, KV_ERR). */
    kvDisconnectCallback *onDisconnect;

    /* Called when the first write event was received. */
    kvConnectCallback *onConnect;

} kvClusterAsyncContext;

/* --- Opaque types --- */

/* 72 bytes needed when using KV's dict. */
typedef uint64_t kvClusterNodeIterator[9];

/* --- Configuration options --- */

/* Enable slotmap updates using the command CLUSTER NODES.
 * Default is the CLUSTER SLOTS command. */
#define KV_OPT_USE_CLUSTER_NODES 0x1000
/* Enable parsing of replica nodes. Currently not used, but the
 * information is added to its primary node structure. */
#define KV_OPT_USE_REPLICAS 0x2000
/* Use a blocking slotmap update after an initial async connect. */
#define KV_OPT_BLOCKING_INITIAL_UPDATE 0x4000

typedef struct {
    const char *initial_nodes;             /* Initial cluster node address(es). */
    int options;                           /* Bit field of KV_OPT_xxx */
    const struct timeval *connect_timeout; /* Timeout value for connect, no timeout if NULL. */
    const struct timeval *command_timeout; /* Timeout value for commands, no timeout if NULL. */
    const char *username;                  /* Authentication username. */
    const char *password;                  /* Authentication password. */
    int max_retry;                         /* Allowed retry attempts. */

    /* Common callbacks. */

    /* A hook to get notified when certain events occur. The `event` is set to
     * KVCLUSTER_EVENT_SLOTMAP_UPDATED when the slot mapping has been updated;
     * KVCLUSTER_EVENT_READY when the slot mapping has been fetched for the first
     * time and the client is ready to accept commands;
     * KVCLUSTER_EVENT_FREE_CONTEXT when the cluster context is being freed. */
    void (*event_callback)(const struct kvClusterContext *cc, int event, void *privdata);
    void *event_privdata;

    /* Synchronous API callbacks. */

    /* A hook for connect and reconnect attempts, e.g. for applying additional
     * socket options. This is called just after connect, before TLS handshake and
     * KV authentication.
     *
     * On successful connection, `status` is set to `KV_OK` and the file
     * descriptor can be accessed as `c->fd` to apply socket options.
     *
     * On failed connection attempt, this callback is called with `status` set to
     * `KV_ERR`. The `err` field in the `kvContext` can be used to find out
     * the cause of the error. */
    void (*connect_callback)(const kvContext *c, int status);

    /* Asynchronous API callbacks. */

    /* A hook for asynchronous connect or reconnect attempts.
     *
     * On successful connection, `status` is set to `KV_OK`.
     * On failed connection attempt, this callback is called with `status` set to
     * `KV_ERR`. The `err` field in the `kvAsyncContext` can be used to
     * find out the cause of the error. */
    void (*async_connect_callback)(struct kvAsyncContext *, int status);

    /* A hook for asynchronous disconnections.
     * Called when either a connection is terminated due to an error or per
     * user request. The status is set accordingly (KV_OK, KV_ERR). */
    void (*async_disconnect_callback)(const struct kvAsyncContext *, int status);

    /* Event engine attach function, initiated using a engine specific helper. */
    int (*attach_fn)(kvAsyncContext *ac, void *attach_data);
    void *attach_data;

    /* TLS context, initiated using kvCreateTLSContext. */
    void *tls;
    int (*tls_init_fn)(struct kvContext *, struct kvTLSContext *);
} kvClusterOptions;

/* --- Synchronous API --- */

LIBKV_API kvClusterContext *kvClusterConnectWithOptions(const kvClusterOptions *options);
LIBKV_API kvClusterContext *kvClusterConnect(const char *addrs);
LIBKV_API kvClusterContext *kvClusterConnectWithTimeout(const char *addrs, const struct timeval tv);
LIBKV_API void kvClusterFree(kvClusterContext *cc);

/* Options configurable in runtime. */
LIBKV_API int kvClusterSetOptionTimeout(kvClusterContext *cc, const struct timeval tv);

/* Blocking
 * The following functions will block for a reply, or return NULL if there was
 * an error in performing the command.
 */

/* Variadic commands (like printf) */
LIBKV_API void *kvClusterCommand(kvClusterContext *cc, const char *format, ...);
LIBKV_API void *kvClusterCommandToNode(kvClusterContext *cc,
                                               kvClusterNode *node, const char *format,
                                               ...);
/* Variadic using va_list */
LIBKV_API void *kvClustervCommand(kvClusterContext *cc, const char *format,
                                          va_list ap);
LIBKV_API void *kvClustervCommandToNode(kvClusterContext *cc,
                                                kvClusterNode *node, const char *format,
                                                va_list ap);
/* Using argc and argv */
LIBKV_API void *kvClusterCommandArgv(kvClusterContext *cc, int argc,
                                             const char **argv, const size_t *argvlen);
/* Send a KV protocol encoded string */
LIBKV_API void *kvClusterFormattedCommand(kvClusterContext *cc, char *cmd,
                                                  int len);

/* Pipelining
 * The following functions will write a command to the output buffer.
 * A call to `kvClusterGetReply()` will flush all commands in the output
 * buffer and read until it has a reply from the first command in the buffer.
 */

/* Variadic commands (like printf) */
LIBKV_API int kvClusterAppendCommand(kvClusterContext *cc, const char *format,
                                             ...);
LIBKV_API int kvClusterAppendCommandToNode(kvClusterContext *cc,
                                                   kvClusterNode *node,
                                                   const char *format, ...);
/* Variadic using va_list */
LIBKV_API int kvClustervAppendCommand(kvClusterContext *cc, const char *format,
                                              va_list ap);
LIBKV_API int kvClustervAppendCommandToNode(kvClusterContext *cc,
                                                    kvClusterNode *node,
                                                    const char *format, va_list ap);
/* Using argc and argv */
LIBKV_API int kvClusterAppendCommandArgv(kvClusterContext *cc, int argc,
                                                 const char **argv, const size_t *argvlen);
/* Use a KV protocol encoded string as command */
LIBKV_API int kvClusterAppendFormattedCommand(kvClusterContext *cc, char *cmd,
                                                      int len);
/* Flush output buffer and return first reply */
LIBKV_API int kvClusterGetReply(kvClusterContext *cc, void **reply);

/* Reset context after a performed pipelining */
LIBKV_API void kvClusterReset(kvClusterContext *cc);

/* Update the slotmap by querying any node. */
LIBKV_API int kvClusterUpdateSlotmap(kvClusterContext *cc);

/* Get the kvContext used for communication with a given node.
 * Connects or reconnects to the node if necessary. */
LIBKV_API kvContext *kvClusterGetKVContext(kvClusterContext *cc,
                                                           kvClusterNode *node);

/* --- Asynchronous API --- */

LIBKV_API kvClusterAsyncContext *kvClusterAsyncConnectWithOptions(const kvClusterOptions *options);
LIBKV_API void kvClusterAsyncDisconnect(kvClusterAsyncContext *acc);
LIBKV_API void kvClusterAsyncFree(kvClusterAsyncContext *acc);

/* Commands */
LIBKV_API int kvClusterAsyncCommand(kvClusterAsyncContext *acc,
                                            kvClusterCallbackFn *fn, void *privdata,
                                            const char *format, ...);
LIBKV_API int kvClusterAsyncCommandToNode(kvClusterAsyncContext *acc,
                                                  kvClusterNode *node,
                                                  kvClusterCallbackFn *fn, void *privdata,
                                                  const char *format, ...);
LIBKV_API int kvClustervAsyncCommand(kvClusterAsyncContext *acc,
                                             kvClusterCallbackFn *fn, void *privdata,
                                             const char *format, va_list ap);
LIBKV_API int kvClusterAsyncCommandArgv(kvClusterAsyncContext *acc,
                                                kvClusterCallbackFn *fn, void *privdata,
                                                int argc, const char **argv,
                                                const size_t *argvlen);
LIBKV_API int kvClusterAsyncCommandArgvToNode(kvClusterAsyncContext *acc,
                                                      kvClusterNode *node,
                                                      kvClusterCallbackFn *fn,
                                                      void *privdata, int argc,
                                                      const char **argv,
                                                      const size_t *argvlen);

/* Use a KV protocol encoded string as command */
LIBKV_API int kvClusterAsyncFormattedCommand(kvClusterAsyncContext *acc,
                                                     kvClusterCallbackFn *fn,
                                                     void *privdata, char *cmd, int len);
LIBKV_API int kvClusterAsyncFormattedCommandToNode(kvClusterAsyncContext *acc,
                                                           kvClusterNode *node,
                                                           kvClusterCallbackFn *fn,
                                                           void *privdata, char *cmd,
                                                           int len);

/* Get the kvAsyncContext used for communication with a given node.
 * Connects or reconnects to the node if necessary. */
LIBKV_API kvAsyncContext *kvClusterGetKVAsyncContext(kvClusterAsyncContext *acc,
                                                                     kvClusterNode *node);

/* Cluster node iterator functions */
LIBKV_API void kvClusterInitNodeIterator(kvClusterNodeIterator *iter,
                                                 kvClusterContext *cc);
LIBKV_API kvClusterNode *kvClusterNodeNext(kvClusterNodeIterator *iter);

/* Helper functions */
LIBKV_API unsigned int kvClusterGetSlotByKey(char *key);
LIBKV_API kvClusterNode *kvClusterGetNodeByKey(kvClusterContext *cc,
                                                           char *key);

#ifdef __cplusplus
}
#endif

#endif /* KV_CLUSTER_H */
