#include "HttpFixture.h"
#include "sessions/SessionEngine.h"
#include "ui/Dialogs.h"
#include <QApplication>
#include <QDialog>
#include <QNetworkCookieJar>
#include <QNetworkDiskCache>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace bt;

class RecoveryProvider : public BooruProvider {
  public:
    explicit RecoveryProvider(Instance instance) : BooruProvider(std::move(instance)) { m_authenticated = true; }
    int reads = 0;
    int writes = 0;
    void authenticate(const QString &, Callback<AuthInfo>) override {}
    void fetchPost(qint64, Callback<Post>) override { ++reads; }
    void searchPosts(const SearchRequest &, Callback<SearchPage>) override {}
    void searchTags(const QString &, Callback<QList<TagSuggestion>>) override {}
    void updateTags(qint64, const QSet<QString> &, const QSet<QString> &, Callback<bool>) override { ++writes; }
    bool remoteAutocomplete() const override { return false; }
};

class RecoveryTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void corruptedSnapshotBlocksWrites()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("test.sqlite");
        Database database(path);
        Instance instance{"recovery", "test", ProviderKind::Danbooru, QUrl("https://example.test"), "tester", false};
        database.saveInstance(instance);
        Session session;
        session.name = "recovery";
        session.instanceId = instance.id;
        session.account = instance.username;
        session = database.createSession(session);
        database.appendQueue(session.id, {1});
        session.status = "ready";
        database.updateSession(session);
        Draft draft;
        draft.changes = {{"solo"}, {"smile"}, {}};
        database.saveDraft(session.id, 1, draft);
        {
            auto other = QSqlDatabase::addDatabase("QSQLITE", "damage-fixture");
            other.setDatabaseName(path);
            QVERIFY(other.open());
            QSqlQuery query(other);
            QVERIFY(query.exec("UPDATE drafts SET original='invalid-json'"));
            query.finish();
            other.close();
        }
        QSqlDatabase::removeDatabase("damage-fixture");
        RecoveryProvider provider(instance);
        SessionEngine engine(&database);
        QSignalSpy failures(&engine, &SessionEngine::databaseError);
        engine.activate(session.id, &provider);
        QCOMPARE(failures.count(), 1);
        QVERIFY(engine.blocked());
        engine.save(true);
        QCOMPARE(provider.writes, 0);
        QCOMPARE(provider.reads, 0);
        QCOMPARE(database.queueEntry(session.id, 0)->status, QString("todo"));
    }
    void cacheDoesNotPersistSessionCookies()
    {
        HttpFixture server;
        server.handler = [](const FixtureRequest &) -> FixtureResponse {
            return {200,
                    "cache-fixture",
                    {{"Cache-Control", "public, max-age=3600"},
                     {"Set-Cookie", "shm_session=rotated-private-token; Path=/; HttpOnly"}},
                    false};
        };
        HttpClient client({"cache-fixture", "fixture", ProviderKind::Shimmie, server.base(), "tester", true});
        client.setSessionCookies("tester", "initial-private-token");
        const auto url = server.base("/image");
        std::optional<Result<HttpResponse>> response;
        client.send("GET", url, {}, [&](auto result) { response = result; }, true);
        QTRY_VERIFY(response.has_value());
        QVERIFY(*response);
        QTRY_VERIFY(client.manager()->cache()->metaData(url).isValid());
        const auto metadata = client.manager()->cache()->metaData(url);
        for (const auto &header : metadata.rawHeaders())
            QVERIFY(header.first.compare("Set-Cookie", Qt::CaseInsensitive) != 0);
        QCOMPARE(client.redact("rotated-private-token"), QString("[redacted]"));
        const auto requests = server.requests.size();
        response.reset();
        client.send("GET", url, {}, [&](auto result) { response = result; }, true);
        QTRY_VERIFY(response.has_value());
        QVERIFY(*response);
        QCOMPARE(server.requests.size(), requests);
    }
    void mediaCredentialsStayInTheInstallation()
    {
        HttpFixture server;
        HttpClient client({"scope-fixture", "fixture", ProviderKind::Danbooru, server.base("/booru"), "tester", true});
        client.setBasicAuth("tester", "secret");
        std::optional<Result<HttpResponse>> response;
        client.send("GET", server.base("/other-installation/image"), {}, [&](auto result) { response = result; }, true);
        QTRY_VERIFY(response.has_value());
        QVERIFY(*response);
        QVERIFY(!server.requests.last().headers.contains("authorization"));
        response.reset();
        client.send("GET", server.base("/booru/image"), {}, [&](auto result) { response = result; }, true);
        QTRY_VERIFY(response.has_value());
        QVERIFY(*response);
        QVERIFY(server.requests.last().headers.contains("authorization"));
    }
    void reviewDialogsHandleLargeTagSets()
    {
        TagMerge merge;
        for (int i = 0; i < 2000; ++i)
            merge.tags.insert(QStringLiteral("custom_tag_%1").arg(i));
        merge.remoteRemoved = {"solo"};
        QTimer::singleShot(0, [] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (dialog)
                dialog->reject();
        });
        QVERIFY(!reviewMerge(merge, nullptr));
        QTimer::singleShot(0, [] {
            auto *dialog = QApplication::activeModalWidget();
            if (dialog) {
                auto *button = dialog->findChild<QPushButton *>("applyMergedTags");
                if (button)
                    button->click();
            }
        });
        QVERIFY(reviewMerge(merge, nullptr));
        Post actual;
        actual.id = 1;
        actual.tags = {"solo"};
        QTimer::singleShot(0, [] {
            auto *dialog = QApplication::activeModalWidget();
            if (dialog) {
                auto *button = dialog->findChild<QPushButton *>("allowNewAttempt");
                if (button)
                    button->click();
            }
        });
        QCOMPARE(reviewVerification(merge.tags, actual, true, nullptr), VerificationDecision::AllowRetry);
    }
};

QTEST_MAIN(RecoveryTests)
#include "test_recovery.moc"
