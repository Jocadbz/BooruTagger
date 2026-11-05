#include "ui/MainWindow.h"
#include "sessions/SessionEngine.h"
#include "HttpFixture.h"
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QImage>
#include <QJsonArray>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

using namespace bt;

namespace {
QByteArray json(const QJsonObject &object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }
QJsonObject scalar(const QString &name) { return {{"kind", "SCALAR"}, {"name", name}}; }
QJsonObject argument(const QString &name, const QJsonObject &type) { return {{"name", name}, {"type", type}}; }
QJsonObject field(const QString &name, const QJsonArray &args) { return {{"name", name}, {"args", args}}; }
QJsonObject schema() {
    const QJsonObject stringList{{"kind", "LIST"}, {"ofType", scalar("String")}};
    const QJsonObject metadataType{{"kind", "INPUT_OBJECT"}, {"name", "MetadataInput"}};
    const QJsonObject metadataList{{"kind", "LIST"}, {"ofType", metadataType}};
    const QJsonArray queries{field("posts", {argument("limit", scalar("Int")), argument("offset", scalar("Int")), argument("terms", stringList)})};
    const QJsonArray mutations{
        field("login", {argument("username", scalar("String")), argument("password", scalar("String"))}),
        field("update_post_metadata", {argument("post_id", scalar("Int")), argument("metadata", metadataList)})};
    const QJsonObject roots{{"queryType", QJsonObject{{"fields", queries}}}, {"mutationType", QJsonObject{{"fields", mutations}}}};
    return {{"data", QJsonObject{{"__schema", roots}}}};
}
}

class WorkflowTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("BooruTaggerTests");
        QCoreApplication::setApplicationName("workflow");
    }
    void editSaveAndRecover_data() {
        QTest::addColumn<bool>("shimmie");
        QTest::newRow("danbooru") << false;
        QTest::newRow("shimmie") << true;
    }
    void editSaveAndRecover() {
        QFETCH(bool, shimmie);
        QTemporaryDir dir;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        QSettings().clear();
        HttpFixture server;
        QMap<qint64, QStringList> tags{{1, {"solo", "tagme"}}, {2, {"solo", "tagme"}}};
        int writes = 0;
        bool deny = false;
        QImage image(64, 48, QImage::Format_RGB32); image.fill(Qt::blue);
        QByteArray png;
        QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG");
        const auto danPost = [&](qint64 id) -> QJsonObject {
            return {{"id", id}, {"tag_string", tags.value(id).join(' ')}, {"file_ext", "png"}, {"file_url", server.base("/image.png").toString()}};
        };
        const auto shmPost = [&](qint64 id) -> QJsonObject {
            return {{"id", "opaque"}, {"post_id", id}, {"tags", QJsonArray::fromStringList(tags.value(id))}, {"ext", "png"}, {"image_link", "/image.png"}};
        };
        server.handler = [&](const FixtureRequest &request) -> FixtureResponse {
            if (request.url.path() == "/image.png")
                return {200, png, {}, false};
            if (!shimmie) {
                if (request.url.path() == "/profile.json")
                    return {200, R"({"id":42,"name":"tester"})", {}, false};
                if (request.url.path().startsWith("/posts/")) {
                    const auto id = request.url.path().section('/', 2).section('.', 0, 0).toLongLong();
                    if (request.method == "PUT") {
                        ++writes;
                        if (deny) return {403, "{}", {}, false};
                        tags[id] = request.json().value("post").toObject().value("tag_string").toString().split(' ', Qt::SkipEmptyParts);
                    }
                    return {200, json(danPost(id)), {}, false};
                }
                return {200, "[]", {}, false};
            }
            const auto body = request.json();
            const auto query = body.value("query").toString();
            const auto vars = body.value("variables").toObject();
            if (query.contains("__schema")) return {200, json(schema()), {}, false};
            if (query.contains("login(")) return {200, R"({"data":{"login":{"user":{"name":"tester"},"session":"test-session"}}})", {}, false};
            if (query.contains("update_post_metadata(")) {
                ++writes;
                if (deny) return {200, R"({"errors":[{"message":"denied","extensions":{"code":"FORBIDDEN"}}]})", {}, false};
                const auto id = jsonId(vars.value("post_id"));
                tags[id] = vars.value("metadata").toArray().first().toObject().value("value").toString().split(' ', Qt::SkipEmptyParts);
                return {200, json({{"data", QJsonObject{{"update_post_metadata", QJsonObject{{"id", "opaque"}, {"tags", tags[id].join(' ')}}}}}}), {}, false};
            }
            const auto term = vars.value("terms").toArray().first().toString();
            const auto id = term.section('=', 1).toLongLong();
            return {200, json({{"data", QJsonObject{{"posts", QJsonArray{shmPost(id)}}}}}), {}, false};
        };
        const auto path = dir.filePath("sessions.sqlite");
        const auto instanceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QString sessionId;
        {
            Database db(path);
            db.saveInstance({instanceId, "local fixture", shimmie ? ProviderKind::Shimmie : ProviderKind::Danbooru, server.base(), "tester", true});
            Session s;
            s.name = "fixture session"; s.instanceId = instanceId; s.account = "tester";
            s = db.createSession(s); sessionId = s.id;
            db.appendQueue(s.id, {1, 2}); s.status = "ready"; db.updateSession(s);
        }
        {
            MainWindow window(nullptr, path);
            window.show();
            auto *sessions = window.findChild<QComboBox *>("sessionSelector");
            auto *engine = window.findChild<SessionEngine *>();
            QVERIFY(sessions); QVERIFY(engine);
            QTimer::singleShot(0, &window, [] {
                auto *dialog = QApplication::activeModalWidget();
                if (!dialog) return;
                auto *secret = dialog->findChild<QLineEdit *>("loginSecret");
                auto *button = dialog->findChild<QPushButton *>("signInButton");
                if (secret && button) { secret->setText("fixture-password"); button->click(); }
            });
            const auto index = sessions->findData(sessionId);
            QVERIFY(index > 0);
            sessions->setCurrentIndex(index);
            QMetaObject::invokeMethod(sessions, "activated", Qt::DirectConnection, Q_ARG(int, index));
            QTRY_VERIFY_WITH_TIMEOUT(engine->ready(), 10000);
            auto *input = window.findChild<QLineEdit *>("tagInput");
            QVERIFY(input->isEnabled());
            if (QApplication::platformName() == "offscreen")
                window.activateWindow();
            QTRY_VERIFY(input->hasFocus());
            QTest::keyClicks(input, "smile");
            auto *sufficient = window.findChild<QCheckBox *>("sufficientlyTagged");
            sufficient->setChecked(true);
            auto *saveNext = window.findChild<QAction *>("saveNextAction");
            QVERIFY(saveNext->isEnabled());
            saveNext->trigger();
            QTRY_COMPARE_WITH_TIMEOUT(engine->post().id, 2, 10000);
            QTRY_VERIFY(input->isEnabled());
            QCOMPARE(writes, 1);
            QCOMPARE(tagSet(tags.value(1)), tagSet({"solo", "smile"}));
            QVERIFY(!sufficient->isChecked());
            QTest::keyClicks(input, "long_hair");
            QTest::keyClick(input, Qt::Key_Return);
            deny = true;
            saveNext->trigger();
            QTRY_COMPARE_WITH_TIMEOUT(engine->state(), SubmissionState::Failed, 10000);
            QCOMPARE(engine->session().current, 1);
            QVERIFY(input->isEnabled());
            QVERIFY(tags[2].contains("tagme"));
            QTest::keyClicks(input, "custom_tag");
            QVERIFY(window.close());
        }
        Database db(path);
        QCOMPARE(db.queueEntry(sessionId, 0)->status, QString("completed"));
        QCOMPARE(db.queueEntry(sessionId, 1)->status, QString("failed"));
        const auto draft = db.draft(sessionId, 2).value();
        QVERIFY(draft.changes.added.contains("long_hair"));
        QVERIFY(draft.changes.added.contains("custom_tag"));
        QVERIFY(draft.changes.original.contains("tagme"));
        QVERIFY(!draft.sufficient);
        if (shimmie) {
            bool sawCookie = false;
            for (const auto &request : server.requests)
                sawCookie |= request.headers.value("cookie").contains("shm_session=test-session");
            QVERIFY(sawCookie);
        }
    }
};

QTEST_MAIN(WorkflowTests)
#include "test_workflow.moc"
