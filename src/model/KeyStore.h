#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

namespace Em {

// 一条 SSH 密钥记录。
//
// 私钥本身不动，还是放在原路径（私钥文件权限敏感，拷来拷去容易出事），
// 这里只记路径和公钥内容，方便界面展示和后面连接时引用。
struct SshKey
{
    QString id;
    QString name;
    QString privateKeyPath;
    QString publicKeyText;      // .pub 文件的内容，可能为空
    QString type;               // ed25519 / rsa / ecdsa ...
    QString comment;
    QString passphrase;         // 明文保存，写盘前走一次 Secret::protect()
    QString fingerprint;        // 形如 SHA256:xxxx，调用 ssh-keygen 算出来的
    QDateTime addedAt;

    SshKey();

    QJsonObject toJson() const;
    static SshKey fromJson(const QJsonObject &o);

    // 展示用的简写，形如 "ed25519 · SHA256:9x2f…"
    QString summary() const;
};

// 密钥仓库。和 ConnectionStore 一样，改完自动落盘。
class KeyStore : public QObject
{
    Q_OBJECT

public:
    explicit KeyStore(QObject *parent = nullptr);

    void load();
    void save();

    const QVector<SshKey> &keys() const { return m_keys; }
    const SshKey *find(const QString &id) const;

    // 从已有的私钥文件导入；同名 .pub 存在的话一起读进来
    bool importFromFile(const QString &privateKeyPath, QString *errorOut = nullptr);

    // 调系统的 ssh-keygen 新生成一对，放进 %APPDATA%/EmShell/keys/
    bool generate(const QString &name, const QString &type,
                  const QString &comment, QString *errorOut = nullptr);

    void remove(const QString &id);
    void update(const SshKey &key);

    // 删除时是否连磁盘上的私钥文件一起删
    static QString keysDirectory();

signals:
    void keysChanged();

private:
    static QString readTextFile(const QString &path);
    static QString detectType(const QString &privateKeyPath);
    static QString computeFingerprint(const QString &publicKeyPath);
    static QString findSshKeygen();

    QVector<SshKey> m_keys;
    QString m_filePath;
};

} // namespace Em
