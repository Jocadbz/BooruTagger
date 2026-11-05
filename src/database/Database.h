#pragma once

#include "sessions/Session.h"
#include <QMap>
#include <QSqlDatabase>
#include <QSqlQuery>

namespace bt {

class Database {
  public:
    explicit Database(const QString &path);
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    void saveInstance(const Instance &instance);
    QList<Instance> instances() const;
    std::optional<Instance> instance(const QString &id) const;
    Session createSession(Session session);
    void updateSession(const Session &session);
    std::optional<Session> session(const QString &id) const;
    QList<Session> sessions(const QString &instanceId = {}) const;
    int appendQueue(const QString &sessionId, const QList<qint64> &ids, int maximumNew = 100000);
    void sortQueue(const QString &sessionId, bool ascending);
    int queueSize(const QString &sessionId) const;
    int doneCount(const QString &sessionId) const;
    std::optional<QueueEntry> queueEntry(const QString &sessionId, int position) const;
    QList<QueueEntry> queueEntries(const QString &sessionId, int position, int limit = 4) const;
    void markQueue(const QString &sessionId, qint64 postId, const QString &status, const QString &error = {});
    int nextPending(const QString &sessionId, int after) const;
    void saveDraft(const QString &sessionId, qint64 postId, const Draft &draft);
    std::optional<Draft> draft(const QString &sessionId, qint64 postId) const;
    bool hasPendingDrafts(const QString &sessionId) const;
    void complete(Session &session, qint64 postId, const Draft &draft, const QSet<QString> &actual, bool advance);
    void recordHistory(const Session &session, qint64 postId, const Draft &draft, const QSet<QString> &actual,
                       const QString &outcome);
    QList<HistoryEntry> history(const QString &instanceId, int limit = 200) const;
    void observeTags(const QString &instanceId, const QStringList &tags, bool recentlyUsed = false);
    void cacheTags(const QString &instanceId, const QList<TagSuggestion> &tags);
    QList<TagSuggestion> cachedTags(const QString &instanceId, const QString &prefix, int limit = 30) const;
    void saveSearchCache(const QString &instanceId, const QString &prefix, const QList<TagSuggestion> &tags);
    QList<TagSuggestion> searchCache(const QString &instanceId, const QString &prefix) const;
    void savePreset(const QString &instanceId, const QString &name, const QStringList &tags);
    QMap<QString, QStringList> presets(const QString &instanceId) const;
    void deletePreset(const QString &instanceId, const QString &name);

  private:
    QSqlQuery run(const QString &sql, const QVariantList &values = {}) const;
    void transaction(const std::function<void()> &operation);
    void pruneTags(const QString &instanceId);
    QSqlDatabase m_db;
    QString m_connection;
};

} // namespace bt
