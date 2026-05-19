#include "kvmodule.h"

#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#define UNUSED(x) (void)(x)

static int n_events = 0;

static int KeySpace_NotificationModuleKeyMissExpired(KVModuleCtx *ctx, int type, const char *event, KVModuleString *key) {
    UNUSED(ctx);
    UNUSED(type);
    UNUSED(event);
    UNUSED(key);
    n_events++;
    return KVMODULE_OK;
}

int test_clear_n_events(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    n_events = 0;
    KVModule_ReplyWithSimpleString(ctx, "OK");
    return KVMODULE_OK;
}

int test_get_n_events(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    KVModule_ReplyWithLongLong(ctx, n_events);
    return KVMODULE_OK;
}

int test_open_key_no_effects(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc<2) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    int supportedMode = KVModule_GetOpenKeyModesAll();
    if (!(supportedMode & KVMODULE_READ) || !(supportedMode & KVMODULE_OPEN_KEY_NOEFFECTS)) {
        KVModule_ReplyWithError(ctx, "OpenKey modes are not supported");
        return KVMODULE_OK;
    }

    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_READ | KVMODULE_OPEN_KEY_NOEFFECTS);
    if (!key) {
        KVModule_ReplyWithError(ctx, "key not found");
        return KVMODULE_OK;
    }

    KVModule_CloseKey(key);
    KVModule_ReplyWithSimpleString(ctx, "OK");
    return KVMODULE_OK;
}

int test_call_generic(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    if (argc<2) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    const char* cmdname = KVModule_StringPtrLen(argv[1], NULL);
    KVModuleCallReply *reply = KVModule_Call(ctx, cmdname, "v", argv+2, (size_t)argc-2);
    if (reply) {
        KVModule_ReplyWithCallReply(ctx, reply);
        KVModule_FreeCallReply(reply);
    } else {
        KVModule_ReplyWithError(ctx, strerror(errno));
    }
    return KVMODULE_OK;
}

int test_call_info(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    KVModuleCallReply *reply;
    if (argc>1)
        reply = KVModule_Call(ctx, "info", "s", argv[1]);
    else
        reply = KVModule_Call(ctx, "info", "");
    if (reply) {
        KVModule_ReplyWithCallReply(ctx, reply);
        KVModule_FreeCallReply(reply);
    } else {
        KVModule_ReplyWithError(ctx, strerror(errno));
    }
    return KVMODULE_OK;
}

int test_ld_conv(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    long double ld = 0.00000000000000001L;
    const char *ldstr = "0.00000000000000001";
    KVModuleString *s1 = KVModule_CreateStringFromLongDouble(ctx, ld, 1);
    KVModuleString *s2 =
        KVModule_CreateString(ctx, ldstr, strlen(ldstr));
    if (KVModule_StringCompare(s1, s2) != 0) {
        char err[4096];
        snprintf(err, 4096,
            "Failed to convert long double to string ('%s' != '%s')",
            KVModule_StringPtrLen(s1, NULL),
            KVModule_StringPtrLen(s2, NULL));
        KVModule_ReplyWithError(ctx, err);
        goto final;
    }
    long double ld2 = 0;
    if (KVModule_StringToLongDouble(s2, &ld2) == KVMODULE_ERR) {
        KVModule_ReplyWithError(ctx,
            "Failed to convert string to long double");
        goto final;
    }
    if (ld2 != ld) {
        char err[4096];
        snprintf(err, 4096,
            "Failed to convert string to long double (%.40Lf != %.40Lf)",
            ld2,
            ld);
        KVModule_ReplyWithError(ctx, err);
        goto final;
    }

    /* Make sure we can't convert a string that has \0 in it */
    char buf[4] = "123";
    buf[1] = '\0';
    KVModuleString *s3 = KVModule_CreateString(ctx, buf, 3);
    long double ld3;
    if (KVModule_StringToLongDouble(s3, &ld3) == KVMODULE_OK) {
        KVModule_ReplyWithError(ctx, "Invalid string successfully converted to long double");
        KVModule_FreeString(ctx, s3);
        goto final;
    }
    KVModule_FreeString(ctx, s3);

    KVModule_ReplyWithLongDouble(ctx, ld2);
final:
    KVModule_FreeString(ctx, s1);
    KVModule_FreeString(ctx, s2);
    return KVMODULE_OK;
}

int test_flushall(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    KVModule_ResetDataset(1, 0);
    KVModule_ReplyWithCString(ctx, "Ok");
    return KVMODULE_OK;
}

int test_dbsize(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    long long ll = KVModule_DbSize(ctx);
    KVModule_ReplyWithLongLong(ctx, ll);
    return KVMODULE_OK;
}

int test_randomkey(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    KVModuleString *str = KVModule_RandomKey(ctx);
    KVModule_ReplyWithString(ctx, str);
    KVModule_FreeString(ctx, str);
    return KVMODULE_OK;
}

int test_keyexists(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc < 2) return KVModule_WrongArity(ctx);
    KVModuleString *key = argv[1];
    int exists = KVModule_KeyExists(ctx, key);
    return KVModule_ReplyWithBool(ctx, exists);
}

KVModuleKey *open_key_or_reply(KVModuleCtx *ctx, KVModuleString *keyname, int mode) {
    KVModuleKey *key = KVModule_OpenKey(ctx, keyname, mode);
    if (!key) {
        KVModule_ReplyWithError(ctx, "key not found");
        return NULL;
    }
    return key;
}

int test_getlru(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    if (argc<2) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }
    KVModuleKey *key = open_key_or_reply(ctx, argv[1], KVMODULE_READ|KVMODULE_OPEN_KEY_NOTOUCH);
    mstime_t lru;
    KVModule_GetLRU(key, &lru);
    KVModule_ReplyWithLongLong(ctx, lru);
    KVModule_CloseKey(key);
    return KVMODULE_OK;
}

int test_setlru(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    if (argc<3) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }
    KVModuleKey *key = open_key_or_reply(ctx, argv[1], KVMODULE_READ|KVMODULE_OPEN_KEY_NOTOUCH);
    mstime_t lru;
    if (KVModule_StringToLongLong(argv[2], &lru) != KVMODULE_OK) {
        KVModule_ReplyWithError(ctx, "invalid idle time");
        return KVMODULE_OK;
    }
    int was_set = KVModule_SetLRU(key, lru)==KVMODULE_OK;
    KVModule_ReplyWithLongLong(ctx, was_set);
    KVModule_CloseKey(key);
    return KVMODULE_OK;
}

int test_getlfu(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    if (argc<2) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }
    KVModuleKey *key = open_key_or_reply(ctx, argv[1], KVMODULE_READ|KVMODULE_OPEN_KEY_NOTOUCH);
    mstime_t lfu;
    KVModule_GetLFU(key, &lfu);
    KVModule_ReplyWithLongLong(ctx, lfu);
    KVModule_CloseKey(key);
    return KVMODULE_OK;
}

int test_setlfu(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    if (argc<3) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }
    KVModuleKey *key = open_key_or_reply(ctx, argv[1], KVMODULE_READ|KVMODULE_OPEN_KEY_NOTOUCH);
    mstime_t lfu;
    if (KVModule_StringToLongLong(argv[2], &lfu) != KVMODULE_OK) {
        KVModule_ReplyWithError(ctx, "invalid freq");
        return KVMODULE_OK;
    }
    int was_set = KVModule_SetLFU(key, lfu)==KVMODULE_OK;
    KVModule_ReplyWithLongLong(ctx, was_set);
    KVModule_CloseKey(key);
    return KVMODULE_OK;
}

int test_serverversion(KVModuleCtx *ctx, KVModuleString **argv, int argc){
    (void) argv;
    (void) argc;

    int version = KVModule_GetServerVersion();
    int patch = version & 0x000000ff;
    int minor = (version & 0x0000ff00) >> 8;
    int major = (version & 0x00ff0000) >> 16;

    KVModuleString* vStr = KVModule_CreateStringPrintf(ctx, "%d.%d.%d", major, minor, patch);
    KVModule_ReplyWithString(ctx, vStr);
    KVModule_FreeString(ctx, vStr);
  
    return KVMODULE_OK;
}

int test_getclientcert(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    (void) argv;
    (void) argc;

    KVModuleString *cert = KVModule_GetClientCertificate(ctx,
            KVModule_GetClientId(ctx));
    if (!cert) {
        KVModule_ReplyWithNull(ctx);
    } else {
        KVModule_ReplyWithString(ctx, cert);
        KVModule_FreeString(ctx, cert);
    }

    return KVMODULE_OK;
}

int test_clientinfo(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    (void) argv;
    (void) argc;

    KVModuleClientInfoV1 ci = KVMODULE_CLIENTINFO_INITIALIZER_V1;
    uint64_t client_id = KVModule_GetClientId(ctx);

    /* Check expected result from the V1 initializer. */
    assert(ci.version == 1);
    /* Trying to populate a future version of the struct should fail. */
    ci.version = KVMODULE_CLIENTINFO_VERSION + 1;
    assert(KVModule_GetClientInfoById(&ci, client_id) == KVMODULE_ERR);

    ci.version = 1;
    if (KVModule_GetClientInfoById(&ci, client_id) == KVMODULE_ERR) {
            KVModule_ReplyWithError(ctx, "failed to get client info");
            return KVMODULE_OK;
    }

    KVModule_ReplyWithArray(ctx, 10);
    char flags[512];
    snprintf(flags, sizeof(flags) - 1, "%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s",
        ci.flags & KVMODULE_CLIENTINFO_FLAG_SSL ? "ssl" : "",
        ci.flags & KVMODULE_CLIENTINFO_FLAG_PUBSUB ? "pubsub" : "",
        ci.flags & KVMODULE_CLIENTINFO_FLAG_BLOCKED ? "blocked" : "",
        ci.flags & KVMODULE_CLIENTINFO_FLAG_TRACKING ? "tracking" : "",
        ci.flags & KVMODULE_CLIENTINFO_FLAG_UNIXSOCKET ? "unixsocket" : "",
        ci.flags & KVMODULE_CLIENTINFO_FLAG_MULTI ? "multi" : "",
        ci.flags & KVMODULE_CLIENTINFO_FLAG_READONLY ? "readonly" : "",
        ci.flags & KVMODULE_CLIENTINFO_FLAG_PRIMARY ? "primary" : "",
        ci.flags & KVMODULE_CLIENTINFO_FLAG_REPLICA ? "replica" : "",
        ci.flags & KVMODULE_CLIENTINFO_FLAG_MONITOR ? "monitor" : "",
        ci.flags & KVMODULE_CLIENTINFO_FLAG_MODULE ? "module" : "",
        ci.flags & KVMODULE_CLIENTINFO_FLAG_AUTHENTICATED ? "authenticated" : "",
        ci.flags & KVMODULE_CLIENTINFO_FLAG_EVER_AUTHENTICATED ? "ever_authenticated" : "",
        ci.flags & KVMODULE_CLIENTINFO_FLAG_FAKE ? "fake" : "");

    KVModule_ReplyWithCString(ctx, "flags");
    KVModule_ReplyWithCString(ctx, flags);
    KVModule_ReplyWithCString(ctx, "id");
    KVModule_ReplyWithLongLong(ctx, ci.id);
    KVModule_ReplyWithCString(ctx, "addr");
    KVModule_ReplyWithCString(ctx, ci.addr);
    KVModule_ReplyWithCString(ctx, "port");
    KVModule_ReplyWithLongLong(ctx, ci.port);
    KVModule_ReplyWithCString(ctx, "db");
    KVModule_ReplyWithLongLong(ctx, ci.db);

    return KVMODULE_OK;
}

int test_getname(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    (void)argv;
    if (argc != 1) return KVModule_WrongArity(ctx);
    unsigned long long id = KVModule_GetClientId(ctx);
    KVModuleString *name = KVModule_GetClientNameById(ctx, id);
    if (name == NULL)
        return KVModule_ReplyWithError(ctx, "-ERR No name");
    KVModule_ReplyWithString(ctx, name);
    KVModule_FreeString(ctx, name);
    return KVMODULE_OK;
}

int test_setname(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) return KVModule_WrongArity(ctx);
    unsigned long long id = KVModule_GetClientId(ctx);
    if (KVModule_SetClientNameById(id, argv[1]) == KVMODULE_OK)
        return KVModule_ReplyWithSimpleString(ctx, "OK");
    else
        return KVModule_ReplyWithError(ctx, strerror(errno));
}

int test_log_tsctx(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    KVModuleCtx *tsctx = KVModule_GetDetachedThreadSafeContext(ctx);

    if (argc != 3) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    char level[50];
    size_t level_len;
    const char *level_str = KVModule_StringPtrLen(argv[1], &level_len);
    snprintf(level, sizeof(level) - 1, "%.*s", (int) level_len, level_str);

    size_t msg_len;
    const char *msg_str = KVModule_StringPtrLen(argv[2], &msg_len);

    KVModule_Log(tsctx, level, "%.*s", (int) msg_len, msg_str);
    KVModule_FreeThreadSafeContext(tsctx);

    KVModule_ReplyWithSimpleString(ctx, "OK");
    return KVMODULE_OK;
}

int test_weird_cmd(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModule_ReplyWithSimpleString(ctx, "OK");
    return KVMODULE_OK;
}

int test_monotonic_time(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModule_ReplyWithLongLong(ctx, KVModule_MonotonicMicroseconds());
    return KVMODULE_OK;
}

/* wrapper for RM_Call */
int test_rm_call(KVModuleCtx *ctx, KVModuleString **argv, int argc){
    if(argc < 2){
        return KVModule_WrongArity(ctx);
    }

    const char* cmd = KVModule_StringPtrLen(argv[1], NULL);

    KVModuleCallReply* rep = KVModule_Call(ctx, cmd, "Ev", argv + 2, (size_t)argc - 2);
    if(!rep){
        KVModule_ReplyWithError(ctx, "NULL reply returned");
    }else{
        KVModule_ReplyWithCallReply(ctx, rep);
        KVModule_FreeCallReply(rep);
    }

    return KVMODULE_OK;
}

/* wrapper for RM_Call which also replicates the module command */
int test_rm_call_replicate(KVModuleCtx *ctx, KVModuleString **argv, int argc){
    test_rm_call(ctx, argv, argc);
    KVModule_ReplicateVerbatim(ctx);

    return KVMODULE_OK;
}

/* wrapper for RM_Call with flags */
int test_rm_call_flags(KVModuleCtx *ctx, KVModuleString **argv, int argc){
    if(argc < 3){
        return KVModule_WrongArity(ctx);
    }

    /* Append Ev to the provided flags. */
    KVModuleString *flags = KVModule_CreateStringFromString(ctx, argv[1]);
    KVModule_StringAppendBuffer(ctx, flags, "Ev", 2);

    const char* flg = KVModule_StringPtrLen(flags, NULL);
    const char* cmd = KVModule_StringPtrLen(argv[2], NULL);

    KVModuleCallReply* rep = KVModule_Call(ctx, cmd, flg, argv + 3, (size_t)argc - 3);
    if(!rep){
        KVModule_ReplyWithError(ctx, "NULL reply returned");
    }else{
        KVModule_ReplyWithCallReply(ctx, rep);
        KVModule_FreeCallReply(rep);
    }
    KVModule_FreeString(ctx, flags);

    return KVMODULE_OK;
}

int test_ull_conv(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    unsigned long long ull = 18446744073709551615ULL;
    const char *ullstr = "18446744073709551615";

    KVModuleString *s1 = KVModule_CreateStringFromULongLong(ctx, ull);
    KVModuleString *s2 =
        KVModule_CreateString(ctx, ullstr, strlen(ullstr));
    if (KVModule_StringCompare(s1, s2) != 0) {
        char err[4096];
        snprintf(err, 4096,
            "Failed to convert unsigned long long to string ('%s' != '%s')",
            KVModule_StringPtrLen(s1, NULL),
            KVModule_StringPtrLen(s2, NULL));
        KVModule_ReplyWithError(ctx, err);
        goto final;
    }
    unsigned long long ull2 = 0;
    if (KVModule_StringToULongLong(s2, &ull2) == KVMODULE_ERR) {
        KVModule_ReplyWithError(ctx,
            "Failed to convert string to unsigned long long");
        goto final;
    }
    if (ull2 != ull) {
        char err[4096];
        snprintf(err, 4096,
            "Failed to convert string to unsigned long long (%llu != %llu)",
            ull2,
            ull);
        KVModule_ReplyWithError(ctx, err);
        goto final;
    }
    
    /* Make sure we can't convert a string more than ULLONG_MAX or less than 0 */
    ullstr = "18446744073709551616";
    KVModuleString *s3 = KVModule_CreateString(ctx, ullstr, strlen(ullstr));
    unsigned long long ull3;
    if (KVModule_StringToULongLong(s3, &ull3) == KVMODULE_OK) {
        KVModule_ReplyWithError(ctx, "Invalid string successfully converted to unsigned long long");
        KVModule_FreeString(ctx, s3);
        goto final;
    }
    KVModule_FreeString(ctx, s3);
    ullstr = "-1";
    KVModuleString *s4 = KVModule_CreateString(ctx, ullstr, strlen(ullstr));
    unsigned long long ull4;
    if (KVModule_StringToULongLong(s4, &ull4) == KVMODULE_OK) {
        KVModule_ReplyWithError(ctx, "Invalid string successfully converted to unsigned long long");
        KVModule_FreeString(ctx, s4);
        goto final;
    }
    KVModule_FreeString(ctx, s4);
   
    KVModule_ReplyWithSimpleString(ctx, "ok");

final:
    KVModule_FreeString(ctx, s1);
    KVModule_FreeString(ctx, s2);
    return KVMODULE_OK;
}

int test_malloc_api(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);

    void *p;

    p = KVModule_TryAlloc(1024);
    memset(p, 0, 1024);
    KVModule_Free(p);

    p = KVModule_TryCalloc(1, 1024);
    memset(p, 1, 1024);

    p = KVModule_TryRealloc(p, 5 * 1024);
    memset(p, 1, 5 * 1024);
    KVModule_Free(p);

    KVModule_ReplyWithSimpleString(ctx, "OK");
    return KVMODULE_OK;
}

int test_keyslot(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    /* Static check of the ClusterKeySlot + ClusterCanonicalKeyNameInSlot
     * round-trip for all slots. */
    for (unsigned int slot = 0; slot < 16384; slot++) {
        const char *tag = KVModule_ClusterCanonicalKeyNameInSlot(slot);
        KVModuleString *key = KVModule_CreateStringPrintf(ctx, "x{%s}y", tag);
        assert(slot == KVModule_ClusterKeySlot(key));

        size_t keylen;
        const char *keyptr = KVModule_StringPtrLen(key, &keylen);
        assert(slot == KVModule_ClusterKeySlotC(keyptr, keylen));

        KVModule_FreeString(ctx, key);
    }
    if (argc != 2){
        return KVModule_WrongArity(ctx);
    }
    unsigned int slot = KVModule_ClusterKeySlot(argv[1]);
    return KVModule_ReplyWithLongLong(ctx, slot);
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    if (KVModule_Init(ctx,"misc",1,KVMODULE_APIVER_1)== KVMODULE_ERR)
        return KVMODULE_ERR;

    if(KVModule_SubscribeToKeyspaceEvents(ctx, KVMODULE_NOTIFY_KEY_MISS | KVMODULE_NOTIFY_EXPIRED, KeySpace_NotificationModuleKeyMissExpired) != KVMODULE_OK){
        return KVMODULE_ERR;
    }

    if (KVModule_CreateCommand(ctx,"test.call_generic", test_call_generic,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"test.call_info", test_call_info,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"test.ld_conversion", test_ld_conv, "",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"test.ull_conversion", test_ull_conv, "",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"test.flushall", test_flushall,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"test.dbsize", test_dbsize,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"test.randomkey", test_randomkey,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"test.keyexists", test_keyexists,"",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"test.setlru", test_setlru,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"test.getlru", test_getlru,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"test.setlfu", test_setlfu,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"test.getlfu", test_getlfu,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"test.clientinfo", test_clientinfo,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"test.getname", test_getname,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"test.setname", test_setname,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"test.serverversion", test_serverversion,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"test.getclientcert", test_getclientcert,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"test.log_tsctx", test_log_tsctx,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    /* Add a command with ':' in it's name, so that we can check commandstats sanitization. */
    if (KVModule_CreateCommand(ctx,"test.weird:cmd", test_weird_cmd,"readonly",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"test.monotonic_time", test_monotonic_time,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx, "test.rm_call", test_rm_call,"allow-stale", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx, "test.rm_call_flags", test_rm_call_flags,"allow-stale", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx, "test.rm_call_replicate", test_rm_call_replicate,"allow-stale", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx, "test.silent_open_key", test_open_key_no_effects,"", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx, "test.get_n_events", test_get_n_events,"", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx, "test.clear_n_events", test_clear_n_events,"", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx, "test.malloc_api", test_malloc_api,"", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx, "test.keyslot", test_keyslot, "", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}
