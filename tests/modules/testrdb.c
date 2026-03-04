#include "kvmodule.h"

#include <string.h>
#include <assert.h>

/* Module configuration, save aux or not? */
#define CONF_AUX_OPTION_NO_AUX           0
#define CONF_AUX_OPTION_SAVE2            1 << 0
#define CONF_AUX_OPTION_BEFORE_KEYSPACE  1 << 1
#define CONF_AUX_OPTION_AFTER_KEYSPACE   1 << 2
#define CONF_AUX_OPTION_NO_DATA          1 << 3
long long conf_aux_count = 0;

/* Registered type */
KVModuleType *testrdb_type = NULL;

/* Global values to store and persist to aux */
KVModuleString *before_str = NULL;
KVModuleString *after_str = NULL;

/* Global values used to keep aux from db being loaded (in case of async_loading) */
KVModuleString *before_str_temp = NULL;
KVModuleString *after_str_temp = NULL;

/* Indicates whether there is an async replication in progress.
 * We control this value from KVModuleEvent_ReplAsyncLoad events. */
int async_loading = 0;

int n_aux_load_called = 0;

void replAsyncLoadCallback(KVModuleCtx *ctx, KVModuleEvent e, uint64_t sub, void *data)
{
    KVMODULE_NOT_USED(e);
    KVMODULE_NOT_USED(data);

    switch (sub) {
    case KVMODULE_SUBEVENT_REPL_ASYNC_LOAD_STARTED:
        assert(async_loading == 0);
        async_loading = 1;
        break;
    case KVMODULE_SUBEVENT_REPL_ASYNC_LOAD_ABORTED:
        /* Discard temp aux */
        if (before_str_temp)
            KVModule_FreeString(ctx, before_str_temp);
        if (after_str_temp)
            KVModule_FreeString(ctx, after_str_temp);
        before_str_temp = NULL;
        after_str_temp = NULL;

        async_loading = 0;
        break;
    case KVMODULE_SUBEVENT_REPL_ASYNC_LOAD_COMPLETED:
        if (before_str)
            KVModule_FreeString(ctx, before_str);
        if (after_str)
            KVModule_FreeString(ctx, after_str);
        before_str = before_str_temp;
        after_str = after_str_temp;

        before_str_temp = NULL;
        after_str_temp = NULL;

        async_loading = 0;
        break;
    default:
        assert(0);
    }
}

void *testrdb_type_load(KVModuleIO *rdb, int encver) {
    int count = KVModule_LoadSigned(rdb);
    KVModuleString *str = KVModule_LoadString(rdb);
    float f = KVModule_LoadFloat(rdb);
    long double ld = KVModule_LoadLongDouble(rdb);
    /* Context creation is part of the test. Creating the context will force the
     * core to allocate a context, which needs to be cleaned up when the
     * KVModuleIO is destructed. */
    KVModuleCtx *ctx = KVModule_GetContextFromIO(rdb);
    if (KVModule_IsIOError(rdb)) {
        if (str)
            KVModule_FreeString(ctx, str);
        return NULL;
    }
    /* Using the values only after checking for io errors. */
    assert(count==1);
    assert(encver==1);
    assert(f==1.5f);
    assert(ld==0.333333333333333333L);
    return str;
}

void testrdb_type_save(KVModuleIO *rdb, void *value) {
    KVModuleString *str = (KVModuleString*)value;
    KVModule_SaveSigned(rdb, 1);
    KVModule_SaveString(rdb, str);
    KVModule_SaveFloat(rdb, 1.5);
    KVModule_SaveLongDouble(rdb, 0.333333333333333333L);
}

void testrdb_aux_save(KVModuleIO *rdb, int when) {
    if (!(conf_aux_count & CONF_AUX_OPTION_BEFORE_KEYSPACE)) assert(when == KVMODULE_AUX_AFTER_RDB);
    if (!(conf_aux_count & CONF_AUX_OPTION_AFTER_KEYSPACE)) assert(when == KVMODULE_AUX_BEFORE_RDB);
    assert(conf_aux_count!=CONF_AUX_OPTION_NO_AUX);
    /* Context creation is part of the test. Creating the context will force the
     * core to allocate a context, which needs to be cleaned up when the
     * KVModuleIO is destructed. */
    KVModuleCtx *ctx = KVModule_GetContextFromIO(rdb);
    KVMODULE_NOT_USED(ctx);
    if (when == KVMODULE_AUX_BEFORE_RDB) {
        if (before_str) {
            KVModule_SaveSigned(rdb, 1);
            KVModule_SaveString(rdb, before_str);
        } else {
            KVModule_SaveSigned(rdb, 0);
        }
    } else {
        if (after_str) {
            KVModule_SaveSigned(rdb, 1);
            KVModule_SaveString(rdb, after_str);
        } else {
            KVModule_SaveSigned(rdb, 0);
        }
    }
}

int testrdb_aux_load(KVModuleIO *rdb, int encver, int when) {
    assert(encver == 1);
    if (!(conf_aux_count & CONF_AUX_OPTION_BEFORE_KEYSPACE)) assert(when == KVMODULE_AUX_AFTER_RDB);
    if (!(conf_aux_count & CONF_AUX_OPTION_AFTER_KEYSPACE)) assert(when == KVMODULE_AUX_BEFORE_RDB);
    assert(conf_aux_count!=CONF_AUX_OPTION_NO_AUX);
    KVModuleCtx *ctx = KVModule_GetContextFromIO(rdb);
    if (when == KVMODULE_AUX_BEFORE_RDB) {
        if (async_loading == 0) {
            if (before_str)
                KVModule_FreeString(ctx, before_str);
            before_str = NULL;
            int count = KVModule_LoadSigned(rdb);
            if (KVModule_IsIOError(rdb))
                return KVMODULE_ERR;
            if (count)
                before_str = KVModule_LoadString(rdb);
        } else {
            if (before_str_temp)
                KVModule_FreeString(ctx, before_str_temp);
            before_str_temp = NULL;
            int count = KVModule_LoadSigned(rdb);
            if (KVModule_IsIOError(rdb))
                return KVMODULE_ERR;
            if (count)
                before_str_temp = KVModule_LoadString(rdb);
        }
    } else {
        if (async_loading == 0) {
            if (after_str)
                KVModule_FreeString(ctx, after_str);
            after_str = NULL;
            int count = KVModule_LoadSigned(rdb);
            if (KVModule_IsIOError(rdb))
                return KVMODULE_ERR;
            if (count)
                after_str = KVModule_LoadString(rdb);
        } else {
            if (after_str_temp)
                KVModule_FreeString(ctx, after_str_temp);
            after_str_temp = NULL;
            int count = KVModule_LoadSigned(rdb);
            if (KVModule_IsIOError(rdb))
                return KVMODULE_ERR;
            if (count)
                after_str_temp = KVModule_LoadString(rdb);
        }
    }

    if (KVModule_IsIOError(rdb))
        return KVMODULE_ERR;
    return KVMODULE_OK;
}

void testrdb_type_free(void *value) {
    if (value)
        KVModule_FreeString(NULL, (KVModuleString*)value);
}

int testrdb_set_before(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    if (argc != 2) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    if (before_str)
        KVModule_FreeString(ctx, before_str);
    before_str = argv[1];
    KVModule_RetainString(ctx, argv[1]);
    KVModule_ReplyWithLongLong(ctx, 1);
    return KVMODULE_OK;
}

int testrdb_get_before(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    KVMODULE_NOT_USED(argv);
    if (argc != 1){
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }
    if (before_str)
        KVModule_ReplyWithString(ctx, before_str);
    else
        KVModule_ReplyWithStringBuffer(ctx, "", 0);
    return KVMODULE_OK;
}

/* For purpose of testing module events, expose variable state during async_loading. */
int testrdb_async_loading_get_before(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    KVMODULE_NOT_USED(argv);
    if (argc != 1){
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }
    if (before_str_temp)
        KVModule_ReplyWithString(ctx, before_str_temp);
    else
        KVModule_ReplyWithStringBuffer(ctx, "", 0);
    return KVMODULE_OK;
}

int testrdb_set_after(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    if (argc != 2){
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    if (after_str)
        KVModule_FreeString(ctx, after_str);
    after_str = argv[1];
    KVModule_RetainString(ctx, argv[1]);
    KVModule_ReplyWithLongLong(ctx, 1);
    return KVMODULE_OK;
}

int testrdb_get_after(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    KVMODULE_NOT_USED(argv);
    if (argc != 1){
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }
    if (after_str)
        KVModule_ReplyWithString(ctx, after_str);
    else
        KVModule_ReplyWithStringBuffer(ctx, "", 0);
    return KVMODULE_OK;
}

int testrdb_set_key(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    if (argc != 3){
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_WRITE);
    KVModuleString *str = KVModule_ModuleTypeGetValue(key);
    if (str)
        KVModule_FreeString(ctx, str);
    KVModule_ModuleTypeSetValue(key, testrdb_type, argv[2]);
    KVModule_RetainString(ctx, argv[2]);
    KVModule_CloseKey(key);
    KVModule_ReplyWithLongLong(ctx, 1);
    return KVMODULE_OK;
}

int testrdb_get_key(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    if (argc != 2){
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_READ);
    KVModuleString *str = KVModule_ModuleTypeGetValue(key);
    KVModule_CloseKey(key);
    KVModule_ReplyWithString(ctx, str);
    return KVMODULE_OK;
}

int testrdb_get_n_aux_load_called(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    KVMODULE_NOT_USED(ctx);
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    KVModule_ReplyWithLongLong(ctx, n_aux_load_called);
    return KVMODULE_OK;
}

int test2rdb_aux_load(KVModuleIO *rdb, int encver, int when) {
    KVMODULE_NOT_USED(rdb);
    KVMODULE_NOT_USED(encver);
    KVMODULE_NOT_USED(when);
    n_aux_load_called++;
    return KVMODULE_OK;
}

void test2rdb_aux_save(KVModuleIO *rdb, int when) {
    KVMODULE_NOT_USED(rdb);
    KVMODULE_NOT_USED(when);
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    if (KVModule_Init(ctx,"testrdb",1,KVMODULE_APIVER_1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    KVModule_SetModuleOptions(ctx, KVMODULE_OPTIONS_HANDLE_IO_ERRORS | KVMODULE_OPTIONS_HANDLE_REPL_ASYNC_LOAD | KVMODULE_OPTIONS_HANDLE_ATOMIC_SLOT_MIGRATION);

    if (argc > 0)
        KVModule_StringToLongLong(argv[0], &conf_aux_count);

    if (conf_aux_count==CONF_AUX_OPTION_NO_AUX) {
        KVModuleTypeMethods datatype_methods = {
            .version = 1,
            .rdb_load = testrdb_type_load,
            .rdb_save = testrdb_type_save,
            .aof_rewrite = NULL,
            .digest = NULL,
            .free = testrdb_type_free,
        };

        testrdb_type = KVModule_CreateDataType(ctx, "test__rdb", 1, &datatype_methods);
        if (testrdb_type == NULL)
            return KVMODULE_ERR;
    } else if (!(conf_aux_count & CONF_AUX_OPTION_NO_DATA)) {
        KVModuleTypeMethods datatype_methods = {
            .version = KVMODULE_TYPE_METHOD_VERSION,
            .rdb_load = testrdb_type_load,
            .rdb_save = testrdb_type_save,
            .aof_rewrite = NULL,
            .digest = NULL,
            .free = testrdb_type_free,
            .aux_load = testrdb_aux_load,
            .aux_save = testrdb_aux_save,
            .aux_save_triggers = ((conf_aux_count & CONF_AUX_OPTION_BEFORE_KEYSPACE) ? KVMODULE_AUX_BEFORE_RDB : 0) |
                                 ((conf_aux_count & CONF_AUX_OPTION_AFTER_KEYSPACE)  ? KVMODULE_AUX_AFTER_RDB : 0)
        };

        if (conf_aux_count & CONF_AUX_OPTION_SAVE2) {
            datatype_methods.aux_save2 = testrdb_aux_save;
        }

        testrdb_type = KVModule_CreateDataType(ctx, "test__rdb", 1, &datatype_methods);
        if (testrdb_type == NULL)
            return KVMODULE_ERR;
    } else {

        /* Used to verify that aux_save2 api without any data, saves nothing to the RDB. */
        KVModuleTypeMethods datatype_methods = {
            .version = KVMODULE_TYPE_METHOD_VERSION,
            .aux_load = test2rdb_aux_load,
            .aux_save = test2rdb_aux_save,
            .aux_save_triggers = ((conf_aux_count & CONF_AUX_OPTION_BEFORE_KEYSPACE) ? KVMODULE_AUX_BEFORE_RDB : 0) |
                                 ((conf_aux_count & CONF_AUX_OPTION_AFTER_KEYSPACE)  ? KVMODULE_AUX_AFTER_RDB : 0)
        };
        if (conf_aux_count & CONF_AUX_OPTION_SAVE2) {
            datatype_methods.aux_save2 = test2rdb_aux_save;
        }

        KVModule_CreateDataType(ctx, "test__rdb", 1, &datatype_methods);
    }

    if (KVModule_CreateCommand(ctx,"testrdb.set.before", testrdb_set_before,"deny-oom",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"testrdb.get.before", testrdb_get_before,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"testrdb.async_loading.get.before", testrdb_async_loading_get_before,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"testrdb.set.after", testrdb_set_after,"deny-oom",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"testrdb.get.after", testrdb_get_after,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"testrdb.set.key", testrdb_set_key,"deny-oom",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"testrdb.get.key", testrdb_get_key,"",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"testrdb.get.n_aux_load_called", testrdb_get_n_aux_load_called,"",1,1,1) == KVMODULE_ERR)
            return KVMODULE_ERR;

    KVModule_SubscribeToServerEvent(ctx,
        KVModuleEvent_ReplAsyncLoad, replAsyncLoadCallback);

    return KVMODULE_OK;
}

int KVModule_OnUnload(KVModuleCtx *ctx) {
    if (before_str)
        KVModule_FreeString(ctx, before_str);
    if (after_str)
        KVModule_FreeString(ctx, after_str);
    if (before_str_temp)
        KVModule_FreeString(ctx, before_str_temp);
    if (after_str_temp)
        KVModule_FreeString(ctx, after_str_temp);
    return KVMODULE_OK;
}
