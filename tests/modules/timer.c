
#include "kvmodule.h"

static void timer_callback(KVModuleCtx *ctx, void *data)
{
    KVModuleString *keyname = data;
    KVModuleCallReply *reply;

    reply = KVModule_Call(ctx, "INCR", "s", keyname);
    if (reply != NULL)
        KVModule_FreeCallReply(reply);
    KVModule_FreeString(ctx, keyname);
}

int test_createtimer(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    if (argc != 3) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    long long period;
    if (KVModule_StringToLongLong(argv[1], &period) == KVMODULE_ERR) {
        KVModule_ReplyWithError(ctx, "Invalid time specified.");
        return KVMODULE_OK;
    }

    KVModuleString *keyname = argv[2];
    KVModule_RetainString(ctx, keyname);

    KVModuleTimerID id = KVModule_CreateTimer(ctx, period, timer_callback, keyname);
    KVModule_ReplyWithLongLong(ctx, id);

    return KVMODULE_OK;
}

int test_gettimer(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    if (argc != 2) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    long long id;
    if (KVModule_StringToLongLong(argv[1], &id) == KVMODULE_ERR) {
        KVModule_ReplyWithError(ctx, "Invalid id specified.");
        return KVMODULE_OK;
    }

    uint64_t remaining;
    KVModuleString *keyname;
    if (KVModule_GetTimerInfo(ctx, id, &remaining, (void **)&keyname) == KVMODULE_ERR) {
        KVModule_ReplyWithNull(ctx);
    } else {
        KVModule_ReplyWithArray(ctx, 2);
        KVModule_ReplyWithString(ctx, keyname);
        KVModule_ReplyWithLongLong(ctx, remaining);
    }

    return KVMODULE_OK;
}

int test_stoptimer(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    if (argc != 2) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    long long id;
    if (KVModule_StringToLongLong(argv[1], &id) == KVMODULE_ERR) {
        KVModule_ReplyWithError(ctx, "Invalid id specified.");
        return KVMODULE_OK;
    }

    int ret = 0;
    KVModuleString *keyname;
    if (KVModule_StopTimer(ctx, id, (void **) &keyname) == KVMODULE_OK) {
        KVModule_FreeString(ctx, keyname);
        ret = 1;
    }

    KVModule_ReplyWithLongLong(ctx, ret);
    return KVMODULE_OK;
}


int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    if (KVModule_Init(ctx,"timer",1,KVMODULE_APIVER_1)== KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"test.createtimer", test_createtimer,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"test.gettimer", test_gettimer,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"test.stoptimer", test_stoptimer,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}
