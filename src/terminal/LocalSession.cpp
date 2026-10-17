#include "terminal/LocalSession.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <vector>

namespace Em {

namespace {

HANDLE toHandle(void *p) { return reinterpret_cast<HANDLE>(p); }
void  *toVoid(HANDLE h)  { return reinterpret_cast<void *>(h); }

// 按顺序找一个存在的可执行文件
QString findFirst(const QStringList &names)
{
    for (const QString &n : names) {
        const QString found = QStandardPaths::findExecutable(n);
        if (!found.isEmpty())
            return found;
    }
    return {};
}

} // namespace

LocalSession::LocalSession(const QString &shellCommand, QObject *parent)
    : TerminalSession(parent)
    , m_command(shellCommand)
{
}

LocalSession::~LocalSession()
{
    shutdown();
}

QString LocalSession::defaultShell()
{
    return findFirst({ QStringLiteral("pwsh"),
                       QStringLiteral("powershell"),
                       QStringLiteral("cmd") });
}

QStringList LocalSession::availableShells()
{
    QStringList out;
    const QString pwsh = QStandardPaths::findExecutable(QStringLiteral("pwsh"));
    if (!pwsh.isEmpty())
        out << pwsh;

    const QString ps = QStandardPaths::findExecutable(QStringLiteral("powershell"));
    if (!ps.isEmpty())
        out << ps;

    const QString cmd = QStandardPaths::findExecutable(QStringLiteral("cmd"));
    if (!cmd.isEmpty())
        out << cmd;

    return out;
}

QString LocalSession::description() const
{
    if (m_command.isEmpty())
        return QStringLiteral("本地终端");
    return QStringLiteral("本地终端 · %1").arg(QFileInfo(m_command).baseName());
}

bool LocalSession::start(int cols, int rows, QString *errorOut)
{
    if (m_running.load())
        return true;

    m_cols = qMax(1, cols);
    m_rows = qMax(1, rows);
    m_stopRequested = false;

    auto fail = [errorOut](const QString &what) {
        if (errorOut)
            *errorOut = what;
        return false;
    };

    SECURITY_ATTRIBUTES sa{};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE inRead = nullptr, inWrite = nullptr;
    HANDLE outRead = nullptr, outWrite = nullptr;

    if (!::CreatePipe(&inRead, &inWrite, &sa, 0))
        return fail(QStringLiteral("创建输入管道失败（错误码 %1）").arg(::GetLastError()));

    if (!::CreatePipe(&outRead, &outWrite, &sa, 0)) {
        ::CloseHandle(inRead);
        ::CloseHandle(inWrite);
        return fail(QStringLiteral("创建输出管道失败（错误码 %1）").arg(::GetLastError()));
    }

    // 我们这端用的两个句柄不能被继承，否则子进程退出后管道关不掉
    ::SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

    // ---- 建伪控制台 ----
    HPCON pc = nullptr;
    const COORD size{ static_cast<SHORT>(m_cols), static_cast<SHORT>(m_rows) };
    const HRESULT hr = ::CreatePseudoConsole(size, inRead, outWrite, 0, &pc);
    if (FAILED(hr)) {
        ::CloseHandle(inRead);
        ::CloseHandle(inWrite);
        ::CloseHandle(outRead);
        ::CloseHandle(outWrite);
        return fail(QStringLiteral(
            "当前系统不支持 ConPTY 伪控制台，本地终端需要 Windows 10 1809 或更高版本。"));
    }

    // ConPTY 内部已经复制了这两个句柄，我们这边的副本可以关了
    ::CloseHandle(inRead);
    ::CloseHandle(outWrite);
    inRead = outWrite = nullptr;

    // 建完之后立刻按同样尺寸 resize 一次。看着多余，但这一步不能省：
    // ConPTY 在收到第一次 ResizePseudoConsole 之前可能一直攒着子进程的输出
    // 不往管道里写，表现为「只收到初始化序列，命令行一个字都收不到」。
    ::ResizePseudoConsole(pc, size);

    // ---- 建进程 ----
    SIZE_T attrBytes = 0;
    ::InitializeProcThreadAttributeList(nullptr, 1, 0, &attrBytes);
    std::vector<BYTE> attrStorage(attrBytes);
    auto *attrList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrStorage.data());

    if (!::InitializeProcThreadAttributeList(attrList, 1, 0, &attrBytes)) {
        ::ClosePseudoConsole(pc);
        ::CloseHandle(inWrite);
        ::CloseHandle(outRead);
        return fail(QStringLiteral("初始化进程属性列表失败"));
    }

    const BOOL attrOk = ::UpdateProcThreadAttribute(attrList, 0,
                                                    PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                                    pc, sizeof(pc), nullptr, nullptr);
    if (!attrOk) {
        ::DeleteProcThreadAttributeList(attrList);
        ::ClosePseudoConsole(pc);
        ::CloseHandle(inWrite);
        ::CloseHandle(outRead);
        return fail(QStringLiteral("绑定伪控制台到进程属性失败"));
    }

    const QString shell = m_command.isEmpty() ? defaultShell() : m_command;
    if (shell.isEmpty()) {
        ::DeleteProcThreadAttributeList(attrList);
        ::ClosePseudoConsole(pc);
        ::CloseHandle(inWrite);
        ::CloseHandle(outRead);
        return fail(QStringLiteral("系统里找不到可用的 shell"));
    }

    // QStandardPaths 返回的是正斜杠路径（C:/WINDOWS/system32/cmd.exe）。
    // CreateProcess 在 lpApplicationName 为空时会按命令行解析；cmd.exe 不认
    // 命令行里的正斜杠，启动后会报「子目录或文件 .exe 已经存在。」，
    // 而且不进入提示符。PowerShell 恰好不炸，所以这个 bug 只在 cmd 下露出来。
    //
    // 是真实文件路径就转成反斜杠、加引号，并把 lpApplicationName 明确传进去；
    // 否则当成命令串（退回系统 ssh 客户端的 "ssh -p ... user@host"）原样处理。
    const QFileInfo shellInfo(shell);
    const bool isExecutablePath = shellInfo.isFile();
    const QString appPath = isExecutablePath
                                ? QDir::toNativeSeparators(shellInfo.absoluteFilePath())
                                : QString();
    const QString commandLine = isExecutablePath
                                    ? QStringLiteral("\"%1\"").arg(appPath)
                                    : shell;

    std::wstring appName = appPath.toStdWString();
    std::wstring cmdLine = commandLine.toStdWString();

    STARTUPINFOEXW siEx{};
    siEx.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    siEx.lpAttributeList = attrList;

    // 这一步是整个 ConPTY 里最容易踩的坑，务必保留。
    //
    // 不显式指定标准句柄的话，子进程会继承我们自己的 stdin/stdout/stderr。
    // 后果：shell 的 stdin 是父进程的管道（多半立刻就是 EOF），一启动就退出；
    // 它的 stdout 也写进了父进程的控制台，ConPTY 那边反而收不到任何东西。
    // 表现出来就是「伪控制台建成功了，却一个字都读不到」。
    //
    // 置成 NULL 表示不继承，让子进程去用它自己那个（伪）控制台。
    siEx.StartupInfo.dwFlags     = STARTF_USESTDHANDLES;
    siEx.StartupInfo.hStdInput   = nullptr;
    siEx.StartupInfo.hStdOutput  = nullptr;
    siEx.StartupInfo.hStdError   = nullptr;

    PROCESS_INFORMATION pi{};
    const BOOL created = ::CreateProcessW(appPath.isEmpty() ? nullptr : appName.c_str(),
                                          cmdLine.data(),
                                          nullptr, nullptr,
                                          FALSE,
                                          EXTENDED_STARTUPINFO_PRESENT,
                                          nullptr, nullptr,
                                          &siEx.StartupInfo,
                                          &pi);

    ::DeleteProcThreadAttributeList(attrList);

    if (!created) {
        const DWORD err = ::GetLastError();
        ::ClosePseudoConsole(pc);
        ::CloseHandle(inWrite);
        ::CloseHandle(outRead);
        return fail(QStringLiteral("启动 %1 失败（错误码 %2）").arg(shell).arg(err));
    }

    ::CloseHandle(pi.hThread);

    // ---- 作业对象：保证主程序关掉时 shell 及其子进程一起结束 ----
    HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info))
            || !::AssignProcessToJobObject(job, pi.hProcess)) {
            ::CloseHandle(job);
            job = nullptr;
        }
    }

    m_pseudoConsole = toVoid(pc);
    m_inputWrite    = toVoid(inWrite);
    m_outputRead    = toVoid(outRead);
    m_process       = toVoid(pi.hProcess);
    m_job           = toVoid(job);

    m_running = true;
    m_reader = std::thread([this] { readLoop(); });

    emit started();
    return true;
}

void LocalSession::readLoop()
{
    std::vector<char> buffer(16 * 1024);
    const HANDLE hRead = toHandle(m_outputRead);

    while (!m_stopRequested.load()) {
        DWORD got = 0;
        const BOOL ok = ::ReadFile(hRead, buffer.data(),
                                   static_cast<DWORD>(buffer.size()), &got, nullptr);
        if (!ok || got == 0)
            break;   // 管道断了 = 子进程结束了

        emit dataReceived(QByteArray(buffer.data(), static_cast<int>(got)));
    }

    m_running = false;

    // 收一下退出码再通知界面
    DWORD exitCode = 0;
    if (m_process) {
        ::WaitForSingleObject(toHandle(m_process), 3000);
        ::GetExitCodeProcess(toHandle(m_process), &exitCode);
    }
    emit exited(static_cast<int>(exitCode));
}

void LocalSession::write(const QByteArray &data)
{
    if (!m_inputWrite || data.isEmpty() || !m_running.load())
        return;

    DWORD written = 0;
    const char *p = data.constData();
    qsizetype remaining = data.size();
    while (remaining > 0) {
        if (!::WriteFile(toHandle(m_inputWrite), p, static_cast<DWORD>(remaining), &written, nullptr))
            break;
        if (written == 0)
            break;
        p += written;
        remaining -= written;
    }
}

void LocalSession::resize(int cols, int rows)
{
    cols = qMax(1, cols);
    rows = qMax(1, rows);
    if (cols == m_cols && rows == m_rows)
        return;

    m_cols = cols;
    m_rows = rows;

    if (m_pseudoConsole) {
        const COORD size{ static_cast<SHORT>(cols), static_cast<SHORT>(rows) };
        ::ResizePseudoConsole(toHandle(m_pseudoConsole), size);
    }
}

void LocalSession::shutdown()
{
    if (m_stopRequested.exchange(true))
        return;

    m_running = false;

    // 先关伪控制台，让子进程收到关闭信号，读者线程才会从 ReadFile 里出来
    if (m_pseudoConsole) {
        ::ClosePseudoConsole(toHandle(m_pseudoConsole));
        m_pseudoConsole = nullptr;
    }

    if (m_inputWrite) {
        ::CloseHandle(toHandle(m_inputWrite));
        m_inputWrite = nullptr;
    }

    // 必须等读者线程收工，否则它可能正在 emit，对象却已经析构了
    if (m_reader.joinable())
        m_reader.join();

    if (m_process) {
        DWORD code = 0;
        if (::GetExitCodeProcess(toHandle(m_process), &code) && code == STILL_ACTIVE)
            ::TerminateProcess(toHandle(m_process), 0);
        ::CloseHandle(toHandle(m_process));
        m_process = nullptr;
    }

    if (m_outputRead) {
        ::CloseHandle(toHandle(m_outputRead));
        m_outputRead = nullptr;
    }

    // 关掉作业对象会把整个子进程树一起带走
    if (m_job) {
        ::CloseHandle(toHandle(m_job));
        m_job = nullptr;
    }
}

} // namespace Em
