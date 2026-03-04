/*
 * Copyright (c) 2009-2011, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2011, Pieter Noordhuis <pcnoordhuis at gmail dot com>
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

#ifndef KV_READ_H
#define KV_READ_H
#include "visibility.h"

#include <stdio.h> /* for size_t */

#define KV_ERR -1
#define KV_OK 0

/* When an error occurs, the err flag in a context is set to hold the type of
 * error that occurred. KV_ERR_IO means there was an I/O error and you
 * should use the "errno" variable to find out what is wrong.
 * For other values, the "errstr" field will hold a description. */
#define KV_ERR_IO 1       /* Error in read or write */
#define KV_ERR_EOF 3      /* End of file */
#define KV_ERR_PROTOCOL 4 /* Protocol error */
#define KV_ERR_OOM 5      /* Out of memory */
#define KV_ERR_TIMEOUT 6  /* Timed out */
#define KV_ERR_OTHER 2    /* Everything else... */

#define KV_REPLY_STRING 1
#define KV_REPLY_ARRAY 2
#define KV_REPLY_INTEGER 3
#define KV_REPLY_NIL 4
#define KV_REPLY_STATUS 5
#define KV_REPLY_ERROR 6
#define KV_REPLY_DOUBLE 7
#define KV_REPLY_BOOL 8
#define KV_REPLY_MAP 9
#define KV_REPLY_SET 10
#define KV_REPLY_ATTR 11
#define KV_REPLY_PUSH 12
#define KV_REPLY_BIGNUM 13
#define KV_REPLY_VERB 14

/* Default max unused reader buffer. */
#define KV_READER_MAX_BUF (1024 * 16)

/* Default multi-bulk element limit */
#define KV_READER_MAX_ARRAY_ELEMENTS ((1LL << 32) - 1)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kvReadTask {
    int type;
    long long elements;            /* number of elements in multibulk container */
    int idx;                       /* index in parent (array) object */
    void *obj;                     /* holds user-generated value for a read task */
    struct kvReadTask *parent; /* parent task */
    void *privdata;                /* user-settable arbitrary field */
} kvReadTask;

typedef struct kvReplyObjectFunctions {
    void *(*createString)(const kvReadTask *, char *, size_t);
    void *(*createArray)(const kvReadTask *, size_t);
    void *(*createInteger)(const kvReadTask *, long long);
    void *(*createDouble)(const kvReadTask *, double, char *, size_t);
    void *(*createNil)(const kvReadTask *);
    void *(*createBool)(const kvReadTask *, int);
    void (*freeObject)(void *);
} kvReplyObjectFunctions;

typedef struct kvReader {
    int err;          /* Error flags, 0 when there is no error */
    char errstr[128]; /* String representation of error when applicable */

    char *buf;             /* Read buffer */
    size_t pos;            /* Buffer cursor */
    size_t len;            /* Buffer length */
    size_t maxbuf;         /* Max length of unused buffer */
    long long maxelements; /* Max multi-bulk elements */

    kvReadTask **task;
    int tasks;

    int ridx;    /* Index of current read task */
    void *reply; /* Temporary reply pointer */

    kvReplyObjectFunctions *fn;
    void *privdata;
} kvReader;

/* Public API for the protocol parser. */
LIBKV_API kvReader *kvReaderCreateWithFunctions(kvReplyObjectFunctions *fn);
LIBKV_API void kvReaderFree(kvReader *r);
LIBKV_API int kvReaderFeed(kvReader *r, const char *buf, size_t len);
LIBKV_API int kvReaderGetReply(kvReader *r, void **reply);

#define kvReaderSetPrivdata(_r, _p) (int)(((kvReader *)(_r))->privdata = (_p))
#define kvReaderGetObject(_r) (((kvReader *)(_r))->reply)
#define kvReaderGetError(_r) (((kvReader *)(_r))->errstr)

#ifdef __cplusplus
}
#endif

#endif /* KV_READ_H */
