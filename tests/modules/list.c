#include "kvmodule.h"
#include <assert.h>
#include <errno.h>
#include <strings.h>

/* LIST.GETALL key [REVERSE] */
int list_getall(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc < 2 || argc > 3) return KVModule_WrongArity(ctx);
    int reverse = (argc == 3 &&
                   !strcasecmp(KVModule_StringPtrLen(argv[2], NULL),
                               "REVERSE"));
    KVModule_AutoMemory(ctx);
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_READ);
    if (KVModule_KeyType(key) != KVMODULE_KEYTYPE_LIST) {
        return KVModule_ReplyWithError(ctx, KVMODULE_ERRORMSG_WRONGTYPE);
    }
    long n = KVModule_ValueLength(key);
    KVModule_ReplyWithArray(ctx, n);
    if (!reverse) {
        for (long i = 0; i < n; i++) {
            KVModuleString *elem = KVModule_ListGet(key, i);
            KVModule_ReplyWithString(ctx, elem);
            KVModule_FreeString(ctx, elem);
        }
    } else {
        for (long i = -1; i >= -n; i--) {
            KVModuleString *elem = KVModule_ListGet(key, i);
            KVModule_ReplyWithString(ctx, elem);
            KVModule_FreeString(ctx, elem);
        }
    }

    /* Test error condition: index out of bounds */
    assert(KVModule_ListGet(key, n) == NULL);
    assert(errno == EDOM); /* no more elements in list */

    /* KVModule_CloseKey(key); //implicit, done by auto memory */
    return KVMODULE_OK;
}

/* LIST.EDIT key [REVERSE] cmdstr [value ..]
 *
 * cmdstr is a string of the following characters:
 *
 *     k -- keep
 *     d -- delete
 *     i -- insert value from args
 *     r -- replace with value from args
 *
 * The number of occurrences of "i" and "r" in cmdstr) should correspond to the
 * number of args after cmdstr.
 *
 * Reply with a RESP3 Map, containing the number of edits (inserts, replaces, deletes)
 * performed, as well as the last index and the entry it points to.
 */
int list_edit(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc < 3) return KVModule_WrongArity(ctx);
    KVModule_AutoMemory(ctx);
    int argpos = 1; /* the next arg */

    /* key */
    int keymode = KVMODULE_READ | KVMODULE_WRITE;
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[argpos++], keymode);
    if (KVModule_KeyType(key) != KVMODULE_KEYTYPE_LIST) {
        return KVModule_ReplyWithError(ctx, KVMODULE_ERRORMSG_WRONGTYPE);
    }

    /* REVERSE */
    int reverse = 0;
    if (argc >= 4 &&
        !strcasecmp(KVModule_StringPtrLen(argv[argpos], NULL), "REVERSE")) {
        reverse = 1;
        argpos++;
    }

    /* cmdstr */
    size_t cmdstr_len;
    const char *cmdstr = KVModule_StringPtrLen(argv[argpos++], &cmdstr_len);

    /* validate cmdstr vs. argc */
    long num_req_args = 0;
    long min_list_length = 0;
    for (size_t cmdpos = 0; cmdpos < cmdstr_len; cmdpos++) {
        char c = cmdstr[cmdpos];
        if (c == 'i' || c == 'r') num_req_args++;
        if (c == 'd' || c == 'r' || c == 'k') min_list_length++;
    }
    if (argc < argpos + num_req_args) {
        return KVModule_ReplyWithError(ctx, "ERR too few args");
    }
    if ((long)KVModule_ValueLength(key) < min_list_length) {
        return KVModule_ReplyWithError(ctx, "ERR list too short");
    }

    /* Iterate over the chars in cmdstr (edit instructions) */
    long long num_inserts = 0, num_deletes = 0, num_replaces = 0;
    long index = reverse ? -1 : 0;
    KVModuleString *value;

    for (size_t cmdpos = 0; cmdpos < cmdstr_len; cmdpos++) {
        switch (cmdstr[cmdpos]) {
        case 'i': /* insert */
            value = argv[argpos++];
            assert(KVModule_ListInsert(key, index, value) == KVMODULE_OK);
            index += reverse ? -1 : 1;
            num_inserts++;
            break;
        case 'd': /* delete */
            assert(KVModule_ListDelete(key, index) == KVMODULE_OK);
            num_deletes++;
            break;
        case 'r': /* replace */
            value = argv[argpos++];
            assert(KVModule_ListSet(key, index, value) == KVMODULE_OK);
            index += reverse ? -1 : 1;
            num_replaces++;
            break;
        case 'k': /* keep */
            index += reverse ? -1 : 1;
            break;
        }
    }

    KVModuleString *v = KVModule_ListGet(key, index);
    KVModule_ReplyWithMap(ctx, v ? 5 : 4);
    KVModule_ReplyWithCString(ctx, "i");
    KVModule_ReplyWithLongLong(ctx, num_inserts);
    KVModule_ReplyWithCString(ctx, "d");
    KVModule_ReplyWithLongLong(ctx, num_deletes);
    KVModule_ReplyWithCString(ctx, "r");
    KVModule_ReplyWithLongLong(ctx, num_replaces);
    KVModule_ReplyWithCString(ctx, "index");
    KVModule_ReplyWithLongLong(ctx, index);
    if (v) {
        KVModule_ReplyWithCString(ctx, "entry");
        KVModule_ReplyWithString(ctx, v);
        KVModule_FreeString(ctx, v);
    } 

    KVModule_CloseKey(key);
    return KVMODULE_OK;
}

/* Reply based on errno as set by the List API functions. */
static int replyByErrno(KVModuleCtx *ctx) {
    switch (errno) {
    case EDOM:
        return KVModule_ReplyWithError(ctx, "ERR index out of bounds");
    case ENOTSUP:
        return KVModule_ReplyWithError(ctx, KVMODULE_ERRORMSG_WRONGTYPE);
    default: assert(0); /* Can't happen */
    }
}

/* LIST.GET key index */
int list_get(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 3) return KVModule_WrongArity(ctx);
    long long index;
    if (KVModule_StringToLongLong(argv[2], &index) != KVMODULE_OK) {
        return KVModule_ReplyWithError(ctx, "ERR index must be a number");
    }
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_READ);
    KVModuleString *value = KVModule_ListGet(key, index);
    if (value) {
        KVModule_ReplyWithString(ctx, value);
        KVModule_FreeString(ctx, value);
    } else {
        replyByErrno(ctx);
    }
    KVModule_CloseKey(key);
    return KVMODULE_OK;
}

/* LIST.SET key index value */
int list_set(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 4) return KVModule_WrongArity(ctx);
    long long index;
    if (KVModule_StringToLongLong(argv[2], &index) != KVMODULE_OK) {
        KVModule_ReplyWithError(ctx, "ERR index must be a number");
        return KVMODULE_OK;
    }
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_WRITE);
    if (KVModule_ListSet(key, index, argv[3]) == KVMODULE_OK) {
        KVModule_ReplyWithSimpleString(ctx, "OK");
    } else {
        replyByErrno(ctx);
    }
    KVModule_CloseKey(key);
    return KVMODULE_OK;
}

/* LIST.INSERT key index value
 *
 * If index is negative, value is inserted after, otherwise before the element
 * at index.
 */
int list_insert(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 4) return KVModule_WrongArity(ctx);
    long long index;
    if (KVModule_StringToLongLong(argv[2], &index) != KVMODULE_OK) {
        KVModule_ReplyWithError(ctx, "ERR index must be a number");
        return KVMODULE_OK;
    }
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_WRITE);
    if (KVModule_ListInsert(key, index, argv[3]) == KVMODULE_OK) {
        KVModule_ReplyWithSimpleString(ctx, "OK");
    } else {
        replyByErrno(ctx);
    }
    KVModule_CloseKey(key);
    return KVMODULE_OK;
}

/* LIST.DELETE key index */
int list_delete(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 3) return KVModule_WrongArity(ctx);
    long long index;
    if (KVModule_StringToLongLong(argv[2], &index) != KVMODULE_OK) {
        KVModule_ReplyWithError(ctx, "ERR index must be a number");
        return KVMODULE_OK;
    }
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_WRITE);
    if (KVModule_ListDelete(key, index) == KVMODULE_OK) {
        KVModule_ReplyWithSimpleString(ctx, "OK");
    } else {
        replyByErrno(ctx);
    }
    KVModule_CloseKey(key);
    return KVMODULE_OK;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    if (KVModule_Init(ctx, "list", 1, KVMODULE_APIVER_1) == KVMODULE_OK &&
        KVModule_CreateCommand(ctx, "list.getall", list_getall, "",
                                  1, 1, 1) == KVMODULE_OK &&
        KVModule_CreateCommand(ctx, "list.edit", list_edit, "write",
                                  1, 1, 1) == KVMODULE_OK &&
        KVModule_CreateCommand(ctx, "list.get", list_get, "write",
                                  1, 1, 1) == KVMODULE_OK &&
        KVModule_CreateCommand(ctx, "list.set", list_set, "write",
                                  1, 1, 1) == KVMODULE_OK &&
        KVModule_CreateCommand(ctx, "list.insert", list_insert, "write",
                                  1, 1, 1) == KVMODULE_OK &&
        KVModule_CreateCommand(ctx, "list.delete", list_delete, "write",
                                  1, 1, 1) == KVMODULE_OK) {
        return KVMODULE_OK;
    } else {
        return KVMODULE_ERR;
    }
}
