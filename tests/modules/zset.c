#include "kvmodule.h"
#include <math.h>
#include <errno.h>

#define UNUSED(V) ((void) V)

/* ZSET.REM key element
 *
 * Removes an occurrence of an element from a sorted set. Replies with the
 * number of removed elements (0 or 1).
 */
int zset_rem(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 3) return KVModule_WrongArity(ctx);
    KVModule_AutoMemory(ctx);
    int keymode = KVMODULE_READ | KVMODULE_WRITE;
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], keymode);
    int deleted;
    if (KVModule_ZsetRem(key, argv[2], &deleted) == KVMODULE_OK)
        return KVModule_ReplyWithLongLong(ctx, deleted);
    else
        return KVModule_ReplyWithError(ctx, "ERR ZsetRem failed");
}

/* ZSET.ADD key score member
 *
 * Adds a specified member with the specified score to the sorted
 * set stored at key.
 */
int zset_add(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 4) return KVModule_WrongArity(ctx);
    KVModule_AutoMemory(ctx);
    int keymode = KVMODULE_READ | KVMODULE_WRITE;
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], keymode);

    size_t len;
    double score;
    char *endptr;
    const char *str = KVModule_StringPtrLen(argv[2], &len);
    score = strtod(str, &endptr);
    if (*endptr != '\0' || errno == ERANGE)
        return KVModule_ReplyWithError(ctx, "value is not a valid float");

    if (KVModule_ZsetAdd(key, score, argv[3], NULL) == KVMODULE_OK)
        return KVModule_ReplyWithSimpleString(ctx, "OK");
    else
        return KVModule_ReplyWithError(ctx, "ERR ZsetAdd failed");
}

/* ZSET.INCRBY key member increment
 *
 * Increments the score stored at member in the sorted set stored at key by increment.
 * Replies with the new score of this element.
 */
int zset_incrby(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 4) return KVModule_WrongArity(ctx);
    KVModule_AutoMemory(ctx);
    int keymode = KVMODULE_READ | KVMODULE_WRITE;
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], keymode);

    size_t len;
    double score, newscore;
    char *endptr;
    const char *str = KVModule_StringPtrLen(argv[3], &len);
    score = strtod(str, &endptr);
    if (*endptr != '\0' || errno == ERANGE)
        return KVModule_ReplyWithError(ctx, "value is not a valid float");

    if (KVModule_ZsetIncrby(key, score, argv[2], NULL, &newscore) == KVMODULE_OK)
        return KVModule_ReplyWithDouble(ctx, newscore);
    else
        return KVModule_ReplyWithError(ctx, "ERR ZsetIncrby failed");
}

static int zset_internal_rangebylex(KVModuleCtx *ctx, KVModuleString **argv, int reverse) {
    KVModule_AutoMemory(ctx);
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_READ);
    if (KVModule_KeyType(key) != KVMODULE_KEYTYPE_ZSET) {
        return KVModule_ReplyWithError(ctx, KVMODULE_ERRORMSG_WRONGTYPE);
    }

    if (reverse) {
        if (KVModule_ZsetLastInLexRange(key, argv[2], argv[3]) != KVMODULE_OK) {
            return KVModule_ReplyWithError(ctx, "invalid range");
        }
    } else {
        if (KVModule_ZsetFirstInLexRange(key, argv[2], argv[3]) != KVMODULE_OK) {
            return KVModule_ReplyWithError(ctx, "invalid range");
        }
    }

    int arraylen = 0;
    KVModule_ReplyWithArray(ctx, KVMODULE_POSTPONED_LEN);
    while (!KVModule_ZsetRangeEndReached(key)) {
        KVModuleString *ele = KVModule_ZsetRangeCurrentElement(key, NULL);
        KVModule_ReplyWithString(ctx, ele);
        KVModule_FreeString(ctx, ele);
        if (reverse) {
            KVModule_ZsetRangePrev(key);
        } else {
            KVModule_ZsetRangeNext(key);
        }
        arraylen += 1;
    }
    KVModule_ZsetRangeStop(key);
    KVModule_CloseKey(key);
    KVModule_ReplySetArrayLength(ctx, arraylen);
    return KVMODULE_OK;
}

/* ZSET.rangebylex key min max
 *
 * Returns members in a sorted set within a lexicographical range.
 */
int zset_rangebylex(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 4)
        return KVModule_WrongArity(ctx);
    return zset_internal_rangebylex(ctx, argv, 0);
}

/* ZSET.revrangebylex key min max
 *
 * Returns members in a sorted set within a lexicographical range in reverse order.
 */
int zset_revrangebylex(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 4)
        return KVModule_WrongArity(ctx);
    return zset_internal_rangebylex(ctx, argv, 1);
}

static void zset_members_cb(KVModuleKey *key, KVModuleString *field, KVModuleString *value, void *privdata) {
    UNUSED(key);
    UNUSED(value);
    KVModuleCtx *ctx = (KVModuleCtx *)privdata;
    KVModule_ReplyWithString(ctx, field);
}

/* ZSET.members key
 *
 * Returns members in a sorted set.
 */
int zset_members(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2)
        return KVModule_WrongArity(ctx);
    KVModule_AutoMemory(ctx);

    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_READ);
    if (KVModule_KeyType(key) != KVMODULE_KEYTYPE_ZSET) {
        return KVModule_ReplyWithError(ctx, KVMODULE_ERRORMSG_WRONGTYPE);
    }

    KVModule_ReplyWithArray(ctx, KVModule_ValueLength(key));
    KVModuleScanCursor *c = KVModule_ScanCursorCreate();
    while (KVModule_ScanKey(key, c, zset_members_cb, ctx));
    KVModule_CloseKey(key);
    KVModule_ScanCursorDestroy(c);
    return KVMODULE_OK;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    if (KVModule_Init(ctx, "zset", 1, KVMODULE_APIVER_1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "zset.rem", zset_rem, "write",
                                  1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "zset.add", zset_add, "write",
                                  1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "zset.incrby", zset_incrby, "write",
                                  1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "zset.rangebylex", zset_rangebylex, "readonly",
                                  1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "zset.revrangebylex", zset_revrangebylex, "readonly",
                                  1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "zset.members", zset_members, "readonly",
                                  1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}
