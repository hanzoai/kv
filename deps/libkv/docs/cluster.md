# Cluster API documentation

This document describes using `libkv` in cluster mode, including an overview of the synchronous and asynchronous APIs.
It is not intended as a complete reference. For that it's always best to refer to the source code.

## Table of Contents

- [Synchronous API](#synchronous-api)
  - [Connecting](#connecting)
  - [Connection options](#connection-options)
  - [Executing commands](#executing-commands)
  - [Executing commands on a specific node](#executing-commands-on-a-specific-node)
  - [Disconnecting/cleanup](#disconnecting-cleanup)
  - [Pipelining](#pipelining)
  - [Events](#events)
- [Asynchronous API](#asynchronous-api)
  - [Connecting](#connecting-1)
  - [Connection options](#connection-options-1)
  - [Executing commands](#executing-commands-1)
  - [Executing commands on a specific node](#executing-commands-on-a-specific-node-1)
  - [Disconnecting/cleanup](#disconnecting-cleanup-1)
  - [Events](#events-1)
- [Miscellaneous](#miscellaneous)
  - [TLS support](#tls-support)
  - [Cluster node iterator](#cluster-node-iterator)
  - [Extend the list of supported commands](#extend-the-list-of-supported-commands)
  - [Random number generator](#random-number-generator)

## Synchronous API

### Connecting

There are a few alternative ways to setup and connect to a cluster.
The basic alternatives lacks most options, but can be enough for some use cases.

```c
kvClusterContext *kvClusterConnect(const char *addrs);
kvClusterContext *kvClusterConnectWithTimeout(const char *addrs,
                                                      const struct timeval tv);
```

There is also a convenience struct to specify various options.

```c
kvClusterContext *kvClusterConnectWithOptions(const kvClusterOptions *options);
```

When connecting to a cluster, `NULL` is returned when the context can't be allocated, or `err` and `errstr` are set in the returned allocated context when there are issues.
So when connecting it's simple to handle error states.

```c
kvClusterContext *cc = kvClusterConnect("127.0.0.1:6379,127.0.0.1:6380");
if (cc == NULL || cc->err) {
    fprintf(stderr, "Error: %s\n", cc ? cc->errstr : "OOM");
}
```

### Connection options

There are a variety of options you can specify using the `kvClusterOptions` struct when connecting to a cluster.
This includes information about how to connect to the cluster and defining optional callbacks and other options.
See [include/kv/cluster.h](../include/kv/cluster.h) for more details.

```c
kvClusterOptions opt = {
   .initial_nodes = "127.0.0.1:6379,127.0.0.1:6380"; // Addresses to initially connect to.
   .options = KV_OPT_USE_CLUSTER_NODES;          // See available option flags below.
   .password = "password";                           // Authenticate connections using the `AUTH` command.
};

kvClusterContext *cc = kvClusterConnectWithOptions(&opt);
if (cc == NULL || cc->err) {
    fprintf(stderr, "Error: %s\n", cc ? cc->errstr : "OOM");
}
```

There are also several flags you can specify in `kvClusterOptions.options`. It's a bitwise OR of the following flags:

| Flag | Description  |
| --- | --- |
| `KV_OPT_USE_CLUSTER_NODES` | Tells libkv to use the command `CLUSTER NODES` when updating its slot map (cluster topology).<br>Libkv uses `CLUSTER SLOTS` by default. |
| `KV_OPT_USE_REPLICAS` | Tells libkv to keep parsed information of replica nodes. |
| `KV_OPT_BLOCKING_INITIAL_UPDATE` | **ASYNC**: Tells libkv to perform the initial slot map update in a blocking fashion. The function call will wait for a slot map update before returning so that the returned context is immediately ready to accept commands. |
| `KV_OPT_REUSEADDR` | Tells libkv to set the [SO_REUSEADDR](https://man7.org/linux/man-pages/man7/socket.7.html) socket option |
| `KV_OPT_PREFER_IPV4`<br>`KV_OPT_PREFER_IPV6`<br>`KV_OPT_PREFER_IP_UNSPEC` | Informs libkv to either prefer IPv4 or IPv6 when invoking [getaddrinfo](https://man7.org/linux/man-pages/man3/gai_strerror.3.html).  `KV_OPT_PREFER_IP_UNSPEC` will cause libkv to specify `AF_UNSPEC` in the getaddrinfo call, which means both IPv4 and IPv6 addresses will be searched simultaneously.<br>Libkv prefers IPv4 by default. |
| `KV_OPT_MPTCP` | Tells libkv to use multipath TCP (MPTCP). Note that only when both the server and client are using MPTCP do they establish an MPTCP connection between them; otherwise, they use a regular TCP connection instead. |

### Executing commands

The primary command interface is a `printf`-like function that takes a format string along with a variable number of arguments.
This will construct a `RESP` command and deliver it to the correct node in the cluster.

```c
kvReply *reply = kvClusterCommand(cc, "SET %s %d", "counter", 42);

if (reply == NULL) {
    fprintf(stderr, "Communication error: %s\n", cc->err ? cc->errstr : "Unknown error");
} else if (reply->type == KV_REPLY_ERROR) {
    fprintf(stderr, "Error response from node: %s\n", reply->str);
} else {
    // Handle reply..
}
freeReplyObject(reply);
```

Commands will be sent to the cluster node that the client perceives handling the given key.
If the cluster topology has changed the KV node might respond with a redirection error which the client will handle, update its slot map and resend the command to correct node.
The reply will in this case arrive from the correct node.

If a node is unreachable, for example if the command times out or if the connect times out, it can indicate that there has been a failover and the node is no longer part of the cluster.
In this case, `kvClusterCommand` returns NULL and sets `err` and `errstr` on the cluster context, but additionally, libkv schedules a slot map update to be performed when the next command is sent.
That means that if you try the same command again, there is a good chance the command will be sent to another node and the command may succeed.

### Executing commands on a specific node

When there is a need to send commands to a specific node, the following low-level API can be used.

```c
kvReply *reply = kvClusterCommandToNode(cc, node, "DBSIZE");
```

This function handles `printf`-like arguments similar to `kvClusterCommand`, but will only attempt to send the command to the given node and will not perform redirects or retries.

If the command times out or the connection to the node fails, a slot map update is scheduled to be performed when the next command is sent.
`kvClusterCommandToNode` also performs a slot map update if it has previously been scheduled.

### Disconnecting/cleanup

To disconnect and free the context the following function can be used:

```c
kvClusterFree(cc);
```

This function closes the sockets and deallocates the context.

### Pipelining

For pipelined commands, every command is simply appended to the output buffer but not delivered to the server, until you attempt to read the first response, at which point the entire buffer will be delivered.

The `kvClusterAppendCommand` function can be used to append a command, which is identical to the `kvClusterCommand` family, apart from not returning a reply.
After calling an append function `kvClusterGetReply` can be used to receive the subsequent replies.

The following example shows a simple cluster pipeline.

```c
if (kvClusterAppendCommand(cc, "SET foo bar") != KV_OK) {
    fprintf(stderr, "Error appending command: %s\n", cc->errstr);
    exit(1);
}

if (kvClusterAppendCommand(cc, "GET foo") != KV_OK) {
    fprintf(stderr, "Error appending command: %s\n", cc->errstr);
    exit(1);
}

kvReply *reply;
if (kvClusterGetReply(cc,&reply) != KV_OK) {
    fprintf(stderr, "Error reading reply %zu: %s\n", i, c->errstr);
    exit(1);
}
// Handle the reply for SET here.
freeReplyObject(reply);

if (kvClusterGetReply(cc,&reply) != KV_OK) {
    fprintf(stderr, "Error reading reply %zu: %s\n", i, c->errstr);
    exit(1);
}
// Handle the reply for GET here.
freeReplyObject(reply);
```

### Events

#### Events per cluster context

There is a hook to get notified when certain events occur.

```c
/* Function to be called when events occur. */
void event_cb(const kvClusterContext *cc, int event, void *privdata) {
    switch (event) {
       // Handle event
    }
}

kvClusterOptions opt = {
   .event_callback = event_cb;
   .event_privdata = my_privdata; // User defined data can be provided to the callback.
};
kvClusterContext *cc = kvClusterConnectWithOptions(&opt);
```

The callback is called with `event` set to one of the following values:

* `KVCLUSTER_EVENT_SLOTMAP_UPDATED` when the slot mapping has been updated;
* `KVCLUSTER_EVENT_READY` when the slot mapping has been fetched for the first
  time and the client is ready to accept commands, useful when initiating the
  client using `kvClusterAsyncConnectWithOptions` without enabling the option
  `KV_OPT_BLOCKING_INITIAL_UPDATE` where a client is not immediately ready
  after a successful call;
* `KVCLUSTER_EVENT_FREE_CONTEXT` when the cluster context is being freed, so
  that the user can free the event `privdata`.

#### Events per connection

There is a hook to get notified about connect and reconnect attempts.
This is useful for applying socket options or access endpoint information for a connection to a particular node.
The callback is registered using an option.

```c
void connect_cb(const kvContext *c, int status) {
   // Perform desired action
}

kvClusterOptions opt = {
   .connect_callback = connect_cb;
};
kvClusterContext *cc = kvClusterConnectWithOptions(&opt);
```

The callback is called just after connect, before TLS handshake and authentication.

On successful connection, `status` is set to `KV_OK` and the `kvContext` can be used, for example,
to see which IP and port it's connected to or to set socket options directly on the file descriptor which can be accessed as `c->fd`.

On failed connection attempt, this callback is called with `status` set to `KV_ERR`.
The `err` field in the `kvContext` can be used to find out the cause of the error.

## Asynchronous API

The asynchronous API supports a wide range of event libraries and uses [adapters](../include/kv/adapters/) to attach to a specific event library.
Each adapter provide a convenience function that configures which event loop instance the created context will be attached to.

### Connecting

To asynchronously connect to a cluster a `kvClusterOptions` should first be initiated with initial nodes and more,
but it's also important to configure which event library to use before calling `kvClusterAsyncConnectWithOptions`.

```c
kvClusterOptions options = {
   .initial_nodes = "127.0.0.1:7000";
};

// Use convenience function to set which event library to use.
kvClusterOptionsUseLibev(&options, EV_DEFAULT);

// Initiate the context and start connecting to the initial nodes.
kvClusterAsyncContext *acc = kvClusterAsyncConnectWithOptions(&options);
```

Since an initial slot map update is performed asynchronously any command sent directly after `kvClusterAsyncConnectWithOptions` may fail
because the initial slot map has not yet been retrieved and the client doesn't know which cluster node to send the command to.
You may use the [`eventCallback`](#events-per-cluster-context-1) to be notified when the slot map is updated and the client is ready to accept commands.
A crude example of using the `eventCallback` can be found in [this test case](../tests/ct_async.c).

Another option is to enable blocking initial slot map updates using the option `KV_OPT_BLOCKING_INITIAL_UPDATE`.
When enabled `kvClusterAsyncConnectWithOptions` will initially connect to the cluster in a blocking fashion and wait for the slot map before returning.
Any command sent by the user thereafter will create a new non-blocking connection, unless a non-blocking connection already exists to the destination.
The function returns a pointer to a newly created `kvClusterAsyncContext` struct and its `err` field should be checked to make sure the initial slot map update was successful.

### Connection options

There is a variety of options you can specify using the `kvClusterOptions` struct when connecting to a cluster.

One asynchronous API specific option is `KV_OPT_BLOCKING_INITIAL_UPDATE` which enables the initial slot map update to be performed in a blocking fashion.
The connect function will wait for a slot map update before returning so that the returned context is immediately ready to accept commands.

See previous [Connection options](#connection-options) section for common options.

### Executing commands

Executing commands in an asynchronous context work similarly to the synchronous context, except that you can pass a callback that will be invoked when the reply is received.

```c
int status = kvClusterAsyncCommand(cc, commandCallback, privdata,
                                       "SET %s %s", "key", "value");
```

The return value is `KV_OK` when the command was successfully added to the output buffer and `KV_ERR` otherwise.
When the connection is being disconnected per user-request, no new commands may be added to the output buffer and `KV_ERR` is returned.

Commands will be sent to the cluster node that the client perceives handling the given key.
If the cluster topology has changed the KV node might respond with a redirection error which the client will handle, update its slot map and resend the command to correct node.
The reply will in this case arrive from the correct node.

The reply callback, that is called when the reply is received, should have the following prototype:

```c
void(kvClusterAsyncContext *acc, void *reply, void *privdata);
```

The `privdata` argument can be used to carry arbitrary data to the callback.

All pending callbacks are called with a `NULL` reply when the context encountered an error.

### Executing commands on a specific node

When there is a need to send commands to a specific node, the following low-level API can be used.

```c
status = kvClusterAsyncCommandToNode(acc, node, commandCallback, privdata, "DBSIZE");
```

This functions will only attempt to send the command to a specific node and will not perform redirects or retries, but communication errors will trigger a slot map update just like the commonly used API.


### Disconnecting/cleanup

Asynchronous cluster connections can be terminated using:

```c
kvClusterAsyncDisconnect(acc);
```

When this function is called, connections are **not** immediately terminated.
Instead, new commands are no longer accepted and connections are only terminated when all pending commands have been written to a socket, their respective replies have been read and their respective callbacks have been executed.
After this, the disconnection callback is executed with the `KV_OK` status and the context object is freed.

### Events

#### Events per cluster context

Use [`event_callback` in `kvClusterOptions`](#events-per-cluster-context) to get notified when certain events occur.

When the callback function requires the current `kvClusterAsyncContext`, it can typecast the given `kvClusterContext` to a `kvClusterAsyncContext`.
The `kvClusterAsyncContext` struct is an extension of the `kvClusterContext` struct.

```c
void eventCallback(const kvClusterContext *cc, int event, void *privdata) {
   kvClusterAsyncContext *acc = (kvClusterAsyncContext *)cc;
}
```

#### Events per connection

Because the connections that will be created are non-blocking, the kernel is not able to instantly return if the specified host and port is able to accept a connection.
Instead, use a connect callback to be notified when a connection is established or failed.
Similarly, a disconnect callback can be used to be notified about a disconnected connection (either because of an error or per user request).
The callbacks can be enabled using the following options when calling `kvClusterAsyncConnectWithOptions`:

```c
kvClusterOptions opt = {
   .async_connect_callback = connect_cb;
   .async_disconnect_callback = disconnect_cb;
}
```

The connect callback function should have the following prototype, aliased to `kvConnectCallback`:
```c
void(kvAsyncContext *ac, int status);
```

On a connection attempt, the `status` argument is set to `KV_OK` when the connection was successful.
The file description of the connection socket can be retrieved from a `kvAsyncContext` as `ac->c->fd`.

The disconnect callback function should have the following prototype, aliased to `kvDisconnectCallback`:
```c
void(const kvAsyncContext *ac, int status);
```

On a disconnection the `status` argument is set to `KV_OK` if it was initiated by the user, or to `KV_ERR` when it was caused by an error.
When caused by an error the `err` field in the context can be accessed to get the error cause.
You don't need to reconnect in the disconnect callback since libkv will reconnect by itself when the next command is handled.

## Miscellaneous

### TLS support

TLS support is not enabled by default and requires an explicit build flag as described in [`README.md`](../README.md#building).

When support is enabled, TLS can be enabled on a cluster context using a prepared `kvTLSContext` and the options `kvClusterOptions.tls` and `kvClusterOptions.tls_init_fn`.

```c
// Initialize the OpenSSL library.
kvInitOpenSSL();

// Initialize a kvTLSContext, which holds an OpenSSL context.
kvTLSContext *tls = kvCreateTLSContext("ca.crt", NULL, "client.crt",
                                               "client.key", NULL, NULL);
// Set options to enable TLS on context.
kvClusterOptions opt = {
   .tls = tls;
   .tls_init_fn = &kvInitiateTLSWithContext;
};

kvClusterContext *cc = kvClusterConnectWithOptions(&opt);
if (cc == NULL || cc->err) {
    fprintf(stderr, "Error: %s\n", cc ? cc->errstr : "OOM");
}
```

### Cluster node iterator

A `kvClusterNodeIterator` can be used to iterate on all known master nodes in a cluster context.
First it needs to be initiated using `kvClusterInitNodeIterator` and then you can repeatedly call `kvClusterNodeNext` to get the next node from the iterator.

```c
kvClusterNodeIterator ni;
kvClusterInitNodeIterator(&ni, cc);

kvClusterNode *node;
while ((node = kvClusterNodeNext(&ni)) != NULL) {
   kvReply *reply = kvClusterCommandToNode(cc, node, "DBSIZE");
   // Handle reply..
}
```

The iterator will handle changes due to slot map updates by restarting the iteration, but on the new set of master nodes.
There is no bookkeeping for already iterated nodes when a restart is triggered, which means that a node can be iterated over more than once depending on when the slot map update happened and the change of cluster nodes.

Note that when `kvClusterCommandToNode` is called, a slot map update can happen if it has been scheduled by the previous command, for example if the previous call to `kvClusterCommandToNode` timed out or the node wasn't reachable.

To detect when the slot map has been updated, you can check if the slot map version (`iter.route_version`) is equal to the current cluster context's slot map version (`cc->route_version`).
If it isn't, it means that the slot map has been updated and the iterator will restart itself at the next call to `kvClusterNodeNext`.

Another way to detect that the slot map has been updated is to [register an event callback](#events-per-cluster-context) and look for the event `KVCLUSTER_EVENT_SLOTMAP_UPDATED`.

### Extend the list of supported commands

The list of commands and the position of the first key in the command line is defined in [`src/cmddef.h`](../src/cmddef.h) which is included in this repository.
It has been generated using the `JSON` files describing the syntax of each command in the KV repository, which makes sure we support all commands in KV, at least in terms of cluster routing.
To add support for custom commands defined in modules, you can regenerate `cmddef.h` using the script [`gencommands.py`](../script/gencommands.py).
Use the `JSON` files from KV and any additional files on the same format as arguments to the script.
For details, see the comments inside `gencommands.py`.

### Random number generator

This library uses [random()](https://linux.die.net/man/3/random) while selecting a node used for requesting the cluster topology (slot map).
A user should seed the random number generator using [`srandom()`](https://linux.die.net/man/3/srandom) to get less predictability in the node selection.
