#include "kvmodule.h"

#define UNUSED(V) ((void) V)

/* This function implements all commands in this module. All we care about is
 * the COMMAND metadata anyway. */
int kspec_impl(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);

    /* Handle getkeys-api introspection (for "kspec.nonewithgetkeys")  */
    if (KVModule_IsKeysPositionRequest(ctx)) {
        for (int i = 1; i < argc; i += 2)
            KVModule_KeyAtPosWithFlags(ctx, i, KVMODULE_CMD_KEY_RO | KVMODULE_CMD_KEY_ACCESS);

        return KVMODULE_OK;
    }

    KVModule_ReplyWithSimpleString(ctx, "OK");
    return KVMODULE_OK;
}

int createKspecNone(KVModuleCtx *ctx) {
    /* A command without keyspecs; only the legacy (first,last,step) triple (MSET like spec). */
    if (KVModule_CreateCommand(ctx,"kspec.none",kspec_impl,"",1,-1,2) == KVMODULE_ERR)
        return KVMODULE_ERR;
    return KVMODULE_OK;
}

int createKspecNoneWithGetkeys(KVModuleCtx *ctx) {
    /* A command without keyspecs; only the legacy (first,last,step) triple (MSET like spec), but also has a getkeys callback */
    if (KVModule_CreateCommand(ctx,"kspec.nonewithgetkeys",kspec_impl,"getkeys-api",1,-1,2) == KVMODULE_ERR)
        return KVMODULE_ERR;
    return KVMODULE_OK;
}

int createKspecTwoRanges(KVModuleCtx *ctx) {
    /* Test that two position/range-based key specs are combined to produce the
     * legacy (first,last,step) values representing both keys. */
    if (KVModule_CreateCommand(ctx,"kspec.tworanges",kspec_impl,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    KVModuleCommand *command = KVModule_GetCommand(ctx,"kspec.tworanges");
    KVModuleCommandInfo info = {
        .version = KVMODULE_COMMAND_INFO_VERSION,
        .arity = -2,
        .key_specs = (KVModuleCommandKeySpec[]){
            {
                .flags = KVMODULE_CMD_KEY_RO | KVMODULE_CMD_KEY_ACCESS,
                .begin_search_type = KVMODULE_KSPEC_BS_INDEX,
                .bs.index.pos = 1,
                .find_keys_type = KVMODULE_KSPEC_FK_RANGE,
                .fk.range = {0,1,0}
            },
            {
                .flags = KVMODULE_CMD_KEY_RW | KVMODULE_CMD_KEY_UPDATE,
                .begin_search_type = KVMODULE_KSPEC_BS_INDEX,
                .bs.index.pos = 2,
                /* Omitted find_keys_type is shorthand for RANGE {0,1,0} */
            },
            {0}
        }
    };
    if (KVModule_SetCommandInfo(command, &info) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}

int createKspecTwoRangesWithGap(KVModuleCtx *ctx) {
    /* Test that two position/range-based key specs are combined to produce the
     * legacy (first,last,step) values representing just one key. */
    if (KVModule_CreateCommand(ctx,"kspec.tworangeswithgap",kspec_impl,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    KVModuleCommand *command = KVModule_GetCommand(ctx,"kspec.tworangeswithgap");
    KVModuleCommandInfo info = {
        .version = KVMODULE_COMMAND_INFO_VERSION,
        .arity = -2,
        .key_specs = (KVModuleCommandKeySpec[]){
            {
                .flags = KVMODULE_CMD_KEY_RO | KVMODULE_CMD_KEY_ACCESS,
                .begin_search_type = KVMODULE_KSPEC_BS_INDEX,
                .bs.index.pos = 1,
                .find_keys_type = KVMODULE_KSPEC_FK_RANGE,
                .fk.range = {0,1,0}
            },
            {
                .flags = KVMODULE_CMD_KEY_RW | KVMODULE_CMD_KEY_UPDATE,
                .begin_search_type = KVMODULE_KSPEC_BS_INDEX,
                .bs.index.pos = 3,
                /* Omitted find_keys_type is shorthand for RANGE {0,1,0} */
            },
            {0}
        }
    };
    if (KVModule_SetCommandInfo(command, &info) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}

int createKspecKeyword(KVModuleCtx *ctx) {
    /* Only keyword-based specs. The legacy triple is wiped and set to (0,0,0). */
    if (KVModule_CreateCommand(ctx,"kspec.keyword",kspec_impl,"",3,-1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    KVModuleCommand *command = KVModule_GetCommand(ctx,"kspec.keyword");
    KVModuleCommandInfo info = {
        .version = KVMODULE_COMMAND_INFO_VERSION,
        .key_specs = (KVModuleCommandKeySpec[]){
            {
                .flags = KVMODULE_CMD_KEY_RO | KVMODULE_CMD_KEY_ACCESS,
                .begin_search_type = KVMODULE_KSPEC_BS_KEYWORD,
                .bs.keyword.keyword = "KEYS",
                .bs.keyword.startfrom = 1,
                .find_keys_type = KVMODULE_KSPEC_FK_RANGE,
                .fk.range = {-1,1,0}
            },
            {0}
        }
    };
    if (KVModule_SetCommandInfo(command, &info) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}

int createKspecComplex1(KVModuleCtx *ctx) {
    /* First is a range a single key. The rest are keyword-based specs. */
    if (KVModule_CreateCommand(ctx,"kspec.complex1",kspec_impl,"",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    KVModuleCommand *command = KVModule_GetCommand(ctx,"kspec.complex1");
    KVModuleCommandInfo info = {
        .version = KVMODULE_COMMAND_INFO_VERSION,
        .key_specs = (KVModuleCommandKeySpec[]){
            {
                .flags = KVMODULE_CMD_KEY_RO,
                .begin_search_type = KVMODULE_KSPEC_BS_INDEX,
                .bs.index.pos = 1,
            },
            {
                .flags = KVMODULE_CMD_KEY_RW | KVMODULE_CMD_KEY_UPDATE,
                .begin_search_type = KVMODULE_KSPEC_BS_KEYWORD,
                .bs.keyword.keyword = "STORE",
                .bs.keyword.startfrom = 2,
            },
            {
                .flags = KVMODULE_CMD_KEY_RO | KVMODULE_CMD_KEY_ACCESS,
                .begin_search_type = KVMODULE_KSPEC_BS_KEYWORD,
                .bs.keyword.keyword = "KEYS",
                .bs.keyword.startfrom = 2,
                .find_keys_type = KVMODULE_KSPEC_FK_KEYNUM,
                .fk.keynum = {0,1,1}
            },
            {0}
        }
    };
    if (KVModule_SetCommandInfo(command, &info) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}

int createKspecComplex2(KVModuleCtx *ctx) {
    /* First is not legacy, more than STATIC_KEYS_SPECS_NUM specs */
    if (KVModule_CreateCommand(ctx,"kspec.complex2",kspec_impl,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    KVModuleCommand *command = KVModule_GetCommand(ctx,"kspec.complex2");
    KVModuleCommandInfo info = {
        .version = KVMODULE_COMMAND_INFO_VERSION,
        .key_specs = (KVModuleCommandKeySpec[]){
            {
                .flags = KVMODULE_CMD_KEY_RW | KVMODULE_CMD_KEY_UPDATE,
                .begin_search_type = KVMODULE_KSPEC_BS_KEYWORD,
                .bs.keyword.keyword = "STORE",
                .bs.keyword.startfrom = 5,
                .find_keys_type = KVMODULE_KSPEC_FK_RANGE,
                .fk.range = {0,1,0}
            },
            {
                .flags = KVMODULE_CMD_KEY_RO | KVMODULE_CMD_KEY_ACCESS,
                .begin_search_type = KVMODULE_KSPEC_BS_INDEX,
                .bs.index.pos = 1,
                .find_keys_type = KVMODULE_KSPEC_FK_RANGE,
                .fk.range = {0,1,0}
            },
            {
                .flags = KVMODULE_CMD_KEY_RO | KVMODULE_CMD_KEY_ACCESS,
                .begin_search_type = KVMODULE_KSPEC_BS_INDEX,
                .bs.index.pos = 2,
                .find_keys_type = KVMODULE_KSPEC_FK_RANGE,
                .fk.range = {0,1,0}
            },
            {
                .flags = KVMODULE_CMD_KEY_RW | KVMODULE_CMD_KEY_UPDATE,
                .begin_search_type = KVMODULE_KSPEC_BS_INDEX,
                .bs.index.pos = 3,
                .find_keys_type = KVMODULE_KSPEC_FK_KEYNUM,
                .fk.keynum = {0,1,1}
            },
            {
                .flags = KVMODULE_CMD_KEY_RW | KVMODULE_CMD_KEY_UPDATE,
                .begin_search_type = KVMODULE_KSPEC_BS_KEYWORD,
                .bs.keyword.keyword = "MOREKEYS",
                .bs.keyword.startfrom = 5,
                .find_keys_type = KVMODULE_KSPEC_FK_RANGE,
                .fk.range = {-1,1,0}
            },
            {0}
        }
    };
    if (KVModule_SetCommandInfo(command, &info) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx, "keyspecs", 1, KVMODULE_APIVER_1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (createKspecNone(ctx) == KVMODULE_ERR) return KVMODULE_ERR;
    if (createKspecNoneWithGetkeys(ctx) == KVMODULE_ERR) return KVMODULE_ERR;
    if (createKspecTwoRanges(ctx) == KVMODULE_ERR) return KVMODULE_ERR;
    if (createKspecTwoRangesWithGap(ctx) == KVMODULE_ERR) return KVMODULE_ERR;
    if (createKspecKeyword(ctx) == KVMODULE_ERR) return KVMODULE_ERR;
    if (createKspecComplex1(ctx) == KVMODULE_ERR) return KVMODULE_ERR;
    if (createKspecComplex2(ctx) == KVMODULE_ERR) return KVMODULE_ERR;
    return KVMODULE_OK;
}
