#include "rdp/RdpLauncher.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QUuid>

#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dpapi.h>

namespace Em {
namespace RdpLauncher {

namespace {

// .rdp 是 INI 风格的一行一个键。值里混进换行会把整个文件拆坏，
// 用户名又是唯一一个原样进去的自由文本，这里先清一遍。
QString sanitizeValue(const QString &value)
{
    QString cleaned = value;
    cleaned.remove(QLatin1Char('\r'));
    cleaned.remove(QLatin1Char('\n'));
    return cleaned;
}

// IPv6 地址在 full address 里要加方括号，否则冒号会把端口切错
QString addressFor(const Connection &c)
{
    QString host = c.host.trimmed();
    if (host.contains(QLatin1Char(':')) && !(host.startsWith(QLatin1Char('['))
                                             && host.endsWith(QLatin1Char(']')))) {
        host = QLatin1Char('[') + host + QLatin1Char(']');
    }
    const int port = c.port > 0 ? c.port : defaultPortFor(c.protocol);
    return host + QLatin1Char(':') + QString::number(port);
}

QString programPath()
{
    // QStandardPaths 返回正斜杠路径，QProcess 能直接用
    const QString found = QStandardPaths::findExecutable(QStringLiteral("mstsc"));
    return found.isEmpty() ? QStringLiteral("mstsc") : found;
}

void removeTempDir(const QString &dir)
{
    if (!dir.isEmpty())
        QDir(dir).removeRecursively();
}

} // namespace

bool protectPassword(const QString &password, QString *hex, QString *errorOut)
{
    if (hex)
        hex->clear();

    // mstsc 把密码按 UTF-16LE 的字节送进 DPAPI，**不是 UTF-8**。
    // 用 UTF-8 加密出来的密文 mstsc 解不出来，表现为照样弹密码框。
    const std::wstring wide = password.toStdWString();
    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE *>(const_cast<wchar_t *>(wide.data()));
    in.cbData = DWORD(wide.size() * sizeof(wchar_t));

    DATA_BLOB out{};
    // 描述固定是 "psw"（mstsc 自己也是这么调的），entropy 为 NULL。
    if (!CryptProtectData(&in, L"psw", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        if (errorOut)
            *errorOut = QStringLiteral("加密密码失败（DPAPI 错误码 %1）").arg(GetLastError());
        return false;
    }

    const QByteArray blob(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
    LocalFree(out.pbData);

    if (hex)
        *hex = QString::fromLatin1(blob.toHex().toUpper());
    return true;
}

QString buildRdpFileContent(const Connection &connection,
                            const QString &protectedPassword)
{
    const Connection &c = connection;
    QStringList lines;

    // screen mode id：1 = 窗口，2 = 全屏。窗口模式下给一个宽一点的默认值，
    // mstsc 会按 DPI 自己缩放。
    lines << (c.rdpFullscreen ? QStringLiteral("screen mode id:i:2")
                              : QStringLiteral("screen mode id:i:1"));
    if (!c.rdpFullscreen) {
        lines << QStringLiteral("desktopwidth:i:1600");
        lines << QStringLiteral("desktopheight:i:900");
    }
    lines << QStringLiteral("use multimon:i:%1").arg(c.rdpMultiMon ? 1 : 0);
    lines << QStringLiteral("session bpp:i:32");

    lines << QStringLiteral("full address:s:%1").arg(addressFor(c));

    if (!c.username.trimmed().isEmpty())
        lines << QStringLiteral("username:s:%1").arg(sanitizeValue(c.username.trimmed()));

    // 密码密文按 mstsc 的格式贴在 51 后面；空串表示让 mstsc 自己弹框
    if (!protectedPassword.isEmpty())
        lines << QStringLiteral("password 51:b:%1").arg(protectedPassword);

    // 有密码时不希望它再弹一次凭据框；没有密码时 mstsc 该弹还是会弹
    lines << QStringLiteral("prompt for credentials:i:0");
    lines << QStringLiteral("promptcredentialonce:i:0");
    lines << QStringLiteral("authentication level:i:2");
    lines << QStringLiteral("negotiate security layer:i:1");
    lines << QStringLiteral("enablecredsspsupport:i:1");
    lines << QStringLiteral("redirectclipboard:i:1");
    lines << QStringLiteral("audiomode:i:0");

    if (c.rdpAdmin)
        lines << QStringLiteral("administrative session:i:1");

    return lines.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n");
}

bool launch(const Connection &connection, QString *errorOut, QString *warningOut)
{
    if (errorOut)
        errorOut->clear();
    if (warningOut)
        warningOut->clear();

    QString protectedPassword;
    if (!connection.password.isEmpty()) {
        QString protectError;
        if (!protectPassword(connection.password, &protectedPassword, &protectError)) {
            // 加密不了就别把密码写进去，让它退化成「mstsc 自己弹框」，
            // 这比直接拒绝连接好 —— 用户至少还能手动输。
            protectedPassword.clear();
            if (warningOut) {
                *warningOut = QStringLiteral("%1，请在远程桌面窗口里手动输入密码。")
                                  .arg(protectError);
            }
        }
    }

    const QString content = buildRdpFileContent(connection, protectedPassword);

    // 每次连接一个独一无二的临时目录，文件本身按连接名命名。
    //
    // 目录名才是唯一的部分；文件叫「自检 10.0.0.1」这种，是因为 mstsc 的窗口
    // 标题直接取 .rdp 的文件名（标题栏 = 文件名 - 地址 - 远程桌面连接）。
    // 用 GUID 当文件名的话，任务栏上就是一串 emshell-3d61a1...，完全看不出连的是哪台机器。
    QString base = connection.name.trimmed();
    if (base.isEmpty())
        base = connection.host;
    // 注意连点也要换掉：mstsc 的窗口标题会在第一个点处截断
    // （"自检 189.24.78.196.rdp" 会显示成 "自检 189"），换成下划线才完整。
    base.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|.\\x00-\\x1f]")),
                 QStringLiteral("_"));
    base = base.trimmed();
    while (base.endsWith(QLatin1Char('.')) || base.endsWith(QLatin1Char(' ')))
        base.chop(1);
    if (base.isEmpty())
        base = QStringLiteral("远程桌面");

    // Windows 的设备名当文件名会直接打不开（CON、PRN、COM1……），
    // 加个前缀绕开，不改用户看到的名字。
    const QString upper = base.toUpper();
    bool reserved = upper == QLatin1String("CON") || upper == QLatin1String("PRN")
                    || upper == QLatin1String("AUX") || upper == QLatin1String("NUL");
    for (int i = 1; i <= 9 && !reserved; ++i) {
        reserved = upper == QStringLiteral("COM%1").arg(i)
                   || upper == QStringLiteral("LPT%1").arg(i);
    }
    if (reserved)
        base.prepend(QStringLiteral("EmShell-"));
    if (base.size() > 48)
        base.truncate(48);

    const QString dirPath = QDir(QDir::tempPath()).filePath(
        QStringLiteral("emshell-rdp-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!QDir().mkpath(dirPath)) {
        if (errorOut)
            *errorOut = QStringLiteral("建不了临时目录：%1").arg(dirPath);
        return false;
    }

    const QString filePath = QDir(dirPath).filePath(base + QStringLiteral(".rdp"));
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut)
            *errorOut = QStringLiteral("写不了临时 .rdp 文件：%1").arg(filePath);
        removeTempDir(dirPath);
        return false;
    }

    // mstsc 自己保存的 .rdp 是 UTF-16LE + BOM + CRLF（Windows 上是原生字节序），
    // 这里照着来，中文用户名才不会乱码。
    QByteArray payload;
    payload.append(char(0xFF));
    payload.append(char(0xFE));
    payload.append(reinterpret_cast<const char *>(content.utf16()),
                   content.size() * int(sizeof(char16_t)));
    const bool written = (file.write(payload) == payload.size());
    file.close();
    if (!written) {
        if (errorOut)
            *errorOut = QStringLiteral("写临时 .rdp 文件时出错：%1").arg(filePath);
        removeTempDir(dirPath);
        return false;
    }

    // startDetached：mstsc 是独立进程，关 EmShell 不会把它一起关掉。
    // 远程桌面本来就是一个外部窗口，用户关它才算结束会话。
    if (!QProcess::startDetached(programPath(),
                                 {QDir::toNativeSeparators(filePath)})) {
        if (errorOut)
            *errorOut = QStringLiteral("启动远程桌面客户端失败：%1").arg(programPath());
        removeTempDir(dirPath);
        return false;
    }

    // mstsc 启动时就把 .rdp 读完了，过几秒把临时文件删掉。
    // 密码密文虽然只对本机当前账户有效，也没必要留在 %TEMP% 里。
    QTimer::singleShot(3000, [dirPath] { removeTempDir(dirPath); });
    return true;
}

} // namespace RdpLauncher
} // namespace Em
