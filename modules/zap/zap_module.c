/*
 * zap_module.c — ZAP binary protocol module for hanzo/kv.
 *
 * Adds native ZAP support to hanzo/kv. Listens on port 9653 for ZAP
 * binary connections and translates to KV commands.
 *
 * Load with: MODULE LOAD /path/to/zap.so [PORT 9653]
 */
#include "kvmodule.h"
#include "zap_protocol.h"

#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

static int zap_port = 9653;
static volatile int zap_running = 0;
static pthread_t zap_thread;
static int zap_server_fd = -1;
static KVModuleCtx *module_ctx = NULL;

/*
 * Extract a string value for a given key from a JSON object.
 * Handles simple JSON like {"key":"value","other":"thing"}.
 * Returns a malloc'd string or NULL if not found. Caller must free().
 */
static char *json_extract_string(const char *json, const char *key) {
    char search[256];
    const char *p, *start, *end;
    size_t len;
    char *result;

    snprintf(search, sizeof(search), "\"%s\"", key);
    p = strstr(json, search);
    if (!p) return NULL;

    p += strlen(search);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':')
        p++;

    if (*p != '"') return NULL;
    p++; /* skip opening quote */

    start = p;
    while (*p && !(*p == '"' && *(p - 1) != '\\'))
        p++;
    if (*p != '"') return NULL;
    end = p;

    len = end - start;
    result = malloc(len + 1);
    if (!result) return NULL;
    memcpy(result, start, len);
    result[len] = '\0';
    return result;
}

/*
 * Extract a JSON array of strings (e.g., ["arg1","arg2"]).
 * Returns count of extracted strings. Caller must free each arg and the array.
 */
static int json_extract_string_array(const char *json, const char *key,
                                      char ***out_args) {
    char search[256];
    const char *p;
    int count = 0, capacity = 8;
    char **args;

    snprintf(search, sizeof(search), "\"%s\"", key);
    p = strstr(json, search);
    if (!p) { *out_args = NULL; return 0; }

    p += strlen(search);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':')
        p++;

    if (*p != '[') { *out_args = NULL; return 0; }
    p++; /* skip [ */

    args = malloc(sizeof(char *) * capacity);
    if (!args) { *out_args = NULL; return 0; }

    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')
            p++;
        if (*p == ']') break;
        if (*p != '"') break;
        p++; /* skip opening quote */

        const char *start = p;
        while (*p && !(*p == '"' && *(p - 1) != '\\'))
            p++;
        if (*p != '"') break;

        size_t len = p - start;
        p++; /* skip closing quote */

        if (count >= capacity) {
            capacity *= 2;
            args = realloc(args, sizeof(char *) * capacity);
            if (!args) { *out_args = NULL; return 0; }
        }
        args[count] = malloc(len + 1);
        if (args[count]) {
            memcpy(args[count], start, len);
            args[count][len] = '\0';
            count++;
        }
    }

    *out_args = args;
    return count;
}

/*
 * Handle a single ZAP message — route to KV commands.
 */
static void handle_message(int client_fd, const uint8_t *data, size_t len) {
    ZapHeader hdr;
    if (!zap_parse_header(data, len, &hdr)) {
        const char *err = "{\"error\":\"invalid ZAP message\"}";
        uint32_t resp_size;
        uint8_t *resp = zap_build_response(400, (const uint8_t *)err, strlen(err), &resp_size);
        if (resp) { send(client_fd, resp, resp_size, 0); free(resp); }
        return;
    }

    uint32_t path_len, body_len;
    const char *path = zap_read_text(data, len, hdr.root_offset, ZAP_FIELD_PATH, &path_len);
    const uint8_t *body = zap_read_bytes(data, len, hdr.root_offset, ZAP_FIELD_BODY, &body_len);

    char path_buf[256] = {0};
    if (path && path_len < sizeof(path_buf))
        memcpy(path_buf, path, path_len);

    char body_buf[65536] = {0};
    if (body && body_len < sizeof(body_buf))
        memcpy(body_buf, body, body_len);

    const char *result = NULL;
    uint32_t status = 200;
    char result_buf[65536];

    KVModuleCtx *ctx = KVModule_GetThreadSafeContext(NULL);

    if (strcmp(path_buf, "/get") == 0) {
        /* GET — body is JSON: {"key":"mykey"} */
        char *key_str = json_extract_string(body_buf, "key");
        if (!key_str) {
            /* Fallback: treat raw body as key for backwards compat */
            key_str = strndup(body_buf, body_len);
        }

        if (key_str && strlen(key_str) > 0) {
            KVModuleString *key = KVModule_CreateString(ctx, key_str, strlen(key_str));
            KVModuleCallReply *reply = KVModule_Call(ctx, "GET", "s", key);
            KVModule_FreeString(ctx, key);

            if (reply && KVModule_CallReplyType(reply) == KVMODULE_REPLY_STRING) {
                size_t rlen;
                const char *rval = KVModule_CallReplyStringPtr(reply, &rlen);
                snprintf(result_buf, sizeof(result_buf),
                         "{\"value\":\"%.*s\"}", (int)rlen, rval);
                result = result_buf;
            } else {
                result = "{\"error\":\"not found\"}";
                status = 404;
            }
            if (reply) KVModule_FreeCallReply(reply);
        } else {
            result = "{\"error\":\"key required\"}";
            status = 400;
        }
        free(key_str);

    } else if (strcmp(path_buf, "/set") == 0) {
        /* SET — body is JSON: {"key":"mykey", "value":"myval"} */
        char *key_str = json_extract_string(body_buf, "key");
        char *val_str = json_extract_string(body_buf, "value");

        if (key_str && val_str) {
            KVModuleString *key = KVModule_CreateString(ctx, key_str, strlen(key_str));
            KVModuleString *val = KVModule_CreateString(ctx, val_str, strlen(val_str));
            KVModuleCallReply *reply = KVModule_Call(ctx, "SET", "ss", key, val);
            KVModule_FreeString(ctx, key);
            KVModule_FreeString(ctx, val);
            if (reply) KVModule_FreeCallReply(reply);
            result = "{\"status\":\"ok\"}";
        } else {
            result = "{\"error\":\"key and value required in JSON body\"}";
            status = 400;
        }
        free(key_str);
        free(val_str);

    } else if (strcmp(path_buf, "/del") == 0) {
        /* DEL — body is JSON: {"key":"mykey"} */
        char *key_str = json_extract_string(body_buf, "key");
        if (!key_str) {
            key_str = strndup(body_buf, body_len);
        }

        if (key_str && strlen(key_str) > 0) {
            KVModuleString *key = KVModule_CreateString(ctx, key_str, strlen(key_str));
            KVModuleCallReply *reply = KVModule_Call(ctx, "DEL", "s", key);
            KVModule_FreeString(ctx, key);

            long long deleted = 0;
            if (reply && KVModule_CallReplyType(reply) == KVMODULE_REPLY_INTEGER)
                deleted = KVModule_CallReplyInteger(reply);
            if (reply) KVModule_FreeCallReply(reply);

            snprintf(result_buf, sizeof(result_buf),
                     "{\"status\":\"ok\",\"deleted\":%lld}", deleted);
            result = result_buf;
        } else {
            result = "{\"error\":\"key required\"}";
            status = 400;
        }
        free(key_str);

    } else if (strcmp(path_buf, "/cmd") == 0) {
        /* Generic command — body is JSON: {"cmd":"COMMAND","args":["arg1","arg2"]} */
        char *cmd_str = json_extract_string(body_buf, "cmd");

        if (cmd_str && strlen(cmd_str) > 0) {
            char **args = NULL;
            int argc = json_extract_string_array(body_buf, "args", &args);

            if (argc == 0) {
                /* No args — call command with no arguments */
                KVModuleCallReply *reply = KVModule_Call(ctx, cmd_str, "");
                if (reply) {
                    int rtype = KVModule_CallReplyType(reply);
                    if (rtype == KVMODULE_REPLY_STRING) {
                        size_t rlen;
                        const char *rval = KVModule_CallReplyStringPtr(reply, &rlen);
                        snprintf(result_buf, sizeof(result_buf),
                                 "{\"result\":\"%.*s\"}", (int)rlen, rval);
                        result = result_buf;
                    } else if (rtype == KVMODULE_REPLY_INTEGER) {
                        long long ival = KVModule_CallReplyInteger(reply);
                        snprintf(result_buf, sizeof(result_buf),
                                 "{\"result\":%lld}", ival);
                        result = result_buf;
                    } else if (rtype == KVMODULE_REPLY_ERROR) {
                        size_t rlen;
                        const char *rval = KVModule_CallReplyStringPtr(reply, &rlen);
                        snprintf(result_buf, sizeof(result_buf),
                                 "{\"error\":\"%.*s\"}", (int)rlen, rval);
                        result = result_buf;
                        status = 400;
                    } else {
                        result = "{\"result\":\"ok\"}";
                    }
                    KVModule_FreeCallReply(reply);
                } else {
                    result = "{\"error\":\"command failed\"}";
                    status = 500;
                }
            } else {
                /* Build KVModuleString array for args */
                KVModuleString **rm_args = malloc(sizeof(KVModuleString *) * argc);
                if (rm_args) {
                    /* Build format string: "sss..." one 's' per arg */
                    char *fmt = malloc(argc + 1);
                    int i;
                    for (i = 0; i < argc; i++) {
                        rm_args[i] = KVModule_CreateString(ctx, args[i], strlen(args[i]));
                        fmt[i] = 's';
                    }
                    fmt[argc] = '\0';

                    KVModuleCallReply *reply = KVModule_Call(ctx, cmd_str, fmt,
                        argc >= 1 ? rm_args[0] : NULL,
                        argc >= 2 ? rm_args[1] : NULL,
                        argc >= 3 ? rm_args[2] : NULL,
                        argc >= 4 ? rm_args[3] : NULL,
                        argc >= 5 ? rm_args[4] : NULL,
                        argc >= 6 ? rm_args[5] : NULL,
                        argc >= 7 ? rm_args[6] : NULL,
                        argc >= 8 ? rm_args[7] : NULL);

                    for (i = 0; i < argc; i++)
                        KVModule_FreeString(ctx, rm_args[i]);
                    free(rm_args);
                    free(fmt);

                    if (reply) {
                        int rtype = KVModule_CallReplyType(reply);
                        if (rtype == KVMODULE_REPLY_STRING) {
                            size_t rlen;
                            const char *rval = KVModule_CallReplyStringPtr(reply, &rlen);
                            snprintf(result_buf, sizeof(result_buf),
                                     "{\"result\":\"%.*s\"}", (int)rlen, rval);
                            result = result_buf;
                        } else if (rtype == KVMODULE_REPLY_INTEGER) {
                            long long ival = KVModule_CallReplyInteger(reply);
                            snprintf(result_buf, sizeof(result_buf),
                                     "{\"result\":%lld}", ival);
                            result = result_buf;
                        } else if (rtype == KVMODULE_REPLY_ERROR) {
                            size_t rlen;
                            const char *rval = KVModule_CallReplyStringPtr(reply, &rlen);
                            snprintf(result_buf, sizeof(result_buf),
                                     "{\"error\":\"%.*s\"}", (int)rlen, rval);
                            result = result_buf;
                            status = 400;
                        } else {
                            result = "{\"result\":\"ok\"}";
                        }
                        KVModule_FreeCallReply(reply);
                    } else {
                        result = "{\"error\":\"command failed\"}";
                        status = 500;
                    }
                }
            }

            /* Clean up args */
            if (args) {
                int i;
                for (i = 0; i < argc; i++)
                    free(args[i]);
                free(args);
            }
        } else {
            result = "{\"error\":\"cmd required in JSON body\"}";
            status = 400;
        }
        free(cmd_str);

    } else {
        result = "{\"error\":\"unknown path\"}";
        status = 400;
    }

    KVModule_FreeThreadSafeContext(ctx);

    /* Send ZAP response */
    uint32_t resp_size;
    uint8_t *resp = zap_build_response(status, (const uint8_t *)result,
                                        strlen(result), &resp_size);
    if (resp) {
        send(client_fd, resp, resp_size, 0);
        free(resp);
    }
}

/*
 * Listener thread — accepts ZAP connections.
 */
static void *zap_listener_thread(void *arg) {
    (void)arg;
    struct sockaddr_in addr;

    zap_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (zap_server_fd < 0) return NULL;

    int opt = 1;
    setsockopt(zap_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(zap_port);

    if (bind(zap_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(zap_server_fd);
        zap_server_fd = -1;
        return NULL;
    }

    listen(zap_server_fd, 32);

    while (zap_running) {
        int client_fd = accept(zap_server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            break;
        }

        uint8_t buf[65536];
        ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
        if (n > 0)
            handle_message(client_fd, buf, (size_t)n);

        close(client_fd);
    }

    return NULL;
}

/*
 * Module load — start ZAP listener.
 */
int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (KVModule_Init(ctx, "zap", 1, KVMODULE_APIVER_1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    module_ctx = ctx;

    /* Parse optional PORT argument */
    if (argc >= 2) {
        const char *arg0 = KVModule_StringPtrLen(argv[0], NULL);
        if (strcasecmp(arg0, "PORT") == 0) {
            long long port;
            if (KVModule_StringToLongLong(argv[1], &port) == KVMODULE_OK)
                zap_port = (int)port;
        }
    }

    zap_running = 1;
    if (pthread_create(&zap_thread, NULL, zap_listener_thread, NULL) != 0) {
        KVModule_Log(ctx, "warning", "zap: failed to start listener thread");
        return KVMODULE_ERR;
    }

    KVModule_Log(ctx, "notice", "zap: listening on port %d", zap_port);
    return KVMODULE_OK;
}

/*
 * Module unload — stop ZAP listener.
 */
int KVModule_OnUnload(KVModuleCtx *ctx) {
    zap_running = 0;
    if (zap_server_fd >= 0) {
        shutdown(zap_server_fd, SHUT_RDWR);
        close(zap_server_fd);
    }
    pthread_join(zap_thread, NULL);
    KVModule_Log(ctx, "notice", "zap: listener stopped");
    return KVMODULE_OK;
}
