/* 
 * A module the tests RM_ReplyWith family of commands
 */

#include "kvmodule.h"
#include <math.h>

int rw_string(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) return KVModule_WrongArity(ctx);

    return KVModule_ReplyWithString(ctx, argv[1]);
}

int rw_cstring(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    if (argc != 1) return KVModule_WrongArity(ctx);

    return KVModule_ReplyWithSimpleString(ctx, "A simple string");
}

int rw_int(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) return KVModule_WrongArity(ctx);

    long long integer;
    if (KVModule_StringToLongLong(argv[1], &integer) != KVMODULE_OK)
        return KVModule_ReplyWithError(ctx, "Arg cannot be parsed as an integer");

    return KVModule_ReplyWithLongLong(ctx, integer);
}

/* When one argument is given, it is returned as a double,
 * when two arguments are given, it returns a/b. */
int rw_double(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc==1)
        return KVModule_ReplyWithDouble(ctx, NAN);

    if (argc != 2 && argc != 3) return KVModule_WrongArity(ctx);

    double dbl, dbl2;
    if (KVModule_StringToDouble(argv[1], &dbl) != KVMODULE_OK)
        return KVModule_ReplyWithError(ctx, "Arg cannot be parsed as a double");
    if (argc == 3) {
        if (KVModule_StringToDouble(argv[2], &dbl2) != KVMODULE_OK)
            return KVModule_ReplyWithError(ctx, "Arg cannot be parsed as a double");
        dbl /= dbl2;
    }

    return KVModule_ReplyWithDouble(ctx, dbl);
}

int rw_longdouble(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) return KVModule_WrongArity(ctx);

    long double longdbl;
    if (KVModule_StringToLongDouble(argv[1], &longdbl) != KVMODULE_OK)
        return KVModule_ReplyWithError(ctx, "Arg cannot be parsed as a double");

    return KVModule_ReplyWithLongDouble(ctx, longdbl);
}

int rw_bignumber(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) return KVModule_WrongArity(ctx);

    size_t bignum_len;
    const char *bignum_str = KVModule_StringPtrLen(argv[1], &bignum_len);

    return KVModule_ReplyWithBigNumber(ctx, bignum_str, bignum_len);
}

int rw_array(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) return KVModule_WrongArity(ctx);

    long long integer;
    if (KVModule_StringToLongLong(argv[1], &integer) != KVMODULE_OK)
        return KVModule_ReplyWithError(ctx, "Arg cannot be parsed as a integer");

    KVModule_ReplyWithArray(ctx, integer);
    for (int i = 0; i < integer; ++i) {
        KVModule_ReplyWithLongLong(ctx, i);
    }

    return KVMODULE_OK;
}

int rw_map(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) return KVModule_WrongArity(ctx);

    long long integer;
    if (KVModule_StringToLongLong(argv[1], &integer) != KVMODULE_OK)
        return KVModule_ReplyWithError(ctx, "Arg cannot be parsed as a integer");

    KVModule_ReplyWithMap(ctx, integer);
    for (int i = 0; i < integer; ++i) {
        KVModule_ReplyWithLongLong(ctx, i);
        KVModule_ReplyWithDouble(ctx, i * 1.5);
    }

    return KVMODULE_OK;
}

int rw_set(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) return KVModule_WrongArity(ctx);

    long long integer;
    if (KVModule_StringToLongLong(argv[1], &integer) != KVMODULE_OK)
        return KVModule_ReplyWithError(ctx, "Arg cannot be parsed as a integer");

    KVModule_ReplyWithSet(ctx, integer);
    for (int i = 0; i < integer; ++i) {
        KVModule_ReplyWithLongLong(ctx, i);
    }

    return KVMODULE_OK;
}

int rw_attribute(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) return KVModule_WrongArity(ctx);

    long long integer;
    if (KVModule_StringToLongLong(argv[1], &integer) != KVMODULE_OK)
        return KVModule_ReplyWithError(ctx, "Arg cannot be parsed as a integer");

    if (KVModule_ReplyWithAttribute(ctx, integer) != KVMODULE_OK) {
        return KVModule_ReplyWithError(ctx, "Attributes aren't supported by RESP 2");
    }

    for (int i = 0; i < integer; ++i) {
        KVModule_ReplyWithLongLong(ctx, i);
        KVModule_ReplyWithDouble(ctx, i * 1.5);
    }

    KVModule_ReplyWithSimpleString(ctx, "OK");
    return KVMODULE_OK;
}

int rw_bool(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    if (argc != 1) return KVModule_WrongArity(ctx);

    KVModule_ReplyWithArray(ctx, 2);
    KVModule_ReplyWithBool(ctx, 0);
    return KVModule_ReplyWithBool(ctx, 1);
}

int rw_null(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    if (argc != 1) return KVModule_WrongArity(ctx);

    return KVModule_ReplyWithNull(ctx);
}

int rw_error(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    if (argc != 1) return KVModule_WrongArity(ctx);

    return KVModule_ReplyWithError(ctx, "An error");
}

int rw_error_format(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 3) return KVModule_WrongArity(ctx);

    return KVModule_ReplyWithErrorFormat(ctx,
                                            KVModule_StringPtrLen(argv[1], NULL),
                                            KVModule_StringPtrLen(argv[2], NULL));
}

int rw_verbatim(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) return KVModule_WrongArity(ctx);

    size_t verbatim_len;
    const char *verbatim_str = KVModule_StringPtrLen(argv[1], &verbatim_len);

    return KVModule_ReplyWithVerbatimString(ctx, verbatim_str, verbatim_len);
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    if (KVModule_Init(ctx, "replywith", 1, KVMODULE_APIVER_1) != KVMODULE_OK)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"rw.string",rw_string,"",0,0,0) != KVMODULE_OK)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"rw.cstring",rw_cstring,"",0,0,0) != KVMODULE_OK)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"rw.bignumber",rw_bignumber,"",0,0,0) != KVMODULE_OK)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"rw.int",rw_int,"",0,0,0) != KVMODULE_OK)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"rw.double",rw_double,"",0,0,0) != KVMODULE_OK)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"rw.longdouble",rw_longdouble,"",0,0,0) != KVMODULE_OK)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"rw.array",rw_array,"",0,0,0) != KVMODULE_OK)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"rw.map",rw_map,"",0,0,0) != KVMODULE_OK)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"rw.attribute",rw_attribute,"",0,0,0) != KVMODULE_OK)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"rw.set",rw_set,"",0,0,0) != KVMODULE_OK)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"rw.bool",rw_bool,"",0,0,0) != KVMODULE_OK)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"rw.null",rw_null,"",0,0,0) != KVMODULE_OK)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"rw.error",rw_error,"",0,0,0) != KVMODULE_OK)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"rw.error_format",rw_error_format,"",0,0,0) != KVMODULE_OK)
        return KVMODULE_ERR;
    if (KVModule_CreateCommand(ctx,"rw.verbatim",rw_verbatim,"",0,0,0) != KVMODULE_OK)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}
