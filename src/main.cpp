#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QLockFile>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QSurfaceFormat>

int main(int argc, char **argv)
{
    QSurfaceFormat format;
    format.setDepthBufferSize(0);
    format.setStencilBufferSize(0);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("BooruTagger");
    QCoreApplication::setApplicationName("BooruTagger");
    QCoreApplication::setApplicationVersion(BOORUTAGGER_VERSION);
    QCommandLineParser parser;
    parser.setApplicationDescription("Native image and video tagging for Danbooru and Shimmie2.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);
    const auto data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(data);
    QLockFile lock(data + "/boorutagger.lock");
    if (!lock.tryLock(0)) {
        QMessageBox::warning(nullptr, "BooruTagger", "Another BooruTagger is already using this data directory.");
        return 1;
    }
    bt::applyAppearance(QSettings().value("appearance/dark", true).toBool());
    QApplication::setWindowIcon(
        QIcon::fromTheme("boorutagger", QApplication::style()->standardIcon(QStyle::SP_FileDialogDetailedView)));
    try {
        bt::MainWindow window;
        window.show();
        return app.exec();
    } catch (const std::exception &error) {
        QMessageBox::critical(nullptr, "BooruTagger", QString::fromUtf8(error.what()));
        return 1;
    }
}
