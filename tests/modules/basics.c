/* Module designed to test the modules subsystem.
 *
 * -----------------------------------------------------------------------------
 *
 * Copyright (c) 2016, Redis Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "kvmodule.h"
#include <string.h>
#include <stdlib.h>

/* --------------------------------- Helpers -------------------------------- */

/* Return true if the reply and the C null term string matches. */
int TestMatchReply(KVModuleCallReply *reply, char *str) {
    KVModuleString *mystr;
    mystr = KVModule_CreateStringFromCallReply(reply);
    if (!mystr) return 0;
    const char *ptr = KVModule_StringPtrLen(mystr,NULL);
    return strcmp(ptr,str) == 0;
}

/* ------------------------------- Test units ------------------------------- */

/* TEST.CALL -- Test Call() API. */
int TestCall(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModule_AutoMemory(ctx);
    KVModuleCallReply *reply;

    KVModule_Call(ctx,"DEL","c","mylist");
    KVModuleString *mystr = KVModule_CreateString(ctx,"foo",3);
    KVModule_Call(ctx,"RPUSH","csl","mylist",mystr,(long long)1234);
    reply = KVModule_Call(ctx,"LRANGE","ccc","mylist","0","-1");
    long long items = KVModule_CallReplyLength(reply);
    if (items != 2) goto fail;

    KVModuleCallReply *item0, *item1;

    item0 = KVModule_CallReplyArrayElement(reply,0);
    item1 = KVModule_CallReplyArrayElement(reply,1);
    if (!TestMatchReply(item0,"foo")) goto fail;
    if (!TestMatchReply(item1,"1234")) goto fail;

    KVModule_ReplyWithSimpleString(ctx,"OK");
    return KVMODULE_OK;

fail:
    KVModule_ReplyWithSimpleString(ctx,"ERR");
    return KVMODULE_OK;
}

int TestCallResp3Attribute(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModule_AutoMemory(ctx);
    KVModuleCallReply *reply;

    reply = KVModule_Call(ctx,"DEBUG","3cc" ,"PROTOCOL", "attrib"); /* 3 stands for resp 3 reply */
    if (KVModule_CallReplyType(reply) != KVMODULE_REPLY_STRING) goto fail;

    /* make sure we can not reply to resp2 client with resp3 (it might be a string but it contains attribute) */
    if (KVModule_ReplyWithCallReply(ctx, reply) != KVMODULE_ERR) goto fail;

    if (!TestMatchReply(reply,"Some real reply following the attribute")) goto fail;

    reply = KVModule_CallReplyAttribute(reply);
    if (!reply || KVModule_CallReplyType(reply) != KVMODULE_REPLY_ATTRIBUTE) goto fail;
    /* make sure we can not reply to resp2 client with resp3 attribute */
    if (KVModule_ReplyWithCallReply(ctx, reply) != KVMODULE_ERR) goto fail;
    if (KVModule_CallReplyLength(reply) != 1) goto fail;

    KVModuleCallReply *key, *val;
    if (KVModule_CallReplyAttributeElement(reply,0,&key,&val) != KVMODULE_OK) goto fail;
    if (!TestMatchReply(key,"key-popularity")) goto fail;
    if (KVModule_CallReplyType(val) != KVMODULE_REPLY_ARRAY) goto fail;
    if (KVModule_CallReplyLength(val) != 2) goto fail;
    if (!TestMatchReply(KVModule_CallReplyArrayElement(val, 0),"key:123")) goto fail;
    if (!TestMatchReply(KVModule_CallReplyArrayElement(val, 1),"90")) goto fail;

    KVModule_ReplyWithSimpleString(ctx,"OK");
    return KVMODULE_OK;

fail:
    KVModule_ReplyWithSimpleString(ctx,"ERR");
    return KVMODULE_OK;
}

int TestGetResp(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    int flags = KVModule_GetContextFlags(ctx);

    if (flags & KVMODULE_CTX_FLAGS_RESP3) {
        KVModule_ReplyWithLongLong(ctx, 3);
    } else {
        KVModule_ReplyWithLongLong(ctx, 2);
    }

    return KVMODULE_OK;
}

int TestCallRespAutoMode(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModule_AutoMemory(ctx);
    KVModuleCallReply *reply;

    KVModule_Call(ctx,"DEL","c","myhash");
    KVModule_Call(ctx,"HSET","ccccc","myhash", "f1", "v1", "f2", "v2");
    /* 0 stands for auto mode, we will get the reply in the same format as the client */
    reply = KVModule_Call(ctx,"HGETALL","0c" ,"myhash");
    KVModule_ReplyWithCallReply(ctx, reply);
    return KVMODULE_OK;
}

int TestCallResp3Map(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModule_AutoMemory(ctx);
    KVModuleCallReply *reply;

    KVModule_Call(ctx,"DEL","c","myhash");
    KVModule_Call(ctx,"HSET","ccccc","myhash", "f1", "v1", "f2", "v2");
    reply = KVModule_Call(ctx,"HGETALL","3c" ,"myhash"); /* 3 stands for resp 3 reply */
    if (KVModule_CallReplyType(reply) != KVMODULE_REPLY_MAP) goto fail;

    /* make sure we can not reply to resp2 client with resp3 map */
    if (KVModule_ReplyWithCallReply(ctx, reply) != KVMODULE_ERR) goto fail;

    long long items = KVModule_CallReplyLength(reply);
    if (items != 2) goto fail;

    KVModuleCallReply *key0, *key1;
    KVModuleCallReply *val0, *val1;
    if (KVModule_CallReplyMapElement(reply,0,&key0,&val0) != KVMODULE_OK) goto fail;
    if (KVModule_CallReplyMapElement(reply,1,&key1,&val1) != KVMODULE_OK) goto fail;
    if (!TestMatchReply(key0,"f1")) goto fail;
    if (!TestMatchReply(key1,"f2")) goto fail;
    if (!TestMatchReply(val0,"v1")) goto fail;
    if (!TestMatchReply(val1,"v2")) goto fail;

    KVModule_ReplyWithSimpleString(ctx,"OK");
    return KVMODULE_OK;

fail:
    KVModule_ReplyWithSimpleString(ctx,"ERR");
    return KVMODULE_OK;
}

int TestCallResp3Bool(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModule_AutoMemory(ctx);
    KVModuleCallReply *reply;

    reply = KVModule_Call(ctx,"DEBUG","3cc" ,"PROTOCOL", "true"); /* 3 stands for resp 3 reply */
    if (KVModule_CallReplyType(reply) != KVMODULE_REPLY_BOOL) goto fail;
    /* make sure we can not reply to resp2 client with resp3 bool */
    if (KVModule_ReplyWithCallReply(ctx, reply) != KVMODULE_ERR) goto fail;

    if (!KVModule_CallReplyBool(reply)) goto fail;
    reply = KVModule_Call(ctx,"DEBUG","3cc" ,"PROTOCOL", "false"); /* 3 stands for resp 3 reply */
    if (KVModule_CallReplyType(reply) != KVMODULE_REPLY_BOOL) goto fail;
    if (KVModule_CallReplyBool(reply)) goto fail;

    KVModule_ReplyWithSimpleString(ctx,"OK");
    return KVMODULE_OK;

fail:
    KVModule_ReplyWithSimpleString(ctx,"ERR");
    return KVMODULE_OK;
}

int TestCallResp3Null(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModule_AutoMemory(ctx);
    KVModuleCallReply *reply;

    reply = KVModule_Call(ctx,"DEBUG","3cc" ,"PROTOCOL", "null"); /* 3 stands for resp 3 reply */
    if (KVModule_CallReplyType(reply) != KVMODULE_REPLY_NULL) goto fail;

    /* make sure we can not reply to resp2 client with resp3 null */
    if (KVModule_ReplyWithCallReply(ctx, reply) != KVMODULE_ERR) goto fail;

    KVModule_ReplyWithSimpleString(ctx,"OK");
    return KVMODULE_OK;

fail:
    KVModule_ReplyWithSimpleString(ctx,"ERR");
    return KVMODULE_OK;
}

int TestCallReplyWithNestedReply(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModule_AutoMemory(ctx);
    KVModuleCallReply *reply;

    KVModule_Call(ctx,"DEL","c","mylist");
    KVModule_Call(ctx,"RPUSH","ccl","mylist","test",(long long)1234);
    reply = KVModule_Call(ctx,"LRANGE","ccc","mylist","0","-1");
    if (KVModule_CallReplyType(reply) != KVMODULE_REPLY_ARRAY) goto fail;
    if (KVModule_CallReplyLength(reply) < 1) goto fail;
    KVModuleCallReply *nestedReply = KVModule_CallReplyArrayElement(reply, 0);

    KVModule_ReplyWithCallReply(ctx,nestedReply);
    return KVMODULE_OK;

fail:
    KVModule_ReplyWithSimpleString(ctx,"ERR");
    return KVMODULE_OK;
}

int TestCallReplyWithArrayReply(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModule_AutoMemory(ctx);
    KVModuleCallReply *reply;

    KVModule_Call(ctx,"DEL","c","mylist");
    KVModule_Call(ctx,"RPUSH","ccl","mylist","test",(long long)1234);
    reply = KVModule_Call(ctx,"LRANGE","ccc","mylist","0","-1");
    if (KVModule_CallReplyType(reply) != KVMODULE_REPLY_ARRAY) goto fail;

    KVModule_ReplyWithCallReply(ctx,reply);
    return KVMODULE_OK;

fail:
    KVModule_ReplyWithSimpleString(ctx,"ERR");
    return KVMODULE_OK;
}

int TestCallResp3Double(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModule_AutoMemory(ctx);
    KVModuleCallReply *reply;

    reply = KVModule_Call(ctx,"DEBUG","3cc" ,"PROTOCOL", "double"); /* 3 stands for resp 3 reply */
    if (KVModule_CallReplyType(reply) != KVMODULE_REPLY_DOUBLE) goto fail;

    /* make sure we can not reply to resp2 client with resp3 double*/
    if (KVModule_ReplyWithCallReply(ctx, reply) != KVMODULE_ERR) goto fail;

    double d = KVModule_CallReplyDouble(reply);
    /* we compare strings, since comparing doubles directly can fail in various architectures, e.g. 32bit */
    char got[30], expected[30];
    snprintf(got, sizeof(got), "%.17g", d);
    snprintf(expected, sizeof(expected), "%.17g", 3.141);
    if (strcmp(got, expected) != 0) goto fail;
    KVModule_ReplyWithSimpleString(ctx,"OK");
    return KVMODULE_OK;

fail:
    KVModule_ReplyWithSimpleString(ctx,"ERR");
    return KVMODULE_OK;
}

int TestCallResp3BigNumber(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModule_AutoMemory(ctx);
    KVModuleCallReply *reply;

    reply = KVModule_Call(ctx,"DEBUG","3cc" ,"PROTOCOL", "bignum"); /* 3 stands for resp 3 reply */
    if (KVModule_CallReplyType(reply) != KVMODULE_REPLY_BIG_NUMBER) goto fail;

    /* make sure we can not reply to resp2 client with resp3 big number */
    if (KVModule_ReplyWithCallReply(ctx, reply) != KVMODULE_ERR) goto fail;

    size_t len;
    const char* big_num = KVModule_CallReplyBigNumber(reply, &len);
    KVModule_ReplyWithStringBuffer(ctx,big_num,len);
    return KVMODULE_OK;

fail:
    KVModule_ReplyWithSimpleString(ctx,"ERR");
    return KVMODULE_OK;
}

int TestCallResp3Verbatim(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModule_AutoMemory(ctx);
    KVModuleCallReply *reply;

    reply = KVModule_Call(ctx,"DEBUG","3cc" ,"PROTOCOL", "verbatim"); /* 3 stands for resp 3 reply */
    if (KVModule_CallReplyType(reply) != KVMODULE_REPLY_VERBATIM_STRING) goto fail;

    /* make sure we can not reply to resp2 client with resp3 verbatim string */
    if (KVModule_ReplyWithCallReply(ctx, reply) != KVMODULE_ERR) goto fail;

    const char* format;
    size_t len;
    const char* str = KVModule_CallReplyVerbatim(reply, &len, &format);
    KVModuleString *s = KVModule_CreateStringPrintf(ctx, "%.*s:%.*s", 3, format, (int)len, str);
    KVModule_ReplyWithString(ctx,s);
    return KVMODULE_OK;

fail:
    KVModule_ReplyWithSimpleString(ctx,"ERR");
    return KVMODULE_OK;
}

int TestCallResp3Set(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModule_AutoMemory(ctx);
    KVModuleCallReply *reply;

    KVModule_Call(ctx,"DEL","c","myset");
    KVModule_Call(ctx,"sadd","ccc","myset", "v1", "v2");
    reply = KVModule_Call(ctx,"smembers","3c" ,"myset"); // N stands for resp 3 reply
    if (KVModule_CallReplyType(reply) != KVMODULE_REPLY_SET) goto fail;

    /* make sure we can not reply to resp2 client with resp3 set */
    if (KVModule_ReplyWithCallReply(ctx, reply) != KVMODULE_ERR) goto fail;

    long long items = KVModule_CallReplyLength(reply);
    if (items != 2) goto fail;

    KVModuleCallReply *val0, *val1;

    val0 = KVModule_CallReplySetElement(reply,0);
    val1 = KVModule_CallReplySetElement(reply,1);

    /*
     * The order of elements on sets are not promised so we just
     * veridy that the reply matches one of the elements.
     */
    if (!TestMatchReply(val0,"v1") && !TestMatchReply(val0,"v2")) goto fail;
    if (!TestMatchReply(val1,"v1") && !TestMatchReply(val1,"v2")) goto fail;

    KVModule_ReplyWithSimpleString(ctx,"OK");
    return KVMODULE_OK;

fail:
    KVModule_ReplyWithSimpleString(ctx,"ERR");
    return KVMODULE_OK;
}

/* TEST.STRING.APPEND -- Test appending to an existing string object. */
int TestStringAppend(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModuleString *s = KVModule_CreateString(ctx,"foo",3);
    KVModule_StringAppendBuffer(ctx,s,"bar",3);
    KVModule_ReplyWithString(ctx,s);
    KVModule_FreeString(ctx,s);
    return KVMODULE_OK;
}

/* TEST.STRING.APPEND.AM -- Test append with retain when auto memory is on. */
int TestStringAppendAM(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModule_AutoMemory(ctx);
    KVModuleString *s = KVModule_CreateString(ctx,"foo",3);
    KVModule_RetainString(ctx,s);
    KVModule_TrimStringAllocation(s);    /* Mostly NOP, but exercises the API function */
    KVModule_StringAppendBuffer(ctx,s,"bar",3);
    KVModule_ReplyWithString(ctx,s);
    KVModule_FreeString(ctx,s);
    return KVMODULE_OK;
}

/* TEST.STRING.TRIM -- Test we trim a string with free space. */
int TestTrimString(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    KVModuleString *s = KVModule_CreateString(ctx,"foo",3);
    char *tmp = KVModule_Alloc(1024);
    KVModule_StringAppendBuffer(ctx,s,tmp,1024);
    size_t string_len = KVModule_MallocSizeString(s);
    KVModule_TrimStringAllocation(s);
    size_t len_after_trim = KVModule_MallocSizeString(s);

    /* Determine if using jemalloc memory allocator. */
    KVModuleServerInfoData *info = KVModule_GetServerInfo(ctx, "memory");
    const char *field = KVModule_ServerInfoGetFieldC(info, "mem_allocator");
    int use_jemalloc = !strncmp(field, "jemalloc", 8);

    /* Jemalloc will reallocate `s` from 2k to 1k after KVModule_TrimStringAllocation(),
     * but non-jemalloc memory allocators may keep the old size. */
    if ((use_jemalloc && len_after_trim < string_len) ||
        (!use_jemalloc && len_after_trim <= string_len))
    {
        KVModule_ReplyWithSimpleString(ctx, "OK");
    } else {
        KVModule_ReplyWithError(ctx, "String was not trimmed as expected.");
    }
    KVModule_FreeServerInfo(ctx, info);
    KVModule_Free(tmp);
    KVModule_FreeString(ctx,s);
    return KVMODULE_OK;
}

/* TEST.STRING.PRINTF -- Test string formatting. */
int TestStringPrintf(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVModule_AutoMemory(ctx);
    if (argc < 3) {
        return KVModule_WrongArity(ctx);
    }
    KVModuleString *s = KVModule_CreateStringPrintf(ctx,
        "Got %d args. argv[1]: %s, argv[2]: %s",
        argc,
        KVModule_StringPtrLen(argv[1], NULL),
        KVModule_StringPtrLen(argv[2], NULL)
    );

    KVModule_ReplyWithString(ctx,s);

    return KVMODULE_OK;
}

int failTest(KVModuleCtx *ctx, const char *msg) {
    KVModule_ReplyWithError(ctx, msg);
    return KVMODULE_ERR;
}

int TestUnlink(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVModule_AutoMemory(ctx);
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModuleKey *k = KVModule_OpenKey(ctx, KVModule_CreateStringPrintf(ctx, "unlinked"), KVMODULE_WRITE | KVMODULE_READ);
    if (!k) return failTest(ctx, "Could not create key");

    if (KVMODULE_ERR == KVModule_StringSet(k, KVModule_CreateStringPrintf(ctx, "Foobar"))) {
        return failTest(ctx, "Could not set string value");
    }

    KVModuleCallReply *rep = KVModule_Call(ctx, "EXISTS", "c", "unlinked");
    if (!rep || KVModule_CallReplyInteger(rep) != 1) {
        return failTest(ctx, "Key does not exist before unlink");
    }

    if (KVMODULE_ERR == KVModule_UnlinkKey(k)) {
        return failTest(ctx, "Could not unlink key");
    }

    rep = KVModule_Call(ctx, "EXISTS", "c", "unlinked");
    if (!rep || KVModule_CallReplyInteger(rep) != 0) {
        return failTest(ctx, "Could not verify key to be unlinked");
    }
    return KVModule_ReplyWithSimpleString(ctx, "OK");
}

int TestNestedCallReplyArrayElement(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVModule_AutoMemory(ctx);
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModuleString *expect_key = KVModule_CreateString(ctx, "mykey", strlen("mykey"));
    KVModule_SelectDb(ctx, 1);
    KVModule_Call(ctx, "LPUSH", "sc", expect_key, "myvalue");

    KVModuleCallReply *scan_reply = KVModule_Call(ctx, "SCAN", "l", (long long)0);
    KVModule_Assert(scan_reply != NULL && KVModule_CallReplyType(scan_reply) == KVMODULE_REPLY_ARRAY);
    KVModule_Assert(KVModule_CallReplyLength(scan_reply) == 2);

    long long scan_cursor;
    KVModuleCallReply *cursor_reply = KVModule_CallReplyArrayElement(scan_reply, 0);
    KVModule_Assert(KVModule_CallReplyType(cursor_reply) == KVMODULE_REPLY_STRING);
    KVModule_Assert(KVModule_StringToLongLong(KVModule_CreateStringFromCallReply(cursor_reply), &scan_cursor) == KVMODULE_OK);
    KVModule_Assert(scan_cursor == 0);

    KVModuleCallReply *keys_reply = KVModule_CallReplyArrayElement(scan_reply, 1);
    KVModule_Assert(KVModule_CallReplyType(keys_reply) == KVMODULE_REPLY_ARRAY);
    KVModule_Assert( KVModule_CallReplyLength(keys_reply) == 1);
 
    KVModuleCallReply *key_reply = KVModule_CallReplyArrayElement(keys_reply, 0);
    KVModule_Assert(KVModule_CallReplyType(key_reply) == KVMODULE_REPLY_STRING);
    KVModuleString *key = KVModule_CreateStringFromCallReply(key_reply);
    KVModule_Assert(KVModule_StringCompare(key, expect_key) == 0);

    KVModule_ReplyWithSimpleString(ctx, "OK");
    return KVMODULE_OK;
}

/* TEST.STRING.TRUNCATE -- Test truncating an existing string object. */
int TestStringTruncate(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVModule_AutoMemory(ctx);
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModule_Call(ctx, "SET", "cc", "foo", "abcde");
    KVModuleKey *k = KVModule_OpenKey(ctx, KVModule_CreateStringPrintf(ctx, "foo"), KVMODULE_READ | KVMODULE_WRITE);
    if (!k) return failTest(ctx, "Could not create key");

    size_t len = 0;
    char* s;

    /* expand from 5 to 8 and check null pad */
    if (KVMODULE_ERR == KVModule_StringTruncate(k, 8)) {
        return failTest(ctx, "Could not truncate string value (8)");
    }
    s = KVModule_StringDMA(k, &len, KVMODULE_READ);
    if (!s) {
        return failTest(ctx, "Failed to read truncated string (8)");
    } else if (len != 8) {
        return failTest(ctx, "Failed to expand string value (8)");
    } else if (0 != strncmp(s, "abcde\0\0\0", 8)) {
        return failTest(ctx, "Failed to null pad string value (8)");
    }

    /* shrink from 8 to 4 */
    if (KVMODULE_ERR == KVModule_StringTruncate(k, 4)) {
        return failTest(ctx, "Could not truncate string value (4)");
    }
    s = KVModule_StringDMA(k, &len, KVMODULE_READ);
    if (!s) {
        return failTest(ctx, "Failed to read truncated string (4)");
    } else if (len != 4) {
        return failTest(ctx, "Failed to shrink string value (4)");
    } else if (0 != strncmp(s, "abcd", 4)) {
        return failTest(ctx, "Failed to truncate string value (4)");
    }

    /* shrink to 0 */
    if (KVMODULE_ERR == KVModule_StringTruncate(k, 0)) {
        return failTest(ctx, "Could not truncate string value (0)");
    }
    s = KVModule_StringDMA(k, &len, KVMODULE_READ);
    if (!s) {
        return failTest(ctx, "Failed to read truncated string (0)");
    } else if (len != 0) {
        return failTest(ctx, "Failed to shrink string value to (0)");
    }

    return KVModule_ReplyWithSimpleString(ctx, "OK");
}

int NotifyCallback(KVModuleCtx *ctx, int type, const char *event,
                   KVModuleString *key) {
  KVModule_AutoMemory(ctx);
  /* Increment a counter on the notifications: for each key notified we
   * increment a counter */
  KVModule_Log(ctx, "notice", "Got event type %d, event %s, key %s", type,
                  event, KVModule_StringPtrLen(key, NULL));

  KVModule_Call(ctx, "HINCRBY", "csc", "notifications", key, "1");
  return KVMODULE_OK;
}

/* TEST.NOTIFICATIONS -- Test Keyspace Notifications. */
int TestNotifications(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVModule_AutoMemory(ctx);
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

#define FAIL(msg, ...)                                                                       \
    {                                                                                        \
        KVModule_Log(ctx, "warning", "Failed NOTIFY Test. Reason: " #msg, ##__VA_ARGS__); \
        goto err;                                                                            \
    }
    KVModule_Call(ctx, "FLUSHDB", "");

    KVModule_Call(ctx, "SET", "cc", "foo", "bar");
    KVModule_Call(ctx, "SET", "cc", "foo", "baz");
    KVModule_Call(ctx, "SADD", "cc", "bar", "x");
    KVModule_Call(ctx, "SADD", "cc", "bar", "y");

    KVModule_Call(ctx, "HSET", "ccc", "baz", "x", "y");
    /* LPUSH should be ignored and not increment any counters */
    KVModule_Call(ctx, "LPUSH", "cc", "l", "y");
    KVModule_Call(ctx, "LPUSH", "cc", "l", "y");

    /* Miss some keys intentionally so we will get a "keymiss" notification. */
    KVModule_Call(ctx, "GET", "c", "nosuchkey");
    KVModule_Call(ctx, "SMEMBERS", "c", "nosuchkey");

    size_t sz;
    const char *rep;
    KVModuleCallReply *r = KVModule_Call(ctx, "HGET", "cc", "notifications", "foo");
    if (r == NULL || KVModule_CallReplyType(r) != KVMODULE_REPLY_STRING) {
        FAIL("Wrong or no reply for foo");
    } else {
        rep = KVModule_CallReplyStringPtr(r, &sz);
        if (sz != 1 || *rep != '2') {
            FAIL("Got reply '%s'. expected '2'", KVModule_CallReplyStringPtr(r, NULL));
        }
    }

    r = KVModule_Call(ctx, "HGET", "cc", "notifications", "bar");
    if (r == NULL || KVModule_CallReplyType(r) != KVMODULE_REPLY_STRING) {
        FAIL("Wrong or no reply for bar");
    } else {
        rep = KVModule_CallReplyStringPtr(r, &sz);
        if (sz != 1 || *rep != '2') {
            FAIL("Got reply '%s'. expected '2'", rep);
        }
    }

    r = KVModule_Call(ctx, "HGET", "cc", "notifications", "baz");
    if (r == NULL || KVModule_CallReplyType(r) != KVMODULE_REPLY_STRING) {
        FAIL("Wrong or no reply for baz");
    } else {
        rep = KVModule_CallReplyStringPtr(r, &sz);
        if (sz != 1 || *rep != '1') {
            FAIL("Got reply '%.*s'. expected '1'", (int)sz, rep);
        }
    }
    /* For l we expect nothing since we didn't subscribe to list events */
    r = KVModule_Call(ctx, "HGET", "cc", "notifications", "l");
    if (r == NULL || KVModule_CallReplyType(r) != KVMODULE_REPLY_NULL) {
        FAIL("Wrong reply for l");
    }

    r = KVModule_Call(ctx, "HGET", "cc", "notifications", "nosuchkey");
    if (r == NULL || KVModule_CallReplyType(r) != KVMODULE_REPLY_STRING) {
        FAIL("Wrong or no reply for nosuchkey");
    } else {
        rep = KVModule_CallReplyStringPtr(r, &sz);
        if (sz != 1 || *rep != '2') {
            FAIL("Got reply '%.*s'. expected '2'", (int)sz, rep);
        }
    }

    KVModule_Call(ctx, "FLUSHDB", "");

    return KVModule_ReplyWithSimpleString(ctx, "OK");
err:
    KVModule_Call(ctx, "FLUSHDB", "");

    return KVModule_ReplyWithSimpleString(ctx, "ERR");
}

/* test.latency latency_ms */
int TestLatency(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) {
        KVModule_WrongArity(ctx);
        return KVMODULE_OK;
    }

    long long latency_ms;
    if (KVModule_StringToLongLong(argv[1], &latency_ms) != KVMODULE_OK) {
        KVModule_ReplyWithError(ctx, "Invalid integer value");
        return KVMODULE_OK;
    }

    KVModule_LatencyAddSample("test", latency_ms);
    KVModule_ReplyWithSimpleString(ctx, "OK");
    return KVMODULE_OK;
}

/* TEST.CTXFLAGS -- Test GetContextFlags. */
int TestCtxFlags(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argc);
    KVMODULE_NOT_USED(argv);

    KVModule_AutoMemory(ctx);

    int ok = 1;
    const char *errString = NULL;
#undef FAIL
#define FAIL(msg)        \
    {                    \
        ok = 0;          \
        errString = msg; \
        goto end;        \
    }

    int flags = KVModule_GetContextFlags(ctx);
    if (flags == 0) {
        FAIL("Got no flags");
    }

    if (flags & KVMODULE_CTX_FLAGS_LUA) FAIL("Lua flag was set");
    if (flags & KVMODULE_CTX_FLAGS_MULTI) FAIL("Multi flag was set");

    if (flags & KVMODULE_CTX_FLAGS_AOF) FAIL("AOF Flag was set")
    /* Enable AOF to test AOF flags */
    KVModule_Call(ctx, "config", "ccc", "set", "appendonly", "yes");
    flags = KVModule_GetContextFlags(ctx);
    if (!(flags & KVMODULE_CTX_FLAGS_AOF)) FAIL("AOF Flag not set after config set");

    /* Disable RDB saving and test the flag. */
    KVModule_Call(ctx, "config", "ccc", "set", "save", "");
    flags = KVModule_GetContextFlags(ctx);
    if (flags & KVMODULE_CTX_FLAGS_RDB) FAIL("RDB Flag was set");
    /* Enable RDB to test RDB flags */
    KVModule_Call(ctx, "config", "ccc", "set", "save", "900 1");
    flags = KVModule_GetContextFlags(ctx);
    if (!(flags & KVMODULE_CTX_FLAGS_RDB)) FAIL("RDB Flag was not set after config set");

    if (!(flags & KVMODULE_CTX_FLAGS_PRIMARY)) FAIL("Master flag was not set");
    if (flags & KVMODULE_CTX_FLAGS_REPLICA) FAIL("Slave flag was set");
    if (flags & KVMODULE_CTX_FLAGS_READONLY) FAIL("Read-only flag was set");
    if (flags & KVMODULE_CTX_FLAGS_CLUSTER) FAIL("Cluster flag was set");

    /* Disable maxmemory and test the flag. (it is implicitly set in 32bit builds. */
    KVModule_Call(ctx, "config", "ccc", "set", "maxmemory", "0");
    flags = KVModule_GetContextFlags(ctx);
    if (flags & KVMODULE_CTX_FLAGS_MAXMEMORY) FAIL("Maxmemory flag was set");

    /* Enable maxmemory and test the flag. */
    KVModule_Call(ctx, "config", "ccc", "set", "maxmemory", "100000000");
    flags = KVModule_GetContextFlags(ctx);
    if (!(flags & KVMODULE_CTX_FLAGS_MAXMEMORY))
        FAIL("Maxmemory flag was not set after config set");

    if (flags & KVMODULE_CTX_FLAGS_EVICT) FAIL("Eviction flag was set");
    KVModule_Call(ctx, "config", "ccc", "set", "maxmemory-policy", "allkeys-lru");
    flags = KVModule_GetContextFlags(ctx);
    if (!(flags & KVMODULE_CTX_FLAGS_EVICT)) FAIL("Eviction flag was not set after config set");

end:
    /* Revert config changes */
    KVModule_Call(ctx, "config", "ccc", "set", "appendonly", "no");
    KVModule_Call(ctx, "config", "ccc", "set", "save", "");
    KVModule_Call(ctx, "config", "ccc", "set", "maxmemory", "0");
    KVModule_Call(ctx, "config", "ccc", "set", "maxmemory-policy", "noeviction");

    if (!ok) {
        KVModule_Log(ctx, "warning", "Failed CTXFLAGS Test. Reason: %s", errString);
        return KVModule_ReplyWithSimpleString(ctx, "ERR");
    }

    return KVModule_ReplyWithSimpleString(ctx, "OK");
}

/* ----------------------------- Test framework ----------------------------- */

/* Return 1 if the reply matches the specified string, otherwise log errors
 * in the server log and return 0. */
int TestAssertErrorReply(KVModuleCtx *ctx, KVModuleCallReply *reply, char *str, size_t len) {
    KVModuleString *mystr, *expected;
    if (KVModule_CallReplyType(reply) != KVMODULE_REPLY_ERROR) {
        return 0;
    }

    mystr = KVModule_CreateStringFromCallReply(reply);
    expected = KVModule_CreateString(ctx,str,len);
    if (KVModule_StringCompare(mystr,expected) != 0) {
        const char *mystr_ptr = KVModule_StringPtrLen(mystr,NULL);
        const char *expected_ptr = KVModule_StringPtrLen(expected,NULL);
        KVModule_Log(ctx,"warning",
            "Unexpected Error reply reply '%s' (instead of '%s')",
            mystr_ptr, expected_ptr);
        return 0;
    }
    return 1;
}

int TestAssertStringReply(KVModuleCtx *ctx, KVModuleCallReply *reply, char *str, size_t len) {
    KVModuleString *mystr, *expected;

    if (KVModule_CallReplyType(reply) == KVMODULE_REPLY_ERROR) {
        KVModule_Log(ctx,"warning","Test error reply: %s",
            KVModule_CallReplyStringPtr(reply, NULL));
        return 0;
    } else if (KVModule_CallReplyType(reply) != KVMODULE_REPLY_STRING) {
        KVModule_Log(ctx,"warning","Unexpected reply type %d",
            KVModule_CallReplyType(reply));
        return 0;
    }
    mystr = KVModule_CreateStringFromCallReply(reply);
    expected = KVModule_CreateString(ctx,str,len);
    if (KVModule_StringCompare(mystr,expected) != 0) {
        const char *mystr_ptr = KVModule_StringPtrLen(mystr,NULL);
        const char *expected_ptr = KVModule_StringPtrLen(expected,NULL);
        KVModule_Log(ctx,"warning",
            "Unexpected string reply '%s' (instead of '%s')",
            mystr_ptr, expected_ptr);
        return 0;
    }
    return 1;
}

/* Return 1 if the reply matches the specified integer, otherwise log errors
 * in the server log and return 0. */
int TestAssertIntegerReply(KVModuleCtx *ctx, KVModuleCallReply *reply, long long expected) {
    if (KVModule_CallReplyType(reply) == KVMODULE_REPLY_ERROR) {
        KVModule_Log(ctx,"warning","Test error reply: %s",
            KVModule_CallReplyStringPtr(reply, NULL));
        return 0;
    } else if (KVModule_CallReplyType(reply) != KVMODULE_REPLY_INTEGER) {
        KVModule_Log(ctx,"warning","Unexpected reply type %d",
            KVModule_CallReplyType(reply));
        return 0;
    }
    long long val = KVModule_CallReplyInteger(reply);
    if (val != expected) {
        KVModule_Log(ctx,"warning",
            "Unexpected integer reply '%lld' (instead of '%lld')",
            val, expected);
        return 0;
    }
    return 1;
}

#define T(name,...) \
    do { \
        KVModule_Log(ctx,"warning","Testing %s", name); \
        reply = KVModule_Call(ctx,name,__VA_ARGS__); \
    } while (0)

/* TEST.BASICS -- Run all the tests.
 * Note: it is useful to run these tests from the module rather than TCL
 * since it's easier to check the reply types like that (make a distinction
 * between 0 and "0", etc. */
int TestBasics(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    KVModule_AutoMemory(ctx);
    KVModuleCallReply *reply;

    /* Make sure the DB is empty before to proceed. */
    T("dbsize","");
    if (!TestAssertIntegerReply(ctx,reply,0)) goto fail;

    T("ping","");
    if (!TestAssertStringReply(ctx,reply,"PONG",4)) goto fail;

    T("test.call","");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.callresp3map","");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.callresp3set","");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.callresp3double","");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.callresp3bool","");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.callresp3null","");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.callreplywithnestedreply","");
    if (!TestAssertStringReply(ctx,reply,"test",4)) goto fail;

    T("test.callreplywithbignumberreply","");
    if (!TestAssertStringReply(ctx,reply,"1234567999999999999999999999999999999",37)) goto fail;

    T("test.callreplywithverbatimstringreply","");
    if (!TestAssertStringReply(ctx,reply,"txt:This is a verbatim\nstring",29)) goto fail;

    T("test.ctxflags","");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.string.append","");
    if (!TestAssertStringReply(ctx,reply,"foobar",6)) goto fail;

    T("test.string.truncate","");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.unlink","");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.nestedcallreplyarray","");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.string.append.am","");
    if (!TestAssertStringReply(ctx,reply,"foobar",6)) goto fail;
    
    T("test.string.trim","");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.string.printf", "cc", "foo", "bar");
    if (!TestAssertStringReply(ctx,reply,"Got 3 args. argv[1]: foo, argv[2]: bar",38)) goto fail;

    T("test.notify", "");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.callreplywitharrayreply", "");
    if (KVModule_CallReplyType(reply) != KVMODULE_REPLY_ARRAY) goto fail;
    if (KVModule_CallReplyLength(reply) != 2) goto fail;
    if (!TestAssertStringReply(ctx,KVModule_CallReplyArrayElement(reply, 0),"test",4)) goto fail;
    if (!TestAssertStringReply(ctx,KVModule_CallReplyArrayElement(reply, 1),"1234",4)) goto fail;

    T("foo", "E");
    if (!TestAssertErrorReply(ctx,reply,"ERR unknown command 'foo', with args beginning with: ",53)) goto fail;

    T("set", "Ec", "x");
    if (!TestAssertErrorReply(ctx,reply,"ERR wrong number of arguments for 'set' command",47)) goto fail;

    T("shutdown", "SE");
    if (!TestAssertErrorReply(ctx,reply,"ERR command 'shutdown' is not allowed on script mode",52)) goto fail;

    T("set", "WEcc", "x", "1");
    if (!TestAssertErrorReply(ctx,reply,"ERR Write command 'set' was called while write is not allowed.",62)) goto fail;

    KVModule_ReplyWithSimpleString(ctx,"ALL TESTS PASSED");
    return KVMODULE_OK;

fail:
    KVModule_ReplyWithSimpleString(ctx,
        "SOME TEST DID NOT PASS! Check server logs");
    return KVMODULE_OK;
}

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);

    if (KVModule_Init(ctx,"test",1,KVMODULE_APIVER_1)
        == KVMODULE_ERR) return KVMODULE_ERR;

    /* Perform RM_Call inside the KVModule_OnLoad
     * to verify that it works as expected without crashing.
     * The tests will verify it on different configurations
     * options (cluster/no cluster). A simple ping command
     * is enough for this test. */
    KVModuleCallReply *reply = KVModule_Call(ctx, "ping", "");
    if (KVModule_CallReplyType(reply) != KVMODULE_REPLY_STRING) {
        KVModule_FreeCallReply(reply);
        return KVMODULE_ERR;
    }
    size_t len;
    const char *reply_str = KVModule_CallReplyStringPtr(reply, &len);
    if (len != 4) {
        KVModule_FreeCallReply(reply);
        return KVMODULE_ERR;
    }
    if (memcmp(reply_str, "PONG", 4) != 0) {
        KVModule_FreeCallReply(reply);
        return KVMODULE_ERR;
    }
    KVModule_FreeCallReply(reply);

    if (KVModule_CreateCommand(ctx,"test.call",
        TestCall,"write deny-oom",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"test.callresp3map",
        TestCallResp3Map,"write deny-oom",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"test.callresp3attribute",
        TestCallResp3Attribute,"write deny-oom",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"test.callresp3set",
        TestCallResp3Set,"write deny-oom",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"test.callresp3double",
        TestCallResp3Double,"write deny-oom",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"test.callresp3bool",
        TestCallResp3Bool,"write deny-oom",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"test.callresp3null",
        TestCallResp3Null,"write deny-oom",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"test.callreplywitharrayreply",
        TestCallReplyWithArrayReply,"write deny-oom",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"test.callreplywithnestedreply",
        TestCallReplyWithNestedReply,"write deny-oom",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"test.callreplywithbignumberreply",
        TestCallResp3BigNumber,"write deny-oom",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"test.callreplywithverbatimstringreply",
        TestCallResp3Verbatim,"write deny-oom",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"test.string.append",
        TestStringAppend,"write deny-oom",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"test.string.trim",
        TestTrimString,"write deny-oom",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"test.string.append.am",
        TestStringAppendAM,"write deny-oom",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"test.string.truncate",
        TestStringTruncate,"write deny-oom",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"test.string.printf",
        TestStringPrintf,"write deny-oom",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"test.ctxflags",
        TestCtxFlags,"readonly",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"test.unlink",
        TestUnlink,"write deny-oom",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"test.nestedcallreplyarray",
        TestNestedCallReplyArrayElement,"write deny-oom",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"test.basics",
        TestBasics,"write",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    /* the following commands are used by an external test and should not be added to TestBasics */
    if (KVModule_CreateCommand(ctx,"test.rmcallautomode",
        TestCallRespAutoMode,"write",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx,"test.getresp",
        TestGetResp,"readonly",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    KVModule_SubscribeToKeyspaceEvents(ctx,
                                            KVMODULE_NOTIFY_HASH |
                                            KVMODULE_NOTIFY_SET |
                                            KVMODULE_NOTIFY_STRING |
                                            KVMODULE_NOTIFY_KEY_MISS,
                                        NotifyCallback);
    if (KVModule_CreateCommand(ctx,"test.notify",
        TestNotifications,"write deny-oom",1,1,1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "test.latency", TestLatency, "readonly", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}
