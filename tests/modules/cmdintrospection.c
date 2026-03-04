#include "kvmodule.h"

#define UNUSED(V) ((void) V)

int cmd_xadd(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    KVModule_ReplyWithSimpleString(ctx, "OK");
    return KVMODULE_OK;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    if (KVModule_Init(ctx, "cmdintrospection", 1, KVMODULE_APIVER_1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"cmdintrospection.xadd",cmd_xadd,"write deny-oom random fast",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    KVModuleCommand *xadd = KVModule_GetCommand(ctx,"cmdintrospection.xadd");

    KVModuleCommandInfo info = {
        .version = KVMODULE_COMMAND_INFO_VERSION,
        .arity = -5,
        .summary = "Appends a new message to a stream. Creates the key if it doesn't exist.",
        .since = "5.0.0",
        .complexity = "O(1) when adding a new entry, O(N) when trimming where N being the number of entries evicted.",
        .tips = "nondeterministic_output",
        .history = (KVModuleCommandHistoryEntry[]){
            /* NOTE: All versions specified should be the module's versions, not
             * the server's! We use server versions in this example for the purpose of
             * testing (comparing the output with the output of the vanilla
             * XADD). */
            {"6.2.0", "Added the `NOMKSTREAM` option, `MINID` trimming strategy and the `LIMIT` option."},
            {"7.0.0", "Added support for the `<ms>-*` explicit ID form."},
            {0}
        },
        .key_specs = (KVModuleCommandKeySpec[]){
            {
                .notes = "UPDATE instead of INSERT because of the optional trimming feature",
                .flags = KVMODULE_CMD_KEY_RW | KVMODULE_CMD_KEY_UPDATE,
                .begin_search_type = KVMODULE_KSPEC_BS_INDEX,
                .bs.index.pos = 1,
                .find_keys_type = KVMODULE_KSPEC_FK_RANGE,
                .fk.range = {0,1,0}
            },
            {0}
        },
        .args = (KVModuleCommandArg[]){
            {
                .name = "key",
                .type = KVMODULE_ARG_TYPE_KEY,
                .key_spec_index = 0
            },
            {
                .name = "nomkstream",
                .type = KVMODULE_ARG_TYPE_PURE_TOKEN,
                .token = "NOMKSTREAM",
                .since = "6.2.0",
                .flags = KVMODULE_CMD_ARG_OPTIONAL
            },
            {
                .name = "trim",
                .type = KVMODULE_ARG_TYPE_BLOCK,
                .flags = KVMODULE_CMD_ARG_OPTIONAL,
                .subargs = (KVModuleCommandArg[]){
                    {
                        .name = "strategy",
                        .type = KVMODULE_ARG_TYPE_ONEOF,
                        .subargs = (KVModuleCommandArg[]){
                            {
                                .name = "maxlen",
                                .type = KVMODULE_ARG_TYPE_PURE_TOKEN,
                                .token = "MAXLEN",
                            },
                            {
                                .name = "minid",
                                .type = KVMODULE_ARG_TYPE_PURE_TOKEN,
                                .token = "MINID",
                                .since = "6.2.0",
                            },
                            {0}
                        }
                    },
                    {
                        .name = "operator",
                        .type = KVMODULE_ARG_TYPE_ONEOF,
                        .flags = KVMODULE_CMD_ARG_OPTIONAL,
                        .subargs = (KVModuleCommandArg[]){
                            {
                                .name = "equal",
                                .type = KVMODULE_ARG_TYPE_PURE_TOKEN,
                                .token = "="
                            },
                            {
                                .name = "approximately",
                                .type = KVMODULE_ARG_TYPE_PURE_TOKEN,
                                .token = "~"
                            },
                            {0}
                        }
                    },
                    {
                        .name = "threshold",
                        .type = KVMODULE_ARG_TYPE_STRING,
                        .display_text = "threshold" /* Just for coverage, doesn't have a visible effect */
                    },
                    {
                        .name = "count",
                        .type = KVMODULE_ARG_TYPE_INTEGER,
                        .token = "LIMIT",
                        .since = "6.2.0",
                        .flags = KVMODULE_CMD_ARG_OPTIONAL
                    },
                    {0}
                }
            },
            {
                .name = "id-selector",
                .type = KVMODULE_ARG_TYPE_ONEOF,
                .subargs = (KVModuleCommandArg[]){
                    {
                        .name = "auto-id",
                        .type = KVMODULE_ARG_TYPE_PURE_TOKEN,
                        .token = "*"
                    },
                    {
                        .name = "id",
                        .type = KVMODULE_ARG_TYPE_STRING,
                    },
                    {0}
                }
            },
            {
                .name = "data",
                .type = KVMODULE_ARG_TYPE_BLOCK,
                .flags = KVMODULE_CMD_ARG_MULTIPLE,
                .subargs = (KVModuleCommandArg[]){
                    {
                        .name = "field",
                        .type = KVMODULE_ARG_TYPE_STRING,
                    },
                    {
                        .name = "value",
                        .type = KVMODULE_ARG_TYPE_STRING,
                    },
                    {0}
                }
            },
            {0}
        }
    };
    if (KVModule_SetCommandInfo(xadd, &info) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}
