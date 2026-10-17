#include "model/Secret.h"

#include <QByteArray>
#include <QString>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <dpapi.h>
#endif

namespace Em {
namespace Secret {

namespace {

// 密文统一加这个前缀，方便和旧版本直接存的明文区分开
constexpr char kPrefix[] = "dpapi:";

QString encodeBlob(const QByteArray &raw)
{
    return QString::fromLatin1(kPrefix) + QString::fromLatin1(raw.toBase64());
}

QByteArray decodeBlob(const QString &value)
{
    if (!value.startsWith(QLatin1String(kPrefix)))
        return {};
    return QByteArray::fromBase64(value.mid(int(sizeof(kPrefix)) - 1).toLatin1());
}

} // namespace

bool looksEncrypted(const QString &value)
{
    return value.startsWith(QLatin1String(kPrefix));
}

#ifdef Q_OS_WIN

QString protect(const QString &plain)
{
    if (plain.isEmpty())
        return {};

    const QByteArray utf8 = plain.toUtf8();

    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(utf8.constData()));
    in.cbData = static_cast<DWORD>(utf8.size());

    DATA_BLOB out{};
    const BOOL ok = ::CryptProtectData(&in,
                                       L"EmShell connection secret",
                                       nullptr, nullptr, nullptr,
                                       CRYPTPROTECT_UI_FORBIDDEN,
                                       &out);
    if (!ok) {
        qWarning("EmShell: CryptProtectData 失败，密码没有保存");
        return {};
    }

    const QByteArray blob(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
    ::LocalFree(out.pbData);
    return encodeBlob(blob);
}

QString unprotect(const QString &encoded)
{
    const QByteArray blob = decodeBlob(encoded);
    if (blob.isEmpty())
        return {};

    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(blob.constData()));
    in.cbData = static_cast<DWORD>(blob.size());

    DATA_BLOB out{};
    const BOOL ok = ::CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                                         CRYPTPROTECT_UI_FORBIDDEN, &out);
    if (!ok) {
        // 最常见的原因：配置文件是从别的机器/别的账户拷过来的
        qWarning("EmShell: 密码解不开（多半是换了机器或换了 Windows 账户），需要重新输入");
        return {};
    }

    const QString plain = QString::fromUtf8(reinterpret_cast<const char *>(out.pbData),
                                            int(out.cbData));
    ::LocalFree(out.pbData);
    return plain;
}

#else // 非 Windows：明确退化，不假装安全

QString protect(const QString &plain)
{
    if (plain.isEmpty())
        return {};
    return encodeBlob(plain.toUtf8());
}

QString unprotect(const QString &encoded)
{
    const QByteArray blob = decodeBlob(encoded);
    return QString::fromUtf8(blob);
}

#endif

} // namespace Secret
} // namespace Em
