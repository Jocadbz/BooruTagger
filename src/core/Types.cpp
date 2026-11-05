#include "Types.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <stdexcept>

namespace bt {

QString normalizeTag(QString tag, ProviderKind kind)
{
    tag = tag.trimmed();
    if (kind == ProviderKind::Danbooru)
        tag = tag.toLower();
    return tag;
}

QStringList splitTags(const QString &text, ProviderKind kind)
{
    QStringList tags;
    QSet<QString> seen;
    for (const auto &part : text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
        const auto tag = normalizeTag(part, kind);
        if (!tag.isEmpty() && !seen.contains(tag)) {
            seen.insert(tag);
            tags.append(tag);
        }
    }
    return tags;
}

QStringList sortedTags(const QSet<QString> &tags)
{
    auto list = tags.values();
    list.sort(Qt::CaseSensitive);
    return list;
}

QSet<QString> tagSet(const QStringList &tags)
{
    return {tags.begin(), tags.end()};
}

QString tagsJson(const QSet<QString> &tags)
{
    return QString::fromUtf8(
        QJsonDocument(QJsonArray::fromStringList(sortedTags(tags))).toJson(QJsonDocument::Compact));
}

QSet<QString> tagsFromJson(const QString &json)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray())
        throw std::runtime_error("A stored tag snapshot is damaged. No edit was submitted.");
    QSet<QString> result;
    for (const auto &item : document.array()) {
        if (!item.isString())
            throw std::runtime_error("A stored tag snapshot contains invalid tags.");
        result.insert(item.toString());
    }
    return result;
}

QUrl endpoint(const QUrl &base, const QString &path)
{
    auto url = base;
    auto prefix = url.path();
    while (prefix.endsWith('/'))
        prefix.chop(1);
    url.setPath(prefix + '/' + path);
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

bool sameOrigin(const QUrl &a, const QUrl &b)
{
    return a.scheme() == b.scheme() && a.host().compare(b.host(), Qt::CaseInsensitive) == 0 &&
           a.port(a.scheme() == "https" ? 443 : 80) == b.port(b.scheme() == "https" ? 443 : 80);
}

QString validateInstance(const Instance &instance)
{
    const auto &url = instance.baseUrl;
    if (instance.name.trimmed().isEmpty() || instance.username.trimmed().isEmpty())
        return QStringLiteral("An instance name and account name are required.");
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment())
        return QStringLiteral("Use an installation base URL without credentials, query, or fragment.");
    if (url.scheme() != "https" && !(url.scheme() == "http" && instance.allowHttp))
        return QStringLiteral("Use HTTPS, or explicitly allow insecure HTTP for this instance.");
    return {};
}

QString kindName(ProviderKind kind)
{
    return kind == ProviderKind::Danbooru ? QStringLiteral("Danbooru") : QStringLiteral("Shimmie2");
}

} // namespace bt
