#include "kvmodule.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * This module implements a very simple stack based scripting language.
 * It's purpose is only to test the kv module API to implement scripting
 * engines.
 *
 * The language is called HELLO, and a program in this language is formed by
 * a list of function definitions.
 * The language supports 32-bit integer and string values, and allows to return
 * a single value from a function.
 * The language also supports error values when returned from the CALL
 * instruction.
 *
 * Example of a program:
 *
 * ```
 * FUNCTION foo  # declaration of function 'foo'
 * ARGS 0        # pushes the value in the first argument to the top of the
 *               # stack
 * RETURN        # returns the current value on the top of the stack and marks
 *               # the end of the function declaration
 *
 * RFUNCTION bar # declaration of read-only function 'bar'
 * CONSTI 432    # pushes the value 432 to the top of the stack
 * RETURN        # returns the current value on the top of the stack and marks
 *               # the end of the function declaration.
 *
 * FUNCTION baz  # declaration of function 'baz'
 * ARGS 0        # pushes the value in the first argument to the top of the
 *               # stack
 * SLEEP         # Pops the current value in the stack and sleeps for `value`
 *               # seconds
 * CONSTS x      # pushes the string 'x' to the top of the stack
 * CONSTI 0      # pushes the value 0 to the top of the stack
 * CONSTI 2      # pushes the value 2 to the top of the stack
 * CALL SET      # calls the command specified in the first argument with the
 *               # with arguments pushed to the stack on previous instructions.
 *               # The top of the stack must always have an integer value that
 *               # specifies the number of arguments to pop from the stack.
 *               # In this case, the command called will be `SET x 0`
 * RETURN        # returns the current value on the top of the stack and marks
 *               # the end of the function declaration.
 * ```
 */

/*
 * List of instructions of the HELLO language.
 */
typedef enum HelloInstKind {
    FUNCTION = 0,
    RFUNCTION,
    CONSTI,
    CONSTS,
    ARGS,
    SLEEP,
    CALL,
    RETURN,
    _NUM_INSTRUCTIONS, // Not a real instruction.
} HelloInstKind;

/*
 * String representations of the instructions above.
 */
const char *HelloInstKindStr[] = {
    "FUNCTION",
    "RFUNCTION",
    "CONSTI",
    "CONSTS",
    "ARGS",
    "SLEEP",
    "CALL",
    "RETURN",
};

/*
 * Value type used in the HELLO language.
 */
typedef enum {
    VALUE_INT,
    VALUE_STRING,
    VALUE_ERROR,
} ValueType;

/*
 * Value used in the HELLO language.
 */
typedef struct {
    ValueType type;
    union {
        uint32_t integer;
        const char *string;
    };
} Value;


/*
 * Struct that represents an instance of an instruction.
 * Instructions may have at most one parameter.
 */
typedef struct HelloInst {
    HelloInstKind kind;
    union {
        uint32_t integer;
        char *string;
    } param;
} HelloInst;

/*
 * Struct that represents an instance of a function.
 * A function is just a list of instruction instances.
 */
typedef struct HelloFunc {
    char *name;
    HelloInst instructions[256];
    uint32_t num_instructions;
    uint32_t index;
    int read_only;
} HelloFunc;

/*
 * Struct that represents an instance of an HELLO program.
 * A program is just a list of function instances.
 */
typedef struct HelloProgram {
    HelloFunc *functions[16];
    uint32_t num_functions;
} HelloProgram;


typedef struct HelloDebugCtx {
    int enabled;
    int stop_on_next_instr;
    int abort;
    const Value *stack;
    uint32_t sp;
} HelloDebugCtx;

/*
 * Struct that represents the runtime context of an HELLO program.
 */
typedef struct HelloLangCtx {
    HelloProgram *program;
    HelloDebugCtx debug;
} HelloLangCtx;


static HelloLangCtx *hello_ctx = NULL;


static Value parseValue(const char *str) {
    char *end;
    errno = 0;
    Value result;

    // Check for NULL or empty string
    if (!str || *str == '\0') {
        KVModule_Log(NULL, "error", "Failed to parse argument: NULL or empty string");
        KVModule_Assert(0);
        return result;
    }

    unsigned long val = strtoul(str, &end, 10);

    // Check if no digits were found
    if (end == str) {
        result.type = VALUE_STRING;
        result.string = str;
        return result;
    }

    // Check for trailing characters (non-digits after the number)
    if (*end != '\0') {
        result.type = VALUE_STRING;
        result.string = str;
        return result;
    }

    // Check for overflow
    if (errno == ERANGE || val > UINT32_MAX) {
        result.type = VALUE_STRING;
        result.string = str;
        return result;
    }

    // Success case
    result.type = VALUE_INT;
    result.integer = (uint32_t)val;
    return result;
}

/*
 * Parses the kind of instruction that the current token points to.
 */
static HelloInstKind helloLangParseInstruction(const char *token) {
    for (HelloInstKind i = 0; i < _NUM_INSTRUCTIONS; i++) {
        if (strcmp(HelloInstKindStr[i], token) == 0) {
            return i;
        }
    }
    return _NUM_INSTRUCTIONS;
}

/*
 * Parses the function param.
 */
static void helloLangParseFunction(HelloFunc *func, int read_only) {
    char *token = strtok(NULL, " \n");
    KVModule_Assert(token != NULL);
    func->name = KVModule_Alloc(sizeof(char) * strlen(token) + 1);
    strcpy(func->name, token);
    func->read_only = read_only;
}

/*
 * Parses an integer parameter.
 */
static void helloLangParseIntegerParam(HelloFunc *func) {
    char *token = strtok(NULL, " \n");
    Value parsed = parseValue(token);
    if (parsed.type == VALUE_INT) {
        func->instructions[func->num_instructions].param.integer = parsed.integer;
    } else {
        KVModule_Log(NULL, "error", "Failed to parse integer parameter: '%s'", token);
        KVModule_Assert(0); // Parse error
    }
}

/*
 * Parses the CONSTI instruction parameter.
 */
static void helloLangParseConstI(HelloFunc *func) {
    helloLangParseIntegerParam(func);
    func->num_instructions++;
}
/*
 * Parses the CONSTS instruction parameter.
 */
static void helloLangParseConstS(HelloFunc *func) {
    char *token = strtok(NULL, " \n");
    KVModule_Assert(token != NULL);
    func->instructions[func->num_instructions].param.string = KVModule_Alloc(sizeof(char) * strlen(token) + 1);
    strcpy(func->instructions[func->num_instructions].param.string, token);
    func->num_instructions++;
}

/*
 * Parses the ARGS instruction parameter.
 */
static void helloLangParseArgs(HelloFunc *func) {
    helloLangParseIntegerParam(func);
    func->num_instructions++;
}

static void helloLangParseCall(HelloFunc *func) {
    char *token = strtok(NULL, " \n");
    KVModule_Assert(token != NULL);
    func->instructions[func->num_instructions].param.string = KVModule_Alloc(sizeof(char) * strlen(token) + 1);
    strcpy(func->instructions[func->num_instructions].param.string, token);
    func->num_instructions++;
}

/*
 * Parses an HELLO program source code.
 */
static int helloLangParseCode(const char *code,
                              HelloProgram *program,
                              KVModuleString **err) {
    char *_code = KVModule_Alloc(sizeof(char) * strlen(code) + 1);
    strcpy(_code, code);

    HelloFunc *currentFunc = NULL;

    char *token = strtok(_code, " \n");
    while (token != NULL) {
        HelloInstKind kind = helloLangParseInstruction(token);

        if (currentFunc != NULL) {
            currentFunc->instructions[currentFunc->num_instructions].kind = kind;
        }

        switch (kind) {
        case FUNCTION:
        case RFUNCTION:
            KVModule_Assert(currentFunc == NULL);
            currentFunc = KVModule_Alloc(sizeof(HelloFunc));
            memset(currentFunc, 0, sizeof(HelloFunc));
            currentFunc->index = program->num_functions;
            program->functions[program->num_functions++] = currentFunc;
            helloLangParseFunction(currentFunc, kind == RFUNCTION);
            break;
        case CONSTI:
            KVModule_Assert(currentFunc != NULL);
            helloLangParseConstI(currentFunc);
            break;
        case CONSTS:
            KVModule_Assert(currentFunc != NULL);
            helloLangParseConstS(currentFunc);
            break;
        case ARGS:
            KVModule_Assert(currentFunc != NULL);
            helloLangParseArgs(currentFunc);
            break;
        case SLEEP:
            KVModule_Assert(currentFunc != NULL);
            currentFunc->num_instructions++;
            break;
        case CALL:
            KVModule_Assert(currentFunc != NULL);
            helloLangParseCall(currentFunc);
            break;
        case RETURN:
            KVModule_Assert(currentFunc != NULL);
            currentFunc->num_instructions++;
            currentFunc = NULL;
            break;
        default:
            *err = KVModule_CreateStringPrintf(NULL, "Failed to parse instruction: '%s'", token);
            KVModule_Free(_code);
            return -1;
        }

        token = strtok(NULL, " \n");
    }

    KVModule_Free(_code);

    return 0;
}

static KVModuleScriptingEngineExecutionState executeSleepInst(KVModuleScriptingEngineServerRuntimeCtx *server_ctx,
                                                                  uint32_t seconds) {
    uint32_t elapsed_milliseconds = 0;
    KVModuleScriptingEngineExecutionState state = VMSE_STATE_EXECUTING;
    while(1) {
        state = KVModule_GetFunctionExecutionState(server_ctx);
        if (state != VMSE_STATE_EXECUTING) {
            break;
        }

        if (elapsed_milliseconds >= (seconds * 1000)) {
            break;
        }

        usleep(1000);
        elapsed_milliseconds++;
    }

    return state;
}

static void executeCallInst(KVModuleCtx *module_ctx,
                            const char *cmd_name,
                            Value *stack,
                            int *sp) {
    KVModule_Assert(stack != NULL);
    KVModule_Assert(sp != NULL);
    KVModuleCallReply *rep = NULL;
    errno = 0;
    KVModule_Assert(*sp > 0);
    Value numargs = stack[--(*sp)];
    KVModule_Assert(numargs.type == VALUE_INT);
    KVModule_Assert(numargs.integer <= (uint32_t)(*sp));
    if (numargs.integer > 0) {
        KVModuleString **cmd_args = KVModule_Alloc(sizeof(KVModuleString *) * numargs.integer);
        int bp = *sp - numargs.integer;
        for (uint32_t i = 0; i < numargs.integer; i++) {
            if (stack[bp + i].type == VALUE_INT) {
                cmd_args[i] = KVModule_CreateStringPrintf(NULL, "%u", stack[bp + i].integer);
            } else {
                cmd_args[i] = KVModule_CreateStringPrintf(NULL, "%s", stack[bp + i].string);
            }
            (*sp)--;
        }
        rep = KVModule_Call(module_ctx, cmd_name, "ESCXv", cmd_args, numargs.integer);
        for (uint32_t i = 0; i < numargs.integer; i++) {
            KVModule_FreeString(NULL, cmd_args[i]);
        }
        KVModule_Free(cmd_args);
    } else {
        rep = KVModule_Call(module_ctx, cmd_name, "ESCX");
    }
    KVModule_Assert(rep != NULL);
    int type = KVModule_CallReplyType(rep);

    KVModuleString *response_str = KVModule_CreateStringFromCallReply(rep);
    const char *resp_cstr = KVModule_StringPtrLen(response_str, NULL);

    if (type == KVMODULE_REPLY_ERROR) {
        stack[*sp].type = VALUE_ERROR;
        stack[(*sp)++].string = resp_cstr;
    } else if (type == KVMODULE_REPLY_ARRAY_NULL) {
        stack[(*sp)].type = VALUE_STRING;
        stack[(*sp)++].string = "(null array)";
    } else if (type == KVMODULE_REPLY_NULL) {
        stack[(*sp)].type = VALUE_STRING;
        stack[(*sp)++].string = "(null string)";
    } else {
        if (response_str != NULL) {
            stack[(*sp)++] = parseValue(resp_cstr);
        } else {
            stack[(*sp)].type = VALUE_STRING;
            stack[(*sp)++].string = "OK";
        }
    }

    KVModule_FreeCallReply(rep);
}

static void helloDebuggerLogCurrentInstr(uint32_t pc, HelloInst *instr) {
    KVModuleString *msg = NULL;
    switch (instr->kind) {
    case CONSTI:
    case ARGS:
        msg = KVModule_CreateStringPrintf(NULL, ">>> %3u: %s %u", pc, HelloInstKindStr[instr->kind], instr->param.integer);
        break;
    case CONSTS:
    case CALL:
        msg = KVModule_CreateStringPrintf(NULL, ">>> %3u: %s %s", pc, HelloInstKindStr[instr->kind], instr->param.string);
        break;
    case SLEEP:
    case RETURN:
        msg = KVModule_CreateStringPrintf(NULL, ">>> %3u: %s", pc, HelloInstKindStr[instr->kind]);
        break;
    case FUNCTION:
    case RFUNCTION:
    case _NUM_INSTRUCTIONS:
        KVModule_Assert(0);
    }

    KVModule_ScriptingEngineDebuggerLog(msg, 0);
}

static int helloDebuggerInstrHook(uint32_t pc, HelloInst *instr) {
    helloDebuggerLogCurrentInstr(pc, instr);
    KVModule_ScriptingEngineDebuggerFlushLogs();

    int client_disconnected = 0;
    KVModuleString *err;
    KVModule_ScriptingEngineDebuggerProcessCommands(&client_disconnected, &err);

    if (err) {
        KVModule_ScriptingEngineDebuggerLog(err, 0);
        goto error;
    } else if (client_disconnected) {
        KVModuleString *msg = KVModule_CreateStringPrintf(NULL, "ERROR: Client socket disconnected");
        KVModule_ScriptingEngineDebuggerLog(msg, 0);
        goto error;
    }

    return 1;

error:
    KVModule_ScriptingEngineDebuggerFlushLogs();
    return 0;
}

typedef enum {
    FINISHED,
    KILLED,
    ABORTED,
} HelloExecutionState;

/*
 * Executes an HELLO function.
 */
static HelloExecutionState executeHelloLangFunction(KVModuleCtx *module_ctx,
                                                    KVModuleScriptingEngineServerRuntimeCtx *server_ctx,
                                                    HelloDebugCtx *debug_ctx,
                                                    HelloFunc *func,
                                                    KVModuleString **args,
                                                    int nargs,
                                                    Value *result) {
    KVModule_Assert(result != NULL);
    Value stack[64];
    int sp = 0;

    for (uint32_t pc = 0; pc < func->num_instructions; pc++) {
        HelloInst instr = func->instructions[pc];
        if (debug_ctx->enabled && debug_ctx->stop_on_next_instr) {
            debug_ctx->stack = stack;
            debug_ctx->sp = sp;
            if (!helloDebuggerInstrHook(pc, &instr)) {
                return ABORTED;
            }
            if (debug_ctx->abort) {
                return ABORTED;
            }
        }
        switch (instr.kind) {
        case CONSTI: {
            stack[sp].type = VALUE_INT;
            stack[sp++].integer = instr.param.integer;
            break;
        }
        case CONSTS: {
            stack[sp].type = VALUE_STRING;
            stack[sp++].string = instr.param.string;
            break;
        }
        case ARGS: {
            uint32_t idx = instr.param.integer;
            KVModule_Assert(idx < (uint32_t)nargs);
            size_t len;
            const char *argStr = KVModule_StringPtrLen(args[idx], &len);
            stack[sp++] = parseValue(argStr);
            break;
	    }
        case SLEEP: {
            KVModule_Assert(sp > 0);
            Value sleepVal = stack[--sp];
            KVModule_Assert(sleepVal.type == VALUE_INT);
            if (executeSleepInst(server_ctx, sleepVal.integer) == VMSE_STATE_KILLED) {
                return KILLED;
            }
            break;
	    }
        case CALL: {
            const char *cmd_name = instr.param.string;
            executeCallInst(module_ctx, cmd_name, stack, &sp);
            break;
        }
        case RETURN: {
            KVModule_Assert(sp > 0);
            Value returnVal = stack[--sp];
            KVModule_Assert(sp == 0);
            *result = returnVal;
            return FINISHED;
	    }
        case FUNCTION:
        case RFUNCTION:
        case _NUM_INSTRUCTIONS:
            KVModule_Assert(0);
        }
    }

    KVModule_Assert(0);
    return ABORTED;
}

static KVModuleScriptingEngineMemoryInfo engineGetMemoryInfo(KVModuleCtx *module_ctx,
                                                                 KVModuleScriptingEngineCtx *engine_ctx,
                                                                 KVModuleScriptingEngineSubsystemType type) {
    KVMODULE_NOT_USED(module_ctx);
    KVMODULE_NOT_USED(type);
    HelloLangCtx *ctx = (HelloLangCtx *)engine_ctx;
    KVModuleScriptingEngineMemoryInfo mem_info = {
        .version = KVMODULE_SCRIPTING_ENGINE_ABI_MEMORY_INFO_VERSION
    };

    if (ctx->program != NULL) {
        mem_info.used_memory += KVModule_MallocSize(ctx->program);

        for (uint32_t i = 0; i < ctx->program->num_functions; i++) {
            HelloFunc *func = ctx->program->functions[i];
            if (func != NULL) {
                mem_info.used_memory += KVModule_MallocSize(func);
                mem_info.used_memory += KVModule_MallocSize(func->name);
            }
        }
    }

    mem_info.engine_memory_overhead = KVModule_MallocSize(ctx);
    if (ctx->program != NULL) {
        mem_info.engine_memory_overhead += KVModule_MallocSize(ctx->program);
    }

    return mem_info;
}

static size_t engineFunctionMemoryOverhead(KVModuleCtx *module_ctx,
                                           KVModuleScriptingEngineCompiledFunction *compiled_function) {
    KVMODULE_NOT_USED(module_ctx);
    HelloFunc *func = (HelloFunc *)compiled_function->function;
    return KVModule_MallocSize(func->name);
}

static void engineFreeFunction(KVModuleCtx *module_ctx,
			                   KVModuleScriptingEngineCtx *engine_ctx,
                               KVModuleScriptingEngineSubsystemType type,
                               KVModuleScriptingEngineCompiledFunction *compiled_function) {
    KVMODULE_NOT_USED(module_ctx);
    KVMODULE_NOT_USED(type);
    HelloLangCtx *ctx = (HelloLangCtx *)engine_ctx;
    HelloFunc *func = (HelloFunc *)compiled_function->function;

    for (uint32_t i = 0; i < func->num_instructions; i++) {
        HelloInst instr = func->instructions[i];
        if (instr.kind == CONSTS || instr.kind == CALL) {
            KVModule_Free(instr.param.string);
        }
    }

    ctx->program->functions[func->index] = NULL;
    KVModule_Free(func->name);
    func->name = NULL;
    KVModule_Free(func);
    KVModule_Free(compiled_function->name);
    KVModule_Free(compiled_function);
}

static KVModuleScriptingEngineCompiledFunction **createHelloLangEngine(KVModuleCtx *module_ctx,
                                                                           KVModuleScriptingEngineCtx *engine_ctx,
                                                                           KVModuleScriptingEngineSubsystemType type,
                                                                           const char *code,
                                                                           size_t code_len,
                                                                           size_t timeout,
                                                                           size_t *out_num_compiled_functions,
                                                                           KVModuleString **err) {
    KVMODULE_NOT_USED(module_ctx);
    KVMODULE_NOT_USED(code_len);
    KVMODULE_NOT_USED(type);
    KVMODULE_NOT_USED(timeout);
    KVMODULE_NOT_USED(err);

    HelloLangCtx *ctx = (HelloLangCtx *)engine_ctx;

    if (ctx->program == NULL) {
        ctx->program = KVModule_Alloc(sizeof(HelloProgram));
        memset(ctx->program, 0, sizeof(HelloProgram));
    } else {
        ctx->program->num_functions = 0;
    }

    int ret = helloLangParseCode(code, ctx->program, err);
    if (ret < 0) {
        for (uint32_t i = 0; i < ctx->program->num_functions; i++) {
            HelloFunc *func = ctx->program->functions[i];
            KVModule_Free(func->name);
            KVModule_Free(func);
            ctx->program->functions[i] = NULL;
        }
        ctx->program->num_functions = 0;
        return NULL;
    }

    KVModuleScriptingEngineCompiledFunction **compiled_functions =
        KVModule_Alloc(sizeof(KVModuleScriptingEngineCompiledFunction *) * ctx->program->num_functions);

    for (uint32_t i = 0; i < ctx->program->num_functions; i++) {
        HelloFunc *func = ctx->program->functions[i];

        KVModuleScriptingEngineCompiledFunction *cfunc =
            KVModule_Alloc(sizeof(KVModuleScriptingEngineCompiledFunction));
        *cfunc = (KVModuleScriptingEngineCompiledFunction) {
            .version = KVMODULE_SCRIPTING_ENGINE_ABI_COMPILED_FUNCTION_VERSION,
            .name = KVModule_CreateString(NULL, func->name, strlen(func->name)),
            .function = func,
            .desc = NULL,
            .f_flags = func->read_only ? VMSE_SCRIPT_FLAG_NO_WRITES : 0,
        };

        compiled_functions[i] = cfunc;
    }

    *out_num_compiled_functions = ctx->program->num_functions;

    return compiled_functions;
}

static void
callHelloLangFunction(KVModuleCtx *module_ctx,
                      KVModuleScriptingEngineCtx *engine_ctx,
                      KVModuleScriptingEngineServerRuntimeCtx *server_ctx,
                      KVModuleScriptingEngineCompiledFunction *compiled_function,
                      KVModuleScriptingEngineSubsystemType type,
                      KVModuleString **keys, size_t nkeys,
                      KVModuleString **args, size_t nargs) {
    KVMODULE_NOT_USED(keys);
    KVMODULE_NOT_USED(nkeys);

    KVModule_Assert(type == VMSE_EVAL || type == VMSE_FUNCTION);

    KVModule_AutoMemory(module_ctx);

    HelloLangCtx *ctx = (HelloLangCtx *)engine_ctx;
    HelloFunc *func = (HelloFunc *)compiled_function->function;
    Value result;
    HelloExecutionState state = executeHelloLangFunction(
        module_ctx,
        server_ctx,
        &ctx->debug,
        func,
        args,
        nargs,
        &result);
    KVModule_Assert(state == KILLED || state == FINISHED || state == ABORTED);

    if (state == KILLED) {
        if (type == VMSE_EVAL) {
            KVModule_ReplyWithCustomErrorFormat(module_ctx, 1, "ERR Script killed by user with SCRIPT KILL.");
            return;
        }
        if (type == VMSE_FUNCTION) {
            KVModule_ReplyWithCustomErrorFormat(module_ctx, 1, "ERR Script killed by user with FUNCTION KILL");
            return;
        }
    }
    else if (state == ABORTED) {
        KVModule_ReplyWithCustomErrorFormat(module_ctx, 1, "ERR execution aborted during debugging session");
    }
    else {
        if (result.type == VALUE_INT) {
            KVModule_Log(module_ctx, "info", "Function '%s' executed, returning %u", func->name, result.integer);
            KVModule_ReplyWithLongLong(module_ctx, result.integer);
        } else if (result.type == VALUE_STRING) {
            KVModule_Log(module_ctx, "info", "Function '%s' executed, returning string '%s'", func->name, result.string);
            KVModule_ReplyWithCString(module_ctx, result.string);
        } else {
            KVModule_Assert(result.type == VALUE_ERROR);
            KVModule_Log(module_ctx, "info", "Function '%s' executed, returning error '%s'", func->name, result.string);
            KVModule_ReplyWithCustomErrorFormat(module_ctx, 0, "%s",result.string);
        }
    }
}

static KVModuleScriptingEngineCallableLazyEnvReset *helloResetEvalEnv(KVModuleCtx *module_ctx,
                                                                          KVModuleScriptingEngineCtx *engine_ctx,
                                                                          int async) {
    KVMODULE_NOT_USED(module_ctx);
    KVMODULE_NOT_USED(engine_ctx);
    KVMODULE_NOT_USED(async);
    return NULL;
}

static KVModuleScriptingEngineCallableLazyEnvReset *helloResetEnv(KVModuleCtx *module_ctx,
                                                                      KVModuleScriptingEngineCtx *engine_ctx,
                                                                      KVModuleScriptingEngineSubsystemType type,
                                                                      int async) {
    KVMODULE_NOT_USED(module_ctx);
    KVMODULE_NOT_USED(engine_ctx);
    KVMODULE_NOT_USED(type);
    KVMODULE_NOT_USED(async);
    return NULL;
}

static int helloDebuggerStepCommand(KVModuleString **argv, size_t argc, void *context) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    HelloDebugCtx *ctx = context;
    ctx->stop_on_next_instr = 1;
    return 0;
}

static int helloDebuggerContinueCommand(KVModuleString **argv, size_t argc, void *context) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    HelloDebugCtx *ctx = context;
    ctx->stop_on_next_instr = 0;
    return 0;
}

static int helloDebuggerStackCommand(KVModuleString **argv, size_t argc, void *context) {
    HelloDebugCtx *ctx = context;
    KVModuleString *msg = NULL;

    if (argc > 1) {
        long long n;
        KVModule_StringToLongLong(argv[1], &n);
        uint32_t index = (uint32_t)n;

        if (index >= ctx->sp) {
            KVModuleString *msg = KVModule_CreateStringPrintf(NULL, "Index out of range. Current stack size: %u", ctx->sp);
            KVModule_ScriptingEngineDebuggerLog(msg, 0);
        }
        else {
            Value stackVal = ctx->stack[ctx->sp - index - 1];
            if (stackVal.type == VALUE_INT) {
                msg = KVModule_CreateStringPrintf(NULL, "[%u] %d", index, stackVal.integer);
            } else {
                msg = KVModule_CreateStringPrintf(NULL, "[%u] \"%s\"", index, stackVal.string);
            }
            KVModule_ScriptingEngineDebuggerLog(msg, 0);
        }
    }
    else {
        msg = KVModule_CreateStringPrintf(NULL, "Stack contents:");
        if (ctx->sp == 0) {
            msg = KVModule_CreateStringPrintf(NULL, "[empty]");
            KVModule_ScriptingEngineDebuggerLog(msg, 0);
        }
        else {
            KVModule_ScriptingEngineDebuggerLog(msg, 0);
            for (uint32_t i=0; i < ctx->sp; i++) {
                Value stackVal = ctx->stack[ctx->sp - i - 1];
                if (stackVal.type == VALUE_INT) {
                    if (i == 0) {
                        msg = KVModule_CreateStringPrintf(NULL, "top -> [%u] %d", i, stackVal.integer);
                    } else {
                        msg = KVModule_CreateStringPrintf(NULL, "       [%u] %d", i, stackVal.integer);
                    }
                } else {
                    if (i == 0) {
                        msg = KVModule_CreateStringPrintf(NULL, "top -> [%u] \"%s\"", i, stackVal.string);
                    } else {
                        msg = KVModule_CreateStringPrintf(NULL, "       [%u] \"%s\"", i, stackVal.string);
                    }
                }
                KVModule_ScriptingEngineDebuggerLog(msg, 0);
            }
        }
    }

    KVModule_ScriptingEngineDebuggerFlushLogs();
    return 1;
}

static int helloDebuggerAbortCommand(KVModuleString **argv, size_t argc, void *context) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    HelloDebugCtx *ctx = context;
    ctx->abort = 1;
    return 0;
}

#define COMMAND_COUNT (4)

static KVModuleScriptingEngineDebuggerCommandParam stack_params[1] = {
    {
        .name = "index",
        .optional = 1
    }
};

static KVModuleScriptingEngineDebuggerCommand helloDebuggerCommands[COMMAND_COUNT] = {
    KVMODULE_SCRIPTING_ENGINE_DEBUGGER_COMMAND("step", 1, NULL, 0, "Execute current instruction.", 0 ,helloDebuggerStepCommand),
    KVMODULE_SCRIPTING_ENGINE_DEBUGGER_COMMAND("continue", 1, NULL, 0, "Continue normal execution.", 0, helloDebuggerContinueCommand),
    KVMODULE_SCRIPTING_ENGINE_DEBUGGER_COMMAND("stack", 2, stack_params, 1, "Print stack contents. If index is specified, print only the value at index. Indexes start at 0 (top = 0).", 0, helloDebuggerStackCommand),
    KVMODULE_SCRIPTING_ENGINE_DEBUGGER_COMMAND("abort", 1, NULL, 0, "Abort execution.", 0, helloDebuggerAbortCommand),
};

static KVModuleScriptingEngineDebuggerEnableRet helloDebuggerEnable(KVModuleCtx *module_ctx,
                                                                        KVModuleScriptingEngineCtx *engine_ctx,
                                                                        KVModuleScriptingEngineSubsystemType type,
                                                                        const KVModuleScriptingEngineDebuggerCommand **commands,
                                                                        size_t *commands_len) {
    KVMODULE_NOT_USED(module_ctx);
    KVMODULE_NOT_USED(type);

    HelloLangCtx *ctx = (HelloLangCtx *)engine_ctx;
    ctx->debug = (HelloDebugCtx) {.enabled = 1};
    *commands = helloDebuggerCommands;
    *commands_len = COMMAND_COUNT;

    for (int i=0; i < COMMAND_COUNT; i++) {
        helloDebuggerCommands[i].context = &ctx->debug;
    }
    return VMSE_DEBUG_ENABLED;
}

static void helloDebuggerDisable(KVModuleCtx *module_ctx,
                                 KVModuleScriptingEngineCtx *engine_ctx,
                                 KVModuleScriptingEngineSubsystemType type) {
    KVMODULE_NOT_USED(module_ctx);
    KVMODULE_NOT_USED(type);

    HelloLangCtx *ctx = (HelloLangCtx *)engine_ctx;
    ctx->debug = (HelloDebugCtx){0};

}

static void helloDebuggerStart(KVModuleCtx *module_ctx,
                               KVModuleScriptingEngineCtx *engine_ctx,
                               KVModuleScriptingEngineSubsystemType type,
                               KVModuleString *code) {
    KVMODULE_NOT_USED(module_ctx);
    KVMODULE_NOT_USED(type);
    KVMODULE_NOT_USED(code);

    HelloLangCtx *ctx = (HelloLangCtx *)engine_ctx;
    ctx->debug.stop_on_next_instr = 1;
}

static void helloDebuggerEnd(KVModuleCtx *module_ctx,
                               KVModuleScriptingEngineCtx *engine_ctx,
                               KVModuleScriptingEngineSubsystemType type) {
    KVMODULE_NOT_USED(module_ctx);
    KVMODULE_NOT_USED(type);

    HelloLangCtx *ctx = (HelloLangCtx *)engine_ctx;
    ctx->debug.stop_on_next_instr = 0;
    ctx->debug.abort = 0;
    ctx->debug.stack = NULL;
    ctx->debug.sp = 0;
}

int KVModule_OnLoad(KVModuleCtx *ctx,
                        KVModuleString **argv,
                        int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx, "helloengine", 1, KVMODULE_APIVER_1) == KVMODULE_ERR) {
        return KVMODULE_ERR;
    }

    unsigned long long abi_version = KVMODULE_SCRIPTING_ENGINE_ABI_VERSION;
    if (argc > 0) {
        if (KVModule_StringToULongLong(argv[0], &abi_version) == KVMODULE_ERR) {
            const char *arg_str = KVModule_StringPtrLen(argv[0], NULL);
            KVModule_Log(ctx, "error", "Invalid ABI version: %s", arg_str);
            return KVMODULE_ERR;
        }
        else {
            const char *arg_str = KVModule_StringPtrLen(argv[0], NULL);
            KVModule_Log(ctx, "info", "initializing Hello scripting engine with ABI version: %s", arg_str);
        }
    }

    hello_ctx = KVModule_Alloc(sizeof(HelloLangCtx));
    hello_ctx->program = NULL;
    hello_ctx->debug.enabled = 0;


    KVModuleScriptingEngineMethodsV3 methodsV3;
    KVModuleScriptingEngineMethodsV4 methodsV4;

    if (abi_version <= 2) {
        methodsV3 = (KVModuleScriptingEngineMethodsV3) {
            .version = abi_version,
            .compile_code = createHelloLangEngine,
            .free_function = engineFreeFunction,
            .call_function = callHelloLangFunction,
            .get_function_memory_overhead = engineFunctionMemoryOverhead,
            .reset_eval_env_v2 = helloResetEvalEnv,
            .get_memory_info = engineGetMemoryInfo,
        };
    } else if (abi_version <= 3) {
        methodsV3 = (KVModuleScriptingEngineMethodsV3) {
            .version = abi_version,
            .compile_code = createHelloLangEngine,
            .free_function = engineFreeFunction,
            .call_function = callHelloLangFunction,
            .get_function_memory_overhead = engineFunctionMemoryOverhead,
            .reset_env = helloResetEnv,
            .get_memory_info = engineGetMemoryInfo,
        };
    } else {
        methodsV4 = (KVModuleScriptingEngineMethodsV4) {
            .version = abi_version,
            .compile_code = createHelloLangEngine,
            .free_function = engineFreeFunction,
            .call_function = callHelloLangFunction,
            .get_function_memory_overhead = engineFunctionMemoryOverhead,
            .reset_env = helloResetEnv,
            .get_memory_info = engineGetMemoryInfo,
            .debugger_enable = helloDebuggerEnable,
            .debugger_disable = helloDebuggerDisable,
            .debugger_start = helloDebuggerStart,
            .debugger_end = helloDebuggerEnd,
        };
    }

    KVModuleScriptingEngineMethods *methods = abi_version <= 3 ?
        (KVModuleScriptingEngineMethods *)&methodsV3 :
        (KVModuleScriptingEngineMethods *)&methodsV4;

    KVModule_RegisterScriptingEngine(ctx,
                                         "HELLO",
                                         hello_ctx,
                                         methods);

    return KVMODULE_OK;
}

int KVModule_OnUnload(KVModuleCtx *ctx) {
    if (KVModule_UnregisterScriptingEngine(ctx, "HELLO") != KVMODULE_OK) {
        KVModule_Log(ctx, "error", "Failed to unregister engine");
        return KVMODULE_ERR;
    }

    KVModule_Free(hello_ctx->program);
    hello_ctx->program = NULL;
    KVModule_Free(hello_ctx);
    hello_ctx = NULL;

    return KVMODULE_OK;
}
