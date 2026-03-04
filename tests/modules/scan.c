#include "kvmodule.h"

#include <string.h>
#include <assert.h>
#include <unistd.h>

typedef struct {
    size_t nkeys;
} scan_strings_pd;

void scan_strings_callback(KVModuleCtx *ctx, KVModuleString* keyname, KVModuleKey* key, void *privdata) {
    scan_strings_pd* pd = privdata;
    int was_opened = 0;
    if (!key) {
        key = KVModule_OpenKey(ctx, keyname, KVMODULE_READ);
        was_opened = 1;
    }

    if (KVModule_KeyType(key) == KVMODULE_KEYTYPE_STRING) {
        size_t len;
        char * data = KVModule_StringDMA(key, &len, KVMODULE_READ);
        KVModule_ReplyWithArray(ctx, 2);
        KVModule_ReplyWithString(ctx, keyname);
        KVModule_ReplyWithStringBuffer(ctx, data, len);
        pd->nkeys++;
    }
    if (was_opened)
        KVModule_CloseKey(key);
}

int scan_strings(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    scan_strings_pd pd = {
        .nkeys = 0,
    };

    KVModule_ReplyWithArray(ctx, KVMODULE_POSTPONED_LEN);

    KVModuleScanCursor* cursor = KVModule_ScanCursorCreate();
    while(KVModule_Scan(ctx, cursor, scan_strings_callback, &pd));
    KVModule_ScanCursorDestroy(cursor);

    KVModule_ReplySetArrayLength(ctx, pd.nkeys);
    return KVMODULE_OK;
}

typedef struct {
    KVModuleCtx *ctx;
    size_t nreplies;
} scan_key_pd;

void scan_key_callback(KVModuleKey *key, KVModuleString* field, KVModuleString* value, void *privdata) {
    KVMODULE_NOT_USED(key);
    scan_key_pd* pd = privdata;
    KVModule_ReplyWithArray(pd->ctx, 2);
    size_t fieldCStrLen;

    // The implementation of KVModuleString is robj with lots of encodings.
    // We want to make sure the robj that passes to this callback in
    // String encoded, this is why we use KVModule_StringPtrLen and
    // KVModule_ReplyWithStringBuffer instead of directly use
    // KVModule_ReplyWithString.
    const char* fieldCStr = KVModule_StringPtrLen(field, &fieldCStrLen);
    KVModule_ReplyWithStringBuffer(pd->ctx, fieldCStr, fieldCStrLen);
    if(value){
        size_t valueCStrLen;
        const char* valueCStr = KVModule_StringPtrLen(value, &valueCStrLen);
        KVModule_ReplyWithStringBuffer(pd->ctx, valueCStr, valueCStrLen);
    } else {
        KVModule_ReplyWithNull(pd->ctx);
    }

    pd->nreplies++;
}

int scan_key(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    if (argc != 2) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }
    scan_key_pd pd = {
        .ctx = ctx,
        .nreplies = 0,
    };

    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_READ);
    if (!key) {
        KVModule_ReplyWithError(ctx, "not found");
        return KVMODULE_OK;
    }

    KVModule_ReplyWithArray(ctx, KVMODULE_POSTPONED_ARRAY_LEN);

    KVModuleScanCursor* cursor = KVModule_ScanCursorCreate();
    while(KVModule_ScanKey(key, cursor, scan_key_callback, &pd));
    KVModule_ScanCursorDestroy(cursor);

    KVModule_ReplySetArrayLength(ctx, pd.nreplies);
    KVModule_CloseKey(key);
    return KVMODULE_OK;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    if (KVModule_Init(ctx, "scan", 1, KVMODULE_APIVER_1)== KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "scan.scan_strings", scan_strings, "", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "scan.scan_key", scan_key, "", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}


