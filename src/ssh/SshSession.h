#pragma once

#include "terminal/TerminalSession.h"

#include <QByteArray>
#include <QMutex>
#include <QString>
#include <QWaitCondition>

#include <atomic>
#include <thread>

namespace Em {

// 原生 SSH 会话，底层是 libssh2。
//
// 线程模型：libssh2 是阻塞式 API，硬塞进 Qt 事件循环会很难看。所以整个会话
// 跑在一个独立的工作线程里，用带超时的 select() 轮询 socket：
//
//   主线程                         工作线程
//   ------                         --------
//   write()  -> 待发队列  ------->  取出来 libssh2_channel_write
//   resize() -> 原子变量  ------->  发现变了就 libssh2_channel_request_pty_size
//   dataReceived  <---------------  libssh2_channel_read 读到什么就发什么
//
// 主机密钥校验需要用户确认（第一次连某台机器时），这一步在工作线程里没法弹窗，
// 所以用一次「发信号 + 等条件变量」把主线程拉进来同步完成，超时或拒绝就断开。
class SshSession : public TerminalSession
{
    Q_OBJECT

public:
    enum class State {
        Idle,
        Connecting,
        Handshaking,
        VerifyingHostKey,
        Authenticating,
        OpeningShell,
        Ready,
        Failed,
        Closed,
    };
    Q_ENUM(State)

    struct Options
    {
        QString host;
        int port = 22;
        QString username;
        QString password;
        QString privateKeyPath;   // 私钥认证时用
        QString publicKeyPath;
        QString passphrase;
        QString knownHostsPath;   // 留空则用 ~/.ssh/known_hosts
        int connectTimeoutMs = 20000;
    };

    // 内置的 libssh2 能不能用这种类型的私钥认证。type 就是 SshKey::type。
    //
    // **只有 RSA 能用。** 内置库是 WinCNG 后端，ed25519 / ecdsa 它连实现都没编
    // 进去（算法名在协商表里认得出，但签名那一步没有代码可跑）。拿这类密钥走
    // 原生路径，会一路走到认证才失败，报的却是「密钥不被服务器接受」—— 看着像
    // 服务器没配好，实际是本地库不支持。
    //
    // 所以调用方在拿不准的时候应该先问这个函数，用不了就退回系统 ssh 客户端
    // （见 TerminalPage::buildSshOptions），那边什么都支持。
    static bool supportsKeyType(const QString &type);

    explicit SshSession(const Options &options, QObject *parent = nullptr);
    ~SshSession() override;

    bool start(int cols, int rows, QString *errorOut = nullptr) override;
    void write(const QByteArray &data) override;
    void resize(int cols, int rows) override;
    void shutdown() override;

    // 关标签用：只叫停、**不 join**，界面线程立刻返回。
    //
    // 工作线程可能正卡在 libssh2 的阻塞握手 / 认证里（服务器接了 TCP 却不回
    // SSH 报文时要等 api_timeout）。以前 closeTab() 直接调 shutdown()，
    // join 把界面线程一起堵住，表现就是点标签的 ✕ 之后整个窗口「未响应」。
    // 这个方法让线程自己在后台收工，然后 deleteLater()。
    void shutdownAsync();

    bool isRunning() const override;
    QString description() const override;

    State state() const { return m_state.load(); }
    QString lastError() const;
    QString hostKeyFingerprint() const;

    // 主线程收到 hostKeyUnknown 之后调这个，把用户的选择送回工作线程
    void answerHostKeyPrompt(bool accept);

signals:
    void stateChanged(Em::SshSession::State state, const QString &detail);

    // 第一次连这台机器，指纹不在 known_hosts 里。
    // 界面应该弹个框把 fingerprint 显示出来让用户判断，然后调 answerHostKeyPrompt()。
    void hostKeyUnknown(const QString &fingerprint, const QString &keyType);

private:
    void workerMain();
    bool connectSocket(QString *errorOut);
    bool doHandshake(QString *errorOut);
    bool verifyHostKey(QString *errorOut);
    bool authenticate(QString *errorOut);
    bool openShell(QString *errorOut);
    void pumpOnce();
    void drainPendingInput();
    void applyPendingResize();
    void setState(State s, const QString &detail = QString());
    void setError(const QString &error);
    void cleanup();
    void requestStop();

    Options m_options;

    std::thread m_worker;
    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_stopRequested{ false };
    // 工作线程彻底收工（包括所有 emit）之后才置 true。shutdownAsync() 靠它
    // 判断什么时候可以安全 join + deleteLater。
    std::atomic<bool> m_workerDone{ false };
    std::atomic<State> m_state{ State::Idle };

    QMutex m_mutex;                 // 保护下面这些跨线程的东西
    QByteArray m_pendingInput;      // 主线程写入，工作线程取走
    QString m_lastError;
    QString m_fingerprint;
    int m_pendingCols = 0;
    int m_pendingRows = 0;

    // 主机密钥确认用
    QMutex m_promptMutex;
    QWaitCondition m_promptCondition;
    bool m_promptAnswered = false;
    bool m_promptAccepted = false;

    int m_cols = 80;
    int m_rows = 24;

    // 全是 libssh2 / winsock 的句柄，为了不在头文件里引它们的头
    void *m_session = nullptr;
    void *m_channel = nullptr;
    quintptr m_socket = ~quintptr(0);
};

} // namespace Em
