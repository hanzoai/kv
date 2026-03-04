# Standalone API documentation

This document describes using `libkv` in standalone (non-cluster) mode, including an overview of the synchronous and asynchronous APIs. It is not intended as a complete reference. For that it's always best to refer to the source code.

## Table of Contents

- [Synchronous API](#synchronous-api)
  - [Connecting](#connecting)
  - [Connection options](#connection-options)
  - [Executing commands](#executing-commands)
  - [Using replies](#using-replies)
  - [Reply types](#reply-types)
  - [Disconnecting/cleanup](#disconnecting-cleanup)
  - [Pipelining](#pipelining)
  - [Errors](#errors)
  - [Thread safety](#thread-safety)
  - [Reader configuration](#reader-configuration)
    - [Input buffer size](#maximum-input-buffer-size)
    - [Maximum array elements](#maximum-array-elements)
    - [RESP3 Push Replies](#resp3-push-replies)
    - [Allocator injection](#allocator-injection)
- [Asynchronous API](#asynchronous-api)
  - [Connecting](#connecting-1)
  - [Executing commands](#executing-commands-1)
  - [Disconnecting/cleanup](#disconnecting-cleanup-1)
- [TLS support](#tls-support)

## Synchronous API

The synchronous API has a pretty small surface area, with only a few commands to use. In general they are very similar to the way printf works, except they construct `RESP` commands.

### Connecting

There are several convenience functions to connect in various ways (e.g. host and port, Unix socket, etc). See [include/kv/kv.h](../include/kv/kv.h) for more details.

```c
kvContext *kvConnect(const char *host, int port);
kvContext *kvConnectUnix(const char *path);

// There is also a convenience struct to specify various options.
kvContext *kvConnectWithOptions(kvOptions *opt);
```

When connecting to a server, libkv will return `NULL` in the event that we can't allocate the context, and set the `err` member if we can connect but there are issues. So when connecting it's simple to handle error states.

```c
kvContext *ctx = kvConnect("localhost", 6379);
if (ctx == NULL || ctx->err) {
    fprintf(stderr, "Error connecting: %s\n", ctx ? ctx->errstr : "OOM");
}
```

### Connection options

There are a variety of options you can specify when connecting to the server, which are delivered via the `kvOptions` helper struct. This includes information to connect to the server as well as other flags.

```c
kvOptions opt = {0};

// You can set primary connection info
if (tcp)  {
    KV_OPTIONS_SET_TCP(&opt, "localhost", 6379);
} else {
    KV_OPTIONS_SET_UNIX(&opt, "/tmp/kv.sock");
}

// You may attach any arbitrary data to the context
KV_OPTIONS_SET_PRIVDATA(&opt, my_data);
```

There are also several flags you can specify when using the `kvOptions` helper struct.

| Flag | Description  |
| --- | --- |
| `KV_OPT_NONBLOCK` | Tells libkv to make a non-blocking connection. |
| `KV_OPT_REUSEADDR` | Tells libkv to set the [SO_REUSEADDR](https://man7.org/linux/man-pages/man7/socket.7.html) socket option |
| `KV_OPT_PREFER_IPV4`<br>`KV_OPT_PREFER_IPV6`<br>`KV_OPT_PREFER_IP_UNSPEC` | Informs libkv to either prefer IPv4 or IPv6 when invoking [getaddrinfo](https://man7.org/linux/man-pages/man3/gai_strerror.3.html).  `KV_OPT_PREFER_IP_UNSPEC` will cause libkv to specify `AF_UNSPEC` in the getaddrinfo call, which means both IPv4 and IPv6 addresses will be searched simultaneously.<br>Libkv prefers IPv4 by default. |
| `KV_OPT_NO_PUSH_AUTOFREE` | Tells libkv to not install the default RESP3 PUSH handler (which just intercepts and frees the replies).  This is useful in situations where you want to process these messages in-band. |
| `KV_OPT_NOAUTOFREEREPLIES` | **ASYNC**: tells libkv not to automatically invoke `freeReplyObject` after executing the reply callback. |
| `KV_OPT_NOAUTOFREE` | **ASYNC**: Tells libkv not to automatically free the `kvAsyncContext` on connection/communication failure, but only if the user makes an explicit call to `kvAsyncDisconnect` or `kvAsyncFree` |
| `KV_OPT_MPTCP` | Tells libkv to use multipath TCP (MPTCP). Note that only when both the server and client are using MPTCP do they establish an MPTCP connection between them; otherwise, they use a regular TCP connection instead. |

### Executing commands

The primary command interface is a `printf`-like function that takes a format string along with a variable number of arguments. This will construct a `RESP` command and deliver it to the server.

```c
kvReply *reply = kvCommand(ctx, "INCRBY %s %d", "counter", 42);

if (reply == NULL) {
    fprintf(stderr, "Communication error: %s\n", c->err ? c->errstr : "Unknown error");
} else if (reply->type == KV_REPLY_ERROR) {
    fprintf(stderr, "Error response from server: %s\n", reply->str);
} else if (reply->type != KV_REPLY_INTEGER) {
    // Very unlikely but should be checked.
    fprintf(stderr, "Error:  Non-integer reply to INCRBY?\n");
}

printf("New value of 'counter' is %lld\n", reply->integer);
freeReplyObject(reply);
```

If you need to deliver binary safe strings to the server, you can use the `%b` format specifier which requires you to pass the length as well.

```c
struct binary { int x; int y; } = {0xdeadbeef, 0xcafebabe};
kvReply *reply = kvCommand(ctx, "SET %s %b", "some-key", &binary, sizeof(binary));
```

Commands may also be constructed by sending an array of arguments along with an optional array of their lengths. If lengths are not provided, libkv will execute `strlen` on each argument.

```c
const char *argv[] = {"SET", "captain", "James Kirk"};
sonst size_t argvlens[] = {3, 7, 10};

kvReply *reply = kvCommandArgv(ctx, 3, argv, argvlens);
// Handle error conditions similarly to `kvCommand`
```

### Using replies

The `kvCommand` and `kvCommandArgv` functions return a `kvReply` on success and `NULL` in the event of a severe error (e.g. a communication failure with the server, out of memory condition, etc).

If the reply is `NULL` you can inspect the nature of the error by querying `kvContext->err` for the error code and `kvContext->errstr` for a human readable error string.

When a `kvReply` is returned, you should test the `kvReply->type` field to determine which kind of reply was received from the server. If for example there was an error in the command, this reply can be `KV_REPLY_ERROR` and the specific error string will be in the `reply->str` member.

### Reply types

- `KV_REPLY_ERROR` - An error reply. The error string is in `reply->str`.
- `KV_REPLY_STATUS` - A status reply which will be in `reply->str`.
- `KV_REPLY_INTEGER` - An integer reply, which will be in `reply->integer`.
- `KV_REPLY_DOUBLE` - A double reply which will be in `reply->dval` as well as `reply->str`.
- `KV_REPLY_NIL` - a nil reply.
- `KV_REPLY_BOOL` - A boolean reply which will be in `reply->integer`.
- `KV_REPLY_BIGNUM` - As of yet unused, but the string would be in `reply->str`.
- `KV_REPLY_STRING` - A string reply which will be in `reply->str`.
- `KV_REPLY_VERB` - A verbatim string reply which will be in `reply->str` and who's type will be in `reply->vtype`.
- `KV_REPLY_ARRAY` - An array reply where each element is in `reply->element` with the number of elements in `reply->elements`.
- `KV_REPLY_MAP` - A map reply, which structurally looks just like `KV_REPLY_ARRAY` only is meant to represent keys and values. As with an array reply you can access the elements with `reply->element` and `reply->elements`.
- `KV_REPLY_SET` - Another array-like reply representing a set (e.g. a reply from `SMEMBERS`). Access via `reply->element` and `reply->elements`.
- `KV_REPLY_ATTR` - An attribute reply. As of yet unused by kv-server.
- `KV_REPLY_PUSH` - An out of band push reply. This is also array-like in nature.

### Disconnecting/cleanup

When libkv returns non-null `kvReply` struts you are responsible for freeing them with `freeReplyObject`.  In order to disconnect and free the context simply call `kvFree`.

```c
kvReply *reply = kvCommand(ctx, "set %s %s", "foo", "bar");
// Error handling ...
freeReplyObject(reply);

// Disconnect and free context
kvFree(ctx);
```

### Pipelining

`kvCommand` and `kvCommandArgv` each make a round-trip to the server, by sending the command and then waiting for a reply. Alternatively commands may be pipelined with the `kvAppendCommand` and `kvAppendCommandArgv` functions.

When you use `kvAppendCommand` the command is simply appended to the output buffer of `kvContext` but not delivered to the server, until you attempt to read the first response, at which point the entire buffer will be delivered.

```c
// No data will be delivered to the server while these commands are being appended.
for (size_t i = 0; i < 100000; i++) {
    if (kvAppendCommand(c, "INCRBY key:%zu %zu", i, i) != KV_OK) {
        fprintf(stderr, "Error appending command: %s\n", c->errstr);
        exit(1);
    }
}

// The entire output buffer will be delivered on the first call to `kvGetReply`.
for (size_t i = 0; i < 100000; i++) {
    if (kvGetReply(c, (void**)&reply) != KV_OK) {
        fprintf(stderr, "Error reading reply %zu: %s\n", i, c->errstr);
        exit(1);
    } else if (reply->type != KV_REPLY_INTEGER) {
        fprintf(stderr, "Error:  Non-integer reply to INCRBY?\n");
        exit(1);
    }

    printf("INCRBY key:%zu => %lld\n", i, reply->integer);
    freeReplyObject(reply);
}
```

`kvGetReply` can also be used in other contexts than pipeline, for example when you want to continuously block for commands for example in a subscribe context.

```c
kvReply *reply = kvCommand(c, "SUBSCRIBE channel");
assert(reply != NULL && !c->err);

while (kvGetReply(c, (void**)&reply) == KV_OK) {
    // Do something with the message...
    freeReplyObject(reply);
}
```

### Errors

As previously mentioned, when there is a communication error libkv will return `NULL` and set the `err` and `errstr` members with the nature of the problem. The specific error types are as follows.

- `KV_ERR_IO` - A problem with the connection.
- `KV_ERR_EOF` - The server closed the connection.
- `KV_ERR_PROTOCOL` - There was an error parsing the reply.
- `KV_ERR_TIMEOUT` - A connect, read, or write timeout.
- `KV_ERR_OOM` - Out of memory.
- `KV_ERR_OTHER` - Some other error (check `c->errstr` for details).

### Thread safety

Libkv context structs are **not** thread safe. You should not attempt to share them between threads, unless you really know what you're doing.

### Reader configuration

Libkv contexts have a few more mechanisms you can customize to your needs.

#### Maximum input buffer size

Libkv uses a buffer to hold incoming bytes, which is typically restored to the configurable max buffer size (`16KB`) when it is empty. To avoid continually reallocating this buffer you can set the value higher, or to zero which means "no limit".

```c
context->reader->maxbuf = 0;
```

#### Maximum array elements

By default, libkv will refuse to parse array-like replies if they have more than 2^32-1 or 4,294,967,295 elements. This value can be set to any arbitrary 64-bit value or zero which just means "no limit".

```c
context->reader->maxelements = 0;
```

#### RESP3 Push Replies

The `RESP` protocol introduced out-of-band "push" replies in the third version of the specification. These replies may come at any point in the data stream. By default, libkv will simply process these messages and discard them.

If your application needs to perform specific actions on PUSH messages you can install your own handler which will be called as they are received. It is also possible to set the push handler to NULL, in which case the messages will be delivered "in-band". This can be useful for example in a blocking subscribe loop.

**NOTE**: You may also specify a push handler in the `kvOptions` struct and set it on initialization .

#### Synchronous context

```c
void my_push_handler(void *privdata, void *reply) {
    // In a synchronous context, you are expected to free the reply after you're done with it.
}

// Initialization, etc.
kvSetPushCallback(c, my_push_handler);
```

#### Asynchronous context

```c
void my_async_push_handler(kvAsyncContext *ac, void *reply) {
    // As with other async replies, libkv will free it for you, unless you have
    // configured the context with `KV_OPT_NOAUTOFREE`.
}

// Initialization, etc
kvAsyncSetPushCallback(ac, my_async_push_handler);
```

#### Allocator injection

Internally libkv uses a layer of indirection from the standard allocation functions, by keeping a global structure with function pointers to the allocators we are going to use. By default they are just set to `malloc`, `calloc`, `realloc`, etc.

These can be overridden like so

```c
kvAllocFuncs my_allocators = {
    .mallocFn = my_malloc,
    .callocFn = my_calloc,
    .reallocFn = my_realloc,
    .strdupFn = my_strdup,
    .freeFn = my_free,
};

// libkv will return the previously set allocators.
kvAllocFuncs old = kvSetAllocators(&my_allocators);
```

They can also be reset to the glibc or musl defaults

```c
kvResetAllocators();
```

**NOTE**: The `vk_calloc` function handles the case where `nmemb` * `size` would overflow a `size_t` and returns `NULL` in that case.

## Asynchronous API

Libkv also has an asynchronous API which supports a great many different event libraries. See the [examples](../examples) directory for specific information about each individual event library.

### Connecting

Libkv provides an `kvAsyncContext` to manage asynchronous connections which works similarly to the synchronous context.

```c
kvAsyncContext *ac = kvAsyncConnect("localhost", 6379);
if (ac == NULL) {
    fprintf(stderr, "Error:  Out of memory trying to allocate kvAsyncContext\n");
    exit(1);
} else if (ac->err) {
    fprintf(stderr, "Error: %s (%d)\n", ac->errstr, ac->err);
    exit(1);
}

// If we're using libev
kvLibevAttach(EV_DEFAULT_ ac);

kvSetConnectCallback(ac, my_connect_callback);
kvSetDisconnectCallback(ac, my_disconnect_callback);

ev_run(EV_DEFAULT_ 0);
```

The asynchronous context _should_ hold a connect callback function that is called when the connection attempt completes, either successfully or with an error.

It _can_ also hold a disconnect callback function that is called when the connection is disconnected (either because of an error or per user request).
The context object is always freed after the disconnect callback fired.

### Executing commands

Executing commands in an asynchronous context work similarly to the synchronous context, except that you can pass a callback that will be invoked when the reply is received.

```c
struct my_app_data {
    size_t incrby_replies;
    size_t get_replies;
};

void my_incrby_callback(kvAsyncContext *ac, void *r, void *privdata) {
    struct my_app_data *data = privdata;
    kvReply *reply = r;

    assert(reply != NULL && reply->type == KV_REPLY_INTEGER);

    printf("Incremented value: %lld\n", reply->integer);
    data->incrby_replies++;
}

void my_get_callback(kvAsyncContext *ac, void *r, void *privdata) {
    struct my_app_data *data = privdata;
    kvReply *reply = r;

    assert(reply != NULL && reply->type == KV_REPLY_STRING);

    printf("Key value: %s\n", reply->str);
    data->get_replies++;
}

int exec_some_commands(struct my_app_data *data) {
    kvAsyncCommand(ac, my_incrby_callback, data, "INCRBY mykey %d", 42);
    kvAsyncCommand(ac, my_get_callback, data, "GET %s", "mykey");
}
```

### Disconnecting/cleanup

For a graceful disconnect use `kvAsyncDisconnect` which will block new commands from being issued.
The connection is only terminated when all pending commands have been sent, their respective replies have been read, and their respective callbacks have been executed.
After this, the disconnection callback is called with the status, and the context object is freed.

To terminate the connection forcefully use `kvAsyncFree` which also will block new commands from being issued.
There will be no more data sent on the socket and all pending callbacks will be called with a `NULL` reply.
After this, the disconnection callback is called with the `KV_OK` status, and the context object is freed.

## TLS support

TLS support is not enabled by default and requires an explicit build flag as described in [`README.md`](../README.md#building).

Libkv implements TLS on top of its `kvContext` and `kvAsyncContext`, so you will need to establish a connection first and then initiate a TLS handshake.
See the [examples](../examples) directory for how to create the TLS context and initiate the handshake.
