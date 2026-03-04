#ifndef KV_EXAMPLE_QT_H
#define KV_EXAMPLE_QT_H

#include <kv/adapters/qt.h>

class ExampleQt : public QObject {

    Q_OBJECT

  public:
    ExampleQt(const char *value, QObject *parent = 0)
        : QObject(parent), m_value(value) {}

  signals:
    void finished();

  public slots:
    void run();

  private:
    void finish() { emit finished(); }

  private:
    const char *m_value;
    kvAsyncContext *m_ctx;
    KVQtAdapter m_adapter;

    friend void getCallback(kvAsyncContext *, void *, void *);
};

#endif /* KV_EXAMPLE_QT_H */
