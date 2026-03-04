/* Helloworld module -- A few examples of the Modules API in the form
 * of commands showing how to accomplish common tasks.
 *
 * This module does not do anything useful, if not for a few commands. The
 * examples are designed in order to show the API.
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

#include "../kvmodule.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

/* HELLO.SIMPLE is among the simplest commands you can implement.
 * It just returns the currently selected DB id, a functionality which is
 * missing in the server. The command uses two important API calls: one to
 * fetch the currently selected DB, the other in order to send the client
 * an integer reply as response. */
int HelloSimple_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    KVModule_ReplyWithLongLong(ctx, KVModule_GetSelectedDb(ctx));
    return KVMODULE_OK;
}

/* HELLO.PUSH.NATIVE re-implements RPUSH, and shows the low level modules API
 * where you can "open" keys, make low level operations, create new keys by
 * pushing elements into non-existing keys, and so forth.
 *
 * You'll find this command to be roughly as fast as the actual RPUSH
 * command. */
int HelloPushNative_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 3) return KVModule_WrongArity(ctx);

    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_READ | KVMODULE_WRITE);

    KVModule_ListPush(key, KVMODULE_LIST_TAIL, argv[2]);
    size_t newlen = KVModule_ValueLength(key);
    KVModule_CloseKey(key);
    KVModule_ReplyWithLongLong(ctx, newlen);
    return KVMODULE_OK;
}

/* HELLO.PUSH.CALL implements RPUSH using an higher level approach, calling
 * a command instead of working with the key in a low level way. This
 * approach is useful when you need to call commands that are not
 * available as low level APIs, or when you don't need the maximum speed
 * possible but instead prefer implementation simplicity. */
int HelloPushCall_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 3) return KVModule_WrongArity(ctx);

    KVModuleCallReply *reply;

    reply = KVModule_Call(ctx, "RPUSH", "ss", argv[1], argv[2]);
    long long len = KVModule_CallReplyInteger(reply);
    KVModule_FreeCallReply(reply);
    KVModule_ReplyWithLongLong(ctx, len);
    return KVMODULE_OK;
}

/* HELLO.PUSH.CALL2
 * This is exactly as HELLO.PUSH.CALL, but shows how we can reply to the
 * client using directly a reply object that Call() returned. */
int HelloPushCall2_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 3) return KVModule_WrongArity(ctx);

    KVModuleCallReply *reply;

    reply = KVModule_Call(ctx, "RPUSH", "ss", argv[1], argv[2]);
    KVModule_ReplyWithCallReply(ctx, reply);
    KVModule_FreeCallReply(reply);
    return KVMODULE_OK;
}

/* HELLO.LIST.SUM.LEN returns the total length of all the items inside
 * a list, by using the high level Call() API.
 * This command is an example of the array reply access. */
int HelloListSumLen_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) return KVModule_WrongArity(ctx);

    KVModuleCallReply *reply;

    reply = KVModule_Call(ctx, "LRANGE", "sll", argv[1], (long long)0, (long long)-1);
    size_t strlen = 0;
    size_t items = KVModule_CallReplyLength(reply);
    size_t j;
    for (j = 0; j < items; j++) {
        KVModuleCallReply *ele = KVModule_CallReplyArrayElement(reply, j);
        strlen += KVModule_CallReplyLength(ele);
    }
    KVModule_FreeCallReply(reply);
    KVModule_ReplyWithLongLong(ctx, strlen);
    return KVMODULE_OK;
}

/* HELLO.LIST.SPLICE srclist dstlist count
 * Moves 'count' elements from the tail of 'srclist' to the head of
 * 'dstlist'. If less than count elements are available, it moves as much
 * elements as possible. */
int HelloListSplice_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 4) return KVModule_WrongArity(ctx);

    KVModuleKey *srckey = KVModule_OpenKey(ctx, argv[1], KVMODULE_READ | KVMODULE_WRITE);
    KVModuleKey *dstkey = KVModule_OpenKey(ctx, argv[2], KVMODULE_READ | KVMODULE_WRITE);

    /* Src and dst key must be empty or lists. */
    if ((KVModule_KeyType(srckey) != KVMODULE_KEYTYPE_LIST &&
         KVModule_KeyType(srckey) != KVMODULE_KEYTYPE_EMPTY) ||
        (KVModule_KeyType(dstkey) != KVMODULE_KEYTYPE_LIST &&
         KVModule_KeyType(dstkey) != KVMODULE_KEYTYPE_EMPTY)) {
        KVModule_CloseKey(srckey);
        KVModule_CloseKey(dstkey);
        return KVModule_ReplyWithError(ctx, KVMODULE_ERRORMSG_WRONGTYPE);
    }

    long long count;
    if ((KVModule_StringToLongLong(argv[3], &count) != KVMODULE_OK) || (count < 0)) {
        KVModule_CloseKey(srckey);
        KVModule_CloseKey(dstkey);
        return KVModule_ReplyWithError(ctx, "ERR invalid count");
    }

    while (count-- > 0) {
        KVModuleString *ele;

        ele = KVModule_ListPop(srckey, KVMODULE_LIST_TAIL);
        if (ele == NULL) break;
        KVModule_ListPush(dstkey, KVMODULE_LIST_HEAD, ele);
        KVModule_FreeString(ctx, ele);
    }

    size_t len = KVModule_ValueLength(srckey);
    KVModule_CloseKey(srckey);
    KVModule_CloseKey(dstkey);
    KVModule_ReplyWithLongLong(ctx, len);
    return KVMODULE_OK;
}

/* Like the HELLO.LIST.SPLICE above, but uses automatic memory management
 * in order to avoid freeing stuff. */
int HelloListSpliceAuto_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 4) return KVModule_WrongArity(ctx);

    KVModule_AutoMemory(ctx);

    KVModuleKey *srckey = KVModule_OpenKey(ctx, argv[1], KVMODULE_READ | KVMODULE_WRITE);
    KVModuleKey *dstkey = KVModule_OpenKey(ctx, argv[2], KVMODULE_READ | KVMODULE_WRITE);

    /* Src and dst key must be empty or lists. */
    if ((KVModule_KeyType(srckey) != KVMODULE_KEYTYPE_LIST &&
         KVModule_KeyType(srckey) != KVMODULE_KEYTYPE_EMPTY) ||
        (KVModule_KeyType(dstkey) != KVMODULE_KEYTYPE_LIST &&
         KVModule_KeyType(dstkey) != KVMODULE_KEYTYPE_EMPTY)) {
        return KVModule_ReplyWithError(ctx, KVMODULE_ERRORMSG_WRONGTYPE);
    }

    long long count;
    if ((KVModule_StringToLongLong(argv[3], &count) != KVMODULE_OK) || (count < 0)) {
        return KVModule_ReplyWithError(ctx, "ERR invalid count");
    }

    while (count-- > 0) {
        KVModuleString *ele;

        ele = KVModule_ListPop(srckey, KVMODULE_LIST_TAIL);
        if (ele == NULL) break;
        KVModule_ListPush(dstkey, KVMODULE_LIST_HEAD, ele);
    }

    size_t len = KVModule_ValueLength(srckey);
    KVModule_ReplyWithLongLong(ctx, len);
    return KVMODULE_OK;
}

/* HELLO.RAND.ARRAY <count>
 * Shows how to generate arrays as commands replies.
 * It just outputs <count> random numbers. */
int HelloRandArray_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) return KVModule_WrongArity(ctx);
    long long count;
    if (KVModule_StringToLongLong(argv[1], &count) != KVMODULE_OK || count < 0)
        return KVModule_ReplyWithError(ctx, "ERR invalid count");

    /* To reply with an array, we call KVModule_ReplyWithArray() followed
     * by other "count" calls to other reply functions in order to generate
     * the elements of the array. */
    KVModule_ReplyWithArray(ctx, count);
    while (count--) KVModule_ReplyWithLongLong(ctx, rand());
    return KVMODULE_OK;
}

/* This is a simple command to test replication. Because of the "!" modified
 * in the KVModule_Call() call, the two INCRs get replicated.
 * Also note how the ECHO is replicated in an unexpected position (check
 * comments the function implementation). */
int HelloRepl1_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVMODULE_NOT_USED(argv);
    KVMODULE_NOT_USED(argc);
    KVModule_AutoMemory(ctx);

    /* This will be replicated *after* the two INCR statements, since
     * the Call() replication has precedence, so the actual replication
     * stream will be:
     *
     * MULTI
     * INCR foo
     * INCR bar
     * ECHO c foo
     * EXEC
     */
    KVModule_Replicate(ctx, "ECHO", "c", "foo");

    /* Using the "!" modifier we replicate the command if it
     * modified the dataset in some way. */
    KVModule_Call(ctx, "INCR", "c!", "foo");
    KVModule_Call(ctx, "INCR", "c!", "bar");

    KVModule_ReplyWithLongLong(ctx, 0);

    return KVMODULE_OK;
}

/* Another command to show replication. In this case, we call
 * KVModule_ReplicateVerbatim() to mean we want just the command to be
 * propagated to replicas / AOF exactly as it was called by the user.
 *
 * This command also shows how to work with string objects.
 * It takes a list, and increments all the elements (that must have
 * a numerical value) by 1, returning the sum of all the elements
 * as reply.
 *
 * Usage: HELLO.REPL2 <list-key> */
int HelloRepl2_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) return KVModule_WrongArity(ctx);

    KVModule_AutoMemory(ctx); /* Use automatic memory management. */
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_READ | KVMODULE_WRITE);

    if (KVModule_KeyType(key) != KVMODULE_KEYTYPE_LIST)
        return KVModule_ReplyWithError(ctx, KVMODULE_ERRORMSG_WRONGTYPE);

    size_t listlen = KVModule_ValueLength(key);
    long long sum = 0;

    /* Rotate and increment. */
    while (listlen--) {
        KVModuleString *ele = KVModule_ListPop(key, KVMODULE_LIST_TAIL);
        long long val;
        if (KVModule_StringToLongLong(ele, &val) != KVMODULE_OK) val = 0;
        val++;
        sum += val;
        KVModuleString *newele = KVModule_CreateStringFromLongLong(ctx, val);
        KVModule_ListPush(key, KVMODULE_LIST_HEAD, newele);
    }
    KVModule_ReplyWithLongLong(ctx, sum);
    KVModule_ReplicateVerbatim(ctx);
    return KVMODULE_OK;
}

/* This is an example of strings DMA access. Given a key containing a string
 * it toggles the case of each character from lower to upper case or the
 * other way around.
 *
 * No automatic memory management is used in this example (for the sake
 * of variety).
 *
 * HELLO.TOGGLE.CASE key */
int HelloToggleCase_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (argc != 2) return KVModule_WrongArity(ctx);

    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_READ | KVMODULE_WRITE);

    int keytype = KVModule_KeyType(key);
    if (keytype != KVMODULE_KEYTYPE_STRING && keytype != KVMODULE_KEYTYPE_EMPTY) {
        KVModule_CloseKey(key);
        return KVModule_ReplyWithError(ctx, KVMODULE_ERRORMSG_WRONGTYPE);
    }

    if (keytype == KVMODULE_KEYTYPE_STRING) {
        size_t len, j;
        char *s = KVModule_StringDMA(key, &len, KVMODULE_WRITE);
        for (j = 0; j < len; j++) {
            if (isupper(s[j])) {
                s[j] = tolower(s[j]);
            } else {
                s[j] = toupper(s[j]);
            }
        }
    }

    KVModule_CloseKey(key);
    KVModule_ReplyWithSimpleString(ctx, "OK");
    KVModule_ReplicateVerbatim(ctx);
    return KVMODULE_OK;
}

/* HELLO.MORE.EXPIRE key milliseconds.
 *
 * If the key has already an associated TTL, extends it by "milliseconds"
 * milliseconds. Otherwise no operation is performed. */
int HelloMoreExpire_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVModule_AutoMemory(ctx); /* Use automatic memory management. */
    if (argc != 3) return KVModule_WrongArity(ctx);

    mstime_t addms, expire;

    if (KVModule_StringToLongLong(argv[2], &addms) != KVMODULE_OK)
        return KVModule_ReplyWithError(ctx, "ERR invalid expire time");

    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_READ | KVMODULE_WRITE);
    expire = KVModule_GetExpire(key);
    if (expire != KVMODULE_NO_EXPIRE) {
        expire += addms;
        KVModule_SetExpire(key, expire);
    }
    return KVModule_ReplyWithSimpleString(ctx, "OK");
}

/* HELLO.ZSUMRANGE key startscore endscore
 * Return the sum of all the scores elements between startscore and endscore.
 *
 * The computation is performed two times, one time from start to end and
 * another time backward. The two scores, returned as a two element array,
 * should match.*/
int HelloZsumRange_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    double score_start, score_end;
    if (argc != 4) return KVModule_WrongArity(ctx);

    if (KVModule_StringToDouble(argv[2], &score_start) != KVMODULE_OK ||
        KVModule_StringToDouble(argv[3], &score_end) != KVMODULE_OK) {
        return KVModule_ReplyWithError(ctx, "ERR invalid range");
    }

    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_READ | KVMODULE_WRITE);
    if (KVModule_KeyType(key) != KVMODULE_KEYTYPE_ZSET) {
        return KVModule_ReplyWithError(ctx, KVMODULE_ERRORMSG_WRONGTYPE);
    }

    double scoresum_a = 0;
    double scoresum_b = 0;

    KVModule_ZsetFirstInScoreRange(key, score_start, score_end, 0, 0);
    while (!KVModule_ZsetRangeEndReached(key)) {
        double score;
        KVModuleString *ele = KVModule_ZsetRangeCurrentElement(key, &score);
        KVModule_FreeString(ctx, ele);
        scoresum_a += score;
        KVModule_ZsetRangeNext(key);
    }
    KVModule_ZsetRangeStop(key);

    KVModule_ZsetLastInScoreRange(key, score_start, score_end, 0, 0);
    while (!KVModule_ZsetRangeEndReached(key)) {
        double score;
        KVModuleString *ele = KVModule_ZsetRangeCurrentElement(key, &score);
        KVModule_FreeString(ctx, ele);
        scoresum_b += score;
        KVModule_ZsetRangePrev(key);
    }

    KVModule_ZsetRangeStop(key);

    KVModule_CloseKey(key);

    KVModule_ReplyWithArray(ctx, 2);
    KVModule_ReplyWithDouble(ctx, scoresum_a);
    KVModule_ReplyWithDouble(ctx, scoresum_b);
    return KVMODULE_OK;
}

/* HELLO.LEXRANGE key min_lex max_lex min_age max_age
 * This command expects a sorted set stored at key in the following form:
 * - All the elements have score 0.
 * - Elements are pairs of "<name>:<age>", for example "Anna:52".
 * The command will return all the sorted set items that are lexicographically
 * between the specified range (using the same format as ZRANGEBYLEX)
 * and having an age between min_age and max_age. */
int HelloLexRange_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVModule_AutoMemory(ctx); /* Use automatic memory management. */

    if (argc != 6) return KVModule_WrongArity(ctx);

    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_READ | KVMODULE_WRITE);
    if (KVModule_KeyType(key) != KVMODULE_KEYTYPE_ZSET) {
        return KVModule_ReplyWithError(ctx, KVMODULE_ERRORMSG_WRONGTYPE);
    }

    if (KVModule_ZsetFirstInLexRange(key, argv[2], argv[3]) != KVMODULE_OK) {
        return KVModule_ReplyWithError(ctx, "invalid range");
    }

    int arraylen = 0;
    KVModule_ReplyWithArray(ctx, KVMODULE_POSTPONED_LEN);
    while (!KVModule_ZsetRangeEndReached(key)) {
        double score;
        KVModuleString *ele = KVModule_ZsetRangeCurrentElement(key, &score);
        KVModule_ReplyWithString(ctx, ele);
        KVModule_FreeString(ctx, ele);
        KVModule_ZsetRangeNext(key);
        arraylen++;
    }
    KVModule_ZsetRangeStop(key);
    KVModule_ReplySetArrayLength(ctx, arraylen);
    KVModule_CloseKey(key);
    return KVMODULE_OK;
}

/* HELLO.HCOPY key srcfield dstfield
 * This is just an example command that sets the hash field dstfield to the
 * same value of srcfield. If srcfield does not exist no operation is
 * performed.
 *
 * The command returns 1 if the copy is performed (srcfield exists) otherwise
 * 0 is returned. */
int HelloHCopy_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVModule_AutoMemory(ctx); /* Use automatic memory management. */

    if (argc != 4) return KVModule_WrongArity(ctx);
    KVModuleKey *key = KVModule_OpenKey(ctx, argv[1], KVMODULE_READ | KVMODULE_WRITE);
    int type = KVModule_KeyType(key);
    if (type != KVMODULE_KEYTYPE_HASH && type != KVMODULE_KEYTYPE_EMPTY) {
        return KVModule_ReplyWithError(ctx, KVMODULE_ERRORMSG_WRONGTYPE);
    }

    /* Get the old field value. */
    KVModuleString *oldval;
    KVModule_HashGet(key, KVMODULE_HASH_NONE, argv[2], &oldval, NULL);
    if (oldval) {
        KVModule_HashSet(key, KVMODULE_HASH_NONE, argv[3], oldval, NULL);
    }
    KVModule_ReplyWithLongLong(ctx, oldval != NULL);
    return KVMODULE_OK;
}

/* HELLO.LEFTPAD str len ch
 * This is an implementation of the infamous LEFTPAD function, that
 * was at the center of an issue with the npm modules system in March 2016.
 *
 * LEFTPAD is a good example of using a Modules API called
 * "pool allocator", that was a famous way to allocate memory in yet another
 * open source project, the Apache web server.
 *
 * The concept is very simple: there is memory that is useful to allocate
 * only in the context of serving a request, and must be freed anyway when
 * the callback implementing the command returns. So in that case the module
 * does not need to retain a reference to these allocations, it is just
 * required to free the memory before returning. When this is the case the
 * module can call KVModule_PoolAlloc() instead, that works like malloc()
 * but will automatically free the memory when the module callback returns.
 *
 * Note that PoolAlloc() does not necessarily require AutoMemory to be
 * active. */
int HelloLeftPad_KVCommand(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    KVModule_AutoMemory(ctx); /* Use automatic memory management. */
    long long padlen;

    if (argc != 4) return KVModule_WrongArity(ctx);

    if ((KVModule_StringToLongLong(argv[2], &padlen) != KVMODULE_OK) || (padlen < 0)) {
        return KVModule_ReplyWithError(ctx, "ERR invalid padding length");
    }
    size_t strlen, chlen;
    const char *str = KVModule_StringPtrLen(argv[1], &strlen);
    const char *ch = KVModule_StringPtrLen(argv[3], &chlen);

    /* If the string is already larger than the target len, just return
     * the string itself. */
    if (strlen >= (size_t)padlen) return KVModule_ReplyWithString(ctx, argv[1]);

    /* Padding must be a single character in this simple implementation. */
    if (chlen != 1) return KVModule_ReplyWithError(ctx, "ERR padding must be a single char");

    /* Here we use our pool allocator, for our throw-away allocation. */
    padlen -= strlen;
    char *buf = KVModule_PoolAlloc(ctx, padlen + strlen);
    for (long long j = 0; j < padlen; j++) buf[j] = *ch;
    memcpy(buf + padlen, str, strlen);

    KVModule_ReplyWithStringBuffer(ctx, buf, padlen + strlen);
    return KVMODULE_OK;
}

/* This function must be present on each module. It is used in order to
 * register the commands into the server. */
int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (KVModule_Init(ctx, "helloworld", 1, KVMODULE_APIVER_1) == KVMODULE_ERR) return KVMODULE_ERR;

    /* Log the list of parameters passing loading the module. */
    for (int j = 0; j < argc; j++) {
        const char *s = KVModule_StringPtrLen(argv[j], NULL);
        printf("Module loaded with ARGV[%d] = %s\n", j, s);
    }

    if (KVModule_CreateCommand(ctx, "hello.simple", HelloSimple_KVCommand, "readonly", 0, 0, 0) ==
        KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hello.push.native", HelloPushNative_KVCommand, "write deny-oom", 1, 1,
                                   1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hello.push.call", HelloPushCall_KVCommand, "write deny-oom", 1, 1, 1) ==
        KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hello.push.call2", HelloPushCall2_KVCommand, "write deny-oom", 1, 1, 1) ==
        KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hello.list.sum.len", HelloListSumLen_KVCommand, "readonly", 1, 1, 1) ==
        KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hello.list.splice", HelloListSplice_KVCommand, "write deny-oom", 1, 2,
                                   1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hello.list.splice.auto", HelloListSpliceAuto_KVCommand, "write deny-oom",
                                   1, 2, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hello.rand.array", HelloRandArray_KVCommand, "readonly", 0, 0, 0) ==
        KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hello.repl1", HelloRepl1_KVCommand, "write", 0, 0, 0) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hello.repl2", HelloRepl2_KVCommand, "write", 1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hello.toggle.case", HelloToggleCase_KVCommand, "write", 1, 1, 1) ==
        KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hello.more.expire", HelloMoreExpire_KVCommand, "write", 1, 1, 1) ==
        KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hello.zsumrange", HelloZsumRange_KVCommand, "readonly", 1, 1, 1) ==
        KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hello.lexrange", HelloLexRange_KVCommand, "readonly", 1, 1, 1) ==
        KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hello.hcopy", HelloHCopy_KVCommand, "write deny-oom", 1, 1, 1) ==
        KVMODULE_ERR)
        return KVMODULE_ERR;

    if (KVModule_CreateCommand(ctx, "hello.leftpad", HelloLeftPad_KVCommand, "", 1, 1, 1) == KVMODULE_ERR)
        return KVMODULE_ERR;

    return KVMODULE_OK;
}
