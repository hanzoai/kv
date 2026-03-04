#include "kvmodule.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

int test_module_update_parameter(KVModuleCtx *ctx,
                                 KVModuleString **argv, int argc) {

  KVModule_UpdateRuntimeArgs(ctx, argv, argc);
  return KVModule_ReplyWithSimpleString(ctx, "OK");
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx, "moduleparameter", 1, KVMODULE_APIVER_1) ==
        KVMODULE_ERR)
      return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "testmoduleparameter.update.parameter",
                                   test_module_update_parameter, "fast", 0, 0,
                                   0) == KVMODULE_ERR)
      return KVMODULE_ERR;

    return KVMODULE_OK;
}
