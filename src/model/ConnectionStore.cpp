#include "model/ConnectionStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace Em {

ConnectionStore::ConnectionStore(QObject *parent)
    : QObject(parent)
{
    m_filePath = dataDirectory() + QStringLiteral("/connections.json");
}

QString ConnectionStore::dataDirectory()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.emshell");
    QDir().mkpath(base);
    return base;
}

void ConnectionStore::load()
{
    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        // 首次运行：从空数据开始。这里**不要**塞示例连接 —— 程序是打包发出去的，
        // 新用户装好一打开就该看到空的「所有连接」，而不是一堆 192.168.x.x 的假条目。
        save();
        return;
    }

    const QByteArray raw = f.readAll();
    f.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        // 文件坏了就别硬读，备份一份再从空开始，免得用户数据无声消失。
        // 同样不补示例数据 —— 那是假数据，补进来只会让人以为自己的连接被换掉了。
        QFile::rename(m_filePath, m_filePath + QStringLiteral(".broken"));
        save();
        return;
    }

    const QJsonObject root = doc.object();

    m_connections.clear();
    for (const QJsonValue &v : root.value(QStringLiteral("connections")).toArray())
        m_connections << Connection::fromJson(v.toObject());

    m_groups.clear();
    for (const QJsonValue &v : root.value(QStringLiteral("groups")).toArray())
        m_groups << ConnectionGroup::fromJson(v.toObject());

    // 历史里每个连接只留一条（最新那条）。老版本的文件里同一个连接可能被记了
    // 很多遍，读进来时顺手去重，不然「最近使用」页会排出一长串重复行。
    // 文件里的顺序就是新的在前，所以保留先遇到的那条即可。
    m_history.clear();
    QSet<QString> seenHistory;
    for (const QJsonValue &v : root.value(QStringLiteral("history")).toArray()) {
        const HistoryEntry entry = HistoryEntry::fromJson(v.toObject());
        if (!entry.connectionId.isEmpty()) {
            if (seenHistory.contains(entry.connectionId))
                continue;
            seenHistory.insert(entry.connectionId);
        }
        m_history << entry;
    }

    emit connectionsChanged();
    emit groupsChanged();
    emit historyChanged();
}

void ConnectionStore::save()
{
    QJsonArray conns;
    for (const Connection &c : m_connections)
        conns.append(c.toJson());

    QJsonArray groups;
    for (const ConnectionGroup &g : m_groups)
        groups.append(g.toJson());

    QJsonArray hist;
    for (const HistoryEntry &h : m_history)
        hist.append(h.toJson());

    QJsonObject root;
    root[QStringLiteral("version")]     = 1;
    root[QStringLiteral("connections")] = conns;
    root[QStringLiteral("groups")]      = groups;
    root[QStringLiteral("history")]     = hist;

    // QSaveFile 保证写一半崩了也不会把原文件毁了
    QSaveFile f(m_filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning("EmShell: 打不开 %s，改动没法保存", qPrintable(m_filePath));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.commit();
}

// ---------------------------------------------------------------- 连接 ---

QVector<Connection> ConnectionStore::connectionsInGroup(const QString &groupId) const
{
    QVector<Connection> out;
    for (const Connection &c : m_connections) {
        if (c.groupId == groupId)
            out << c;
    }
    return out;
}

QVector<Connection> ConnectionStore::favorites() const
{
    QVector<Connection> out;
    for (const Connection &c : m_connections) {
        if (c.favorite)
            out << c;
    }
    return out;
}

QVector<Connection> ConnectionStore::search(const QString &keyword) const
{
    if (keyword.trimmed().isEmpty())
        return m_connections;

    const QString needle = keyword.trimmed().toLower();
    QVector<Connection> out;
    for (const Connection &c : m_connections) {
        if (c.searchBlob().contains(needle))
            out << c;
    }
    return out;
}

const Connection *ConnectionStore::find(const QString &id) const
{
    for (const Connection &c : m_connections) {
        if (c.id == id)
            return &c;
    }
    return nullptr;
}

void ConnectionStore::addConnection(const Connection &c)
{
    m_connections.prepend(c);
    save();
    emit connectionsChanged();
}

void ConnectionStore::updateConnection(const Connection &c)
{
    for (Connection &existing : m_connections) {
        if (existing.id == c.id) {
            existing = c;
            save();
            emit connectionsChanged();
            return;
        }
    }
}

void ConnectionStore::removeConnection(const QString &id)
{
    const auto before = m_connections.size();
    m_connections.erase(std::remove_if(m_connections.begin(), m_connections.end(),
                                       [&id](const Connection &c) { return c.id == id; }),
                        m_connections.end());
    if (m_connections.size() != before) {
        save();
        emit connectionsChanged();
    }
}

void ConnectionStore::toggleFavorite(const QString &id)
{
    for (Connection &c : m_connections) {
        if (c.id == id) {
            c.favorite = !c.favorite;
            save();
            emit connectionsChanged();
            return;
        }
    }
}

void ConnectionStore::recordUse(const QString &id, bool ok)
{
    Connection *c = nullptr;
    for (Connection &item : m_connections) {
        if (item.id == id) {
            c = &item;
            break;
        }
    }
    if (!c)
        return;

    c->lastUsedAt = QDateTime::currentDateTime();
    c->useCount  += 1;

    HistoryEntry h;
    h.connectionId = c->id;
    h.name         = c->name;
    h.host         = c->host;
    h.username     = c->username;
    h.at           = c->lastUsedAt;
    h.ok           = ok;

    // 同一个连接在历史里只占一行：把旧的摘掉，用最新时间重新放到最前面。
    // 记录的是「最近一次使用」，不是每次连接的流水账。
    for (int i = m_history.size() - 1; i >= 0; --i) {
        if (m_history.at(i).connectionId == h.connectionId)
            m_history.removeAt(i);
    }
    m_history.prepend(h);

    while (m_history.size() > kMaxHistory)
        m_history.removeLast();

    save();
    emit connectionsChanged();
    emit historyChanged();
}

// ---------------------------------------------------------------- 分组 ---

const ConnectionGroup *ConnectionStore::findGroup(const QString &id) const
{
    for (const ConnectionGroup &g : m_groups) {
        if (g.id == id)
            return &g;
    }
    return nullptr;
}

void ConnectionStore::addGroup(const QString &name, const QString &color)
{
    ConnectionGroup g(name);
    g.color = color;
    g.order = m_groups.size();
    m_groups << g;
    save();
    emit groupsChanged();
}

void ConnectionStore::updateGroup(const QString &id, const QString &name, const QString &color)
{
    for (ConnectionGroup &g : m_groups) {
        if (g.id == id) {
            g.name = name;
            g.color = color;
            save();
            emit groupsChanged();
            return;
        }
    }
}

void ConnectionStore::moveGroup(const QString &id, int delta)
{
    int index = -1;
    for (int i = 0; i < m_groups.size(); ++i) {
        if (m_groups[i].id == id) {
            index = i;
            break;
        }
    }

    const int target = index + delta;
    if (index < 0 || delta == 0 || target < 0 || target >= m_groups.size())
        return;

    // 显示顺序就是 m_groups 的下标顺序（全项目没有按 order 排序的代码），
    // 所以交换元素就生效；order 跟着重编号，保证存盘数据和数组顺序一致。
    m_groups.move(index, target);
    for (int i = 0; i < m_groups.size(); ++i)
        m_groups[i].order = i;

    save();
    emit groupsChanged();
}

void ConnectionStore::removeGroup(const QString &id, bool deleteConnections)
{
    if (deleteConnections) {
        m_connections.erase(std::remove_if(m_connections.begin(), m_connections.end(),
                                           [&id](const Connection &c) { return c.groupId == id; }),
                            m_connections.end());
    } else {
        // 不删连接的话，把它们挪到"未分组"
        for (Connection &c : m_connections) {
            if (c.groupId == id)
                c.groupId.clear();
        }
    }

    m_groups.erase(std::remove_if(m_groups.begin(), m_groups.end(),
                                  [&id](const ConnectionGroup &g) { return g.id == id; }),
                   m_groups.end());

    for (int i = 0; i < m_groups.size(); ++i)
        m_groups[i].order = i;

    save();
    emit groupsChanged();
    emit connectionsChanged();
}

// ---------------------------------------------------------------- 历史 ---

void ConnectionStore::clearHistory()
{
    m_history.clear();
    save();
    emit historyChanged();
}

} // namespace Em
