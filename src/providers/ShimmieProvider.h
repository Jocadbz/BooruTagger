#pragma once

#include "BooruProvider.h"
#include <QHash>

namespace bt {

class ShimmieProvider : public BooruProvider {
    Q_OBJECT
  public:
    using BooruProvider::BooruProvider;
    void authenticate(const QString &secret, Callback<AuthInfo> callback) override;
    void fetchPost(qint64 id, Callback<Post> callback) override;
    void searchPosts(const SearchRequest &request, Callback<SearchPage> callback) override;
    void searchTags(const QString &prefix, Callback<QList<TagSuggestion>> callback) override;
    void updateTags(qint64 id, const QSet<QString> &tags, const QSet<QString> &latest,
                    Callback<bool> callback) override;
    bool remoteAutocomplete() const override { return false; }
    static Result<QJsonObject> parseEnvelope(const QByteArray &body);
    static Result<Post> parsePost(const QJsonObject &object, const QUrl &base);

  private:
    void graphql(const QString &query, const QJsonObject &variables, Callback<QJsonObject> callback,
                 bool login = false);
    void detectSchema(std::function<void()> callback);
    QJsonObject operation(const QString &field, const QJsonObject &arguments, const QString &selection,
                          bool mutation) const;
    QHash<QString, QString> m_argumentTypes;
    bool m_schemaChecked = false;
};

} // namespace bt
