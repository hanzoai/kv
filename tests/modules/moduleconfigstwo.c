#include "kvmodule.h"
#include <strings.h>

/* Second module configs module, for testing.
 * Need to make sure that multiple modules with configs don't interfere with each other */
int bool_config;

int getBoolConfigCommand(const char *name, void *privdata) {
    KVMODULE_NOT_USED(privdata);
    if (!strcasecmp(name, "test")) {
        return bool_config;
    }
    return 0;
}

int setBoolConfigCommand(const char *name, int new, void *privdata, KVModuleString **err) {
    KVMODULE_NOT_USED(privdata);
    KVMODULE_NOT_USED(err);
    if (!strcasecmp(name, "test")) {
        bool_config = new;
        return KVMODULE_OK;
    }
    return KVMODULE_ERR;
}

/* No arguments are expected */ 
int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    if (KVModule_Init(ctx, "configs", 1, KVMODULE_APIVER_1) == KVMODULE_ERR) return KVMODULE_ERR;

    if (KVModule_RegisterBoolConfig(ctx, "test", 1, KVMODULE_CONFIG_DEFAULT, getBoolConfigCommand, setBoolConfigCommand, NULL, &argc) == KVMODULE_ERR) {
        return KVMODULE_ERR;
    }
    if (KVModule_LoadConfigs(ctx) == KVMODULE_ERR) {
        return KVMODULE_ERR;
    }
    return KVMODULE_OK;
}