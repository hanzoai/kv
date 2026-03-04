
#include "kvmodule.h"

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx,"unsupported_features",1,KVMODULE_APIVER_1)== KVMODULE_ERR)
        return KVMODULE_ERR;

    /* This module does not set any options, meaning it will not opt-in to
     * features like Atomic Slot Migration and Async Loading */

    return KVMODULE_OK;
}