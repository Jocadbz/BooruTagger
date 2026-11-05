#include "TagChanges.h"

namespace bt {

QSet<QString> TagChanges::result() const
{
    return (original - removed) | added;
}

bool TagChanges::add(const QString &tag)
{
    if (tag.isEmpty() || result().contains(tag))
        return false;
    if (original.contains(tag))
        removed.remove(tag);
    else
        added.insert(tag);
    return true;
}

bool TagChanges::remove(const QString &tag)
{
    if (!result().contains(tag))
        return false;
    if (!added.remove(tag))
        removed.insert(tag);
    return true;
}

TagMerge mergeTags(const TagChanges &changes, const QSet<QString> &latest)
{
    TagMerge merge;
    merge.tags = (latest - changes.removed) | changes.added;
    merge.remoteAdded = latest - changes.original;
    merge.remoteRemoved = changes.original - latest;
    // Remote additions are safe. Deletions of tags we still rely on deserve a look.
    merge.needsReview = changes.dirty() && !(merge.remoteRemoved - changes.removed).isEmpty();
    return merge;
}

QList<qint64> idRange(qint64 first, qint64 last, qsizetype maximum)
{
    QList<qint64> ids;
    if (first < 1 || last < 1 || maximum < 1)
        return ids;
    const qint64 distance = first > last ? first - last : last - first;
    if (distance >= maximum)
        return ids;
    ids.reserve(distance + 1);
    const qint64 step = first <= last ? 1 : -1;
    for (auto id = first;; id += step) {
        ids.append(id);
        if (id == last)
            break;
    }
    return ids;
}

QList<qint64> uniqueIds(const QList<qint64> &ids)
{
    QSet<qint64> seen;
    QList<qint64> result;
    for (auto id : ids) {
        if (id > 0 && !seen.contains(id)) {
            seen.insert(id);
            result.append(id);
        }
    }
    return result;
}

} // namespace bt
