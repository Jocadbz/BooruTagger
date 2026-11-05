#pragma once

#include "BooruProvider.h"

namespace bt {

class DanbooruProvider : public BooruProvider {
    Q_OBJECT
  public:
    using BooruProvider::BooruProvider;
    void authenticate(const QString &secret, Callback<AuthInfo> callback) override;
    void fetchPost(qint64 id, Callback<Post> callback) override;
    void searchPosts(const SearchRequest &request, Callback<SearchPage> callback) override;
    void searchTags(const QString &prefix, Callback<QList<TagSuggestion>> callback) override;
    void updateTags(qint64 id, const QSet<QString> &tags, const QSet<QString> &latest,
                    Callback<bool> callback) override;
    bool remoteAutocomplete() const override { return true; }
    static Result<Post> parsePost(const QJsonObject &object, const QUrl &base);

  private:
    void get(const QUrl &url, Callback<QJsonDocument> callback);
};

} // namespace bt
