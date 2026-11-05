#pragma once

#include "database/Database.h"
#include "providers/BooruProvider.h"
#include <QPointer>
#include <QTimer>

namespace bt {

class Completion : public QObject {
    Q_OBJECT
  public:
    explicit Completion(Database *database, QObject *parent = nullptr);
    void setProvider(BooruProvider *provider);
    void request(const QString &prefix);
    void setExcluded(QSet<QString> excluded);
  signals:
    void suggestions(const QList<bt::TagSuggestion> &suggestions);
    void warning(const QString &message);

  private:
    void publish(QList<TagSuggestion> tags);
    Database *m_db;
    QPointer<BooruProvider> m_provider;
    QTimer m_debounce;
    quint64 m_sequence = 0;
    QString m_prefix;
    QSet<QString> m_excluded;
    QList<TagSuggestion> m_current;
};

} // namespace bt
