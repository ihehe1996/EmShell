#pragma once

#include "terminal/TerminalSession.h"

#include <QString>
#include <QStringList>

#include <atomic>
#include <mutex>
#include <thread>

namespace Em {

// 本地终端会话，底层用 Windows 的 ConPTY（伪控制台）。
//
// 为什么不用管道直接读子进程输出：那样拿到的是「没有终端」的输出，
// 没有颜色、没有光标控制、vim/top 这类全屏程序根本跑不起来。
// ConPTY 会在中间模拟一个真终端，我们拿到的就是标准的 VT 序列，
// 和 SSH 会话的输出格式完全一致，TerminalScreen 一套代码通吃。
//
// 头文件里不引 windows.h，句柄统一用 void* 存，免得污染所有包含方。
class LocalSession : public TerminalSession
{
    Q_OBJECT

public:
    // shellCommand 留空则自动挑选（pwsh > powershell > cmd）
    explicit LocalSession(const QString &shellCommand = QString(), QObject *parent = nullptr);
    ~LocalSession() override;

    bool start(int cols, int rows, QString *errorOut = nullptr) override;
    void write(const QByteArray &data) override;
    void resize(int cols, int rows) override;
    void shutdown() override;

    bool isRunning() const override { return m_running.load(); }
    QString description() const override;

    // 按 pwsh -> powershell -> cmd 的顺序找可用的 shell
    static QString defaultShell();
    // 给界面下拉框用的候选列表，第一项是自动
    static QStringList availableShells();

private:
    void readLoop();

    QString m_command;
    int m_cols = 80;
    int m_rows = 24;

    // 全是 Win32 句柄，为了不在头文件里 include <windows.h> 而用 void*
    void *m_pseudoConsole = nullptr;
    void *m_inputWrite     = nullptr;   // 我们写 -> 子进程读
    void *m_outputRead     = nullptr;   // 子进程写 -> 我们读
    void *m_inputRead      = nullptr;   // 交给 ConPTY 后由它持有
    void *m_outputWrite    = nullptr;   // 同上
    void *m_process        = nullptr;
    void *m_job            = nullptr;   // 作业对象，保证关窗时子进程树一起死

    // 伪控制台会在两个地方被关掉：主线程的 shutdown()，以及子进程自己退出时
    // 的等待线程。用一把小锁保证不会同时 ClosePseudoConsole / ResizePseudoConsole。
    std::mutex m_pcMutex;

    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_stopRequested{ false };
    std::thread m_reader;
    std::thread m_processWaiter;
};

} // namespace Em
