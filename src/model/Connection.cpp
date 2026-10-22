#include "model/Connection.h"
#include "model/Secret.h"

#include <QJsonArray>

namespace Em {

QString authTypeToString(AuthType t)
{
    switch (t) {
    case AuthType::Password:              return QStringLiteral("password");
    case AuthType::PublicKey:             return QStringLiteral("publickey");
    case AuthType::KeyboardInteractive:   return QStringLiteral("keyboard-interactive");
    case AuthType::Agent:                 return QStringLiteral("agent");
    }
    return QStringLiteral("password");
}

AuthType authTypeFromString(const QString &s)
{
    if (s == QLatin1String("publickey"))            return AuthType::PublicKey;
    if (s == QLatin1String("keyboard-interactive")) return AuthType::KeyboardInteractive;
    if (s == QLatin1String("agent"))                return AuthType::Agent;
    return AuthType::Password;
}

ConnectionLink ConnectionLink::fromJson(const QJsonObject &o)
{
    ConnectionLink link;
    link.name = o.value(QStringLiteral("name")).toString();
    link.url  = o.value(QStringLiteral("url")).toString();
    return link;
}

QJsonObject ConnectionLink::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("url")]  = url;
    return o;
}

Connection::Connection()
    : id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , createdAt(QDateTime::currentDateTime())
{
}

Connection Connection::create(const QString &name, const QString &host, const QString &user)
{
    Connection c;
    c.name = name;
    c.host = host;
    c.username = user;
    return c;
}

QString Connection::identity() const
{
    if (username.isEmpty())
        return host;
    return username + QLatin1Char('@') + host;
}

QString Connection::endpoint() const
{
    return host + QLatin1Char(':') + QString::number(port);
}

QString Connection::searchBlob() const
{
    return (name + QLatin1Char(' ') + host + QLatin1Char(' ') + username + QLatin1Char(' ')
            + QString::number(port) + QLatin1Char(' ') + note + QLatin1Char(' ')
            + tags.join(QLatin1Char(' ')))
        .toLower();
}

QJsonObject Connection::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")]           = id;
    o[QStringLiteral("name")]         = name;
    o[QStringLiteral("host")]         = host;
    o[QStringLiteral("port")]         = port;
    o[QStringLiteral("username")]     = username;
    o[QStringLiteral("auth")]         = authTypeToString(auth);
    o[QStringLiteral("savePassword")] = savePassword;
    // 密码按原样落盘（现在明文保存，见 model/Secret.h 的说明）
    o[QStringLiteral("password")]     = savePassword ? Secret::protect(password) : QString();
    o[QStringLiteral("keyId")]        = keyId;
    o[QStringLiteral("groupId")]      = groupId;
    o[QStringLiteral("note")]         = note;
    o[QStringLiteral("favorite")]     = favorite;
    o[QStringLiteral("tags")]         = QJsonArray::fromStringList(tags);

    QJsonArray linkArray;
    for (const ConnectionLink &link : links)
        linkArray.append(link.toJson());
    o[QStringLiteral("links")]        = linkArray;

    o[QStringLiteral("createdAt")]    = createdAt.toString(Qt::ISODate);
    o[QStringLiteral("lastUsedAt")]   = lastUsedAt.toString(Qt::ISODate);
    o[QStringLiteral("useCount")]     = useCount;
    return o;
}

Connection Connection::fromJson(const QJsonObject &o)
{
    Connection c;
    c.id           = o.value(QStringLiteral("id")).toString(c.id);
    c.name         = o.value(QStringLiteral("name")).toString();
    c.host         = o.value(QStringLiteral("host")).toString();
    c.port         = o.value(QStringLiteral("port")).toInt(22);
    c.username     = o.value(QStringLiteral("username")).toString();
    c.auth         = authTypeFromString(o.value(QStringLiteral("auth")).toString());
    c.savePassword = o.value(QStringLiteral("savePassword")).toBool(true);
    {
        const QString stored = o.value(QStringLiteral("password")).toString();
        // 没有 dpapi: 前缀的当作老版本留下的明文，原样读进来
        c.password = Secret::looksEncrypted(stored) ? Secret::unprotect(stored) : stored;
    }
    c.keyId        = o.value(QStringLiteral("keyId")).toString();
    c.groupId      = o.value(QStringLiteral("groupId")).toString();
    c.note         = o.value(QStringLiteral("note")).toString();
    c.favorite     = o.value(QStringLiteral("favorite")).toBool(false);
    for (const QJsonValue &v : o.value(QStringLiteral("tags")).toArray())
        c.tags << v.toString();
    for (const QJsonValue &v : o.value(QStringLiteral("links")).toArray()) {
        const ConnectionLink link = ConnectionLink::fromJson(v.toObject());
        if (!link.url.isEmpty())
            c.links << link;
    }
    c.createdAt    = QDateTime::fromString(o.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
    c.lastUsedAt   = QDateTime::fromString(o.value(QStringLiteral("lastUsedAt")).toString(), Qt::ISODate);
    c.useCount     = o.value(QStringLiteral("useCount")).toInt(0);
    return c;
}

QJsonObject ConnectionGroup::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")]    = id;
    o[QStringLiteral("name")]  = name;
    o[QStringLiteral("color")] = color;
    o[QStringLiteral("order")] = order;
    return o;
}

ConnectionGroup ConnectionGroup::fromJson(const QJsonObject &o)
{
    ConnectionGroup g;
    g.id    = o.value(QStringLiteral("id")).toString(g.id);
    g.name  = o.value(QStringLiteral("name")).toString();
    // 旧文件没有 color —— 取到空串，显示时回退主题色，不需要迁移
    g.color = o.value(QStringLiteral("color")).toString();
    g.order = o.value(QStringLiteral("order")).toInt(0);
    return g;
}

QJsonObject HistoryEntry::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("connectionId")] = connectionId;
    o[QStringLiteral("name")]         = name;
    o[QStringLiteral("host")]         = host;
    o[QStringLiteral("username")]     = username;
    o[QStringLiteral("at")]           = at.toString(Qt::ISODate);
    o[QStringLiteral("ok")]           = ok;
    return o;
}

HistoryEntry HistoryEntry::fromJson(const QJsonObject &o)
{
    HistoryEntry h;
    h.connectionId = o.value(QStringLiteral("connectionId")).toString();
    h.name         = o.value(QStringLiteral("name")).toString();
    h.host         = o.value(QStringLiteral("host")).toString();
    h.username     = o.value(QStringLiteral("username")).toString();
    h.at           = QDateTime::fromString(o.value(QStringLiteral("at")).toString(), Qt::ISODate);
    h.ok           = o.value(QStringLiteral("ok")).toBool(true);
    return h;
}

} // namespace Em
