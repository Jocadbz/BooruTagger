#include "HttpClient.h"

#include <QDir>
#include <QNetworkCacheMetaData>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QNetworkDiskCache>
#include <QStandardPaths>

namespace bt {
namespace {
class MediaDiskCache : public QNetworkDiskCache {
  public:
    using QNetworkDiskCache::QNetworkDiskCache;
    QIODevice *prepare(const QNetworkCacheMetaData &data) override { return QNetworkDiskCache::prepare(clean(data)); }
    void updateMetaData(const QNetworkCacheMetaData &data) override { QNetworkDiskCache::updateMetaData(clean(data)); }

  private:
    static QNetworkCacheMetaData clean(QNetworkCacheMetaData data)
    {
        auto headers = data.rawHeaders();
        headers.removeIf([](const auto &header) {
            return header.first.compare("Set-Cookie", Qt::CaseInsensitive) == 0 ||
                   header.first.compare("Authorization", Qt::CaseInsensitive) == 0;
        });
        data.setRawHeaders(headers);
        return data;
    }
};
} // namespace

HttpClient::HttpClient(Instance instance, QObject *parent) : QObject(parent), m_instance(std::move(instance))
{
    m_manager.setRedirectPolicy(QNetworkRequest::ManualRedirectPolicy);
    auto *cache = new MediaDiskCache(&m_manager);
    cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/media/" +
                             m_instance.id);
    cache->setMaximumCacheSize(128 * 1024 * 1024);
    m_manager.setCache(cache);
}

HttpClient::~HttpClient()
{
    for (auto *reply : std::as_const(m_replies)) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
    }
}

void HttpClient::setBasicAuth(const QString &username, const QString &secret)
{
    clearAuthentication();
    m_authorization = "Basic " + (username + ':' + secret).toUtf8().toBase64();
    m_secrets = {secret, QString::fromLatin1(m_authorization)};
}

void HttpClient::setSessionCookies(const QString &username, const QString &session)
{
    clearAuthentication();
    QList<QNetworkCookie> cookies;
    for (const auto &pair : {qMakePair(QByteArray("shm_user"), username.toUtf8()),
                             qMakePair(QByteArray("shm_session"), session.toUtf8())}) {
        QNetworkCookie cookie(pair.first, pair.second);
        cookie.setPath(m_instance.baseUrl.path().isEmpty() ? "/" : m_instance.baseUrl.path());
        cookie.setSecure(m_instance.baseUrl.scheme() == "https");
        cookie.setHttpOnly(true);
        cookies.append(cookie);
    }
    m_manager.cookieJar()->setCookiesFromUrl(cookies, m_instance.baseUrl);
    m_secrets = {session};
}

void HttpClient::clearAuthentication()
{
    m_authorization.clear();
    m_accountId = 0;
    m_secrets.clear();
    m_manager.setCookieJar(new QNetworkCookieJar(&m_manager));
    m_manager.clearAccessCache();
}

QString HttpClient::redact(QString text) const
{
    for (const auto &secret : m_secrets) {
        if (!secret.isEmpty())
            text.replace(secret, QStringLiteral("[redacted]"));
    }
    return text.left(600);
}

QNetworkReply *HttpClient::send(const QByteArray &method, const QUrl &url, const QByteArray &body,
                                Callback<HttpResponse> callback, bool media, int redirects)
{
    const bool local = sameOrigin(url, m_instance.baseUrl);
    if ((!media && !local) || (url.scheme() != "https" && !(url.scheme() == "http" && m_instance.allowHttp)) ||
        !url.userInfo().isEmpty()) {
        QMetaObject::invokeMethod(
            this,
            [callback] {
                callback(Result<HttpResponse>::failure(
                    {ErrorKind::Permission, QStringLiteral("Blocked an unsafe network URL.")}));
            },
            Qt::QueuedConnection);
        return nullptr;
    }
    if (!media && m_retryAt > QDateTime::currentDateTimeUtc()) {
        const auto seconds = qMax<qint64>(1, QDateTime::currentDateTimeUtc().secsTo(m_retryAt) + 1);
        QMetaObject::invokeMethod(
            this,
            [callback, seconds] {
                callback(Result<HttpResponse>::failure(
                    {ErrorKind::RateLimit,
                     QStringLiteral("Respecting Retry-After: wait %1 seconds before retrying.").arg(seconds), 429,
                     static_cast<int>(seconds)}));
            },
            Qt::QueuedConnection);
        return nullptr;
    }
    QNetworkRequest request(url);
    request.setTransferTimeout(media ? 60000 : 30000);
    QByteArray agent = "BooruTagger/" BOORUTAGGER_VERSION " (native tagging workstation";
    if (m_accountId > 0)
        agent += "; user #" + QByteArray::number(m_accountId);
    request.setRawHeader("User-Agent", agent + ')');
    request.setRawHeader("Accept", media ? "*/*" : "application/json");
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         media ? QNetworkRequest::PreferCache : QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, media);
    if (!local) {
        request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    }
    if (local && !m_authorization.isEmpty())
        request.setRawHeader("Authorization", m_authorization);
    if (!body.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // API redirects are rejected. Media redirects are followed only after rebuilding the request.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto *reply = m_manager.sendCustomRequest(request, method, body);
    reply->setProperty("mediaRequest", media);
    m_replies.insert(reply);
    const qint64 maximum = media ? 64 * 1024 * 1024 : 16 * 1024 * 1024;
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, this, [reply, bytes, maximum] {
        bytes->append(reply->readAll());
        if (bytes->size() > maximum) {
            reply->setProperty("tooLarge", true);
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::metaDataChanged, this, [reply, maximum] {
        if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > maximum) {
            reply->setProperty("tooLarge", true);
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, callback, method, url, media, redirects] {
        m_replies.remove(reply);
        bytes->append(reply->readAll());
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (media && status >= 300 && status < 400 && redirect.isValid() && redirects < 5) {
            reply->deleteLater();
            send("GET", url.resolved(redirect), {}, callback, true, redirects + 1);
            return;
        }
        Error error;
        error.httpStatus = status;
        if (reply->property("tooLarge").toBool()) {
            error.kind = ErrorKind::InvalidData;
            error.message = QStringLiteral("Response exceeds the download size limit (64 MiB for images).");
        } else if (status >= 300 && status < 400 && redirect.isValid()) {
            error.kind = ErrorKind::Other;
            error.message = media
                                ? QStringLiteral("Media redirected. Use the direct media URL supplied by the instance.")
                                : QStringLiteral("The API redirected. Check the configured installation URL.");
        } else if (status == 401) {
            error.kind = ErrorKind::Authentication;
            error.message = QStringLiteral("Authentication expired or the credentials were rejected. Sign in again.");
        } else if (status == 403) {
            error.kind = ErrorKind::Permission;
            error.message = QStringLiteral("The server denied this operation. Check account permissions.");
        } else if (status == 404 || status == 410) {
            error.kind = ErrorKind::Missing;
            error.message = QStringLiteral("This post is missing, deleted, or inaccessible.");
        } else if (status == 429) {
            error.kind = ErrorKind::RateLimit;
            bool numeric = false;
            int seconds = reply->rawHeader("Retry-After").toInt(&numeric);
            if (!numeric) {
                const auto when =
                    QDateTime::fromString(QString::fromLatin1(reply->rawHeader("Retry-After")), Qt::RFC2822Date);
                seconds = when.isValid() ? static_cast<int>(QDateTime::currentDateTimeUtc().secsTo(when)) : 5;
            }
            error.retryAfterSeconds = qMax(1, seconds);
            m_retryAt = QDateTime::currentDateTimeUtc().addSecs(error.retryAfterSeconds);
            error.message =
                QStringLiteral("Rate limited. Wait %1 seconds before retrying.").arg(error.retryAfterSeconds);
        } else if (reply->error() == QNetworkReply::SslHandshakeFailedError) {
            error.kind = ErrorKind::Network;
            error.message = QStringLiteral("TLS certificate verification failed. Fix the certificate or your OS trust "
                                           "store; certificate checks are never bypassed.");
        } else if (reply->error() != QNetworkReply::NoError || status >= 400) {
            error.kind =
                reply->error() == QNetworkReply::OperationCanceledError ? ErrorKind::Cancelled : ErrorKind::Network;
            error.message = status >= 400
                                ? QStringLiteral("Server returned HTTP %1.").arg(status)
                                : QStringLiteral("Network request failed. Check connectivity and TLS certificates.");
            error.uncertain = method != "GET" && (status == 0 || status >= 500);
        }
        reply->deleteLater();
        if (error.kind != ErrorKind::None)
            callback(Result<HttpResponse>::failure(error));
        else
            callback(Result<HttpResponse>::success({*bytes, status}));
    });
    return reply;
}

void HttpClient::cancelAll()
{
    const auto replies = m_replies;
    for (auto *reply : replies)
        reply->abort();
}

void HttpClient::cancelMedia()
{
    const auto replies = m_replies;
    for (auto *reply : replies) {
        if (reply->property("mediaRequest").toBool())
            reply->abort();
    }
}

} // namespace bt
