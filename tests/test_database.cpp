#include "database/Database.h"
#include <QTemporaryDir>
#include <QtTest>

using namespace bt;

class DatabaseTests : public QObject {
    Q_OBJECT
  private slots:
    void persistenceAndIsolation()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("sessions.sqlite");
        QString sessionId;
        {
            Database db(path);
            db.saveInstance({"a", "one", ProviderKind::Danbooru, QUrl("https://a.test"), "alice", false});
            db.saveInstance({"b", "two", ProviderKind::Shimmie, QUrl("https://b.test/booru"), "bob", false});
            Session s;
            s.name = "cleanup";
            s.instanceId = "a";
            s.account = "alice";
            s = db.createSession(s);
            sessionId = s.id;
            QCOMPARE(db.appendQueue(s.id, {8, 3, 8, 5}), 3);
            QCOMPARE(db.appendQueue(s.id, {3, 9}), 1);
            db.sortQueue(s.id, true);
            QCOMPARE(db.queueEntry(s.id, 0)->postId, 3);
            QCOMPARE(db.queueEntry(s.id, 3)->postId, 9);
            s.status = "ready";
            s.current = 1;
            db.updateSession(s);
            Draft d;
            d.changes = {{"solo", "tagme"}, {"smile"}, {"tagme"}};
            d.state = SubmissionState::Submitting;
            d.hasIntended = true;
            d.intended = d.changes.result();
            d.sufficient = true;
            db.saveDraft(s.id, 5, d);
            db.observeTags("a", {"solo", "custom_tag"}, true);
            db.cacheTags("a", {{"smile", 0, 100, false, "smiling"}});
            db.saveSearchCache("a", "sm", {{"smile", 0, 100, false, "smiling"}});
            db.savePreset("a", "faces", {"solo", "smile"});
            QVERIFY(db.cachedTags("b", "s").isEmpty());
            QVERIFY(db.searchCache("b", "sm").isEmpty());
            QVERIFY(db.presets("b").isEmpty());
            auto invalid = s;
            invalid.id.clear();
            invalid.account = "bob";
            QVERIFY_EXCEPTION_THROWN(db.createSession(invalid), std::runtime_error);
            auto i = db.instance("a").value();
            i.baseUrl = QUrl("https://b.test");
            QVERIFY_EXCEPTION_THROWN(db.saveInstance(i), std::runtime_error);
            QVERIFY_EXCEPTION_THROWN(db.saveDraft(s.id, 999, d), std::runtime_error);
            QVERIFY_EXCEPTION_THROWN(db.complete(s, 999, d, d.intended, true), std::runtime_error);
            QVERIFY(db.draft(s.id, 5));
            QCOMPARE(s.current, 1);
            auto wrongContext = s;
            wrongContext.instanceId = "b";
            QVERIFY_EXCEPTION_THROWN(db.updateSession(wrongContext), std::runtime_error);
        }
        {
            Database db(path);
            auto s = db.session(sessionId).value();
            QCOMPARE(s.current, 1);
            QCOMPARE(s.instanceId, QString("a"));
            QCOMPARE(db.queueSize(s.id), 4);
            const auto d = db.draft(s.id, 5).value();
            QCOMPARE(d.state, SubmissionState::Submitting);
            QVERIFY(d.sufficient);
            QCOMPARE(d.changes.result(), tagSet({"solo", "smile"}));
            QCOMPARE(db.cachedTags("a", "custom_").size(), 1);
            QCOMPARE(db.cachedTags("a", "sm").first().postCount.value(), 100);
            QCOMPARE(db.searchCache("a", "sm").first().alias, QString("smiling"));
            db.complete(s, 5, d, d.intended, true);
            QCOMPARE(s.current, 2);
            QCOMPARE(db.doneCount(s.id), 1);
            QVERIFY(!db.draft(s.id, 5));
            QCOMPARE(db.history("a").first().outcome, QString("completed"));
            QVERIFY(db.history("b").isEmpty());
            db.markQueue(s.id, 8, "skipped", "missing");
            QCOMPARE(db.nextPending(s.id, 1), 3);
            QCOMPARE(db.presets("a").value("faces"), QStringList({"smile", "solo"}));
        }
    }
    void rangeOrder()
    {
        Database db(":memory:");
        db.saveInstance({"a", "one", ProviderKind::Shimmie, QUrl("https://a.test"), "user", false});
        Session s;
        s.name = "range";
        s.instanceId = "a";
        s.account = "user";
        s = db.createSession(s);
        db.appendQueue(s.id, idRange(8, 4));
        QCOMPARE(db.queueEntry(s.id, 0)->postId, 8);
        QCOMPARE(db.queueEntry(s.id, 4)->postId, 4);
        db.sortQueue(s.id, false);
        QCOMPARE(db.queueEntry(s.id, 0)->postId, 8);
    }
};

QTEST_GUILESS_MAIN(DatabaseTests)
#include "test_database.moc"
