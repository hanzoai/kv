#include "kvmodule.h"
#include <strings.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>

/* A sample with declarable channels, that are used to validate against ACLs */
int getChannels_subscribe(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if ((argc - 1) % 3 != 0) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }
    char *err = NULL;
    
    /* getchannels.command [[subscribe|unsubscribe|publish] [pattern|literal] <channel> ...]
     * This command marks the given channel is accessed based on the
     * provided modifiers. */
    for (int i = 1; i < argc; i += 3) {
        const char *operation = KVModule_StringPtrLen(argv[i], NULL);
        const char *type = KVModule_StringPtrLen(argv[i+1], NULL);
        int flags = 0;

        if (!strcasecmp(operation, "subscribe")) {
            flags |= KVMODULE_CMD_CHANNEL_SUBSCRIBE;
        } else if (!strcasecmp(operation, "unsubscribe")) {
            flags |= KVMODULE_CMD_CHANNEL_UNSUBSCRIBE;
        } else if (!strcasecmp(operation, "publish")) {
            flags |= KVMODULE_CMD_CHANNEL_PUBLISH;
        } else {
            err = "Invalid channel operation";
            break;
        }

        if (!strcasecmp(type, "literal")) {
            /* No op */
        } else if (!strcasecmp(type, "pattern")) {
            flags |= KVMODULE_CMD_CHANNEL_PATTERN;
        } else {
            err = "Invalid channel type";
            break;
        }
        if (KVModule_IsChannelsPositionRequest(ctx)) {
            KVModule_ChannelAtPosWithFlags(ctx, i+2, flags);
        }
    }

    if (!KVModule_IsChannelsPositionRequest(ctx)) {
        if (err) {
            KVModule_ReplyWithError(ctx, err);
        } else {
            /* Normal implementation would go here, but for tests just return okay */
            KVModule_ReplyWithSimpleString(ctx, "OK");
        }
    }

    return KVMODULE_OK;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    if (KVModule_Init(ctx, "getchannels", 1, KVMODULE_APIVER_1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "getchannels.command", getChannels_subscribe, "getchannels-api", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}
