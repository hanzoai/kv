/*
 * Copyright (c) 2024, zhenwei pi <pizhenwei@bytedance.com>
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
 *   * Neither the name of the copyright holder nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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

#ifndef KV_VK_PRIVATE_H
#define KV_VK_PRIVATE_H

#include "win32.h"

#include "kv.h"
#include "visibility.h"

#include <sds.h>

#include <limits.h>
#include <string.h>

LIBKV_API void kvSetError(kvContext *c, int type, const char *str);

/* Helper function. Convert struct timeval to millisecond. */
static inline int kvContextTimeoutMsec(const struct timeval *timeout, long *result) {
    long max_msec = (LONG_MAX - 999) / 1000;
    long msec = INT_MAX;

    /* Only use timeout when not NULL. */
    if (timeout != NULL) {
        if (timeout->tv_usec > 1000000 || timeout->tv_sec > max_msec) {
            *result = msec;
            return KV_ERR;
        }

        msec = (timeout->tv_sec * 1000) + ((timeout->tv_usec + 999) / 1000);

        if (msec < 0 || msec > INT_MAX) {
            msec = INT_MAX;
        }
    }

    *result = msec;
    return KV_OK;
}

/* Get connect timeout of kvContext */
static inline int kvConnectTimeoutMsec(kvContext *c, long *result) {
    const struct timeval *timeout = c->connect_timeout;
    int ret = kvContextTimeoutMsec(timeout, result);

    if (ret != KV_OK) {
        kvSetError(c, KV_ERR_IO, "Invalid timeout specified");
    }

    return ret;
}

/* Get command timeout of kvContext */
static inline int kvCommandTimeoutMsec(kvContext *c, long *result) {
    const struct timeval *timeout = c->command_timeout;
    int ret = kvContextTimeoutMsec(timeout, result);

    if (ret != KV_OK) {
        kvSetError(c, KV_ERR_IO, "Invalid timeout specified");
    }

    return ret;
}

static inline int kvContextUpdateConnectTimeout(kvContext *c,
                                                    const struct timeval *timeout) {
    /* Same timeval struct, short circuit */
    if (c->connect_timeout == timeout)
        return KV_OK;

    /* Allocate context timeval if we need to */
    if (c->connect_timeout == NULL) {
        c->connect_timeout = vk_malloc(sizeof(*c->connect_timeout));
        if (c->connect_timeout == NULL)
            return KV_ERR;
    }

    memcpy(c->connect_timeout, timeout, sizeof(*c->connect_timeout));
    return KV_OK;
}

static inline int kvContextUpdateCommandTimeout(kvContext *c,
                                                    const struct timeval *timeout) {
    /* Same timeval struct, short circuit */
    if (c->command_timeout == timeout)
        return KV_OK;

    /* Allocate context timeval if we need to */
    if (c->command_timeout == NULL) {
        c->command_timeout = vk_malloc(sizeof(*c->command_timeout));
        if (c->command_timeout == NULL)
            return KV_ERR;
    }

    memcpy(c->command_timeout, timeout, sizeof(*c->command_timeout));
    return KV_OK;
}

int kvContextRegisterFuncs(kvContextFuncs *funcs, enum kvConnectionType type);
void kvContextRegisterTcpFuncs(void);
void kvContextRegisterUnixFuncs(void);
void kvContextRegisterUserfdFuncs(void);

void kvContextSetFuncs(kvContext *c);

long long kvFormatSdsCommandArgv(sds *target, int argc, const char **argv, const size_t *argvlen);

#endif /* KV_VK_PRIVATE_H */
