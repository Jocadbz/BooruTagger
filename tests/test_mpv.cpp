#include "HttpFixture.h"
#include "media/MediaViewer.h"
#include "media/MpvWidget.h"
#include "network/HttpClient.h"
#include <QApplication>
#include <QFile>
#include <QProcess>
#include <QSlider>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace bt;

class MpvTests : public QObject {
    Q_OBJECT
  private slots:
    void localPlayback()
    {
        if (QApplication::platformName() == "offscreen" || QApplication::platformName() == "minimal")
            QSKIP("An X11 or Wayland display is needed for the OpenGL smoke test.");
        const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        if (ffmpeg.isEmpty())
            QSKIP("ffmpeg is needed to generate the local video fixture.");
        QTemporaryDir directory;
        const auto file = directory.filePath("test.mp4");
        QProcess generator;
        generator.start(ffmpeg, {"-v", "error", "-f", "lavfi", "-i", "color=c=red:s=320x240:r=24:d=3", "-c:v", "mpeg4",
                                 "-y", file});
        QVERIFY(generator.waitForFinished(15000));
        QCOMPARE(generator.exitCode(), 0);
        MpvWidget widget;
        QSignalSpy errors(&widget, &MpvWidget::playbackError);
        QSignalSpy duration(&widget, &MpvWidget::durationChanged);
        QSignalSpy position(&widget, &MpvWidget::positionChanged);
        QSignalSpy frames(&widget, &MpvWidget::frameRendered);
        QVERIFY(widget.available());
        widget.resize(480, 320);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        widget.load(QUrl::fromLocalFile(file));
        QTRY_VERIFY_WITH_TIMEOUT(duration.count() >= 2, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(position.count() >= 3, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 2, 10000);
        QCOMPARE(errors.count(), 0);
        const auto frame = widget.grabFramebuffer();
        QVERIFY(!frame.isNull());
        const auto center = frame.pixelColor(frame.width() / 2, frame.height() / 2);
        QVERIFY(center.red() > 150);
        QVERIFY(center.green() < 100);
        widget.togglePause();
        widget.frameStep();
        widget.seek(1, true);
        widget.setSpeed(0.5);
        widget.setVolume(10);
        widget.setMuted(true);
        widget.setLoop(true);
        widget.resize(640, 360);
        QTest::qWait(100);
        widget.stop();
        widget.load(QUrl::fromLocalFile(file));
        QTest::qWait(150);
        QCOMPARE(errors.count(), 0);
    }
    void embeddedStreaming()
    {
        if (QApplication::platformName() == "offscreen" || QApplication::platformName() == "minimal")
            QSKIP("An X11 or Wayland display is needed for the streaming renderer test.");
        const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        if (ffmpeg.isEmpty())
            QSKIP("ffmpeg is needed to generate the local stream fixture.");
        QStandardPaths::setTestModeEnabled(true);
        QTemporaryDir directory;
        const auto path = directory.filePath("stream.mp4");
        QProcess generator;
        generator.start(ffmpeg, {"-v", "error", "-f", "lavfi", "-i", "color=c=red:s=320x240:r=24:d=3", "-c:v", "mpeg4",
                                 "-y", path});
        QVERIFY(generator.waitForFinished(15000));
        QCOMPARE(generator.exitCode(), 0);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll();
        HttpFixture server;
        server.handler = [bytes](const FixtureRequest &) -> FixtureResponse { return {200, bytes, {}, false}; };
        HttpClient client({"stream", "fixture", ProviderKind::Danbooru, server.base(), "tester", true});
        client.setBasicAuth("tester", "private-api-key");
        MediaViewer viewer;
        auto *player = viewer.findChild<MpvWidget *>();
        QVERIFY(player);
        QSignalSpy errors(&viewer, &MediaViewer::mediaError);
        QSignalSpy durations(player, &MpvWidget::durationChanged);
        QSignalSpy positions(player, &MpvWidget::positionChanged);
        viewer.resize(640, 480);
        viewer.show();
        QVERIFY(QTest::qWaitForWindowExposed(&viewer));
        Post post;
        post.id = 1;
        post.mediaUrl = server.base("/stream.mp4");
        post.extension = "mp4";
        viewer.load(post, &client);
        QTRY_VERIFY_WITH_TIMEOUT(durations.count() >= 2, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(positions.count() >= 3, 10000);
        QVERIFY(!server.requests.isEmpty());
        for (const auto &request : server.requests)
            QVERIFY(!request.headers.contains("authorization"));
        auto *timeline = viewer.findChild<QSlider *>("videoTimeline");
        QVERIFY(timeline);
        timeline->setFocus();
        QTest::keyClick(timeline, Qt::Key_End);
        QTest::keyClick(timeline, Qt::Key_Home);
        QTest::qWait(150);
        QCOMPARE(errors.count(), 0);
        viewer.clear();
    }
};

int main(int argc, char **argv)
{
    if (qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") &&
        qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    MpvTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_mpv.moc"
