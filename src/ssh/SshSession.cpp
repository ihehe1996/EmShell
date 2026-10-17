#include "ssh/SshSession.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QStandardPaths>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <libssh2.h>

#include <vector>

namespace Em {

namespace {

// libssh2 要求进程里至少调用一次 libssh2_init / WSAStartup，
// 用一个静态对象保证在任何 SshSession 之前完成。
struct Libssh2Global
{
    Libssh2Global()
    {
        WSADATA wsa{};
        WSAStartup(MAKEWORD(2, 2), &wsa);
        libssh2_init(0);
        ok = true;
    }
    ~Libssh2Global()
    {
        libssh2_exit();
        WSACleanup();
    }
    bool ok = false;
};

bool ensureLibssh2(QString *errorOut)
{
    static Libssh2Global global;
    if (!global.ok) {
        if (errorOut)
            *errorOut = QStringLiteral("初始化网络库失败");
        return false;
    }
    return true;
}

LIBSSH2_SESSION *sess(void *p) { return static_cast<LIBSSH2_SESSION *>(p); }
LIBSSH2_CHANNEL *chan(void *p) { return static_cast<LIBSSH2_CHANNEL *>(p); }
SOCKET sockOf(quintptr s) { return static_cast<SOCKET>(s); }

QString sessionError(LIBSSH2_SESSION *session, const QString &context)
{
    char *msg = nullptr;
    int len = 0;
    const int code = libssh2_session_last_error(session, &msg, &len, 0);
    const QString detail = msg ? QString::fromUtf8(msg, len) : QStringLiteral("未知错误");
    return QStringLiteral("%1：%2（libssh2 错误码 %3）").arg(context, detail).arg(code);
}

// 把 session_hostkey 给的类型映射成 knownhost 的密钥类型位
int knownHostKeyType(int hostKeyType)
{
    switch (hostKeyType) {
    case LIBSSH2_HOSTKEY_TYPE_RSA:        return LIBSSH2_KNOWNHOST_KEY_SSHRSA;
    case LIBSSH2_HOSTKEY_TYPE_DSS:        return LIBSSH2_KNOWNHOST_KEY_SSHDSS;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_256:  return LIBSSH2_KNOWNHOST_KEY_ECDSA_256;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_384:  return LIBSSH2_KNOWNHOST_KEY_ECDSA_384;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_521:  return LIBSSH2_KNOWNHOST_KEY_ECDSA_521;
#ifdef LIBSSH2_HOSTKEY_TYPE_ED25519
    case LIBSSH2_HOSTKEY_TYPE_ED25519:    return LIBSSH2_KNOWNHOST_KEY_ED25519;
#endif
    default:                              return LIBSSH2_KNOWNHOST_KEY_UNKNOWN;
    }
}

QString describeKeyType(int hostKeyType)
{
    switch (hostKeyType) {
    case LIBSSH2_HOSTKEY_TYPE_RSA:        return QStringLiteral("RSA");
    case LIBSSH2_HOSTKEY_TYPE_DSS:        return QStringLiteral("DSA");
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_256:  return QStringLiteral("ECDSA 256");
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_384:  return QStringLiteral("ECDSA 384");
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_521:  return QStringLiteral("ECDSA 521");
#ifdef LIBSSH2_HOSTKEY_TYPE_ED25519
    case LIBSSH2_HOSTKEY_TYPE_ED25519:    return QStringLiteral("ED25519");
#endif
    default:                              return QStringLiteral("未知类型");
    }
}

QString defaultKnownHostsPath()
{
    return QDir::homePath() + QStringLiteral("/.ssh/known_hosts");
}

} // namespace

// ------------------------------------------------------ 密钥类型支持 ---

// 见头文件里的说明：内置库只认 RSA。
//
// 这里用**白名单**而不是黑名单。除了 ed25519 / ecdsa 这些已知不支持的，
// KeyStore::detectType() 还可能给出 "openssh"（.pub 丢了，判不出算法）、
// "pkcs8"（只知道是 PKCS#8 容器，里面装的可能是 RSA 也可能是 EC）甚至空串。
// 这些都走系统 ssh 更稳 —— 绕远一点，总比再撞一次那个把人往服务器方向带的
// 「密钥不被服务器接受」强。
bool SshSession::supportsKeyType(const QString &type)
{
    return type.compare(QLatin1String("rsa"), Qt::CaseInsensitive) == 0;
}

// ---------------------------------------------------------------- 构造 ---

SshSession::SshSession(const Options &options, QObject *parent)
    : TerminalSession(parent)
    , m_options(options)
{
}

SshSession::~SshSession()
{
    shutdown();
}

QString SshSession::lastError() const
{
    QMutexLocker locker(&const_cast<SshSession *>(this)->m_mutex);
    return m_lastError;
}

QString SshSession::hostKeyFingerprint() const
{
    QMutexLocker locker(&const_cast<SshSession *>(this)->m_mutex);
    return m_fingerprint;
}

bool SshSession::isRunning() const
{
    return m_running.load();
}

QString SshSession::description() const
{
    const QString who = m_options.username.isEmpty()
                            ? m_options.host
                            : m_options.username + QLatin1Char('@') + m_options.host;
    return QStringLiteral("SSH · %1").arg(who);
}

void SshSession::setState(State s, const QString &detail)
{
    m_state.store(s);
    emit stateChanged(s, detail);
}

void SshSession::setError(const QString &error)
{
    {
        QMutexLocker locker(&m_mutex);
        // 同一个错误只报一次。传输层断掉之后 libssh2 每次读写都会返回同一个
        // 错误码，如果每次都 emit，界面的事件队列会被同一条消息灌满。
        if (m_state.load() == State::Failed && m_lastError == error)
            return;
        m_lastError = error;
    }
    setState(State::Failed, error);
}

void SshSession::answerHostKeyPrompt(bool accept)
{
    QMutexLocker locker(&m_promptMutex);
    m_promptAnswered = true;
    m_promptAccepted = accept;
    m_promptCondition.wakeAll();
}

// ---------------------------------------------------------------- 启动 ---

bool SshSession::start(int cols, int rows, QString *errorOut)
{
    if (m_running.load())
        return true;

    if (m_options.host.trimmed().isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("主机地址为空");
        return false;
    }

    if (!ensureLibssh2(errorOut))
        return false;

    m_cols = qMax(1, cols);
    m_rows = qMax(1, rows);
    m_stopRequested = false;

    {
        QMutexLocker locker(&m_mutex);
        m_pendingInput.clear();
        m_lastError.clear();
        m_fingerprint.clear();
        m_pendingCols = 0;
        m_pendingRows = 0;
    }
    {
        QMutexLocker locker(&m_promptMutex);
        m_promptAnswered = false;
        m_promptAccepted = false;
    }

    m_running = true;
    m_worker = std::thread([this] { workerMain(); });
    return true;
}

// ------------------------------------------------------------ 工作线程 ---

void SshSession::workerMain()
{
    QString error;

    if (!connectSocket(&error) || !doHandshake(&error) || !verifyHostKey(&error)
        || !authenticate(&error) || !openShell(&error)) {
        setError(error);
        cleanup();
        m_running = false;
        emit exited(-1);
        return;
    }

    setState(State::Ready);
    emit started();

    // ---- 主循环：非阻塞 libssh2 + select 等 socket ----
    std::vector<char> buffer(32 * 1024);
    LIBSSH2_CHANNEL *channel = chan(m_channel);
    const SOCKET sock = sockOf(m_socket);

    QByteArray writeBuffer;
    bool fatal = false;

    while (!m_stopRequested.load()) {
        // 1) 把主线程攒下的输入搬到本地写缓冲
        {
            QMutexLocker locker(&m_mutex);
            if (!m_pendingInput.isEmpty()) {
                writeBuffer.append(m_pendingInput);
                m_pendingInput.clear();
            }
        }

        // 2) 尽量往外写
        while (!writeBuffer.isEmpty()) {
            const ssize_t n = libssh2_channel_write(channel,
                                                    writeBuffer.constData(),
                                                    size_t(writeBuffer.size()));
            if (n > 0) {
                writeBuffer.remove(0, int(n));
                continue;
            }
            if (n == LIBSSH2_ERROR_EAGAIN)
                break;                 // socket 满了，等下一轮
            setError(sessionError(sess(m_session), QStringLiteral("写入失败")));
            fatal = true;
            break;
        }

        // 传输层已经断了就立刻收工。继续轮询的话，select 会一直认为 socket
        // 可读、channel_read 每次都返回同一个错误，循环空转，还会不停 emit
        // stateChanged —— 界面的事件队列越积越大，内存能涨到 GB 级并卡死。
        if (fatal)
            break;

        // 3) 尽量往里读
        for (;;) {
            const ssize_t got = libssh2_channel_read(channel, buffer.data(), buffer.size());
            if (got > 0) {
                emit dataReceived(QByteArray(buffer.data(), int(got)));
                continue;
            }
            if (got == LIBSSH2_ERROR_EAGAIN)
                break;
            if (got < 0) {
                setError(sessionError(sess(m_session), QStringLiteral("读取失败")));
                fatal = true;
                break;
            }
            break;   // 0 字节 = 对端不再有输出
        }

        if (fatal)
            break;

        // 4) 尺寸变了就通知远端（vim 之类要靠这个重排界面）
        int newCols = 0, newRows = 0;
        {
            QMutexLocker locker(&m_mutex);
            if (m_pendingCols > 0 && m_pendingRows > 0) {
                newCols = m_pendingCols;
                newRows = m_pendingRows;
                m_pendingCols = m_pendingRows = 0;
            }
        }
        if (newCols > 0) {
            libssh2_channel_request_pty_size(channel, newCols, newRows);
            m_cols = newCols;
            m_rows = newRows;
        }

        // 5) 等 socket 有动静。15ms 超时是为了及时把新输入发出去，
        //    代价可以忽略（空闲时每秒钟也就 60 来次 select）
        fd_set readSet, writeSet;
        FD_ZERO(&readSet);
        FD_ZERO(&writeSet);
        FD_SET(sock, &readSet);
        if (!writeBuffer.isEmpty())
            FD_SET(sock, &writeSet);

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 15000;
        select(0, &readSet, &writeSet, nullptr, &timeout);

        if (libssh2_channel_eof(channel))
            break;
    }

    // 出错退出时保留 Failed 状态，别再盖成「已断开」，否则界面上的错误
    // 提示会一闪而过。
    if (!fatal)
        setState(State::Closed, QStringLiteral("会话已断开"));
    cleanup();
    m_running = false;
    emit exited(fatal ? -1 : 0);
}

// ------------------------------------------------------------ 建立连接 ---

bool SshSession::connectSocket(QString *errorOut)
{
    setState(State::Connecting, QStringLiteral("正在连接 %1:%2")
                                    .arg(m_options.host).arg(m_options.port));

    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    const QByteArray hostUtf8 = m_options.host.toUtf8();
    const QByteArray portUtf8 = QByteArray::number(m_options.port);

    struct addrinfo *result = nullptr;
    if (::getaddrinfo(hostUtf8.constData(), portUtf8.constData(), &hints, &result) != 0) {
        *errorOut = QStringLiteral("解析主机名失败：%1").arg(m_options.host);
        return false;
    }

    // 一个域名可能解析出多个地址（IPv4/IPv6），挨个试
    for (struct addrinfo *ai = result; ai; ai = ai->ai_next) {
        SOCKET s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == INVALID_SOCKET)
            continue;

        // 非阻塞 connect + select 实现带超时的连接，
        // 否则连一台不响应的机器会一直挂着
        u_long nonBlocking = 1;
        ::ioctlsocket(s, FIONBIO, &nonBlocking);

        int rc = ::connect(s, ai->ai_addr, int(ai->ai_addrlen));
        if (rc == SOCKET_ERROR && ::WSAGetLastError() == WSAEWOULDBLOCK) {
            fd_set writeSet;
            FD_ZERO(&writeSet);
            FD_SET(s, &writeSet);

            timeval tv{};
            tv.tv_sec = m_options.connectTimeoutMs / 1000;
            tv.tv_usec = (m_options.connectTimeoutMs % 1000) * 1000;

            if (::select(0, nullptr, &writeSet, nullptr, &tv) <= 0) {
                ::closesocket(s);
                continue;   // 超时，试下一个地址
            }

            int soError = 0;
            int soErrorLen = sizeof(soError);
            ::getsockopt(s, SOL_SOCKET, SO_ERROR,
                         reinterpret_cast<char *>(&soError), &soErrorLen);
            if (soError != 0) {
                ::closesocket(s);
                continue;
            }
        } else if (rc == SOCKET_ERROR) {
            ::closesocket(s);
            continue;
        }

        u_long blocking = 0;
        ::ioctlsocket(s, FIONBIO, &blocking);

        // 握手和认证阶段用的是 libssh2 的阻塞模式，得给 socket 设收发超时，
        // 否则遇到不吭声的服务器会把工作线程永久挂住，连 shutdown() 都返回不了。
        // 建好 shell 切成非阻塞之后这两个超时会取消掉。
        const DWORD timeoutMs = DWORD(m_options.connectTimeoutMs);
        ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char *>(&timeoutMs), sizeof(timeoutMs));
        ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO,
                     reinterpret_cast<const char *>(&timeoutMs), sizeof(timeoutMs));

        m_socket = quintptr(s);
        ::freeaddrinfo(result);
        return true;
    }

    ::freeaddrinfo(result);
    *errorOut = QStringLiteral("连不上 %1:%2（超时或目标拒绝）")
                    .arg(m_options.host).arg(m_options.port);
    return false;
}

bool SshSession::doHandshake(QString *errorOut)
{
    setState(State::Handshaking, QStringLiteral("正在协商加密参数"));

    LIBSSH2_SESSION *session = libssh2_session_init();
    if (!session) {
        *errorOut = QStringLiteral("创建 SSH 会话失败");
        return false;
    }
    m_session = session;

    // 主机密钥算法按「和 OpenSSH 一致的顺序」来排。
    //
    // 不设的话 libssh2 可能协商到 RSA，而本机 ssh 客户端用的是 ed25519 ——
    // 服务器两种都提供，但客户端偏好不同就会挑到不同的那把。后果是明明
    // 已经在 known_hosts 里信任过这台机器了，EmShell 还会再问一次
    // 「这是新主机，指纹是 xxx」，让人以为出了什么事。
    //
    // 万一这套算法名有 libssh2 不认的（比如这个 build 没编 ed25519），
    // 设置会失败，那就退回它自己的默认顺序，不影响能连上。
    libssh2_session_method_pref(session, LIBSSH2_METHOD_HOSTKEY,
                                "ssh-ed25519,"
                                "ecdsa-sha2-nistp256,ecdsa-sha2-nistp384,ecdsa-sha2-nistp521,"
                                "rsa-sha2-512,rsa-sha2-256,ssh-rsa");

    // 握手和认证阶段用阻塞模式，代码简单不容易写错。
    // 建好 shell 之后再切成非阻塞，交给主循环。
    libssh2_session_set_blocking(session, 1);

    if (libssh2_session_handshake(session, sockOf(m_socket)) != 0) {
        *errorOut = sessionError(session, QStringLiteral("SSH 握手失败"));
        return false;
    }
    return true;
}

// -------------------------------------------------------- 主机密钥校验 ---

bool SshSession::verifyHostKey(QString *errorOut)
{
    setState(State::VerifyingHostKey, QStringLiteral("正在校验主机密钥"));

    LIBSSH2_SESSION *session = sess(m_session);

    size_t keyLen = 0;
    int keyType = 0;
    const char *key = libssh2_session_hostkey(session, &keyLen, &keyType);
    if (!key) {
        *errorOut = QStringLiteral("拿不到服务器的主机密钥");
        return false;
    }

    // 算一个跟 OpenSSH 一致格式的指纹给用户看：SHA256:base64(...)
    QString fingerprint;
    if (const char *hash = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA256)) {
        const QByteArray raw(hash, 32);
        fingerprint = QStringLiteral("SHA256:") + QString::fromLatin1(raw.toBase64().chopped(1));
    }
    {
        QMutexLocker locker(&m_mutex);
        m_fingerprint = fingerprint;
    }

    const QString knownHostsPath = m_options.knownHostsPath.isEmpty()
                                       ? defaultKnownHostsPath()
                                       : m_options.knownHostsPath;

    LIBSSH2_KNOWNHOSTS *knownHosts = libssh2_knownhost_init(session);
    if (!knownHosts) {
        *errorOut = QStringLiteral("初始化 known_hosts 失败");
        return false;
    }

    // 文件不存在不算错误，第一次连任何机器都没有这个文件
    if (QFileInfo::exists(knownHostsPath))
        libssh2_knownhost_readfile(knownHosts, knownHostsPath.toUtf8().constData(),
                                   LIBSSH2_KNOWNHOST_FILE_OPENSSH);

    // typemask 里必须带上协商到的密钥类型。
    // 只写 TYPE_PLAIN|KEYENC_RAW 的话，libssh2 会拿当前这个密钥去和
    // known_hosts 里同主机名、但**密钥类型不同**的条目比对（服务器常常同时
    // 提供 ed25519 / rsa / ecdsa），结果就是明明没被改过却报「密钥不一致」。
    const int typeMask = LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW
                         | knownHostKeyType(keyType);

    struct libssh2_knownhost *entry = nullptr;
    const int check = libssh2_knownhost_checkp(knownHosts,
                                               m_options.host.toUtf8().constData(),
                                               m_options.port,
                                               key, keyLen,
                                               typeMask,
                                               &entry);

    const bool accept = [&] {
        switch (check) {
        case LIBSSH2_KNOWNHOST_CHECK_MATCH:
            return true;

        case LIBSSH2_KNOWNHOST_CHECK_MISMATCH:
            // 这里绝对不能放行。密钥对不上要么是服务器换了密钥，要么是中间人。
            *errorOut = QStringLiteral(
                            "主机密钥和本地记录不一致，连接已中止。\n\n"
                            "本次协商到的类型：%1\n"
                            "当前指纹：%2\n\n"
                            "如果你确定服务器换过密钥，请手动编辑 %3 删掉旧记录后重试；"
                            "否则这台机器可能被劫持了。")
                            .arg(describeKeyType(keyType), fingerprint, knownHostsPath);
            return false;

        case LIBSSH2_KNOWNHOST_CHECK_NOTFOUND: {
            // 第一次连这台机器，把指纹交给主线程让用户判断
            setState(State::VerifyingHostKey,
                     QStringLiteral("等待确认主机指纹 %1").arg(fingerprint));

            {
                QMutexLocker locker(&m_promptMutex);
                m_promptAnswered = false;
                m_promptAccepted = false;
            }

            // emit 一定要放在锁外面。
            // 放在锁里面的话，如果这条连接是直连（槽在工作线程上跑），
            // 槽里的 answerHostKeyPrompt() 会去锁同一把锁 —— 自己等自己，死锁。
            // 放到锁外面，两种情况都安全。
            emit hostKeyUnknown(fingerprint, describeKeyType(keyType));

            // 等主线程把用户的选择送回来。每 200ms 醒一次是为了能响应 shutdown。
            QMutexLocker locker(&m_promptMutex);
            while (!m_promptAnswered && !m_stopRequested.load()) {
                // 用户可能在 emit 之后、拿锁之前就答完了，所以进来先看条件再等
                m_promptCondition.wait(&m_promptMutex, 200);
            }

            if (m_stopRequested.load()) {
                *errorOut = QStringLiteral("连接已取消");
                return false;
            }
            if (!m_promptAccepted) {
                *errorOut = QStringLiteral("用户没有接受主机密钥，已取消连接");
                return false;
            }
            return true;
        }

        case LIBSSH2_KNOWNHOST_CHECK_FAILURE:
        default:
            *errorOut = QStringLiteral("校验主机密钥时出错（known_hosts 可能损坏或被占用）");
            return false;
        }
    }();

    if (!accept) {
        libssh2_knownhost_free(knownHosts);
        return false;
    }

    // 用户刚接受的新指纹，写进 known_hosts，下次就不再问了
    if (check == LIBSSH2_KNOWNHOST_CHECK_NOTFOUND) {
        if (libssh2_knownhost_addc(knownHosts,
                                   m_options.host.toUtf8().constData(), nullptr,
                                   key, keyLen, nullptr, 0, typeMask, nullptr) == 0) {
            QDir().mkpath(QFileInfo(knownHostsPath).absolutePath());
            if (libssh2_knownhost_writefile(knownHosts, knownHostsPath.toUtf8().constData(),
                                            LIBSSH2_KNOWNHOST_FILE_OPENSSH) != 0) {
                qWarning("EmShell: known_hosts 写不进去 %s", qPrintable(knownHostsPath));
            }
        }
    }

    libssh2_knownhost_free(knownHosts);
    return true;
}

// -------------------------------------------------------------- 认证 ---

bool SshSession::authenticate(QString *errorOut)
{
    setState(State::Authenticating,
             QStringLiteral("正在以 %1 身份认证").arg(m_options.username));

    LIBSSH2_SESSION *session = sess(m_session);
    const QByteArray user = m_options.username.toUtf8();

    int rc = 0;

    if (!m_options.privateKeyPath.isEmpty()) {
        // 密钥认证。公钥路径可以不填，libssh2 会自己试着加 ".pub"
        QByteArray pubPath;
        if (!m_options.publicKeyPath.isEmpty())
            pubPath = m_options.publicKeyPath.toUtf8();

        rc = libssh2_userauth_publickey_fromfile_ex(
            session, user.constData(),
            uint(user.size()),
            pubPath.isEmpty() ? nullptr : pubPath.constData(),
            m_options.privateKeyPath.toUtf8().constData(),
            m_options.passphrase.toUtf8().constData());
    } else {
        rc = libssh2_userauth_password_ex(session, user.constData(), uint(user.size()),
                                          m_options.password.toUtf8().constData(),
                                          uint(m_options.password.toUtf8().size()),
                                          nullptr);
    }

    if (rc != 0) {
        if (rc == LIBSSH2_ERROR_AUTHENTICATION_FAILED) {
            *errorOut = m_options.privateKeyPath.isEmpty()
                            ? QStringLiteral("认证失败：用户名或密码不对")
                            : QStringLiteral("认证失败：密钥不被服务器接受，或密钥口令不对");
        } else if (rc == LIBSSH2_ERROR_SOCKET_TIMEOUT) {
            *errorOut = QStringLiteral("认证超时");
        } else {
            *errorOut = sessionError(session, QStringLiteral("认证失败"));
        }
        return false;
    }
    return true;
}

// ------------------------------------------------------------ 打开终端 ---

bool SshSession::openShell(QString *errorOut)
{
    setState(State::OpeningShell, QStringLiteral("正在打开终端"));

    LIBSSH2_SESSION *session = sess(m_session);

    LIBSSH2_CHANNEL *channel = libssh2_channel_open_session(session);
    if (!channel) {
        *errorOut = sessionError(session, QStringLiteral("打开 SSH 通道失败"));
        return false;
    }
    m_channel = channel;

    // 让远端把 stderr 也并到同一条流里，省得界面上顺序错乱
    libssh2_channel_handle_extended_data2(channel, LIBSSH2_CHANNEL_EXTENDED_DATA_MERGE);

    if (libssh2_channel_request_pty_ex(channel,
                                       "xterm-256color", 14,
                                       nullptr, 0,
                                       m_cols, m_rows, 0, 0) != 0) {
        *errorOut = sessionError(session, QStringLiteral("申请伪终端失败"));
        return false;
    }

    if (libssh2_channel_shell(channel) != 0) {
        *errorOut = sessionError(session, QStringLiteral("启动远端 shell 失败"));
        return false;
    }

    // 切换到非阻塞，后面交给主循环用 EAGAIN + select 驱动。
    //
    // 注意：libssh2_session_set_blocking(session, 0) 只改 libssh2 自己的标志位，
    // **不会**把 socket 设成非阻塞 —— 这件事得调用方自己做。忘了设的话，
    // libssh2 内部调 recv() 仍然会阻塞，主循环就卡死在 channel_read 里，
    // 连 shutdown() 的 join 都返回不了。
    if (m_socket != ~quintptr(0)) {
        u_long nonBlocking = 1;
        ::ioctlsocket(sockOf(m_socket), FIONBIO, &nonBlocking);

        // 连接阶段设的收发超时也要清掉，进了轮询模式它会干扰 EAGAIN 判断
        const DWORD noTimeout = 0;
        ::setsockopt(sockOf(m_socket), SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char *>(&noTimeout), sizeof(noTimeout));
        ::setsockopt(sockOf(m_socket), SOL_SOCKET, SO_SNDTIMEO,
                     reinterpret_cast<const char *>(&noTimeout), sizeof(noTimeout));
    }

    libssh2_session_set_blocking(session, 0);
    return true;
}

// ---------------------------------------------------------------- 收尾 ---

void SshSession::cleanup()
{
    if (m_channel) {
        LIBSSH2_CHANNEL *channel = chan(m_channel);
        libssh2_channel_close(channel);
        libssh2_channel_free(channel);
        m_channel = nullptr;
    }

    if (m_session) {
        LIBSSH2_SESSION *session = sess(m_session);
        libssh2_session_disconnect(session, "bye");
        libssh2_session_free(session);
        m_session = nullptr;
    }

    if (m_socket != ~quintptr(0)) {
        ::closesocket(sockOf(m_socket));
        m_socket = ~quintptr(0);
    }
}

void SshSession::shutdown()
{
    if (m_stopRequested.exchange(true)) {
        // 已经有人叫停了，但线程可能还没收工，等它一下
        if (m_worker.joinable())
            m_worker.join();
        return;
    }

    // 把可能还在等用户确认主机密钥的工作线程叫醒，别让它一直挂着
    {
        QMutexLocker locker(&m_promptMutex);
        m_promptCondition.wakeAll();
    }

    if (m_worker.joinable())
        m_worker.join();

    m_running = false;
}

// -------------------------------------------------------------- 输入 ---

void SshSession::write(const QByteArray &data)
{
    if (data.isEmpty() || !m_running.load())
        return;

    QMutexLocker locker(&m_mutex);
    m_pendingInput.append(data);

    // 防止某个失控的程序把内存吃光：超过 4MB 就丢掉最老的部分
    constexpr int kMaxPending = 4 * 1024 * 1024;
    if (m_pendingInput.size() > kMaxPending)
        m_pendingInput.remove(0, m_pendingInput.size() - kMaxPending);
}

void SshSession::resize(int cols, int rows)
{
    cols = qMax(1, cols);
    rows = qMax(1, rows);
    if (cols == m_cols && rows == m_rows)
        return;

    QMutexLocker locker(&m_mutex);
    m_pendingCols = cols;
    m_pendingRows = rows;
}

} // namespace Em
