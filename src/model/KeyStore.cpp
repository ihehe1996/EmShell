#include "model/KeyStore.h"
#include "model/Secret.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace Em {

SshKey::SshKey()
    : id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , addedAt(QDateTime::currentDateTime())
{
}

QJsonObject SshKey::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")]             = id;
    o[QStringLiteral("name")]           = name;
    o[QStringLiteral("privateKeyPath")] = privateKeyPath;
    o[QStringLiteral("publicKeyText")]  = publicKeyText;
    o[QStringLiteral("type")]           = type;
    o[QStringLiteral("comment")]        = comment;
    o[QStringLiteral("passphrase")]     = Secret::protect(passphrase);
    o[QStringLiteral("fingerprint")]    = fingerprint;
    o[QStringLiteral("addedAt")]        = addedAt.toString(Qt::ISODate);
    return o;
}

SshKey SshKey::fromJson(const QJsonObject &o)
{
    SshKey k;
    k.id             = o.value(QStringLiteral("id")).toString(k.id);
    k.name           = o.value(QStringLiteral("name")).toString();
    k.privateKeyPath = o.value(QStringLiteral("privateKeyPath")).toString();
    k.publicKeyText  = o.value(QStringLiteral("publicKeyText")).toString();
    k.type           = o.value(QStringLiteral("type")).toString();
    k.comment        = o.value(QStringLiteral("comment")).toString();
    {
        const QString stored = o.value(QStringLiteral("passphrase")).toString();
        k.passphrase = Secret::looksEncrypted(stored) ? Secret::unprotect(stored) : stored;
    }
    k.fingerprint    = o.value(QStringLiteral("fingerprint")).toString();
    k.addedAt        = QDateTime::fromString(o.value(QStringLiteral("addedAt")).toString(), Qt::ISODate);
    return k;
}

QString SshKey::summary() const
{
    QStringList parts;
    if (!type.isEmpty())
        parts << type;
    if (!fingerprint.isEmpty()) {
        // SHA256:abcdef… 只留前 12 位，界面上不占地方
        QString fp = fingerprint;
        if (fp.size() > 18)
            fp = fp.left(18) + QStringLiteral("…");
        parts << fp;
    }
    if (parts.isEmpty())
        return QFileInfo(privateKeyPath).fileName();
    return parts.join(QStringLiteral(" · "));
}

// -------------------------------------------------------------- KeyStore ---

KeyStore::KeyStore(QObject *parent)
    : QObject(parent)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    m_filePath = (dir.isEmpty() ? QDir::homePath() + QStringLiteral("/.emshell") : dir)
                 + QStringLiteral("/keys.json");
}

KeyStore::KeyStore(const QString &filePath, QObject *parent)
    : QObject(parent)
    , m_filePath(filePath)
{
}

QString KeyStore::keysDirectory()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.emshell");
    const QString dir = base + QStringLiteral("/keys");
    QDir().mkpath(dir);
    return dir;
}

void KeyStore::load()
{
    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        emit keysChanged();
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    // 和 ConnectionStore::load 一样：旧版的 dpapi: 密文读出来后马上重写成明文。
    // 有解不开的就保持原文件不动，免得把原密文洗掉。
    bool migrateSecrets = false;
    bool migrationFailed = false;
    m_keys.clear();
    for (const QJsonValue &v : doc.object().value(QStringLiteral("keys")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString stored = o.value(QStringLiteral("passphrase")).toString();
        if (Secret::looksEncrypted(stored)) {
            migrateSecrets = true;
            if (Secret::unprotect(stored).isEmpty())
                migrationFailed = true;
        }
        m_keys << SshKey::fromJson(o);
    }

    if (migrateSecrets && !migrationFailed)
        save();

    emit keysChanged();
}

void KeyStore::save()
{
    QJsonArray arr;
    for (const SshKey &k : m_keys)
        arr.append(k.toJson());

    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("keys")]    = arr;

    QSaveFile f(m_filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning("EmShell: 密钥索引写不进去 %s", qPrintable(m_filePath));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.commit();
}

const SshKey *KeyStore::find(const QString &id) const
{
    for (const SshKey &k : m_keys) {
        if (k.id == id)
            return &k;
    }
    return nullptr;
}

QString KeyStore::readTextFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

// 私钥第一行就能看出类型；老式 PEM 再看一眼长度兜底
QString KeyStore::detectType(const QString &privateKeyPath)
{
    const QString head = readTextFile(privateKeyPath).left(200);
    if (head.contains(QLatin1String("OPENSSH PRIVATE KEY"))) {
        // 新格式看不出算法，去 .pub 里找
        const QString pub = readTextFile(privateKeyPath + QStringLiteral(".pub"));
        if (pub.contains(QLatin1String("ssh-ed25519")))  return QStringLiteral("ed25519");
        if (pub.contains(QLatin1String("ssh-rsa")))      return QStringLiteral("rsa");
        if (pub.contains(QLatin1String("ecdsa-")))       return QStringLiteral("ecdsa");
        return QStringLiteral("openssh");
    }
    if (head.contains(QLatin1String("RSA PRIVATE KEY")))       return QStringLiteral("rsa");
    if (head.contains(QLatin1String("EC PRIVATE KEY")))        return QStringLiteral("ecdsa");
    if (head.contains(QLatin1String("DSA PRIVATE KEY")))       return QStringLiteral("dsa");
    if (head.contains(QLatin1String("PRIVATE KEY")))           return QStringLiteral("pkcs8");
    return QString();
}

QString KeyStore::findSshKeygen()
{
    // 先看 PATH，再试 Windows 自带的 OpenSSH 目录
    const QStringList candidates = {
        QStringLiteral("ssh-keygen"),
        QStringLiteral("C:/Windows/System32/OpenSSH/ssh-keygen.exe"),
    };
    for (const QString &c : candidates) {
        if (c.contains(QLatin1Char('/'))) {
            if (QFile::exists(c))
                return c;
        } else {
            const QString found = QStandardPaths::findExecutable(c);
            if (!found.isEmpty())
                return found;
        }
    }
    return {};
}

QString KeyStore::computeFingerprint(const QString &publicKeyPath)
{
    if (!QFile::exists(publicKeyPath))
        return {};

    const QString keygen = findSshKeygen();
    if (keygen.isEmpty())
        return {};

    QProcess p;
    p.start(keygen, { QStringLiteral("-l"), QStringLiteral("-f"), publicKeyPath });
    if (!p.waitForFinished(5000))
        return {};

    // 输出形如：2048 SHA256:xxxx yyy@host (RSA)
    const QString out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    const QStringList fields = out.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &f : fields) {
        if (f.startsWith(QLatin1String("SHA256:")) || f.startsWith(QLatin1String("MD5:")))
            return f;
    }
    return {};
}

bool KeyStore::importFromFile(const QString &privateKeyPath, QString *errorOut)
{
    const QFileInfo info(privateKeyPath);
    if (!info.exists() || !info.isFile()) {
        if (errorOut) *errorOut = QStringLiteral("找不到文件：%1").arg(privateKeyPath);
        return false;
    }

    const QString head = readTextFile(privateKeyPath).left(200);
    if (!head.contains(QLatin1String("PRIVATE KEY"))) {
        if (errorOut)
            *errorOut = QStringLiteral("这看起来不是私钥文件（没找到 PRIVATE KEY 标记）");
        return false;
    }

    // 已经导入过就直接复用，不重复添加
    for (const SshKey &existing : m_keys) {
        if (QFileInfo(existing.privateKeyPath) == info) {
            if (errorOut) *errorOut = QStringLiteral("这个密钥已经导入过了：%1").arg(existing.name);
            return false;
        }
    }

    SshKey k;
    k.name           = info.completeBaseName();
    k.privateKeyPath = info.absoluteFilePath();
    k.type           = detectType(k.privateKeyPath);

    const QString pubPath = k.privateKeyPath + QStringLiteral(".pub");
    k.publicKeyText = readTextFile(pubPath);
    k.fingerprint   = computeFingerprint(pubPath);

    // 注释就是 .pub 的最后一列
    if (!k.publicKeyText.isEmpty()) {
        const QStringList parts = k.publicKeyText.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 3)
            k.comment = parts.mid(2).join(QLatin1Char(' '));
    }

    m_keys.prepend(k);
    save();
    emit keysChanged();
    return true;
}

bool KeyStore::generate(const QString &name, const QString &type,
                        const QString &comment, QString *errorOut)
{
    const QString keygen = findSshKeygen();
    if (keygen.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("找不到 ssh-keygen，请确认系统装了 OpenSSH 客户端");
        return false;
    }

    QString safe = name.trimmed();
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    if (safe.isEmpty())
        safe = QStringLiteral("id_%1").arg(type);

    const QString target = keysDirectory() + QLatin1Char('/') + safe;
    if (QFile::exists(target)) {
        if (errorOut)
            *errorOut = QStringLiteral("同名密钥已存在：%1").arg(target);
        return false;
    }

    // -N "" 表示不设口令。留空口令在本地开发机上很常见，
    // 真要口令的话用户可以自己生成好再导入。
    QProcess p;
    p.start(keygen, { QStringLiteral("-t"), type,
                      QStringLiteral("-f"), target,
                      QStringLiteral("-N"), QString(),
                      QStringLiteral("-C"), comment.isEmpty() ? safe : comment });
    if (!p.waitForFinished(15000)) {
        p.kill();
        if (errorOut) *errorOut = QStringLiteral("ssh-keygen 超时了");
        return false;
    }
    if (p.exitCode() != 0) {
        if (errorOut) {
            *errorOut = QStringLiteral("ssh-keygen 失败：%1")
                            .arg(QString::fromUtf8(p.readAllStandardError()).trimmed());
        }
        return false;
    }

    QString ignored;
    return importFromFile(target, &ignored);
}

void KeyStore::remove(const QString &id)
{
    const auto before = m_keys.size();
    m_keys.erase(std::remove_if(m_keys.begin(), m_keys.end(),
                                [&id](const SshKey &k) { return k.id == id; }),
                 m_keys.end());
    if (m_keys.size() != before) {
        save();
        emit keysChanged();
    }
}

void KeyStore::update(const SshKey &key)
{
    for (SshKey &existing : m_keys) {
        if (existing.id == key.id) {
            existing = key;
            save();
            emit keysChanged();
            return;
        }
    }
}

void KeyStore::importSnapshot(const QVector<SshKey> &keys, bool replace)
{
    if (replace) {
        m_keys = keys;
    } else {
        for (const SshKey &incoming : keys) {
            bool found = false;
            for (SshKey &existing : m_keys) {
                if (existing.id == incoming.id) {
                    existing = incoming;
                    found = true;
                    break;
                }
            }
            if (!found)
                m_keys << incoming;
        }
    }

    save();
    emit keysChanged();
}

} // namespace Em
