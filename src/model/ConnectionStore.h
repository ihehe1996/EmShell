#pragma once

#include "model/Connection.h"

#include <QObject>
#include <QVector>

namespace Em {

// 所有连接 / 分组 / 历史的唯一数据源。
//
// 存在 %APPDATA%/EmShell/connections.json（QStandardPaths::AppDataLocation）。
// 任何修改都会自动落盘并发信号，界面监听信号刷新即可，不要绕过它直接改文件。
class ConnectionStore : public QObject
{
    Q_OBJECT

public:
    explicit ConnectionStore(QObject *parent = nullptr);

    // 从磁盘读。文件不存在（首次运行）时连接 / 历史为空，只预置
    // 「生产环境 / 测试环境」两个分组 —— 不要往列表里塞示例连接。
    void load();
    void save();

    QString filePath() const { return m_filePath; }

    // ---- 连接 ----
    const QVector<Connection> &connections() const { return m_connections; }
    QVector<Connection> connectionsInGroup(const QString &groupId) const;
    QVector<Connection> favorites() const;
    QVector<Connection> search(const QString &keyword) const;
    const Connection *find(const QString &id) const;

    void addConnection(const Connection &c);
    void updateConnection(const Connection &c);
    void removeConnection(const QString &id);
    void toggleFavorite(const QString &id);

    // 连上了之后调一下：刷新 lastUsedAt / useCount，并往历史里加一条
    void recordUse(const QString &id, bool ok = true);

    // ---- 分组 ----
    const QVector<ConnectionGroup> &groups() const { return m_groups; }
    const ConnectionGroup *findGroup(const QString &id) const;

    void addGroup(const QString &name, const QString &color = QString());
    // 改名和改色一起做：分组的编辑就一个对话框，没必要拆成两个几乎一样的函数
    void updateGroup(const QString &id, const QString &name, const QString &color);
    // 上移 / 下移：delta = -1 / +1。越界时什么都不做（也不发信号）
    void moveGroup(const QString &id, int delta);
    void removeGroup(const QString &id, bool deleteConnections = false);

    // ---- 历史 ----
    // 每个连接只保留最近一次使用的一条，按时间从新到旧排列。
    const QVector<HistoryEntry> &history() const { return m_history; }
    void clearHistory();

signals:
    void connectionsChanged();
    void groupsChanged();
    void historyChanged();

private:
    void seedDefaultGroups();
    static QString dataDirectory();

    QString m_filePath;
    QVector<Connection> m_connections;
    QVector<ConnectionGroup> m_groups;
    QVector<HistoryEntry> m_history;

    static constexpr int kMaxHistory = 500;
};

} // namespace Em
