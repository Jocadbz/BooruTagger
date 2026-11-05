#include "ShimmieProvider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace bt {
namespace {
QString typeName(const QJsonObject &type)
{
    const auto kind = type.value("kind").toString();
    if (kind == "NON_NULL")
        return typeName(type.value("ofType").toObject()) + '!';
    if (kind == "LIST")
        return '[' + typeName(type.value("ofType").toObject()) + ']';
    return type.value("name").toString();
}

QString literal(const QJsonValue &value)
{
    if (value.isArray()) {
        QStringList values;
        for (const auto &item : value.toArray())
            values.append(literal(item));
        return '[' + values.join(',') + ']';
    }
    if (value.isObject()) {
        QStringList values;
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            values.append(it.key() + ':' + literal(it.value()));
        return '{' + values.join(',') + '}';
    }
    auto json = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}
}

Result<QJsonObject> ShimmieProvider::parseEnvelope(const QByteArray &body)
{
    const auto document = parseJson(body);
    if (!document)
        return Result<QJsonObject>::failure(document.error);
    if (!document.value->isObject())
        return Result<QJsonObject>::failure({ErrorKind::InvalidData, QStringLiteral("Expected a GraphQL response object.")});
    const auto object = document.value->object();
    if (object.contains("errors") && !object.value("errors").isArray())
        return Result<QJsonObject>::failure({ErrorKind::InvalidData, QStringLiteral("Invalid GraphQL errors field.")});
    if (object.contains("errors") && !object.value("errors").toArray().isEmpty()) {
        QStringList errors;
        ErrorKind kind = ErrorKind::Other;
        for (const auto &error : object.value("errors").toArray()) {
            const auto item = error.toObject();
            errors.append(item.value("message").toString().left(300));
            const auto code = item.value("extensions").toObject().value("code").toString();
            if (code == "UNAUTHENTICATED")
                kind = ErrorKind::Authentication;
            else if (code == "FORBIDDEN")
                kind = ErrorKind::Permission;
        }
        return Result<QJsonObject>::failure({kind, QStringLiteral("GraphQL: ") + errors.join("; ")});
    }
    if (!object.value("data").isObject())
        return Result<QJsonObject>::failure({ErrorKind::InvalidData, QStringLiteral("GraphQL response has no usable data.")});
    return Result<QJsonObject>::success(object.value("data").toObject());
}

void ShimmieProvider::graphql(const QString &query, const QJsonObject &variables, Callback<QJsonObject> callback, bool login)
{
    const QJsonObject body{{"query", query}, {"variables", variables}};
    m_http.send("POST", endpoint(instance().baseUrl, "graphql"), QJsonDocument(body).toJson(QJsonDocument::Compact),
                [this, callback, login](Result<HttpResponse> response) {
        auto result = response ? parseEnvelope(response.value->body) : Result<QJsonObject>::failure(response.error);
        if (!result) {
            result.error.message = login ? tr("Shimmie login failed. Check the credentials and the GraphQL extension (2.12+).")
                                         : m_http.redact(result.error.message);
        }
        callback(result);
    });
}

void ShimmieProvider::detectSchema(std::function<void()> callback)
{
    if (m_schemaChecked) {
        callback();
        return;
    }
    m_schemaChecked = true;
    const QString type = "type { kind name ofType { kind name ofType { kind name ofType { kind name } } } }";
    const QString fields = "fields { name args { name " + type + " } }";
    graphql("query BooruTaggerSchema { __schema { queryType { " + fields + " } mutationType { " + fields + " } } }", {},
            [this, callback](Result<QJsonObject> result) {
        if (result) {
            const auto schema = result.value->value("__schema").toObject();
            for (const auto &root : {QStringLiteral("queryType"), QStringLiteral("mutationType")}) {
                for (const auto &fieldValue : schema.value(root).toObject().value("fields").toArray()) {
                    const auto field = fieldValue.toObject();
                    const auto name = field.value("name").toString();
                    if (name != "login" && name != "posts" && name != "update_post_metadata")
                        continue;
                    for (const auto &argValue : field.value("args").toArray()) {
                        const auto arg = argValue.toObject();
                        const auto type = typeName(arg.value("type").toObject());
                        if (QRegularExpression("^[A-Za-z_\\[][A-Za-z0-9_\\[\\]!]*$").match(type).hasMatch())
                            m_argumentTypes.insert(name + '/' + arg.value("name").toString(), type);
                    }
                }
            }
        }
        emit status(tr("Shimmie autocomplete uses this instance's locally observed tags."));
        callback();
    });
}

QJsonObject ShimmieProvider::operation(const QString &field, const QJsonObject &arguments,
                                     const QString &selection, bool mutation) const
{
    bool variablesAvailable = true;
    for (auto it = arguments.begin(); it != arguments.end(); ++it)
        variablesAvailable &= m_argumentTypes.contains(field + '/' + it.key());
    QStringList definitions;
    QStringList values;
    for (auto it = arguments.begin(); it != arguments.end(); ++it) {
        if (variablesAvailable) {
            definitions.append('$' + it.key() + ':' + m_argumentTypes.value(field + '/' + it.key()));
            values.append(it.key() + ":$" + it.key());
        } else {
            values.append(it.key() + ':' + literal(it.value()));
        }
    }
    QString query = mutation ? "mutation BooruTagger" : "query BooruTagger";
    if (!definitions.isEmpty())
        query += '(' + definitions.join(',') + ')';
    query += " { " + field + '(' + values.join(',') + ") { " + selection + " } }";
    return {{"query", query}, {"variables", variablesAvailable ? arguments : QJsonObject{}}};
}

void ShimmieProvider::authenticate(const QString &secret, Callback<AuthInfo> callback)
{
    m_authenticated = false;
    m_http.clearAuthentication();
    detectSchema([this, secret, callback] {
        const auto request = operation("login", {{"username", instance().username}, {"password", secret}}, "user { name } session", true);
        graphql(request.value("query").toString(), request.value("variables").toObject(), [this, callback](Result<QJsonObject> result) {
            if (!result) {
                callback(Result<AuthInfo>::failure(result.error));
                return;
            }
            const auto login = result.value->value("login").toObject();
            const auto name = login.value("user").toObject().value("name").toString();
            const auto session = login.value("session").toString();
            if (name.isEmpty() || session.isEmpty() || name.compare(instance().username, Qt::CaseInsensitive) != 0) {
                callback(Result<AuthInfo>::failure({ErrorKind::Authentication, tr("The Shimmie login response was incomplete or returned another account.")}));
                return;
            }
            m_http.setSessionCookies(name, session);
            m_authenticated = true;
            callback(Result<AuthInfo>::success({name, 0}));
        }, true);
    });
}

Result<Post> ShimmieProvider::parsePost(const QJsonObject &object, const QUrl &base)
{
    Post post;
    post.id = jsonId(object.value("post_id"));
    const auto tags = object.value("tags");
    if (post.id < 1 || (!tags.isString() && !tags.isArray()))
        return Result<Post>::failure({ErrorKind::InvalidData, QStringLiteral("Invalid Shimmie post: post_id and tags are required.")});
    if (tags.isString()) {
        post.tags = splitTags(tags.toString(), ProviderKind::Shimmie);
    } else {
        for (const auto &value : tags.toArray()) {
            if (!value.isString())
                return Result<Post>::failure({ErrorKind::InvalidData, QStringLiteral("Invalid Shimmie tag array.")});
            post.tags.append(value.toString());
        }
        post.tags.removeDuplicates();
    }
    const auto link = object.value("image_link").toString();
    if (!link.isEmpty()) {
        auto directory = base;
        if (!directory.path().endsWith('/'))
            directory.setPath(directory.path() + '/');
        post.mediaUrl = directory.resolved(QUrl(link));
    }
    post.extension = object.value("ext").toString().toLower();
    return Result<Post>::success(post);
}

void ShimmieProvider::searchPosts(const SearchRequest &request, Callback<SearchPage> callback)
{
    const int limit = qBound(1, request.limit, 100);
    const auto terms = request.query.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    const auto operationRequest = operation("posts", {{"limit", limit}, {"offset", request.offset}, {"terms", QJsonArray::fromStringList(terms)}},
                                            "id post_id tags source hash ext image_link", false);
    graphql(operationRequest.value("query").toString(), operationRequest.value("variables").toObject(),
            [this, limit, callback](Result<QJsonObject> result) {
        if (!result) {
            callback(Result<SearchPage>::failure(result.error));
            return;
        }
        if (!result.value->value("posts").isArray()) {
            callback(Result<SearchPage>::failure({ErrorKind::InvalidData, tr("Expected a Shimmie post array. Check the GraphQL schema version.")}));
            return;
        }
        SearchPage page;
        for (const auto &value : result.value->value("posts").toArray()) {
            const auto post = parsePost(value.toObject(), instance().baseUrl);
            if (!post) {
                callback(Result<SearchPage>::failure(post.error));
                return;
            }
            page.posts.append(*post.value);
        }
        page.more = page.posts.size() >= limit;
        callback(Result<SearchPage>::success(page));
    });
}

void ShimmieProvider::fetchPost(qint64 id, Callback<Post> callback)
{
    searchPosts({QStringLiteral("id=%1").arg(id), 2, {}, 0}, [id, callback](Result<SearchPage> page) {
        if (!page) {
            callback(Result<Post>::failure(page.error));
            return;
        }
        for (const auto &post : page.value->posts) {
            if (post.id == id) {
                callback(Result<Post>::success(post));
                return;
            }
        }
        callback(Result<Post>::failure({ErrorKind::Missing, QStringLiteral("The Shimmie post is missing or inaccessible.")}));
    });
}

void ShimmieProvider::searchTags(const QString &, Callback<QList<TagSuggestion>> callback)
{
    QMetaObject::invokeMethod(this, [callback] { callback(Result<QList<TagSuggestion>>::success({})); }, Qt::QueuedConnection);
}

void ShimmieProvider::updateTags(qint64 id, const QSet<QString> &tags, const QSet<QString> &, Callback<bool> callback)
{
    if (!m_authenticated) {
        callback(Result<bool>::failure({ErrorKind::Authentication, tr("Sign in before saving tags.")}));
        return;
    }
    const QJsonArray metadata{QJsonObject{{"key", "tags"}, {"value", sortedTags(tags).join(' ')}}};
    const auto request = operation("update_post_metadata", {{"post_id", id}, {"metadata", metadata}}, "id tags", true);
    writeLater([this, request, callback] {
        graphql(request.value("query").toString(), request.value("variables").toObject(), [callback](Result<QJsonObject> result) {
            if (!result) {
                result.error.uncertain = result.error.kind != ErrorKind::Permission && result.error.kind != ErrorKind::Authentication
                    && result.error.kind != ErrorKind::RateLimit;
                callback(Result<bool>::failure(result.error));
            } else if (!result.value->value("update_post_metadata").isObject()
                       || !result.value->value("update_post_metadata").toObject().contains("tags")) {
                callback(Result<bool>::failure({ErrorKind::InvalidData, QStringLiteral("Unexpected metadata response; verifying before retrying."), 200, 0, true}));
            } else {
                callback(Result<bool>::success(true));
            }
        });
    });
}

}
