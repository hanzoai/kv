#include "kvmodule.h"

#include <string.h>

/* This is a second sample module to validate that module authentication callbacks can be registered
 * from multiple modules. */

/* Non Blocking Module Auth callback / implementation. */
int auth_cb(KVModuleCtx *ctx, KVModuleString *username, KVModuleString *password, KVModuleString **err) {
    const char *user = KVModule_StringPtrLen(username, NULL);
    const char *pwd = KVModule_StringPtrLen(password, NULL);
    if (!strcmp(user,"foo") && !strcmp(pwd,"allow_two")) {
        KVModule_AuthenticateClientWithACLUser(ctx, "foo", 3, NULL, NULL, NULL);
        return KVMODULE_AUTH_HANDLED;
    }
    else if (!strcmp(user,"foo") && !strcmp(pwd,"deny_two")) {
        KVModuleString *log = KVModule_CreateString(ctx, "Module Auth", 11);
        KVModule_ACLAddLogEntryByUserName(ctx, username, log, KVMODULE_ACL_LOG_AUTH);
        KVModule_FreeString(ctx, log);
        const char *err_msg = "Auth denied by Misc Module.";
        *err = KVModule_CreateString(ctx, err_msg, strlen(err_msg));
        return KVMODULE_AUTH_HANDLED;
    }
    return KVMODULE_AUTH_NOT_HANDLED;
}

int test_rm_register_auth_cb(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    KVModule_RegisterAuthCallback(ctx, auth_cb);
    KVModule_ReplyWithSimpleString(ctx, "OK");
    return KVMODULE_OK;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    if (KVModule_Init(ctx,"moduleauthtwo",1,KVMODULE_APIVER_1)== KVMODULE_ERR)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"testmoduletwo.rm_register_auth_cb", test_rm_register_auth_cb,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    return KVMODULE_OK;
}