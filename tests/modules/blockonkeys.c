#include "kvmodule.h"

#include <string.h>
#include <strings.h>
#include <assert.h>
#include <unistd.h>

#define UNUSED(V) ((void) V)

#define LIST_SIZE 1024

/* The FSL (Fixed-Size List) data type is a low-budget imitation of the
 * list data type, in order to test list-like commands implemented
 * by a module.
 * Examples: FSL.PUSH, FSL.BPOP, etc. */

typedef struct {
    long long list[LIST_SIZE];
    long long length;
} fsl_t; /* Fixed-size list */

static KVModuleType *fsltype = NULL;

fsl_t *fsl_type_create(void) {
    fsl_t *o;
    o = KVModule_Alloc(sizeof(*o));
    o->length = 0;
    return o;
}

void fsl_type_free(fsl_t *o) {
    KVModule_Free(o);
}

/* ========================== "fsltype" type methods ======================= */

void *fsl_rdb_load(KVModuleIO *rdb, int encver) {
    if (encver != 0) {
        return NULL;
    }
    fsl_t *fsl = fsl_type_create();
    fsl->length = KVModule_LoadUnsigned(rdb);
    for (long long i = 0; i < fsl->length; i++)
        fsl->list[i] = KVModule_LoadSigned(rdb);
    return fsl;
}

void fsl_rdb_save(KVModuleIO *rdb, void *value) {
    fsl_t *fsl = value;
    KVModule_SaveUnsigned(rdb,fsl->length);
    for (long long i = 0; i < fsl->length; i++)
        KVModule_SaveSigned(rdb, fsl->list[i]);
}

void fsl_aofrw(KVModuleIO *aof, KVModuleString *key, void *value) {
    fsl_t *fsl = value;
    for (long long i = 0; i < fsl->length; i++)
        KVModule_EmitAOF(aof, "FSL.PUSH","sl", key, fsl->list[i]);
}

void fsl_free(void *value) {
    fsl_type_free(value);
}

/* ========================== helper methods ======================= */

/* Wrapper to the boilerplate code of opening a key, checking its type, etc.
 * Returns 0 if `keyname` exists in the dataset, but it's of the wrong type (i.e. not FSL) */
int get_fsl(KVModuleCtx *ctx, KVModuleString *keyname, int mode, int create, fsl_t **fsl, int reply_on_failure) {
    *fsl = NULL;
    KVModuleKey *key = KVModule_OpenKey(ctx, keyname, mode);

    if (KVModule_KeyType(key) != KVMODULE_KEYTYPE_EMPTY) {
        /* Key exists */
        if (KVModule_ModuleTypeGetType(key) != fsltype) {
            /* Key is not FSL */
            KVModule_CloseKey(key);
            if (reply_on_failure)
                KVModule_ReplyWithError(ctx, KVMODULE_ERRORMSG_WRONGTYPE);
            KVModuleCallReply *reply = KVModule_Call(ctx, "INCR", "c", "fsl_wrong_type");
            KVModule_FreeCallReply(reply);
            return 0;
        }

        *fsl = KVModule_ModuleTypeGetValue(key);
        if (*fsl && !(*fsl)->length && mode & KVMODULE_WRITE) {
            /* Key exists, but it's logically empty */
            if (create) {
                create = 0; /* No need to create, key exists in its basic state */
            } else {
                KVModule_DeleteKey(key);
                *fsl = NULL;
            }
        } else {
            /* Key exists, and has elements in it - no need to create anything */
            create = 0;
        }
    }

    if (create) {
        *fsl = fsl_type_create();
        KVModule_ModuleTypeSetValue(key, fsltype, *fsl);
    }

    KVModule_CloseKey(key);
    return 1;
}

/* ========================== commands ======================= */

/* FSL.PUSH <key> <int> - Push an integer to the fixed-size list (to the right).
 * It must be greater than the element in the head of the list. */
int fsl_push(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 3)
        return KVModule_WrongArity(ctx);

    long long ele;
    if (KVModule_StringToLongLong(argv[2],&ele) != KVMODULE_OK)
        return KVModule_ReplyWithError(ctx,"ERR invalid integer");

    fsl_t *fsl;
    if (!get_fsl(ctx, argv[1], KVMODULE_WRITE, 1, &fsl, 1))
        return KVMODULE_OK;

    if (fsl->length == LIST_SIZE)
        return KVModule_ReplyWithError(ctx,"ERR list is full");

    if (fsl->length != 0 && fsl->list[fsl->length-1] >= ele)
        return KVModule_ReplyWithError(ctx,"ERR new element has to be greater than the head element");

    fsl->list[fsl->length++] = ele;
    KVModule_SignalKeyAsReady(ctx, argv[1]);

    KVModule_ReplicateVerbatim(ctx);

    return KVModule_ReplyWithSimpleString(ctx, "OK");
}

typedef struct {
    KVModuleString *keyname;
    long long ele;
} timer_data_t;

static void timer_callback(KVModuleCtx *ctx, void *data)
{
    timer_data_t *td = data;

    fsl_t *fsl;
    if (!get_fsl(ctx, td->keyname, KVMODULE_WRITE, 1, &fsl, 1))
        return;

    if (fsl->length == LIST_SIZE)
        return; /* list is full */

    if (fsl->length != 0 && fsl->list[fsl->length-1] >= td->ele)
        return; /* new element has to be greater than the head element */

    fsl->list[fsl->length++] = td->ele;
    KVModule_SignalKeyAsReady(ctx, td->keyname);

    KVModule_Replicate(ctx, "FSL.PUSH", "sl", td->keyname, td->ele);

    KVModule_FreeString(ctx, td->keyname);
    KVModule_Free(td);
}

/* FSL.PUSHTIMER <key> <int> <period-in-ms> - Push the number 9000 to the fixed-size list (to the right).
 * It must be greater than the element in the head of the list. */
int fsl_pushtimer(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    if (argc != 4)
        return KVModule_WrongArity(ctx);

    long long ele;
    if (KVModule_StringToLongLong(argv[2],&ele) != KVMODULE_OK)
        return KVModule_ReplyWithError(ctx,"ERR invalid integer");

    long long period;
    if (KVModule_StringToLongLong(argv[3],&period) != KVMODULE_OK)
        return KVModule_ReplyWithError(ctx,"ERR invalid period");

    fsl_t *fsl;
    if (!get_fsl(ctx, argv[1], KVMODULE_WRITE, 1, &fsl, 1))
        return KVMODULE_OK;

    if (fsl->length == LIST_SIZE)
        return KVModule_ReplyWithError(ctx,"ERR list is full");

    timer_data_t *td = KVModule_Alloc(sizeof(*td));
    td->keyname = argv[1];
    KVModule_RetainString(ctx, td->keyname);
    td->ele = ele;

    KVModuleTimerID id = KVModule_CreateTimer(ctx, period, timer_callback, td);
    KVModule_ReplyWithLongLong(ctx, id);

    return KVMODULE_OK;
}

int bpop_reply_callback(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    KVModuleString *keyname = KVModule_GetBlockedClientReadyKey(ctx);

    fsl_t *fsl;
    if (!get_fsl(ctx, keyname, KVMODULE_WRITE, 0, &fsl, 0) || !fsl)
        return KVMODULE_ERR;

    KVModule_Assert(fsl->length);
    KVModule_ReplyWithLongLong(ctx, fsl->list[--fsl->length]);

    /* I'm lazy so i'll replicate a potentially blocking command, it shouldn't block in this flow. */
    KVModule_ReplicateVerbatim(ctx);
    return KVMODULE_OK;
}

int bpop_timeout_callback(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    return KVModule_ReplyWithSimpleString(ctx, "Request timedout");
}

/* FSL.BPOP <key> <timeout> [NO_TO_CB]- Block clients until list has two or more elements.
 * When that happens, unblock client and pop the last two elements (from the right). */
int fsl_bpop(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc < 3)
        return KVModule_WrongArity(ctx);

    long long timeout;
    if (KVModule_StringToLongLong(argv[2],&timeout) != KVMODULE_OK || timeout < 0)
        return KVModule_ReplyWithError(ctx,"ERR invalid timeout");

    int to_cb = 1;
    if (argc == 4) {
        if (strcasecmp("NO_TO_CB", KVModule_StringPtrLen(argv[3], NULL)))
            return KVModule_ReplyWithError(ctx,"ERR invalid argument");
        to_cb = 0;
    }

    fsl_t *fsl;
    if (!get_fsl(ctx, argv[1], KVMODULE_WRITE, 0, &fsl, 1))
        return KVMODULE_OK;

    if (!fsl) {
        KVModule_BlockClientOnKeys(ctx, bpop_reply_callback, to_cb ? bpop_timeout_callback : NULL,
                                      NULL, timeout, &argv[1], 1, NULL);
    } else {
        KVModule_Assert(fsl->length);
        KVModule_ReplyWithLongLong(ctx, fsl->list[--fsl->length]);
        /* I'm lazy so i'll replicate a potentially blocking command, it shouldn't block in this flow. */
        KVModule_ReplicateVerbatim(ctx);
    }

    return KVMODULE_OK;
}

int bpopgt_reply_callback(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    KVModuleString *keyname = KVModule_GetBlockedClientReadyKey(ctx);
    long long *pgt = KVModule_GetBlockedClientPrivateData(ctx);

    fsl_t *fsl;
    if (!get_fsl(ctx, keyname, KVMODULE_WRITE, 0, &fsl, 0) || !fsl)
        return KVModule_ReplyWithError(ctx,"UNBLOCKED key no longer exists");

    if (fsl->list[fsl->length-1] <= *pgt)
        return KVMODULE_ERR;

    KVModule_Assert(fsl->length);
    KVModule_ReplyWithLongLong(ctx, fsl->list[--fsl->length]);
    /* I'm lazy so i'll replicate a potentially blocking command, it shouldn't block in this flow. */
    KVModule_ReplicateVerbatim(ctx);
    return KVMODULE_OK;
}

int bpopgt_timeout_callback(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    return KVModule_ReplyWithSimpleString(ctx, "Request timedout");
}

void bpopgt_free_privdata(KVModuleCtx *ctx, void *privdata) {
    KVMODULE_NOT_USED(ctx);
    KVModule_Free(privdata);
}

/* FSL.BPOPGT <key> <gt> <timeout> - Block clients until list has an element greater than <gt>.
 * When that happens, unblock client and pop the last element (from the right). */
int fsl_bpopgt(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 4)
        return KVModule_WrongArity(ctx);

    long long gt;
    if (KVModule_StringToLongLong(argv[2],&gt) != KVMODULE_OK)
        return KVModule_ReplyWithError(ctx,"ERR invalid integer");

    long long timeout;
    if (KVModule_StringToLongLong(argv[3],&timeout) != KVMODULE_OK || timeout < 0)
        return KVModule_ReplyWithError(ctx,"ERR invalid timeout");

    fsl_t *fsl;
    if (!get_fsl(ctx, argv[1], KVMODULE_WRITE, 0, &fsl, 1))
        return KVMODULE_OK;

    if (!fsl)
        return KVModule_ReplyWithError(ctx,"ERR key must exist");

    if (fsl->list[fsl->length-1] <= gt) {
        /* We use malloc so the tests in blockedonkeys.tcl can check for memory leaks */
        long long *pgt = KVModule_Alloc(sizeof(long long));
        *pgt = gt;
        KVModule_BlockClientOnKeysWithFlags(
            ctx, bpopgt_reply_callback, bpopgt_timeout_callback,
            bpopgt_free_privdata, timeout, &argv[1], 1, pgt,
            KVMODULE_BLOCK_UNBLOCK_DELETED);
    } else {
        KVModule_Assert(fsl->length);
        KVModule_ReplyWithLongLong(ctx, fsl->list[--fsl->length]);
        /* I'm lazy so i'll replicate a potentially blocking command, it shouldn't block in this flow. */
        KVModule_ReplicateVerbatim(ctx);
    }

    return KVMODULE_OK;
}

int bpoppush_reply_callback(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    KVModuleString *src_keyname = KVModule_GetBlockedClientReadyKey(ctx);
    KVModuleString *dst_keyname = KVModule_GetBlockedClientPrivateData(ctx);

    fsl_t *src;
    if (!get_fsl(ctx, src_keyname, KVMODULE_WRITE, 0, &src, 0) || !src)
        return KVMODULE_ERR;

    fsl_t *dst;
    if (!get_fsl(ctx, dst_keyname, KVMODULE_WRITE, 1, &dst, 0) || !dst)
        return KVMODULE_ERR;

    KVModule_Assert(src->length);
    long long ele = src->list[--src->length];
    dst->list[dst->length++] = ele;
    KVModule_SignalKeyAsReady(ctx, dst_keyname);
    /* I'm lazy so i'll replicate a potentially blocking command, it shouldn't block in this flow. */
    KVModule_ReplicateVerbatim(ctx);
    return KVModule_ReplyWithLongLong(ctx, ele);
}

int bpoppush_timeout_callback(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    return KVModule_ReplyWithSimpleString(ctx, "Request timedout");
}

void bpoppush_free_privdata(KVModuleCtx *ctx, void *privdata) {
    KVModule_FreeString(ctx, privdata);
}

/* FSL.BPOPPUSH <src> <dst> <timeout> - Block clients until <src> has an element.
 * When that happens, unblock client, pop the last element from <src> and push it to <dst>
 * (from the right). */
int fsl_bpoppush(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 4)
        return KVModule_WrongArity(ctx);

    long long timeout;
    if (KVModule_StringToLongLong(argv[3],&timeout) != KVMODULE_OK || timeout < 0)
        return KVModule_ReplyWithError(ctx,"ERR invalid timeout");

    fsl_t *src;
    if (!get_fsl(ctx, argv[1], KVMODULE_WRITE, 0, &src, 1))
        return KVMODULE_OK;

    if (!src) {
        /* Retain string for reply callback */
        KVModule_RetainString(ctx, argv[2]);
        /* Key is empty, we must block */
        KVModule_BlockClientOnKeys(ctx, bpoppush_reply_callback, bpoppush_timeout_callback,
                                      bpoppush_free_privdata, timeout, &argv[1], 1, argv[2]);
    } else {
        fsl_t *dst;
        if (!get_fsl(ctx, argv[2], KVMODULE_WRITE, 1, &dst, 1))
            return KVMODULE_OK;

        KVModule_Assert(src->length);
        long long ele = src->list[--src->length];
        dst->list[dst->length++] = ele;
        KVModule_SignalKeyAsReady(ctx, argv[2]);
        KVModule_ReplyWithLongLong(ctx, ele);
        /* I'm lazy so i'll replicate a potentially blocking command, it shouldn't block in this flow. */
        KVModule_ReplicateVerbatim(ctx);
    }

    return KVMODULE_OK;
}

/* FSL.GETALL <key> - Reply with an array containing all elements. */
int fsl_getall(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2)
        return KVModule_WrongArity(ctx);

    fsl_t *fsl;
    if (!get_fsl(ctx, argv[1], KVMODULE_READ, 0, &fsl, 1))
        return KVMODULE_OK;

    if (!fsl)
        return KVModule_ReplyWithArray(ctx, 0);

    KVModule_ReplyWithArray(ctx, fsl->length);
    for (int i = 0; i < fsl->length; i++)
        KVModule_ReplyWithLongLong(ctx, fsl->list[i]);
    return KVMODULE_OK;
}

/* Callback for blockonkeys_popall */
int blockonkeys_popall_reply_callback(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argc);
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_WRITE);
    if (KVModule_KeyType(key) == KVMODULE_KEYTYPE_LIST) {
        KVModuleString *elem;
        long len = 0;
        KVModule_ReplyWithArray(ctx, KVMODULE_POSTPONED_ARRAY_LEN);
        while ((elem = KVModule_ListPop(key, KVMODULE_LIST_HEAD)) != NULL) {
            len++;
            KVModule_ReplyWithString(ctx, elem);
            KVModule_FreeString(ctx, elem);
        }
        /* I'm lazy so i'll replicate a potentially blocking command, it shouldn't block in this flow. */
        KVModule_ReplicateVerbatim(ctx);
        KVModule_ReplySetArrayLength(ctx, len);
    } else {
        KVModule_ReplyWithError(ctx, "ERR Not a list");
    }
    KVModule_CloseKey(key);
    return KVMODULE_OK;
}

int blockonkeys_popall_timeout_callback(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    return KVModule_ReplyWithError(ctx, "ERR Timeout");
}

/* BLOCKONKEYS.POPALL key
 *
 * Blocks on an empty key for up to 3 seconds. When unblocked by a list
 * operation like LPUSH, all the elements are popped and returned. Fails with an
 * error on timeout. */
int blockonkeys_popall(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2)
        return KVModule_WrongArity(ctx);

    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_READ);
    if (KVModule_KeyType(key) == KVMODULE_KEYTYPE_EMPTY) {
        KVModule_BlockClientOnKeys(ctx, blockonkeys_popall_reply_callback,
                                      blockonkeys_popall_timeout_callback,
                                      NULL, 3000, &argv[1], 1, NULL);
    } else {
        KVModule_ReplyWithError(ctx, "ERR Key not empty");
    }
    KVModule_CloseKey(key);
    return KVMODULE_OK;
}

/* BLOCKONKEYS.LPUSH key val [val ..]
 * BLOCKONKEYS.LPUSH_UNBLOCK key val [val ..]
 *
 * A module equivalent of LPUSH. If the name LPUSH_UNBLOCK is used,
 * RM_SignalKeyAsReady() is also called. */
int blockonkeys_lpush(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc < 3)
        return KVModule_WrongArity(ctx);

    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_WRITE);
    if (KVModule_KeyType(key) != KVMODULE_KEYTYPE_EMPTY &&
        KVModule_KeyType(key) != KVMODULE_KEYTYPE_LIST) {
        KVModule_ReplyWithError(ctx, KVMODULE_ERRORMSG_WRONGTYPE);
    } else {
        for (int i = 2; i < argc; i++) {
            if (KVModule_ListPush(key, KVMODULE_LIST_HEAD,
                                     argv[i]) != KVMODULE_OK) {
                KVModule_CloseKey(key);
                return KVModule_ReplyWithError(ctx, "ERR Push failed");
            }
        }
    }
    KVModule_CloseKey(key);

    /* signal key as ready if the command is lpush_unblock */
    size_t len;
    const char *str = KVModule_StringPtrLen(argv[0], &len);
    if (!strncasecmp(str, "blockonkeys.lpush_unblock", len)) {
        KVModule_SignalKeyAsReady(ctx, argv[1]);
    }
    KVModule_ReplicateVerbatim(ctx);
    return KVModule_ReplyWithSimpleString(ctx, "OK");
}

/* Callback for the BLOCKONKEYS.BLPOPN command */
int blockonkeys_blpopn_reply_callback(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argc);
    long long n;
    KVModule_StringToLongLong(argv[2], &n);
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_WRITE);
    int result;
    if (KVModule_KeyType(key) == KVMODULE_KEYTYPE_LIST &&
        KVModule_ValueLength(key) >= (size_t)n) {
        KVModule_ReplyWithArray(ctx, n);
        for (long i = 0; i < n; i++) {
            KVModuleString *elem = KVModule_ListPop(key, KVMODULE_LIST_HEAD);
            KVModule_ReplyWithString(ctx, elem);
            KVModule_FreeString(ctx, elem);
        }
        /* I'm lazy so i'll replicate a potentially blocking command, it shouldn't block in this flow. */
        KVModule_ReplicateVerbatim(ctx);
        result = KVMODULE_OK;
    } else if (KVModule_KeyType(key) == KVMODULE_KEYTYPE_LIST ||
               KVModule_KeyType(key) == KVMODULE_KEYTYPE_EMPTY) {
        const char *module_cmd = KVModule_StringPtrLen(argv[0], NULL);
        if (!strcasecmp(module_cmd, "blockonkeys.blpopn_or_unblock"))
            KVModule_UnblockClient(KVModule_GetBlockedClientHandle(ctx), NULL);

        /* continue blocking */
        result = KVMODULE_ERR;
    } else {
        result = KVModule_ReplyWithError(ctx, KVMODULE_ERRORMSG_WRONGTYPE);
    }
    KVModule_CloseKey(key);
    return result;
}

int blockonkeys_blpopn_timeout_callback(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    return KVModule_ReplyWithError(ctx, "ERR Timeout");
}

int blockonkeys_blpopn_abort_callback(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    return KVModule_ReplyWithSimpleString(ctx, "Action aborted");
}

/* BLOCKONKEYS.BLPOPN key N
 *
 * Blocks until key has N elements and then pops them or fails after 3 seconds.
 */
int blockonkeys_blpopn(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc < 3) return KVModule_WrongArity(ctx);

    long long n, timeout = 3000LL;
    if (KVModule_StringToLongLong(argv[2], &n) != KVMODULE_OK) {
        return KVModule_ReplyWithError(ctx, "ERR Invalid N");
    }

    if (argc > 3 ) {
        if (KVModule_StringToLongLong(argv[3], &timeout) != KVMODULE_OK) {
            return KVModule_ReplyWithError(ctx, "ERR Invalid timeout value");
        }
    }
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_WRITE);
    int keytype = KVModule_KeyType(key);
    if (keytype != KVMODULE_KEYTYPE_EMPTY &&
        keytype != KVMODULE_KEYTYPE_LIST) {
        KVModule_ReplyWithError(ctx, KVMODULE_ERRORMSG_WRONGTYPE);
    } else if (keytype == KVMODULE_KEYTYPE_LIST &&
               KVModule_ValueLength(key) >= (size_t)n) {
        KVModule_ReplyWithArray(ctx, n);
        for (long i = 0; i < n; i++) {
            KVModuleString *elem = KVModule_ListPop(key, KVMODULE_LIST_HEAD);
            KVModule_ReplyWithString(ctx, elem);
            KVModule_FreeString(ctx, elem);
        }
        /* I'm lazy so i'll replicate a potentially blocking command, it shouldn't block in this flow. */
        KVModule_ReplicateVerbatim(ctx);
    } else {
        KVModule_BlockClientOnKeys(ctx, blockonkeys_blpopn_reply_callback,
                                      timeout ? blockonkeys_blpopn_timeout_callback : blockonkeys_blpopn_abort_callback,
                                      NULL, timeout, &argv[1], 1, NULL);
    }
    KVModule_CloseKey(key);
    return KVMODULE_OK;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx, "blockonkeys", 1, KVMODULE_APIVER_1)== KVMODULE_ERR)
        return KVMODULE_ERR;

    KVModuleTypeMethods tm = {
        .version = KVMODULE_TYPE_METHOD_VERSION,
        .rdb_load = fsl_rdb_load,
        .rdb_save = fsl_rdb_save,
        .aof_rewrite = fsl_aofrw,
        .mem_usage = NULL,
        .free = fsl_free,
        .digest = NULL,
    };

    fsltype = KVModule_CreateDataType(ctx, "fsltype_t", 0, &tm);
    if (fsltype == NULL)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"fsl.push",fsl_push,"write",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"fsl.pushtimer",fsl_pushtimer,"write",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"fsl.bpop",fsl_bpop,"write",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"fsl.bpopgt",fsl_bpopgt,"write",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"fsl.bpoppush",fsl_bpoppush,"write",1,2,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"fsl.getall",fsl_getall,"",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "blockonkeys.popall", blockonkeys_popall,
                                  "write", 1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "blockonkeys.lpush", blockonkeys_lpush,
                                  "write", 1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "blockonkeys.lpush_unblock", blockonkeys_lpush,
                                  "write", 1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "blockonkeys.blpopn", blockonkeys_blpopn,
                                  "write", 1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "blockonkeys.blpopn_or_unblock", blockonkeys_blpopn,
                                      "write", 1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;
    return KVMODULE_OK;
}
