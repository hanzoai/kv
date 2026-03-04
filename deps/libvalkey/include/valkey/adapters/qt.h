/*-
 * Copyright (C) 2014 Pietro Cerutti <gahr@gahr.ch>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef KV_ADAPTERS_QT_H
#define KV_ADAPTERS_QT_H
#include "../async.h"

#include <QSocketNotifier>

static void KVQtAddRead(void *);
static void KVQtDelRead(void *);
static void KVQtAddWrite(void *);
static void KVQtDelWrite(void *);
static void KVQtCleanup(void *);

class KVQtAdapter : public QObject {

    Q_OBJECT

    friend void KVQtAddRead(void *adapter) {
        KVQtAdapter *a = static_cast<KVQtAdapter *>(adapter);
        a->addRead();
    }

    friend void KVQtDelRead(void *adapter) {
        KVQtAdapter *a = static_cast<KVQtAdapter *>(adapter);
        a->delRead();
    }

    friend void KVQtAddWrite(void *adapter) {
        KVQtAdapter *a = static_cast<KVQtAdapter *>(adapter);
        a->addWrite();
    }

    friend void KVQtDelWrite(void *adapter) {
        KVQtAdapter *a = static_cast<KVQtAdapter *>(adapter);
        a->delWrite();
    }

    friend void KVQtCleanup(void *adapter) {
        KVQtAdapter *a = static_cast<KVQtAdapter *>(adapter);
        a->cleanup();
    }

  public:
    KVQtAdapter(QObject *parent = 0)
        : QObject(parent), m_ctx(0), m_read(0), m_write(0) {}

    ~KVQtAdapter() {
        if (m_ctx != 0) {
            m_ctx->ev.data = NULL;
        }
    }

    int setContext(kvAsyncContext *ac) {
        if (ac->ev.data != NULL) {
            return KV_ERR;
        }
        m_ctx = ac;
        m_ctx->ev.data = this;
        m_ctx->ev.addRead = KVQtAddRead;
        m_ctx->ev.delRead = KVQtDelRead;
        m_ctx->ev.addWrite = KVQtAddWrite;
        m_ctx->ev.delWrite = KVQtDelWrite;
        m_ctx->ev.cleanup = KVQtCleanup;
        return KV_OK;
    }

  private:
    void addRead() {
        if (m_read)
            return;
        m_read = new QSocketNotifier(m_ctx->c.fd, QSocketNotifier::Read, 0);
        connect(m_read, SIGNAL(activated(int)), this, SLOT(read()));
    }

    void delRead() {
        if (!m_read)
            return;
        delete m_read;
        m_read = 0;
    }

    void addWrite() {
        if (m_write)
            return;
        m_write = new QSocketNotifier(m_ctx->c.fd, QSocketNotifier::Write, 0);
        connect(m_write, SIGNAL(activated(int)), this, SLOT(write()));
    }

    void delWrite() {
        if (!m_write)
            return;
        delete m_write;
        m_write = 0;
    }

    void cleanup() {
        delRead();
        delWrite();
    }

  private slots:
    void read() { kvAsyncHandleRead(m_ctx); }
    void write() { kvAsyncHandleWrite(m_ctx); }

  private:
    kvAsyncContext *m_ctx;
    QSocketNotifier *m_read;
    QSocketNotifier *m_write;
};

#endif /* KV_ADAPTERS_QT_H */
