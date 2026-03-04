/* This module emulates a linked list for lazyfree testing of modules, which
 is a simplified version of 'hellotype.c'
 */
#include "kvmodule.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>

static KVModuleType *LazyFreeLinkType;

struct LazyFreeLinkNode {
    int64_t value;
    struct LazyFreeLinkNode *next;
};

struct LazyFreeLinkObject {
    struct LazyFreeLinkNode *head;
    size_t len; /* Number of elements added. */
};

struct LazyFreeLinkObject *createLazyFreeLinkObject(void) {
    struct LazyFreeLinkObject *o;
    o = KVModule_Alloc(sizeof(*o));
    o->head = NULL;
    o->len = 0;
    return o;
}

void LazyFreeLinkInsert(struct LazyFreeLinkObject *o, int64_t ele) {
    struct LazyFreeLinkNode *next = o->head, *newnode, *prev = NULL;

    while(next && next->value < ele) {
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

void LazyFreeLinkReleaseObject(struct LazyFreeLinkObject *o) {
    struct LazyFreeLinkNode *cur, *next;
    cur = o->head;
    while(cur) {
        next = cur->next;
        KVModule_Free(cur);
        cur = next;
    }
    KVModule_Free(o);
}

/* LAZYFREELINK.INSERT key value */
int LazyFreeLinkInsert_RedisCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVModule_AutoMemory(ctx); /* Use automatic memory management. */

    if (argc != 3) return KVModule_WrongArity(ctx);
    KVModuleKey *key = KVModule_OpenKey(ctx,argv[1],
        KVMODULE_READ|KVMODULE_WRITE);
    int type = KVModule_KeyType(key);
    if (type != KVMODULE_KEYTYPE_EMPTY &&
        KVModule_ModuleTypeGetType(key) != LazyFreeLinkType)
    {
        return KVModule_ReplyWithError(ctx,KVMODULE_ERRORMSG_WRONGTYPE);
    }

    long long value;
    if ((KVModule_StringToLongLong(argv[2],&value) != KVMODULE_OK)) {
        return KVModule_ReplyWithError(ctx,"ERR invalid value: must be a signed 64 bit integer");
    }

    struct LazyFreeLinkObject *hto;
    if (type == KVMODULE_KEYTYPE_EMPTY) {
        hto = createLazyFreeLinkObject();
        KVModule_ModuleTypeSetValue(key,LazyFreeLinkType,hto);
    } else {
        hto = KVModule_ModuleTypeGetValue(key);
    }

    LazyFreeLinkInsert(hto,value);
    KVModule_SignalKeyAsReady(ctx,argv[1]);

    KVModule_ReplyWithLongLong(ctx,hto->len);
    KVModule_ReplicateVerbatim(ctx);
    return KVMODULE_OK;
}

/* LAZYFREELINK.LEN key */
int LazyFreeLinkLen_RedisCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVModule_AutoMemory(ctx); /* Use automatic memory management. */

    if (argc != 2) return KVModule_WrongArity(ctx);
    KVModuleKey *key = KVModule_OpenKey(ctx,argv[1],
                                              KVMODULE_READ);
    int type = KVModule_KeyType(key);
    if (type != KVMODULE_KEYTYPE_EMPTY &&
        KVModule_ModuleTypeGetType(key) != LazyFreeLinkType)
    {
        return KVModule_ReplyWithError(ctx,KVMODULE_ERRORMSG_WRONGTYPE);
    }

    struct LazyFreeLinkObject *hto = KVModule_ModuleTypeGetValue(key);
    KVModule_ReplyWithLongLong(ctx,hto ? hto->len : 0);
    return KVMODULE_OK;
}

void *LazyFreeLinkRdbLoad(KVModuleIO *rdb, int encver) {
    if (encver != 0) {
        return NULL;
    }
    uint64_t elements = KVModule_LoadUnsigned(rdb);
    struct LazyFreeLinkObject *hto = createLazyFreeLinkObject();
    while(elements--) {
        int64_t ele = KVModule_LoadSigned(rdb);
        LazyFreeLinkInsert(hto,ele);
    }
    return hto;
}

void LazyFreeLinkRdbSave(KVModuleIO *rdb, void *value) {
    struct LazyFreeLinkObject *hto = value;
    struct LazyFreeLinkNode *node = hto->head;
    KVModule_SaveUnsigned(rdb,hto->len);
    while(node) {
        KVModule_SaveSigned(rdb,node->value);
        node = node->next;
    }
}

void LazyFreeLinkAofRewrite(KVModuleIO *aof, KVModuleString *key, void *value) {
    struct LazyFreeLinkObject *hto = value;
    struct LazyFreeLinkNode *node = hto->head;
    while(node) {
        KVModule_EmitAOF(aof,"LAZYFREELINK.INSERT","sl",key,node->value);
        node = node->next;
    }
}

void LazyFreeLinkFree(void *value) {
    LazyFreeLinkReleaseObject(value);
}

size_t LazyFreeLinkFreeEffort(KVModuleString *key, const void *value) {
    KVMODULE_NOT_USED(key);
    const struct LazyFreeLinkObject *hto = value;
    return hto->len;
}

void LazyFreeLinkUnlink(KVModuleString *key, const void *value) {
    KVMODULE_NOT_USED(key);
    KVMODULE_NOT_USED(value);
    /* Here you can know which key and value is about to be freed. */
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx,"lazyfreetest",1,KVMODULE_APIVER_1)
        == KVMODULE_ERR) return KVMODULE_ERR;

    /* We only allow our module to be loaded when the core version is greater than the version of my module */
    if (KVModule_GetTypeMethodVersion() < KVMODULE_TYPE_METHOD_VERSION) {
        return KVMODULE_ERR;
    }

    KVModuleTypeMethods tm = {
        .version = KVMODULE_TYPE_METHOD_VERSION,
        .rdb_load = LazyFreeLinkRdbLoad,
        .rdb_save = LazyFreeLinkRdbSave,
        .aof_rewrite = LazyFreeLinkAofRewrite,
        .free = LazyFreeLinkFree,
        .free_effort = LazyFreeLinkFreeEffort,
        .unlink = LazyFreeLinkUnlink,
    };

    LazyFreeLinkType = KVModule_CreateDataType(ctx,"test_lazy",0,&tm);
    if (LazyFreeLinkType == NULL) return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"lazyfreelink.insert",
        LazyFreeLinkInsert_RedisCommand,"write deny-oom",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"lazyfreelink.len",
        LazyFreeLinkLen_RedisCommand,"readonly",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}
