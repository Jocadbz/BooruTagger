#pragma once

#include "core/Types.h"
#include <QMainWindow>
#include <memory>

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QMenu;
class QProgressBar;
class QSplitter;

namespace bt {
class BooruProvider;
class Completion;
class Credentials;
class Database;
class MediaViewer;
class SessionEngine;
class TagEditor;

class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget *parent = nullptr, const QString &databasePath = {});
    ~MainWindow() override;

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    void buildUi();
    void connectEngine();
    void refreshInstances(const QString &selected = {});
    void selectInstance(const QString &id);
    void refreshSessions();
    void addInstance();
    bool signIn();
    void newSession();
    void openSession(const QString &id);
    void flushEdit();
    void updateActions();
    void showError(const Error &error);
    void openPost(qint64 id);
    void jump();
    void history();
    void rebuildPresets();
    void savePreset();
    bool exportDraft();
    void guarded(const std::function<void()> &operation);
    std::unique_ptr<Database> m_db;
    SessionEngine *m_engine = nullptr;
    BooruProvider *m_provider = nullptr;
    Credentials *m_credentials;
    Completion *m_completion;
    TagEditor *m_editor;
    MediaViewer *m_media;
    QSplitter *m_splitter;
    QComboBox *m_instances;
    QComboBox *m_sessions;
    QCheckBox *m_sufficient;
    QLabel *m_info;
    QLabel *m_progress;
    QLabel *m_state;
    QLabel *m_error;
    QLabel *m_notice;
    QProgressBar *m_progressBar;
    QMenu *m_presets;
    QAction *m_addInstance;
    QAction *m_signIn;
    QAction *m_newSession;
    QAction *m_previous;
    QAction *m_skip;
    QAction *m_save;
    QAction *m_saveNext;
    QAction *m_openBrowser;
    QAction *m_retry;
    QAction *m_jump;
    QAction *m_useQueue;
    QList<QAction *> m_tagActions;
    bool m_syncing = false;
    bool m_loading = false;
    QString m_noticeSession;
    QString m_mediaContext;
};

void applyAppearance(bool dark);

} // namespace bt
