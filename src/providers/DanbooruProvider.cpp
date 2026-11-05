#include "DanbooruProvider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QUrlQuery>
#include <limits>

namespace bt {

void DanbooruProvider::get(const QUrl &url, Callback<QJsonDocument> callback)
{
    const auto generation = m_generation;
    m_http.send("GET", url, {}, [this, generation, callback](Result<HttpResponse> response) {
        if (generation != m_generation)
            return;
        if (!response && response.error.kind == ErrorKind::Authentication)
            m_authenticated = false;
        if (!response)
            callback(Result<QJsonDocument>::failure(response.error));
        else
            callback(parseJson(response.value->body));
    });
}

void DanbooruProvider::authenticate(const QString &secret, Callback<AuthInfo> callback)
{
    m_authenticated = false;
    m_http.setBasicAuth(instance().username, secret);
    get(endpoint(instance().baseUrl, "profile.json"), [this, callback](Result<QJsonDocument> document) {
        if (!document) {
            m_http.clearAuthentication();
            callback(Result<AuthInfo>::failure(document.error));
            return;
        }
        const auto object = document.value->object();
        const auto id = jsonId(object.value("id"));
        const auto name = object.value("name").toString();
        if (id < 1 || name.isEmpty() || name.compare(instance().username, Qt::CaseInsensitive) != 0) {
            m_http.clearAuthentication();
            callback(Result<AuthInfo>::failure(
                {ErrorKind::Authentication, tr("The profile did not match the configured account.")}));
            return;
        }
        m_authenticated = true;
        m_http.setAccountId(id);
        callback(Result<AuthInfo>::success({name, id}));
    });
}

Result<Post> DanbooruProvider::parsePost(const QJsonObject &object, const QUrl &base)
{
    Post post;
    post.id = jsonId(object.value("id"));
    if (post.id < 1 || !object.value("tag_string").isString())
        return Result<Post>::failure(
            {ErrorKind::InvalidData, QStringLiteral("The server returned an invalid Danbooru post.")});
    post.tags = splitTags(object.value("tag_string").toString(), ProviderKind::Danbooru);
    auto file = object.value("file_url").toString();
    if (file.isEmpty())
        file = object.value("large_file_url").toString();
    if (!file.isEmpty())
        post.mediaUrl = base.resolved(QUrl(file));
    const auto preview = object.value("preview_file_url").toString();
    if (!preview.isEmpty())
        post.previewUrl = base.resolved(QUrl(preview));
    post.extension = object.value("file_ext").toString().toLower();
    post.dimensions = {object.value("image_width").toInt(), object.value("image_height").toInt()};
    post.deleted = object.value("is_deleted").toBool();
    return Result<Post>::success(post);
}

void DanbooruProvider::fetchPost(qint64 id, Callback<Post> callback)
{
    get(endpoint(instance().baseUrl, QStringLiteral("posts/%1.json").arg(id)),
        [this, id, callback](Result<QJsonDocument> document) {
            if (!document) {
                callback(Result<Post>::failure(document.error));
                return;
            }
            auto result = parsePost(document.value->object(), instance().baseUrl);
            if (result && result.value->id != id)
                result =
                    Result<Post>::failure({ErrorKind::InvalidData, tr("The response contained a different post ID.")});
            callback(result);
        });
}

void DanbooruProvider::searchPosts(const SearchRequest &request, Callback<SearchPage> callback)
{
    auto url = endpoint(instance().baseUrl, "posts.json");
    QUrlQuery query;
    query.addQueryItem("tags", request.query + " order:id_desc");
    const int limit = qBound(1, request.limit, 100);
    query.addQueryItem("limit", QString::number(limit));
    if (!request.cursor.isEmpty())
        query.addQueryItem("page", request.cursor);
    url.setQuery(query);
    get(url, [this, callback, limit](Result<QJsonDocument> document) {
        if (!document) {
            callback(Result<SearchPage>::failure(document.error));
            return;
        }
        if (!document.value->isArray()) {
            callback(Result<SearchPage>::failure({ErrorKind::InvalidData, tr("Expected a Danbooru post array.")}));
            return;
        }
        SearchPage page;
        qint64 lowest = std::numeric_limits<qint64>::max();
        for (const auto &value : document.value->array()) {
            auto post = parsePost(value.toObject(), instance().baseUrl);
            if (!post) {
                callback(Result<SearchPage>::failure(post.error));
                return;
            }
            lowest = qMin(lowest, post.value->id);
            page.posts.append(*post.value);
        }
        page.more = page.posts.size() >= limit;
        if (!page.posts.isEmpty())
            page.nextCursor = 'b' + QString::number(lowest);
        callback(Result<SearchPage>::success(page));
    });
}

void DanbooruProvider::searchTags(const QString &prefix, Callback<QList<TagSuggestion>> callback)
{
    auto url = endpoint(instance().baseUrl, "tags.json");
    QUrlQuery query;
    query.addQueryItem("search[name_or_alias_matches]", normalizeTag(prefix, ProviderKind::Danbooru) + '*');
    query.addQueryItem("limit", "20");
    url.setQuery(query);
    get(url, [this, callback, prefix](Result<QJsonDocument> document) {
        if (!document || !document.value->isArray()) {
            callback(Result<QList<TagSuggestion>>::failure(
                document ? Error{ErrorKind::InvalidData, tr("Expected a tag array.")} : document.error));
            return;
        }
        QList<TagSuggestion> suggestions;
        for (const auto &value : document.value->array()) {
            const auto tag = value.toObject();
            const auto name = tag.value("name").toString();
            if (name.isEmpty())
                continue;
            TagSuggestion suggestion;
            suggestion.name = name;
            if (tag.value("category").isDouble())
                suggestion.category = tag.value("category").toInt();
            if (tag.value("post_count").isDouble())
                suggestion.postCount = tag.value("post_count").toInteger();
            suggestion.deprecated = tag.value("is_deprecated").toBool();
            suggestions.append(suggestion);
        }
        auto aliasUrl = endpoint(instance().baseUrl, "tag_aliases.json");
        QUrlQuery aliases;
        aliases.addQueryItem("search[antecedent_name_matches]", normalizeTag(prefix, ProviderKind::Danbooru) + '*');
        aliases.addQueryItem("search[status]", "active");
        aliases.addQueryItem("limit", "10");
        aliasUrl.setQuery(aliases);
        get(aliasUrl, [callback, suggestions](Result<QJsonDocument> aliasDocument) mutable {
            if (aliasDocument && aliasDocument.value->isArray()) {
                for (const auto &value : aliasDocument.value->array()) {
                    const auto alias = value.toObject();
                    if (alias.value("status").toString() != "active")
                        continue;
                    const auto source = alias.value("antecedent_name").toString();
                    const auto target = alias.value("consequent_name").toString();
                    if (source.isEmpty() || target.isEmpty())
                        continue;
                    bool found = false;
                    for (auto &suggestion : suggestions) {
                        if (suggestion.name == target) {
                            suggestion.alias = source;
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        suggestions.append({target, {}, {}, false, source});
                }
            }
            callback(Result<QList<TagSuggestion>>::success(suggestions));
        });
    });
}

void DanbooruProvider::updateTags(qint64 id, const QSet<QString> &tags, const QSet<QString> &latest,
                                  Callback<bool> callback)
{
    if (!m_authenticated) {
        callback(Result<bool>::failure({ErrorKind::Authentication, tr("Sign in before saving tags.")}));
        return;
    }
    const QJsonObject body{{"post", QJsonObject{{"tag_string", sortedTags(tags).join(' ')},
                                                {"old_tag_string", sortedTags(latest).join(' ')}}}};
    writeLater([this, id, body, callback] {
        m_http.send("PUT", endpoint(instance().baseUrl, QStringLiteral("posts/%1.json").arg(id)),
                    QJsonDocument(body).toJson(QJsonDocument::Compact),
                    [this, generation = m_generation, id, callback](Result<HttpResponse> response) {
                        if (generation != m_generation)
                            return;
                        if (!response) {
                            callback(Result<bool>::failure(response.error));
                            return;
                        }
                        const auto document = parseJson(response.value->body);
                        if (!document || jsonId(document.value->object().value("id")) != id ||
                            document.value->object().value("success").toBool(true) == false) {
                            callback(Result<bool>::failure(
                                {ErrorKind::InvalidData,
                                 QStringLiteral("Unexpected update response; verifying the post before retrying."),
                                 response.value->status, 0, true}));
                            return;
                        }
                        callback(Result<bool>::success(true));
                    });
    });
}

} // namespace bt
