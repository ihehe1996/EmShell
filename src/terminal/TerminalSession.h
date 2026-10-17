#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

namespace Em {

// 终端会话的抽象接口。
//
// 现在只有 LocalSession（走 Windows ConPTY 开本地 shell）一个实现。
// 以后接 SSH 的时候，只要再写一个 SshSession 继承这个类，
// TerminalView / TerminalPage 一行都不用改。
class TerminalSession : public QObject
{
    Q_OBJECT

public:
    explicit TerminalSession(QObject *parent = nullptr);
    ~TerminalSession() override;

    // 开一个 cols x rows 的会话。失败时把原因写进 errorOut。
    virtual bool start(int cols, int rows, QString *errorOut = nullptr) = 0;

    // 把用户敲的字节送给会话（按键 → 转义序列由 TerminalView 负责转换）
    virtual void write(const QByteArray &data) = 0;

    // 终端窗口尺寸变了
    virtual void resize(int cols, int rows) = 0;

    // 主动结束。不要指望马上返回，实现里可能会等子进程退出。
    virtual void shutdown() = 0;

    virtual bool isRunning() const = 0;

    // 一行说明，显示在标签页提示里，比如 "本地 shell · PowerShell"
    virtual QString description() const = 0;

signals:
    // 会话真正可以用了（本地是 shell 起来了，SSH 是登录并拿到 shell 了）。
    // 注意它和 start() 返回不是一回事：SSH 的 start() 只是把线程支起来，
    // 连上要等这个信号。
    void started();

    // 会话产生了输出，原样喂给 TerminalScreen::feed()
    void dataReceived(const QByteArray &data);

    // 会话结束
    void exited(int exitCode);

    // 会话自己改了标题（本地 shell 一般不用，SSH 会话可以用它显示主机名）
    void titleChanged(const QString &title);
};

} // namespace Em
