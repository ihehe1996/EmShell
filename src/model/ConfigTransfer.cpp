#include "model/ConfigTransfer.h"

#include "app/AppConfig.h"
#include "model/ConnectionStore.h"
#include "model/KeyStore.h"
#include "model/Secret.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

namespace Em {
namespace ConfigTransfer {

namespace {

// 导出文件的身份证。导入时认这个字段，免得把随便一个 JSON 导进来。
constexpr char kFormat[] = "emshell-config";
// 格式版本。以后改结构时 +1，并在 readImportFile 里兼容旧版本。
constexpr int kFormatVersion = 1;

void setError(QString *errorOut, const QString &text)
{
    if (errorOut)
        *errorOut = text;
}

} // namespace

Counts countsOf(const ImportData &data)
{
    return { int(data.connections.size()), int(data.groups.size()),
             int(data.history.size()), int(data.keys.size()) };
}

bool writeExportFile(const QString &path, const ConnectionStore &connections,
                     const KeyStore &keys, QString *errorOut)
{
    QJsonArray connArray;
    for (const Connection &c : connections.connections())
        connArray.append(c.toJson());

    QJsonArray groupArray;
    for (const ConnectionGroup &g : connections.groups())
        groupArray.append(g.toJson());

    QJsonArray historyArray;
    for (const HistoryEntry &h : connections.history())
        historyArray.append(h.toJson());

    QJsonArray keyArray;
    for (const SshKey &k : keys.keys())
        keyArray.append(k.toJson());

    QJsonObject root;
    root[QStringLiteral("format")]       = QString::fromLatin1(kFormat);
    root[QStringLiteral("version")]      = kFormatVersion;
    root[QStringLiteral("appVersion")]   = appVersion();
    root[QStringLiteral("exportedAt")]   = QDateTime::currentDateTime().toString(Qt::ISODate);
    root[QStringLiteral("connections")]  = connArray;
    root[QStringLiteral("groups")]       = groupArray;
    root[QStringLiteral("history")]      = historyArray;
    root[QStringLiteral("keys")]         = keyArray;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setError(errorOut, QStringLiteral("打不开要写入的文件：%1").arg(path));
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        setError(errorOut, QStringLiteral("写入失败：%1").arg(file.errorString()));
        return false;
    }
    return true;
}

bool readImportFile(const QString &path, ImportData *out, QString *errorOut)
{
    if (!out) {
        setError(errorOut, QStringLiteral("内部错误：没有接收数据的位置"));
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(errorOut, QStringLiteral("打不开文件：%1").arg(path));
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        setError(errorOut, QStringLiteral("不是有效的 JSON：%1").arg(parseError.errorString()));
        return false;
    }

    const QJsonObject root = doc.object();

    // format 缺省时也接受：允许手写 / 老版本裸 connections.json。
    // 但只要写了 format，就必须对得上。
    const QString format = root.value(QStringLiteral("format")).toString();
    if (!format.isEmpty() && format != QString::fromLatin1(kFormat)) {
        setError(errorOut, QStringLiteral("这不是 EmShell 导出的配置文件。"));
        return false;
    }

    const int version = root.value(QStringLiteral("version")).toInt(0);
    if (version > kFormatVersion) {
        setError(errorOut,
                 QStringLiteral("配置文件版本太新（%1），请先升级 EmShell。").arg(version));
        return false;
    }

    ImportData data;

    for (const QJsonValue &v : root.value(QStringLiteral("groups")).toArray()) {
        const QJsonObject o = v.toObject();
        ConnectionGroup g = ConnectionGroup::fromJson(o);
        if (g.name.trimmed().isEmpty())
            continue;
        data.groups << g;
    }

    for (const QJsonValue &v : root.value(QStringLiteral("connections")).toArray()) {
        const QJsonObject o = v.toObject();
        // 没有主机的行导进来也没法用，跳过
        if (o.value(QStringLiteral("host")).toString().trimmed().isEmpty())
            continue;
        data.connections << Connection::fromJson(o);
    }

    for (const QJsonValue &v : root.value(QStringLiteral("history")).toArray()) {
        HistoryEntry h = HistoryEntry::fromJson(v.toObject());
        if (h.connectionId.isEmpty())
            continue;
        data.history << h;
    }

    for (const QJsonValue &v : root.value(QStringLiteral("keys")).toArray()) {
        SshKey k = SshKey::fromJson(v.toObject());
        // 没有路径的密钥记录没有任何用处
        if (k.privateKeyPath.trimmed().isEmpty())
            continue;
        data.keys << k;
    }

    if (data.connections.isEmpty() && data.groups.isEmpty()
        && data.history.isEmpty() && data.keys.isEmpty()) {
        setError(errorOut, QStringLiteral("文件里没有可导入的数据。"));
        return false;
    }

    *out = data;
    return true;
}

} // namespace ConfigTransfer
} // namespace Em
