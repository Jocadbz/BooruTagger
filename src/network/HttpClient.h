#pragma once

#include "core/Types.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace bt {

struct HttpResponse {
    QByteArray body;
    int status = 0;
};

class HttpClient : public QObject {
    Q_OBJECT
public:
    explicit HttpClient(Instance instance, QObject *parent = nullptr);
    ~HttpClient() override;
    QNetworkReply *send(const QByteArray &method, const QUrl &url, const QByteArray &body,
                       Callback<HttpResponse> callback, bool media = false, int redirects = 0);
    void setBasicAuth(const QString &username, const QString &secret);
    void setSessionCookies(const QString &username, const QString &session);
    void clearAuthentication();
    void setAccountId(qint64 id) { m_accountId = id; }
    void cancelAll();
    void cancelMedia();
    QString redact(QString text) const;
    QNetworkAccessManager *manager() { return &m_manager; }
    const Instance &instance() const { return m_instance; }
    QDateTime retryAt() const { return m_retryAt; }
signals:
    void activity(const QString &message);
private:
    Instance m_instance;
    QNetworkAccessManager m_manager;
    QByteArray m_authorization;
    QStringList m_secrets;
    QSet<QNetworkReply *> m_replies;
    QDateTime m_retryAt;
    qint64 m_accountId = 0;
};

}
