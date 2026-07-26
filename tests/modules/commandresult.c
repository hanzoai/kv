/* Test module for command result event API
 *
 * This module tests the KVMODULE_EVENT_COMMAND_RESULT_SUCCESS,
 * KVMODULE_EVENT_COMMAND_RESULT_FAILURE,
 * KVMODULE_EVENT_COMMAND_RESULT_REJECTED, and
 * KVMODULE_EVENT_COMMAND_RESULT_ACL_REJECTED server events.
 *
 * Commands provided:
 * - CMDRESULT.REGISTER <mode> - Register event subscription
 * (success/failure/rejected/acl_rejected/all)
 * - CMDRESULT.UNSUBSCRIBE - Unsubscribe from the event
 * - CMDRESULT.STATS - Get statistics about event invocations
 * - CMDRESULT.RESET - Reset statistics
 * - CMDRESULT.GETLOG [count] - Get the last N logged command results
 * - CMDRESULT.SUCCESS - A command that always succeeds
 * - CMDRESULT.FAIL - A command that always fails
 * - CMDRESULT.RMCALL <command> [args...] - Call a command via RM_Call
 */

#include "kvmodule.h"
#include <stdlib.h>
#include <string.h>

/* Statistics tracking */
static struct {
  long long total_callbacks;
  long long success_count;
  long long failure_count;
  long long rejected_count;
  long long acl_denied_count;
  long long total_duration_us;
  long long total_dirty;
} stats = {0};

/* Command result log entry */
#define MAX_LOG_ENTRIES 100
#define MAX_ARGV_LOG 10
#define MAX_ARG_LEN 128

typedef struct {
  char command_name[64];
  int status; /* 0 = success, 1 = failure, 2 = acl_rejected, 3 = rejected */
  uint64_t subevent;
  long long duration;
  long long dirty;
  unsigned long long client_id;
  int is_module_client;
  int argc;
  char argv[MAX_ARGV_LOG][MAX_ARG_LEN];
  char rejection_context[MAX_ARG_LEN];
} ResultLogEntry;

static ResultLogEntry result_log[MAX_LOG_ENTRIES];
static int log_head = 0;
static int log_count = 0;

/* Track subscription mode bitmask:
 * bit 0 = success, bit 1 = failure, bit 2 = rejected, bit 3 = acl_rejected */
#define MODE_SUCCESS 0x1
#define MODE_FAILURE 0x2
#define MODE_REJECTED 0x4
#define MODE_ACL_REJECTED 0x8
static int subscription_mode = 0;

static void ResetState(void) {
  memset(&stats, 0, sizeof(stats));
  memset(result_log, 0, sizeof(result_log));
  log_head = 0;
  log_count = 0;
  subscription_mode = 0;
}

/* Add entry to circular log */
void LogResult(const char *cmd_name, int status, uint64_t subevent,
               long long duration, long long dirty,
               unsigned long long client_id, int is_module_client,
               KVModuleString **argv, int argc,
               const char *rejection_context) {
  ResultLogEntry *entry = &result_log[log_head];

  strncpy(entry->command_name, cmd_name, sizeof(entry->command_name) - 1);
  entry->command_name[sizeof(entry->command_name) - 1] = '\0';
  entry->status = status;
  entry->subevent = subevent;
  entry->duration = duration;
  entry->dirty = dirty;
  entry->client_id = client_id;
  entry->is_module_client = is_module_client;

  if (rejection_context) {
    strncpy(entry->rejection_context, rejection_context,
            sizeof(entry->rejection_context) - 1);
    entry->rejection_context[sizeof(entry->rejection_context) - 1] = '\0';
  } else {
    entry->rejection_context[0] = '\0';
  }

  /* Store argv */
  if (argv && argc > 0) {
    entry->argc = (argc < MAX_ARGV_LOG) ? argc : MAX_ARGV_LOG;
    for (int i = 0; i < entry->argc; i++) {
      if (argv[i] == NULL) {
        strcpy(entry->argv[i], "(null)");
        continue;
      }
      size_t len;
      const char *arg = KVModule_StringPtrLen(argv[i], &len);
      if (arg == NULL) {
        strcpy(entry->argv[i], "(empty)");
        continue;
      }
      size_t copy_len = (len < MAX_ARG_LEN - 1) ? len : MAX_ARG_LEN - 1;
      memcpy(entry->argv[i], arg, copy_len);
      entry->argv[i][copy_len] = '\0';
    }
  } else {
    entry->argc = 0;
  }

  log_head = (log_head + 1) % MAX_LOG_ENTRIES;
  if (log_count < MAX_LOG_ENTRIES)
    log_count++;
}

/* Command result event callback — handles success, failure, and rejected
 * events */
void CommandResultEventCallback(KVModuleCtx *ctx, KVModuleEvent eid,
                                uint64_t subevent, void *data) {
  KVMODULE_NOT_USED(ctx);

  KVModuleCommandResultInfo *info = (KVModuleCommandResultInfo *)data;

  if (info->version != KVMODULE_COMMANDRESULTINFO_VERSION)
    return;

  stats.total_callbacks++;

  int status;
  if (eid.id == KVMODULE_EVENT_COMMAND_RESULT_ACL_REJECTED) {
    status = 2;
    stats.acl_denied_count++;
  } else if (eid.id == KVMODULE_EVENT_COMMAND_RESULT_REJECTED) {
    status = 3;
    stats.rejected_count++;
  } else if (eid.id == KVMODULE_EVENT_COMMAND_RESULT_FAILURE) {
    status = 1;
    stats.failure_count++;
  } else {
    status = 0;
    stats.success_count++;
  }

  stats.total_duration_us += info->duration_us;
  stats.total_dirty += info->dirty;

  LogResult(info->command_name ? info->command_name : "unknown", status,
            subevent, info->duration_us, info->dirty, info->client_id,
            info->is_module_client, info->argv, info->argc,
            info->rejection_context);
}

/* CMDRESULT.REGISTER <mode>
 * Mode can be: "all", "success", "failure", "rejected"
 */
int CmdResultRegister_ValkeyCommand(KVModuleCtx *ctx,
                                    KVModuleString **argv, int argc) {
  if (argc != 2) {
    return KVModule_WrongArity(ctx);
  }

  if (subscription_mode != 0) {
    return KVModule_ReplyWithError(
        ctx, "ERR already subscribed to command result events");
  }

  size_t len;
  const char *mode_str = KVModule_StringPtrLen(argv[1], &len);

  int new_mode = 0;
  if (strcmp(mode_str, "all") == 0) {
    new_mode = MODE_SUCCESS | MODE_FAILURE | MODE_REJECTED | MODE_ACL_REJECTED;
  } else if (strcmp(mode_str, "success") == 0) {
    new_mode = MODE_SUCCESS;
  } else if (strcmp(mode_str, "failure") == 0) {
    new_mode = MODE_FAILURE;
  } else if (strcmp(mode_str, "rejected") == 0) {
    new_mode = MODE_REJECTED;
  } else if (strcmp(mode_str, "acl_rejected") == 0) {
    new_mode = MODE_ACL_REJECTED;
  } else {
    return KVModule_ReplyWithError(ctx,
                                       "ERR invalid mode. Use: all, success, "
                                       "failure, rejected, or acl_rejected");
  }

  if ((new_mode & MODE_SUCCESS) &&
      KVModule_SubscribeToServerEvent(
          ctx, KVModuleEvent_CommandResultSuccess,
          CommandResultEventCallback) == KVMODULE_ERR) {
    return KVModule_ReplyWithError(
        ctx, "ERR failed to subscribe to success event");
  }

  if ((new_mode & MODE_FAILURE) &&
      KVModule_SubscribeToServerEvent(
          ctx, KVModuleEvent_CommandResultFailure,
          CommandResultEventCallback) == KVMODULE_ERR) {
    if (new_mode & MODE_SUCCESS)
      KVModule_SubscribeToServerEvent(
          ctx, KVModuleEvent_CommandResultSuccess, NULL);
    return KVModule_ReplyWithError(
        ctx, "ERR failed to subscribe to failure event");
  }

  if ((new_mode & MODE_REJECTED) &&
      KVModule_SubscribeToServerEvent(
          ctx, KVModuleEvent_CommandResultRejected,
          CommandResultEventCallback) == KVMODULE_ERR) {
    if (new_mode & MODE_SUCCESS)
      KVModule_SubscribeToServerEvent(
          ctx, KVModuleEvent_CommandResultSuccess, NULL);
    if (new_mode & MODE_FAILURE)
      KVModule_SubscribeToServerEvent(
          ctx, KVModuleEvent_CommandResultFailure, NULL);
    return KVModule_ReplyWithError(
        ctx, "ERR failed to subscribe to rejected event");
  }

  if ((new_mode & MODE_ACL_REJECTED) &&
      KVModule_SubscribeToServerEvent(
          ctx, KVModuleEvent_CommandResultACLRejected,
          CommandResultEventCallback) == KVMODULE_ERR) {
    if (new_mode & MODE_SUCCESS)
      KVModule_SubscribeToServerEvent(
          ctx, KVModuleEvent_CommandResultSuccess, NULL);
    if (new_mode & MODE_FAILURE)
      KVModule_SubscribeToServerEvent(
          ctx, KVModuleEvent_CommandResultFailure, NULL);
    if (new_mode & MODE_REJECTED)
      KVModule_SubscribeToServerEvent(
          ctx, KVModuleEvent_CommandResultRejected, NULL);
    return KVModule_ReplyWithError(
        ctx, "ERR failed to subscribe to acl_rejected event");
  }

  subscription_mode = new_mode;
  return KVModule_ReplyWithSimpleString(ctx, "OK");
}

/* CMDRESULT.UNSUBSCRIBE */
int CmdResultUnsubscribe_ValkeyCommand(KVModuleCtx *ctx,
                                       KVModuleString **argv, int argc) {
  KVMODULE_NOT_USED(argv);
  int had_subscription = (subscription_mode != 0);

  if (argc != 1) {
    return KVModule_WrongArity(ctx);
  }

  KVModule_SubscribeToServerEvent(ctx, KVModuleEvent_CommandResultSuccess, NULL);
  KVModule_SubscribeToServerEvent(ctx, KVModuleEvent_CommandResultFailure, NULL);
  KVModule_SubscribeToServerEvent(ctx, KVModuleEvent_CommandResultRejected, NULL);
  KVModule_SubscribeToServerEvent(ctx, KVModuleEvent_CommandResultACLRejected, NULL);
  subscription_mode = 0;

  if (!had_subscription) {
    return KVModule_ReplyWithError(
        ctx, "ERR not subscribed to command result events");
  }

  return KVModule_ReplyWithSimpleString(ctx, "OK");
}

/* CMDRESULT.STATS
 * Returns: total_callbacks, success_count, failure_count, rejected_count,
 *          total_duration_us, total_dirty
 */
int CmdResultStats_ValkeyCommand(KVModuleCtx *ctx,
                                 KVModuleString **argv, int argc) {
  KVMODULE_NOT_USED(argv);

  if (argc != 1) {
    return KVModule_WrongArity(ctx);
  }

  KVModule_ReplyWithArray(ctx, 14);
  KVModule_ReplyWithSimpleString(ctx, "total_callbacks");
  KVModule_ReplyWithLongLong(ctx, stats.total_callbacks);
  KVModule_ReplyWithSimpleString(ctx, "success_count");
  KVModule_ReplyWithLongLong(ctx, stats.success_count);
  KVModule_ReplyWithSimpleString(ctx, "failure_count");
  KVModule_ReplyWithLongLong(ctx, stats.failure_count);
  KVModule_ReplyWithSimpleString(ctx, "rejected_count");
  KVModule_ReplyWithLongLong(ctx, stats.rejected_count);
  KVModule_ReplyWithSimpleString(ctx, "acl_denied_count");
  KVModule_ReplyWithLongLong(ctx, stats.acl_denied_count);
  KVModule_ReplyWithSimpleString(ctx, "total_duration_us");
  KVModule_ReplyWithLongLong(ctx, stats.total_duration_us);
  KVModule_ReplyWithSimpleString(ctx, "total_dirty");
  KVModule_ReplyWithLongLong(ctx, stats.total_dirty);

  return KVMODULE_OK;
}

/* CMDRESULT.RESET */
int CmdResultReset_ValkeyCommand(KVModuleCtx *ctx,
                                 KVModuleString **argv, int argc) {
  KVMODULE_NOT_USED(argv);

  if (argc != 1) {
    return KVModule_WrongArity(ctx);
  }

  stats.total_callbacks = 0;
  stats.success_count = 0;
  stats.failure_count = 0;
  stats.rejected_count = 0;
  stats.acl_denied_count = 0;
  stats.total_duration_us = 0;
  stats.total_dirty = 0;

  log_head = 0;
  log_count = 0;

  return KVModule_ReplyWithSimpleString(ctx, "OK");
}

/* CMDRESULT.GETLOG [count]
 * Returns the last N command results from the log
 */
int CmdResultGetLog_ValkeyCommand(KVModuleCtx *ctx,
                                  KVModuleString **argv, int argc) {
  if (argc > 2) {
    return KVModule_WrongArity(ctx);
  }

  long long count = log_count;
  if (argc == 2) {
    if (KVModule_StringToLongLong(argv[1], &count) != KVMODULE_OK) {
      return KVModule_ReplyWithError(ctx, "ERR invalid count");
    }
    if (count < 0)
      count = 0;
    if (count > log_count)
      count = log_count;
  }

  KVModule_ReplyWithArray(ctx, count);

  /* Get entries from newest to oldest */
  for (int i = 0; i < count; i++) {
    int idx = (log_head - 1 - i + MAX_LOG_ENTRIES) % MAX_LOG_ENTRIES;
    ResultLogEntry *entry = &result_log[idx];

    const char *status_str;
    if (entry->status == 3)
      status_str = "rejected";
    else if (entry->status == 2)
      status_str = "acl_rejected";
    else if (entry->status == 1)
      status_str = "failure";
    else
      status_str = "success";

    KVModule_ReplyWithArray(ctx, 18);
    KVModule_ReplyWithSimpleString(ctx, "command");
    KVModule_ReplyWithCString(ctx, entry->command_name);
    KVModule_ReplyWithSimpleString(ctx, "status");
    KVModule_ReplyWithCString(ctx, status_str);
    KVModule_ReplyWithSimpleString(ctx, "duration_us");
    KVModule_ReplyWithLongLong(ctx, entry->duration);
    KVModule_ReplyWithSimpleString(ctx, "dirty");
    KVModule_ReplyWithLongLong(ctx, entry->dirty);
    KVModule_ReplyWithSimpleString(ctx, "client_id");
    KVModule_ReplyWithLongLong(ctx, entry->client_id);
    KVModule_ReplyWithSimpleString(ctx, "is_module_client");
    KVModule_ReplyWithLongLong(ctx, entry->is_module_client);
    KVModule_ReplyWithSimpleString(ctx, "subevent");
    KVModule_ReplyWithLongLong(ctx, entry->subevent);
    KVModule_ReplyWithSimpleString(ctx, "rejection_context");
    KVModule_ReplyWithCString(ctx, entry->rejection_context);
    KVModule_ReplyWithSimpleString(ctx, "argv");
    KVModule_ReplyWithArray(ctx, entry->argc);
    for (int j = 0; j < entry->argc; j++) {
      KVModule_ReplyWithCString(ctx, entry->argv[j]);
    }
  }

  return KVMODULE_OK;
}

/* CMDRESULT.SUCCESS
 * A command that always succeeds
 */
int CmdResultSuccess_ValkeyCommand(KVModuleCtx *ctx,
                                   KVModuleString **argv, int argc) {
  KVMODULE_NOT_USED(argv);
  KVMODULE_NOT_USED(argc);

  return KVModule_ReplyWithSimpleString(ctx, "OK");
}

/* CMDRESULT.FAIL
 * A command that always fails
 */
int CmdResultFail_ValkeyCommand(KVModuleCtx *ctx, KVModuleString **argv,
                                int argc) {
  KVMODULE_NOT_USED(argv);
  KVMODULE_NOT_USED(argc);

  return KVModule_ReplyWithError(ctx, "ERR intentional failure");
}

/* CMDRESULT.RMCALL <command> [args...]
 * Test calling a command via RM_Call - allows testing is_module_client
 * detection
 */
int CmdResultRMCall_ValkeyCommand(KVModuleCtx *ctx,
                                  KVModuleString **argv, int argc) {
  if (argc < 2) {
    return KVModule_WrongArity(ctx);
  }

  /* Call the command via RM_Call */
  KVModuleCallReply *reply = KVModule_Call(
      ctx, KVModule_StringPtrLen(argv[1], NULL), "v", argv + 2, argc - 2);

  if (!reply) {
    return KVModule_ReplyWithError(ctx, "ERR call failed");
  }

  /* Forward the reply */
  KVModule_ReplyWithCallReply(ctx, reply);
  KVModule_FreeCallReply(reply);

  return KVMODULE_OK;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv,
                        int argc) {
  KVMODULE_NOT_USED(argv);
  KVMODULE_NOT_USED(argc);

  ResetState();

  if (KVModule_Init(ctx, "commandresult", 1, KVMODULE_APIVER_1) ==
      KVMODULE_ERR) {
    return KVMODULE_ERR;
  }

  if (KVModule_CreateCommand(ctx, "cmdresult.register",
                                 CmdResultRegister_ValkeyCommand, "admin", 0, 0,
                                 0) == KVMODULE_ERR) {
    return KVMODULE_ERR;
  }

  if (KVModule_CreateCommand(ctx, "cmdresult.unsubscribe",
                                 CmdResultUnsubscribe_ValkeyCommand, "admin", 0,
                                 0, 0) == KVMODULE_ERR) {
    return KVMODULE_ERR;
  }

  if (KVModule_CreateCommand(ctx, "cmdresult.stats",
                                 CmdResultStats_ValkeyCommand, "readonly", 0, 0,
                                 0) == KVMODULE_ERR) {
    return KVMODULE_ERR;
  }

  if (KVModule_CreateCommand(ctx, "cmdresult.reset",
                                 CmdResultReset_ValkeyCommand, "admin", 0, 0,
                                 0) == KVMODULE_ERR) {
    return KVMODULE_ERR;
  }

  if (KVModule_CreateCommand(ctx, "cmdresult.getlog",
                                 CmdResultGetLog_ValkeyCommand, "readonly", 0,
                                 0, 0) == KVMODULE_ERR) {
    return KVMODULE_ERR;
  }

  if (KVModule_CreateCommand(ctx, "cmdresult.success",
                                 CmdResultSuccess_ValkeyCommand, "readonly", 0,
                                 0, 0) == KVMODULE_ERR) {
    return KVMODULE_ERR;
  }

  if (KVModule_CreateCommand(ctx, "cmdresult.fail",
                                 CmdResultFail_ValkeyCommand, "readonly", 0, 0,
                                 0) == KVMODULE_ERR) {
    return KVMODULE_ERR;
  }

  if (KVModule_CreateCommand(ctx, "cmdresult.rmcall",
                                 CmdResultRMCall_ValkeyCommand, "readonly", 0,
                                 0, 0) == KVMODULE_ERR) {
    return KVMODULE_ERR;
  }

  return KVMODULE_OK;
}
