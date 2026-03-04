#include "kvmodule.h"
#include <strings.h>
int mutable_bool_val;
int immutable_bool_val;
long long longval;
long long memval;
KVModuleString *strval = NULL;
int enumval;
int flagsval;

/* Series of get and set callbacks for each type of config, these rely on the privdata ptr
 * to point to the config, and they register the configs as such. Note that one could also just
 * use names if they wanted, and store anything in privdata. */
int getBoolConfigCommand(const char *name, void *privdata) {
    KVMODULE_NOT_USED(name);
    return (*(int *)privdata);
}

int setBoolConfigCommand(const char *name, int new, void *privdata, KVModuleString **err) {
    KVMODULE_NOT_USED(name);
    KVMODULE_NOT_USED(err);
    *(int *)privdata = new;
    return KVMODULE_OK;
}

long long getNumericConfigCommand(const char *name, void *privdata) {
    KVMODULE_NOT_USED(name);
    return (*(long long *) privdata);
}

int setNumericConfigCommand(const char *name, long long new, void *privdata, KVModuleString **err) {
    KVMODULE_NOT_USED(name);
    KVMODULE_NOT_USED(err);
    *(long long *)privdata = new;
    return KVMODULE_OK;
}

KVModuleString *getStringConfigCommand(const char *name, void *privdata) {
    KVMODULE_NOT_USED(name);
    KVMODULE_NOT_USED(privdata);
    return strval;
}
int setStringConfigCommand(const char *name, KVModuleString *new, void *privdata, KVModuleString **err) {
    KVMODULE_NOT_USED(name);
    KVMODULE_NOT_USED(err);
    KVMODULE_NOT_USED(privdata);
    size_t len;
    if (!strcasecmp(KVModule_StringPtrLen(new, &len), "rejectisfreed")) {
        *err = KVModule_CreateString(NULL, "Cannot set string to 'rejectisfreed'", 36);
        return KVMODULE_ERR;
    }
    if (strval) KVModule_FreeString(NULL, strval);
    KVModule_RetainString(NULL, new);
    strval = new;
    return KVMODULE_OK;
}

int getEnumConfigCommand(const char *name, void *privdata) {
    KVMODULE_NOT_USED(name);
    KVMODULE_NOT_USED(privdata);
    return enumval;
}

int setEnumConfigCommand(const char *name, int val, void *privdata, KVModuleString **err) {
    KVMODULE_NOT_USED(name);
    KVMODULE_NOT_USED(err);
    KVMODULE_NOT_USED(privdata);
    enumval = val;
    return KVMODULE_OK;
}

int getFlagsConfigCommand(const char *name, void *privdata) {
    KVMODULE_NOT_USED(name);
    KVMODULE_NOT_USED(privdata);
    return flagsval;
}

int setFlagsConfigCommand(const char *name, int val, void *privdata, KVModuleString **err) {
    KVMODULE_NOT_USED(name);
    KVMODULE_NOT_USED(err);
    KVMODULE_NOT_USED(privdata);
    flagsval = val;
    return KVMODULE_OK;
}

int boolApplyFunc(KVModuleCtx *ctx, void *privdata, KVModuleString **err) {
    KVMODULE_NOT_USED(ctx);
    KVMODULE_NOT_USED(privdata);
    if (mutable_bool_val && immutable_bool_val) {
        *err = KVModule_CreateString(NULL, "Bool configs cannot both be yes.", 32);
        return KVMODULE_ERR;
    }
    return KVMODULE_OK;
}

int longlongApplyFunc(KVModuleCtx *ctx, void *privdata, KVModuleString **err) {
    KVMODULE_NOT_USED(ctx);
    KVMODULE_NOT_USED(privdata);
    if (longval == memval) {
        *err = KVModule_CreateString(NULL, "These configs cannot equal each other.", 38);
        return KVMODULE_ERR;
    }
    return KVMODULE_OK;
}

int registerBlockCheck(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    int response_ok = 0;
    int result = KVModule_RegisterBoolConfig(ctx, "mutable_bool", 1, KVMODULE_CONFIG_DEFAULT, getBoolConfigCommand, setBoolConfigCommand, boolApplyFunc, &mutable_bool_val);
    response_ok |= (result == KVMODULE_OK);

    result = KVModule_RegisterStringConfig(ctx, "string", "secret password", KVMODULE_CONFIG_DEFAULT, getStringConfigCommand, setStringConfigCommand, NULL, NULL);
    response_ok |= (result == KVMODULE_OK);

    const char *enum_vals[] = {"none", "five", "one", "two", "four"};
    const int int_vals[] = {0, 5, 1, 2, 4};
    result = KVModule_RegisterEnumConfig(ctx, "enum", 1, KVMODULE_CONFIG_DEFAULT, enum_vals, int_vals, 5, getEnumConfigCommand, setEnumConfigCommand, NULL, NULL);
    response_ok |= (result == KVMODULE_OK);

    result = KVModule_RegisterNumericConfig(ctx, "numeric", -1, KVMODULE_CONFIG_DEFAULT, -5, 2000, getNumericConfigCommand, setNumericConfigCommand, longlongApplyFunc, &longval);
    response_ok |= (result == KVMODULE_OK);

    result = KVModule_LoadConfigs(ctx);
    response_ok |= (result == KVMODULE_OK);
    
    /* This validates that it's not possible to register/load configs outside OnLoad,
     * thus returns an error if they succeed. */
    if (response_ok) {
        KVModule_ReplyWithError(ctx, "UNEXPECTEDOK");
    } else {
        KVModule_ReplyWithSimpleString(ctx, "OK");
    }
    return KVMODULE_OK;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx, "moduleconfigs", 1, KVMODULE_APIVER_1) == KVMODULE_ERR) return KVMODULE_ERR;

    if (KVModule_RegisterBoolConfig(ctx, "mutable_bool", 1, KVMODULE_CONFIG_DEFAULT, getBoolConfigCommand, setBoolConfigCommand, boolApplyFunc, &mutable_bool_val) == KVMODULE_ERR) {
        return KVMODULE_ERR;
    }
    /* Immutable config here. */
    if (KVModule_RegisterBoolConfig(ctx, "immutable_bool", 0, KVMODULE_CONFIG_IMMUTABLE, getBoolConfigCommand, setBoolConfigCommand, boolApplyFunc, &immutable_bool_val) == KVMODULE_ERR) {
        return KVMODULE_ERR;
    }
    if (KVModule_RegisterStringConfig(ctx, "string", "secret password", KVMODULE_CONFIG_DEFAULT, getStringConfigCommand, setStringConfigCommand, NULL, NULL) == KVMODULE_ERR) {
        return KVMODULE_ERR;
    }

    /* On the stack to make sure we're copying them. */
    const char *enum_vals[] = {"none", "five", "one", "two", "four"};
    const int int_vals[] = {0, 5, 1, 2, 4};

    if (KVModule_RegisterEnumConfig(ctx, "enum", 1, KVMODULE_CONFIG_DEFAULT, enum_vals, int_vals, 5, getEnumConfigCommand, setEnumConfigCommand, NULL, NULL) == KVMODULE_ERR) {
        return KVMODULE_ERR;
    }
    if (KVModule_RegisterEnumConfig(ctx, "flags", 3, KVMODULE_CONFIG_DEFAULT | KVMODULE_CONFIG_BITFLAGS, enum_vals, int_vals, 5, getFlagsConfigCommand, setFlagsConfigCommand, NULL, NULL) == KVMODULE_ERR) {
        return KVMODULE_ERR;
    }
    /* Memory config here. */
    if (KVModule_RegisterNumericConfig(ctx, "memory_numeric", 1024, KVMODULE_CONFIG_DEFAULT | KVMODULE_CONFIG_MEMORY, 0, 3000000, getNumericConfigCommand, setNumericConfigCommand, longlongApplyFunc, &memval) == KVMODULE_ERR) {
        return KVMODULE_ERR;
    }
    if (KVModule_RegisterNumericConfig(ctx, "numeric", -1, KVMODULE_CONFIG_DEFAULT, -5, 2000, getNumericConfigCommand, setNumericConfigCommand, longlongApplyFunc, &longval) == KVMODULE_ERR) {
        return KVMODULE_ERR;
    }
    size_t len;
    if (argc && !strcasecmp(KVModule_StringPtrLen(argv[0], &len), "noload")) {
        return KVMODULE_OK;
    } else if (KVModule_LoadConfigs(ctx) == KVMODULE_ERR) {
        if (strval) {
            KVModule_FreeString(ctx, strval);
            strval = NULL;
        }
        return KVMODULE_ERR;
    }
    /* Creates a command which registers configs outside OnLoad() function. */
    if (KVModule_CreateCommand(ctx,"block.register.configs.outside.onload", registerBlockCheck, "write", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;
  
    return KVMODULE_OK;
}

int KVModule_OnUnload(KVModuleCtx *ctx) {
    KVMODULE_NOT_USED(ctx);
    if (strval) {
        KVModule_FreeString(ctx, strval);
        strval = NULL;
    }
    return KVMODULE_OK;
}
