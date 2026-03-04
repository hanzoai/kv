#include "kvmodule.h"

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <memory.h>
#include <errno.h>

/* Sanity tests to verify inputs and return values. */
int sanity(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModuleRdbStream *s = KVModule_RdbStreamCreateFromFile("dbnew.rdb");

    /* NULL stream should fail. */
    if (KVModule_RdbLoad(ctx, NULL, 0) == KVMODULE_OK || errno != EINVAL) {
        KVModule_ReplyWithError(ctx, strerror(errno));
        goto out;
    }

    /* Invalid flags should fail. */
    if (KVModule_RdbLoad(ctx, s, 188) == KVMODULE_OK || errno != EINVAL) {
        KVModule_ReplyWithError(ctx, strerror(errno));
        goto out;
    }

    /* Missing file should fail. */
    if (KVModule_RdbLoad(ctx, s, 0) == KVMODULE_OK || errno != ENOENT) {
        KVModule_ReplyWithError(ctx, strerror(errno));
        goto out;
    }

    /* Save RDB file. */
    if (KVModule_RdbSave(ctx, s, 0) != KVMODULE_OK || errno != 0) {
        KVModule_ReplyWithError(ctx, strerror(errno));
        goto out;
    }

    /* Load the saved RDB file. */
    if (KVModule_RdbLoad(ctx, s, 0) != KVMODULE_OK || errno != 0) {
        KVModule_ReplyWithError(ctx, strerror(errno));
        goto out;
    }

    KVModule_ReplyWithSimpleString(ctx, "OK");

 out:
    KVModule_RdbStreamFree(s);
    return KVMODULE_OK;
}

int cmd_rdbsave(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    size_t len;
    const char *filename = KVModule_StringPtrLen(argv[1], &len);

    char tmp[len + 1];
    memcpy(tmp, filename, len);
    tmp[len] = '\0';

    KVModuleRdbStream *stream = KVModule_RdbStreamCreateFromFile(tmp);

    if (KVModule_RdbSave(ctx, stream, 0) != KVMODULE_OK || errno != 0) {
        KVModule_ReplyWithError(ctx, strerror(errno));
        goto out;
    }

    KVModule_ReplyWithSimpleString(ctx, "OK");

out:
    KVModule_RdbStreamFree(stream);
    return KVMODULE_OK;
}

/* Fork before calling RM_RdbSave(). */
int cmd_rdbsave_fork(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    size_t len;
    const char *filename = KVModule_StringPtrLen(argv[1], &len);

    char tmp[len + 1];
    memcpy(tmp, filename, len);
    tmp[len] = '\0';

    int fork_child_pid = KVModule_Fork(NULL, NULL);
    if (fork_child_pid < 0) {
        KVModule_ReplyWithError(ctx, strerror(errno));
        return KVMODULE_OK;
    } else if (fork_child_pid > 0) {
        /* parent */
        KVModule_ReplyWithSimpleString(ctx, "OK");
        return KVMODULE_OK;
    }

    KVModuleRdbStream *stream = KVModule_RdbStreamCreateFromFile(tmp);

    int ret = 0;
    if (KVModule_RdbSave(ctx, stream, 0) != KVMODULE_OK) {
        ret = errno;
    }
    KVModule_RdbStreamFree(stream);

    KVModule_ExitFromChild(ret);
    return KVMODULE_OK;
}

int cmd_rdbload(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    size_t len;
    const char *filename = KVModule_StringPtrLen(argv[1], &len);

    char tmp[len + 1];
    memcpy(tmp, filename, len);
    tmp[len] = '\0';

    KVModuleRdbStream *stream = KVModule_RdbStreamCreateFromFile(tmp);

    if (KVModule_RdbLoad(ctx, stream, 0) != KVMODULE_OK || errno != 0) {
        KVModule_RdbStreamFree(stream);
        KVModule_ReplyWithError(ctx, strerror(errno));
        return KVMODULE_OK;
    }

    KVModule_RdbStreamFree(stream);
    KVModule_ReplyWithSimpleString(ctx, "OK");
    return KVMODULE_OK;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx, "rdbloadsave", 1, KVMODULE_APIVER_1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "test.sanity", sanity, "", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "test.rdbsave", cmd_rdbsave, "", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "test.rdbsave_fork", cmd_rdbsave_fork, "", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "test.rdbload", cmd_rdbload, "", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}
