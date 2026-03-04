#include "kvmodule.h"

#include <strings.h>
#include <sys/mman.h>

#define UNUSED(V) ((void) V)

void assertCrash(KVModuleInfoCtx *ctx, int for_crash_report) {
    UNUSED(ctx);
    UNUSED(for_crash_report);
    KVModule_Assert(0);
}

void segfaultCrash(KVModuleInfoCtx *ctx, int for_crash_report) {
    UNUSED(ctx);
    UNUSED(for_crash_report);
    /* Compiler gives warnings about writing to a random address
     * e.g "*((char*)-1) = 'x';". As a workaround, we map a read-only area
     * and try to write there to trigger segmentation fault. */
    char *p = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    *p = 'x';
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    if (KVModule_Init(ctx,"infocrash",1,KVMODULE_APIVER_1)
            == KVMODULE_ERR) return KVMODULE_ERR;
    KVModule_Assert(argc == 1);
    if (!strcasecmp(KVModule_StringPtrLen(argv[0], NULL), "segfault")) {
        if (KVModule_RegisterInfoFunc(ctx, segfaultCrash) == KVMODULE_ERR) return KVMODULE_ERR;
    } else if(!strcasecmp(KVModule_StringPtrLen(argv[0], NULL), "assert")) {
        if (KVModule_RegisterInfoFunc(ctx, assertCrash) == KVMODULE_ERR) return KVMODULE_ERR;
    } else {
        return KVMODULE_ERR;
    }

    return KVMODULE_OK;
}
