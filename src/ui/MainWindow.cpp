#include "MainWindow.h"
#include "TagEditor.h"
#include "media/MediaViewer.h"

#include <QCloseEvent>
#include <QLabel>
#include <QMenuBar>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>

namespace bt {

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("BooruTagger"));
    resize(1180, 780);
    auto *splitter = new QSplitter(this);
    auto *media = new MediaViewer(splitter);
    m_editor = new TagEditor(splitter);
    m_editor->setEnabled(false);
    splitter->addWidget(media);
    splitter->addWidget(m_editor);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);
    auto *file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);
    auto *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(tr("&Undo tag edit"), QKeySequence::Undo, m_editor, &TagEditor::undo);
    edit->addAction(tr("&Redo tag edit"), QKeySequence("Ctrl+Shift+Z"), m_editor, &TagEditor::redo);
    edit->addAction(tr("Focus tag &input"), QKeySequence("Ctrl+K"), m_editor, &TagEditor::focusInput);
    auto *view = menuBar()->addMenu(tr("&View"));
    view->addAction(tr("&Fit image"), media, &MediaViewer::fit);
    view->addAction(tr("&Actual size"), media, &MediaViewer::actualSize);
    menuBar()->addMenu(tr("&Session"));
    menuBar()->addMenu(tr("&Settings"));
    menuBar()->addMenu(tr("&Help"));
    QSettings settings;
    restoreGeometry(settings.value("window/geometry").toByteArray());
    statusBar()->showMessage(tr("Ready"));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings().setValue("window/geometry", saveGeometry());
    event->accept();
}

}
