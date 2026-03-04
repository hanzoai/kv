#include "kvmodule.h"
#include <string.h>
#include <assert.h>
#include <unistd.h>

#define UNUSED(V) ((void) V)

int cmd_publish_classic_multi(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    if (argc < 3)
        return KVModule_WrongArity(ctx);
    KVModule_ReplyWithArray(ctx, argc-2);
    for (int i = 2; i < argc; i++) {
        int receivers = KVModule_PublishMessage(ctx, argv[1], argv[i]);
        KVModule_ReplyWithLongLong(ctx, receivers);
    }
    return KVMODULE_OK;
}

int cmd_publish_classic(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    if (argc != 3)
        return KVModule_WrongArity(ctx);
    
    int receivers = KVModule_PublishMessage(ctx, argv[1], argv[2]);
    KVModule_ReplyWithLongLong(ctx, receivers);
    return KVMODULE_OK;
}

int cmd_publish_shard(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    if (argc != 3)
        return KVModule_WrongArity(ctx);
    
    int receivers = KVModule_PublishMessageShard(ctx, argv[1], argv[2]);
    KVModule_ReplyWithLongLong(ctx, receivers);
    return KVMODULE_OK;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    
    if (KVModule_Init(ctx,"publish",1,KVMODULE_APIVER_1)== KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"publish.classic",cmd_publish_classic,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"publish.classic_multi",cmd_publish_classic_multi,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"publish.shard",cmd_publish_shard,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
        
    return KVMODULE_OK;
}
