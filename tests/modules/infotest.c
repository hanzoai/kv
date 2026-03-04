#include "kvmodule.h"

#include <string.h>

void InfoFunc(KVModuleInfoCtx *ctx, int for_crash_report) {
    KVModule_InfoAddSection(ctx, "");
    KVModule_InfoAddFieldLongLong(ctx, "global", -2);
    KVModule_InfoAddFieldULongLong(ctx, "uglobal", (unsigned long long)-2);

    KVModule_InfoAddSection(ctx, "Spanish");
    KVModule_InfoAddFieldCString(ctx, "uno", "one");
    KVModule_InfoAddFieldLongLong(ctx, "dos", 2);

    KVModule_InfoAddSection(ctx, "Italian");
    KVModule_InfoAddFieldLongLong(ctx, "due", 2);
    KVModule_InfoAddFieldDouble(ctx, "tre", 3.3);

    KVModule_InfoAddSection(ctx, "keyspace");
    KVModule_InfoBeginDictField(ctx, "db0");
    KVModule_InfoAddFieldLongLong(ctx, "keys", 3);
    KVModule_InfoAddFieldLongLong(ctx, "expires", 1);
    KVModule_InfoEndDictField(ctx);

    KVModule_InfoAddSection(ctx, "unsafe");
    KVModule_InfoBeginDictField(ctx, "unsafe:field");
    KVModule_InfoAddFieldLongLong(ctx, "value", 1);
    KVModule_InfoEndDictField(ctx);

    if (for_crash_report) {
        KVModule_InfoAddSection(ctx, "Klingon");
        KVModule_InfoAddFieldCString(ctx, "one", "wa'");
        KVModule_InfoAddFieldCString(ctx, "two", "cha'");
        KVModule_InfoAddFieldCString(ctx, "three", "wej");
    }

}

int info_get(KVModuleCtx *ctx, KVModuleString **argv, int argc, char field_type)
{
    if (argc != 3 && argc != 4) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }
    int err = KVMODULE_OK;
    const char *section, *field;
    section = KVModule_StringPtrLen(argv[1], NULL);
    field = KVModule_StringPtrLen(argv[2], NULL);
    KVModuleServerInfoData *info = KVModule_GetServerInfo(ctx, section);
    if (field_type=='i') {
        long long ll = KVModule_ServerInfoGetFieldSigned(info, field, &err);
        if (err==KVMODULE_OK)
            KVModule_ReplyWithLongLong(ctx, ll);
    } else if (field_type=='u') {
        unsigned long long ll = (unsigned long long)KVModule_ServerInfoGetFieldUnsigned(info, field, &err);
        if (err==KVMODULE_OK)
            KVModule_ReplyWithLongLong(ctx, ll);
    } else if (field_type=='d') {
        double d = KVModule_ServerInfoGetFieldDouble(info, field, &err);
        if (err==KVMODULE_OK)
            KVModule_ReplyWithDouble(ctx, d);
    } else if (field_type=='c') {
        const char *str = KVModule_ServerInfoGetFieldC(info, field);
        if (str)
            KVModule_ReplyWithCString(ctx, str);
    } else {
        KVModuleString *str = KVModule_ServerInfoGetField(ctx, info, field);
        if (str) {
            KVModule_ReplyWithString(ctx, str);
            KVModule_FreeString(ctx, str);
        } else
            err=KVMODULE_ERR;
    }
    if (err!=KVMODULE_OK)
        KVModule_ReplyWithError(ctx, "not found");
    KVModule_FreeServerInfo(ctx, info);
    return KVMODULE_OK;
}

int info_gets(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    return info_get(ctx, argv, argc, 's');
}

int info_getc(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    return info_get(ctx, argv, argc, 'c');
}

int info_geti(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    return info_get(ctx, argv, argc, 'i');
}

int info_getu(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    return info_get(ctx, argv, argc, 'u');
}

int info_getd(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    return info_get(ctx, argv, argc, 'd');
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    if (KVModule_Init(ctx,"infotest",1,KVMODULE_APIVER_1)
            == KVMODULE_ERR) return KVMODULE_ERR;

    if (KVModule_RegisterInfoFunc(ctx, InfoFunc) == KVMODULE_ERR) return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"info.gets", info_gets,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"info.getc", info_getc,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"info.geti", info_geti,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"info.getu", info_getu,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"info.getd", info_getd,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}
