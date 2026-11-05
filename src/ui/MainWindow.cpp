#include "MainWindow.h"
#include "Dialogs.h"
#include "TagEditor.h"
#include "database/Database.h"
#include "media/MediaViewer.h"
#include "providers/BooruProvider.h"
#include "sessions/SessionEngine.h"
#include "settings/Credentials.h"
#include "tagging/Completion.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace bt {

void applyAppearance(bool dark)
{
    if (!dark) {
        QApplication::setPalette(QApplication::style()->standardPalette());
        return;
    }
    auto palette = QApplication::palette();
    palette.setColor(QPalette::Window, QColor(40, 42, 47));
    palette.setColor(QPalette::WindowText, QColor(228, 229, 232));
    palette.setColor(QPalette::Base, QColor(29, 31, 35));
    palette.setColor(QPalette::AlternateBase, QColor(35, 37, 42));
    palette.setColor(QPalette::Text, QColor(228, 229, 232));
    palette.setColor(QPalette::Button, QColor(48, 51, 57));
    palette.setColor(QPalette::ButtonText, QColor(228, 229, 232));
    palette.setColor(QPalette::Highlight, QColor(69, 102, 160));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Link, QColor(134, 174, 237));
    palette.setColor(QPalette::ToolTipBase, QColor(48, 51, 57));
    palette.setColor(QPalette::ToolTipText, QColor(228, 229, 232));
    for (const auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, QColor(125, 128, 135));
    QApplication::setPalette(palette);
}

MainWindow::MainWindow(QWidget *parent, const QString &databasePath) : QMainWindow(parent)
{
    m_db = std::make_unique<Database>(databasePath.isEmpty()
                                          ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                                                "/boorutagger.sqlite"
                                          : databasePath);
    m_engine = new SessionEngine(m_db.get(), this);
    m_credentials = new Credentials(this);
    m_completion = new Completion(m_db.get(), this);
    buildUi();
    connectEngine();
    QSettings settings;
    restoreGeometry(settings.value("window/geometry").toByteArray());
    m_splitter->restoreState(settings.value("window/splitter").toByteArray());
    refreshInstances(settings.value("last_instance").toString());
    updateActions();
    QTimer::singleShot(0, this, [this] {
        guarded([&] {
            const auto id = QSettings().value("last_session").toString();
            const auto session = m_db->session(id);
            if (session && (session->status != "finished" || m_db->hasPendingDrafts(session->id)) &&
                QMessageBox::question(this, tr("Resume session"),
                                      tr("Resume “%1”? Your pending edits are stored locally.").arg(session->name),
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes) {
                selectInstance(session->instanceId);
                openSession(id);
            }
        });
    });
}

MainWindow::~MainWindow()
{
    m_completion->setProvider(nullptr);
    m_media->clear();
    delete m_engine;
    m_engine = nullptr;
    delete m_provider;
    m_provider = nullptr;
}

void MainWindow::buildUi()
{
    setWindowTitle("BooruTagger");
    resize(1240, 820);
    auto *file = menuBar()->addMenu(tr("&File"));
    m_addInstance =
        file->addAction(tr("Add &instance / account…"), QKeySequence("Ctrl+Shift+I"), this, &MainWindow::addInstance);
    m_signIn = file->addAction(tr("&Sign in…"), QKeySequence("Ctrl+L"), this, [this] { signIn(); });
    m_openBrowser = file->addAction(tr("Open post in &browser"), QKeySequence("Ctrl+B"), this,
                                    [this] { openPost(m_engine->post().id); });
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);
    auto *editMenu = menuBar()->addMenu(tr("&Edit"));
    auto *view = menuBar()->addMenu(tr("&View"));
    auto *sessions = menuBar()->addMenu(tr("&Session"));
    m_newSession = sessions->addAction(tr("&New session…"), QKeySequence::New, this, &MainWindow::newSession);
    m_previous = sessions->addAction(tr("&Previous"), QKeySequence("Alt+Left"), this, [this] {
        flushEdit();
        m_engine->previous();
    });
    m_skip = sessions->addAction(tr("S&kip / next"), QKeySequence("Alt+Right"), this, [this] {
        flushEdit();
        m_engine->skip();
    });
    m_save = sessions->addAction(tr("&Save"), QKeySequence::Save, this, [this] {
        flushEdit();
        m_engine->save(false);
    });
    m_saveNext = sessions->addAction(tr("Save && &Next"), this, [this] {
        flushEdit();
        m_engine->save(true);
    });
    m_saveNext->setShortcuts({QKeySequence("Ctrl+Return"), QKeySequence("Ctrl+Enter")});
    m_saveNext->setObjectName("saveNextAction");
    m_save->setObjectName("saveAction");
    m_retry = sessions->addAction(tr("&Reload / retry discovery"), QKeySequence::Refresh, this, [this] {
        flushEdit();
        m_engine->reload();
    });
    m_jump = sessions->addAction(tr("&Jump to queue position…"), QKeySequence("Ctrl+J"), this, &MainWindow::jump);
    m_useQueue =
        sessions->addAction(tr("Stop discovery and use stored &IDs"), this, [this] { m_engine->useDiscoveredQueue(); });
    sessions->addSeparator();
    sessions->addAction(tr("Local tagging &history…"), this, &MainWindow::history);
    sessions->addAction(tr("Export draft for local &recovery…"), this, [this] { exportDraft(); });
    auto *settings = menuBar()->addMenu(tr("Se&ttings"));
    auto *dark = settings->addAction(tr("&Dark appearance"));
    dark->setCheckable(true);
    dark->setChecked(QSettings().value("appearance/dark", true).toBool());
    connect(dark, &QAction::toggled, this, [](bool checked) {
        QSettings().setValue("appearance/dark", checked);
        applyAppearance(checked);
    });
    auto *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&Keyboard shortcuts"), this, [this] {
        QMessageBox::information(
            this, tr("Keyboard shortcuts"),
            tr("Ctrl+Enter   Save & Next\nCtrl+S   Save without advancing\nAlt+Left   Previous post\nAlt+Right   Skip "
               "(draft kept, nothing submitted)\n"
               "Ctrl+K   Focus tag input\nEnter / Tab   Accept suggestion or add typed tags\nEscape   Close "
               "suggestions\nDelete   Remove selected tags\n"
               "Ctrl+E   Edit full tag list\nCtrl+Z / Ctrl+Shift+Z   Undo / redo tags\nAlt+T   Confirm sufficiently "
               "tagged\nCtrl+J   Jump in queue\n"
               "Ctrl+B   Open post in browser\nF5   Reload / retry discovery\n\nWith media focused:\nF   Fit "
               "image\nSpace   Play / pause\nLeft / Right   Seek 5 seconds\n. / ,   Step forward / backward"));
    });
    help->addAction(tr("&About"), this, [this] {
        QMessageBox::about(this, "BooruTagger",
                           tr("BooruTagger %1\nA native tagging workstation for Danbooru and Shimmie2.\nNo telemetry "
                              "or automatic tagging.")
                               .arg(BOORUTAGGER_VERSION));
    });

    auto *toolbar = addToolBar(tr("Instance and session"));
    toolbar->setObjectName("sessionToolbar");
    toolbar->setMovable(false);
    toolbar->addWidget(new QLabel(tr(" Instance: "), toolbar));
    m_instances = new QComboBox(toolbar);
    m_instances->setMinimumContentsLength(16);
    m_instances->setObjectName("instanceSelector");
    toolbar->addWidget(m_instances);
    toolbar->addAction(m_addInstance);
    toolbar->addAction(m_signIn);
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(tr(" Session: "), toolbar));
    m_sessions = new QComboBox(toolbar);
    m_sessions->setMinimumContentsLength(20);
    m_sessions->setObjectName("sessionSelector");
    toolbar->addWidget(m_sessions);
    toolbar->addAction(m_newSession);
    connect(m_instances, &QComboBox::activated, this,
            [this] { selectInstance(m_instances->currentData().toString()); });
    connect(m_sessions, &QComboBox::activated, this, [this] { openSession(m_sessions->currentData().toString()); });

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(8, 8, 8, 8);
    m_error = new QLabel(central);
    m_error->setObjectName("errorBanner");
    m_error->setTextFormat(Qt::PlainText);
    m_error->setWordWrap(true);
    m_error->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_error->hide();
    layout->addWidget(m_error);
    m_notice = new QLabel(central);
    m_notice->setTextFormat(Qt::PlainText);
    m_notice->setWordWrap(true);
    m_notice->hide();
    layout->addWidget(m_notice);
    m_info = new QLabel(tr("Choose an instance, sign in, and create or resume a session."), central);
    m_info->setTextFormat(Qt::PlainText);
    layout->addWidget(m_info);
    m_splitter = new QSplitter(central);
    m_media = new MediaViewer(m_splitter);
    auto *tagPanel = new QWidget(m_splitter);
    auto *tagLayout = new QVBoxLayout(tagPanel);
    tagLayout->setContentsMargins(0, 0, 0, 0);
    m_editor = new TagEditor(tagPanel);
    tagLayout->addWidget(m_editor, 1);
    m_sufficient = new QCheckBox(tr("Sufficiently &tagged — remove tagme on save"), tagPanel);
    m_sufficient->setObjectName("sufficientlyTagged");
    m_sufficient->setToolTip(
        tr("Explicitly confirms this post is finished. Unchecked keeps tagme. Skip never submits any edit."));
    tagLayout->addWidget(m_sufficient);
    m_splitter->addWidget(m_media);
    m_splitter->addWidget(tagPanel);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({840, 380});
    layout->addWidget(m_splitter, 1);
    auto *footer = new QHBoxLayout;
    const auto actionButton = [footer, central](QAction *action) {
        auto *button = new QToolButton(central);
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        footer->addWidget(button);
    };
    actionButton(m_previous);
    actionButton(m_skip);
    footer->addStretch();
    actionButton(m_save);
    actionButton(m_saveNext);
    m_progress = new QLabel(tr("No session"), central);
    m_progressBar = new QProgressBar(central);
    m_progressBar->setMaximumWidth(120);
    m_progressBar->setTextVisible(false);
    m_progressBar->setRange(0, 1);
    m_progressBar->setValue(0);
    footer->addSpacing(16);
    footer->addWidget(m_progress);
    footer->addWidget(m_progressBar);
    layout->addLayout(footer);
    setCentralWidget(central);
    m_state = new QLabel(tr("Ready"), this);
    statusBar()->addPermanentWidget(m_state);
    statusBar()->showMessage(tr("Ready"));
    m_tagActions.append(editMenu->addAction(tr("&Undo tag edit"), QKeySequence::Undo, m_editor, &TagEditor::undo));
    m_tagActions.append(
        editMenu->addAction(tr("&Redo tag edit"), QKeySequence("Ctrl+Shift+Z"), m_editor, &TagEditor::redo));
    m_tagActions.append(
        editMenu->addAction(tr("Focus tag &input"), QKeySequence("Ctrl+K"), m_editor, &TagEditor::focusInput));
    m_tagActions.append(
        editMenu->addAction(tr("Edit &full tag list…"), QKeySequence("Ctrl+E"), m_editor, &TagEditor::replaceAll));
    m_presets = editMenu->addMenu(tr("Tag &presets"));
    connect(m_presets, &QMenu::aboutToShow, this, &MainWindow::rebuildPresets);
    view->addAction(tr("&Fit image (F in viewer)"), m_media, &MediaViewer::fit);
    view->addAction(tr("&Actual image size"), QKeySequence("Ctrl+1"), m_media, &MediaViewer::actualSize);
}

void MainWindow::connectEngine()
{
    connect(m_engine, &SessionEngine::postReady, this,
            [this](const Post &post, const TagChanges &changes, bool sufficient) {
                m_syncing = true;
                m_completion->setProvider(m_provider);
                m_completion->setExcluded(changes.result());
                m_editor->load(changes, m_provider->instance().kind);
                m_sufficient->setChecked(sufficient);
                m_sufficient->setVisible(m_engine->session().removeTagme && changes.original.contains("tagme"));
                m_editor->setRemoveTagmePreview(sufficient && m_engine->session().removeTagme);
                m_syncing = false;
                QString info = QStringLiteral("#%1 · %2").arg(post.id).arg(post.extension.toUpper());
                if (!post.dimensions.isEmpty())
                    info += QStringLiteral(" · %1 × %2").arg(post.dimensions.width()).arg(post.dimensions.height());
                m_info->setText(info + " · " + m_provider->instance().name);
                m_error->hide();
                m_media->load(post, m_provider->http());
                setWindowTitle(QStringLiteral("#%1 — %2 — BooruTagger").arg(post.id).arg(m_engine->session().name));
                m_editor->focusInput();
                updateActions();
            });
    connect(m_editor, &TagEditor::changed, this, [this] {
        if (m_syncing)
            return;
        m_engine->edit(m_editor->changes(), m_sufficient->isChecked());
        m_completion->setExcluded(m_editor->changes().result());
    });
    connect(m_sufficient, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_syncing)
            return;
        m_editor->setRemoveTagmePreview(checked && m_engine->session().removeTagme);
        m_engine->edit(m_editor->changes(), checked);
    });
    connect(m_editor, &TagEditor::prefixChanged, m_completion, &Completion::request);
    connect(m_completion, &Completion::suggestions, m_editor, &TagEditor::setSuggestions);
    connect(m_completion, &Completion::warning, this,
            [this](const QString &message) { statusBar()->showMessage(message, 7000); });
    connect(m_media, &MediaViewer::mediaError, this,
            [this](const QString &message) { showError({ErrorKind::Other, message}); });
    connect(m_engine, &SessionEngine::previewAvailable, this, [this](const Post &post) {
        if (m_provider)
            m_media->prefetch(post, m_provider->http());
    });
    connect(m_engine, &SessionEngine::sessionChanged, this, [this](const Session &session) {
        if (m_noticeSession != session.id) {
            m_noticeSession = session.id;
            m_notice->clear();
            m_notice->hide();
        }
        if (!session.id.isEmpty())
            QSettings().setValue("last_session", session.id);
        refreshSessions();
        updateActions();
    });
    connect(m_engine, &SessionEngine::loadingChanged, this, [this](bool loading) {
        m_loading = loading;
        if (loading) {
            m_error->hide();
            m_media->clear(m_engine->building() ? tr("Discovering and storing post IDs…") : tr("Loading post…"));
            m_info->setText(m_engine->building()
                                ? tr("Building a stable queue. No edits are submitted during discovery.")
                                : tr("Loading post…"));
        }
        updateActions();
        if (!loading && m_editor->isEnabled())
            m_editor->focusInput();
    });
    connect(m_engine, &SessionEngine::stateChanged, this, [this](SubmissionState state) {
        auto label = stateName(state);
        if (!label.isEmpty())
            label[0] = label[0].toUpper();
        m_state->setText(label);
        updateActions();
    });
    connect(m_engine, &SessionEngine::status, this,
            [this](const QString &message) { statusBar()->showMessage(message); });
    connect(m_engine, &SessionEngine::queueSummary, this, [this](const QString &message) {
        m_notice->setText(message);
        m_notice->show();
    });
    connect(m_engine, &SessionEngine::postSkipped, this, [this](qint64 id, const QString &reason) {
        m_notice->setText(tr("Last skipped: #%1 · %2").arg(id).arg(reason));
        m_notice->show();
    });
    connect(m_engine, &SessionEngine::error, this, &MainWindow::showError);
    connect(m_engine, &SessionEngine::databaseError, this, [this](const QString &message) {
        m_error->setText(message);
        m_error->show();
        updateActions();
        QMessageBox::critical(this, tr("Local storage error"), message);
    });
    connect(m_engine, &SessionEngine::progressChanged, this, [this](int position, int total, int done) {
        m_progress->setText(tr("%1 / %2 · %3 processed").arg(position).arg(total).arg(done));
        m_progressBar->setRange(0, qMax(1, total));
        m_progressBar->setValue(done);
    });
    connect(m_engine, &SessionEngine::finished, this, [this] {
        m_media->clear(tr("Queue finished. Use Previous or Jump to review posts and skipped drafts."));
        m_info->setText(tr("End of “%1”").arg(m_engine->session().name));
        updateActions();
    });
    connect(m_engine, &SessionEngine::conflict, this, [this](const Post &, const TagChanges &, const TagMerge &merge) {
        m_engine->resolveConflict(reviewMerge(merge, this));
    });
    connect(m_engine, &SessionEngine::verificationMismatch, this,
            [this](const QSet<QString> &expected, const Post &actual, bool uncertain) {
                m_engine->resolveVerification(reviewVerification(expected, actual, uncertain, this));
            });
}

void MainWindow::guarded(const std::function<void()> &operation)
{
    try {
        operation();
    } catch (const std::exception &error) {
        showError({ErrorKind::Other, QString::fromUtf8(error.what())});
    }
}

void MainWindow::refreshInstances(const QString &selected)
{
    const QSignalBlocker block(m_instances);
    m_instances->clear();
    for (const auto &instance : m_db->instances())
        m_instances->addItem(instance.name + " · " + instance.username, instance.id);
    auto index = m_instances->findData(selected);
    m_instances->setCurrentIndex(index >= 0 ? index : m_instances->count() > 0 ? 0 : -1);
    selectInstance(m_instances->currentData().toString());
}

void MainWindow::selectInstance(const QString &id)
{
    if (m_engine->busy())
        return;
    guarded([&] {
        if (m_provider && m_provider->instance().id == id) {
            const QSignalBlocker block(m_instances);
            m_instances->setCurrentIndex(m_instances->findData(id));
            return;
        }
        flushEdit();
        m_completion->setProvider(nullptr);
        m_engine->deactivate();
        m_media->clear();
        delete m_provider;
        m_provider = nullptr;
        const auto instance = m_db->instance(id);
        if (instance) {
            m_provider = createProvider(*instance, this);
            connect(m_provider, &BooruProvider::status, this,
                    [this](const QString &message) { statusBar()->showMessage(message); });
            QSettings().setValue("last_instance", id);
        }
        const QSignalBlocker block(m_instances);
        m_instances->setCurrentIndex(m_instances->findData(id));
        m_info->setText(
            instance ? tr("%1 · %2 — sign in, then create or choose a session.").arg(instance->name, instance->username)
                     : tr("Add an instance / account to get started."));
        refreshSessions();
        updateActions();
    });
}

void MainWindow::refreshSessions()
{
    guarded([&] {
        const QSignalBlocker block(m_sessions);
        m_sessions->clear();
        m_sessions->addItem(tr("Choose a saved session…"), QString());
        if (m_provider) {
            for (const auto &session : m_db->sessions(m_provider->instance().id))
                m_sessions->addItem(session.name + (session.status == "finished" ? tr(" (finished)") : ""), session.id);
        }
        const auto index = m_sessions->findData(m_engine->session().id);
        m_sessions->setCurrentIndex(qMax(0, index));
    });
}

void MainWindow::addInstance()
{
    if (m_engine->busy())
        return;
    flushEdit();
    const auto instance = instanceDialog(this);
    if (instance)
        guarded([&] {
            m_db->saveInstance(*instance);
            refreshInstances(instance->id);
        });
}

bool MainWindow::signIn()
{
    if (!m_provider || m_engine->busy())
        return false;
    flushEdit();
    LoginDialog dialog(m_provider, m_credentials, this);
    const bool success = dialog.exec() == QDialog::Accepted;
    if (success) {
        m_error->hide();
        statusBar()->showMessage(
            tr("Signed in to %1 as %2.").arg(m_provider->instance().name, m_provider->instance().username));
    }
    updateActions();
    return success;
}

void MainWindow::newSession()
{
    if (!m_provider || m_engine->busy())
        return;
    flushEdit();
    if (!m_provider->authenticated() && !signIn())
        return;
    const auto session = sessionDialog(m_provider->instance(), this);
    if (session)
        m_engine->create(*session, m_provider);
}

void MainWindow::openSession(const QString &id)
{
    if (id.isEmpty() || !m_provider || m_engine->busy())
        return;
    flushEdit();
    if (!m_provider->authenticated() && !signIn()) {
        refreshSessions();
        return;
    }
    m_completion->setProvider(nullptr);
    m_engine->activate(id, m_provider);
}

void MainWindow::flushEdit()
{
    if (!m_engine->ready() || m_engine->busy() || m_engine->state() == SubmissionState::Uncertain)
        return;
    m_editor->commitInput();
    m_engine->edit(m_editor->changes(), m_sufficient->isChecked());
}

void MainWindow::updateActions()
{
    const bool busy = m_engine->busy() || m_engine->blocked();
    const bool ready = m_engine->ready() && !m_loading;
    const bool active = !m_engine->session().id.isEmpty();
    m_instances->setEnabled(!busy);
    m_sessions->setEnabled(m_provider && !busy);
    m_addInstance->setEnabled(!busy);
    m_signIn->setEnabled(m_provider && !busy && !m_loading);
    m_newSession->setEnabled(m_provider && !busy);
    m_previous->setEnabled(active && !busy && !m_engine->building());
    m_skip->setEnabled(active && !busy && !m_engine->building());
    m_jump->setEnabled(active && !busy && !m_engine->building());
    m_save->setEnabled(ready && !busy);
    m_saveNext->setEnabled(ready && !busy);
    m_openBrowser->setEnabled(m_engine->post().id > 0);
    m_retry->setEnabled(active && !busy);
    m_useQueue->setEnabled(m_engine->building() && !busy);
    m_editor->setEnabled(ready && !busy && m_engine->state() != SubmissionState::Uncertain);
    m_sufficient->setEnabled(m_editor->isEnabled());
    for (auto *action : m_tagActions)
        action->setEnabled(m_editor->isEnabled());
    m_presets->setEnabled(m_editor->isEnabled());
    if (!active)
        m_sufficient->hide();
    m_save->setText(m_engine->state() == SubmissionState::Uncertain ? tr("&Check submission") : tr("&Save"));
}

void MainWindow::showError(const Error &error)
{
    m_error->setText(error.message);
    m_error->show();
    statusBar()->showMessage(error.message);
}

void MainWindow::openPost(qint64 id)
{
    if (!m_provider || id < 1)
        return;
    const auto path = m_provider->instance().kind == ProviderKind::Danbooru ? QStringLiteral("posts/%1").arg(id)
                                                                            : QStringLiteral("post/view/%1").arg(id);
    QDesktopServices::openUrl(endpoint(m_provider->instance().baseUrl, path));
}

void MainWindow::jump()
{
    flushEdit();
    guarded([&] {
        const int total = m_db->queueSize(m_engine->session().id);
        if (total < 1)
            return;
        bool ok = false;
        const auto position = QInputDialog::getInt(this, tr("Jump in queue"),
                                                   tr("Queue position (including skipped and completed posts)"),
                                                   qMin(m_engine->session().current + 1, total), 1, total, 1, &ok);
        if (ok)
            m_engine->jump(position - 1);
    });
}

void MainWindow::history()
{
    if (!m_provider)
        return;
    guarded([&] {
        const auto entries = m_db->history(m_provider->instance().id);
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Local tagging history · %1").arg(m_provider->instance().name));
        dialog.resize(900, 550);
        auto *layout = new QVBoxLayout(&dialog);
        auto *splitter = new QSplitter(&dialog);
        auto *list = new QListWidget(splitter);
        auto *text = new QPlainTextEdit(splitter);
        text->setReadOnly(true);
        for (const auto &entry : entries)
            list->addItem(QStringLiteral("#%1 · %2 · %3")
                              .arg(entry.postId)
                              .arg(entry.outcome, entry.time.toString("MM-dd hh:mm")));
        splitter->addWidget(list);
        splitter->addWidget(text);
        splitter->setStretchFactor(1, 2);
        layout->addWidget(splitter);
        layout->addWidget(new QLabel(tr("Local records only. This does not undo remote edits."), &dialog));
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        auto *browser = buttons->addButton(tr("Open post in browser"), QDialogButtonBox::ActionRole);
        layout->addWidget(buttons);
        connect(list, &QListWidget::currentRowChanged, &dialog, [entries, text](int row) {
            if (row < 0 || row >= entries.size())
                return;
            const auto &entry = entries.at(row);
            text->setPlainText(QObject::tr("Original snapshot:\n%1\n\nVerified result (empty for failed "
                                           "attempts):\n%2\n\nAdded:\n%3\n\nRemoved:\n%4\n\n%5")
                                   .arg(sortedTags(entry.original).join(' '), sortedTags(entry.result).join(' '),
                                        sortedTags(entry.result - entry.original).join(' '),
                                        sortedTags(entry.original - entry.result).join(' '), entry.error));
        });
        connect(browser, &QPushButton::clicked, &dialog, [this, entries, list] {
            if (list->currentRow() >= 0)
                openPost(entries.at(list->currentRow()).postId);
        });
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        list->setCurrentRow(entries.isEmpty() ? -1 : 0);
        dialog.exec();
    });
}

void MainWindow::rebuildPresets()
{
    m_presets->clear();
    if (!m_provider)
        return;
    guarded([&] {
        const auto presets = m_db->presets(m_provider->instance().id);
        for (auto it = presets.begin(); it != presets.end(); ++it) {
            auto *action = m_presets->addAction(it.key(), this, [this, tags = it.value()] {
                if (m_editor->isEnabled())
                    m_editor->addText(tags.join(' '));
            });
            action->setEnabled(m_editor->isEnabled());
        }
        m_presets->addSeparator();
        m_presets->addAction(tr("Save a preset…"), this, &MainWindow::savePreset);
        if (!presets.isEmpty()) {
            m_presets->addAction(tr("Delete a preset…"), this, [this, names = presets.keys()] {
                bool ok = false;
                const auto name = QInputDialog::getItem(this, tr("Delete preset"), tr("Preset"), names, 0, false, &ok);
                if (ok)
                    guarded([&] { m_db->deletePreset(m_provider->instance().id, name); });
            });
        }
    });
}

void MainWindow::savePreset()
{
    if (!m_provider)
        return;
    bool ok = false;
    const auto name = QInputDialog::getText(this, tr("Save preset"), tr("Preset name (this instance only)"),
                                            QLineEdit::Normal, {}, &ok)
                          .trimmed();
    if (!ok || name.isEmpty())
        return;
    const auto text = QInputDialog::getMultiLineText(this, tr("Preset tags"), tr("Space-separated tags"),
                                                     sortedTags(m_editor->changes().added).join(' '), &ok);
    if (ok)
        guarded(
            [&] { m_db->savePreset(m_provider->instance().id, name, splitTags(text, m_provider->instance().kind)); });
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_engine->busy()) {
        statusBar()->showMessage(
            tr("Wait for submission verification before closing. The draft is already stored locally."));
        event->ignore();
        return;
    }
    if (m_engine->blocked()) {
        QMessageBox warning(
            QMessageBox::Warning, tr("Draft may not be saved"),
            tr("Local storage failed. Export the current draft before closing to avoid losing the last edit."),
            QMessageBox::NoButton, this);
        auto *exportAndClose = warning.addButton(tr("Export draft and close"), QMessageBox::AcceptRole);
        auto *discard = warning.addButton(tr("Close anyway"), QMessageBox::DestructiveRole);
        auto *cancel = warning.addButton(QMessageBox::Cancel);
        warning.setDefaultButton(cancel);
        warning.exec();
        if (warning.clickedButton() != discard && (warning.clickedButton() != exportAndClose || !exportDraft())) {
            event->ignore();
            return;
        }
    }
    flushEdit();
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/splitter", m_splitter->saveState());
    event->accept();
}

bool MainWindow::exportDraft()
{
    if (!m_provider || m_engine->post().id < 1)
        return false;
    m_editor->commitInput();
    const auto &changes = m_editor->changes();
    const auto &draft = m_engine->draft();
    const QJsonObject object{{"instance_id", m_provider->instance().id},
                             {"session_id", m_engine->session().id},
                             {"post_id", m_engine->post().id},
                             {"original", QJsonArray::fromStringList(sortedTags(changes.original))},
                             {"added", QJsonArray::fromStringList(sortedTags(changes.added))},
                             {"removed", QJsonArray::fromStringList(sortedTags(changes.removed))},
                             {"intended", QJsonArray::fromStringList(sortedTags(draft.intended))},
                             {"state", stateName(draft.state)},
                             {"sufficient", m_sufficient->isChecked()}};
    const auto path = QFileDialog::getSaveFileName(this, tr("Export recovery draft"), "boorutagger-draft.json",
                                                   tr("JSON files (*.json)"));
    if (path.isEmpty())
        return false;
    QSaveFile file(path);
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        QMessageBox::critical(this, tr("Export failed"), file.errorString());
        return false;
    }
    return true;
}

} // namespace bt
