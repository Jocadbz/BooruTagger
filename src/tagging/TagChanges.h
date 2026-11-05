#pragma once

#include "core/Types.h"

namespace bt {

struct TagChanges {
    QSet<QString> original;
    QSet<QString> added;
    QSet<QString> removed;

    QSet<QString> result() const;
    bool add(const QString &tag);
    bool remove(const QString &tag);
    bool dirty() const { return !added.isEmpty() || !removed.isEmpty(); }
    bool operator==(const TagChanges &) const = default;
};

struct TagMerge {
    QSet<QString> tags;
    QSet<QString> remoteAdded;
    QSet<QString> remoteRemoved;
    bool needsReview = false;
};

TagMerge mergeTags(const TagChanges &changes, const QSet<QString> &latest);
QList<qint64> idRange(qint64 first, qint64 last, qsizetype maximum = 100000);
QList<qint64> uniqueIds(const QList<qint64> &ids);

} // namespace bt

Q_DECLARE_METATYPE(bt::TagChanges)
