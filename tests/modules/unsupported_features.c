
<<<<<<< HEAD
#include "kvmodule.h"

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx,"unsupported_features",1,KVMODULE_APIVER_1)== KVMODULE_ERR)
        return KVMODULE_ERR;
=======
#include "valkeymodule.h"

int ValkeyModule_OnLoad(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    VALKEYMODULE_NOT_USED(argv);
    VALKEYMODULE_NOT_USED(argc);

    if (ValkeyModule_Init(ctx,"unsupported_features",1,VALKEYMODULE_APIVER_1)== VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;
>>>>>>> v9.0.4

    /* This module does not set any options, meaning it will not opt-in to
     * features like Atomic Slot Migration and Async Loading */

<<<<<<< HEAD
    return KVMODULE_OK;
=======
    return VALKEYMODULE_OK;
>>>>>>> v9.0.4
}