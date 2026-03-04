/* This file implements a new module native data type called "HELLOTYPE".
 * The data structure implemented is a very simple ordered linked list of
 * 64 bit integers, in order to have something that is real world enough, but
 * at the same time, extremely simple to understand, to show how the API
 * works, how a new data type is created, and how to write basic methods
 * for RDB loading, saving and AOF rewriting.
 *
 * -----------------------------------------------------------------------------
 *
 * Copyright (c) 2016, Redis Ltd.
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
#include <stdint.h>

static KVModuleType *HelloType;

/* ========================== Internal data structure  =======================
 * This is just a linked list of 64 bit integers where elements are inserted
 * in-place, so it's ordered. There is no pop/push operation but just insert
 * because it is enough to show the implementation of new data types without
 * making things complex. */

struct HelloTypeNode {
    int64_t value;
    struct HelloTypeNode *next;
};

struct HelloTypeObject {
    struct HelloTypeNode *head;
    size_t len; /* Number of elements added. */
};

struct HelloTypeObject *createHelloTypeObject(void) {
    struct HelloTypeObject *o;
    o = KVModule_Alloc(sizeof(*o));
    o->head = NULL;
    o->len = 0;
    return o;
}

void HelloTypeInsert(struct HelloTypeObject *o, int64_t ele) {
    struct HelloTypeNode *next = o->head, *newnode, *prev = NULL;

    while (next && next->value < ele) {
        prev = next;
        next = next->next;
    }
    newnode = KVModule_Alloc(sizeof(*newnode));
    newnode->value = ele;
    newnode->next = next;
    if (prev) {
        prev->next = newnode;
    } else {
        o->head = newnode;
    }
    o->len++;
}

void HelloTypeReleaseObject(struct HelloTypeObject *o) {
    struct HelloTypeNode *cur, *next;
    cur = o->head;
    while (cur) {
        next = cur->next;
        KVModule_Free(cur);
        cur = next;
    }
    KVModule_Free(o);
}

/* ========================= "hellotype" type commands ======================= */

/* HELLOTYPE.INSERT key value */
int HelloTypeInsert_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVModule_AutoMemory(ctx); /* Use automatic memory management. */

    if (argc != 3) return KVModule_WrongArity(ctx);
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_READ | KVMODULE_WRITE);
    int type = KVModule_KeyType(key);
    if (type != KVMODULE_KEYTYPE_EMPTY && KVModule_ModuleTypeGetType(key) != HelloType) {
        return KVModule_ReplyWithError(ctx, KVMODULE_ERRORMSG_WRONGTYPE);
    }

    long long value;
    if ((KVModule_StringToLongLong(argv[2], &value) != KVMODULE_OK)) {
        return KVModule_ReplyWithError(ctx, "ERR invalid value: must be a signed 64 bit integer");
    }

    /* Create an empty value object if the key is currently empty. */
    struct HelloTypeObject *hto;
    if (type == KVMODULE_KEYTYPE_EMPTY) {
        hto = createHelloTypeObject();
        KVModule_ModuleTypeSetValue(key, HelloType, hto);
    } else {
        hto = KVModule_ModuleTypeGetValue(key);
    }

    /* Insert the new element. */
    HelloTypeInsert(hto, value);
    KVModule_SignalKeyAsReady(ctx, argv[1]);

    KVModule_ReplyWithLongLong(ctx, hto->len);
    KVModule_ReplicateVerbatim(ctx);
    return KVMODULE_OK;
}

/* HELLOTYPE.RANGE key first count */
int HelloTypeRange_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVModule_AutoMemory(ctx); /* Use automatic memory management. */

    if (argc != 4) return KVModule_WrongArity(ctx);
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_READ | KVMODULE_WRITE);
    int type = KVModule_KeyType(key);
    if (type != KVMODULE_KEYTYPE_EMPTY && KVModule_ModuleTypeGetType(key) != HelloType) {
        return KVModule_ReplyWithError(ctx, KVMODULE_ERRORMSG_WRONGTYPE);
    }

    long long first, count;
    if (KVModule_StringToLongLong(argv[2], &first) != KVMODULE_OK ||
        KVModule_StringToLongLong(argv[3], &count) != KVMODULE_OK || first < 0 || count < 0) {
        return KVModule_ReplyWithError(ctx, "ERR invalid first or count parameters");
    }

    struct HelloTypeObject *hto = KVModule_ModuleTypeGetValue(key);
    struct HelloTypeNode *node = hto ? hto->head : NULL;
    KVModule_ReplyWithArray(ctx, KVMODULE_POSTPONED_LEN);
    long long arraylen = 0;
    while (node && count--) {
        KVModule_ReplyWithLongLong(ctx, node->value);
        arraylen++;
        node = node->next;
    }
    KVModule_ReplySetArrayLength(ctx, arraylen);
    return KVMODULE_OK;
}

/* HELLOTYPE.LEN key */
int HelloTypeLen_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVModule_AutoMemory(ctx); /* Use automatic memory management. */

    if (argc != 2) return KVModule_WrongArity(ctx);
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_READ | KVMODULE_WRITE);
    int type = KVModule_KeyType(key);
    if (type != KVMODULE_KEYTYPE_EMPTY && KVModule_ModuleTypeGetType(key) != HelloType) {
        return KVModule_ReplyWithError(ctx, KVMODULE_ERRORMSG_WRONGTYPE);
    }

    struct HelloTypeObject *hto = KVModule_ModuleTypeGetValue(key);
    KVModule_ReplyWithLongLong(ctx, hto ? hto->len : 0);
    return KVMODULE_OK;
}

/* ====================== Example of a blocking command ==================== */

/* Reply callback for blocking command HELLOTYPE.BRANGE, this will get
 * called when the key we blocked for is ready: we need to check if we
 * can really serve the client, and reply OK or ERR accordingly. */
int HelloBlock_Reply(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModuleString *keyname = KVModule_GetBlockedClientReadyKey(ctx);
    KVModuleKey *key = KVModule_OpenKey(ctx, keyname, KVMODULE_READ);
    int type = KVModule_KeyType(key);
    if (type != KVMODULE_KEYTYPE_MODULE || KVModule_ModuleTypeGetType(key) != HelloType) {
        KVModule_CloseKey(key);
        return KVMODULE_ERR;
    }

    /* In case the key is able to serve our blocked client, let's directly
     * use our original command implementation to make this example simpler. */
    KVModule_CloseKey(key);
    return HelloTypeRange_KVCommand(ctx, argv, argc - 1);
}

/* Timeout callback for blocking command HELLOTYPE.BRANGE */
int HelloBlock_Timeout(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    return KVModule_ReplyWithSimpleString(ctx, "Request timedout");
}

/* Private data freeing callback for HELLOTYPE.BRANGE command. */
void HelloBlock_FreeData(KVModuleCtx *ctx, void *privdata) {
    KVMODULE_NOT_USED(ctx);
    KVModule_Free(privdata);
}

/* HELLOTYPE.BRANGE key first count timeout -- This is a blocking version of
 * the RANGE operation, in order to show how to use the API
 * KVModule_BlockClientOnKeys(). */
int HelloTypeBRange_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 5) return KVModule_WrongArity(ctx);
    KVModule_AutoMemory(ctx); /* Use automatic memory management. */
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_READ | KVMODULE_WRITE);
    int type = KVModule_KeyType(key);
    if (type != KVMODULE_KEYTYPE_EMPTY && KVModule_ModuleTypeGetType(key) != HelloType) {
        return KVModule_ReplyWithError(ctx, KVMODULE_ERRORMSG_WRONGTYPE);
    }

    /* Parse the timeout before even trying to serve the client synchronously,
     * so that we always fail ASAP on syntax errors. */
    long long timeout;
    if (KVModule_StringToLongLong(argv[4], &timeout) != KVMODULE_OK) {
        return KVModule_ReplyWithError(ctx, "ERR invalid timeout parameter");
    }

    /* Can we serve the reply synchronously? */
    if (type != KVMODULE_KEYTYPE_EMPTY) {
        return HelloTypeRange_KVCommand(ctx, argv, argc - 1);
    }

    /* Otherwise let's block on the key. */
    void *privdata = KVModule_Alloc(100);
    KVModule_BlockClientOnKeys(ctx, HelloBlock_Reply, HelloBlock_Timeout, HelloBlock_FreeData, timeout, argv + 1, 1,
                                   privdata);
    return KVMODULE_OK;
}

/* ========================== "hellotype" type methods ======================= */

void *HelloTypeRdbLoad(KVModuleIO *rdb, int encver) {
    if (encver != 0) {
        /* KVModule_Log("warning","Can't load data with version %d", encver);*/
        return NULL;
    }
    uint64_t elements = KVModule_LoadUnsigned(rdb);
    struct HelloTypeObject *hto = createHelloTypeObject();
    while (elements--) {
        int64_t ele = KVModule_LoadSigned(rdb);
        HelloTypeInsert(hto, ele);
    }
    return hto;
}

void HelloTypeRdbSave(KVModuleIO *rdb, void *value) {
    struct HelloTypeObject *hto = value;
    struct HelloTypeNode *node = hto->head;
    KVModule_SaveUnsigned(rdb, hto->len);
    while (node) {
        KVModule_SaveSigned(rdb, node->value);
        node = node->next;
    }
}

void HelloTypeAofRewrite(KVModuleIO *aof, KVModuleString *key, void *value) {
    struct HelloTypeObject *hto = value;
    struct HelloTypeNode *node = hto->head;
    while (node) {
        KVModule_EmitAOF(aof, "HELLOTYPE.INSERT", "sl", key, node->value);
        node = node->next;
    }
}

/* The goal of this function is to return the amount of memory used by
 * the HelloType value. */
size_t HelloTypeMemUsage(const void *value) {
    const struct HelloTypeObject *hto = value;
    struct HelloTypeNode *node = hto->head;
    return sizeof(*hto) + sizeof(*node) * hto->len;
}

void HelloTypeFree(void *value) {
    HelloTypeReleaseObject(value);
}

void HelloTypeDigest(KVModuleDigest *md, void *value) {
    struct HelloTypeObject *hto = value;
    struct HelloTypeNode *node = hto->head;
    while (node) {
        KVModule_DigestAddLongLong(md, node->value);
        node = node->next;
    }
    KVModule_DigestEndSequence(md);
}

/* This function must be present on each module. It is used in order to
 * register the commands into the server. */
int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx, "hellotype", 1, KVMODULE_APIVER_1) == KVMODULE_ERR) return KVMODULE_ERR;

    KVModuleTypeMethods tm = {.version = KVMODULE_TYPE_METHOD_VERSION,
                                  .rdb_load = HelloTypeRdbLoad,
                                  .rdb_save = HelloTypeRdbSave,
                                  .aof_rewrite = HelloTypeAofRewrite,
                                  .mem_usage = HelloTypeMemUsage,
                                  .free = HelloTypeFree,
                                  .digest = HelloTypeDigest};

    HelloType = KVModule_CreateDataType(ctx, "hellotype", 0, &tm);
    if (HelloType == NULL) return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hellotype.insert", HelloTypeInsert_KVCommand, "write deny-oom", 1, 1, 1) ==
        KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hellotype.range", HelloTypeRange_KVCommand, "readonly", 1, 1, 1) ==
        KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hellotype.len", HelloTypeLen_KVCommand, "readonly", 1, 1, 1) ==
        KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hellotype.brange", HelloTypeBRange_KVCommand, "readonly", 1, 1, 1) ==
        KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}
