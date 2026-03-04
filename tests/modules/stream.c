#include "kvmodule.h"

#include <string.h>
#include <strings.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>

/* Command which adds a stream entry with automatic ID, like XADD *.
 *
 * Syntax: STREAM.ADD key field1 value1 [ field2 value2 ... ]
 *
 * The response is the ID of the added stream entry or an error message.
 */
int stream_add(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc < 2 || argc % 2 != 0) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_WRITE);
    KVModuleStreamID id;
    if (KVModule_StreamAdd(key, KVMODULE_STREAM_ADD_AUTOID, &id,
                              &argv[2], (argc-2)/2) == KVMODULE_OK) {
        KVModuleString *id_str = KVModule_CreateStringFromStreamID(ctx, &id);
        KVModule_ReplyWithString(ctx, id_str);
        KVModule_FreeString(ctx, id_str);
    } else {
        KVModule_ReplyWithError(ctx, "ERR StreamAdd failed");
    }
    KVModule_CloseKey(key);
    return KVMODULE_OK;
}

/* Command which adds a stream entry N times.
 *
 * Syntax: STREAM.ADD key N field1 value1 [ field2 value2 ... ]
 *
 * Returns the number of successfully added entries.
 */
int stream_addn(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc < 3 || argc % 2 == 0) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    long long n, i;
    if (KVModule_StringToLongLong(argv[2], &n) == KVMODULE_ERR) {
        KVModule_ReplyWithError(ctx, "N must be a number");
        return KVMODULE_OK;
    }

    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_WRITE);
    for (i = 0; i < n; i++) {
        if (KVModule_StreamAdd(key, KVMODULE_STREAM_ADD_AUTOID, NULL,
                                  &argv[3], (argc-3)/2) == KVMODULE_ERR)
            break;
    }
    KVModule_ReplyWithLongLong(ctx, i);
    KVModule_CloseKey(key);
    return KVMODULE_OK;
}

/* STREAM.DELETE key stream-id */
int stream_delete(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 3) return KVModule_WrongArity(ctx);
    KVModuleStreamID id;
    if (KVModule_StringToStreamID(argv[2], &id) != KVMODULE_OK) {
        return KVModule_ReplyWithError(ctx, "Invalid stream ID");
    }
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_WRITE);
    if (KVModule_StreamDelete(key, &id) == KVMODULE_OK) {
        KVModule_ReplyWithSimpleString(ctx, "OK");
    } else {
        KVModule_ReplyWithError(ctx, "ERR StreamDelete failed");
    }
    KVModule_CloseKey(key);
    return KVMODULE_OK;
}

/* STREAM.RANGE key start-id end-id
 *
 * Returns an array of stream items. Each item is an array on the form
 * [stream-id, [field1, value1, field2, value2, ...]].
 *
 * A funny side-effect used for testing RM_StreamIteratorDelete() is that if any
 * entry has a field named "selfdestruct", the stream entry is deleted. It is
 * however included in the results of this command.
 */
int stream_range(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 4) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    KVModuleStreamID startid, endid;
    if (KVModule_StringToStreamID(argv[2], &startid) != KVMODULE_OK ||
        KVModule_StringToStreamID(argv[3], &endid) != KVMODULE_OK) {
        KVModule_ReplyWithError(ctx, "Invalid stream ID");
        return KVMODULE_OK;
    }

    /* If startid > endid, we swap and set the reverse flag. */
    int flags = 0;
    if (startid.ms > endid.ms ||
        (startid.ms == endid.ms && startid.seq > endid.seq)) {
        KVModuleStreamID tmp = startid;
        startid = endid;
        endid = tmp;
        flags |= KVMODULE_STREAM_ITERATOR_REVERSE;
    }

    /* Open key and start iterator. */
    int openflags = KVMODULE_READ | KVMODULE_WRITE;
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], openflags);
    if (KVModule_StreamIteratorStart(key, flags,
                                        &startid, &endid) != KVMODULE_OK) {
        /* Key is not a stream, etc. */
        KVModule_ReplyWithError(ctx, "ERR StreamIteratorStart failed");
        KVModule_CloseKey(key);
        return KVMODULE_OK;
    }

    /* Check error handling: Delete current entry when no current entry. */
    assert(KVModule_StreamIteratorDelete(key) ==
           KVMODULE_ERR);
    assert(errno == ENOENT);

    /* Check error handling: Fetch fields when no current entry. */
    assert(KVModule_StreamIteratorNextField(key, NULL, NULL) ==
           KVMODULE_ERR);
    assert(errno == ENOENT);

    /* Return array. */
    KVModule_ReplyWithArray(ctx, KVMODULE_POSTPONED_LEN);
    KVModule_AutoMemory(ctx);
    KVModuleStreamID id;
    long numfields;
    long len = 0;
    while (KVModule_StreamIteratorNextID(key, &id,
                                            &numfields) == KVMODULE_OK) {
        KVModule_ReplyWithArray(ctx, 2);
        KVModuleString *id_str = KVModule_CreateStringFromStreamID(ctx, &id);
        KVModule_ReplyWithString(ctx, id_str);
        KVModule_ReplyWithArray(ctx, numfields * 2);
        int delete = 0;
        KVModuleString *field, *value;
        for (long i = 0; i < numfields; i++) {
            assert(KVModule_StreamIteratorNextField(key, &field, &value) ==
                   KVMODULE_OK);
            KVModule_ReplyWithString(ctx, field);
            KVModule_ReplyWithString(ctx, value);
            /* check if this is a "selfdestruct" field */
            size_t field_len;
            const char *field_str = KVModule_StringPtrLen(field, &field_len);
            if (!strncmp(field_str, "selfdestruct", field_len)) delete = 1;
        }
        if (delete) {
            assert(KVModule_StreamIteratorDelete(key) == KVMODULE_OK);
        }
        /* check error handling: no more fields to fetch */
        assert(KVModule_StreamIteratorNextField(key, &field, &value) ==
               KVMODULE_ERR);
        assert(errno == ENOENT);
        len++;
    }
    KVModule_ReplySetArrayLength(ctx, len);
    KVModule_StreamIteratorStop(key);
    KVModule_CloseKey(key);
    return KVMODULE_OK;
}

/*
 * STREAM.TRIM key (MAXLEN (=|~) length | MINID (=|~) id)
 */
int stream_trim(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 5) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    /* Parse args */
    int trim_by_id = 0; /* 0 = maxlen, 1 = minid */
    long long maxlen;
    KVModuleStreamID minid;
    size_t arg_len;
    const char *arg = KVModule_StringPtrLen(argv[2], &arg_len);
    if (!strcasecmp(arg, "minid")) {
        trim_by_id = 1;
        if (KVModule_StringToStreamID(argv[4], &minid) != KVMODULE_OK) {
            KVModule_ReplyWithError(ctx, "ERR Invalid stream ID");
            return KVMODULE_OK;
        }
    } else if (!strcasecmp(arg, "maxlen")) {
        if (KVModule_StringToLongLong(argv[4], &maxlen) == KVMODULE_ERR) {
            KVModule_ReplyWithError(ctx, "ERR Maxlen must be a number");
            return KVMODULE_OK;
        }
    } else {
        KVModule_ReplyWithError(ctx, "ERR Invalid arguments");
        return KVMODULE_OK;
    }

    /* Approx or exact */
    int flags;
    arg = KVModule_StringPtrLen(argv[3], &arg_len);
    if (arg_len == 1 && arg[0] == '~') {
        flags = KVMODULE_STREAM_TRIM_APPROX;
    } else if (arg_len == 1 && arg[0] == '=') {
        flags = 0;
    } else {
        KVModule_ReplyWithError(ctx, "ERR Invalid approx-or-exact mark");
        return KVMODULE_OK;
    }

    /* Trim */
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_WRITE);
    long long trimmed;
    if (trim_by_id) {
        trimmed = KVModule_StreamTrimByID(key, flags, &minid);
    } else {
        trimmed = KVModule_StreamTrimByLength(key, flags, maxlen);
    }

    /* Return result */
    if (trimmed < 0) {
        KVModule_ReplyWithError(ctx, "ERR Trimming failed");
    } else {
        KVModule_ReplyWithLongLong(ctx, trimmed);
    }
    KVModule_CloseKey(key);
    return KVMODULE_OK;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    if (KVModule_Init(ctx, "stream", 1, KVMODULE_APIVER_1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "stream.add", stream_add, "write",
                                  1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx, "stream.addn", stream_addn, "write",
                                  1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx, "stream.delete", stream_delete, "write",
                                  1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx, "stream.range", stream_range, "write",
                                  1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx, "stream.trim", stream_trim, "write",
                                  1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}
