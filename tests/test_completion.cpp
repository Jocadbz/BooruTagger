#include "tagging/Completion.h"
#include "settings/Credentials.h"
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using namespace bt;

class CompletionProvider : public BooruProvider {
public:
    using BooruProvider::BooruProvider;
    QList<QString> prefixes;
    QList<Callback<QList<TagSuggestion>>> callbacks;
    void authenticate(const QString &, Callback<AuthInfo>) override {}
    void fetchPost(qint64, Callback<Post>) override {}
    void searchPosts(const SearchRequest &, Callback<SearchPage>) override {}
    void updateTags(qint64, const QSet<QString> &, const QSet<QString> &, Callback<bool>) override {}
    bool remoteAutocomplete() const override { return true; }
    void searchTags(const QString &prefix, Callback<QList<TagSuggestion>> callback) override {
        prefixes.append(prefix); callbacks.append(callback);
    }
};

class CompletionTests : public QObject {
    Q_OBJECT
private slots:
    void cachedAndStaleResults() {
        Database db(":memory:");
        Instance a{"a", "one", ProviderKind::Danbooru, QUrl("https://a.test"), "alice", false};
        Instance b{"b", "two", ProviderKind::Shimmie, QUrl("https://b.test"), "bob", false};
        db.saveInstance(a); db.saveInstance(b);
        db.observeTags("a", {"solo", "smile", "smiling"});
        db.observeTags("b", {"Custom:Tag"});
        CompletionProvider provider(a), other(b);
        Completion completion(&db);
        QList<TagSuggestion> last;
        connect(&completion, &Completion::suggestions, this, [&](const auto &tags) { last = tags; });
        completion.setProvider(&provider);
        completion.request("SO");
        QCOMPARE(last.first().name, QString("solo"));
        QTRY_COMPARE(provider.prefixes.size(), 1);
        completion.request("sm");
        provider.callbacks.first()(Result<QList<TagSuggestion>>::success({{"some_old_result", {}, {}, false, {}}}));
        QVERIFY(last.first().name.startsWith("sm"));
        QTRY_COMPARE(provider.prefixes.size(), 2);
        provider.callbacks.last()(Result<QList<TagSuggestion>>::success({{"smile", 0, 20, false, "smiling"}}));
        QCOMPARE(last.first().name, QString("smile"));
        completion.setExcluded({"smile"});
        QVERIFY(std::none_of(last.begin(), last.end(), [](const auto &tag) { return tag.name == "smile"; }));
        completion.request("smile");
        QTRY_COMPARE(provider.prefixes.size(), 3);
        completion.setProvider(&other);
        completion.request("Custom");
        provider.callbacks.last()(Result<QList<TagSuggestion>>::success({{"smile", 0, 20, false, {}}}));
        QCOMPARE(last.first().name, QString("Custom:Tag"));
        QVERIFY(!last.first().category);
        completion.request("custom");
        QVERIFY(last.isEmpty());
        completion.setProvider(nullptr);
        QVERIFY(last.isEmpty());
    }
    void sessionOnlyCredentials() {
        QTemporaryDir dir;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        QCoreApplication::setOrganizationName("BooruTaggerTests");
        QCoreApplication::setApplicationName("credentials");
        Credentials credentials;
        std::optional<Result<bool>> stored;
        credentials.store("a", "never-persist-this", false, [&](auto result) { stored = result; });
        QTRY_VERIFY(stored.has_value());
        QVERIFY(*stored);
        std::optional<Result<QString>> loaded;
        credentials.load("a", [&](auto result) { loaded = result; });
        QTRY_VERIFY(loaded.has_value());
        QCOMPARE(*loaded->value, QString("never-persist-this"));
        loaded.reset();
        credentials.load("b", [&](auto result) { loaded = result; });
        QTRY_VERIFY(loaded.has_value());
        QVERIFY(loaded->value->isEmpty());
        QVERIFY(QSettings().allKeys().isEmpty());
    }
};

QTEST_GUILESS_MAIN(CompletionTests)
#include "test_completion.moc"
