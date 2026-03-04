#include "kvmodule.h"

#include <string.h>
#include <strings.h>

static KVModuleString *log_key_name;

static const char log_command_name[] = "commandfilter.log";
static const char ping_command_name[] = "commandfilter.ping";
static const char retained_command_name[] = "commandfilter.retained";
static const char unregister_command_name[] = "commandfilter.unregister";
static const char unfiltered_clientid_name[] = "unfilter_clientid";
static int in_log_command = 0;

unsigned long long unfiltered_clientid = 0;

static KVModuleCommandFilter *filter, *filter1;
static KVModuleString *retained;

int CommandFilter_UnregisterCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    (void) argc;
    (void) argv;

    KVModule_ReplyWithLongLong(ctx,
            KVModule_UnregisterCommandFilter(ctx, filter));

    return KVMODULE_OK;
}

int CommandFilter_PingCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    (void) argc;
    (void) argv;

    KVModuleCallReply *reply = KVModule_Call(ctx, "ping", "c", "@log");
    if (reply) {
        KVModule_ReplyWithCallReply(ctx, reply);
        KVModule_FreeCallReply(reply);
    } else {
        KVModule_ReplyWithSimpleString(ctx, "Unknown command or invalid arguments");
    }

    return KVMODULE_OK;
}

int CommandFilter_Retained(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    (void) argc;
    (void) argv;

    if (retained) {
        KVModule_ReplyWithString(ctx, retained);
    } else {
        KVModule_ReplyWithNull(ctx);
    }

    return KVMODULE_OK;
}

int CommandFilter_LogCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    KVModuleString *s = KVModule_CreateString(ctx, "", 0);

    int i;
    for (i = 1; i < argc; i++) {
        size_t arglen;
        const char *arg = KVModule_StringPtrLen(argv[i], &arglen);

        if (i > 1) KVModule_StringAppendBuffer(ctx, s, " ", 1);
        KVModule_StringAppendBuffer(ctx, s, arg, arglen);
    }

    KVModuleKey *log = KVModule_OpenKey(ctx, log_key_name, KVMODULE_WRITE|KVMODULE_READ);
    KVModule_ListPush(log, KVMODULE_LIST_HEAD, s);
    KVModule_CloseKey(log);
    KVModule_FreeString(ctx, s);

    in_log_command = 1;

    size_t cmdlen;
    const char *cmdname = KVModule_StringPtrLen(argv[1], &cmdlen);
    KVModuleCallReply *reply = KVModule_Call(ctx, cmdname, "v", &argv[2], (size_t)argc - 2);
    if (reply) {
        KVModule_ReplyWithCallReply(ctx, reply);
        KVModule_FreeCallReply(reply);
    } else {
        KVModule_ReplyWithSimpleString(ctx, "Unknown command or invalid arguments");
    }

    in_log_command = 0;

    return KVMODULE_OK;
}

int CommandFilter_UnfilteredClientId(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    if (argc < 2)
        return KVModule_WrongArity(ctx);

    long long id;
    if (KVModule_StringToLongLong(argv[1], &id) != KVMODULE_OK) {
        KVModule_ReplyWithError(ctx, "invalid client id");
        return KVMODULE_OK;
    }
    if (id < 0) {
        KVModule_ReplyWithError(ctx, "invalid client id");
        return KVMODULE_OK;
    }

    unfiltered_clientid = id;
    KVModule_ReplyWithSimpleString(ctx, "OK");
    return KVMODULE_OK;
}

/* Filter to protect against Bug #11894 reappearing
 *
 * ensures that the filter is only run the first time through, and not on reprocessing
 */
void CommandFilter_BlmoveSwap(KVModuleCommandFilterCtx *filter)
{
    if (KVModule_CommandFilterArgsCount(filter) != 6)
        return;

    KVModuleString *arg = KVModule_CommandFilterArgGet(filter, 0);
    size_t arg_len;
    const char *arg_str = KVModule_StringPtrLen(arg, &arg_len);

    if (arg_len != 6 || strncmp(arg_str, "blmove", 6))
        return;

    /*
     * Swapping directional args (right/left) from source and destination.
     * need to hold here, can't push into the ArgReplace func, as it will cause other to freed -> use after free
     */
    KVModuleString *dir1 = KVModule_HoldString(NULL, KVModule_CommandFilterArgGet(filter, 3));
    KVModuleString *dir2 = KVModule_HoldString(NULL, KVModule_CommandFilterArgGet(filter, 4));
    KVModule_CommandFilterArgReplace(filter, 3, dir2);
    KVModule_CommandFilterArgReplace(filter, 4, dir1);
}

void CommandFilter_CommandFilter(KVModuleCommandFilterCtx *filter)
{
    unsigned long long id = KVModule_CommandFilterGetClientId(filter);
    if (id == unfiltered_clientid) return;

    if (in_log_command) return;  /* don't process our own RM_Call() from CommandFilter_LogCommand() */

    /* Fun manipulations:
     * - Remove @delme
     * - Replace @replaceme
     * - Append @insertbefore or @insertafter
     * - Prefix with Log command if @log encountered
     */
    int log = 0;
    int pos = 0;
    while (pos < KVModule_CommandFilterArgsCount(filter)) {
        const KVModuleString *arg = KVModule_CommandFilterArgGet(filter, pos);
        size_t arg_len;
        const char *arg_str = KVModule_StringPtrLen(arg, &arg_len);

        if (arg_len == 6 && !memcmp(arg_str, "@delme", 6)) {
            KVModule_CommandFilterArgDelete(filter, pos);
            continue;
        } 
        if (arg_len == 10 && !memcmp(arg_str, "@replaceme", 10)) {
            KVModule_CommandFilterArgReplace(filter, pos,
                    KVModule_CreateString(NULL, "--replaced--", 12));
        } else if (arg_len == 13 && !memcmp(arg_str, "@insertbefore", 13)) {
            KVModule_CommandFilterArgInsert(filter, pos,
                    KVModule_CreateString(NULL, "--inserted-before--", 19));
            pos++;
        } else if (arg_len == 12 && !memcmp(arg_str, "@insertafter", 12)) {
            KVModule_CommandFilterArgInsert(filter, pos + 1,
                    KVModule_CreateString(NULL, "--inserted-after--", 18));
            pos++;
        } else if (arg_len == 7 && !memcmp(arg_str, "@retain", 7)) {
            if (retained) KVModule_FreeString(NULL, retained);
            retained = KVModule_CommandFilterArgGet(filter, pos + 1);
            KVModule_RetainString(NULL, retained);
            pos++;
        } else if (arg_len == 4 && !memcmp(arg_str, "@log", 4)) {
            log = 1;
        }
        pos++;
    }

    if (log) KVModule_CommandFilterArgInsert(filter, 0,
            KVModule_CreateString(NULL, log_command_name, sizeof(log_command_name)-1));
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (KVModule_Init(ctx,"commandfilter",1,KVMODULE_APIVER_1)
            == KVMODULE_ERR) return KVMODULE_ERR;

    if (argc != 2 && argc != 3) {
        KVModule_Log(ctx, "warning", "Log key name not specified");
        return KVMODULE_ERR;
    }

    long long noself = 0;
    log_key_name = KVModule_CreateStringFromString(ctx, argv[0]);
    KVModule_StringToLongLong(argv[1], &noself);
    retained = NULL;

    if (KVModule_CreateCommand(ctx,log_command_name,
                CommandFilter_LogCommand,"write deny-oom",1,1,1) == KVMODULE_ERR)
            return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,ping_command_name,
                CommandFilter_PingCommand,"deny-oom",1,1,1) == KVMODULE_ERR)
            return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,retained_command_name,
                CommandFilter_Retained,"readonly",1,1,1) == KVMODULE_ERR)
            return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,unregister_command_name,
                CommandFilter_UnregisterCommand,"write deny-oom",1,1,1) == KVMODULE_ERR)
            return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, unfiltered_clientid_name,
                CommandFilter_UnfilteredClientId, "admin", 1,1,1) == KVMODULE_ERR)
            return KVMODULE_ERR;

    if ((filter = KVModule_RegisterCommandFilter(ctx, CommandFilter_CommandFilter, 
                    noself ? KVMODULE_CMDFILTER_NOSELF : 0))
            == NULL) return KVMODULE_ERR;

    if ((filter1 = KVModule_RegisterCommandFilter(ctx, CommandFilter_BlmoveSwap, 0)) == NULL)
        return KVMODULE_ERR;

    if (argc == 3) {
        const char *ptr = KVModule_StringPtrLen(argv[2], NULL);
        if (!strcasecmp(ptr, "noload")) {
            /* This is a hint that we return ERR at the last moment of OnLoad. */
            KVModule_FreeString(ctx, log_key_name);
            if (retained) KVModule_FreeString(NULL, retained);
            return KVMODULE_ERR;
        }
    }

    return KVMODULE_OK;
}

int KVModule_OnUnload(KVModuleCtx *ctx) {
    KVModule_FreeString(ctx, log_key_name);
    if (retained) KVModule_FreeString(NULL, retained);

    return KVMODULE_OK;
}
