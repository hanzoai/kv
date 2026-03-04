
/* define macros for having usleep */
#define _BSD_SOURCE
#define _DEFAULT_SOURCE

#include "kvmodule.h"
#include <string.h>
#include <assert.h>
#include <unistd.h>

#define UNUSED(V) ((void) V)

int child_pid = -1;
int exited_with_code = -1;

void done_handler(int exitcode, int bysignal, void *user_data) {
    child_pid = -1;
    exited_with_code = exitcode;
    assert(user_data==(void*)0xdeadbeef);
    UNUSED(bysignal);
}

int fork_create(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    long long code_to_exit_with;
    long long usleep_us;
    if (argc != 3) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    if(!RMAPI_FUNC_SUPPORTED(KVModule_Fork)){
        KVModule_ReplyWithError(ctx, "Fork api is not supported in the current kv version");
        return KVMODULE_OK;
    }

    KVModule_StringToLongLong(argv[1], &code_to_exit_with);
    KVModule_StringToLongLong(argv[2], &usleep_us);
    exited_with_code = -1;
    int fork_child_pid = KVModule_Fork(done_handler, (void*)0xdeadbeef);
    if (fork_child_pid < 0) {
        KVModule_ReplyWithError(ctx, "Fork failed");
        return KVMODULE_OK;
    } else if (fork_child_pid > 0) {
        /* parent */
        child_pid = fork_child_pid;
        KVModule_ReplyWithLongLong(ctx, child_pid);
        return KVMODULE_OK;
    }

    /* child */
    KVModule_Log(ctx, "notice", "fork child started");
    usleep(usleep_us);
    KVModule_Log(ctx, "notice", "fork child exiting");
    KVModule_ExitFromChild(code_to_exit_with);
    /* unreachable */
    return 0;
}

int fork_exitcode(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    UNUSED(argv);
    UNUSED(argc);
    KVModule_ReplyWithLongLong(ctx, exited_with_code);
    return KVMODULE_OK;
}

int fork_kill(KVModuleCtx *ctx, KVModuleString **argv, int argc)
{
    UNUSED(argv);
    UNUSED(argc);
    if (KVModule_KillForkChild(child_pid) != KVMODULE_OK)
        KVModule_ReplyWithError(ctx, "KillForkChild failed");
    else
        KVModule_ReplyWithLongLong(ctx, 1);
    child_pid = -1;
    return KVMODULE_OK;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    if (KVModule_Init(ctx,"fork",1,KVMODULE_APIVER_1)== KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"fork.create", fork_create,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"fork.exitcode", fork_exitcode,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"fork.kill", fork_kill,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}
