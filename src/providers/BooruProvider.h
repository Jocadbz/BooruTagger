#pragma once

#include "core/Types.h"
#include "network/HttpClient.h"
#include <QJsonDocument>
#include <QTimer>

namespace bt {

struct AuthInfo {
    QString username;
    qint64 userId = 0;
};

struct SearchRequest {
    QString query;
    int limit = 100;
    QString cursor;
    int offset = 0;
};

struct SearchPage {
    QList<Post> posts;
    QString nextCursor;
    bool more = false;
};

class BooruProvider : public QObject {
    Q_OBJECT
  public:
    explicit BooruProvider(Instance instance, QObject *parent = nullptr);
    virtual void authenticate(const QString &secret, Callback<AuthInfo> callback) = 0;
    virtual void fetchPost(qint64 id, Callback<Post> callback) = 0;
    virtual void searchPosts(const SearchRequest &request, Callback<SearchPage> callback) = 0;
    virtual void searchTags(const QString &prefix, Callback<QList<TagSuggestion>> callback) = 0;
    virtual void updateTags(qint64 id, const QSet<QString> &tags, const QSet<QString> &latest,
                            Callback<bool> callback) = 0;
    virtual bool remoteAutocomplete() const = 0;
    virtual bool prefetchTagIndex() const { return false; }
    virtual void cancel();
    const Instance &instance() const { return m_http.instance(); }
    HttpClient *http() { return &m_http; }
    bool authenticated() const { return m_authenticated; }
  signals:
    void status(const QString &message);

  protected:
    void writeLater(std::function<void()> operation);
    HttpClient m_http;
    bool m_authenticated = false;
    quint64 m_generation = 0;

  private:
    QTimer m_writeTimer;
    QDateTime m_nextWrite;
    std::function<void()> m_write;
};

BooruProvider *createProvider(const Instance &instance, QObject *parent);
Result<QJsonDocument> parseJson(const QByteArray &body);
qint64 jsonId(const QJsonValue &value);

} // namespace bt
