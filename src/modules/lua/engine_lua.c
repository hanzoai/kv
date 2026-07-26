/*
 * Copyright (c) KV Contributors
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "../../kvmodule.h"
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <string.h>
#if defined(__GLIBC__) && !defined(USE_LIBC)
#include <malloc.h>
#endif
#include <errno.h>

#include "engine_structs.h"
#include "function_lua.h"
#include "script_lua.h"
#include "debug_lua.h"


#define LUA_ENGINE_NAME "LUA"
#define REGISTRY_ERROR_HANDLER_NAME "__ERROR_HANDLER__"

/* Adds server.debug() function used by lua debugger
 *
 * Log a string message into the output console.
 * Can take multiple arguments that will be separated by commas.
 * Nothing is returned to the caller. */
static int luaServerDebugCommand(lua_State *lua) {
    if (!ldbIsActive()) return 0;
    int argc = lua_gettop(lua);
    KVModuleString *log = KVModule_CreateStringPrintf(NULL, "<debug> line %d: ", ldbGetCurrentLine());
    while (argc--) {
        log = ldbCatStackValue(log, lua, -1 - argc);
        if (argc != 0) {
            KVModule_StringAppendBuffer(NULL, log, ", ", 2);
        }
    }
    ldbLog(log);
    return 0;
}

/* Adds server.breakpoint() function used by lua debugger.
 *
 * Allows to stop execution during a debugging session from within
 * the Lua code implementation, like if a breakpoint was set in the code
 * immediately after the function. */
static int luaServerBreakpointCommand(lua_State *lua) {
    if (ldbIsActive()) {
        ldbSetBreakpointOnNextLine(1);
        lua_pushboolean(lua, 1);
    } else {
        lua_pushboolean(lua, 0);
    }
    return 1;
}

/* Adds server.replicate_commands()
 *
 * DEPRECATED: Now do nothing and always return true.
 * Turn on single commands replication if the script never called
 * a write command so far, and returns true. Otherwise if the script
 * already started to write, returns false and stick to whole scripts
 * replication, which is our default. */
int luaServerReplicateCommandsCommand(lua_State *lua) {
    lua_pushboolean(lua, 1);
    return 1;
}

static void luaStateInstallErrorHandler(lua_State *lua) {
    /* Add a helper function we use for pcall error reporting.
     * Note that when the error is in the C function we want to report the
     * information about the caller, that's what makes sense from the point
     * of view of the user debugging a script. */
    lua_pushstring(lua, REGISTRY_ERROR_HANDLER_NAME);
    char *errh_func = "local dbg = debug\n"
                      "debug = nil\n"
                      "local error_handler = function (err)\n"
                      "  local i = dbg.getinfo(2,'nSl')\n"
                      "  if i and i.what == 'C' then\n"
                      "    i = dbg.getinfo(3,'nSl')\n"
                      "  end\n"
                      "  if type(err) ~= 'table' then\n"
                      "    err = {err='ERR ' .. tostring(err)}"
                      "  end"
                      "  if i then\n"
                      "    err['source'] = i.source\n"
                      "    err['line'] = i.currentline\n"
                      "  end"
                      "  return err\n"
                      "end\n"
                      "return error_handler";
    luaL_loadbuffer(lua, errh_func, strlen(errh_func), "@err_handler_def");
    lua_pcall(lua, 0, 1, 0);
    lua_settable(lua, LUA_REGISTRYINDEX);
}

static void luaStateLockGlobalTable(lua_State *lua) {
    /* Lock the global table from any changes */
    lua_pushvalue(lua, LUA_GLOBALSINDEX);
    luaSetErrorMetatable(lua);
    /* Recursively lock all tables that can be reached from the global table */
    luaSetTableProtectionRecursively(lua);
    lua_pop(lua, 1);
    /* Set metatables of basic types (string, number, nil etc.) readonly. */
    luaSetTableProtectionForBasicTypes(lua);
}


static void initializeEvalLuaState(lua_State *lua) {
    /* register debug commands. we only need to add it under 'server' as 'redis'
     * is effectively aliased to 'server' table at this point. */
    lua_getglobal(lua, "server");

    /* server.breakpoint */
    lua_pushstring(lua, "breakpoint");
    lua_pushcfunction(lua, luaServerBreakpointCommand);
    lua_settable(lua, -3);

    /* server.debug */
    lua_pushstring(lua, "debug");
    lua_pushcfunction(lua, luaServerDebugCommand);
    lua_settable(lua, -3);

    /* server.replicate_commands */
    lua_pushstring(lua, "replicate_commands");
    lua_pushcfunction(lua, luaServerReplicateCommandsCommand);
    lua_settable(lua, -3);

    lua_setglobal(lua, "server");

    /* Duplicate the function with __server__err__handler and
     * __redis__err_handler name for backwards compatibility. */
    lua_pushstring(lua, REGISTRY_ERROR_HANDLER_NAME);
    lua_gettable(lua, LUA_REGISTRYINDEX);
    lua_setglobal(lua, "__server__err__handler");
    lua_getglobal(lua, "__server__err__handler");
    lua_setglobal(lua, "__redis__err__handler");
}

static uint32_t parse_semver(const char *version) {
    unsigned int major = 0, minor = 0, patch = 0;
    sscanf(version, "%u.%u.%u", &major, &minor, &patch);
    return ((major & 0xFF) << 16) | ((minor & 0xFF) << 8) | (patch & 0xFF);
}

static void get_version_info(KVModuleCtx *ctx,
                             char **redis_version,
                             uint32_t *redis_version_num,
                             char **server_name,
                             char **kv_version,
                             uint32_t *kv_version_num) {
    KVModuleServerInfoData *info = KVModule_GetServerInfo(ctx, "server");
    KVModule_Assert(info != NULL);

    const char *rv = KVModule_ServerInfoGetFieldC(info, "redis_version");
    *redis_version = lm_strcpy(rv);
    *redis_version_num = parse_semver(*redis_version);

    const char *sn = KVModule_ServerInfoGetFieldC(info, "server_name");
    *server_name = lm_strcpy(sn);

    const char *vv = KVModule_ServerInfoGetFieldC(info, "kv_version");
    *kv_version = lm_strcpy(vv);
    *kv_version_num = parse_semver(*kv_version);

    KVModule_FreeServerInfo(ctx, info);
}

static void initializeLuaState(luaEngineCtx *lua_engine_ctx,
                               KVModuleScriptingEngineSubsystemType type) {
    lua_State *lua = lua_open();

    if (type == VMSE_EVAL) {
        lua_engine_ctx->eval_lua = lua;
    } else {
        KVModule_Assert(type == VMSE_FUNCTION);
        lua_engine_ctx->function_lua = lua;
    }

    luaRegisterServerAPI(lua_engine_ctx, lua);
    luaStateInstallErrorHandler(lua);

    if (type == VMSE_EVAL) {
        initializeEvalLuaState(lua);
        luaStateLockGlobalTable(lua);
    } else {
        luaStateLockGlobalTable(lua);
        luaFunctionInitializeLuaState(lua_engine_ctx, lua);
    }
}

static struct luaEngineCtx *createEngineContext(KVModuleCtx *ctx) {
    luaEngineCtx *lua_engine_ctx = KVModule_Alloc(sizeof(*lua_engine_ctx));

    get_version_info(ctx,
                     &lua_engine_ctx->redis_version,
                     &lua_engine_ctx->redis_version_num,
                     &lua_engine_ctx->server_name,
                     &lua_engine_ctx->kv_version,
                     &lua_engine_ctx->kv_version_num);

    lua_engine_ctx->lua_enable_insecure_api = 0;

    initializeLuaState(lua_engine_ctx, VMSE_EVAL);
    initializeLuaState(lua_engine_ctx, VMSE_FUNCTION);

    return lua_engine_ctx;
}

static void destroyEngineContext(luaEngineCtx *lua_engine_ctx) {
    lua_close(lua_engine_ctx->eval_lua);
    lua_close(lua_engine_ctx->function_lua);
    KVModule_Free(lua_engine_ctx->redis_version);
    KVModule_Free(lua_engine_ctx->server_name);
    KVModule_Free(lua_engine_ctx->kv_version);
    KVModule_Free(lua_engine_ctx);
}

static KVModuleScriptingEngineMemoryInfo luaEngineGetMemoryInfo(KVModuleCtx *module_ctx,
                                                                    KVModuleScriptingEngineCtx *engine_ctx,
                                                                    KVModuleScriptingEngineSubsystemType type) {
    KVMODULE_NOT_USED(module_ctx);
    luaEngineCtx *lua_engine_ctx = engine_ctx;
    KVModuleScriptingEngineMemoryInfo mem_info = {0};

    if (type == VMSE_EVAL || type == VMSE_ALL) {
        mem_info.used_memory += luaMemory(lua_engine_ctx->eval_lua);
    }
    if (type == VMSE_FUNCTION || type == VMSE_ALL) {
        mem_info.used_memory += luaMemory(lua_engine_ctx->function_lua);
    }

    mem_info.engine_memory_overhead = KVModule_MallocSize(engine_ctx);

    return mem_info;
}

static KVModuleScriptingEngineCompiledFunction **luaEngineCompileCode(KVModuleCtx *module_ctx,
                                                                          KVModuleScriptingEngineCtx *engine_ctx,
                                                                          KVModuleScriptingEngineSubsystemType type,
                                                                          const char *code,
                                                                          size_t code_len,
                                                                          size_t timeout,
                                                                          size_t *out_num_compiled_functions,
                                                                          KVModuleString **err) {
    luaEngineCtx *lua_engine_ctx = (luaEngineCtx *)engine_ctx;
    KVModuleScriptingEngineCompiledFunction **functions = NULL;

    if (type == VMSE_EVAL) {
        lua_State *lua = lua_engine_ctx->eval_lua;

        if (luaL_loadbuffer(
                lua, code, code_len, "@user_script")) {
            *err = KVModule_CreateStringPrintf(module_ctx, "Error compiling script (new function): %s", lua_tostring(lua, -1));
            lua_pop(lua, 1);
            return functions;
        }

        KVModule_Assert(lua_isfunction(lua, -1));
        int function_ref = luaL_ref(lua, LUA_REGISTRYINDEX);

        luaFunction *script = KVModule_Calloc(1, sizeof(luaFunction));
        *script = (luaFunction){
            .lua = lua,
            .function_ref = function_ref,
        };

        KVModuleScriptingEngineCompiledFunction *func = KVModule_Alloc(sizeof(*func));
        *func = (KVModuleScriptingEngineCompiledFunction){
            .name = NULL,
            .function = script,
            .desc = NULL,
            .f_flags = 0};

        *out_num_compiled_functions = 1;
        functions = KVModule_Calloc(1, sizeof(KVModuleScriptingEngineCompiledFunction *));
        *functions = func;
    } else {
        functions = luaFunctionLibraryCreate(lua_engine_ctx->function_lua,
                                             code,
                                             timeout,
                                             out_num_compiled_functions,
                                             err);
    }

    return functions;
}

static void luaEngineFunctionCall(KVModuleCtx *module_ctx,
                                  KVModuleScriptingEngineCtx *engine_ctx,
                                  KVModuleScriptingEngineServerRuntimeCtx *server_ctx,
                                  KVModuleScriptingEngineCompiledFunction *compiled_function,
                                  KVModuleScriptingEngineSubsystemType type,
                                  KVModuleString **keys,
                                  size_t nkeys,
                                  KVModuleString **args,
                                  size_t nargs) {
    luaEngineCtx *lua_engine_ctx = (luaEngineCtx *)engine_ctx;
    lua_State *lua = type == VMSE_EVAL ? lua_engine_ctx->eval_lua : lua_engine_ctx->function_lua;
    luaFunction *script = compiled_function->function;
    int lua_function_ref = script->function_ref;

    /* Push the pcall error handler function on the stack. */
    lua_pushstring(lua, REGISTRY_ERROR_HANDLER_NAME);
    lua_gettable(lua, LUA_REGISTRYINDEX);

    lua_rawgeti(lua, LUA_REGISTRYINDEX, lua_function_ref);
    KVModule_Assert(!lua_isnil(lua, -1));

    luaCallFunction(module_ctx,
                    server_ctx,
                    type,
                    lua,
                    keys,
                    nkeys,
                    args,
                    nargs,
                    type == VMSE_EVAL ? ldbIsActive() : 0,
                    lua_engine_ctx->lua_enable_insecure_api);

    lua_pop(lua, 1); /* Remove the error handler. */
}

static void resetLuaContext(void *context) {
    lua_State *lua = context;
    lua_gc(lua, LUA_GCCOLLECT, 0);
    lua_close(lua);

#if defined(__GLIBC__) && !defined(USE_LIBC)
    /* The lua interpreter may hold a lot of memory internally, and lua is
     * using libc. libc may take a bit longer to return the memory to the OS,
     * so after lua_close, we call malloc_trim try to purge it earlier.
     *
     * We do that only when the server itself does not use libc. When Lua and the server
     * use different allocators, one won't use the fragmentation holes of the
     * other, and released memory can take a long time until it is returned to
     * the OS. */
    malloc_trim(0);
#endif
}

static int isLuaInsecureAPIEnabled(KVModuleCtx *module_ctx) {
    int result = 0;
    KVModuleCallReply *reply = KVModule_Call(module_ctx, "CONFIG", "ccE", "GET", "lua-enable-insecure-api");
    if (KVModule_CallReplyType(reply) == KVMODULE_REPLY_ERROR) {
        KVModule_Log(module_ctx,
                         "warning",
                         "Unable to determine 'lua-enable-insecure-api' configuration value: %s",
                         KVModule_CallReplyStringPtr(reply, NULL));
        KVModule_FreeCallReply(reply);
        return 0;
    }
    KVModule_Assert(KVModule_CallReplyType(reply) == KVMODULE_REPLY_ARRAY &&
                        KVModule_CallReplyLength(reply) == 2);
    KVModuleCallReply *val = KVModule_CallReplyArrayElement(reply, 1);
    KVModule_Assert(KVModule_CallReplyType(val) == KVMODULE_REPLY_STRING);
    const char *val_str = KVModule_CallReplyStringPtr(val, NULL);
    result = strncmp(val_str, "yes", 3) == 0;
    KVModule_FreeCallReply(reply);
    return result;
}

static KVModuleScriptingEngineCallableLazyEnvReset *luaEngineResetEnv(KVModuleCtx *module_ctx,
                                                                          KVModuleScriptingEngineCtx *engine_ctx,
                                                                          KVModuleScriptingEngineSubsystemType type,
                                                                          int async) {
    KVMODULE_NOT_USED(module_ctx);
    luaEngineCtx *lua_engine_ctx = (luaEngineCtx *)engine_ctx;
    KVModule_Assert(type == VMSE_EVAL || type == VMSE_FUNCTION);
    lua_State *lua = type == VMSE_EVAL ? lua_engine_ctx->eval_lua : lua_engine_ctx->function_lua;
    KVModule_Assert(lua);
    KVModuleScriptingEngineCallableLazyEnvReset *callback = NULL;

    if (async) {
        callback = KVModule_Calloc(1, sizeof(*callback));
        *callback = (KVModuleScriptingEngineCallableLazyEnvReset){
            .context = lua,
            .engineLazyEnvResetCallback = resetLuaContext,
        };
    } else {
        resetLuaContext(lua);
    }

    lua_engine_ctx->lua_enable_insecure_api = isLuaInsecureAPIEnabled(module_ctx);

    initializeLuaState(lua_engine_ctx, type);

    return callback;
}

static size_t luaEngineFunctionMemoryOverhead(KVModuleCtx *module_ctx,
                                              KVModuleScriptingEngineCompiledFunction *compiled_function) {
    KVMODULE_NOT_USED(module_ctx);
    return KVModule_MallocSize(compiled_function->function) +
           (compiled_function->name ? KVModule_MallocSize(compiled_function->name) : 0) +
           (compiled_function->desc ? KVModule_MallocSize(compiled_function->desc) : 0) +
           KVModule_MallocSize(compiled_function);
}

static void luaEngineFreeFunction(KVModuleCtx *module_ctx,
                                  KVModuleScriptingEngineCtx *engine_ctx,
                                  KVModuleScriptingEngineSubsystemType type,
                                  KVModuleScriptingEngineCompiledFunction *compiled_function) {
    KVMODULE_NOT_USED(module_ctx);
    KVModule_Assert(type == VMSE_EVAL || type == VMSE_FUNCTION);

    luaEngineCtx *lua_engine_ctx = engine_ctx;
    lua_State *lua = type == VMSE_EVAL ? lua_engine_ctx->eval_lua : lua_engine_ctx->function_lua;
    KVModule_Assert(lua);

    luaFunction *script = (luaFunction *)compiled_function->function;
    if (lua == script->lua) {
        /* The lua context is still the same, which means that we're not
         * resetting the whole eval context, and therefore, we need to
         * delete the function from the lua context.
         */
        lua_unref(lua, script->function_ref);
    }
    KVModule_Free(script);

    if (compiled_function->name) {
        KVModule_Free(compiled_function->name);
    }
    if (compiled_function->desc) {
        KVModule_Free(compiled_function->desc);
    }
    KVModule_Free(compiled_function);
}

static KVModuleScriptingEngineDebuggerEnableRet luaEngineDebuggerEnable(KVModuleCtx *module_ctx,
                                                                            KVModuleScriptingEngineCtx *engine_ctx,
                                                                            KVModuleScriptingEngineSubsystemType type,
                                                                            const KVModuleScriptingEngineDebuggerCommand **commands,
                                                                            size_t *commands_len) {
    KVMODULE_NOT_USED(module_ctx);

    if (type != VMSE_EVAL) {
        return VMSE_DEBUG_NOT_SUPPORTED;
    }

    ldbEnable();

    luaEngineCtx *lua_engine_ctx = engine_ctx;
    ldbGenerateDebuggerCommandsArray(lua_engine_ctx->eval_lua,
                                     commands,
                                     commands_len);

    return VMSE_DEBUG_ENABLED;
}

static void luaEngineDebuggerDisable(KVModuleCtx *module_ctx,
                                     KVModuleScriptingEngineCtx *engine_ctx,
                                     KVModuleScriptingEngineSubsystemType type) {
    KVMODULE_NOT_USED(module_ctx);
    KVMODULE_NOT_USED(engine_ctx);
    KVMODULE_NOT_USED(type);
    ldbDisable();
}

static void luaEngineDebuggerStart(KVModuleCtx *module_ctx,
                                   KVModuleScriptingEngineCtx *engine_ctx,
                                   KVModuleScriptingEngineSubsystemType type,
                                   KVModuleString *source) {
    KVMODULE_NOT_USED(module_ctx);
    KVMODULE_NOT_USED(engine_ctx);
    KVMODULE_NOT_USED(type);
    ldbStart(source);
}

static void luaEngineDebuggerEnd(KVModuleCtx *module_ctx,
                                 KVModuleScriptingEngineCtx *engine_ctx,
                                 KVModuleScriptingEngineSubsystemType type) {
    KVMODULE_NOT_USED(module_ctx);
    KVMODULE_NOT_USED(engine_ctx);
    KVMODULE_NOT_USED(type);
    ldbEnd();
}

static struct luaEngineCtx *engine_ctx = NULL;

int KVModule_OnLoad(KVModuleCtx *ctx,
                        KVModuleString **argv,
                        int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx, "lua", 1, KVMODULE_APIVER_1) == KVMODULE_ERR) {
        return KVMODULE_ERR;
    }

    KVModule_SetModuleOptions(ctx, KVMODULE_OPTIONS_HANDLE_REPL_ASYNC_LOAD |
                                           KVMODULE_OPTIONS_HANDLE_ATOMIC_SLOT_MIGRATION);

    engine_ctx = createEngineContext(ctx);

    if (KVModule_LoadConfigs(ctx) == KVMODULE_ERR) {
        KVModule_Log(ctx, "warning", "Failed to load LUA module configs");
        destroyEngineContext(engine_ctx);
        engine_ctx = NULL;
        return KVMODULE_ERR;
    }

    KVModuleScriptingEngineMethods methods = {
        .version = KVMODULE_SCRIPTING_ENGINE_ABI_VERSION,
        .compile_code = luaEngineCompileCode,
        .free_function = luaEngineFreeFunction,
        .call_function = luaEngineFunctionCall,
        .get_function_memory_overhead = luaEngineFunctionMemoryOverhead,
        .reset_env = luaEngineResetEnv,
        .get_memory_info = luaEngineGetMemoryInfo,
        .debugger_enable = luaEngineDebuggerEnable,
        .debugger_disable = luaEngineDebuggerDisable,
        .debugger_start = luaEngineDebuggerStart,
        .debugger_end = luaEngineDebuggerEnd,
    };

    int result = KVModule_RegisterScriptingEngine(ctx,
                                                      LUA_ENGINE_NAME,
                                                      engine_ctx,
                                                      &methods);

    if (result == KVMODULE_ERR) {
        KVModule_Log(ctx, "warning", "Failed to register LUA scripting engine");
        destroyEngineContext(engine_ctx);
        engine_ctx = NULL;
        return KVMODULE_ERR;
    }

    engine_ctx->lua_enable_insecure_api = isLuaInsecureAPIEnabled(ctx);

    return KVMODULE_OK;
}

int KVModule_OnUnload(KVModuleCtx *ctx) {
    if (KVModule_UnregisterScriptingEngine(ctx, LUA_ENGINE_NAME) != KVMODULE_OK) {
        KVModule_Log(ctx, "error", "Failed to unregister engine");
        return KVMODULE_ERR;
    }

    destroyEngineContext(engine_ctx);
    engine_ctx = NULL;

    return KVMODULE_OK;
}
