#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>

namespace Em {

// 认证方式。目前只有密码是真正可用的，其它几种留着，
// 等 SSH 层接上 libssh2 之后再实现。
enum class AuthType {
    Password = 0,
    PublicKey,
    KeyboardInteractive,
    Agent,
};

QString authTypeToString(AuthType t);
AuthType authTypeFromString(const QString &s);

// 连接上挂的一条网址。由「管理链接」维护，跟着连接一起落盘。
struct ConnectionLink
{
    QString name;       // 显示名；空时用 url 顶上
    QString url;

    QJsonObject toJson() const;
    static ConnectionLink fromJson(const QJsonObject &o);
};

// 一条连接记录
struct Connection
{
    QString id;
    QString name;
    QString host;
    int port = 22;
    QString username;
    AuthType auth = AuthType::Password;
    QString password;
    bool savePassword = true;
    QString keyId;              // auth == PublicKey 时，指向 KeyStore 里的一条密钥
    QString groupId;            // 空表示未分组
    QString note;
    bool favorite = false;
    QStringList tags;
    QVector<ConnectionLink> links;   // 这台机器相关的网址（后台、文档……）
    QDateTime createdAt;
    QDateTime lastUsedAt;
    int useCount = 0;

    Connection();
    static Connection create(const QString &name, const QString &host, const QString &user);

    bool isValid() const { return !host.isEmpty(); }

    // root@192.168.1.100
    QString identity() const;
    // 192.168.1.100:22
    QString endpoint() const;
    // 用于搜索匹配的一整串
    QString searchBlob() const;

    QJsonObject toJson() const;
    static Connection fromJson(const QJsonObject &o);
};

// 分组
struct ConnectionGroup
{
    QString id;
    QString name;
    QString color;      // 标记色 #rrggbb；空 = 用主题的默认蓝（老数据没有这个字段）
    int order = 0;

    ConnectionGroup() = default;
    ConnectionGroup(const QString &n) : id(QUuid::createUuid().toString(QUuid::WithoutBraces)), name(n) {}

    QJsonObject toJson() const;
    static ConnectionGroup fromJson(const QJsonObject &o);
};

// 历史记录里的一条：记录某个连接在什么时候被连过
struct HistoryEntry
{
    QString connectionId;
    QString name;       // 冗余存一份，连接被删了历史还在
    QString host;
    QString username;
    QDateTime at;
    bool ok = true;

    QJsonObject toJson() const;
    static HistoryEntry fromJson(const QJsonObject &o);
};

} // namespace Em
