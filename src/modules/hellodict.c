/* Hellodict -- An example of modules dictionary API
 *
 * This module implements a volatile key-value store on top of the
 * dictionary exported by the modules API.
 *
 * -----------------------------------------------------------------------------
 *
 * Copyright (c) 2018, Redis Ltd.
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

#include "../kvmodule.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

static KVModuleDict *Keyspace;

/* HELLODICT.SET <key> <value>
 *
 * Set the specified key to the specified value. */
int cmd_SET(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 3) return KVModule_WrongArity(ctx);
    KVModule_DictSet(Keyspace, argv[1], argv[2]);
    /* We need to keep a reference to the value stored at the key, otherwise
     * it would be freed when this callback returns. */
    KVModule_RetainString(NULL, argv[2]);
    return KVModule_ReplyWithSimpleString(ctx, "OK");
}

/* HELLODICT.GET <key>
 *
 * Return the value of the specified key, or a null reply if the key
 * is not defined. */
int cmd_GET(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) return KVModule_WrongArity(ctx);
    KVModuleString *val = KVModule_DictGet(Keyspace, argv[1], NULL);
    if (val == NULL) {
        return KVModule_ReplyWithNull(ctx);
    } else {
        return KVModule_ReplyWithString(ctx, val);
    }
}

/* HELLODICT.KEYRANGE <startkey> <endkey> <count>
 *
 * Return a list of matching keys, lexicographically between startkey
 * and endkey. No more than 'count' items are emitted. */
int cmd_KEYRANGE(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 4) return KVModule_WrongArity(ctx);

    /* Parse the count argument. */
    long long count;
    if (KVModule_StringToLongLong(argv[3], &count) != KVMODULE_OK) {
        return KVModule_ReplyWithError(ctx, "ERR invalid count");
    }

    /* Seek the iterator. */
    KVModuleDictIter *iter = KVModule_DictIteratorStart(Keyspace, ">=", argv[1]);

    /* Reply with the matching items. */
    char *key;
    size_t keylen;
    long long replylen = 0; /* Keep track of the emitted array len. */
    KVModule_ReplyWithArray(ctx, KVMODULE_POSTPONED_LEN);
    while ((key = KVModule_DictNextC(iter, &keylen, NULL)) != NULL) {
        if (replylen >= count) break;
        if (KVModule_DictCompare(iter, "<=", argv[2]) == KVMODULE_ERR) break;
        KVModule_ReplyWithStringBuffer(ctx, key, keylen);
        replylen++;
    }
    KVModule_ReplySetArrayLength(ctx, replylen);

    /* Cleanup. */
    KVModule_DictIteratorStop(iter);
    return KVMODULE_OK;
}

/* This function must be present on each module. It is used in order to
 * register the commands into the server. */
int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx, "hellodict", 1, KVMODULE_APIVER_1) == KVMODULE_ERR) return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hellodict.set", cmd_SET, "write deny-oom", 1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hellodict.get", cmd_GET, "readonly", 1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hellodict.keyrange", cmd_KEYRANGE, "readonly", 1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    /* Create our global dictionary. Here we'll set our keys and values. */
    Keyspace = KVModule_CreateDict(NULL);

    return KVMODULE_OK;
}
