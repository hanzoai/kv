/*
 * Copyright (c) 2009-2011, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2014, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 * Copyright (c) 2015, Matt Stancliff <matt at genges dot com>,
 *                     Jan-Erik Rediger <janerik at fnordig dot com>
 *
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

#ifndef KV_KV_H
#define KV_KV_H
#include "read.h"
#include "visibility.h"

#include <stdarg.h> /* for va_list */
#ifndef _MSC_VER
#include <sys/time.h>  /* for struct timeval */
#include <sys/types.h> /* for ssize_t */
#else
#include <basetsd.h>
struct timeval; /* forward declaration */
typedef SSIZE_T ssize_t;
#endif
#include "alloc.h" /* for allocation wrappers */

#include <stdint.h> /* uintXX_t, etc */

#define LIBKV_VERSION_MAJOR 0
#define LIBKV_VERSION_MINOR 2
#define LIBKV_VERSION_PATCH 1

/* Connection type can be blocking or non-blocking and is set in the
 * least significant bit of the flags field in kvContext. */
#define KV_BLOCK 0x1

/* Connection may be disconnected before being free'd. The second bit
 * in the flags field is set when the context is connected. */
#define KV_CONNECTED 0x2

/* The async API might try to disconnect cleanly and flush the output
 * buffer and read all subsequent replies before disconnecting.
 * This flag means no new commands can come in and the connection
 * should be terminated once all replies have been read. */
#define KV_DISCONNECTING 0x4

/* Flag specific to the async API which means that the context should be clean
 * up as soon as possible. */
#define KV_FREEING 0x8

/* Flag that is set when an async callback is executed. */
#define KV_IN_CALLBACK 0x10

/* Flag that is set when the async context has one or more subscriptions. */
#define KV_SUBSCRIBED 0x20

/* Flag that is set when monitor mode is active */
#define KV_MONITORING 0x40

/* Flag that is set when we should set SO_REUSEADDR before calling bind() */
#define KV_REUSEADDR 0x80

/* Flag that is set when the async connection supports push replies. */
#define KV_SUPPORTS_PUSH 0x100

/**
 * Flag that indicates the user does not want the context to
 * be automatically freed upon error
 */
#define KV_NO_AUTO_FREE 0x200

/* Flag that indicates the user does not want replies to be automatically freed */
#define KV_NO_AUTO_FREE_REPLIES 0x400

/* Flags to prefer IPv6 or IPv4 when doing DNS lookup. (If both are set,
 * AF_UNSPEC is used.) */
#define KV_PREFER_IPV4 0x800
#define KV_PREFER_IPV6 0x1000

/* Flag specific to use Multipath TCP (MPTCP) */
#define KV_MPTCP 0x2000

#define KV_KEEPALIVE_INTERVAL 15 /* seconds */

/* number of times we retry to connect in the case of EADDRNOTAVAIL and
 * SO_REUSEADDR is being used. */
#define KV_CONNECT_RETRIES 10

/* Forward declarations for structs defined elsewhere */
struct kvAsyncContext;
struct kvContext;

/* RESP3 push helpers and callback prototypes */
#define kvIsPushReply(r) (((kvReply *)(r))->type == KV_REPLY_PUSH)
typedef void(kvPushFn)(void *, void *);
typedef void(kvAsyncPushFn)(struct kvAsyncContext *, void *);

#ifdef __cplusplus
extern "C" {
#endif

/* This is the reply object returned by kvCommand() */
typedef struct kvReply {
    int type;                     /* KV_REPLY_* */
    long long integer;            /* The integer when type is KV_REPLY_INTEGER */
    double dval;                  /* The double when type is KV_REPLY_DOUBLE */
    size_t len;                   /* Length of string */
    char *str;                    /* Used for KV_REPLY_ERROR, KV_REPLY_STRING
                                   * KV_REPLY_VERB,
                                   * KV_REPLY_DOUBLE (in additional to dval),
                                   * and KV_REPLY_BIGNUM. */
    char vtype[4];                /* Used for KV_REPLY_VERB, contains the null
                                   * terminated 3 character content type,
                                   * such as "txt". */
    size_t elements;              /* number of elements, for KV_REPLY_ARRAY */
    struct kvReply **element; /* elements vector for KV_REPLY_ARRAY */
} kvReply;

LIBKV_API kvReader *kvReaderCreate(void);

/* Function to free the reply objects hikv returns by default. */
LIBKV_API void freeReplyObject(void *reply);

/* Functions to format a command according to the protocol. */
LIBKV_API int kvvFormatCommand(char **target, const char *format, va_list ap);
LIBKV_API int kvFormatCommand(char **target, const char *format, ...);
LIBKV_API long long kvFormatCommandArgv(char **target, int argc, const char **argv, const size_t *argvlen);
LIBKV_API void kvFreeCommand(char *cmd);

enum kvConnectionType {
    KV_CONN_TCP,
    KV_CONN_UNIX,
    KV_CONN_USERFD,
    KV_CONN_RDMA, /* experimental, may be removed in any version */

    KV_CONN_MAX
};

#define KV_OPT_NONBLOCK 0x01
#define KV_OPT_REUSEADDR 0x02
#define KV_OPT_NOAUTOFREE 0x04        /* Don't automatically free the async
                                          * object on a connection failure, or
                                          * other implicit conditions. Only free
                                          * on an explicit call to disconnect()
                                          * or free() */
#define KV_OPT_NO_PUSH_AUTOFREE 0x08  /* Don't automatically intercept and
                                          * free RESP3 PUSH replies. */
#define KV_OPT_NOAUTOFREEREPLIES 0x10 /* Don't automatically free replies. */
#define KV_OPT_PREFER_IPV4 0x20       /* Prefer IPv4 in DNS lookups. */
#define KV_OPT_PREFER_IPV6 0x40       /* Prefer IPv6 in DNS lookups. */
#define KV_OPT_PREFER_IP_UNSPEC (KV_OPT_PREFER_IPV4 | KV_OPT_PREFER_IPV6)
#define KV_OPT_MPTCP 0x80
#define KV_OPT_LAST_SA_OPTION 0x80 /* Last defined standalone option. */

/* In Unix systems a file descriptor is a regular signed int, with -1
 * representing an invalid descriptor. In Windows it is a SOCKET
 * (32- or 64-bit unsigned integer depending on the architecture), where
 * all bits set (~0) is INVALID_SOCKET.  */
#ifndef _WIN32
typedef int kvFD;
#define KV_INVALID_FD -1
#else
#ifdef _WIN64
typedef unsigned long long kvFD; /* SOCKET = 64-bit UINT_PTR */
#else
typedef unsigned long kvFD; /* SOCKET = 32-bit UINT_PTR */
#endif
#define KV_INVALID_FD ((kvFD)(~0)) /* INVALID_SOCKET */
#endif

typedef struct {
    /*
     * the type of connection to use. This also indicates which
     * `endpoint` member field to use
     */
    int type;
    /* bit field of KV_OPT_xxx */
    int options;
    /* timeout value for connect operation. If NULL, no timeout is used */
    const struct timeval *connect_timeout;
    /* timeout value for commands. If NULL, no timeout is used.  This can be
     * updated at runtime with kvSetTimeout/kvAsyncSetTimeout. */
    const struct timeval *command_timeout;
    union {
        /** use this field for tcp/ip connections */
        struct {
            const char *source_addr;
            const char *ip;
            int port;
        } tcp;
        /** use this field for unix domain sockets */
        const char *unix_socket;
        /**
         * use this field to have libkv operate an already-open
         * file descriptor */
        kvFD fd;
    } endpoint;

    /* Optional user defined data/destructor */
    void *privdata;
    void (*free_privdata)(void *);

    /* A user defined PUSH message callback */
    kvPushFn *push_cb;
    kvAsyncPushFn *async_push_cb;
} kvOptions;

/**
 * Helper macros to initialize options to their specified fields.
 */
#define KV_OPTIONS_SET_TCP(opts, ip_, port_) \
    do {                                         \
        (opts)->type = KV_CONN_TCP;          \
        (opts)->endpoint.tcp.ip = ip_;           \
        (opts)->endpoint.tcp.port = port_;       \
    } while (0)

#define KV_OPTIONS_SET_MPTCP(opts, ip_, port_) \
    do {                                           \
        (opts)->type = KV_CONN_TCP;            \
        (opts)->endpoint.tcp.ip = ip_;             \
        (opts)->endpoint.tcp.port = port_;         \
        (opts)->options |= KV_OPT_MPTCP;       \
    } while (0)

#define KV_OPTIONS_SET_UNIX(opts, path)  \
    do {                                     \
        (opts)->type = KV_CONN_UNIX;     \
        (opts)->endpoint.unix_socket = path; \
    } while (0)

#define KV_OPTIONS_SET_PRIVDATA(opts, data, dtor) \
    do {                                              \
        (opts)->privdata = data;                      \
        (opts)->free_privdata = dtor;                 \
    } while (0)

typedef struct kvContextFuncs {
    int (*connect)(struct kvContext *, const kvOptions *);
    void (*close)(struct kvContext *);
    void (*free_privctx)(void *);
    void (*async_read)(struct kvAsyncContext *);
    void (*async_write)(struct kvAsyncContext *);

    /* Read/Write data to the underlying communication stream, returning the
     * number of bytes read/written.  In the event of an unrecoverable error
     * these functions shall return a value < 0.  In the event of a
     * recoverable error, they should return 0. */
    ssize_t (*read)(struct kvContext *, char *, size_t);
    ssize_t (*write)(struct kvContext *);
    int (*set_timeout)(struct kvContext *, const struct timeval);
} kvContextFuncs;

/* Context for a connection to KV */
typedef struct kvContext {
    const kvContextFuncs *funcs; /* Function table */

    int err;          /* Error flags, 0 when there is no error */
    char errstr[128]; /* String representation of error when applicable */
    kvFD fd;
    int flags;
    char *obuf;           /* Write buffer */
    kvReader *reader; /* Protocol reader */

    enum kvConnectionType connection_type;
    struct timeval *connect_timeout;
    struct timeval *command_timeout;

    struct {
        char *host;
        char *source_addr;
        int port;
    } tcp;

    struct {
        char *path;
    } unix_sock;

    /* For non-blocking connect */
    struct sockaddr *saddr;
    size_t addrlen;

    /* Optional data and corresponding destructor users can use to provide
     * context to a given kvContext.  Not used by libkv. */
    void *privdata;
    void (*free_privdata)(void *);

    /* Internal context pointer presently used by libkv to manage
     * TLS connections. */
    void *privctx;

    /* An optional RESP3 PUSH handler */
    kvPushFn *push_cb;
} kvContext;

LIBKV_API kvContext *kvConnectWithOptions(const kvOptions *options);
LIBKV_API kvContext *kvConnect(const char *ip, int port);
LIBKV_API kvContext *kvConnectWithTimeout(const char *ip, int port, const struct timeval tv);
LIBKV_API kvContext *kvConnectNonBlock(const char *ip, int port);
LIBKV_API kvContext *kvConnectBindNonBlock(const char *ip, int port,
                                                       const char *source_addr);
LIBKV_API kvContext *kvConnectBindNonBlockWithReuse(const char *ip, int port,
                                                                const char *source_addr);
LIBKV_API kvContext *kvConnectUnix(const char *path);
LIBKV_API kvContext *kvConnectUnixWithTimeout(const char *path, const struct timeval tv);
LIBKV_API kvContext *kvConnectUnixNonBlock(const char *path);
LIBKV_API kvContext *kvConnectFd(kvFD fd);

/**
 * Reconnect the given context using the saved information.
 *
 * This re-uses the exact same connect options as in the initial connection.
 * host, ip (or path), timeout and bind address are reused,
 * flags are used unmodified from the existing context.
 *
 * Returns KV_OK on successful connect or KV_ERR otherwise.
 */
LIBKV_API int kvReconnect(kvContext *c);

LIBKV_API kvPushFn *kvSetPushCallback(kvContext *c, kvPushFn *fn);
LIBKV_API int kvSetTimeout(kvContext *c, const struct timeval tv);

/* Configurations using socket options. Applied directly to the underlying
 * socket and not automatically applied after a reconnect. */
LIBKV_API int kvEnableKeepAlive(kvContext *c);
LIBKV_API int kvEnableKeepAliveWithInterval(kvContext *c, int interval);
LIBKV_API int kvSetTcpUserTimeout(kvContext *c, unsigned int timeout);

LIBKV_API void kvFree(kvContext *c);
LIBKV_API kvFD kvFreeKeepFd(kvContext *c);
LIBKV_API int kvBufferRead(kvContext *c);
LIBKV_API int kvBufferWrite(kvContext *c, int *done);

/* In a blocking context, this function first checks if there are unconsumed
 * replies to return and returns one if so. Otherwise, it flushes the output
 * buffer to the socket and reads until it has a reply. In a non-blocking
 * context, it will return unconsumed replies until there are no more. */
LIBKV_API int kvGetReply(kvContext *c, void **reply);
LIBKV_API int kvGetReplyFromReader(kvContext *c, void **reply);

/* Write a formatted command to the output buffer. Use these functions in blocking mode
 * to get a pipeline of commands. */
LIBKV_API int kvAppendFormattedCommand(kvContext *c, const char *cmd, size_t len);

/* Write a command to the output buffer. Use these functions in blocking mode
 * to get a pipeline of commands. */
LIBKV_API int kvvAppendCommand(kvContext *c, const char *format, va_list ap);
LIBKV_API int kvAppendCommand(kvContext *c, const char *format, ...);
LIBKV_API int kvAppendCommandArgv(kvContext *c, int argc, const char **argv, const size_t *argvlen);

/* Issue a command to KV. In a blocking context, it is identical to calling
 * kvAppendCommand, followed by kvGetReply. The function will return
 * NULL if there was an error in performing the request; otherwise, it will
 * return the reply. In a non-blocking context, it is identical to calling
 * only kvAppendCommand and will always return NULL. */
LIBKV_API void *kvvCommand(kvContext *c, const char *format, va_list ap);
LIBKV_API void *kvCommand(kvContext *c, const char *format, ...);
LIBKV_API void *kvCommandArgv(kvContext *c, int argc, const char **argv, const size_t *argvlen);

#ifdef __cplusplus
}
#endif

#endif /* KV_KV_H */
