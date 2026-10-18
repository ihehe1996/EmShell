#pragma once

#include "model/Connection.h"
#include "ssh/SshSession.h"

#include <QVector>
#include <QWidget>

class QStackedWidget;
class QTabWidget;

namespace Em {

class ConnectionStore;
class KeyStore;
class TerminalSession;
class TerminalView;

// 终端页：一个标签栏 + 若干会话。
//
// 一个标签都没有的时候不显示空标签栏，而是显示一张空状态页（buildEmptyState），
// 上面两个入口：打开本机终端 / 查看所有连接。**进这一页不会自动
// 开会话** —— 以前会自动起一个 PowerShell，那还会连带弄出「Ctrl+T 一次开两个」
// 和「点连接多一个本地终端」两个毛病。
//
// 本版本里「连接服务器」是这么做的：起一个本地终端，在里面跑系统自带的
// ssh 客户端（`ssh user@host`）。好处是 SSH 立刻就能用，不用先啃 libssh2；
// 密码在终端里正常输入，行为和平时用 ssh 完全一致。
//
// 以后接原生 SSH 库时，只要写一个 SshSession : TerminalSession，
// 把这里 openConnection() 里的 LocalSession 换掉，界面一行都不用动。
class TerminalPage : public QWidget
{
    Q_OBJECT

public:
    TerminalPage(ConnectionStore *store, KeyStore *keys, QWidget *parent = nullptr);

    // 开一个本地 shell
    void openLocalSession(const QString &shell = QString());

    // 连服务器。有可用凭据（存了密码，或指定了密钥）就走原生 SSH；
    // 没有的话退回系统 ssh 客户端，让用户在终端里自己输密码。
    void openConnection(const Connection &connection);

    int tabCount() const { return m_tabs.size(); }

signals:
    // error 为 true 时底部状态条会用警示色显示
    void statusMessage(const QString &text, bool error = false);

    // 空状态里的「查看所有连接」：切页的事交给 MainWindow
    void browseConnectionsRequested();

private:
    struct Tab {
        QWidget *container = nullptr;
        TerminalView *view = nullptr;
        TerminalSession *session = nullptr;
        QString title;
        int index = -1;
    };

    Tab *createTab(const QString &title);
    void closeTab(int index);
    void applyTitle(Tab *tab, const QString &title);
    Tab *tabAt(int index) const;
    Tab *currentTab() const;

    // 没有标签时显示的那张页
    QWidget *buildEmptyState();
    // 按标签数在「空状态 / 标签栏」之间切
    void updateEmptyState();

    // 原生 SSH（libssh2）
    void openNativeSsh(const Connection &connection, const SshSession::Options &options);
    // 退回系统 ssh 客户端，密码由用户在终端里输入
    void openConnectionViaCli(const Connection &connection);
    void promptForHostKey(SshSession *session, const QString &fingerprint,
                          const QString &keyType);

    static QString buildSshCommand(const Connection &connection, const KeyStore *keys);
    // 从一条连接记录拼出原生 SSH 需要的参数；返回 false 表示凭据不够，得走命令行
    bool buildSshOptions(const Connection &connection, SshSession::Options *options) const;

    QStackedWidget *m_stack = nullptr;
    QWidget *m_emptyPage = nullptr;
    QTabWidget *m_tabBar = nullptr;
    QVector<Tab *> m_tabs;
    ConnectionStore *m_store = nullptr;
    KeyStore *m_keys = nullptr;
    int m_untitledCount = 0;
};

} // namespace Em
