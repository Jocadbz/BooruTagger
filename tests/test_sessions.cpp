#include "sessions/SessionEngine.h"
#include <QTemporaryDir>
#include <QtTest>

using namespace bt;

class MockProvider : public BooruProvider {
  public:
    explicit MockProvider(const Instance &instance) : BooruProvider(instance) { m_authenticated = true; }
    QMap<qint64, Post> posts;
    QList<SearchPage> pages;
    QList<SearchRequest> searches;
    int writes = 0;
    Error writeError;
    bool applyBeforeError = false;
    bool fetchFails = false;
    bool normalizeResult = false;
    bool failConfirmation = false;
    int fetchDelay = 0;
    QSet<QString> lastBaseline;
    void authenticate(const QString &, Callback<AuthInfo> cb) override
    {
        cb(Result<AuthInfo>::success({instance().username, 1}));
    }
    void fetchPost(qint64 id, Callback<Post> cb) override
    {
        QTimer::singleShot(fetchDelay, this, [this, id, cb] {
            if (fetchFails || (failConfirmation && writes > 0))
                cb(Result<Post>::failure({ErrorKind::Network, "offline"}));
            else if (posts.contains(id))
                cb(Result<Post>::success(posts.value(id)));
            else
                cb(Result<Post>::failure({ErrorKind::Missing, "missing"}));
        });
    }
    void searchPosts(const SearchRequest &request, Callback<SearchPage> cb) override
    {
        searches.append(request);
        QTimer::singleShot(0, this, [this, cb] {
            if (pages.isEmpty())
                cb(Result<SearchPage>::success({}));
            else
                cb(Result<SearchPage>::success(pages.takeFirst()));
        });
    }
    void searchTags(const QString &, Callback<QList<TagSuggestion>> cb) override
    {
        cb(Result<QList<TagSuggestion>>::success({}));
    }
    bool remoteAutocomplete() const override { return false; }
    void updateTags(qint64 id, const QSet<QString> &tags, const QSet<QString> &latest, Callback<bool> cb) override
    {
        ++writes;
        lastBaseline = latest;
        QTimer::singleShot(0, this, [this, id, tags, cb] {
            if (writeError.kind == ErrorKind::None || applyBeforeError) {
                posts[id].tags = sortedTags(tags);
                if (normalizeResult)
                    posts[id].tags.append("server_implied_tag");
            }
            if (writeError.kind == ErrorKind::None)
                cb(Result<bool>::success(true));
            else
                cb(Result<bool>::failure(writeError));
        });
    }
};

class SessionTests : public QObject {
    Q_OBJECT
    Instance instance{"a", "test", ProviderKind::Danbooru, QUrl("https://a.test"), "alice", false};
    Session spec(Source source = Source::Single)
    {
        Session s;
        s.name = "test";
        s.instanceId = "a";
        s.account = "alice";
        s.source = source;
        s.firstId = 1;
        s.lastId = 3;
        return s;
    }
    Post post(qint64 id, QStringList tags = {"solo", "tagme"})
    {
        Post p;
        p.id = id;
        p.tags = tags;
        return p;
    }
  private slots:
    void saveAndNext()
    {
        Database db(":memory:");
        db.saveInstance(instance);
        MockProvider provider(instance);
        provider.posts = {{1, post(1)}, {2, post(2)}, {3, post(3)}};
        SessionEngine engine(&db);
        QSignalSpy states(&engine, &SessionEngine::stateChanged);
        auto s = spec(Source::Search);
        provider.pages = {{{post(1), post(2), post(3)}, "b1", false}};
        engine.create(s, &provider);
        QTRY_VERIFY(engine.ready());
        auto changes = engine.draft().changes;
        changes.add("smile");
        engine.edit(changes, true);
        QVERIFY(db.draft(engine.session().id, 1)->changes.added.contains("smile"));
        provider.posts[1].tags.append("blue_eyes");
        engine.save(true);
        engine.save(true);
        QTRY_COMPARE(engine.post().id, 2);
        QCOMPARE(provider.writes, 1);
        QVERIFY(!provider.posts[1].tags.contains("tagme"));
        QVERIFY(provider.posts[1].tags.contains("blue_eyes"));
        QVERIFY(provider.lastBaseline.contains("blue_eyes"));
        QCOMPARE(db.queueEntry(engine.session().id, 0)->status, QString("completed"));
        QVERIFY(!db.draft(engine.session().id, 1));
        QList<SubmissionState> seen;
        for (const auto &args : states)
            seen.append(args.first().value<SubmissionState>());
        QVERIFY(seen.contains(SubmissionState::Pending));
        QVERIFY(seen.contains(SubmissionState::Submitting));
        QVERIFY(seen.contains(SubmissionState::Confirming));
        QVERIFY(seen.contains(SubmissionState::Completed));
    }
    void failureKeepsDraft()
    {
        Database db(":memory:");
        db.saveInstance(instance);
        MockProvider provider(instance);
        provider.posts.insert(1, post(1));
        provider.writeError = {ErrorKind::Permission, "not allowed"};
        SessionEngine engine(&db);
        engine.create(spec(), &provider);
        QTRY_VERIFY(engine.ready());
        auto changes = engine.draft().changes;
        changes.add("smile");
        engine.edit(changes, false);
        engine.save(true);
        QTRY_COMPARE(engine.state(), SubmissionState::Failed);
        QCOMPARE(engine.session().current, 0);
        QVERIFY(db.draft(engine.session().id, 1)->changes.added.contains("smile"));
        QVERIFY(provider.posts[1].tags.contains("tagme"));
        QCOMPARE(db.history("a").first().outcome, QString("failed"));
    }
    void uncertainWriteIsNotRepeated()
    {
        Database db(":memory:");
        db.saveInstance(instance);
        MockProvider provider(instance);
        provider.posts.insert(1, post(1));
        provider.writeError = {ErrorKind::Network, "lost response", 0, 0, true};
        provider.applyBeforeError = true;
        SessionEngine engine(&db);
        engine.create(spec(), &provider);
        QTRY_VERIFY(engine.ready());
        auto changes = engine.draft().changes;
        changes.add("smile");
        engine.edit(changes, false);
        engine.save(false);
        QTRY_COMPARE(engine.state(), SubmissionState::Completed);
        QCOMPARE(provider.writes, 1);
        QVERIFY(provider.posts[1].tags.contains("tagme"));
    }
    void failedConfirmationIsRecoverable()
    {
        Database db(":memory:");
        db.saveInstance(instance);
        MockProvider provider(instance);
        provider.posts.insert(1, post(1));
        provider.failConfirmation = true;
        SessionEngine engine(&db);
        engine.create(spec(), &provider);
        QTRY_VERIFY(engine.ready());
        auto changes = engine.draft().changes;
        changes.add("smile");
        engine.edit(changes, true);
        engine.save(true);
        QTRY_COMPARE(engine.state(), SubmissionState::Uncertain);
        QCOMPARE(engine.session().current, 0);
        QVERIFY(db.draft(engine.session().id, 1)->hasIntended);
        QCOMPARE(provider.writes, 1);
        provider.failConfirmation = false;
        engine.save(true);
        QTRY_COMPARE(engine.session().status, QString("finished"));
        QCOMPARE(provider.writes, 1);
    }
    void uncertainWithoutApplicationNeedsReview()
    {
        Database db(":memory:");
        db.saveInstance(instance);
        MockProvider provider(instance);
        provider.posts.insert(1, post(1));
        provider.writeError = {ErrorKind::Network, "no response", 0, 0, true};
        SessionEngine engine(&db);
        QSignalSpy reviews(&engine, &SessionEngine::verificationMismatch);
        engine.create(spec(), &provider);
        QTRY_VERIFY(engine.ready());
        auto changes = engine.draft().changes;
        changes.add("smile");
        engine.edit(changes, true);
        engine.save(true);
        QTRY_COMPARE(reviews.count(), 1);
        QCOMPARE(provider.writes, 1);
        QCOMPARE(engine.session().current, 0);
        engine.resolveVerification(VerificationDecision::KeepUncertain);
        engine.save(true);
        QTRY_COMPARE(reviews.count(), 2);
        QCOMPARE(provider.writes, 1);
        engine.resolveVerification(VerificationDecision::AllowRetry);
        provider.writeError = {};
        engine.edit(changes, false);
        engine.save(false);
        QTRY_COMPARE(engine.state(), SubmissionState::Completed);
        QCOMPARE(provider.writes, 2);
        QVERIFY(provider.posts[1].tags.contains("tagme"));
    }
    void conflictAndVerificationReview()
    {
        Database db(":memory:");
        db.saveInstance(instance);
        MockProvider provider(instance);
        provider.posts.insert(1, post(1));
        SessionEngine engine(&db);
        QSignalSpy conflicts(&engine, &SessionEngine::conflict);
        QSignalSpy verification(&engine, &SessionEngine::verificationMismatch);
        engine.create(spec(), &provider);
        QTRY_VERIFY(engine.ready());
        auto changes = engine.draft().changes;
        changes.add("smile");
        engine.edit(changes, false);
        provider.posts[1].tags.removeAll("solo");
        engine.save(false);
        QTRY_COMPARE(conflicts.count(), 1);
        QCOMPARE(provider.writes, 0);
        engine.resolveConflict(false);
        QCOMPARE(engine.state(), SubmissionState::Editing);
        engine.save(false);
        QTRY_COMPARE(conflicts.count(), 2);
        provider.normalizeResult = true;
        engine.resolveConflict(true);
        QTRY_COMPARE(verification.count(), 1);
        engine.resolveVerification(VerificationDecision::KeepUncertain);
        QCOMPARE(engine.state(), SubmissionState::Uncertain);
        engine.save(false);
        QTRY_COMPARE(verification.count(), 2);
        QCOMPARE(provider.writes, 1);
        engine.resolveVerification(VerificationDecision::AcceptServer);
        QCOMPARE(engine.state(), SubmissionState::Completed);
        QVERIFY(engine.post().tags.contains("server_implied_tag"));
    }
    void crashRecovery()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("sessions.sqlite");
        QString id;
        MockProvider provider(instance);
        provider.posts.insert(1, post(1, {"solo", "smile"}));
        {
            Database db(path);
            db.saveInstance(instance);
            auto s = db.createSession(spec());
            id = s.id;
            db.appendQueue(id, {1});
            s.status = "ready";
            db.updateSession(s);
            Draft d;
            d.changes = {{"solo", "tagme"}, {"smile"}, {"tagme"}};
            d.state = SubmissionState::Submitting;
            d.hasIntended = true;
            d.intended = {"solo", "smile"};
            db.saveDraft(id, 1, d);
        }
        Database db(path);
        SessionEngine engine(&db);
        engine.activate(id, &provider);
        QTRY_VERIFY(engine.ready());
        QCOMPARE(engine.state(), SubmissionState::Uncertain);
        engine.save(false);
        QTRY_COMPARE(engine.state(), SubmissionState::Completed);
        QCOMPARE(provider.writes, 0);
    }
    void deduplicationAndSkipping()
    {
        Database db(":memory:");
        db.saveInstance(instance);
        MockProvider provider(instance);
        provider.posts = {{1, post(1)}, {3, post(3)}};
        provider.pages = {{{post(3), post(3)}, "b3", true}, {{post(3), post(2), post(1)}, "b1", false}};
        SessionEngine engine(&db);
        engine.create(spec(Source::Search), &provider);
        QTRY_VERIFY(engine.ready());
        QCOMPARE(db.queueSize(engine.session().id), 3);
        QCOMPARE(provider.searches.size(), 2);
        QCOMPARE(provider.searches.last().cursor, QString("b3"));
        QCOMPARE(engine.post().id, 3);
        auto changes = engine.draft().changes;
        changes.add("unfinished");
        engine.edit(changes, true);
        engine.skip();
        QTRY_COMPARE(engine.post().id, 1);
        QCOMPARE(db.queueEntry(engine.session().id, 1)->status, QString("skipped"));
        QVERIFY(db.draft(engine.session().id, 3)->changes.added.contains("unfinished"));
        QCOMPARE(provider.writes, 0);
        QVERIFY(provider.posts[3].tags.contains("tagme"));
    }
    void shimmieRangeAndInstanceGuard()
    {
        auto shimmie = instance;
        shimmie.kind = ProviderKind::Shimmie;
        Database db(":memory:");
        db.saveInstance(shimmie);
        MockProvider provider(shimmie);
        provider.posts = {{1, post(1)}, {2, post(2)}, {3, post(3)}};
        SessionEngine engine(&db);
        auto s = spec(Source::Range);
        s.firstId = 3;
        s.lastId = 1;
        engine.create(s, &provider);
        QTRY_VERIFY(engine.ready());
        QCOMPARE(engine.post().id, 3);
        QVERIFY(provider.searches.isEmpty());
        auto other = instance;
        other.id = "other";
        MockProvider wrong(other);
        QSignalSpy errors(&engine, &SessionEngine::error);
        engine.activate(engine.session().id, &wrong);
        QCOMPARE(errors.count(), 1);
        QCOMPARE(engine.post().id, 3);
    }
    void danbooruRangeUsesBoundedSearch()
    {
        Database db(":memory:");
        db.saveInstance(instance);
        MockProvider provider(instance);
        provider.posts = {{1, post(1)}, {3, post(3)}, {5, post(5)}};
        provider.pages = {{{post(5), post(3), post(1)}, "b1", false}};
        SessionEngine engine(&db);
        auto s = spec(Source::Range);
        s.firstId = 1;
        s.lastId = 5;
        engine.create(s, &provider);
        QTRY_VERIFY(engine.ready());
        QCOMPARE(provider.searches.size(), 1);
        QCOMPARE(provider.searches.first().query, QString("id:1..5"));
        QCOMPARE(engine.post().id, 1);
        QCOMPARE(db.queueSize(engine.session().id), 3);
        QCOMPARE(db.queueEntry(engine.session().id, 2)->postId, 5);
    }
    void shimmieMaterializesBeforeEditing()
    {
        auto shm = instance;
        shm.kind = ProviderKind::Shimmie;
        Database db(":memory:");
        db.saveInstance(shm);
        MockProvider provider(shm);
        provider.posts = {{1, post(1)}, {2, post(2)}, {3, post(3)}};
        provider.pages = {{{post(3), post(2)}, {}, true}, {{post(2), post(1)}, {}, false}};
        SessionEngine engine(&db);
        engine.create(spec(Source::Search), &provider);
        QVERIFY(!engine.ready());
        QTRY_VERIFY(engine.ready());
        QCOMPARE(provider.searches.size(), 2);
        QCOMPARE(provider.searches.last().offset, 2);
        QCOMPARE(db.queueSize(engine.session().id), 3);
        engine.edit(engine.draft().changes, true);
        engine.save(true);
        QTRY_COMPARE(engine.post().id, 2);
        QCOMPARE(provider.searches.size(), 2);
        QVERIFY(!provider.posts[3].tags.contains("tagme"));
    }
    void oldResponsesAndDestroyedControllers()
    {
        Database db(":memory:");
        db.saveInstance(instance);
        MockProvider provider(instance);
        provider.posts.insert(1, post(1));
        provider.fetchDelay = 30;
        {
            SessionEngine engine(&db);
            engine.create(spec(), &provider);
            engine.jump(1);
            QTest::qWait(50);
            QVERIFY(!engine.ready());
            QCOMPARE(engine.post().id, 0);
        }
        {
            auto engine = std::make_unique<SessionEngine>(&db);
            engine->create(spec(), &provider);
            engine.reset();
        }
        QTest::qWait(50);
    }
    void resumeSkippedDraftAfterEndOfQueue()
    {
        Database db(":memory:");
        db.saveInstance(instance);
        MockProvider provider(instance);
        provider.posts.insert(1, post(1));
        SessionEngine engine(&db);
        engine.create(spec(), &provider);
        QTRY_VERIFY(engine.ready());
        const auto id = engine.session().id;
        auto changes = engine.draft().changes;
        changes.add("unfinished_tag");
        engine.edit(changes, false);
        engine.skip();
        QCOMPARE(engine.session().status, QString("finished"));
        QVERIFY(db.hasPendingDrafts(id));
        engine.deactivate();
        engine.activate(id, &provider);
        QTRY_VERIFY(engine.ready());
        QCOMPARE(engine.post().id, 1);
        QCOMPARE(engine.session().current, 0);
        QVERIFY(engine.draft().changes.added.contains("unfinished_tag"));
        QCOMPARE(provider.writes, 0);
    }
};

QTEST_GUILESS_MAIN(SessionTests)
#include "test_sessions.moc"
