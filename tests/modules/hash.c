#include "kvmodule.h"
#include <strings.h>
#include <errno.h>
#include <stdlib.h>

/* If a string is ":deleted:", the special value for deleted hash fields is
 * returned; otherwise the input string is returned. */
static KVModuleString *value_or_delete(KVModuleString *s) {
    if (!strcasecmp(KVModule_StringPtrLen(s, NULL), ":delete:"))
        return KVMODULE_HASH_DELETE;
    else
        return s;
}

/* HASH.SET key flags field1 value1 [field2 value2 ..]
 *
 * Sets 1-4 fields. Returns the same as KVModule_HashSet().
 * Flags is a string of "nxa" where n = NX, x = XX, a = COUNT_ALL.
 * To delete a field, use the value ":delete:".
 */
int hash_set(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc < 5 || argc % 2 == 0 || argc > 11)
        return KVModule_WrongArity(ctx);

    KVModule_AutoMemory(ctx);
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_WRITE);

    size_t flags_len;
    const char *flags_str = KVModule_StringPtrLen(argv[2], &flags_len);
    int flags = KVMODULE_HASH_NONE;
    for (size_t i = 0; i < flags_len; i++) {
        switch (flags_str[i]) {
        case 'n': flags |= KVMODULE_HASH_NX; break;
        case 'x': flags |= KVMODULE_HASH_XX; break;
        case 'a': flags |= KVMODULE_HASH_COUNT_ALL; break;
        }
    }

    /* Test some varargs. (In real-world, use a loop and set one at a time.) */
    int result;
    errno = 0;
    if (argc == 5) {
        result = KVModule_HashSet(key, flags,
                                     argv[3], value_or_delete(argv[4]),
                                     NULL);
    } else if (argc == 7) {
        result = KVModule_HashSet(key, flags,
                                     argv[3], value_or_delete(argv[4]),
                                     argv[5], value_or_delete(argv[6]),
                                     NULL);
    } else if (argc == 9) {
        result = KVModule_HashSet(key, flags,
                                     argv[3], value_or_delete(argv[4]),
                                     argv[5], value_or_delete(argv[6]),
                                     argv[7], value_or_delete(argv[8]),
                                     NULL);
    } else if (argc == 11) {
        result = KVModule_HashSet(key, flags,
                                     argv[3], value_or_delete(argv[4]),
                                     argv[5], value_or_delete(argv[6]),
                                     argv[7], value_or_delete(argv[8]),
                                     argv[9], value_or_delete(argv[10]),
                                     NULL);
    } else {
        return KVModule_ReplyWithError(ctx, "ERR too many fields");
    }

    /* Check errno */
    if (result == 0) {
        if (errno == ENOTSUP)
            return KVModule_ReplyWithError(ctx, KVMODULE_ERRORMSG_WRONGTYPE);
        else
            KVModule_Assert(errno == ENOENT);
    }

    return KVModule_ReplyWithLongLong(ctx, result);
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    if (KVModule_Init(ctx, "hash", 1, KVMODULE_APIVER_1) ==
        KVMODULE_OK &&
        KVModule_CreateCommand(ctx, "hash.set", hash_set, "write",
                                  1, 1, 1) == KVMODULE_OK) {
        return KVMODULE_OK;
    } else {
        return KVMODULE_ERR;
    }
}
