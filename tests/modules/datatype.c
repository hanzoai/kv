/* This module current tests a small subset but should be extended in the future
 * for general ModuleDataType coverage.
 */

/* define macros for having usleep */
#define _BSD_SOURCE
#define _DEFAULT_SOURCE
#include <unistd.h>

#include "kvmodule.h"

static KVModuleType *datatype = NULL;
static int load_encver = 0;

/* used to test processing events during slow loading */
static volatile int slow_loading = 0;
static volatile int is_in_slow_loading = 0;

#define DATATYPE_ENC_VER 1

typedef struct {
    long long intval;
    KVModuleString *strval;
} DataType;

static void *datatype_load(KVModuleIO *io, int encver) {
    load_encver = encver;
    int intval = KVModule_LoadSigned(io);
    if (KVModule_IsIOError(io)) return NULL;

    KVModuleString *strval = KVModule_LoadString(io);
    if (KVModule_IsIOError(io)) return NULL;

    DataType *dt = (DataType *) KVModule_Alloc(sizeof(DataType));
    dt->intval = intval;
    dt->strval = strval;

    if (slow_loading) {
        KVModuleCtx *ctx = KVModule_GetContextFromIO(io);
        is_in_slow_loading = 1;
        while (slow_loading) {
            KVModule_Yield(ctx, KVMODULE_YIELD_FLAG_CLIENTS, "Slow module operation");
            usleep(1000);
        }
        is_in_slow_loading = 0;
    }

    return dt;
}

static void datatype_save(KVModuleIO *io, void *value) {
    DataType *dt = (DataType *) value;
    KVModule_SaveSigned(io, dt->intval);
    KVModule_SaveString(io, dt->strval);
}

static void datatype_free(void *value) {
    if (value) {
        DataType *dt = (DataType *) value;

        if (dt->strval) KVModule_FreeString(NULL, dt->strval);
        KVModule_Free(dt);
    }
}

static void *datatype_copy(KVModuleString *fromkey, KVModuleString *tokey, const void *value) {
    const DataType *old = value;

    /* Answers to ultimate questions cannot be copied! */
    if (old->intval == 42)
        return NULL;

    DataType *new = (DataType *) KVModule_Alloc(sizeof(DataType));

    new->intval = old->intval;
    new->strval = KVModule_CreateStringFromString(NULL, old->strval);

    /* Breaking the rules here! We return a copy that also includes traces
     * of fromkey/tokey to confirm we get what we expect.
     */
    size_t len;
    const char *str = KVModule_StringPtrLen(fromkey, &len);
    KVModule_StringAppendBuffer(NULL, new->strval, "/", 1);
    KVModule_StringAppendBuffer(NULL, new->strval, str, len);
    KVModule_StringAppendBuffer(NULL, new->strval, "/", 1);
    str = KVModule_StringPtrLen(tokey, &len);
    KVModule_StringAppendBuffer(NULL, new->strval, str, len);

    return new;
}

static int datatype_set(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 4) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    long long intval;

    if (KVModule_StringToLongLong(argv[2], &intval) != KVMODULE_OK) {
        KVModule_ReplyWithError(ctx, "Invalid integer value");
        return KVMODULE_OK;
    }

    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_WRITE);
    DataType *dt = KVModule_Calloc(sizeof(DataType), 1);
    dt->intval = intval;
    dt->strval = argv[3];
    KVModule_RetainString(ctx, dt->strval);

    KVModule_ModuleTypeSetValue(key, datatype, dt);
    KVModule_CloseKey(key);
    KVModule_ReplyWithSimpleString(ctx, "OK");

    return KVMODULE_OK;
}

static int datatype_restore(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 4) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    long long encver;
    if (KVModule_StringToLongLong(argv[3], &encver) != KVMODULE_OK) {
        KVModule_ReplyWithError(ctx, "Invalid integer value");
        return KVMODULE_OK;
    }

    DataType *dt = KVModule_LoadDataTypeFromStringEncver(argv[2], datatype, encver);
    if (!dt) {
        KVModule_ReplyWithError(ctx, "Invalid data");
        return KVMODULE_OK;
    }

    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_WRITE);
    KVModule_ModuleTypeSetValue(key, datatype, dt);
    KVModule_CloseKey(key);
    KVModule_ReplyWithLongLong(ctx, load_encver);

    return KVMODULE_OK;
}

static int datatype_get(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_READ);
    DataType *dt = KVModule_ModuleTypeGetValue(key);
    KVModule_CloseKey(key);

    if (!dt) {
        KVModule_ReplyWithNullArray(ctx);
    } else {
        KVModule_ReplyWithArray(ctx, 2);
        KVModule_ReplyWithLongLong(ctx, dt->intval);
        KVModule_ReplyWithString(ctx, dt->strval);
    }
    return KVMODULE_OK;
}

static int datatype_dump(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_READ);
    DataType *dt = KVModule_ModuleTypeGetValue(key);
    KVModule_CloseKey(key);

    KVModuleString *reply = KVModule_SaveDataTypeToString(ctx, dt, datatype);
    if (!reply) {
        KVModule_ReplyWithError(ctx, "Failed to save");
        return KVMODULE_OK;
    }

    KVModule_ReplyWithString(ctx, reply);
    KVModule_FreeString(ctx, reply);
    return KVMODULE_OK;
}

static int datatype_swap(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 3) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    KVModuleKey *a = KVModule_OpenKey(ctx, argv[1], KVMODULE_WRITE);
    KVModuleKey *b = KVModule_OpenKey(ctx, argv[2], KVMODULE_WRITE);
    void *val = KVModule_ModuleTypeGetValue(a);

    int error = (KVModule_ModuleTypeReplaceValue(b, datatype, val, &val) == KVMODULE_ERR ||
                 KVModule_ModuleTypeReplaceValue(a, datatype, val, NULL) == KVMODULE_ERR);
    if (!error)
        KVModule_ReplyWithSimpleString(ctx, "OK");
    else
        KVModule_ReplyWithError(ctx, "ERR failed");

    KVModule_CloseKey(a);
    KVModule_CloseKey(b);

    return KVMODULE_OK;
}

/* used to enable or disable slow loading */
static int datatype_slow_loading(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    long long ll;
    if (KVModule_StringToLongLong(argv[1], &ll) != KVMODULE_OK) {
        KVModule_ReplyWithError(ctx, "Invalid integer value");
        return KVMODULE_OK;
    }
    slow_loading = ll;
    KVModule_ReplyWithSimpleString(ctx, "OK");
    return KVMODULE_OK;
}

/* used to test if we reached the slow loading code */
static int datatype_is_in_slow_loading(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    if (argc != 1) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    KVModule_ReplyWithLongLong(ctx, is_in_slow_loading);
    return KVMODULE_OK;
}

int createDataTypeBlockCheck(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    static KVModuleType *datatype_outside_onload = NULL;

    KVModuleTypeMethods datatype_methods = {
        .version = KVMODULE_TYPE_METHOD_VERSION,
        .rdb_load = datatype_load,
        .rdb_save = datatype_save,
        .free = datatype_free,
        .copy = datatype_copy
    };

    datatype_outside_onload = KVModule_CreateDataType(ctx, "test_dt_outside_onload", 1, &datatype_methods);

    /* This validates that it's not possible to create datatype outside OnLoad,
     * thus returns an error if it succeeds. */
    if (datatype_outside_onload == NULL) {
        KVModule_ReplyWithSimpleString(ctx, "OK");
    } else {
        KVModule_ReplyWithError(ctx, "UNEXPECTEDOK");
    }
    return KVMODULE_OK;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx,"datatype",DATATYPE_ENC_VER,KVMODULE_APIVER_1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    /* Creates a command which creates a datatype outside OnLoad() function. */
    if (KVModule_CreateCommand(ctx,"block.create.datatype.outside.onload", createDataTypeBlockCheck, "write", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    KVModule_SetModuleOptions(ctx, KVMODULE_OPTIONS_HANDLE_IO_ERRORS);

    KVModuleTypeMethods datatype_methods = {
        .version = KVMODULE_TYPE_METHOD_VERSION,
        .rdb_load = datatype_load,
        .rdb_save = datatype_save,
        .free = datatype_free,
        .copy = datatype_copy
    };

    datatype = KVModule_CreateDataType(ctx, "test___dt", 1, &datatype_methods);
    if (datatype == NULL)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"datatype.set", datatype_set,
                                  "write deny-oom", 1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"datatype.get", datatype_get,"",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"datatype.restore", datatype_restore,
                                  "write deny-oom", 1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"datatype.dump", datatype_dump,"",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "datatype.swap", datatype_swap,
                                  "write", 1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "datatype.slow_loading", datatype_slow_loading,
                                  "allow-loading", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "datatype.is_in_slow_loading", datatype_is_in_slow_loading,
                                  "allow-loading", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}
