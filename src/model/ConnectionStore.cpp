#include "model/ConnectionStore.h"

#include "model/Secret.h"

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

ConnectionStore::ConnectionStore(const QString &filePath, QObject *parent)
    : QObject(parent)
    , m_filePath(filePath)
{
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
        // 首次运行：连接 / 历史都从空开始，只预置两个常用分组。
        // 这里**不要**塞示例连接 —— 程序是打包发出去的，新用户装好一打开
        // 就该看到空的「所有连接」，而不是一堆 192.168.x.x 的假条目。
        seedDefaultGroups();
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

    // 旧版本把密码存成 dpapi: 密文。现在统一存明文，加载时顺手把旧文件
    // 重写一遍 —— 否则用户不编辑连接的话，文件里会一直留着密文，
    // 导出 / 换机器都不方便。
    //
    // **只要有一条解不开就不要重写**：那多半是从别的机器 / 账户拷过来的，
    // 原密文拷回原机器还能用；重写会把解不开的那几条洗成空密码。
    bool migrateSecrets = false;
    bool migrationFailed = false;

    m_connections.clear();
    for (const QJsonValue &v : root.value(QStringLiteral("connections")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString stored = o.value(QStringLiteral("password")).toString();
        if (Secret::looksEncrypted(stored)) {
            migrateSecrets = true;
            // dpapi: 前缀只可能来自非空密码（空密码不加密），解出空串就是失败
            if (Secret::unprotect(stored).isEmpty())
                migrationFailed = true;
        }
        m_connections << Connection::fromJson(o);
    }

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

    if (migrateSecrets && !migrationFailed)
        save();

    emit connectionsChanged();
    emit groupsChanged();
    emit historyChanged();
}

void ConnectionStore::importSnapshot(const QVector<Connection> &connections,
                                     const QVector<ConnectionGroup> &groups,
                                     const QVector<HistoryEntry> &history,
                                     bool replace)
{
    if (replace) {
        m_connections = connections;
        m_groups = groups;
        m_history = history;
    } else {
        // 分组：同 id 的更新名字 / 颜色，缺的追加。
        // order 先不定，最后统一重排，免得导入的 order 和现有的撞在一起。
        for (const ConnectionGroup &incoming : groups) {
            bool found = false;
            for (ConnectionGroup &existing : m_groups) {
                if (existing.id == incoming.id) {
                    existing.name = incoming.name;
                    existing.color = incoming.color;
                    found = true;
                    break;
                }
            }
            if (!found)
                m_groups << incoming;
        }

        // 连接：同 id 整体覆盖，缺的追加
        for (const Connection &incoming : connections) {
            bool found = false;
            for (Connection &existing : m_connections) {
                if (existing.id == incoming.id) {
                    existing = incoming;
                    found = true;
                    break;
                }
            }
            if (!found)
                m_connections << incoming;
        }

        // 历史：每个连接只留最近一条，和 recordUse() 的语义一致
        for (const HistoryEntry &incoming : history) {
            bool found = false;
            for (HistoryEntry &existing : m_history) {
                if (existing.connectionId == incoming.connectionId) {
                    if (incoming.at > existing.at)
                        existing = incoming;
                    found = true;
                    break;
                }
            }
            if (!found)
                m_history << incoming;
        }
    }

    // 历史永远按时间从新到旧，只留最近 kMaxHistory 条
    std::stable_sort(m_history.begin(), m_history.end(),
                     [](const HistoryEntry &a, const HistoryEntry &b) {
                         return a.at > b.at;
                     });

    // order 统一成 0..n-1，下拉框 / 侧栏都按它排序
    std::stable_sort(m_groups.begin(), m_groups.end(),
                     [](const ConnectionGroup &a, const ConnectionGroup &b) {
                         return a.order < b.order;
                     });
    for (int i = 0; i < m_groups.size(); ++i)
        m_groups[i].order = i;

    if (m_history.size() > kMaxHistory)
        m_history.resize(kMaxHistory);

    save();
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

// ------------------------------------------------------------ 默认分组 ---

void ConnectionStore::seedDefaultGroups()
{
    // 只给分组，不给连接：新装的程序打开「所有连接」还是空的，
    // 但侧栏已经有两个能直接往里放东西的常用分组。
    // 颜色取的是 GroupDialog 色板里的前两个中间调（模型层不引 UI 头文件）。
    ConnectionGroup prod(QStringLiteral("生产环境"));
    prod.color = QStringLiteral("#4F8CFF");
    prod.order = 0;

    ConnectionGroup test(QStringLiteral("测试环境"));
    test.color = QStringLiteral("#9B6BE8");
    test.order = 1;

    m_groups << prod << test;
}

// ---------------------------------------------------------------- 历史 ---

void ConnectionStore::clearHistory()
{
    m_history.clear();
    save();
    emit historyChanged();
}

} // namespace Em
