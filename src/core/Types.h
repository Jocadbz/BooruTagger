#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QSet>
#include <QSize>
#include <QStringList>
#include <QUrl>
#include <functional>
#include <optional>

namespace bt {

enum class ProviderKind { Danbooru, Shimmie };

struct Instance {
    QString id;
    QString name;
    ProviderKind kind = ProviderKind::Danbooru;
    QUrl baseUrl;
    QString username;
    bool allowHttp = false;
};

struct Post {
    qint64 id = 0;
    QStringList tags;
    QUrl mediaUrl;
    QUrl previewUrl;
    QString extension;
    QSize dimensions;
    bool deleted = false;
};

struct TagSuggestion {
    QString name;
    std::optional<int> category;
    std::optional<qint64> postCount;
    bool deprecated = false;
    QString alias;
};

enum class ErrorKind { None, Network, Authentication, Permission, Missing, RateLimit, InvalidData, Cancelled, Other };

struct Error {
    ErrorKind kind = ErrorKind::None;
    QString message;
    int httpStatus = 0;
    int retryAfterSeconds = 0;
    bool uncertain = false;
};

template<class T> struct Result {
    std::optional<T> value;
    Error error;
    explicit operator bool() const { return value.has_value(); }
    static Result success(T value) { return {std::move(value), {}}; }
    static Result failure(Error error) { return {std::nullopt, std::move(error)}; }
};

template<class T> using Callback = std::function<void(Result<T>)>;

QString normalizeTag(QString tag, ProviderKind kind);
QStringList splitTags(const QString &text, ProviderKind kind);
QStringList sortedTags(const QSet<QString> &tags);
QSet<QString> tagSet(const QStringList &tags);
QString tagsJson(const QSet<QString> &tags);
QSet<QString> tagsFromJson(const QString &json);
QUrl endpoint(const QUrl &base, const QString &path);
bool sameOrigin(const QUrl &a, const QUrl &b);
QString validateInstance(const Instance &instance);
QString kindName(ProviderKind kind);

}

Q_DECLARE_METATYPE(bt::Post)
Q_DECLARE_METATYPE(bt::Error)
