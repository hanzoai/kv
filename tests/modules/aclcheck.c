
#include "kvmodule.h"
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <strings.h>

/* A wrap for SET command with ACL check on the key. */
int set_aclcheck_key(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc < 4) {
        return KVModule_WrongArity(ctx);
    }

    int permissions;
    const char *flags = KVModule_StringPtrLen(argv[1], NULL);

    if (!strcasecmp(flags, "W")) {
        permissions = KVMODULE_CMD_KEY_UPDATE;
    } else if (!strcasecmp(flags, "R")) {
        permissions = KVMODULE_CMD_KEY_ACCESS;
    } else if (!strcasecmp(flags, "*")) {
        permissions = KVMODULE_CMD_KEY_UPDATE | KVMODULE_CMD_KEY_ACCESS;
    } else if (!strcasecmp(flags, "~")) {
        permissions = 0; /* Requires either read or write */
    } else {
        KVModule_ReplyWithError(ctx, "INVALID FLAGS");
        return KVMODULE_OK;
    }

    /* Check that the key can be accessed */
    KVModuleString *user_name = KVModule_GetCurrentUserName(ctx);
    KVModuleUser *user = KVModule_GetModuleUserFromUserName(user_name);
    int ret = KVModule_ACLCheckKeyPermissions(user, argv[2], permissions);
    if (ret != 0) {
        KVModule_ReplyWithError(ctx, "DENIED KEY");
        KVModule_FreeModuleUser(user);
        KVModule_FreeString(ctx, user_name);
        return KVMODULE_OK;
    }

    KVModuleCallReply *rep = KVModule_Call(ctx, "SET", "v", argv + 2, (size_t)argc - 2);
    if (!rep) {
        KVModule_ReplyWithError(ctx, "NULL reply returned");
    } else {
        KVModule_ReplyWithCallReply(ctx, rep);
        KVModule_FreeCallReply(rep);
    }

    KVModule_FreeModuleUser(user);
    KVModule_FreeString(ctx, user_name);
    return KVMODULE_OK;
}

/* A wrap for PUBLISH command with ACL check on the channel. */
int publish_aclcheck_channel(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 3) {
        return KVModule_WrongArity(ctx);
    }

    /* Check that the pubsub channel can be accessed */
    KVModuleString *user_name = KVModule_GetCurrentUserName(ctx);
    KVModuleUser *user = KVModule_GetModuleUserFromUserName(user_name);
    int ret = KVModule_ACLCheckChannelPermissions(user, argv[1], KVMODULE_CMD_CHANNEL_SUBSCRIBE);
    if (ret != 0) {
        KVModule_ReplyWithError(ctx, "DENIED CHANNEL");
        KVModule_FreeModuleUser(user);
        KVModule_FreeString(ctx, user_name);
        return KVMODULE_OK;
    }

    KVModuleCallReply *rep = KVModule_Call(ctx, "PUBLISH", "v", argv + 1, (size_t)argc - 1);
    if (!rep) {
        KVModule_ReplyWithError(ctx, "NULL reply returned");
    } else {
        KVModule_ReplyWithCallReply(ctx, rep);
        KVModule_FreeCallReply(rep);
    }

    KVModule_FreeModuleUser(user);
    KVModule_FreeString(ctx, user_name);
    return KVMODULE_OK;
}


/* ACL check that validates command execution with all permissions
 * including command, keys, channels, and database access */
int aclcheck_check_permissions(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc < 3) {
        return KVModule_WrongArity(ctx);
    }

    long long dbid;
    if (KVModule_StringToLongLong(argv[1], &dbid) != KVMODULE_OK) {
        KVModule_ReplyWithError(ctx, "invalid DB index");
        return KVMODULE_OK;
    }

    KVModuleString *user_name = KVModule_GetCurrentUserName(ctx);
    KVModuleUser *user = KVModule_GetModuleUserFromUserName(user_name);

    KVModuleACLLogEntryReason denial_reason;
    int ret = KVModule_ACLCheckPermissions(user, argv + 2, argc - 2, (int)dbid, &denial_reason);

    if (ret != KVMODULE_OK) {
        int saved_errno = errno;
        if (saved_errno == EINVAL) {
            KVModule_ReplyWithError(ctx, "ERR invalid arguments");
        } else if (saved_errno == EACCES) {
            KVModule_ReplyWithError(ctx, "NOPERM");
            KVModuleString *obj;
            switch (denial_reason) {
                case KVMODULE_ACL_LOG_CMD:
                    obj = argv[2];
                    break;
                case KVMODULE_ACL_LOG_KEY:
                    obj = (argc > 3) ? argv[3] : argv[2];
                    break;
                case KVMODULE_ACL_LOG_CHANNEL:
                    obj = (argc > 3) ? argv[3] : argv[2];
                    break;
                case KVMODULE_ACL_LOG_DB:
                    obj = argv[1];
                    break;
                default:
                    obj = argv[2];
                    break;
            }
            KVModule_ACLAddLogEntry(ctx, user, obj, denial_reason);
        } else {
            KVModule_ReplyWithError(ctx, "ERR unexpected error");
        }
        KVModule_FreeModuleUser(user);
        KVModule_FreeString(ctx, user_name);
        return KVMODULE_OK;
    }

    KVModuleCallReply *rep = KVModule_Call(ctx, KVModule_StringPtrLen(argv[2], NULL), "v", argv + 3, (size_t)argc - 3);
    if (!rep) {
        KVModule_ReplyWithError(ctx, "NULL reply");
    } else {
        KVModule_ReplyWithCallReply(ctx, rep);
        KVModule_FreeCallReply(rep);
    }

    KVModule_FreeModuleUser(user);
    KVModule_FreeString(ctx, user_name);
    return KVMODULE_OK;
}

/* A wrap for RM_Call that check first that the command can be executed */
int rm_call_aclcheck_cmd(KVModuleCtx *ctx, KVModuleUser *user, KVModuleString **argv, int argc) {
    if (argc < 2) {
        return KVModule_WrongArity(ctx);
    }

    /* Check that the command can be executed */
    int ret = KVModule_ACLCheckCommandPermissions(user, argv + 1, argc - 1);
    if (ret != 0) {
        KVModule_ReplyWithError(ctx, "DENIED CMD");
        /* Add entry to ACL log */
        KVModule_ACLAddLogEntry(ctx, user, argv[1], KVMODULE_ACL_LOG_CMD);
        return KVMODULE_OK;
    }

    const char* cmd = KVModule_StringPtrLen(argv[1], NULL);

    KVModuleCallReply* rep = KVModule_Call(ctx, cmd, "v", argv + 2, (size_t)argc - 2);
    if(!rep){
        KVModule_ReplyWithError(ctx, "NULL reply returned");
    }else{
        KVModule_ReplyWithCallReply(ctx, rep);
        KVModule_FreeCallReply(rep);
    }

    return KVMODULE_OK;
}

int rm_call_aclcheck_cmd_default_user(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVModuleString *user_name = KVModule_GetCurrentUserName(ctx);
    KVModuleUser *user = KVModule_GetModuleUserFromUserName(user_name);

    int res = rm_call_aclcheck_cmd(ctx, user, argv, argc);

    KVModule_FreeModuleUser(user);
    KVModule_FreeString(ctx, user_name);
    return res;
}

int rm_call_aclcheck_cmd_module_user(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    /* Create a user and authenticate */
    KVModuleUser *user = KVModule_CreateModuleUser("testuser1");
    KVModule_SetModuleUserACL(user, "allcommands");
    KVModule_SetModuleUserACL(user, "allkeys");
    KVModule_SetModuleUserACL(user, "on");
    KVModule_AuthenticateClientWithUser(ctx, user, NULL, NULL, NULL);

    int res = rm_call_aclcheck_cmd(ctx, user, argv, argc);

    /* authenticated back to "default" user (so once we free testuser1 we will not disconnected */
    KVModule_AuthenticateClientWithACLUser(ctx, "default", 7, NULL, NULL, NULL);
    KVModule_FreeModuleUser(user);
    return res;
}

int rm_call_aclcheck_with_errors(KVModuleCtx *ctx, KVModuleString **argv, int argc){
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if(argc < 2){
        return KVModule_WrongArity(ctx);
    }

    const char* cmd = KVModule_StringPtrLen(argv[1], NULL);

    KVModuleCallReply* rep = KVModule_Call(ctx, cmd, "vEC", argv + 2, (size_t)argc - 2);
    KVModule_ReplyWithCallReply(ctx, rep);
    KVModule_FreeCallReply(rep);
    return KVMODULE_OK;
}

/* A wrap for RM_Call that pass the 'C' flag to do ACL check on the command. */
int rm_call_aclcheck(KVModuleCtx *ctx, KVModuleString **argv, int argc){
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if(argc < 2){
        return KVModule_WrongArity(ctx);
    }

    const char* cmd = KVModule_StringPtrLen(argv[1], NULL);

    KVModuleCallReply* rep = KVModule_Call(ctx, cmd, "vC", argv + 2, (size_t)argc - 2);
    if(!rep) {
        char err[100];
        switch (errno) {
            case EACCES:
                KVModule_ReplyWithError(ctx, "ERR NOPERM");
                break;
            default:
                snprintf(err, sizeof(err) - 1, "ERR errno=%d", errno);
                KVModule_ReplyWithError(ctx, err);
                break;
        }
    } else {
        KVModule_ReplyWithCallReply(ctx, rep);
        KVModule_FreeCallReply(rep);
    }

    return KVMODULE_OK;
}

int module_check_key_permission(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if(argc != 3){
        return KVModule_WrongArity(ctx);
    }

    size_t key_len = 0;
    unsigned int flags = 0;
    const char* key_prefix = KVModule_StringPtrLen(argv[1], &key_len);
    const char* check_what = KVModule_StringPtrLen(argv[2], NULL);
    if (strcasecmp(check_what, "R") == 0) {
        flags = KVMODULE_CMD_KEY_ACCESS;
    } else if(strcasecmp(check_what, "W") == 0) {
        flags = KVMODULE_CMD_KEY_INSERT | KVMODULE_CMD_KEY_DELETE | KVMODULE_CMD_KEY_UPDATE;
    } else {
        KVModule_ReplyWithSimpleString(ctx, "EINVALID");
        return KVMODULE_OK;
    }

    KVModuleString* user_name = KVModule_GetCurrentUserName(ctx);
    KVModuleUser *user = KVModule_GetModuleUserFromUserName(user_name);
    int rc = KVModule_ACLCheckKeyPrefixPermissions(user, key_prefix, key_len, flags);
    if (rc == KVMODULE_OK) {
        // Access granted.
        KVModule_ReplyWithSimpleString(ctx, "OK");
    } else {
        // Access denied.
        KVModule_ReplyWithSimpleString(ctx, "EACCESS");
    }
    KVModule_FreeModuleUser(user);
    KVModule_FreeString(ctx, user_name);
    return KVMODULE_OK;
}

int module_test_acl_category(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    KVModule_ReplyWithSimpleString(ctx, "OK");
    return KVMODULE_OK;
}

int commandBlockCheck(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    int response_ok = 0;
    int result = KVModule_CreateCommand(ctx,"command.that.should.fail", module_test_acl_category, "", 0, 0, 0);
    response_ok |= (result == KVMODULE_OK);

    result = KVModule_AddACLCategory(ctx,"blockedcategory");
    response_ok |= (result == KVMODULE_OK);
    
    KVModuleCommand *parent = KVModule_GetCommand(ctx,"block.commands.outside.onload");
    result = KVModule_SetCommandACLCategories(parent, "write");
    response_ok |= (result == KVMODULE_OK);

    result = KVModule_CreateSubcommand(parent,"subcommand.that.should.fail",module_test_acl_category,"",0,0,0);
    response_ok |= (result == KVMODULE_OK);
    
    /* This validates that it's not possible to create commands or add
     * a new ACL Category outside OnLoad function.
     * thus returns an error if they succeed. */
    if (response_ok) {
        KVModule_ReplyWithError(ctx, "UNEXPECTEDOK");
    } else {
        KVModule_ReplyWithSimpleString(ctx, "OK");
    }
    return KVMODULE_OK;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {

    if (KVModule_Init(ctx,"aclcheck",1,KVMODULE_APIVER_1)== KVMODULE_ERR)
        return KVMODULE_ERR;

    if (argc > 1) return KVModule_WrongArity(ctx);
    
    /* When that flag is passed, we try to create too many categories,
     * and the test expects this to fail. In this case the server returns KVMODULE_ERR
     * and set errno to ENOMEM*/
    if (argc == 1) {
        long long fail_flag = 0;
        KVModule_StringToLongLong(argv[0], &fail_flag);
        if (fail_flag) {
            for (size_t j = 0; j < 45; j++) {
                KVModuleString* name =  KVModule_CreateStringPrintf(ctx, "customcategory%zu", j);
                if (KVModule_AddACLCategory(ctx, KVModule_StringPtrLen(name, NULL)) == KVMODULE_ERR) {
                    KVModule_Assert(errno == ENOMEM);
                    KVModule_FreeString(ctx, name);
                    return KVMODULE_ERR;
                }
                KVModule_FreeString(ctx, name);
            }
        }
    }

    if (KVModule_CreateCommand(ctx,"aclcheck.set.check.key", set_aclcheck_key,"write",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"block.commands.outside.onload", commandBlockCheck,"write",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"aclcheck.module.command.aclcategories.write", module_test_acl_category,"write",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    KVModuleCommand *aclcategories_write = KVModule_GetCommand(ctx,"aclcheck.module.command.aclcategories.write");

    if (KVModule_SetCommandACLCategories(aclcategories_write, "write") == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"aclcheck.module.command.aclcategories.write.function.read.category", module_test_acl_category,"write",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    KVModuleCommand *read_category = KVModule_GetCommand(ctx,"aclcheck.module.command.aclcategories.write.function.read.category");

    if (KVModule_SetCommandACLCategories(read_category, "read") == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"aclcheck.module.command.aclcategories.read.only.category", module_test_acl_category,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    KVModuleCommand *read_only_category = KVModule_GetCommand(ctx,"aclcheck.module.command.aclcategories.read.only.category");

    if (KVModule_SetCommandACLCategories(read_only_category, "read") == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"aclcheck.publish.check.channel", publish_aclcheck_channel,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"aclcheck.check.permissions", aclcheck_check_permissions,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"aclcheck.rm_call.check.cmd", rm_call_aclcheck_cmd_default_user,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"aclcheck.rm_call.check.cmd.module.user", rm_call_aclcheck_cmd_module_user,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"aclcheck.rm_call", rm_call_aclcheck,
                                  "write",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"aclcheck.rm_call_with_errors", rm_call_aclcheck_with_errors,
                                      "write",0,0,0) == KVMODULE_ERR)
            return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"acl.check_key_prefix", 
                                   module_check_key_permission, 
                                   "", 
                                   0, 
                                   0, 
                                   0) == KVMODULE_ERR) {
        return KVMODULE_ERR;
    } 
    /* This validates that, when module tries to add a category with invalid characters,
     * the server returns KVMODULE_ERR and set errno to `EINVAL` */
    if (KVModule_AddACLCategory(ctx,"!nval!dch@r@cter$") == KVMODULE_ERR)
        KVModule_Assert(errno == EINVAL);
    else 
        return KVMODULE_ERR;
    
    /* This validates that, when module tries to add a category that already exists,
     * the server returns KVMODULE_ERR and set errno to `EBUSY` */
    if (KVModule_AddACLCategory(ctx,"write") == KVMODULE_ERR)
        KVModule_Assert(errno == EBUSY);
    else 
        return KVMODULE_ERR;
    
    if (KVModule_AddACLCategory(ctx,"foocategory") == KVMODULE_ERR)
        return KVMODULE_ERR;
    
    if (KVModule_CreateCommand(ctx,"aclcheck.module.command.test.add.new.aclcategories", module_test_acl_category,"",0,0,0) == KVMODULE_ERR)
        return KVMODULE_ERR;
    KVModuleCommand *test_add_new_aclcategories = KVModule_GetCommand(ctx,"aclcheck.module.command.test.add.new.aclcategories");

    if (KVModule_SetCommandACLCategories(test_add_new_aclcategories, "foocategory") == KVMODULE_ERR)
        return KVMODULE_ERR;
    
    return KVMODULE_OK;
}
