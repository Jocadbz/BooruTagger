#include "media/MpvWidget.h"
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace bt;

class MpvTests : public QObject {
    Q_OBJECT
private slots:
    void localPlayback() {
        if (qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
            QSKIP("An X11 or Wayland display is needed for the OpenGL smoke test.");
        const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        if (ffmpeg.isEmpty())
            QSKIP("ffmpeg is needed to generate the local video fixture.");
        QTemporaryDir directory;
        const auto file = directory.filePath("test.mp4");
        QProcess generator;
        generator.start(ffmpeg, {"-v", "error", "-f", "lavfi", "-i", "color=c=red:s=320x240:r=24:d=3",
                                "-c:v", "mpeg4", "-y", file});
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
};

QTEST_MAIN(MpvTests)
#include "test_mpv.moc"
