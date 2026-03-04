/* A module that implements defrag callback mechanisms.
 */

#include "kvmodule.h"
#include <stdlib.h>

static KVModuleType *FragType;

struct FragObject {
    unsigned long len;
    void **values;
    int maxstep;
};

/* Make sure we get the expected cursor */
unsigned long int last_set_cursor = 0;

unsigned long int datatype_attempts = 0;
unsigned long int datatype_defragged = 0;
unsigned long int datatype_resumes = 0;
unsigned long int datatype_wrong_cursor = 0;
unsigned long int global_attempts = 0;
unsigned long int global_defragged = 0;

int global_strings_len = 0;
KVModuleString **global_strings = NULL;

static void createGlobalStrings(KVModuleCtx *ctx, int count)
{
    global_strings_len = count;
    global_strings = KVModule_Alloc(sizeof(KVModuleString *) * count);

    for (int i = 0; i < count; i++) {
        global_strings[i] = KVModule_CreateStringFromLongLong(ctx, i);
    }
}

static void defragGlobalStrings(KVModuleDefragCtx *ctx)
{
    for (int i = 0; i < global_strings_len; i++) {
        KVModuleString *new = KVModule_DefragKVModuleString(ctx, global_strings[i]);
        global_attempts++;
        if (new != NULL) {
            global_strings[i] = new;
            global_defragged++;
        }
    }
}

static void FragInfo(KVModuleInfoCtx *ctx, int for_crash_report) {
    KVMODULE_NOT_USED(for_crash_report);

    KVModule_InfoAddSection(ctx, "stats");
    KVModule_InfoAddFieldLongLong(ctx, "datatype_attempts", datatype_attempts);
    KVModule_InfoAddFieldLongLong(ctx, "datatype_defragged", datatype_defragged);
    KVModule_InfoAddFieldLongLong(ctx, "datatype_resumes", datatype_resumes);
    KVModule_InfoAddFieldLongLong(ctx, "datatype_wrong_cursor", datatype_wrong_cursor);
    KVModule_InfoAddFieldLongLong(ctx, "global_attempts", global_attempts);
    KVModule_InfoAddFieldLongLong(ctx, "global_defragged", global_defragged);
}

struct FragObject *createFragObject(unsigned long len, unsigned long size, int maxstep) {
    struct FragObject *o = KVModule_Alloc(sizeof(*o));
    o->len = len;
    o->values = KVModule_Alloc(sizeof(KVModuleString*) * len);
    o->maxstep = maxstep;

    for (unsigned long i = 0; i < len; i++) {
        o->values[i] = KVModule_Calloc(1, size);
    }

    return o;
}

/* FRAG.RESETSTATS */
static int fragResetStatsCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    datatype_attempts = 0;
    datatype_defragged = 0;
    datatype_resumes = 0;
    datatype_wrong_cursor = 0;
    global_attempts = 0;
    global_defragged = 0;

    KVModule_ReplyWithSimpleString(ctx, "OK");
    return KVMODULE_OK;
}

/* FRAG.CREATE key len size maxstep */
static int fragCreateCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 5)
        return KVModule_WrongArity(ctx);

    KVModuleKey *key = KVModule_OpenKey(ctx,argv[1],
                                              KVMODULE_READ|KVMODULE_WRITE);
    int type = KVModule_KeyType(key);
    if (type != KVMODULE_KEYTYPE_EMPTY)
    {
        return KVModule_ReplyWithError(ctx, "ERR key exists");
    }

    long long len;
    if ((KVModule_StringToLongLong(argv[2], &len) != KVMODULE_OK)) {
        return KVModule_ReplyWithError(ctx, "ERR invalid len");
    }

    long long size;
    if ((KVModule_StringToLongLong(argv[3], &size) != KVMODULE_OK)) {
        return KVModule_ReplyWithError(ctx, "ERR invalid size");
    }

    long long maxstep;
    if ((KVModule_StringToLongLong(argv[4], &maxstep) != KVMODULE_OK)) {
        return KVModule_ReplyWithError(ctx, "ERR invalid maxstep");
    }

    struct FragObject *o = createFragObject(len, size, maxstep);
    KVModule_ModuleTypeSetValue(key, FragType, o);
    KVModule_ReplyWithSimpleString(ctx, "OK");
    KVModule_CloseKey(key);

    return KVMODULE_OK;
}

void FragFree(void *value) {
    struct FragObject *o = value;

    for (unsigned long i = 0; i < o->len; i++)
        KVModule_Free(o->values[i]);
    KVModule_Free(o->values);
    KVModule_Free(o);
}

size_t FragFreeEffort(KVModuleString *key, const void *value) {
    KVMODULE_NOT_USED(key);

    const struct FragObject *o = value;
    return o->len;
}

int FragDefrag(KVModuleDefragCtx *ctx, KVModuleString *key, void **value) {
    KVMODULE_NOT_USED(key);
    unsigned long i = 0;
    int steps = 0;

    int dbid = KVModule_GetDbIdFromDefragCtx(ctx);
    KVModule_Assert(dbid != -1);

    /* Attempt to get cursor, validate it's what we're exepcting */
    if (KVModule_DefragCursorGet(ctx, &i) == KVMODULE_OK) {
        if (i > 0) datatype_resumes++;

        /* Validate we're expecting this cursor */
        if (i != last_set_cursor) datatype_wrong_cursor++;
    } else {
        if (last_set_cursor != 0) datatype_wrong_cursor++;
    }

    /* Attempt to defrag the object itself */
    datatype_attempts++;
    struct FragObject *o = KVModule_DefragAlloc(ctx, *value);
    if (o == NULL) {
        /* Not defragged */
        o = *value;
    } else {
        /* Defragged */
        *value = o;
        datatype_defragged++;
    }

    /* Deep defrag now */
    for (; i < o->len; i++) {
        datatype_attempts++;
        void *new = KVModule_DefragAlloc(ctx, o->values[i]);
        if (new) {
            o->values[i] = new;
            datatype_defragged++;
        }

        if ((o->maxstep && ++steps > o->maxstep) ||
            ((i % 64 == 0) && KVModule_DefragShouldStop(ctx)))
        {
            KVModule_DefragCursorSet(ctx, i);
            last_set_cursor = i;
            return 1;
        }
    }

    last_set_cursor = 0;
    return 0;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx, "defragtest", 1, KVMODULE_APIVER_1)
        == KVMODULE_ERR) return KVMODULE_ERR;

    if (KVModule_GetTypeMethodVersion() < KVMODULE_TYPE_METHOD_VERSION) {
        return KVMODULE_ERR;
    }

    long long glen;
    if (argc != 1 || KVModule_StringToLongLong(argv[0], &glen) == KVMODULE_ERR) {
        return KVMODULE_ERR;
    }

    createGlobalStrings(ctx, glen);

    KVModuleTypeMethods tm = {
            .version = KVMODULE_TYPE_METHOD_VERSION,
            .free = FragFree,
            .free_effort = FragFreeEffort,
            .defrag = FragDefrag
    };

    FragType = KVModule_CreateDataType(ctx, "frag_type", 0, &tm);
    if (FragType == NULL) return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "frag.create",
                                  fragCreateCommand, "write deny-oom", 1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "frag.resetstats",
                                  fragResetStatsCommand, "write deny-oom", 1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    KVModule_RegisterInfoFunc(ctx, FragInfo);
    KVModule_RegisterDefragFunc(ctx, defragGlobalStrings);

    return KVMODULE_OK;
}
