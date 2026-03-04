
#include "kvmodule.h"
#include <strings.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>

#define UNUSED(V) ((void) V)

/* A sample movable keys command that returns a list of all
 * arguments that follow a KEY argument, i.e.
 */
int getkeys_command(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    int i;
    int count = 0;

    /* Handle getkeys-api introspection */
    if (KVModule_IsKeysPositionRequest(ctx)) {
        for (i = 0; i < argc; i++) {
            size_t len;
            const char *str = KVModule_StringPtrLen(argv[i], &len);

            if (len == 3 && !strncasecmp(str, "key", 3) && i + 1 < argc)
                KVModule_KeyAtPos(ctx, i + 1);
        }

        return KVMODULE_OK;
    }

    /* Handle real command invocation */
    KVModule_ReplyWithArray(ctx, KVMODULE_POSTPONED_LEN);
    for (i = 0; i < argc; i++) {
        size_t len;
        const char *str = KVModule_StringPtrLen(argv[i], &len);

        if (len == 3 && !strncasecmp(str, "key", 3) && i + 1 < argc) {
            KVModule_ReplyWithString(ctx, argv[i+1]);
            count++;
        }
    }
    KVModule_ReplySetArrayLength(ctx, count);

    return KVMODULE_OK;
}

int getkeys_command_with_flags(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    int i;
    int count = 0;

    /* Handle getkeys-api introspection */
    if (KVModule_IsKeysPositionRequest(ctx)) {
        for (i = 0; i < argc; i++) {
            size_t len;
            const char *str = KVModule_StringPtrLen(argv[i], &len);

            if (len == 3 && !strncasecmp(str, "key", 3) && i + 1 < argc)
                KVModule_KeyAtPosWithFlags(ctx, i + 1, KVMODULE_CMD_KEY_RO | KVMODULE_CMD_KEY_ACCESS);
        }

        return KVMODULE_OK;
    }

    /* Handle real command invocation */
    KVModule_ReplyWithArray(ctx, KVMODULE_POSTPONED_LEN);
    for (i = 0; i < argc; i++) {
        size_t len;
        const char *str = KVModule_StringPtrLen(argv[i], &len);

        if (len == 3 && !strncasecmp(str, "key", 3) && i + 1 < argc) {
            KVModule_ReplyWithString(ctx, argv[i+1]);
            count++;
        }
    }
    KVModule_ReplySetArrayLength(ctx, count);

    return KVMODULE_OK;
}

int getkeys_fixed(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    int i;

    KVModule_ReplyWithArray(ctx, argc - 1);
    for (i = 1; i < argc; i++) {
        KVModule_ReplyWithString(ctx, argv[i]);
    }
    return KVMODULE_OK;
}

/* Introspect a command using RM_GetCommandKeys() and returns the list
 * of keys. Essentially this is COMMAND GETKEYS implemented in a module.
 * INTROSPECT <with-flags> <cmd> <args>
 */
int getkeys_introspect(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    long long with_flags = 0;

    if (argc < 4) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    if (KVModule_StringToLongLong(argv[1],&with_flags) != KVMODULE_OK)
        return KVModule_ReplyWithError(ctx,"ERR invalid integer");

    int num_keys, *keyflags = NULL;
    int *keyidx = KVModule_GetCommandKeysWithFlags(ctx, &argv[2], argc - 2, &num_keys, with_flags ? &keyflags : NULL);

    if (!keyidx) {
        if (!errno)
            KVModule_ReplyWithEmptyArray(ctx);
        else {
            char err[100];
            switch (errno) {
                case ENOENT:
                    KVModule_ReplyWithError(ctx, "ERR ENOENT");
                    break;
                case EINVAL:
                    KVModule_ReplyWithError(ctx, "ERR EINVAL");
                    break;
                default:
                    snprintf(err, sizeof(err) - 1, "ERR errno=%d", errno);
                    KVModule_ReplyWithError(ctx, err);
                    break;
            }
        }
    } else {
        int i;

        KVModule_ReplyWithArray(ctx, num_keys);
        for (i = 0; i < num_keys; i++) {
            if (!with_flags) {
                KVModule_ReplyWithString(ctx, argv[2 + keyidx[i]]);
                continue;
            }
            KVModule_ReplyWithArray(ctx, 2);
            KVModule_ReplyWithString(ctx, argv[2 + keyidx[i]]);
            char* sflags = "";
            if (keyflags[i] & KVMODULE_CMD_KEY_RO)
                sflags = "RO";
            else if (keyflags[i] & KVMODULE_CMD_KEY_RW)
                sflags = "RW";
            else if (keyflags[i] & KVMODULE_CMD_KEY_OW)
                sflags = "OW";
            else if (keyflags[i] & KVMODULE_CMD_KEY_RM)
                sflags = "RM";
            KVModule_ReplyWithCString(ctx, sflags);
        }

        KVModule_Free(keyidx);
        KVModule_Free(keyflags);
    }

    return KVMODULE_OK;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    if (KVModule_Init(ctx,"getkeys",1,KVMODULE_APIVER_1)== KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"getkeys.command", getkeys_command,"getkeys-api",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"getkeys.command_with_flags", getkeys_command_with_flags,"getkeys-api",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"getkeys.fixed", getkeys_fixed,"",2,4,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"getkeys.introspect", getkeys_introspect,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}
