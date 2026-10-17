#include "ui/ConnectionProbe.h"

#include <QDateTime>
#include <QTcpSocket>
#include <QTimer>

namespace Em {

ConnectionProbe::ConnectionProbe(QObject *parent) : QObject(parent) {}

void ConnectionProbe::probe(const QString &id, const QString &host, quint16 port)
{
    if (id.isEmpty() || host.trimmed().isEmpty())
        return;
    if (m_active.contains(id))
        return;
    for (const Job &job : m_queue) {
        if (job.id == id)
            return;
    }

    m_queue.enqueue({ id, host, port });
    startNext();
}

void ConnectionProbe::startNext()
{
    while (m_active.size() < kMaxConcurrent && !m_queue.isEmpty()) {
        const Job job = m_queue.dequeue();

        auto *socket = new QTcpSocket(this);
        socket->setProperty("probeId", job.id);
        socket->setProperty("startedAt", QDateTime::currentMSecsSinceEpoch());
        m_active.insert(job.id, socket);

        connect(socket, &QTcpSocket::connected, this, [this, socket] {
            const qint64 started = socket->property("startedAt").toLongLong();
            const int latency = int(QDateTime::currentMSecsSinceEpoch() - started);
            finish(socket, true, latency);
        });
        connect(socket, &QTcpSocket::errorOccurred, this,
                [this, socket](QAbstractSocket::SocketError) {
            finish(socket, false, -1);
        });

        // 超时也走 finish；done 标记保证只会结算一次
        QTimer::singleShot(kTimeoutMs, socket, [this, socket] {
            finish(socket, false, -1);
        });

        socket->connectToHost(job.host, job.port);
    }
}

void ConnectionProbe::finish(QTcpSocket *socket, bool online, int latencyMs)
{
    if (!socket || socket->property("done").toBool())
        return;

    socket->setProperty("done", true);
    const QString id = socket->property("probeId").toString();
    m_active.remove(id);

    emit finished(id, online, latencyMs);

    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();

    // 用队列连接再排下一个，别在 errorOccurred 的调用栈里递归下去
    QTimer::singleShot(0, this, &ConnectionProbe::startNext);
}

} // namespace Em
