#pragma once

#include "database/Database.h"
#include "providers/BooruProvider.h"
#include <QCache>
#include <QPointer>

namespace bt {

enum class VerificationDecision { AcceptServer, AllowRetry, KeepUncertain };

class SessionEngine : public QObject {
    Q_OBJECT
  public:
    explicit SessionEngine(Database *database, QObject *parent = nullptr);
    ~SessionEngine() override;
    void create(Session session, BooruProvider *provider);
    void activate(const QString &sessionId, BooruProvider *provider);
    void deactivate();
    void useDiscoveredQueue();
    void edit(const TagChanges &changes, bool sufficient);
    void previous();
    void skip();
    void jump(int position);
    void reload();
    void save(bool advance);
    void resolveConflict(bool applyMerged);
    void resolveVerification(VerificationDecision decision);
    const Session &session() const { return m_session; }
    const Post &post() const { return m_post; }
    const Draft &draft() const { return m_draft; }
    SubmissionState state() const { return m_state; }
    bool busy() const { return submissionBusy(m_state); }
    bool building() const { return m_building; }
    bool ready() const { return m_ready && !m_blocked; }
    bool blocked() const { return m_blocked; }
  signals:
    void sessionChanged(const bt::Session &session);
    void postReady(const bt::Post &post, const bt::TagChanges &changes, bool sufficient);
    void stateChanged(bt::SubmissionState state);
    void loadingChanged(bool loading);
    void progressChanged(int position, int total, int done);
    void status(const QString &message);
    void queueSummary(const QString &message);
    void postSkipped(qint64 id, const QString &reason);
    void error(const bt::Error &error);
    void databaseError(const QString &message);
    void conflict(const bt::Post &latest, const bt::TagChanges &changes, const bt::TagMerge &merge);
    void verificationMismatch(const QSet<QString> &expected, const bt::Post &actual, bool uncertainWrite);
    void previewAvailable(const bt::Post &post);
    void finished();

  private:
    void safely(const std::function<void()> &operation);
    bool canUse(BooruProvider *provider, const Session &session);
    void detach();
    void discover();
    void finishDiscovery();
    void loadCurrent();
    void acceptPost(const Post &post);
    void prefetch();
    void progress();
    void transition(SubmissionState state);
    void submitMerged();
    void verify(bool uncertainWrite);
    void complete(const Post &actual);
    void fail(Error error, bool uncertain = false);
    Database *m_db;
    QPointer<BooruProvider> m_provider;
    Session m_session;
    Post m_post;
    Draft m_draft;
    Post m_latest;
    TagMerge m_merge;
    SubmissionState m_state = SubmissionState::Editing;
    QCache<qint64, Post> m_prefetched{8};
    quint64 m_epoch = 0;
    quint64 m_loadToken = 0;
    bool m_ready = false;
    bool m_building = false;
    bool m_discovering = false;
    bool m_advance = false;
    bool m_blocked = false;
    QString m_lastPage;
};

} // namespace bt
