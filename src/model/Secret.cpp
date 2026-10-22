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

// 旧版本 DPAPI 密文的前缀。现在不再写入，只在读取旧配置时识别。
constexpr char kPrefix[] = "dpapi:";

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

// 新数据一律原样落盘。想换回加密的话，只改这里和各 Store 里的迁移判断。
QString protect(const QString &plain)
{
    return plain;
}

#ifdef Q_OS_WIN

QString unprotect(const QString &value)
{
    // 新格式就是明文，原样返回
    if (!looksEncrypted(value))
        return value;

    const QByteArray blob = decodeBlob(value);
    if (blob.isEmpty())
        return {};

    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(blob.constData()));
    in.cbData = static_cast<DWORD>(blob.size());

    DATA_BLOB out{};
    const BOOL ok = ::CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                                         CRYPTPROTECT_UI_FORBIDDEN, &out);
    if (!ok) {
        // 最常见的原因：旧配置是从别的机器/账户拷过来的
        qWarning("EmShell: 旧版 DPAPI 密码解不开（多半是换了机器或 Windows 账户），需要重新输入");
        return {};
    }

    const QString plain = QString::fromUtf8(reinterpret_cast<const char *>(out.pbData),
                                            int(out.cbData));
    ::LocalFree(out.pbData);
    return plain;
}

#else // 非 Windows：旧版 DPAPI 密文解不开，明文照常返回

QString unprotect(const QString &value)
{
    if (!looksEncrypted(value))
        return value;
    qWarning("EmShell: 旧版 DPAPI 密文只能在 Windows 上解，需要重新输入密码");
    return {};
}

#endif

} // namespace Secret
} // namespace Em
