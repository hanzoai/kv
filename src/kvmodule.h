/*
 * kvmodule.h
 *
 * This header file is forked from redismodule.h to reflect the new naming conventions adopted in KV.
 *
 * Key Changes:
 * - Symbolic names have been changed from containing RedisModule, REDISMODULE, etc. to KVModule, KVMODULE, etc.
 * to align with the new module naming convention. Developers must use these new symbolic names in their module
 *   implementations.
 * - Terminology has been updated to be more inclusive: "slave" is now "replica", and "master"
 *   is now "primary". These changes are part of an effort to use more accurate and inclusive language.
 *
 * When developing modules for KV, ensure to include "kvmodule.h". This header file contains
 * the updated definitions and should be used to maintain compatibility with the changes made in KV.
 */

#ifndef KVMODULE_H
#define KVMODULE_H

#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>


typedef struct KVModuleString KVModuleString;
typedef struct KVModuleKey KVModuleKey;

/* -------------- Defines NOT common between core and modules ------------- */

#if defined KVMODULE_CORE
/* Things only defined for the modules core (server), not exported to modules
 * that include this file. */

#define KVModuleString robj

#endif /* defined KVMODULE_CORE */

#if !defined KVMODULE_CORE && !defined KVMODULE_CORE_MODULE
/* Things defined for modules, but not for core-modules. */

typedef long long mstime_t;
typedef long long ustime_t;

#endif /* !defined KVMODULE_CORE && !defined KVMODULE_CORE_MODULE */

/* ---------------- Defines common between core and modules --------------- */

/* Error status return values. */
#define KVMODULE_OK 0
#define KVMODULE_ERR 1

/* Module Based Authentication status return values. */
#define KVMODULE_AUTH_HANDLED 0
#define KVMODULE_AUTH_NOT_HANDLED 1

/* API versions. */
#define KVMODULE_APIVER_1 1

/* Version of the KVModuleTypeMethods structure. Once the KVModuleTypeMethods
 * structure is changed, this version number needs to be changed synchronistically. */
#define KVMODULE_TYPE_METHOD_VERSION 5

/* API flags and constants */
#define KVMODULE_READ (1 << 0)
#define KVMODULE_WRITE (1 << 1)

/* KVModule_OpenKey extra flags for the 'mode' argument.
 * Avoid touching the LRU/LFU of the key when opened. */
#define KVMODULE_OPEN_KEY_NOTOUCH (1 << 16)
/* Don't trigger keyspace event on key misses. */
#define KVMODULE_OPEN_KEY_NONOTIFY (1 << 17)
/* Don't update keyspace hits/misses counters. */
#define KVMODULE_OPEN_KEY_NOSTATS (1 << 18)
/* Avoid deleting lazy expired keys. */
#define KVMODULE_OPEN_KEY_NOEXPIRE (1 << 19)
/* Avoid any effects from fetching the key */
#define KVMODULE_OPEN_KEY_NOEFFECTS (1 << 20)
/* Mask of all KVMODULE_OPEN_KEY_* values. Any new mode should be added to this list.
 * Should not be used directly by the module, use RM_GetOpenKeyModesAll instead.
 * Located here so when we will add new modes we will not forget to update it. */
#define _KVMODULE_OPEN_KEY_ALL                                                                            \
    KVMODULE_READ | KVMODULE_WRITE | KVMODULE_OPEN_KEY_NOTOUCH | KVMODULE_OPEN_KEY_NONOTIFY | \
        KVMODULE_OPEN_KEY_NOSTATS | KVMODULE_OPEN_KEY_NOEXPIRE | KVMODULE_OPEN_KEY_NOEFFECTS

/* List push and pop */
#define KVMODULE_LIST_HEAD 0
#define KVMODULE_LIST_TAIL 1

/* Key types. */
#define KVMODULE_KEYTYPE_EMPTY 0
#define KVMODULE_KEYTYPE_STRING 1
#define KVMODULE_KEYTYPE_LIST 2
#define KVMODULE_KEYTYPE_HASH 3
#define KVMODULE_KEYTYPE_SET 4
#define KVMODULE_KEYTYPE_ZSET 5
#define KVMODULE_KEYTYPE_MODULE 6
#define KVMODULE_KEYTYPE_STREAM 7

/* Reply types. */
#define KVMODULE_REPLY_UNKNOWN -1
#define KVMODULE_REPLY_STRING 0
#define KVMODULE_REPLY_ERROR 1
#define KVMODULE_REPLY_INTEGER 2
#define KVMODULE_REPLY_ARRAY 3
#define KVMODULE_REPLY_NULL 4
#define KVMODULE_REPLY_MAP 5
#define KVMODULE_REPLY_SET 6
#define KVMODULE_REPLY_BOOL 7
#define KVMODULE_REPLY_DOUBLE 8
#define KVMODULE_REPLY_BIG_NUMBER 9
#define KVMODULE_REPLY_VERBATIM_STRING 10
#define KVMODULE_REPLY_ATTRIBUTE 11
#define KVMODULE_REPLY_PROMISE 12
#define KVMODULE_REPLY_SIMPLE_STRING 13
#define KVMODULE_REPLY_ARRAY_NULL 14

/* Postponed array length. */
#define KVMODULE_POSTPONED_ARRAY_LEN -1 /* Deprecated, please use KVMODULE_POSTPONED_LEN */
#define KVMODULE_POSTPONED_LEN -1

/* Expire */
#define KVMODULE_NO_EXPIRE -1

/* Sorted set API flags. */
#define KVMODULE_ZADD_XX (1 << 0)
#define KVMODULE_ZADD_NX (1 << 1)
#define KVMODULE_ZADD_ADDED (1 << 2)
#define KVMODULE_ZADD_UPDATED (1 << 3)
#define KVMODULE_ZADD_NOP (1 << 4)
#define KVMODULE_ZADD_GT (1 << 5)
#define KVMODULE_ZADD_LT (1 << 6)

/* Hash API flags. */
#define KVMODULE_HASH_NONE 0
#define KVMODULE_HASH_NX (1 << 0)
#define KVMODULE_HASH_XX (1 << 1)
#define KVMODULE_HASH_CFIELDS (1 << 2)
#define KVMODULE_HASH_EXISTS (1 << 3)
#define KVMODULE_HASH_COUNT_ALL (1 << 4)

#define KVMODULE_CONFIG_DEFAULT 0                /* This is the default for a module config. */
#define KVMODULE_CONFIG_IMMUTABLE (1ULL << 0)    /* Can this value only be set at startup? */
#define KVMODULE_CONFIG_SENSITIVE (1ULL << 1)    /* Does this value contain sensitive information */
#define KVMODULE_CONFIG_HIDDEN (1ULL << 4)       /* This config is hidden in `config get <pattern>` (used for tests/debugging) */
#define KVMODULE_CONFIG_PROTECTED (1ULL << 5)    /* Becomes immutable if enable-protected-configs is enabled. */
#define KVMODULE_CONFIG_DENY_LOADING (1ULL << 6) /* This config is forbidden during loading. */

#define KVMODULE_CONFIG_MEMORY (1ULL << 7)   /* Indicates if this value can be set as a memory value */
#define KVMODULE_CONFIG_BITFLAGS (1ULL << 8) /* Indicates if this value can be set as a multiple enum values */

/* StreamID type. */
typedef struct KVModuleStreamID {
    uint64_t ms;
    uint64_t seq;
} KVModuleStreamID;

/* StreamAdd() flags. */
#define KVMODULE_STREAM_ADD_AUTOID (1 << 0)
/* StreamIteratorStart() flags. */
#define KVMODULE_STREAM_ITERATOR_EXCLUSIVE (1 << 0)
#define KVMODULE_STREAM_ITERATOR_REVERSE (1 << 1)
/* StreamIteratorTrim*() flags. */
#define KVMODULE_STREAM_TRIM_APPROX (1 << 0)

/* Context Flags: Info about the current context returned by
 * RM_GetContextFlags(). */

/* The command is running in the context of a Lua script */
#define KVMODULE_CTX_FLAGS_LUA (1 << 0)
/* The command is running inside a KV transaction */
#define KVMODULE_CTX_FLAGS_MULTI (1 << 1)
/* The instance is a primary */
#define KVMODULE_CTX_FLAGS_PRIMARY (1 << 2)
/* The instance is a replica */
#define KVMODULE_CTX_FLAGS_REPLICA (1 << 3)
/* The instance is read-only (usually meaning it's a replica as well) */
#define KVMODULE_CTX_FLAGS_READONLY (1 << 4)
/* The instance is running in cluster mode */
#define KVMODULE_CTX_FLAGS_CLUSTER (1 << 5)
/* The instance has AOF enabled */
#define KVMODULE_CTX_FLAGS_AOF (1 << 6)
/* The instance has RDB enabled */
#define KVMODULE_CTX_FLAGS_RDB (1 << 7)
/* The instance has Maxmemory set */
#define KVMODULE_CTX_FLAGS_MAXMEMORY (1 << 8)
/* Maxmemory is set and has an eviction policy that may delete keys */
#define KVMODULE_CTX_FLAGS_EVICT (1 << 9)
/* KV is out of memory according to the maxmemory flag. */
#define KVMODULE_CTX_FLAGS_OOM (1 << 10)
/* Less than 25% of memory available according to maxmemory. */
#define KVMODULE_CTX_FLAGS_OOM_WARNING (1 << 11)
/* The command was sent over the replication link. */
#define KVMODULE_CTX_FLAGS_REPLICATED (1 << 12)
/* KV is currently loading either from AOF or RDB. */
#define KVMODULE_CTX_FLAGS_LOADING (1 << 13)
/* The replica has no link with its primary, note that
 * there is the inverse flag as well:
 *
 *  KVMODULE_CTX_FLAGS_REPLICA_IS_ONLINE
 *
 * The two flags are exclusive, one or the other can be set. */
#define KVMODULE_CTX_FLAGS_REPLICA_IS_STALE (1 << 14)
/* The replica is trying to connect with the primary.
 * (REPL_STATE_CONNECT and REPL_STATE_CONNECTING states) */
#define KVMODULE_CTX_FLAGS_REPLICA_IS_CONNECTING (1 << 15)
/* THe replica is receiving an RDB file from its primary. */
#define KVMODULE_CTX_FLAGS_REPLICA_IS_TRANSFERRING (1 << 16)
/* The replica is online, receiving updates from its primary. */
#define KVMODULE_CTX_FLAGS_REPLICA_IS_ONLINE (1 << 17)
/* There is currently some background process active. */
#define KVMODULE_CTX_FLAGS_ACTIVE_CHILD (1 << 18)
/* The next EXEC will fail due to dirty CAS (touched keys). */
#define KVMODULE_CTX_FLAGS_MULTI_DIRTY (1 << 19)
/* KV is currently running inside background child process. */
#define KVMODULE_CTX_FLAGS_IS_CHILD (1 << 20)
/* The current client does not allow blocking, either called from
 * within multi, lua, or from another module using RM_Call */
#define KVMODULE_CTX_FLAGS_DENY_BLOCKING (1 << 21)
/* The current client uses RESP3 protocol */
#define KVMODULE_CTX_FLAGS_RESP3 (1 << 22)
/* KV is currently async loading database for diskless replication. */
#define KVMODULE_CTX_FLAGS_ASYNC_LOADING (1 << 23)
/* KV is starting. */
#define KVMODULE_CTX_FLAGS_SERVER_STARTUP (1 << 24)
/* The current client is the slot import client */
#define KVMODULE_CTX_FLAGS_SLOT_IMPORT_CLIENT (1 << 25)
/* The current client is the slot export client */
#define KVMODULE_CTX_FLAGS_SLOT_EXPORT_CLIENT (1 << 26)

/* Next context flag, must be updated when adding new flags above!
This flag should not be used directly by the module.
 * Use KVModule_GetContextFlagsAll instead. */
#define _KVMODULE_CTX_FLAGS_NEXT (1 << 27)

/* Keyspace changes notification classes. Every class is associated with a
 * character for configuration purposes.
 * NOTE: These have to be in sync with NOTIFY_* in server.h */
#define KVMODULE_NOTIFY_KEYSPACE (1 << 0)  /* K */
#define KVMODULE_NOTIFY_KEYEVENT (1 << 1)  /* E */
#define KVMODULE_NOTIFY_GENERIC (1 << 2)   /* g */
#define KVMODULE_NOTIFY_STRING (1 << 3)    /* $ */
#define KVMODULE_NOTIFY_LIST (1 << 4)      /* l */
#define KVMODULE_NOTIFY_SET (1 << 5)       /* s */
#define KVMODULE_NOTIFY_HASH (1 << 6)      /* h */
#define KVMODULE_NOTIFY_ZSET (1 << 7)      /* z */
#define KVMODULE_NOTIFY_EXPIRED (1 << 8)   /* x */
#define KVMODULE_NOTIFY_EVICTED (1 << 9)   /* e */
#define KVMODULE_NOTIFY_STREAM (1 << 10)   /* t */
#define KVMODULE_NOTIFY_KEY_MISS (1 << 11) /* m (Note: This one is excluded from KVMODULE_NOTIFY_ALL on purpose) */
#define KVMODULE_NOTIFY_LOADED (1 << 12)   /* module only key space notification, indicate a key loaded from rdb */
#define KVMODULE_NOTIFY_MODULE (1 << 13)   /* d, module key space notification */
#define KVMODULE_NOTIFY_NEW (1 << 14)      /* n, new key notification */

/* Next notification flag, must be updated when adding new flags above!
This flag should not be used directly by the module.
 * Use KVModule_GetKeyspaceNotificationFlagsAll instead. */
#define _KVMODULE_NOTIFY_NEXT (1 << 15)

#define KVMODULE_NOTIFY_ALL                                                                                        \
    (KVMODULE_NOTIFY_GENERIC | KVMODULE_NOTIFY_STRING | KVMODULE_NOTIFY_LIST | KVMODULE_NOTIFY_SET |   \
     KVMODULE_NOTIFY_HASH | KVMODULE_NOTIFY_ZSET | KVMODULE_NOTIFY_EXPIRED | KVMODULE_NOTIFY_EVICTED | \
     KVMODULE_NOTIFY_STREAM | KVMODULE_NOTIFY_MODULE) /* A */

/* A special pointer that we can use between the core and the module to signal
 * field deletion, and that is impossible to be a valid pointer. */
#define KVMODULE_HASH_DELETE ((KVModuleString *)(long)1)

/* Error messages. */
#define KVMODULE_ERRORMSG_WRONGTYPE "WRONGTYPE Operation against a key holding the wrong kind of value"

#define KVMODULE_POSITIVE_INFINITE (1.0 / 0.0)
#define KVMODULE_NEGATIVE_INFINITE (-1.0 / 0.0)

/* Cluster API defines. */
#define KVMODULE_NODE_ID_LEN 40
#define KVMODULE_NODE_MYSELF (1 << 0)
#define KVMODULE_NODE_PRIMARY (1 << 1)
#define KVMODULE_NODE_REPLICA (1 << 2)
#define KVMODULE_NODE_PFAIL (1 << 3)
#define KVMODULE_NODE_FAIL (1 << 4)
#define KVMODULE_NODE_NOFAILOVER (1 << 5)

#define KVMODULE_CLUSTER_FLAG_NONE 0
#define KVMODULE_CLUSTER_FLAG_NO_FAILOVER (1 << 1)
#define KVMODULE_CLUSTER_FLAG_NO_REDIRECTION (1 << 2)

#define KVMODULE_NOT_USED(V) ((void)V)

/* Logging level strings */
#define KVMODULE_LOGLEVEL_DEBUG "debug"
#define KVMODULE_LOGLEVEL_VERBOSE "verbose"
#define KVMODULE_LOGLEVEL_NOTICE "notice"
#define KVMODULE_LOGLEVEL_WARNING "warning"

/* Bit flags for aux_save_triggers and the aux_load and aux_save callbacks */
#define KVMODULE_AUX_BEFORE_RDB (1 << 0)
#define KVMODULE_AUX_AFTER_RDB (1 << 1)

/* RM_Yield flags */
#define KVMODULE_YIELD_FLAG_NONE (1 << 0)
#define KVMODULE_YIELD_FLAG_CLIENTS (1 << 1)

/* RM_BlockClientOnKeysWithFlags flags */
#define KVMODULE_BLOCK_UNBLOCK_DEFAULT (0)
#define KVMODULE_BLOCK_UNBLOCK_DELETED (1 << 0)

/* This type represents a timer handle, and is returned when a timer is
 * registered and used in order to invalidate a timer. It's just a 64 bit
 * number, because this is how each timer is represented inside the radix tree
 * of timers that are going to expire, sorted by expire time. */
typedef uint64_t KVModuleTimerID;

/* CommandFilter Flags */

/* Do filter KVModule_Call() commands initiated by module itself. */
#define KVMODULE_CMDFILTER_NOSELF (1 << 0)

/* Declare that the module can handle errors with KVModule_SetModuleOptions. */
#define KVMODULE_OPTIONS_HANDLE_IO_ERRORS (1 << 0)

/* When set, KV will not call KVModule_SignalModifiedKey(), implicitly in
 * KVModule_CloseKey, and the module needs to do that when manually when keys
 * are modified from the user's perspective, to invalidate WATCH. */
#define KVMODULE_OPTION_NO_IMPLICIT_SIGNAL_MODIFIED (1 << 1)

/* Declare that the module can handle diskless async replication with KVModule_SetModuleOptions. */
#define KVMODULE_OPTIONS_HANDLE_REPL_ASYNC_LOAD (1 << 2)

/* Declare that the module want to get nested key space notifications.
 * If enabled, the module is responsible to break endless loop. */
#define KVMODULE_OPTIONS_ALLOW_NESTED_KEYSPACE_NOTIFICATIONS (1 << 3)

/* Skipping command validation can improve performance by reducing the overhead associated
 * with command checking, especially in high-throughput scenarios where commands
 * are already pre-validated or trusted. */
#define KVMODULE_OPTIONS_SKIP_COMMAND_VALIDATION (1 << 4)

/* Declare that the module can handle atomic slot migration. When not set,
 * CLUSTER MIGRATESLOTS will return an error, and the CLUSTER SETSLOTS based
 * slot migration must be used. */
#define KVMODULE_OPTIONS_HANDLE_ATOMIC_SLOT_MIGRATION (1 << 5)

/* Next option flag, must be updated when adding new module flags above!
 * This flag should not be used directly by the module.
 * Use KVModule_GetModuleOptionsAll instead. */
#define _KVMODULE_OPTIONS_FLAGS_NEXT (1 << 6)

/* Definitions for KVModule_SetCommandInfo. */

typedef enum {
    KVMODULE_ARG_TYPE_STRING,
    KVMODULE_ARG_TYPE_INTEGER,
    KVMODULE_ARG_TYPE_DOUBLE,
    KVMODULE_ARG_TYPE_KEY, /* A string, but represents a keyname */
    KVMODULE_ARG_TYPE_PATTERN,
    KVMODULE_ARG_TYPE_UNIX_TIME,
    KVMODULE_ARG_TYPE_PURE_TOKEN,
    KVMODULE_ARG_TYPE_ONEOF, /* Must have sub-arguments */
    KVMODULE_ARG_TYPE_BLOCK  /* Must have sub-arguments */
} KVModuleCommandArgType;

#define KVMODULE_CMD_ARG_NONE (0)
#define KVMODULE_CMD_ARG_OPTIONAL (1 << 0)       /* The argument is optional (like GET in SET command) */
#define KVMODULE_CMD_ARG_MULTIPLE (1 << 1)       /* The argument may repeat itself (like key in DEL) */
#define KVMODULE_CMD_ARG_MULTIPLE_TOKEN (1 << 2) /* The argument may repeat itself, and so does its token (like `GET pattern` in SORT) */
#define _KVMODULE_CMD_ARG_NEXT (1 << 3)

typedef enum {
    KVMODULE_KSPEC_BS_INVALID = 0, /* Must be zero. An implicitly value of
                                        * zero is provided when the field is
                                        * absent in a struct literal. */
    KVMODULE_KSPEC_BS_UNKNOWN,
    KVMODULE_KSPEC_BS_INDEX,
    KVMODULE_KSPEC_BS_KEYWORD
} KVModuleKeySpecBeginSearchType;

typedef enum {
    KVMODULE_KSPEC_FK_OMITTED = 0, /* Used when the field is absent in a
                                        * struct literal. Don't use this value
                                        * explicitly. */
    KVMODULE_KSPEC_FK_UNKNOWN,
    KVMODULE_KSPEC_FK_RANGE,
    KVMODULE_KSPEC_FK_KEYNUM
} KVModuleKeySpecFindKeysType;

/* Key-spec flags. For details, see the documentation of
 * KVModule_SetCommandInfo and the key-spec flags in server.h. */
#define KVMODULE_CMD_KEY_RO (1ULL << 0)
#define KVMODULE_CMD_KEY_RW (1ULL << 1)
#define KVMODULE_CMD_KEY_OW (1ULL << 2)
#define KVMODULE_CMD_KEY_RM (1ULL << 3)
#define KVMODULE_CMD_KEY_ACCESS (1ULL << 4)
#define KVMODULE_CMD_KEY_UPDATE (1ULL << 5)
#define KVMODULE_CMD_KEY_INSERT (1ULL << 6)
#define KVMODULE_CMD_KEY_DELETE (1ULL << 7)
#define KVMODULE_CMD_KEY_NOT_KEY (1ULL << 8)
#define KVMODULE_CMD_KEY_INCOMPLETE (1ULL << 9)
#define KVMODULE_CMD_KEY_VARIABLE_FLAGS (1ULL << 10)

/* Channel flags, for details see the documentation of
 * KVModule_ChannelAtPosWithFlags. */
#define KVMODULE_CMD_CHANNEL_PATTERN (1ULL << 0)
#define KVMODULE_CMD_CHANNEL_PUBLISH (1ULL << 1)
#define KVMODULE_CMD_CHANNEL_SUBSCRIBE (1ULL << 2)
#define KVMODULE_CMD_CHANNEL_UNSUBSCRIBE (1ULL << 3)

typedef struct KVModuleCommandArg {
    const char *name;
    KVModuleCommandArgType type;
    int key_spec_index; /* If type is KEY, this is a zero-based index of
                         * the key_spec in the command. For other types,
                         * you may specify -1. */
    const char *token;  /* If type is PURE_TOKEN, this is the token. */
    const char *summary;
    const char *since;
    int flags; /* The KVMODULE_CMD_ARG_* macros. */
    const char *deprecated_since;
    struct KVModuleCommandArg *subargs;
    const char *display_text;
} KVModuleCommandArg;

typedef struct {
    const char *since;
    const char *changes;
} KVModuleCommandHistoryEntry;

typedef struct {
    const char *notes;
    uint64_t flags; /* KVMODULE_CMD_KEY_* macros. */
    KVModuleKeySpecBeginSearchType begin_search_type;
    union {
        struct {
            /* The index from which we start the search for keys */
            int pos;
        } index;
        struct {
            /* The keyword that indicates the beginning of key args */
            const char *keyword;
            /* An index in argv from which to start searching.
             * Can be negative, which means start search from the end, in reverse
             * (Example: -2 means to start in reverse from the penultimate arg) */
            int startfrom;
        } keyword;
    } bs;
    KVModuleKeySpecFindKeysType find_keys_type;
    union {
        struct {
            /* Index of the last key relative to the result of the begin search
             * step. Can be negative, in which case it's not relative. -1
             * indicating till the last argument, -2 one before the last and so
             * on. */
            int lastkey;
            /* How many args should we skip after finding a key, in order to
             * find the next one. */
            int keystep;
            /* If lastkey is -1, we use limit to stop the search by a factor. 0
             * and 1 mean no limit. 2 means 1/2 of the remaining args, 3 means
             * 1/3, and so on. */
            int limit;
        } range;
        struct {
            /* Index of the argument containing the number of keys to come
             * relative to the result of the begin search step */
            int keynumidx;
            /* Index of the fist key. (Usually it's just after keynumidx, in
             * which case it should be set to keynumidx + 1.) */
            int firstkey;
            /* How many args should we skip after finding a key, in order to
             * find the next one, relative to the result of the begin search
             * step. */
            int keystep;
        } keynum;
    } fk;
} KVModuleCommandKeySpec;

typedef struct {
    int version;
    size_t sizeof_historyentry;
    size_t sizeof_keyspec;
    size_t sizeof_arg;
} KVModuleCommandInfoVersion;

static const KVModuleCommandInfoVersion KVModule_CurrentCommandInfoVersion = {
    .version = 1,
    .sizeof_historyentry = sizeof(KVModuleCommandHistoryEntry),
    .sizeof_keyspec = sizeof(KVModuleCommandKeySpec),
    .sizeof_arg = sizeof(KVModuleCommandArg)};

#define KVMODULE_COMMAND_INFO_VERSION (&KVModule_CurrentCommandInfoVersion)

typedef struct {
    /* Always set version to KVMODULE_COMMAND_INFO_VERSION */
    const KVModuleCommandInfoVersion *version;
    const char *summary;                      /* Summary of the command */
    const char *complexity;                   /* Complexity description */
    const char *since;                        /* Debut module version of the command */
    KVModuleCommandHistoryEntry *history; /* History */
    /* A string of space-separated tips meant for clients/proxies regarding this
     * command */
    const char *tips;
    /* Number of arguments, it is possible to use -N to say >= N */
    int arity;
    KVModuleCommandKeySpec *key_specs;
    KVModuleCommandArg *args;
} KVModuleCommandInfo;

/* Eventloop definitions. */
#define KVMODULE_EVENTLOOP_READABLE 1
#define KVMODULE_EVENTLOOP_WRITABLE 2
typedef void (*KVModuleEventLoopFunc)(int fd, void *user_data, int mask);
typedef void (*KVModuleEventLoopOneShotFunc)(void *user_data);

/* Server events definitions.
 * Those flags should not be used directly by the module, instead
 * the module should use KVModuleEvent_* variables.
 * Note: This must be synced with moduleEventVersions */
#define KVMODULE_EVENT_REPLICATION_ROLE_CHANGED 0
#define KVMODULE_EVENT_PERSISTENCE 1
#define KVMODULE_EVENT_FLUSHDB 2
#define KVMODULE_EVENT_LOADING 3
#define KVMODULE_EVENT_CLIENT_CHANGE 4
#define KVMODULE_EVENT_SHUTDOWN 5
#define KVMODULE_EVENT_REPLICA_CHANGE 6
#define KVMODULE_EVENT_PRIMARY_LINK_CHANGE 7
#define KVMODULE_EVENT_CRON_LOOP 8
#define KVMODULE_EVENT_MODULE_CHANGE 9
#define KVMODULE_EVENT_LOADING_PROGRESS 10
#define KVMODULE_EVENT_SWAPDB 11
#define KVMODULE_EVENT_REPL_BACKUP 12 /* Not used anymore. */
#define KVMODULE_EVENT_FORK_CHILD 13
#define KVMODULE_EVENT_REPL_ASYNC_LOAD 14
#define KVMODULE_EVENT_EVENTLOOP 15
#define KVMODULE_EVENT_CONFIG 16
#define KVMODULE_EVENT_KEY 17
#define KVMODULE_EVENT_AUTHENTICATION_ATTEMPT 18
#define KVMODULE_EVENT_ATOMIC_SLOT_MIGRATION 19
#define _KVMODULE_EVENT_NEXT 20 /* Next event flag, should be updated if a new event added. */

typedef struct KVModuleEvent {
    uint64_t id;      /* KVMODULE_EVENT_... defines. */
    uint64_t dataver; /* Version of the structure we pass as 'data'. */
} KVModuleEvent;

struct KVModuleCtx;
struct KVModuleDefragCtx;
typedef void (*KVModuleEventCallback)(struct KVModuleCtx *ctx,
                                          KVModuleEvent eid,
                                          uint64_t subevent,
                                          void *data);

/* IMPORTANT: When adding a new version of one of below structures that contain
 * event data (KVModuleFlushInfoV1 for example) we have to avoid renaming the
 * old KVModuleEvent structure.
 * For example, if we want to add KVModuleFlushInfoV2, the KVModuleEvent
 * structures should be:
 *      KVModuleEvent_FlushDB = {
 *          KVMODULE_EVENT_FLUSHDB,
 *          1
 *      },
 *      KVModuleEvent_FlushDBV2 = {
 *          KVMODULE_EVENT_FLUSHDB,
 *          2
 *      }
 * and NOT:
 *      KVModuleEvent_FlushDBV1 = {
 *          KVMODULE_EVENT_FLUSHDB,
 *          1
 *      },
 *      KVModuleEvent_FlushDB = {
 *          KVMODULE_EVENT_FLUSHDB,
 *          2
 *      }
 * The reason for that is forward-compatibility: We want that module that
 * compiled with a new kvmodule.h to be able to work with a old server,
 * unless the author explicitly decided to use the newer event type.
 */
static const KVModuleEvent KVModuleEvent_ReplicationRoleChanged = {KVMODULE_EVENT_REPLICATION_ROLE_CHANGED,
                                                                           1},
                               KVModuleEvent_Persistence = {KVMODULE_EVENT_PERSISTENCE, 1},
                               KVModuleEvent_FlushDB = {KVMODULE_EVENT_FLUSHDB, 1},
                               KVModuleEvent_Loading = {KVMODULE_EVENT_LOADING, 1},
                               KVModuleEvent_ClientChange = {KVMODULE_EVENT_CLIENT_CHANGE, 1},
                               KVModuleEvent_Shutdown = {KVMODULE_EVENT_SHUTDOWN, 1},
                               KVModuleEvent_ReplicaChange = {KVMODULE_EVENT_REPLICA_CHANGE, 1},
                               KVModuleEvent_CronLoop = {KVMODULE_EVENT_CRON_LOOP, 1},
                               KVModuleEvent_PrimaryLinkChange = {KVMODULE_EVENT_PRIMARY_LINK_CHANGE, 1},
                               KVModuleEvent_ModuleChange = {KVMODULE_EVENT_MODULE_CHANGE, 1},
                               KVModuleEvent_LoadingProgress = {KVMODULE_EVENT_LOADING_PROGRESS, 1},
                               KVModuleEvent_SwapDB = {KVMODULE_EVENT_SWAPDB, 1},
                               KVModuleEvent_ReplAsyncLoad = {KVMODULE_EVENT_REPL_ASYNC_LOAD, 1},
                               KVModuleEvent_ForkChild = {KVMODULE_EVENT_FORK_CHILD, 1},
                               KVModuleEvent_EventLoop = {KVMODULE_EVENT_EVENTLOOP, 1},
                               KVModuleEvent_Config = {KVMODULE_EVENT_CONFIG, 1},
                               KVModuleEvent_Key = {KVMODULE_EVENT_KEY, 1},
                               KVModuleEvent_AuthenticationAttempt = {KVMODULE_EVENT_AUTHENTICATION_ATTEMPT, 1},
                               KVModuleEvent_AtomicSlotMigration = {KVMODULE_EVENT_ATOMIC_SLOT_MIGRATION, 1};

/* Those are values that are used for the 'subevent' callback argument. */
#define KVMODULE_SUBEVENT_PERSISTENCE_RDB_START 0
#define KVMODULE_SUBEVENT_PERSISTENCE_AOF_START 1
#define KVMODULE_SUBEVENT_PERSISTENCE_SYNC_RDB_START 2
#define KVMODULE_SUBEVENT_PERSISTENCE_ENDED 3
#define KVMODULE_SUBEVENT_PERSISTENCE_FAILED 4
#define KVMODULE_SUBEVENT_PERSISTENCE_SYNC_AOF_START 5
#define _KVMODULE_SUBEVENT_PERSISTENCE_NEXT 6

#define KVMODULE_SUBEVENT_LOADING_RDB_START 0
#define KVMODULE_SUBEVENT_LOADING_AOF_START 1
#define KVMODULE_SUBEVENT_LOADING_REPL_START 2
#define KVMODULE_SUBEVENT_LOADING_ENDED 3
#define KVMODULE_SUBEVENT_LOADING_FAILED 4
#define _KVMODULE_SUBEVENT_LOADING_NEXT 5

#define KVMODULE_SUBEVENT_CLIENT_CHANGE_CONNECTED 0
#define KVMODULE_SUBEVENT_CLIENT_CHANGE_DISCONNECTED 1
#define _KVMODULE_SUBEVENT_CLIENT_CHANGE_NEXT 2

#define KVMODULE_SUBEVENT_PRIMARY_LINK_UP 0
#define KVMODULE_SUBEVENT_PRIMARY_LINK_DOWN 1
#define _KVMODULE_SUBEVENT_PRIMARY_NEXT 2

#define KVMODULE_SUBEVENT_REPLICA_CHANGE_ONLINE 0
#define KVMODULE_SUBEVENT_REPLICA_CHANGE_OFFLINE 1
#define _KVMODULE_SUBEVENT_REPLICA_CHANGE_NEXT 2

#define KVMODULE_EVENT_REPLROLECHANGED_NOW_PRIMARY 0
#define KVMODULE_EVENT_REPLROLECHANGED_NOW_REPLICA 1
#define _KVMODULE_EVENT_REPLROLECHANGED_NEXT 2

#define KVMODULE_SUBEVENT_FLUSHDB_START 0
#define KVMODULE_SUBEVENT_FLUSHDB_END 1
#define _KVMODULE_SUBEVENT_FLUSHDB_NEXT 2

#define KVMODULE_SUBEVENT_MODULE_LOADED 0
#define KVMODULE_SUBEVENT_MODULE_UNLOADED 1
#define _KVMODULE_SUBEVENT_MODULE_NEXT 2

#define KVMODULE_SUBEVENT_CONFIG_CHANGE 0
#define _KVMODULE_SUBEVENT_CONFIG_NEXT 1

#define KVMODULE_SUBEVENT_LOADING_PROGRESS_RDB 0
#define KVMODULE_SUBEVENT_LOADING_PROGRESS_AOF 1
#define _KVMODULE_SUBEVENT_LOADING_PROGRESS_NEXT 2

#define KVMODULE_SUBEVENT_REPL_ASYNC_LOAD_STARTED 0
#define KVMODULE_SUBEVENT_REPL_ASYNC_LOAD_ABORTED 1
#define KVMODULE_SUBEVENT_REPL_ASYNC_LOAD_COMPLETED 2
#define _KVMODULE_SUBEVENT_REPL_ASYNC_LOAD_NEXT 3

#define KVMODULE_SUBEVENT_FORK_CHILD_BORN 0
#define KVMODULE_SUBEVENT_FORK_CHILD_DIED 1
#define _KVMODULE_SUBEVENT_FORK_CHILD_NEXT 2

#define KVMODULE_SUBEVENT_EVENTLOOP_BEFORE_SLEEP 0
#define KVMODULE_SUBEVENT_EVENTLOOP_AFTER_SLEEP 1
#define _KVMODULE_SUBEVENT_EVENTLOOP_NEXT 2

#define KVMODULE_SUBEVENT_KEY_DELETED 0
#define KVMODULE_SUBEVENT_KEY_EXPIRED 1
#define KVMODULE_SUBEVENT_KEY_EVICTED 2
#define KVMODULE_SUBEVENT_KEY_OVERWRITTEN 3
#define _KVMODULE_SUBEVENT_KEY_NEXT 4

#define _KVMODULE_SUBEVENT_SHUTDOWN_NEXT 0
#define _KVMODULE_SUBEVENT_CRON_LOOP_NEXT 0
#define _KVMODULE_SUBEVENT_SWAPDB_NEXT 0

#define KVMODULE_SUBEVENT_ATOMIC_SLOT_MIGRATION_IMPORT_STARTED 0
#define KVMODULE_SUBEVENT_ATOMIC_SLOT_MIGRATION_EXPORT_STARTED 1
#define KVMODULE_SUBEVENT_ATOMIC_SLOT_MIGRATION_IMPORT_ABORTED 2
#define KVMODULE_SUBEVENT_ATOMIC_SLOT_MIGRATION_EXPORT_ABORTED 3
#define KVMODULE_SUBEVENT_ATOMIC_SLOT_MIGRATION_IMPORT_COMPLETED 4
#define KVMODULE_SUBEVENT_ATOMIC_SLOT_MIGRATION_EXPORT_COMPLETED 5
#define _KVMODULE_SUBEVENT_ATOMIC_SLOT_MIGRATION_NEXT 6

/* KVModuleClientInfo flags.
 * Note: flags KVMODULE_CLIENTINFO_FLAG_PRIMARY and below were added in KV 9.1 */
#define KVMODULE_CLIENTINFO_FLAG_SSL (1 << 0)
#define KVMODULE_CLIENTINFO_FLAG_PUBSUB (1 << 1)
#define KVMODULE_CLIENTINFO_FLAG_BLOCKED (1 << 2)
#define KVMODULE_CLIENTINFO_FLAG_TRACKING (1 << 3)
#define KVMODULE_CLIENTINFO_FLAG_UNIXSOCKET (1 << 4)
#define KVMODULE_CLIENTINFO_FLAG_MULTI (1 << 5)
#define KVMODULE_CLIENTINFO_FLAG_READONLY (1 << 6)
#define KVMODULE_CLIENTINFO_FLAG_PRIMARY (1 << 7)
#define KVMODULE_CLIENTINFO_FLAG_REPLICA (1 << 8)
#define KVMODULE_CLIENTINFO_FLAG_MONITOR (1 << 9)
#define KVMODULE_CLIENTINFO_FLAG_MODULE (1 << 10)
#define KVMODULE_CLIENTINFO_FLAG_AUTHENTICATED (1 << 11)
#define KVMODULE_CLIENTINFO_FLAG_EVER_AUTHENTICATED (1 << 12)
#define KVMODULE_CLIENTINFO_FLAG_FAKE (1 << 13)

/* Here we take all the structures that the module pass to the core
 * and the other way around. Notably the list here contains the structures
 * used by the hooks API KVModule_RegisterToServerEvent().
 *
 * The structures always start with a 'version' field. This is useful
 * when we want to pass a reference to the structure to the core APIs,
 * for the APIs to fill the structure. In that case, the structure 'version'
 * field is initialized before passing it to the core, so that the core is
 * able to cast the pointer to the appropriate structure version. In this
 * way we obtain ABI compatibility.
 *
 * Here we'll list all the structure versions in case they evolve over time,
 * however using a define, we'll make sure to use the last version as the
 * public name for the module to use. */

#define KVMODULE_CLIENTINFO_VERSION 1
typedef struct KVModuleClientInfo {
    uint64_t version; /* Version of this structure for ABI compat. */
    uint64_t flags;   /* KVMODULE_CLIENTINFO_FLAG_* */
    uint64_t id;      /* Client ID. */
    char addr[46];    /* IPv4 or IPv6 address. */
    uint16_t port;    /* TCP port. */
    uint16_t db;      /* Selected DB. */
} KVModuleClientInfoV1;

#define KVModuleClientInfo KVModuleClientInfoV1

#define KVMODULE_CLIENTINFO_INITIALIZER_V1 {.version = 1}

#define KVMODULE_REPLICATIONINFO_VERSION 1
typedef struct KVModuleReplicationInfo {
    uint64_t version;      /* Not used since this structure is never passed
                              from the module to the core right now. Here
                              for future compatibility. */
    int primary;           /* true if primary, false if replica */
    char *primary_host;    /* primary instance hostname for NOW_REPLICA */
    int primary_port;      /* primary instance port for NOW_REPLICA */
    char *replid1;         /* Main replication ID */
    char *replid2;         /* Secondary replication ID */
    uint64_t repl1_offset; /* Main replication offset */
    uint64_t repl2_offset; /* Offset of replid2 validity */
} KVModuleReplicationInfoV1;

#define KVModuleReplicationInfo KVModuleReplicationInfoV1

#define KVMODULE_FLUSHINFO_VERSION 1
typedef struct KVModuleFlushInfo {
    uint64_t version; /* Not used since this structure is never passed
                         from the module to the core right now. Here
                         for future compatibility. */
    int32_t sync;     /* Synchronous or threaded flush?. */
    int32_t dbnum;    /* Flushed database number, -1 for ALL. */
} KVModuleFlushInfoV1;

#define KVModuleFlushInfo KVModuleFlushInfoV1

#define KVMODULE_MODULE_CHANGE_VERSION 1
typedef struct KVModuleModuleChange {
    uint64_t version;        /* Not used since this structure is never passed
                                from the module to the core right now. Here
                                for future compatibility. */
    const char *module_name; /* Name of module loaded or unloaded. */
    int32_t module_version;  /* Module version. */
} KVModuleModuleChangeV1;

#define KVModuleModuleChange KVModuleModuleChangeV1

#define KVMODULE_CONFIGCHANGE_VERSION 1
typedef struct KVModuleConfigChange {
    uint64_t version;          /* Not used since this structure is never passed
                                  from the module to the core right now. Here
                                  for future compatibility. */
    uint32_t num_changes;      /* how many KV config options were changed */
    const char **config_names; /* the config names that were changed */
} KVModuleConfigChangeV1;

#define KVModuleConfigChange KVModuleConfigChangeV1

#define KVMODULE_CRON_LOOP_VERSION 1
typedef struct KVModuleCronLoopInfo {
    uint64_t version; /* Not used since this structure is never passed
                         from the module to the core right now. Here
                         for future compatibility. */
    int32_t hz;       /* Approximate number of events per second. */
} KVModuleCronLoopV1;

#define KVModuleCronLoop KVModuleCronLoopV1

#define KVMODULE_LOADING_PROGRESS_VERSION 1
typedef struct KVModuleLoadingProgressInfo {
    uint64_t version; /* Not used since this structure is never passed
                         from the module to the core right now. Here
                         for future compatibility. */
    int32_t hz;       /* Approximate number of events per second. */
    int32_t progress; /* Approximate progress between 0 and 1024, or -1
                       * if unknown. */
} KVModuleLoadingProgressV1;

#define KVModuleLoadingProgress KVModuleLoadingProgressV1

#define KVMODULE_SWAPDBINFO_VERSION 1
typedef struct KVModuleSwapDbInfo {
    uint64_t version;     /* Not used since this structure is never passed
                             from the module to the core right now. Here
                             for future compatibility. */
    int32_t dbnum_first;  /* Swap Db first dbnum */
    int32_t dbnum_second; /* Swap Db second dbnum */
} KVModuleSwapDbInfoV1;

#define KVModuleSwapDbInfo KVModuleSwapDbInfoV1

#define KVMODULE_KEYINFO_VERSION 1
typedef struct KVModuleKeyInfo {
    uint64_t version;     /* Not used since this structure is never passed
                             from the module to the core right now. Here
                             for future compatibility. */
    KVModuleKey *key; /* Opened key. */
} KVModuleKeyInfoV1;

#define KVModuleKeyInfo KVModuleKeyInfoV1

#define KVMODULE_AUTHENTICATION_INFO_VERSION 1

typedef enum {
    KVMODULE_AUTH_RESULT_GRANTED = 0, /* Authentication succeeded. */
    KVMODULE_AUTH_RESULT_DENIED = 1,  /* Authentication failed. */
} KVModuleAuthenticationResult;

typedef struct KVModuleAuthenticationInfo {
    uint64_t version;                        /* Version of this structure for ABI compat. */
    uint64_t client_id;                      /* Client ID. */
    const char *username;                    /* Username used for authentication. */
    const char *module_name;                 /* Name of the module that is handling the authentication. */
    KVModuleAuthenticationResult result; /* Result of the authentication */
} KVModuleAuthenticationInfoV1;

#define KVModuleAuthenticationInfo KVModuleAuthenticationInfoV1

#define KVMODULE_AUTHENTICATIONINFO_INITIALIZER_V1 {.version = 1}

#define KVMODULE_ATOMICSLOTMIGRATION_INFO_VERSION 1

typedef struct KVModuleSlotRange {
    int start; /* Start slot, inclusive. */
    int end;   /* End slot, inclusive. */
} KVModuleSlotRange;

typedef struct KVModuleAtomicSlotMigrationInfo {
    uint64_t version;                            /* Version of this structure for ABI compat. */
    char job_name[KVMODULE_NODE_ID_LEN + 1]; /* Unique ID for the migration operation. */
    KVModuleSlotRange *slot_ranges;          /* Array of slot ranges involved in the migration. */
    uint32_t num_slot_ranges;                    /* Number of slot ranges in the array. */
} KVModuleAtomicSlotMigrationInfoV1;

#define KVModuleAtomicSlotMigrationInfo KVModuleAtomicSlotMigrationInfoV1

#define KVMODULE_ATOMICSLOTMIGRATIONINFO_INITIALIZER_V1 {.version = 1}

typedef enum {
    KVMODULE_ACL_LOG_AUTH = 0, /* Authentication failure */
    KVMODULE_ACL_LOG_CMD,      /* Command authorization failure */
    KVMODULE_ACL_LOG_KEY,      /* Key authorization failure */
    KVMODULE_ACL_LOG_CHANNEL,  /* Channel authorization failure */
    KVMODULE_ACL_LOG_DB        /* Database authorization failure */
} KVModuleACLLogEntryReason;

/* Incomplete structures needed by both the core and modules. */
typedef struct KVModuleCtx KVModuleCtx;
typedef struct KVModuleIO KVModuleIO;
typedef struct KVModuleDigest KVModuleDigest;
typedef struct KVModuleInfoCtx KVModuleInfoCtx;
typedef struct KVModuleDefragCtx KVModuleDefragCtx;

/* Function pointers needed by both the core and modules, these needs to be
 * exposed since you can't cast a function pointer to (void *). */
typedef void (*KVModuleInfoFunc)(KVModuleInfoCtx *ctx, int for_crash_report);
typedef void (*KVModuleDefragFunc)(KVModuleDefragCtx *ctx);
typedef void (*KVModuleUserChangedFunc)(uint64_t client_id, void *privdata);

/* Type definitions for implementing scripting engines modules. */
typedef void KVModuleScriptingEngineCtx;
typedef void KVModuleScriptingEngineServerRuntimeCtx;

/* Current ABI version for scripting engine compiled functions structure. */
#define KVMODULE_SCRIPTING_ENGINE_ABI_COMPILED_FUNCTION_VERSION 1UL

/* This struct represents a scripting engine function that results from the
 * compilation of a script by the engine implementation.
 */
typedef struct KVModuleScriptingEngineCompiledFunction {
    uint64_t version;         /* Version of this structure for ABI compat. */
    KVModuleString *name; /* Function name */
    void *function;           /* Opaque object representing a function, usually it's
                                 the function compiled code. */
    KVModuleString *desc; /* Function description */
    uint64_t f_flags;         /* Function flags */
} KVModuleScriptingEngineCompiledFunctionV1;

#define KVModuleScriptingEngineCompiledFunction KVModuleScriptingEngineCompiledFunctionV1

/* Current ABI version for scripting engine memory info structure. */
#define KVMODULE_SCRIPTING_ENGINE_ABI_MEMORY_INFO_VERSION 1UL

/* This struct is used to return the memory information of the scripting
 * engine.
 */
typedef struct KVModuleScriptingEngineMemoryInfo {
    uint64_t version;              /* Version of this structure for ABI compat. */
    size_t used_memory;            /* The memory used by the scripting engine runtime. */
    size_t engine_memory_overhead; /* The memory used by the scripting engine data structures. */
} KVModuleScriptingEngineMemoryInfoV1;

#define KVModuleScriptingEngineMemoryInfo KVModuleScriptingEngineMemoryInfoV1

typedef enum KVModuleScriptingEngineSubsystemType {
    VMSE_EVAL,
    VMSE_FUNCTION,
    VMSE_ALL
} KVModuleScriptingEngineSubsystemType;

typedef enum KVModuleScriptingEngineExecutionState {
    VMSE_STATE_EXECUTING,
    VMSE_STATE_KILLED,
} KVModuleScriptingEngineExecutionState;

typedef enum KVModuleScriptingEngineScriptFlag {
    VMSE_SCRIPT_FLAG_NO_WRITES = (1ULL << 0),
    VMSE_SCRIPT_FLAG_ALLOW_OOM = (1ULL << 1),
    VMSE_SCRIPT_FLAG_ALLOW_STALE = (1ULL << 2),
    VMSE_SCRIPT_FLAG_NO_CLUSTER = (1ULL << 3),
    VMSE_SCRIPT_FLAG_EVAL_COMPAT_MODE = (1ULL << 4), /* EVAL Script backwards compatible behavior, no shebang provided */
    VMSE_SCRIPT_FLAG_ALLOW_CROSS_SLOT = (1ULL << 5),
} KVModuleScriptingEngineScriptFlag;

typedef struct KVModuleScriptingEngineCallableLazyEnvReset {
    void *context;

    /*
     * Callback function used for resetting the EVAL/FUNCTION context implemented by an
     * engine. This callback will be called by a background thread when it's
     * ready for resetting the context.
     *
     * - `context`: a generic pointer to a context object, stored in the
     * callableLazyEnvReset struct.
     *
     */
    void (*engineLazyEnvResetCallback)(void *context);
} KVModuleScriptingEngineCallableLazyEnvReset;

/* The callback function called when either `EVAL`, `SCRIPT LOAD`, or
 * `FUNCTION LOAD` command is called to compile the code.
 * This callback function evaluates the source code passed and produces a list
 * of pointers to the compiled functions structure.
 * In the `EVAL` and `SCRIPT LOAD` case, the list only contains a single
 * function.
 * In the `FUNCTION LOAD` case, there are as many functions as there are calls
 * to the `server.register_function` function in the source code.
 *
 * - `module_ctx`: the module runtime context.
 *
 * - `engine_ctx`: the scripting engine runtime context.
 *
 * - `type`: the subsystem type. Either EVAL or FUNCTION.
 *
 * - `code`: string pointer to the source code.
 *
 * - `code_len`: The length of the code string.
 *
 * - `timeout`: timeout for the library creation (0 for no timeout).
 *
 * - `out_num_compiled_functions`: out param with the number of objects
 *   returned by this function.
 *
 * - `err` - out param with the description of error (if occurred).
 *
 * Returns an array of compiled function objects, or `NULL` if some error
 * occurred.
 */
typedef KVModuleScriptingEngineCompiledFunction **(*KVModuleScriptingEngineCompileCodeFunc)(
    KVModuleCtx *module_ctx,
    KVModuleScriptingEngineCtx *engine_ctx,
    KVModuleScriptingEngineSubsystemType type,
    const char *code,
    size_t code_len,
    size_t timeout,
    size_t *out_num_compiled_functions,
    KVModuleString **err);

/* Version one of source code compilation interface. This API does not allow the compiler to
 * safely handle binary data. You should use a newer version of the API if possible. */
typedef KVModuleScriptingEngineCompiledFunctionV1 **(*KVModuleScriptingEngineCompileCodeFuncV1)(
    KVModuleCtx *module_ctx,
    KVModuleScriptingEngineCtx *engine_ctx,
    KVModuleScriptingEngineSubsystemType type,
    const char *code,
    size_t timeout,
    size_t *out_num_compiled_functions,
    KVModuleString **err);

/* Free the given function.
 *
 * - `module_ctx`: the module runtime context.
 *
 * - `engine_ctx`: the scripting engine runtime context.
 *
 * - `type`: the subsystem where the function is associated with, either `EVAL`
 *   or `FUNCTION`.
 *
 * - `compiled_function`: the compiled function to be freed.
 */
typedef void (*KVModuleScriptingEngineFreeFunctionFunc)(
    KVModuleCtx *module_ctx,
    KVModuleScriptingEngineCtx *engine_ctx,
    KVModuleScriptingEngineSubsystemType type,
    KVModuleScriptingEngineCompiledFunction *compiled_function);

/* The callback function called when either `EVAL`, or`FCALL`, command is
 * called.
 * This callback function executes the `compiled_function` code.
 *
 * - `module_ctx`: the module runtime context.
 *
 * - `engine_ctx`: the scripting engine runtime context.
 *
 * - `server_ctx`: the context opaque structure that represents the server-side
 *   runtime context for the function.
 *
 * - `compiled_function`: pointer to the compiled function registered by the
 *   engine.
 *
 * - `type`: the subsystem type. Either EVAL or FUNCTION.
 *
 * - `keys`: the array of key strings passed in the `FCALL` command.
 *
 * - `nkeys`: the number of elements present in the `keys` array.
 *
 * - `args`: the array of string arguments passed in the `FCALL` command.
 *
 * - `nargs`: the number of elements present in the `args` array.
 */
typedef void (*KVModuleScriptingEngineCallFunctionFunc)(
    KVModuleCtx *module_ctx,
    KVModuleScriptingEngineCtx *engine_ctx,
    KVModuleScriptingEngineServerRuntimeCtx *server_ctx,
    KVModuleScriptingEngineCompiledFunction *compiled_function,
    KVModuleScriptingEngineSubsystemType type,
    KVModuleString **keys,
    size_t nkeys,
    KVModuleString **args,
    size_t nargs);

/* Return memory overhead for a given function, such memory is not counted as
 * engine memory but as general structs memory that hold different information
 */
typedef size_t (*KVModuleScriptingEngineGetFunctionMemoryOverheadFunc)(
    KVModuleCtx *module_ctx,
    KVModuleScriptingEngineCompiledFunction *compiled_function);

/* The callback function called when `SCRIPT FLUSH` command is called. The
 * engine should reset the runtime environment used for EVAL scripts.
 * This callback has been replaced by `KVModuleScriptingEngineResetEnvFunc`
 * callback in ABI version 3.
 *
 * - `module_ctx`: the module runtime context.
 *
 * - `engine_ctx`: the scripting engine runtime context.
 *
 * - `async`: if has value 1 then the reset is done asynchronously through
 * the callback structure returned by this function.
 */
typedef KVModuleScriptingEngineCallableLazyEnvReset *(*KVModuleScriptingEngineResetEvalFuncV2)(
    KVModuleCtx *module_ctx,
    KVModuleScriptingEngineCtx *engine_ctx,
    int async);

/* The callback function called when `SCRIPT FLUSH` or `FUNCTION FLUSH` command is called.
 * The engine should reset the runtime environment used for EVAL scripts or FUNCTION scripts.
 *
 * - `module_ctx`: the module runtime context.
 *
 * - `engine_ctx`: the scripting engine runtime context.
 *
 * - `type`: the subsystem type.
 *
 * - `async`: if has value 1 then the reset is done asynchronously through
 * the callback structure returned by this function.
 */
typedef KVModuleScriptingEngineCallableLazyEnvReset *(*KVModuleScriptingEngineResetEnvFunc)(
    KVModuleCtx *module_ctx,
    KVModuleScriptingEngineCtx *engine_ctx,
    KVModuleScriptingEngineSubsystemType type,
    int async);

/* Return the current used memory by the engine.
 *
 * - `module_ctx`: the module runtime context.
 *
 * - `engine_ctx`: the scripting engine runtime context.
 *
 * - `type`: the subsystem type.
 */
typedef KVModuleScriptingEngineMemoryInfo (*KVModuleScriptingEngineGetMemoryInfoFunc)(
    KVModuleCtx *module_ctx,
    KVModuleScriptingEngineCtx *engine_ctx,
    KVModuleScriptingEngineSubsystemType type);

typedef enum KVModuleScriptingEngineDebuggerEnableRet {
    VMSE_DEBUG_NOT_SUPPORTED, /* The scripting engine does not support debugging. */
    VMSE_DEBUG_ENABLED,       /* The scripting engine has enabled the debugging mode. */
    VMSE_DEBUG_ENABLE_FAIL,   /* The scripting engine failed to enable the debugging mode. */
} KVModuleScriptingEngineDebuggerEnableRet;

typedef int (*KVModuleScriptingEngineDebuggerCommandHandlerFunc)(
    KVModuleString **argv,
    size_t argc,
    void *context);

/* Current ABI version for scripting engine debugger commands. */
#define KVMODULE_SCRIPTING_ENGINE_ABI_DEBUGGER_COMMAND_VERSION 1UL

/* The structure that represents the parameter of a debugger command. */
typedef struct KVModuleScriptingEngineDebuggerCommandParam {
    const char *name;
    int optional;
    int variadic;

} KVModuleScriptingEngineDebuggerCommandParam;

/* The structure that represents a debugger command. */
typedef struct KVModuleScriptingEngineDebuggerCommand {
    uint64_t version; /* Version of this structure for ABI compat. */

    const char *name;                                              /* The command name. */
    const size_t prefix_len;                                       /* The prefix of the command name that can be used as a short name. */
    const KVModuleScriptingEngineDebuggerCommandParam *params; /* The array of parameters of this command. */
    size_t params_len;                                             /* The length of the array of parameters. */
    const char *desc;                                              /* The description of the command that is shown in the help message. */
    int invisible;                                                 /* Whether this command should be hidden in the help message. */
    KVModuleScriptingEngineDebuggerCommandHandlerFunc handler; /* The function pointer that implements this command. */
    void *context;                                                 /* The pointer to a context structure that is passed when invoking the command handler. */
} KVModuleScriptingEngineDebuggerCommandV1;

#define KVModuleScriptingEngineDebuggerCommand KVModuleScriptingEngineDebuggerCommandV1

#define KVMODULE_SCRIPTING_ENGINE_DEBUGGER_COMMAND(NAME, PREFIX, PARAMS, PARAMS_LEN, DESC, INVISIBLE, HANDLER) \
    {                                                                                                              \
        .version = KVMODULE_SCRIPTING_ENGINE_ABI_DEBUGGER_COMMAND_VERSION,                                     \
        .name = NAME,                                                                                              \
        .prefix_len = PREFIX,                                                                                      \
        .params = PARAMS,                                                                                          \
        .params_len = PARAMS_LEN,                                                                                  \
        .desc = DESC,                                                                                              \
        .invisible = INVISIBLE,                                                                                    \
        .handler = HANDLER,                                                                                        \
        .context = NULL}

#define KVMODULE_SCRIPTING_ENGINE_DEBUGGER_COMMAND_WITH_CTX(NAME, PREFIX, PARAMS, PARAMS_LEN, DESC, INVISIBLE, HANDLER, CTX) \
    {                                                                                                                            \
        .version = KVMODULE_SCRIPTING_ENGINE_ABI_DEBUGGER_COMMAND_VERSION,                                                   \
        .name = NAME,                                                                                                            \
        .prefix_len = PREFIX,                                                                                                    \
        .params = PARAMS,                                                                                                        \
        .params_len = PARAMS_LEN,                                                                                                \
        .desc = DESC,                                                                                                            \
        .invisible = INVISIBLE,                                                                                                  \
        .handler = HANDLER,                                                                                                      \
        .context = CTX}

/* The callback function called when `SCRIPT DEBUG (YES|SYNC)` command is called
 * to enable the remote debugger when executing a compiled function.
 *
 * - `module_ctx`: the module runtime context.
 *
 * - `engine_ctx`: the scripting engine runtime context.
 *
 * - `type`: the subsystem type. Either EVAL or FUNCTION.
 *
 * - `commands`: the array of commands exposed by the remote debugger
 *   implemented by this scripting engine.
 *
 * - `commands_len`: the length of the commands array.
 *
 * Returns an enum value of type `KVModuleScriptingEngineDebuggerEnableRet`.
 * Check the enum comments for more details.
 */
typedef KVModuleScriptingEngineDebuggerEnableRet (*KVModuleScriptingEngineDebuggerEnableFunc)(
    KVModuleCtx *module_ctx,
    KVModuleScriptingEngineCtx *engine_ctx,
    KVModuleScriptingEngineSubsystemType type,
    const KVModuleScriptingEngineDebuggerCommand **commands,
    size_t *commands_length);

/* The callback function called when `SCRIPT DEBUG NO` command is called to
 * disable the remote debugger.
 *
 * - `module_ctx`: the module runtime context.
 *
 * - `engine_ctx`: the scripting engine runtime context.
 *
 * - `type`: the subsystem type. Either EVAL or FUNCTION.
 */
typedef void (*KVModuleScriptingEngineDebuggerDisableFunc)(
    KVModuleCtx *module_ctx,
    KVModuleScriptingEngineCtx *engine_ctx,
    KVModuleScriptingEngineSubsystemType type);

/* The callback function called just before the execution of a compiled function
 * when the debugging mode is enabled.
 *
 * - `module_ctx`: the module runtime context.
 *
 * - `engine_ctx`: the scripting engine runtime context.
 *
 * - `type`: the subsystem type. Either EVAL or FUNCTION.
 *
 * - `source`: the original source code from where the code of the compiled
 *   function was compiled.
 */
typedef void (*KVModuleScriptingEngineDebuggerStartFunc)(
    KVModuleCtx *module_ctx,
    KVModuleScriptingEngineCtx *engine_ctx,
    KVModuleScriptingEngineSubsystemType type,
    KVModuleString *source);

/* The callback function called just after the execution of a compiled function
 * when the debugging mode is enabled.
 *
 * - `module_ctx`: the module runtime context.
 *
 * - `engine_ctx`: the scripting engine runtime context.
 *
 * - `type`: the subsystem type. Either EVAL or FUNCTION.
 */
typedef void (*KVModuleScriptingEngineDebuggerEndFunc)(
    KVModuleCtx *module_ctx,
    KVModuleScriptingEngineCtx *engine_ctx,
    KVModuleScriptingEngineSubsystemType type);

/* Current ABI version for scripting engine modules. */
/* Version Changelog:
 *  1. Initial version.
 *  2. Changed the `compile_code` callback to support binary data in the source code.
 *  3. Renamed reset_eval_env callback to reset_env and added a type parameter to be
 *     able to reset both EVAL or FUNCTION scripts env.
 *  4. Added support for new debugging commands.
 */
#define KVMODULE_SCRIPTING_ENGINE_ABI_VERSION 4UL

#define KVMODULE_SCRIPTING_ENGINE_METHODS_STRUCT_FIELDS_V3                                 \
    struct {                                                                                   \
        /* Compile code function callback. When a new script is loaded, this                   \
         * callback will be called with the script code, compiles it, and returns a            \
         * list of `KVModuleScriptingEngineCompiledFunc` objects. */                       \
        union {                                                                                \
            KVModuleScriptingEngineCompileCodeFuncV1 compile_code_v1;                      \
            KVModuleScriptingEngineCompileCodeFunc compile_code;                           \
        };                                                                                     \
                                                                                               \
        /* Function callback to free the memory of a registered engine function. */            \
        KVModuleScriptingEngineFreeFunctionFunc free_function;                             \
                                                                                               \
                                                                                               \
        /* The callback function called when `FCALL` command is called on a function           \
         * registered in this engine. */                                                       \
        KVModuleScriptingEngineCallFunctionFunc call_function;                             \
                                                                                               \
        /* Function callback to return memory overhead for a given function. */                \
        KVModuleScriptingEngineGetFunctionMemoryOverheadFunc get_function_memory_overhead; \
                                                                                               \
        /* The callback function used to reset the runtime environment used                    \
         * by the scripting engine for EVAL scripts or FUNCTION scripts. */                    \
        union {                                                                                \
            KVModuleScriptingEngineResetEvalFuncV2 reset_eval_env_v2;                      \
            KVModuleScriptingEngineResetEnvFunc reset_env;                                 \
        };                                                                                     \
                                                                                               \
        /* Function callback to get the used memory by the engine. */                          \
        KVModuleScriptingEngineGetMemoryInfoFunc get_memory_info;                          \
    }

typedef struct KVModuleScriptingEngineMethods {
    uint64_t version; /* Version of this structure for ABI compat. */

    KVMODULE_SCRIPTING_ENGINE_METHODS_STRUCT_FIELDS_V3;

} KVModuleScriptingEngineMethodsV3;

typedef struct KVModuleScriptingEngineMethodsV4 {
    uint64_t version; /* Version of this structure for ABI compat. */

    KVMODULE_SCRIPTING_ENGINE_METHODS_STRUCT_FIELDS_V3;

    /* Function callback to enable the debugger for the future execution of scripts. */
    KVModuleScriptingEngineDebuggerEnableFunc debugger_enable;

    /* Function callback to disable the debugger. */
    KVModuleScriptingEngineDebuggerDisableFunc debugger_disable;

    /* Function callback to start the debugger on a particular script. */
    KVModuleScriptingEngineDebuggerStartFunc debugger_start;

    /* Function callback to end the debugger on a particular script. */
    KVModuleScriptingEngineDebuggerEndFunc debugger_end;


} KVModuleScriptingEngineMethodsV4;

#define KVModuleScriptingEngineMethods KVModuleScriptingEngineMethodsV4

/* ------------------------- End of common defines ------------------------ */

/* ----------- The rest of the defines are only for modules ----------------- */
#if !defined KVMODULE_CORE || defined KVMODULE_CORE_MODULE
/* Things defined for modules and core-modules. */

/* Macro definitions specific to individual compilers */
#ifndef KVMODULE_ATTR_UNUSED
#ifdef __GNUC__
#define KVMODULE_ATTR_UNUSED __attribute__((unused))
#else
#define KVMODULE_ATTR_UNUSED
#endif
#endif

#ifndef KVMODULE_ATTR_PRINTF
#ifdef __GNUC__
#define KVMODULE_ATTR_PRINTF(idx, cnt) __attribute__((format(printf, idx, cnt)))
#else
#define KVMODULE_ATTR_PRINTF(idx, cnt)
#endif
#endif

#ifndef KVMODULE_ATTR_COMMON
#if defined(__GNUC__) && !(defined(__clang__) && defined(__cplusplus))
#define KVMODULE_ATTR_COMMON __attribute__((__common__))
#else
#define KVMODULE_ATTR_COMMON
#endif
#endif

/* Incomplete structures for compiler checks but opaque access. */
typedef struct KVModuleCommand KVModuleCommand;
typedef struct KVModuleCallReply KVModuleCallReply;
typedef struct KVModuleType KVModuleType;
typedef struct KVModuleBlockedClient KVModuleBlockedClient;
typedef struct KVModuleClusterInfo KVModuleClusterInfo;
typedef struct KVModuleDict KVModuleDict;
typedef struct KVModuleDictIter KVModuleDictIter;
typedef struct KVModuleCommandFilterCtx KVModuleCommandFilterCtx;
typedef struct KVModuleCommandFilter KVModuleCommandFilter;
typedef struct KVModuleServerInfoData KVModuleServerInfoData;
typedef struct KVModuleScanCursor KVModuleScanCursor;
typedef struct KVModuleUser KVModuleUser;
typedef struct KVModuleKeyOptCtx KVModuleKeyOptCtx;
typedef struct KVModuleRdbStream KVModuleRdbStream;

typedef int (*KVModuleCmdFunc)(KVModuleCtx *ctx, KVModuleString **argv, int argc);
typedef void (*KVModuleDisconnectFunc)(KVModuleCtx *ctx, KVModuleBlockedClient *bc);
typedef int (*KVModuleNotificationFunc)(KVModuleCtx *ctx, int type, const char *event, KVModuleString *key);
typedef void (*KVModulePostNotificationJobFunc)(KVModuleCtx *ctx, void *pd);
typedef void *(*KVModuleTypeLoadFunc)(KVModuleIO *rdb, int encver);
typedef void (*KVModuleTypeSaveFunc)(KVModuleIO *rdb, void *value);
typedef int (*KVModuleTypeAuxLoadFunc)(KVModuleIO *rdb, int encver, int when);
typedef void (*KVModuleTypeAuxSaveFunc)(KVModuleIO *rdb, int when);
typedef void (*KVModuleTypeRewriteFunc)(KVModuleIO *aof, KVModuleString *key, void *value);
typedef size_t (*KVModuleTypeMemUsageFunc)(const void *value);
typedef size_t (*KVModuleTypeMemUsageFunc2)(KVModuleKeyOptCtx *ctx, const void *value, size_t sample_size);
typedef void (*KVModuleTypeDigestFunc)(KVModuleDigest *digest, void *value);
typedef void (*KVModuleTypeFreeFunc)(void *value);
typedef size_t (*KVModuleTypeFreeEffortFunc)(KVModuleString *key, const void *value);
typedef size_t (*KVModuleTypeFreeEffortFunc2)(KVModuleKeyOptCtx *ctx, const void *value);
typedef void (*KVModuleTypeUnlinkFunc)(KVModuleString *key, const void *value);
typedef void (*KVModuleTypeUnlinkFunc2)(KVModuleKeyOptCtx *ctx, const void *value);
typedef void *(*KVModuleTypeCopyFunc)(KVModuleString *fromkey, KVModuleString *tokey, const void *value);
typedef void *(*KVModuleTypeCopyFunc2)(KVModuleKeyOptCtx *ctx, const void *value);
typedef int (*KVModuleTypeDefragFunc)(KVModuleDefragCtx *ctx, KVModuleString *key, void **value);
typedef void (*KVModuleClusterMessageReceiver)(KVModuleCtx *ctx,
                                                   const char *sender_id,
                                                   uint8_t type,
                                                   const unsigned char *payload,
                                                   uint32_t len);
typedef void (*KVModuleTimerProc)(KVModuleCtx *ctx, void *data);
typedef void (*KVModuleCommandFilterFunc)(KVModuleCommandFilterCtx *filter);
typedef void (*KVModuleForkDoneHandler)(int exitcode, int bysignal, void *user_data);
typedef void (*KVModuleScanCB)(KVModuleCtx *ctx,
                                   KVModuleString *keyname,
                                   KVModuleKey *key,
                                   void *privdata);
typedef void (*KVModuleScanKeyCB)(KVModuleKey *key,
                                      KVModuleString *field,
                                      KVModuleString *value,
                                      void *privdata);
typedef KVModuleString *(*KVModuleConfigGetStringFunc)(const char *name, void *privdata);
typedef long long (*KVModuleConfigGetNumericFunc)(const char *name, void *privdata);
typedef int (*KVModuleConfigGetBoolFunc)(const char *name, void *privdata);
typedef int (*KVModuleConfigGetEnumFunc)(const char *name, void *privdata);
typedef int (*KVModuleConfigSetStringFunc)(const char *name,
                                               KVModuleString *val,
                                               void *privdata,
                                               KVModuleString **err);
typedef int (*KVModuleConfigSetNumericFunc)(const char *name,
                                                long long val,
                                                void *privdata,
                                                KVModuleString **err);
typedef int (*KVModuleConfigSetBoolFunc)(const char *name, int val, void *privdata, KVModuleString **err);
typedef int (*KVModuleConfigSetEnumFunc)(const char *name, int val, void *privdata, KVModuleString **err);
typedef int (*KVModuleConfigApplyFunc)(KVModuleCtx *ctx, void *privdata, KVModuleString **err);
typedef void (*KVModuleOnUnblocked)(KVModuleCtx *ctx, KVModuleCallReply *reply, void *private_data);
typedef int (*KVModuleAuthCallback)(KVModuleCtx *ctx,
                                        KVModuleString *username,
                                        KVModuleString *password,
                                        KVModuleString **err);

typedef struct KVModuleTypeMethods {
    uint64_t version;
    KVModuleTypeLoadFunc rdb_load;
    KVModuleTypeSaveFunc rdb_save;
    KVModuleTypeRewriteFunc aof_rewrite;
    KVModuleTypeMemUsageFunc mem_usage;
    KVModuleTypeDigestFunc digest;
    KVModuleTypeFreeFunc free;
    KVModuleTypeAuxLoadFunc aux_load;
    KVModuleTypeAuxSaveFunc aux_save;
    int aux_save_triggers;
    KVModuleTypeFreeEffortFunc free_effort;
    KVModuleTypeUnlinkFunc unlink;
    KVModuleTypeCopyFunc copy;
    KVModuleTypeDefragFunc defrag;
    KVModuleTypeMemUsageFunc2 mem_usage2;
    KVModuleTypeFreeEffortFunc2 free_effort2;
    KVModuleTypeUnlinkFunc2 unlink2;
    KVModuleTypeCopyFunc2 copy2;
    KVModuleTypeAuxSaveFunc aux_save2;
} KVModuleTypeMethods;

#define KVMODULE_GET_API(name) KVModule_GetApi("KVModule_" #name, ((void **)&KVModule_##name))

/* Default API declaration prefix (not 'extern' for backwards compatibility) */
#ifndef KVMODULE_API
#define KVMODULE_API
#endif

/* Default API declaration suffix (compiler attributes) */
#ifndef KVMODULE_ATTR
#define KVMODULE_ATTR KVMODULE_ATTR_COMMON
#endif

KVMODULE_API void *(*KVModule_Alloc)(size_t bytes)KVMODULE_ATTR;
KVMODULE_API void *(*KVModule_TryAlloc)(size_t bytes)KVMODULE_ATTR;
KVMODULE_API void *(*KVModule_Realloc)(void *ptr, size_t bytes)KVMODULE_ATTR;
KVMODULE_API void *(*KVModule_TryRealloc)(void *ptr, size_t bytes)KVMODULE_ATTR;
KVMODULE_API void (*KVModule_Free)(void *ptr) KVMODULE_ATTR;
KVMODULE_API void *(*KVModule_Calloc)(size_t nmemb, size_t size)KVMODULE_ATTR;
KVMODULE_API void *(*KVModule_TryCalloc)(size_t nmemb, size_t size)KVMODULE_ATTR;
KVMODULE_API char *(*KVModule_Strdup)(const char *str)KVMODULE_ATTR;
KVMODULE_API int (*KVModule_GetApi)(const char *, void *) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_CreateCommand)(KVModuleCtx *ctx,
                                                   const char *name,
                                                   KVModuleCmdFunc cmdfunc,
                                                   const char *strflags,
                                                   int firstkey,
                                                   int lastkey,
                                                   int keystep) KVMODULE_ATTR;
KVMODULE_API KVModuleCommand *(*KVModule_GetCommand)(KVModuleCtx *ctx,
                                                                 const char *name)KVMODULE_ATTR;
KVMODULE_API int (*KVModule_CreateSubcommand)(KVModuleCommand *parent,
                                                      const char *name,
                                                      KVModuleCmdFunc cmdfunc,
                                                      const char *strflags,
                                                      int firstkey,
                                                      int lastkey,
                                                      int keystep) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_SetCommandInfo)(KVModuleCommand *command,
                                                    const KVModuleCommandInfo *info) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_SetCommandACLCategories)(KVModuleCommand *command,
                                                             const char *ctgrsflags) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_AddACLCategory)(KVModuleCtx *ctx, const char *name) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_SetModuleAttribs)(KVModuleCtx *ctx, const char *name, int ver, int apiver)
    KVMODULE_ATTR;
KVMODULE_API int (*KVModule_IsModuleNameBusy)(const char *name) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_WrongArity)(KVModuleCtx *ctx) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_UpdateRuntimeArgs)(KVModuleCtx *ctx, KVModuleString **argv, int argc) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplyWithLongLong)(KVModuleCtx *ctx, long long ll) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_GetSelectedDb)(KVModuleCtx *ctx) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_SelectDb)(KVModuleCtx *ctx, int newid) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_KeyExists)(KVModuleCtx *ctx, KVModuleString *keyname) KVMODULE_ATTR;
KVMODULE_API KVModuleKey *(*KVModule_OpenKey)(KVModuleCtx *ctx,
                                                          KVModuleString *keyname,
                                                          int mode)KVMODULE_ATTR;
KVMODULE_API int (*KVModule_GetOpenKeyModesAll)(void) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_CloseKey)(KVModuleKey *kp) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_KeyType)(KVModuleKey *kp) KVMODULE_ATTR;
KVMODULE_API size_t (*KVModule_ValueLength)(KVModuleKey *kp) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ListPush)(KVModuleKey *kp,
                                              int where,
                                              KVModuleString *ele) KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_ListPop)(KVModuleKey *key, int where)KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_ListGet)(KVModuleKey *key, long index)KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ListSet)(KVModuleKey *key,
                                             long index,
                                             KVModuleString *value) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ListInsert)(KVModuleKey *key,
                                                long index,
                                                KVModuleString *value) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ListDelete)(KVModuleKey *key, long index) KVMODULE_ATTR;
KVMODULE_API KVModuleCallReply *(*KVModule_Call)(KVModuleCtx *ctx,
                                                             const char *cmdname,
                                                             const char *fmt,
                                                             ...)KVMODULE_ATTR;
KVMODULE_API const char *(*KVModule_CallReplyProto)(KVModuleCallReply *reply, size_t *len)KVMODULE_ATTR;
KVMODULE_API void (*KVModule_FreeCallReply)(KVModuleCallReply *reply) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_CallReplyType)(KVModuleCallReply *reply) KVMODULE_ATTR;
KVMODULE_API long long (*KVModule_CallReplyInteger)(KVModuleCallReply *reply) KVMODULE_ATTR;
KVMODULE_API double (*KVModule_CallReplyDouble)(KVModuleCallReply *reply) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_CallReplyBool)(KVModuleCallReply *reply) KVMODULE_ATTR;
KVMODULE_API const char *(*KVModule_CallReplyBigNumber)(KVModuleCallReply *reply,
                                                                size_t *len)KVMODULE_ATTR;
KVMODULE_API const char *(*KVModule_CallReplyVerbatim)(KVModuleCallReply *reply,
                                                               size_t *len,
                                                               const char **format)KVMODULE_ATTR;
KVMODULE_API KVModuleCallReply *(*KVModule_CallReplySetElement)(KVModuleCallReply *reply,
                                                                            size_t idx)KVMODULE_ATTR;
KVMODULE_API int (*KVModule_CallReplyMapElement)(KVModuleCallReply *reply,
                                                         size_t idx,
                                                         KVModuleCallReply **key,
                                                         KVModuleCallReply **val) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_CallReplyAttributeElement)(KVModuleCallReply *reply,
                                                               size_t idx,
                                                               KVModuleCallReply **key,
                                                               KVModuleCallReply **val) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_CallReplyPromiseSetUnblockHandler)(KVModuleCallReply *reply,
                                                                        KVModuleOnUnblocked on_unblock,
                                                                        void *private_data) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_CallReplyPromiseAbort)(KVModuleCallReply *reply,
                                                           void **private_data) KVMODULE_ATTR;
KVMODULE_API KVModuleCallReply *(*KVModule_CallReplyAttribute)(KVModuleCallReply *reply)
    KVMODULE_ATTR;
KVMODULE_API size_t (*KVModule_CallReplyLength)(KVModuleCallReply *reply) KVMODULE_ATTR;
KVMODULE_API KVModuleCallReply *(*KVModule_CallReplyArrayElement)(KVModuleCallReply *reply,
                                                                              size_t idx)KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_CreateString)(KVModuleCtx *ctx,
                                                                  const char *ptr,
                                                                  size_t len)KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_CreateStringFromLongLong)(KVModuleCtx *ctx,
                                                                              long long ll)KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_CreateStringFromULongLong)(KVModuleCtx *ctx,
                                                                               unsigned long long ull)KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_CreateStringFromDouble)(KVModuleCtx *ctx,
                                                                            double d)KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_CreateStringFromLongDouble)(KVModuleCtx *ctx,
                                                                                long double ld,
                                                                                int humanfriendly)KVMODULE_ATTR;
KVMODULE_API KVModuleString *(
    *KVModule_CreateStringFromString)(KVModuleCtx *ctx, const KVModuleString *str)KVMODULE_ATTR;
KVMODULE_API KVModuleString *(
    *KVModule_CreateStringFromStreamID)(KVModuleCtx *ctx, const KVModuleStreamID *id)KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_CreateStringPrintf)(KVModuleCtx *ctx, const char *fmt, ...)
    KVMODULE_ATTR_PRINTF(2, 3) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_FreeString)(KVModuleCtx *ctx, KVModuleString *str) KVMODULE_ATTR;
KVMODULE_API const char *(*KVModule_StringPtrLen)(const KVModuleString *str, size_t *len)KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplyWithError)(KVModuleCtx *ctx, const char *err) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplyWithErrorFormat)(KVModuleCtx *ctx, const char *fmt, ...) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplyWithCustomErrorFormat)(KVModuleCtx *ctx, int update_error_stats, const char *fmt, ...) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplyWithSimpleString)(KVModuleCtx *ctx, const char *msg) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplyWithArray)(KVModuleCtx *ctx, long len) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplyWithMap)(KVModuleCtx *ctx, long len) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplyWithSet)(KVModuleCtx *ctx, long len) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplyWithAttribute)(KVModuleCtx *ctx, long len) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplyWithNullArray)(KVModuleCtx *ctx) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplyWithEmptyArray)(KVModuleCtx *ctx) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_ReplySetArrayLength)(KVModuleCtx *ctx, long len) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_ReplySetMapLength)(KVModuleCtx *ctx, long len) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_ReplySetSetLength)(KVModuleCtx *ctx, long len) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_ReplySetAttributeLength)(KVModuleCtx *ctx, long len) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_ReplySetPushLength)(KVModuleCtx *ctx, long len) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplyWithStringBuffer)(KVModuleCtx *ctx,
                                                           const char *buf,
                                                           size_t len) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplyWithCString)(KVModuleCtx *ctx, const char *buf) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplyWithString)(KVModuleCtx *ctx, KVModuleString *str) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplyWithEmptyString)(KVModuleCtx *ctx) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplyWithVerbatimString)(KVModuleCtx *ctx,
                                                             const char *buf,
                                                             size_t len) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplyWithVerbatimStringType)(KVModuleCtx *ctx,
                                                                 const char *buf,
                                                                 size_t len,
                                                                 const char *ext) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplyWithNull)(KVModuleCtx *ctx) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplyWithBool)(KVModuleCtx *ctx, int b) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplyWithLongDouble)(KVModuleCtx *ctx, long double d) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplyWithDouble)(KVModuleCtx *ctx, double d) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplyWithBigNumber)(KVModuleCtx *ctx,
                                                        const char *bignum,
                                                        size_t len) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplyWithCallReply)(KVModuleCtx *ctx,
                                                        KVModuleCallReply *reply) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_StringToLongLong)(const KVModuleString *str, long long *ll) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_StringToULongLong)(const KVModuleString *str,
                                                       unsigned long long *ull) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_StringToDouble)(const KVModuleString *str, double *d) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_StringToLongDouble)(const KVModuleString *str,
                                                        long double *d) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_StringToStreamID)(const KVModuleString *str,
                                                      KVModuleStreamID *id) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_AutoMemory)(KVModuleCtx *ctx) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_Replicate)(KVModuleCtx *ctx, const char *cmdname, const char *fmt, ...)
    KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ReplicateVerbatim)(KVModuleCtx *ctx) KVMODULE_ATTR;
KVMODULE_API const char *(*KVModule_CallReplyStringPtr)(KVModuleCallReply *reply,
                                                                size_t *len)KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_CreateStringFromCallReply)(KVModuleCallReply *reply)
    KVMODULE_ATTR;
KVMODULE_API int (*KVModule_DeleteKey)(KVModuleKey *key) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_UnlinkKey)(KVModuleKey *key) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_StringSet)(KVModuleKey *key, KVModuleString *str) KVMODULE_ATTR;
KVMODULE_API char *(*KVModule_StringDMA)(KVModuleKey *key, size_t *len, int mode)KVMODULE_ATTR;
KVMODULE_API int (*KVModule_StringTruncate)(KVModuleKey *key, size_t newlen) KVMODULE_ATTR;
KVMODULE_API mstime_t (*KVModule_GetExpire)(KVModuleKey *key) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_SetExpire)(KVModuleKey *key, mstime_t expire) KVMODULE_ATTR;
KVMODULE_API mstime_t (*KVModule_GetAbsExpire)(KVModuleKey *key) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_SetAbsExpire)(KVModuleKey *key, mstime_t expire) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_ResetDataset)(int restart_aof, int async) KVMODULE_ATTR;
KVMODULE_API unsigned long long (*KVModule_DbSize)(KVModuleCtx *ctx) KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_RandomKey)(KVModuleCtx *ctx)KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ZsetAdd)(KVModuleKey *key, double score, KVModuleString *ele, int *flagsptr)
    KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ZsetIncrby)(KVModuleKey *key,
                                                double score,
                                                KVModuleString *ele,
                                                int *flagsptr,
                                                double *newscore) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ZsetScore)(KVModuleKey *key,
                                               KVModuleString *ele,
                                               double *score) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ZsetRem)(KVModuleKey *key,
                                             KVModuleString *ele,
                                             int *deleted) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_ZsetRangeStop)(KVModuleKey *key) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ZsetFirstInScoreRange)(KVModuleKey *key,
                                                           double min,
                                                           double max,
                                                           int minex,
                                                           int maxex) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ZsetLastInScoreRange)(KVModuleKey *key,
                                                          double min,
                                                          double max,
                                                          int minex,
                                                          int maxex) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ZsetFirstInLexRange)(KVModuleKey *key,
                                                         KVModuleString *min,
                                                         KVModuleString *max) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ZsetLastInLexRange)(KVModuleKey *key,
                                                        KVModuleString *min,
                                                        KVModuleString *max) KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_ZsetRangeCurrentElement)(KVModuleKey *key,
                                                                             double *score)KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ZsetRangeNext)(KVModuleKey *key) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ZsetRangePrev)(KVModuleKey *key) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ZsetRangeEndReached)(KVModuleKey *key) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_HashSet)(KVModuleKey *key, int flags, ...) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_HashGet)(KVModuleKey *key, int flags, ...) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_HashSetStringRef)(KVModuleKey *key, KVModuleString *field, const char *buf, size_t len) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_HashHasStringRef)(KVModuleKey *key, KVModuleString *field) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_StreamAdd)(KVModuleKey *key,
                                               int flags,
                                               KVModuleStreamID *id,
                                               KVModuleString **argv,
                                               int64_t numfields) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_StreamDelete)(KVModuleKey *key, KVModuleStreamID *id) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_StreamIteratorStart)(KVModuleKey *key,
                                                         int flags,
                                                         KVModuleStreamID *startid,
                                                         KVModuleStreamID *endid) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_StreamIteratorStop)(KVModuleKey *key) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_StreamIteratorNextID)(KVModuleKey *key,
                                                          KVModuleStreamID *id,
                                                          long *numfields) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_StreamIteratorNextField)(KVModuleKey *key,
                                                             KVModuleString **field_ptr,
                                                             KVModuleString **value_ptr) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_StreamIteratorDelete)(KVModuleKey *key) KVMODULE_ATTR;
KVMODULE_API long long (*KVModule_StreamTrimByLength)(KVModuleKey *key,
                                                              int flags,
                                                              long long length) KVMODULE_ATTR;
KVMODULE_API long long (*KVModule_StreamTrimByID)(KVModuleKey *key,
                                                          int flags,
                                                          KVModuleStreamID *id) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_IsKeysPositionRequest)(KVModuleCtx *ctx) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_KeyAtPos)(KVModuleCtx *ctx, int pos) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_KeyAtPosWithFlags)(KVModuleCtx *ctx, int pos, int flags) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_IsChannelsPositionRequest)(KVModuleCtx *ctx) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_ChannelAtPosWithFlags)(KVModuleCtx *ctx, int pos, int flags) KVMODULE_ATTR;
KVMODULE_API unsigned long long (*KVModule_GetClientId)(KVModuleCtx *ctx) KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_GetClientUserNameById)(KVModuleCtx *ctx,
                                                                           uint64_t id)KVMODULE_ATTR;
KVMODULE_API int (*KVModule_MustObeyClient)(KVModuleCtx *ctx) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_GetClientInfoById)(void *ci, uint64_t id) KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_GetClientNameById)(KVModuleCtx *ctx,
                                                                       uint64_t id)KVMODULE_ATTR;
KVMODULE_API int (*KVModule_SetClientNameById)(uint64_t id, KVModuleString *name) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_PublishMessage)(KVModuleCtx *ctx,
                                                    KVModuleString *channel,
                                                    KVModuleString *message) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_PublishMessageShard)(KVModuleCtx *ctx,
                                                         KVModuleString *channel,
                                                         KVModuleString *message) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_GetContextFlags)(KVModuleCtx *ctx) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_AvoidReplicaTraffic)(void) KVMODULE_ATTR;
KVMODULE_API void *(*KVModule_PoolAlloc)(KVModuleCtx *ctx, size_t bytes)KVMODULE_ATTR;
KVMODULE_API KVModuleType *(*KVModule_CreateDataType)(KVModuleCtx *ctx,
                                                                  const char *name,
                                                                  int encver,
                                                                  KVModuleTypeMethods *typemethods)KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ModuleTypeSetValue)(KVModuleKey *key,
                                                        KVModuleType *mt,
                                                        void *value) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ModuleTypeReplaceValue)(KVModuleKey *key,
                                                            KVModuleType *mt,
                                                            void *new_value,
                                                            void **old_value) KVMODULE_ATTR;
KVMODULE_API KVModuleType *(*KVModule_ModuleTypeGetType)(KVModuleKey *key)KVMODULE_ATTR;
KVMODULE_API void *(*KVModule_ModuleTypeGetValue)(KVModuleKey *key)KVMODULE_ATTR;
KVMODULE_API int (*KVModule_IsIOError)(KVModuleIO *io) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_SetModuleOptions)(KVModuleCtx *ctx, int options) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_SignalModifiedKey)(KVModuleCtx *ctx,
                                                       KVModuleString *keyname) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_SaveUnsigned)(KVModuleIO *io, uint64_t value) KVMODULE_ATTR;
KVMODULE_API uint64_t (*KVModule_LoadUnsigned)(KVModuleIO *io) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_SaveSigned)(KVModuleIO *io, int64_t value) KVMODULE_ATTR;
KVMODULE_API int64_t (*KVModule_LoadSigned)(KVModuleIO *io) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_EmitAOF)(KVModuleIO *io, const char *cmdname, const char *fmt, ...)
    KVMODULE_ATTR;
KVMODULE_API void (*KVModule_SaveString)(KVModuleIO *io, KVModuleString *s) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_SaveStringBuffer)(KVModuleIO *io,
                                                       const char *str,
                                                       size_t len) KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_LoadString)(KVModuleIO *io)KVMODULE_ATTR;
KVMODULE_API char *(*KVModule_LoadStringBuffer)(KVModuleIO *io, size_t *lenptr)KVMODULE_ATTR;
KVMODULE_API void (*KVModule_SaveDouble)(KVModuleIO *io, double value) KVMODULE_ATTR;
KVMODULE_API double (*KVModule_LoadDouble)(KVModuleIO *io) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_SaveFloat)(KVModuleIO *io, float value) KVMODULE_ATTR;
KVMODULE_API float (*KVModule_LoadFloat)(KVModuleIO *io) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_SaveLongDouble)(KVModuleIO *io, long double value) KVMODULE_ATTR;
KVMODULE_API long double (*KVModule_LoadLongDouble)(KVModuleIO *io) KVMODULE_ATTR;
KVMODULE_API void *(*KVModule_LoadDataTypeFromString)(const KVModuleString *str,
                                                              const KVModuleType *mt)KVMODULE_ATTR;
KVMODULE_API void *(*KVModule_LoadDataTypeFromStringEncver)(const KVModuleString *str,
                                                                    const KVModuleType *mt,
                                                                    int encver)KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_SaveDataTypeToString)(KVModuleCtx *ctx,
                                                                          void *data,
                                                                          const KVModuleType *mt)KVMODULE_ATTR;
KVMODULE_API void (*KVModule_Log)(KVModuleCtx *ctx, const char *level, const char *fmt, ...)
    KVMODULE_ATTR KVMODULE_ATTR_PRINTF(3, 4);
KVMODULE_API void (*KVModule_LogIOError)(KVModuleIO *io, const char *levelstr, const char *fmt, ...)
    KVMODULE_ATTR KVMODULE_ATTR_PRINTF(3, 4);
KVMODULE_API void (*KVModule__Assert)(const char *estr, const char *file, int line) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_LatencyAddSample)(const char *event, mstime_t latency) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_StringAppendBuffer)(KVModuleCtx *ctx,
                                                        KVModuleString *str,
                                                        const char *buf,
                                                        size_t len) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_TrimStringAllocation)(KVModuleString *str) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_RetainString)(KVModuleCtx *ctx, KVModuleString *str) KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_HoldString)(KVModuleCtx *ctx,
                                                                KVModuleString *str)KVMODULE_ATTR;
KVMODULE_API int (*KVModule_StringCompare)(const KVModuleString *a,
                                                   const KVModuleString *b) KVMODULE_ATTR;
KVMODULE_API KVModuleCtx *(*KVModule_GetContextFromIO)(KVModuleIO *io)KVMODULE_ATTR;
KVMODULE_API const KVModuleString *(*KVModule_GetKeyNameFromIO)(KVModuleIO *io)KVMODULE_ATTR;
KVMODULE_API const KVModuleString *(*KVModule_GetKeyNameFromModuleKey)(KVModuleKey *key)
    KVMODULE_ATTR;
KVMODULE_API int (*KVModule_GetDbIdFromModuleKey)(KVModuleKey *key) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_GetDbIdFromIO)(KVModuleIO *io) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_GetDbIdFromOptCtx)(KVModuleKeyOptCtx *ctx) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_GetToDbIdFromOptCtx)(KVModuleKeyOptCtx *ctx) KVMODULE_ATTR;
KVMODULE_API const KVModuleString *(*KVModule_GetKeyNameFromOptCtx)(KVModuleKeyOptCtx *ctx)
    KVMODULE_ATTR;
KVMODULE_API const KVModuleString *(*KVModule_GetToKeyNameFromOptCtx)(KVModuleKeyOptCtx *ctx)
    KVMODULE_ATTR;
KVMODULE_API mstime_t (*KVModule_Milliseconds)(void) KVMODULE_ATTR;
KVMODULE_API uint64_t (*KVModule_MonotonicMicroseconds)(void) KVMODULE_ATTR;
KVMODULE_API ustime_t (*KVModule_Microseconds)(void) KVMODULE_ATTR;
KVMODULE_API ustime_t (*KVModule_CachedMicroseconds)(void) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_DigestAddStringBuffer)(KVModuleDigest *md,
                                                            const char *ele,
                                                            size_t len) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_DigestAddLongLong)(KVModuleDigest *md, long long ele) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_DigestEndSequence)(KVModuleDigest *md) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_GetDbIdFromDigest)(KVModuleDigest *dig) KVMODULE_ATTR;
KVMODULE_API const KVModuleString *(*KVModule_GetKeyNameFromDigest)(KVModuleDigest *dig)
    KVMODULE_ATTR;
KVMODULE_API KVModuleDict *(*KVModule_CreateDict)(KVModuleCtx *ctx)KVMODULE_ATTR;
KVMODULE_API void (*KVModule_FreeDict)(KVModuleCtx *ctx, KVModuleDict *d) KVMODULE_ATTR;
KVMODULE_API uint64_t (*KVModule_DictSize)(KVModuleDict *d) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_DictSetC)(KVModuleDict *d, void *key, size_t keylen, void *ptr)
    KVMODULE_ATTR;
KVMODULE_API int (*KVModule_DictReplaceC)(KVModuleDict *d, void *key, size_t keylen, void *ptr)
    KVMODULE_ATTR;
KVMODULE_API int (*KVModule_DictSet)(KVModuleDict *d, KVModuleString *key, void *ptr) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_DictReplace)(KVModuleDict *d,
                                                 KVModuleString *key,
                                                 void *ptr) KVMODULE_ATTR;
KVMODULE_API void *(*KVModule_DictGetC)(KVModuleDict *d, void *key, size_t keylen, int *nokey)
    KVMODULE_ATTR;
KVMODULE_API void *(*KVModule_DictGet)(KVModuleDict *d,
                                               KVModuleString *key,
                                               int *nokey)KVMODULE_ATTR;
KVMODULE_API int (*KVModule_DictDelC)(KVModuleDict *d, void *key, size_t keylen, void *oldval)
    KVMODULE_ATTR;
KVMODULE_API int (*KVModule_DictDel)(KVModuleDict *d,
                                             KVModuleString *key,
                                             void *oldval) KVMODULE_ATTR;
KVMODULE_API KVModuleDictIter *(*KVModule_DictIteratorStartC)(KVModuleDict *d,
                                                                          const char *op,
                                                                          void *key,
                                                                          size_t keylen)KVMODULE_ATTR;
KVMODULE_API KVModuleDictIter *(*KVModule_DictIteratorStart)(KVModuleDict *d,
                                                                         const char *op,
                                                                         KVModuleString *key)KVMODULE_ATTR;
KVMODULE_API void (*KVModule_DictIteratorStop)(KVModuleDictIter *di) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_DictIteratorReseekC)(KVModuleDictIter *di,
                                                         const char *op,
                                                         void *key,
                                                         size_t keylen) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_DictIteratorReseek)(KVModuleDictIter *di,
                                                        const char *op,
                                                        KVModuleString *key) KVMODULE_ATTR;
KVMODULE_API void *(*KVModule_DictNextC)(KVModuleDictIter *di,
                                                 size_t *keylen,
                                                 void **dataptr)KVMODULE_ATTR;
KVMODULE_API void *(*KVModule_DictPrevC)(KVModuleDictIter *di,
                                                 size_t *keylen,
                                                 void **dataptr)KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_DictNext)(KVModuleCtx *ctx,
                                                              KVModuleDictIter *di,
                                                              void **dataptr)KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_DictPrev)(KVModuleCtx *ctx,
                                                              KVModuleDictIter *di,
                                                              void **dataptr)KVMODULE_ATTR;
KVMODULE_API int (*KVModule_DictCompareC)(KVModuleDictIter *di, const char *op, void *key, size_t keylen)
    KVMODULE_ATTR;
KVMODULE_API int (*KVModule_DictCompare)(KVModuleDictIter *di,
                                                 const char *op,
                                                 KVModuleString *key) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_RegisterInfoFunc)(KVModuleCtx *ctx, KVModuleInfoFunc cb) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_RegisterAuthCallback)(KVModuleCtx *ctx,
                                                           KVModuleAuthCallback cb) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_InfoAddSection)(KVModuleInfoCtx *ctx, const char *name) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_InfoBeginDictField)(KVModuleInfoCtx *ctx, const char *name) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_InfoEndDictField)(KVModuleInfoCtx *ctx) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_InfoAddFieldString)(KVModuleInfoCtx *ctx,
                                                        const char *field,
                                                        KVModuleString *value) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_InfoAddFieldCString)(KVModuleInfoCtx *ctx,
                                                         const char *field,
                                                         const char *value) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_InfoAddFieldDouble)(KVModuleInfoCtx *ctx,
                                                        const char *field,
                                                        double value) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_InfoAddFieldLongLong)(KVModuleInfoCtx *ctx,
                                                          const char *field,
                                                          long long value) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_InfoAddFieldULongLong)(KVModuleInfoCtx *ctx,
                                                           const char *field,
                                                           unsigned long long value) KVMODULE_ATTR;
KVMODULE_API KVModuleServerInfoData *(*KVModule_GetServerInfo)(KVModuleCtx *ctx,
                                                                           const char *section)KVMODULE_ATTR;
KVMODULE_API void (*KVModule_FreeServerInfo)(KVModuleCtx *ctx,
                                                     KVModuleServerInfoData *data) KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_ServerInfoGetField)(KVModuleCtx *ctx,
                                                                        KVModuleServerInfoData *data,
                                                                        const char *field)KVMODULE_ATTR;
KVMODULE_API const char *(*KVModule_ServerInfoGetFieldC)(KVModuleServerInfoData *data,
                                                                 const char *field)KVMODULE_ATTR;
KVMODULE_API long long (*KVModule_ServerInfoGetFieldSigned)(KVModuleServerInfoData *data,
                                                                    const char *field,
                                                                    int *out_err) KVMODULE_ATTR;
KVMODULE_API unsigned long long (*KVModule_ServerInfoGetFieldUnsigned)(KVModuleServerInfoData *data,
                                                                               const char *field,
                                                                               int *out_err) KVMODULE_ATTR;
KVMODULE_API double (*KVModule_ServerInfoGetFieldDouble)(KVModuleServerInfoData *data,
                                                                 const char *field,
                                                                 int *out_err) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_SubscribeToServerEvent)(KVModuleCtx *ctx,
                                                            KVModuleEvent event,
                                                            KVModuleEventCallback callback) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_SetLRU)(KVModuleKey *key, mstime_t lru_idle) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_GetLRU)(KVModuleKey *key, mstime_t *lru_idle) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_SetLFU)(KVModuleKey *key, long long lfu_freq) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_GetLFU)(KVModuleKey *key, long long *lfu_freq) KVMODULE_ATTR;
KVMODULE_API KVModuleBlockedClient *(*KVModule_BlockClientOnKeys)(KVModuleCtx *ctx,
                                                                              KVModuleCmdFunc reply_callback,
                                                                              KVModuleCmdFunc timeout_callback,
                                                                              void (*free_privdata)(KVModuleCtx *,
                                                                                                    void *),
                                                                              long long timeout_ms,
                                                                              KVModuleString **keys,
                                                                              int numkeys,
                                                                              void *privdata)KVMODULE_ATTR;
KVMODULE_API KVModuleBlockedClient *(*KVModule_BlockClientOnKeysWithFlags)(
    KVModuleCtx *ctx,
    KVModuleCmdFunc reply_callback,
    KVModuleCmdFunc timeout_callback,
    void (*free_privdata)(KVModuleCtx *, void *),
    long long timeout_ms,
    KVModuleString **keys,
    int numkeys,
    void *privdata,
    int flags)KVMODULE_ATTR;
KVMODULE_API void (*KVModule_SignalKeyAsReady)(KVModuleCtx *ctx, KVModuleString *key) KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_GetBlockedClientReadyKey)(KVModuleCtx *ctx)KVMODULE_ATTR;
KVMODULE_API KVModuleScanCursor *(*KVModule_ScanCursorCreate)(void)KVMODULE_ATTR;
KVMODULE_API void (*KVModule_ScanCursorRestart)(KVModuleScanCursor *cursor) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_ScanCursorDestroy)(KVModuleScanCursor *cursor) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_Scan)(KVModuleCtx *ctx,
                                          KVModuleScanCursor *cursor,
                                          KVModuleScanCB fn,
                                          void *privdata) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ScanKey)(KVModuleKey *key,
                                             KVModuleScanCursor *cursor,
                                             KVModuleScanKeyCB fn,
                                             void *privdata) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_GetContextFlagsAll)(void) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_GetModuleOptionsAll)(void) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_GetKeyspaceNotificationFlagsAll)(void) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_IsSubEventSupported)(KVModuleEvent event, uint64_t subevent) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_GetServerVersion)(void) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_GetTypeMethodVersion)(void) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_Yield)(KVModuleCtx *ctx, int flags, const char *busy_reply) KVMODULE_ATTR;
KVMODULE_API KVModuleBlockedClient *(*KVModule_BlockClient)(KVModuleCtx *ctx,
                                                                        KVModuleCmdFunc reply_callback,
                                                                        KVModuleCmdFunc timeout_callback,
                                                                        void (*free_privdata)(KVModuleCtx *,
                                                                                              void *),
                                                                        long long timeout_ms)KVMODULE_ATTR;
KVMODULE_API void *(*KVModule_BlockClientGetPrivateData)(KVModuleBlockedClient *blocked_client)
    KVMODULE_ATTR;
KVMODULE_API void (*KVModule_BlockClientSetPrivateData)(KVModuleBlockedClient *blocked_client,
                                                                void *private_data) KVMODULE_ATTR;
KVMODULE_API KVModuleBlockedClient *(*KVModule_BlockClientOnAuth)(
    KVModuleCtx *ctx,
    KVModuleAuthCallback reply_callback,
    void (*free_privdata)(KVModuleCtx *, void *))KVMODULE_ATTR;
KVMODULE_API int (*KVModule_UnblockClient)(KVModuleBlockedClient *bc, void *privdata) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_IsBlockedReplyRequest)(KVModuleCtx *ctx) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_IsBlockedTimeoutRequest)(KVModuleCtx *ctx) KVMODULE_ATTR;
KVMODULE_API void *(*KVModule_GetBlockedClientPrivateData)(KVModuleCtx *ctx)KVMODULE_ATTR;
KVMODULE_API KVModuleBlockedClient *(*KVModule_GetBlockedClientHandle)(KVModuleCtx *ctx)
    KVMODULE_ATTR;
KVMODULE_API int (*KVModule_AbortBlock)(KVModuleBlockedClient *bc) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_BlockedClientMeasureTimeStart)(KVModuleBlockedClient *bc) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_BlockedClientMeasureTimeEnd)(KVModuleBlockedClient *bc) KVMODULE_ATTR;
KVMODULE_API KVModuleCtx *(*KVModule_GetThreadSafeContext)(KVModuleBlockedClient *bc)KVMODULE_ATTR;
KVMODULE_API KVModuleCtx *(*KVModule_GetDetachedThreadSafeContext)(KVModuleCtx *ctx)KVMODULE_ATTR;
KVMODULE_API void (*KVModule_FreeThreadSafeContext)(KVModuleCtx *ctx) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_ThreadSafeContextLock)(KVModuleCtx *ctx) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ThreadSafeContextTryLock)(KVModuleCtx *ctx) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_ThreadSafeContextUnlock)(KVModuleCtx *ctx) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_SubscribeToKeyspaceEvents)(KVModuleCtx *ctx,
                                                               int types,
                                                               KVModuleNotificationFunc cb) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_AddPostNotificationJob)(KVModuleCtx *ctx,
                                                            KVModulePostNotificationJobFunc callback,
                                                            void *pd,
                                                            void (*free_pd)(void *)) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_NotifyKeyspaceEvent)(KVModuleCtx *ctx,
                                                         int type,
                                                         const char *event,
                                                         KVModuleString *key) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_GetNotifyKeyspaceEvents)(void) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_BlockedClientDisconnected)(KVModuleCtx *ctx) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_RegisterClusterMessageReceiver)(KVModuleCtx *ctx,
                                                                     uint8_t type,
                                                                     KVModuleClusterMessageReceiver callback)
    KVMODULE_ATTR;
KVMODULE_API int (*KVModule_SendClusterMessage)(KVModuleCtx *ctx,
                                                        const char *target_id,
                                                        uint8_t type,
                                                        const char *msg,
                                                        uint32_t len) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_GetClusterNodeInfo)(KVModuleCtx *ctx,
                                                        const char *id,
                                                        char *ip,
                                                        char *primary_id,
                                                        int *port,
                                                        int *flags) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_GetClusterNodeInfoForClient)(KVModuleCtx *ctx,
                                                                 uint64_t client_id,
                                                                 const char *node_id,
                                                                 char *ip,
                                                                 char *primary_id,
                                                                 int *port,
                                                                 int *flags) KVMODULE_ATTR;
KVMODULE_API char **(*KVModule_GetClusterNodesList)(KVModuleCtx *ctx, size_t *numnodes)KVMODULE_ATTR;
KVMODULE_API void (*KVModule_FreeClusterNodesList)(char **ids) KVMODULE_ATTR;
KVMODULE_API KVModuleTimerID (*KVModule_CreateTimer)(KVModuleCtx *ctx,
                                                                 mstime_t period,
                                                                 KVModuleTimerProc callback,
                                                                 void *data) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_StopTimer)(KVModuleCtx *ctx,
                                               KVModuleTimerID id,
                                               void **data) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_GetTimerInfo)(KVModuleCtx *ctx,
                                                  KVModuleTimerID id,
                                                  uint64_t *remaining,
                                                  void **data) KVMODULE_ATTR;
KVMODULE_API const char *(*KVModule_GetMyClusterID)(void)KVMODULE_ATTR;
KVMODULE_API size_t (*KVModule_GetClusterSize)(void) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_GetRandomBytes)(unsigned char *dst, size_t len) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_GetRandomHexChars)(char *dst, size_t len) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_SetDisconnectCallback)(KVModuleBlockedClient *bc,
                                                            KVModuleDisconnectFunc callback) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_SetClusterFlags)(KVModuleCtx *ctx, uint64_t flags) KVMODULE_ATTR;
KVMODULE_API unsigned int (*KVModule_ClusterKeySlotC)(const char *key, size_t keylen) KVMODULE_ATTR;
KVMODULE_API unsigned int (*KVModule_ClusterKeySlot)(KVModuleString *key) KVMODULE_ATTR;
KVMODULE_API const char *(*KVModule_ClusterCanonicalKeyNameInSlot)(unsigned int slot)KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ExportSharedAPI)(KVModuleCtx *ctx,
                                                     const char *apiname,
                                                     void *func) KVMODULE_ATTR;
KVMODULE_API void *(*KVModule_GetSharedAPI)(KVModuleCtx *ctx, const char *apiname)KVMODULE_ATTR;
KVMODULE_API KVModuleCommandFilter *(*KVModule_RegisterCommandFilter)(KVModuleCtx *ctx,
                                                                                  KVModuleCommandFilterFunc cb,
                                                                                  int flags)KVMODULE_ATTR;
KVMODULE_API int (*KVModule_UnregisterCommandFilter)(KVModuleCtx *ctx,
                                                             KVModuleCommandFilter *filter) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_CommandFilterArgsCount)(KVModuleCommandFilterCtx *fctx) KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_CommandFilterArgGet)(KVModuleCommandFilterCtx *fctx,
                                                                         int pos)KVMODULE_ATTR;
KVMODULE_API int (*KVModule_CommandFilterArgInsert)(KVModuleCommandFilterCtx *fctx,
                                                            int pos,
                                                            KVModuleString *arg) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_CommandFilterArgReplace)(KVModuleCommandFilterCtx *fctx,
                                                             int pos,
                                                             KVModuleString *arg) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_CommandFilterArgDelete)(KVModuleCommandFilterCtx *fctx,
                                                            int pos) KVMODULE_ATTR;
KVMODULE_API unsigned long long (*KVModule_CommandFilterGetClientId)(KVModuleCommandFilterCtx *fctx)
    KVMODULE_ATTR;
KVMODULE_API int (*KVModule_Fork)(KVModuleForkDoneHandler cb, void *user_data) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_SendChildHeartbeat)(double progress) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ExitFromChild)(int retcode) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_KillForkChild)(int child_pid) KVMODULE_ATTR;
KVMODULE_API float (*KVModule_GetUsedMemoryRatio)(void) KVMODULE_ATTR;
KVMODULE_API size_t (*KVModule_MallocSize)(void *ptr) KVMODULE_ATTR;
KVMODULE_API size_t (*KVModule_MallocUsableSize)(void *ptr) KVMODULE_ATTR;
KVMODULE_API size_t (*KVModule_MallocSizeString)(KVModuleString *str) KVMODULE_ATTR;
KVMODULE_API size_t (*KVModule_MallocSizeDict)(KVModuleDict *dict) KVMODULE_ATTR;
KVMODULE_API KVModuleUser *(*KVModule_CreateModuleUser)(const char *name)KVMODULE_ATTR;
KVMODULE_API void (*KVModule_FreeModuleUser)(KVModuleUser *user) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_SetContextUser)(KVModuleCtx *ctx,
                                                     const KVModuleUser *user) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_SetModuleUserACL)(KVModuleUser *user, const char *acl) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_SetModuleUserACLString)(KVModuleCtx *ctx,
                                                            KVModuleUser *user,
                                                            const char *acl,
                                                            KVModuleString **error) KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_GetModuleUserACLString)(KVModuleUser *user)KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_GetCurrentUserName)(KVModuleCtx *ctx)KVMODULE_ATTR;
KVMODULE_API KVModuleUser *(*KVModule_GetModuleUserFromUserName)(KVModuleString *name)KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ACLCheckCommandPermissions)(KVModuleUser *user,
                                                                KVModuleString **argv,
                                                                int argc) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ACLCheckKeyPermissions)(KVModuleUser *user,
                                                            KVModuleString *key,
                                                            int flags) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ACLCheckChannelPermissions)(KVModuleUser *user,
                                                                KVModuleString *ch,
                                                                int literal) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_ACLCheckPermissions)(KVModuleUser *user,
                                                         KVModuleString **argv,
                                                         int argc,
                                                         int dbid,
                                                         KVModuleACLLogEntryReason *denial_reason) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_ACLAddLogEntry)(KVModuleCtx *ctx,
                                                     KVModuleUser *user,
                                                     KVModuleString *object,
                                                     KVModuleACLLogEntryReason reason) KVMODULE_ATTR;
KVMODULE_API void (*KVModule_ACLAddLogEntryByUserName)(KVModuleCtx *ctx,
                                                               KVModuleString *user,
                                                               KVModuleString *object,
                                                               KVModuleACLLogEntryReason reason) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_AuthenticateClientWithACLUser)(KVModuleCtx *ctx,
                                                                   const char *name,
                                                                   size_t len,
                                                                   KVModuleUserChangedFunc callback,
                                                                   void *privdata,
                                                                   uint64_t *client_id) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_AuthenticateClientWithUser)(KVModuleCtx *ctx,
                                                                KVModuleUser *user,
                                                                KVModuleUserChangedFunc callback,
                                                                void *privdata,
                                                                uint64_t *client_id) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_DeauthenticateAndCloseClient)(KVModuleCtx *ctx,
                                                                  uint64_t client_id) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_RedactClientCommandArgument)(KVModuleCtx *ctx, int pos) KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_GetClientCertificate)(KVModuleCtx *ctx,
                                                                          uint64_t id)KVMODULE_ATTR;
KVMODULE_API int *(*KVModule_GetCommandKeys)(KVModuleCtx *ctx,
                                                     KVModuleString **argv,
                                                     int argc,
                                                     int *num_keys)KVMODULE_ATTR;
KVMODULE_API int *(*KVModule_GetCommandKeysWithFlags)(KVModuleCtx *ctx,
                                                              KVModuleString **argv,
                                                              int argc,
                                                              int *num_keys,
                                                              int **out_flags)KVMODULE_ATTR;
KVMODULE_API const char *(*KVModule_GetCurrentCommandName)(KVModuleCtx *ctx)KVMODULE_ATTR;
KVMODULE_API int (*KVModule_RegisterDefragFunc)(KVModuleCtx *ctx,
                                                        KVModuleDefragFunc func) KVMODULE_ATTR;
KVMODULE_API void *(*KVModule_DefragAlloc)(KVModuleDefragCtx *ctx, void *ptr)KVMODULE_ATTR;
KVMODULE_API KVModuleString *(*KVModule_DefragKVModuleString)(KVModuleDefragCtx *ctx,
                                                                              KVModuleString *str)KVMODULE_ATTR;
KVMODULE_API int (*KVModule_DefragShouldStop)(KVModuleDefragCtx *ctx) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_DefragCursorSet)(KVModuleDefragCtx *ctx,
                                                     unsigned long cursor) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_DefragCursorGet)(KVModuleDefragCtx *ctx,
                                                     unsigned long *cursor) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_GetDbIdFromDefragCtx)(KVModuleDefragCtx *ctx) KVMODULE_ATTR;
KVMODULE_API const KVModuleString *(*KVModule_GetKeyNameFromDefragCtx)(KVModuleDefragCtx *ctx)
    KVMODULE_ATTR;
KVMODULE_API int (*KVModule_EventLoopAdd)(int fd, int mask, KVModuleEventLoopFunc func, void *user_data)
    KVMODULE_ATTR;
KVMODULE_API int (*KVModule_EventLoopDel)(int fd, int mask) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_EventLoopAddOneShot)(KVModuleEventLoopOneShotFunc func,
                                                         void *user_data) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_RegisterBoolConfig)(KVModuleCtx *ctx,
                                                        const char *name,
                                                        int default_val,
                                                        unsigned int flags,
                                                        KVModuleConfigGetBoolFunc getfn,
                                                        KVModuleConfigSetBoolFunc setfn,
                                                        KVModuleConfigApplyFunc applyfn,
                                                        void *privdata) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_RegisterNumericConfig)(KVModuleCtx *ctx,
                                                           const char *name,
                                                           long long default_val,
                                                           unsigned int flags,
                                                           long long min,
                                                           long long max,
                                                           KVModuleConfigGetNumericFunc getfn,
                                                           KVModuleConfigSetNumericFunc setfn,
                                                           KVModuleConfigApplyFunc applyfn,
                                                           void *privdata) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_RegisterStringConfig)(KVModuleCtx *ctx,
                                                          const char *name,
                                                          const char *default_val,
                                                          unsigned int flags,
                                                          KVModuleConfigGetStringFunc getfn,
                                                          KVModuleConfigSetStringFunc setfn,
                                                          KVModuleConfigApplyFunc applyfn,
                                                          void *privdata) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_RegisterEnumConfig)(KVModuleCtx *ctx,
                                                        const char *name,
                                                        int default_val,
                                                        unsigned int flags,
                                                        const char **enum_values,
                                                        const int *int_values,
                                                        int num_enum_vals,
                                                        KVModuleConfigGetEnumFunc getfn,
                                                        KVModuleConfigSetEnumFunc setfn,
                                                        KVModuleConfigApplyFunc applyfn,
                                                        void *privdata) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_LoadConfigs)(KVModuleCtx *ctx) KVMODULE_ATTR;
KVMODULE_API KVModuleRdbStream *(*KVModule_RdbStreamCreateFromFile)(const char *filename)KVMODULE_ATTR;
KVMODULE_API void (*KVModule_RdbStreamFree)(KVModuleRdbStream *stream) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_RdbLoad)(KVModuleCtx *ctx,
                                             KVModuleRdbStream *stream,
                                             int flags) KVMODULE_ATTR;
KVMODULE_API int (*KVModule_RdbSave)(KVModuleCtx *ctx,
                                             KVModuleRdbStream *stream,
                                             int flags) KVMODULE_ATTR;

KVMODULE_API int (*KVModule_RegisterScriptingEngine)(KVModuleCtx *module_ctx,
                                                             const char *engine_name,
                                                             KVModuleScriptingEngineCtx *engine_ctx,
                                                             KVModuleScriptingEngineMethods *engine_methods) KVMODULE_ATTR;

KVMODULE_API int (*KVModule_UnregisterScriptingEngine)(KVModuleCtx *module_ctx,
                                                               const char *engine_name) KVMODULE_ATTR;

KVMODULE_API KVModuleScriptingEngineExecutionState (*KVModule_GetFunctionExecutionState)(KVModuleScriptingEngineServerRuntimeCtx *server_ctx) KVMODULE_ATTR;

KVMODULE_API void (*KVModule_ScriptingEngineDebuggerLog)(KVModuleString *msg,
                                                                 int truncate) KVMODULE_ATTR;

KVMODULE_API void (*KVModule_ScriptingEngineDebuggerLogRespReplyStr)(const char *reply) KVMODULE_ATTR;

KVMODULE_API void (*KVModule_ScriptingEngineDebuggerLogRespReply)(KVModuleCallReply *reply) KVMODULE_ATTR;

KVMODULE_API void (*KVModule_ScriptingEngineDebuggerFlushLogs)(void) KVMODULE_ATTR;

KVMODULE_API void (*KVModule_ScriptingEngineDebuggerProcessCommands)(int *client_disconnected,
                                                                             KVModuleString **err) KVMODULE_ATTR;

KVMODULE_API int (*KVModule_ACLCheckKeyPrefixPermissions)(KVModuleUser *user,
                                                                  const char *key,
                                                                  size_t len,
                                                                  unsigned int flags) KVMODULE_ATTR;

#define KVModule_IsAOFClient(id) ((id) == UINT64_MAX)
/* This is included inline inside each KV module. */
static int KVModule_Init(KVModuleCtx *ctx, const char *name, int ver, int apiver) KVMODULE_ATTR_UNUSED;
static int KVModule_Init(KVModuleCtx *ctx, const char *name, int ver, int apiver) {
    void *getapifuncptr = ((void **)ctx)[0];
    KVModule_GetApi = (int (*)(const char *, void *))(unsigned long)getapifuncptr;
    KVMODULE_GET_API(Alloc);
    KVMODULE_GET_API(TryAlloc);
    KVMODULE_GET_API(Calloc);
    KVMODULE_GET_API(TryCalloc);
    KVMODULE_GET_API(Free);
    KVMODULE_GET_API(Realloc);
    KVMODULE_GET_API(TryRealloc);
    KVMODULE_GET_API(Strdup);
    KVMODULE_GET_API(CreateCommand);
    KVMODULE_GET_API(GetCommand);
    KVMODULE_GET_API(CreateSubcommand);
    KVMODULE_GET_API(SetCommandInfo);
    KVMODULE_GET_API(SetCommandACLCategories);
    KVMODULE_GET_API(AddACLCategory);
    KVMODULE_GET_API(SetModuleAttribs);
    KVMODULE_GET_API(IsModuleNameBusy);
    KVMODULE_GET_API(WrongArity);
    KVMODULE_GET_API(UpdateRuntimeArgs);
    KVMODULE_GET_API(ReplyWithLongLong);
    KVMODULE_GET_API(ReplyWithError);
    KVMODULE_GET_API(ReplyWithErrorFormat);
    KVMODULE_GET_API(ReplyWithCustomErrorFormat);
    KVMODULE_GET_API(ReplyWithSimpleString);
    KVMODULE_GET_API(ReplyWithArray);
    KVMODULE_GET_API(ReplyWithMap);
    KVMODULE_GET_API(ReplyWithSet);
    KVMODULE_GET_API(ReplyWithAttribute);
    KVMODULE_GET_API(ReplyWithNullArray);
    KVMODULE_GET_API(ReplyWithEmptyArray);
    KVMODULE_GET_API(ReplySetArrayLength);
    KVMODULE_GET_API(ReplySetMapLength);
    KVMODULE_GET_API(ReplySetSetLength);
    KVMODULE_GET_API(ReplySetAttributeLength);
    KVMODULE_GET_API(ReplySetPushLength);
    KVMODULE_GET_API(ReplyWithStringBuffer);
    KVMODULE_GET_API(ReplyWithCString);
    KVMODULE_GET_API(ReplyWithString);
    KVMODULE_GET_API(ReplyWithEmptyString);
    KVMODULE_GET_API(ReplyWithVerbatimString);
    KVMODULE_GET_API(ReplyWithVerbatimStringType);
    KVMODULE_GET_API(ReplyWithNull);
    KVMODULE_GET_API(ReplyWithBool);
    KVMODULE_GET_API(ReplyWithCallReply);
    KVMODULE_GET_API(ReplyWithDouble);
    KVMODULE_GET_API(ReplyWithBigNumber);
    KVMODULE_GET_API(ReplyWithLongDouble);
    KVMODULE_GET_API(GetSelectedDb);
    KVMODULE_GET_API(SelectDb);
    KVMODULE_GET_API(KeyExists);
    KVMODULE_GET_API(OpenKey);
    KVMODULE_GET_API(GetOpenKeyModesAll);
    KVMODULE_GET_API(CloseKey);
    KVMODULE_GET_API(KeyType);
    KVMODULE_GET_API(ValueLength);
    KVMODULE_GET_API(ListPush);
    KVMODULE_GET_API(ListPop);
    KVMODULE_GET_API(ListGet);
    KVMODULE_GET_API(ListSet);
    KVMODULE_GET_API(ListInsert);
    KVMODULE_GET_API(ListDelete);
    KVMODULE_GET_API(StringToLongLong);
    KVMODULE_GET_API(StringToULongLong);
    KVMODULE_GET_API(StringToDouble);
    KVMODULE_GET_API(StringToLongDouble);
    KVMODULE_GET_API(StringToStreamID);
    KVMODULE_GET_API(Call);
    KVMODULE_GET_API(CallReplyProto);
    KVMODULE_GET_API(FreeCallReply);
    KVMODULE_GET_API(CallReplyInteger);
    KVMODULE_GET_API(CallReplyDouble);
    KVMODULE_GET_API(CallReplyBool);
    KVMODULE_GET_API(CallReplyBigNumber);
    KVMODULE_GET_API(CallReplyVerbatim);
    KVMODULE_GET_API(CallReplySetElement);
    KVMODULE_GET_API(CallReplyMapElement);
    KVMODULE_GET_API(CallReplyAttributeElement);
    KVMODULE_GET_API(CallReplyPromiseSetUnblockHandler);
    KVMODULE_GET_API(CallReplyPromiseAbort);
    KVMODULE_GET_API(CallReplyAttribute);
    KVMODULE_GET_API(CallReplyType);
    KVMODULE_GET_API(CallReplyLength);
    KVMODULE_GET_API(CallReplyArrayElement);
    KVMODULE_GET_API(CallReplyStringPtr);
    KVMODULE_GET_API(CreateStringFromCallReply);
    KVMODULE_GET_API(CreateString);
    KVMODULE_GET_API(CreateStringFromLongLong);
    KVMODULE_GET_API(CreateStringFromULongLong);
    KVMODULE_GET_API(CreateStringFromDouble);
    KVMODULE_GET_API(CreateStringFromLongDouble);
    KVMODULE_GET_API(CreateStringFromString);
    KVMODULE_GET_API(CreateStringFromStreamID);
    KVMODULE_GET_API(CreateStringPrintf);
    KVMODULE_GET_API(FreeString);
    KVMODULE_GET_API(StringPtrLen);
    KVMODULE_GET_API(AutoMemory);
    KVMODULE_GET_API(Replicate);
    KVMODULE_GET_API(ReplicateVerbatim);
    KVMODULE_GET_API(DeleteKey);
    KVMODULE_GET_API(UnlinkKey);
    KVMODULE_GET_API(StringSet);
    KVMODULE_GET_API(StringDMA);
    KVMODULE_GET_API(StringTruncate);
    KVMODULE_GET_API(GetExpire);
    KVMODULE_GET_API(SetExpire);
    KVMODULE_GET_API(GetAbsExpire);
    KVMODULE_GET_API(SetAbsExpire);
    KVMODULE_GET_API(ResetDataset);
    KVMODULE_GET_API(DbSize);
    KVMODULE_GET_API(RandomKey);
    KVMODULE_GET_API(ZsetAdd);
    KVMODULE_GET_API(ZsetIncrby);
    KVMODULE_GET_API(ZsetScore);
    KVMODULE_GET_API(ZsetRem);
    KVMODULE_GET_API(ZsetRangeStop);
    KVMODULE_GET_API(ZsetFirstInScoreRange);
    KVMODULE_GET_API(ZsetLastInScoreRange);
    KVMODULE_GET_API(ZsetFirstInLexRange);
    KVMODULE_GET_API(ZsetLastInLexRange);
    KVMODULE_GET_API(ZsetRangeCurrentElement);
    KVMODULE_GET_API(ZsetRangeNext);
    KVMODULE_GET_API(ZsetRangePrev);
    KVMODULE_GET_API(ZsetRangeEndReached);
    KVMODULE_GET_API(HashSet);
    KVMODULE_GET_API(HashGet);
    KVMODULE_GET_API(HashSetStringRef);
    KVMODULE_GET_API(HashHasStringRef);
    KVMODULE_GET_API(StreamAdd);
    KVMODULE_GET_API(StreamDelete);
    KVMODULE_GET_API(StreamIteratorStart);
    KVMODULE_GET_API(StreamIteratorStop);
    KVMODULE_GET_API(StreamIteratorNextID);
    KVMODULE_GET_API(StreamIteratorNextField);
    KVMODULE_GET_API(StreamIteratorDelete);
    KVMODULE_GET_API(StreamTrimByLength);
    KVMODULE_GET_API(StreamTrimByID);
    KVMODULE_GET_API(IsKeysPositionRequest);
    KVMODULE_GET_API(KeyAtPos);
    KVMODULE_GET_API(KeyAtPosWithFlags);
    KVMODULE_GET_API(IsChannelsPositionRequest);
    KVMODULE_GET_API(ChannelAtPosWithFlags);
    KVMODULE_GET_API(GetClientId);
    KVMODULE_GET_API(GetClientUserNameById);
    KVMODULE_GET_API(MustObeyClient);
    KVMODULE_GET_API(GetContextFlags);
    KVMODULE_GET_API(AvoidReplicaTraffic);
    KVMODULE_GET_API(PoolAlloc);
    KVMODULE_GET_API(CreateDataType);
    KVMODULE_GET_API(ModuleTypeSetValue);
    KVMODULE_GET_API(ModuleTypeReplaceValue);
    KVMODULE_GET_API(ModuleTypeGetType);
    KVMODULE_GET_API(ModuleTypeGetValue);
    KVMODULE_GET_API(IsIOError);
    KVMODULE_GET_API(SetModuleOptions);
    KVMODULE_GET_API(SignalModifiedKey);
    KVMODULE_GET_API(SaveUnsigned);
    KVMODULE_GET_API(LoadUnsigned);
    KVMODULE_GET_API(SaveSigned);
    KVMODULE_GET_API(LoadSigned);
    KVMODULE_GET_API(SaveString);
    KVMODULE_GET_API(SaveStringBuffer);
    KVMODULE_GET_API(LoadString);
    KVMODULE_GET_API(LoadStringBuffer);
    KVMODULE_GET_API(SaveDouble);
    KVMODULE_GET_API(LoadDouble);
    KVMODULE_GET_API(SaveFloat);
    KVMODULE_GET_API(LoadFloat);
    KVMODULE_GET_API(SaveLongDouble);
    KVMODULE_GET_API(LoadLongDouble);
    KVMODULE_GET_API(SaveDataTypeToString);
    KVMODULE_GET_API(LoadDataTypeFromString);
    KVMODULE_GET_API(LoadDataTypeFromStringEncver);
    KVMODULE_GET_API(EmitAOF);
    KVMODULE_GET_API(Log);
    KVMODULE_GET_API(LogIOError);
    KVMODULE_GET_API(_Assert);
    KVMODULE_GET_API(LatencyAddSample);
    KVMODULE_GET_API(StringAppendBuffer);
    KVMODULE_GET_API(TrimStringAllocation);
    KVMODULE_GET_API(RetainString);
    KVMODULE_GET_API(HoldString);
    KVMODULE_GET_API(StringCompare);
    KVMODULE_GET_API(GetContextFromIO);
    KVMODULE_GET_API(GetKeyNameFromIO);
    KVMODULE_GET_API(GetKeyNameFromModuleKey);
    KVMODULE_GET_API(GetDbIdFromModuleKey);
    KVMODULE_GET_API(GetDbIdFromIO);
    KVMODULE_GET_API(GetKeyNameFromOptCtx);
    KVMODULE_GET_API(GetToKeyNameFromOptCtx);
    KVMODULE_GET_API(GetDbIdFromOptCtx);
    KVMODULE_GET_API(GetToDbIdFromOptCtx);
    KVMODULE_GET_API(Milliseconds);
    KVMODULE_GET_API(MonotonicMicroseconds);
    KVMODULE_GET_API(Microseconds);
    KVMODULE_GET_API(CachedMicroseconds);
    KVMODULE_GET_API(DigestAddStringBuffer);
    KVMODULE_GET_API(DigestAddLongLong);
    KVMODULE_GET_API(DigestEndSequence);
    KVMODULE_GET_API(GetKeyNameFromDigest);
    KVMODULE_GET_API(GetDbIdFromDigest);
    KVMODULE_GET_API(CreateDict);
    KVMODULE_GET_API(FreeDict);
    KVMODULE_GET_API(DictSize);
    KVMODULE_GET_API(DictSetC);
    KVMODULE_GET_API(DictReplaceC);
    KVMODULE_GET_API(DictSet);
    KVMODULE_GET_API(DictReplace);
    KVMODULE_GET_API(DictGetC);
    KVMODULE_GET_API(DictGet);
    KVMODULE_GET_API(DictDelC);
    KVMODULE_GET_API(DictDel);
    KVMODULE_GET_API(DictIteratorStartC);
    KVMODULE_GET_API(DictIteratorStart);
    KVMODULE_GET_API(DictIteratorStop);
    KVMODULE_GET_API(DictIteratorReseekC);
    KVMODULE_GET_API(DictIteratorReseek);
    KVMODULE_GET_API(DictNextC);
    KVMODULE_GET_API(DictPrevC);
    KVMODULE_GET_API(DictNext);
    KVMODULE_GET_API(DictPrev);
    KVMODULE_GET_API(DictCompare);
    KVMODULE_GET_API(DictCompareC);
    KVMODULE_GET_API(RegisterInfoFunc);
    KVMODULE_GET_API(RegisterAuthCallback);
    KVMODULE_GET_API(InfoAddSection);
    KVMODULE_GET_API(InfoBeginDictField);
    KVMODULE_GET_API(InfoEndDictField);
    KVMODULE_GET_API(InfoAddFieldString);
    KVMODULE_GET_API(InfoAddFieldCString);
    KVMODULE_GET_API(InfoAddFieldDouble);
    KVMODULE_GET_API(InfoAddFieldLongLong);
    KVMODULE_GET_API(InfoAddFieldULongLong);
    KVMODULE_GET_API(GetServerInfo);
    KVMODULE_GET_API(FreeServerInfo);
    KVMODULE_GET_API(ServerInfoGetField);
    KVMODULE_GET_API(ServerInfoGetFieldC);
    KVMODULE_GET_API(ServerInfoGetFieldSigned);
    KVMODULE_GET_API(ServerInfoGetFieldUnsigned);
    KVMODULE_GET_API(ServerInfoGetFieldDouble);
    KVMODULE_GET_API(GetClientInfoById);
    KVMODULE_GET_API(GetClientNameById);
    KVMODULE_GET_API(SetClientNameById);
    KVMODULE_GET_API(PublishMessage);
    KVMODULE_GET_API(PublishMessageShard);
    KVMODULE_GET_API(SubscribeToServerEvent);
    KVMODULE_GET_API(SetLRU);
    KVMODULE_GET_API(GetLRU);
    KVMODULE_GET_API(SetLFU);
    KVMODULE_GET_API(GetLFU);
    KVMODULE_GET_API(BlockClientOnKeys);
    KVMODULE_GET_API(BlockClientOnKeysWithFlags);
    KVMODULE_GET_API(SignalKeyAsReady);
    KVMODULE_GET_API(GetBlockedClientReadyKey);
    KVMODULE_GET_API(ScanCursorCreate);
    KVMODULE_GET_API(ScanCursorRestart);
    KVMODULE_GET_API(ScanCursorDestroy);
    KVMODULE_GET_API(Scan);
    KVMODULE_GET_API(ScanKey);
    KVMODULE_GET_API(GetContextFlagsAll);
    KVMODULE_GET_API(GetModuleOptionsAll);
    KVMODULE_GET_API(GetKeyspaceNotificationFlagsAll);
    KVMODULE_GET_API(IsSubEventSupported);
    KVMODULE_GET_API(GetServerVersion);
    KVMODULE_GET_API(GetTypeMethodVersion);
    KVMODULE_GET_API(Yield);
    KVMODULE_GET_API(GetThreadSafeContext);
    KVMODULE_GET_API(GetDetachedThreadSafeContext);
    KVMODULE_GET_API(FreeThreadSafeContext);
    KVMODULE_GET_API(ThreadSafeContextLock);
    KVMODULE_GET_API(ThreadSafeContextTryLock);
    KVMODULE_GET_API(ThreadSafeContextUnlock);
    KVMODULE_GET_API(BlockClient);
    KVMODULE_GET_API(BlockClientGetPrivateData);
    KVMODULE_GET_API(BlockClientSetPrivateData);
    KVMODULE_GET_API(BlockClientOnAuth);
    KVMODULE_GET_API(UnblockClient);
    KVMODULE_GET_API(IsBlockedReplyRequest);
    KVMODULE_GET_API(IsBlockedTimeoutRequest);
    KVMODULE_GET_API(GetBlockedClientPrivateData);
    KVMODULE_GET_API(GetBlockedClientHandle);
    KVMODULE_GET_API(AbortBlock);
    KVMODULE_GET_API(BlockedClientMeasureTimeStart);
    KVMODULE_GET_API(BlockedClientMeasureTimeEnd);
    KVMODULE_GET_API(SetDisconnectCallback);
    KVMODULE_GET_API(SubscribeToKeyspaceEvents);
    KVMODULE_GET_API(AddPostNotificationJob);
    KVMODULE_GET_API(NotifyKeyspaceEvent);
    KVMODULE_GET_API(GetNotifyKeyspaceEvents);
    KVMODULE_GET_API(BlockedClientDisconnected);
    KVMODULE_GET_API(RegisterClusterMessageReceiver);
    KVMODULE_GET_API(SendClusterMessage);
    KVMODULE_GET_API(GetClusterNodeInfo);
    KVMODULE_GET_API(GetClusterNodeInfoForClient);
    KVMODULE_GET_API(GetClusterNodesList);
    KVMODULE_GET_API(FreeClusterNodesList);
    KVMODULE_GET_API(CreateTimer);
    KVMODULE_GET_API(StopTimer);
    KVMODULE_GET_API(GetTimerInfo);
    KVMODULE_GET_API(GetMyClusterID);
    KVMODULE_GET_API(GetClusterSize);
    KVMODULE_GET_API(GetRandomBytes);
    KVMODULE_GET_API(GetRandomHexChars);
    KVMODULE_GET_API(SetClusterFlags);
    KVMODULE_GET_API(ClusterKeySlotC);
    KVMODULE_GET_API(ClusterKeySlot);
    KVMODULE_GET_API(ClusterCanonicalKeyNameInSlot);
    KVMODULE_GET_API(ExportSharedAPI);
    KVMODULE_GET_API(GetSharedAPI);
    KVMODULE_GET_API(RegisterCommandFilter);
    KVMODULE_GET_API(UnregisterCommandFilter);
    KVMODULE_GET_API(CommandFilterArgsCount);
    KVMODULE_GET_API(CommandFilterArgGet);
    KVMODULE_GET_API(CommandFilterArgInsert);
    KVMODULE_GET_API(CommandFilterArgReplace);
    KVMODULE_GET_API(CommandFilterArgDelete);
    KVMODULE_GET_API(CommandFilterGetClientId);
    KVMODULE_GET_API(Fork);
    KVMODULE_GET_API(SendChildHeartbeat);
    KVMODULE_GET_API(ExitFromChild);
    KVMODULE_GET_API(KillForkChild);
    KVMODULE_GET_API(GetUsedMemoryRatio);
    KVMODULE_GET_API(MallocSize);
    KVMODULE_GET_API(MallocUsableSize);
    KVMODULE_GET_API(MallocSizeString);
    KVMODULE_GET_API(MallocSizeDict);
    KVMODULE_GET_API(CreateModuleUser);
    KVMODULE_GET_API(FreeModuleUser);
    KVMODULE_GET_API(SetContextUser);
    KVMODULE_GET_API(SetModuleUserACL);
    KVMODULE_GET_API(SetModuleUserACLString);
    KVMODULE_GET_API(GetModuleUserACLString);
    KVMODULE_GET_API(GetCurrentUserName);
    KVMODULE_GET_API(GetModuleUserFromUserName);
    KVMODULE_GET_API(ACLCheckCommandPermissions);
    KVMODULE_GET_API(ACLCheckKeyPermissions);
    KVMODULE_GET_API(ACLCheckChannelPermissions);
    KVMODULE_GET_API(ACLCheckPermissions);
    KVMODULE_GET_API(ACLAddLogEntry);
    KVMODULE_GET_API(ACLAddLogEntryByUserName);
    KVMODULE_GET_API(DeauthenticateAndCloseClient);
    KVMODULE_GET_API(AuthenticateClientWithACLUser);
    KVMODULE_GET_API(AuthenticateClientWithUser);
    KVMODULE_GET_API(RedactClientCommandArgument);
    KVMODULE_GET_API(GetClientCertificate);
    KVMODULE_GET_API(GetCommandKeys);
    KVMODULE_GET_API(GetCommandKeysWithFlags);
    KVMODULE_GET_API(GetCurrentCommandName);
    KVMODULE_GET_API(RegisterDefragFunc);
    KVMODULE_GET_API(DefragAlloc);
    KVMODULE_GET_API(DefragKVModuleString);
    KVMODULE_GET_API(DefragShouldStop);
    KVMODULE_GET_API(DefragCursorSet);
    KVMODULE_GET_API(DefragCursorGet);
    KVMODULE_GET_API(GetKeyNameFromDefragCtx);
    KVMODULE_GET_API(GetDbIdFromDefragCtx);
    KVMODULE_GET_API(EventLoopAdd);
    KVMODULE_GET_API(EventLoopDel);
    KVMODULE_GET_API(EventLoopAddOneShot);
    KVMODULE_GET_API(RegisterBoolConfig);
    KVMODULE_GET_API(RegisterNumericConfig);
    KVMODULE_GET_API(RegisterStringConfig);
    KVMODULE_GET_API(RegisterEnumConfig);
    KVMODULE_GET_API(LoadConfigs);
    KVMODULE_GET_API(RdbStreamCreateFromFile);
    KVMODULE_GET_API(RdbStreamFree);
    KVMODULE_GET_API(RdbLoad);
    KVMODULE_GET_API(RdbSave);
    KVMODULE_GET_API(RegisterScriptingEngine);
    KVMODULE_GET_API(UnregisterScriptingEngine);
    KVMODULE_GET_API(GetFunctionExecutionState);
    KVMODULE_GET_API(ScriptingEngineDebuggerLog);
    KVMODULE_GET_API(ScriptingEngineDebuggerLogRespReplyStr);
    KVMODULE_GET_API(ScriptingEngineDebuggerLogRespReply);
    KVMODULE_GET_API(ScriptingEngineDebuggerFlushLogs);
    KVMODULE_GET_API(ScriptingEngineDebuggerProcessCommands);
    KVMODULE_GET_API(ACLCheckKeyPrefixPermissions);

    if (KVModule_IsModuleNameBusy && KVModule_IsModuleNameBusy(name)) return KVMODULE_ERR;
    KVModule_SetModuleAttribs(ctx, name, ver, apiver);
    return KVMODULE_OK;
}

#define KVModule_Assert(_e) ((_e) ? (void)0 : (KVModule__Assert(#_e, __FILE__, __LINE__), exit(1)))

#define RMAPI_FUNC_SUPPORTED(func) (func != NULL)

#endif /* KVMODULE_CORE */
#endif /* KVMODULE_H */
