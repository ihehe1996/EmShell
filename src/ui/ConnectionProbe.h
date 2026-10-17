#pragma once

#include <QHash>
#include <QObject>
#include <QQueue>
#include <QString>

class QTcpSocket;

namespace Em {

// 对连接做一次 TCP 端口探测，用来填列表里的「状态 / 延迟」。
//
// 只测「这个端口能不能连上」，**不代表 SSH 认证一定能过** —— 端口开着但密码错了，
// 在列表里仍然是「在线」。这和 ping 也不是一回事：ICMP 被禁的机器也能测出来。
//
// 全程异步：QTcpSocket 在 GUI 线程里跑，DNS 解析和连接都不阻塞界面。
// 同一时刻最多 6 个探测，其余排队；每个 2.5 秒超时。
class ConnectionProbe : public QObject
{
    Q_OBJECT

public:
    explicit ConnectionProbe(QObject *parent = nullptr);

    // 同一个 id 已经在探测 / 队列里时不会再排一次。
    void probe(const QString &id, const QString &host, quint16 port);

signals:
    void finished(const QString &id, bool online, int latencyMs);

private:
    struct Job
    {
        QString id;
        QString host;
        quint16 port = 0;
    };

    void startNext();
    void finish(QTcpSocket *socket, bool online, int latencyMs);

    QQueue<Job> m_queue;
    QHash<QString, QTcpSocket *> m_active;

    static constexpr int kMaxConcurrent = 6;
    static constexpr int kTimeoutMs = 2500;
};

} // namespace Em
