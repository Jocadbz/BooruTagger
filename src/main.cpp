#include "ui/MainWindow.h"

#include <QApplication>
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
    bt::MainWindow window;
    window.show();
    return app.exec();
}
