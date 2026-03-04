#include "kvmodule.h"
#include <string.h>
#include <assert.h>
#include <unistd.h>

#define UNUSED(V) ((void) V)

/* Registered type */
KVModuleType *mallocsize_type = NULL;

typedef enum {
    UDT_RAW,
    UDT_STRING,
    UDT_DICT
} udt_type_t;

typedef struct {
    void *ptr;
    size_t len;
} raw_t;

typedef struct {
    udt_type_t type;
    union {
        raw_t raw;
        KVModuleString *str;
        KVModuleDict *dict;
    } data;
} udt_t;

void udt_free(void *value) {
    udt_t *udt = value;
    switch (udt->type) {
        case (UDT_RAW): {
            KVModule_Free(udt->data.raw.ptr);
            break;
        }
        case (UDT_STRING): {
            KVModule_FreeString(NULL, udt->data.str);
            break;
        }
        case (UDT_DICT): {
            KVModuleString *dk, *dv;
            KVModuleDictIter *iter = KVModule_DictIteratorStartC(udt->data.dict, "^", NULL, 0);
            while((dk = KVModule_DictNext(NULL, iter, (void **)&dv)) != NULL) {
                KVModule_FreeString(NULL, dk);
                KVModule_FreeString(NULL, dv);
            }
            KVModule_DictIteratorStop(iter);
            KVModule_FreeDict(NULL, udt->data.dict);
            break;
        }
    }
    KVModule_Free(udt);
}

void udt_rdb_save(KVModuleIO *rdb, void *value) {
    udt_t *udt = value;
    KVModule_SaveUnsigned(rdb, udt->type);
    switch (udt->type) {
        case (UDT_RAW): {
            KVModule_SaveStringBuffer(rdb, udt->data.raw.ptr, udt->data.raw.len);
            break;
        }
        case (UDT_STRING): {
            KVModule_SaveString(rdb, udt->data.str);
            break;
        }
        case (UDT_DICT): {
            KVModule_SaveUnsigned(rdb, KVModule_DictSize(udt->data.dict));
            KVModuleString *dk, *dv;
            KVModuleDictIter *iter = KVModule_DictIteratorStartC(udt->data.dict, "^", NULL, 0);
            while((dk = KVModule_DictNext(NULL, iter, (void **)&dv)) != NULL) {
                KVModule_SaveString(rdb, dk);
                KVModule_SaveString(rdb, dv);
                KVModule_FreeString(NULL, dk); /* Allocated by KVModule_DictNext */
            }
            KVModule_DictIteratorStop(iter);
            break;
        }
    }
}

void *udt_rdb_load(KVModuleIO *rdb, int encver) {
    if (encver != 0)
        return NULL;
    udt_t *udt = KVModule_Alloc(sizeof(*udt));
    udt->type = KVModule_LoadUnsigned(rdb);
    switch (udt->type) {
        case (UDT_RAW): {
            udt->data.raw.ptr = KVModule_LoadStringBuffer(rdb, &udt->data.raw.len);
            break;
        }
        case (UDT_STRING): {
            udt->data.str = KVModule_LoadString(rdb);
            break;
        }
        case (UDT_DICT): {
            long long dict_len = KVModule_LoadUnsigned(rdb);
            udt->data.dict = KVModule_CreateDict(NULL);
            for (int i = 0; i < dict_len; i += 2) {
                KVModuleString *key = KVModule_LoadString(rdb);
                KVModuleString *val = KVModule_LoadString(rdb);
                KVModule_DictSet(udt->data.dict, key, val);
            }
            break;
        }
    }

    return udt;
}

size_t udt_mem_usage(KVModuleKeyOptCtx *ctx, const void *value, size_t sample_size) {
    UNUSED(ctx);
    UNUSED(sample_size);
    
    const udt_t *udt = value;
    size_t size = sizeof(*udt);
    
    switch (udt->type) {
        case (UDT_RAW): {
            size += KVModule_MallocSize(udt->data.raw.ptr);
            break;
        }
        case (UDT_STRING): {
            size += KVModule_MallocSizeString(udt->data.str);
            break;
        }
        case (UDT_DICT): {
            void *dk;
            size_t keylen;
            KVModuleString *dv;
            KVModuleDictIter *iter = KVModule_DictIteratorStartC(udt->data.dict, "^", NULL, 0);
            while((dk = KVModule_DictNextC(iter, &keylen, (void **)&dv)) != NULL) {
                size += keylen;
                size += KVModule_MallocSizeString(dv);
            }
            KVModule_DictIteratorStop(iter);
            break;
        }
    }
    
    return size;
}

/* MALLOCSIZE.SETRAW key len */
int cmd_setraw(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 3)
        return KVModule_WrongArity(ctx);
        
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_WRITE);

    udt_t *udt = KVModule_Alloc(sizeof(*udt));
    udt->type = UDT_RAW;
    
    long long raw_len;
    KVModule_StringToLongLong(argv[2], &raw_len);
    udt->data.raw.ptr = KVModule_Alloc(raw_len);
    udt->data.raw.len = raw_len;
    
    KVModule_ModuleTypeSetValue(key, mallocsize_type, udt);
    KVModule_CloseKey(key);

    return KVModule_ReplyWithSimpleString(ctx, "OK");
}

/* MALLOCSIZE.SETSTR key string */
int cmd_setstr(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 3)
        return KVModule_WrongArity(ctx);
        
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_WRITE);

    udt_t *udt = KVModule_Alloc(sizeof(*udt));
    udt->type = UDT_STRING;
    
    udt->data.str = argv[2];
    KVModule_RetainString(ctx, argv[2]);
    
    KVModule_ModuleTypeSetValue(key, mallocsize_type, udt);
    KVModule_CloseKey(key);

    return KVModule_ReplyWithSimpleString(ctx, "OK");
}

/* MALLOCSIZE.SETDICT key field value [field value ...] */
int cmd_setdict(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc < 4 || argc % 2)
        return KVModule_WrongArity(ctx);
        
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_WRITE);

    udt_t *udt = KVModule_Alloc(sizeof(*udt));
    udt->type = UDT_DICT;
    
    udt->data.dict = KVModule_CreateDict(ctx);
    for (int i = 2; i < argc; i += 2) {
        KVModule_DictSet(udt->data.dict, argv[i], argv[i+1]);
        /* No need to retain argv[i], it is copied as the rax key */
        KVModule_RetainString(ctx, argv[i+1]);   
    }
    
    KVModule_ModuleTypeSetValue(key, mallocsize_type, udt);
    KVModule_CloseKey(key);

    return KVModule_ReplyWithSimpleString(ctx, "OK");
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    if (KVModule_Init(ctx,"mallocsize",1,KVMODULE_APIVER_1)== KVMODULE_ERR)
        return KVMODULE_ERR;
        
    KVModuleTypeMethods tm = {
        .version = KVMODULE_TYPE_METHOD_VERSION,
        .rdb_load = udt_rdb_load,
        .rdb_save = udt_rdb_save,
        .free = udt_free,
        .mem_usage2 = udt_mem_usage,
    };

    mallocsize_type = KVModule_CreateDataType(ctx, "allocsize", 0, &tm);
    if (mallocsize_type == NULL)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "mallocsize.setraw", cmd_setraw, "", 1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;
        
    if (KVModule_CreateCommand(ctx, "mallocsize.setstr", cmd_setstr, "", 1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;
        
    if (KVModule_CreateCommand(ctx, "mallocsize.setdict", cmd_setdict, "", 1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}
