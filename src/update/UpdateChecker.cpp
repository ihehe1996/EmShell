#include "update/UpdateChecker.h"

#include "app/AppConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace Em {
namespace {

// 一步到位构造请求，省得 check 和 download 两处各写一遍头。
//
// User-Agent 是**必须**的：GitHub 的 API 对没有 UA 的请求直接回 403，
// 报的还是「rate limit exceeded」这种会把人带偏的消息。
QNetworkRequest makeRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("EmShell/%1")
                          .arg(QCoreApplication::applicationVersion()));
    request.setRawHeader("Accept", "application/vnd.github+json");

    // 别让一次检查卡住界面：15 秒还没动静就当超时。
    // 对下载来说这个值是「两个数据块之间」的超时，不是总时长，所以不碍事。
    request.setTransferTimeout(15000);

    // 附件地址会 302 到 objects.githubusercontent.com。
    // NoLessSafeRedirectPolicy 是 Qt6 的默认值，这里写出来是为了让它显式可见 ——
    // 换掉它就意味着允许 https 降级到 http，那不行。
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

// 把 HTTP 状态码翻译成能给用户看的一句话。
// 404 不走这里 —— 它有两种含义，要先再问一次仓库在不在，见 resolveNotFound()。
QString describeHttpError(int status, const QString &reason)
{
    Q_UNUSED(reason);
    switch (status) {
    case 0:
        return QStringLiteral("连不上更新服务");
    case 403:
        // 更新服务的接口对未认证调用有频率限制
        return QStringLiteral("更新服务暂时不可用，请过几分钟再试");
    default:
        return QStringLiteral("更新服务返回错误（HTTP %1）").arg(status);
    }
}

} // namespace

// 这两条是给用户看的，**不要写仓库名、接口地址或者配置文件路径** ——
// 更新走哪儿是内部实现，用户只需要知道「现在能不能更新」。
QString repoNotFoundMessage(const QString &repo)
{
    Q_UNUSED(repo);
    return QStringLiteral("暂时获取不到更新信息。\n\n"
                          "请检查网络后稍后再试，或者访问作者主页获取最新版本。");
}

QString noReleaseYetMessage(const QString &repo)
{
    Q_UNUSED(repo);
    return QStringLiteral("更新服务里还没有可用的版本，请稍后再试。");
}

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent) {}

UpdateChecker::~UpdateChecker()
{
    // m_sink 是裸的 new（它要跨 reply 的生命周期活），这里收尾
    teardownDownload();
}

bool UpdateChecker::isBusy() const
{
    return !m_reply.isNull();
}

void UpdateChecker::abort()
{
    if (m_reply) {
        // 先断开再 abort，否则 abort 触发的 finished 会把半截下载当成失败报上去
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    teardownDownload();
}

void UpdateChecker::check()
{
    const QString repo = updateRepo();
    if (repo.isEmpty() || !repo.contains(QLatin1Char('/'))) {
        Result result;
        result.error = QStringLiteral("更新服务暂时不可用，请稍后再试。");
        emit checkFinished(result);
        return;
    }

    abort();

    const QUrl url(QStringLiteral("https://api.github.com/repos/%1/releases/latest")
                       .arg(repo.trimmed()));
    m_reply = m_network.get(makeRequest(url));
    connect(m_reply, &QNetworkReply::finished, this, &UpdateChecker::onCheckReplyFinished);
}

void UpdateChecker::onCheckReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;

    reply->deleteLater();
    if (m_reply == reply)
        m_reply = nullptr;

    Result result;
    result.current = Version{};   // 下面解析；解析不出来就保持 0.0.0

    const QVariant statusAttribute = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int status = statusAttribute.toInt();

    // 网络层直接失败（DNS、TLS、超时）。HttpStatusCode 拿不到才走这里 ——
    // 服务端返回了 4xx/5xx 的报文时 status 是有值的，那种走下面的分支。
    if (reply->error() != QNetworkReply::NoError && status == 0) {
        // errorString() 里可能带主机名或接口路径，不能直接摊给用户看
        result.error = QStringLiteral("%1，请检查网络后重试。")
                           .arg(describeHttpError(0, QString()));
        emit checkFinished(result);
        return;
    }

    // 404 得再问一次才能说清楚是哪种情况（见 resolveNotFound），
    // 这里交给它，它会自己 emit
    if (status == 404) {
        resolveNotFound(result);
        return;
    }

    if (status != 200) {
        result.error = describeHttpError(status, reply->attribute(
                                                    QNetworkRequest::HttpReasonPhraseAttribute)
                                                    .toString());
        emit checkFinished(result);
        return;
    }

    ReleaseInfo info;
    QString parseError;
    if (!parseGitHubRelease(reply->readAll(), &info, &parseError)) {
        result.error = QStringLiteral("更新信息格式不正确，请稍后再试。");
        emit checkFinished(result);
        return;
    }

    // 本程序的版本解析不出来就没法比 —— 这属于「配置写错了」，
    // 不能当成「没有更新」静默吞掉，否则永远查不出问题。
    if (!parseVersion(QCoreApplication::applicationVersion(), &result.current)) {
        result.error = QStringLiteral("本程序的版本号「%1」不是合法版本号，没法比较")
                           .arg(QCoreApplication::applicationVersion());
        emit checkFinished(result);
        return;
    }

    Version latest;
    if (!parseVersion(info.tagName, &latest)) {
        result.error = QStringLiteral("新版本的版本号「%1」不合法，没法比较").arg(info.tagName);
        emit checkFinished(result);
        return;
    }

    result.ok = true;
    result.release = info;
    result.latest = latest;
    result.updateAvailable = isNewerVersion(latest, result.current);

    emit checkFinished(result);
}

void UpdateChecker::resolveNotFound(const Result &result)
{
    // GitHub 对「仓库不存在」和「仓库存在但没有 Release」都回 404，
    // 只凭第一次的响应分不出来。所以再查一次仓库本身：它还回 404 就是仓库没有，
    // 回 200 就是仓库在、只是没发过 Release。
    //
    // 多花一次请求只在出错路径上，值。
    const QString repo = updateRepo().trimmed();
    const QUrl url(QStringLiteral("https://api.github.com/repos/%1").arg(repo));

    QNetworkReply *reply = m_network.get(makeRequest(url));
    m_reply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply, result, repo] {
        reply->deleteLater();
        if (m_reply == reply)
            m_reply = nullptr;

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError error = reply->error();

        Result resolved = result;
        if (status == 200) {
            resolved.error = noReleaseYetMessage(repo);
        } else if (status == 404) {
            resolved.error = repoNotFoundMessage(repo);
        } else if (error != QNetworkReply::NoError && status == 0) {
            // 第一次问到了 404 说明网络本来是通的，这次却挂了。不要说具体地址。
            resolved.error = QStringLiteral("连接更新服务失败，请检查网络后重试。");
        } else {
            resolved.error = describeHttpError(status, QString());
        }

        emit checkFinished(resolved);
    });
}

void UpdateChecker::download(const ReleaseAsset &asset, const QString &destPath)
{
    abort();

    // 目标目录可能还不存在（第一次下载时临时目录是新建的）
    const QFileInfo info(destPath);
    if (!QDir().mkpath(info.absolutePath())) {
        emit downloadFailed(QStringLiteral("建不出目录 %1").arg(info.absolutePath()));
        return;
    }

    m_sink = new QFile(destPath);
    if (!m_sink->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString reason = m_sink->errorString();
        teardownDownload();
        emit downloadFailed(QStringLiteral("打不开 %1：%2").arg(destPath, reason));
        return;
    }

    m_destPath = destPath;
    m_asset = asset;
    m_hash.reset();
    m_written = 0;

    m_reply = m_network.get(makeRequest(QUrl(asset.downloadUrl)));
    connect(m_reply, &QNetworkReply::readyRead, this, &UpdateChecker::onDownloadReadyRead);
    connect(m_reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) { emit downloadProgress(received, total); });
    connect(m_reply, &QNetworkReply::finished, this, &UpdateChecker::onDownloadReplyFinished);
}

void UpdateChecker::onDownloadReadyRead()
{
    if (!m_reply || !m_sink)
        return;

    const QByteArray chunk = m_reply->readAll();
    if (chunk.isEmpty())
        return;

    m_hash.addData(chunk);
    if (m_sink->write(chunk) != chunk.size()) {
        const QString reason = m_sink->errorString();
        abort();
        emit downloadFailed(QStringLiteral("写盘失败：%1").arg(reason));
        return;
    }
    m_written += chunk.size();
}

void UpdateChecker::onDownloadReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;

    reply->deleteLater();
    if (m_reply == reply)
        m_reply = nullptr;

    // 收尾：把可能还留在缓冲区里的最后一块取出来
    onDownloadReadyRead();

    const QString destPath = m_destPath;
    const ReleaseAsset asset = m_asset;
    const qint64 written = m_written;
    const QString actualHash = QString::fromLatin1(m_hash.result().toHex());

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError error = reply->error();

    // 关文件这一步不能省：不关的话下面 statSize 拿到的可能还是旧的长度
    teardownDownload();

    if (error != QNetworkReply::NoError && status == 0) {
        QFile::remove(destPath);
        emit downloadFailed(QStringLiteral("下载中断，请检查网络后重试。"));
        return;
    }
    if (status != 200 && status != 0) {
        QFile::remove(destPath);
        emit downloadFailed(describeHttpError(status, QString()));
        return;
    }

    // ---- 完整性校验。这一段是自更新的关键防线： ----
    // 一个被截断或者被中间人换掉的 exe 要是替了上去，用户手上就是个坏掉的程序，
    // 而且很可能连「检查更新」都再点不动了（因为新的 exe 起不来）。
    if (asset.size >= 0 && written != asset.size) {
        QFile::remove(destPath);
        emit downloadFailed(QStringLiteral("下载的文件大小不对（预期 %1 字节，实收 %2），已丢弃")
                                .arg(asset.size)
                                .arg(written));
        return;
    }
    if (!asset.sha256.isEmpty() && actualHash.compare(asset.sha256, Qt::CaseInsensitive) != 0) {
        QFile::remove(destPath);
        emit downloadFailed(QStringLiteral("下载的文件校验不过（sha256 对不上），已丢弃。\n"
                                           "可能是网络出了问题，也可能是更新包的摘要"
                                           "和附件不一致。"));
        return;
    }

    emit downloadFinished(destPath);
}

void UpdateChecker::teardownDownload()
{
    if (m_sink) {
        m_sink->close();
        delete m_sink;
        m_sink = nullptr;
    }
    m_destPath.clear();
    m_asset = ReleaseAsset{};
    m_written = 0;
}

} // namespace Em
