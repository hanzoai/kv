#include "kvmodule.h"

#define UNUSED(V) ((void) V)

int cmd_set(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    KVModule_ReplyWithSimpleString(ctx, "OK");
    return KVMODULE_OK;
}

int cmd_get(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    UNUSED(argv);

    if (argc > 4) /* For testing */
        return KVModule_WrongArity(ctx);

    KVModule_ReplyWithSimpleString(ctx, "OK");
    return KVMODULE_OK;
}

int cmd_get_fullname(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);

    const char *command_name = KVModule_GetCurrentCommandName(ctx);
    KVModule_ReplyWithSimpleString(ctx, command_name);
    return KVMODULE_OK;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx, "subcommands", 1, KVMODULE_APIVER_1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    /* Module command names cannot contain special characters. */
    KVModule_Assert(KVModule_CreateCommand(ctx,"subcommands.char\r",NULL,"",0,0,0) == KVMODULE_ERR);
    KVModule_Assert(KVModule_CreateCommand(ctx,"subcommands.char\n",NULL,"",0,0,0) == KVMODULE_ERR);
    KVModule_Assert(KVModule_CreateCommand(ctx,"subcommands.char ",NULL,"",0,0,0) == KVMODULE_ERR);

    if (KVModule_CreateCommand(ctx,"subcommands.bitarray",NULL,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    KVModuleCommand *parent = KVModule_GetCommand(ctx,"subcommands.bitarray");

    if (KVModule_CreateSubcommand(parent,"set",cmd_set,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    /* Module subcommand names cannot contain special characters. */
    KVModule_Assert(KVModule_CreateSubcommand(parent,"char|",cmd_set,"",0,0,0) == KVMODULE_ERR);
    KVModule_Assert(KVModule_CreateSubcommand(parent,"char@",cmd_set,"",0,0,0) == KVMODULE_ERR);
    KVModule_Assert(KVModule_CreateSubcommand(parent,"char=",cmd_set,"",0,0,0) == KVMODULE_ERR);

    KVModuleCommand *subcmd = KVModule_GetCommand(ctx,"subcommands.bitarray|set");
    KVModuleCommandInfo cmd_set_info = {
        .version = KVMODULE_COMMAND_INFO_VERSION,
        .key_specs = (KVModuleCommandKeySpec[]){
            {
                .flags = KVMODULE_CMD_KEY_RW | KVMODULE_CMD_KEY_UPDATE,
                .begin_search_type = KVMODULE_KSPEC_BS_INDEX,
                .bs.index.pos = 1,
                .find_keys_type = KVMODULE_KSPEC_FK_RANGE,
                .fk.range = {0,1,0}
            },
            {0}
        }
    };
    if (KVModule_SetCommandInfo(subcmd, &cmd_set_info) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateSubcommand(parent,"get",cmd_get,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    subcmd = KVModule_GetCommand(ctx,"subcommands.bitarray|get");
    KVModuleCommandInfo cmd_get_info = {
        .version = KVMODULE_COMMAND_INFO_VERSION,
        .key_specs = (KVModuleCommandKeySpec[]){
            {
                .flags = KVMODULE_CMD_KEY_RO | KVMODULE_CMD_KEY_ACCESS,
                .begin_search_type = KVMODULE_KSPEC_BS_INDEX,
                .bs.index.pos = 1,
                .find_keys_type = KVMODULE_KSPEC_FK_RANGE,
                .fk.range = {0,1,0}
            },
            {0}
        }
    };
    if (KVModule_SetCommandInfo(subcmd, &cmd_get_info) == KVMODULE_ERR)
        return KVMODULE_ERR;

    /* Get the name of the command currently running. */
    if (KVModule_CreateCommand(ctx,"subcommands.parent_get_fullname",cmd_get_fullname,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    /* Get the name of the subcommand currently running. */
    if (KVModule_CreateCommand(ctx,"subcommands.sub",NULL,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    KVModuleCommand *fullname_parent = KVModule_GetCommand(ctx,"subcommands.sub");
    if (KVModule_CreateSubcommand(fullname_parent,"get_fullname",cmd_get_fullname,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    /* Sanity */

    /* Trying to create the same subcommand fails */
    KVModule_Assert(KVModule_CreateSubcommand(parent,"get",NULL,"",0,0,0) == KVMODULE_ERR);

    /* Trying to create a sub-subcommand fails */
    KVModule_Assert(KVModule_CreateSubcommand(subcmd,"get",NULL,"",0,0,0) == KVMODULE_ERR);

    return KVMODULE_OK;
}
