#include "Completion.h"

#include <QRegularExpression>
#include <algorithm>

namespace bt {

Completion::Completion(Database *database, QObject *parent) : QObject(parent), m_db(database)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(180);
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        if (!m_provider)
            return;
        const auto sequence = m_sequence;
        const auto prefix = m_prefix;
        const auto instanceId = m_provider->instance().id;
        QPointer<Completion> guard(this);
        m_provider->searchTags(prefix, [this, guard, sequence, prefix, instanceId](Result<QList<TagSuggestion>> result) {
            if (!guard || sequence != m_sequence || !m_provider)
                return;
            if (!result) {
                emit warning(result.error.message);
                return;
            }
            try {
                m_db->cacheTags(instanceId, *result.value);
                m_db->saveSearchCache(instanceId, prefix, *result.value);
                auto tags = *result.value;
                tags.append(m_db->cachedTags(instanceId, prefix));
                publish(tags);
            } catch (const std::exception &error) {
                emit warning(tr("Tag cache could not be saved: %1").arg(QString::fromUtf8(error.what())));
            }
        });
    });
}

void Completion::setProvider(BooruProvider *provider)
{
    ++m_sequence;
    m_debounce.stop();
    m_provider = provider;
    m_prefix.clear();
    m_current.clear();
    m_excluded.clear();
    emit suggestions({});
}

void Completion::request(const QString &text)
{
    ++m_sequence;
    m_debounce.stop();
    if (!m_provider)
        return;
    m_prefix = normalizeTag(text, m_provider->instance().kind);
    if (m_prefix.isEmpty() || m_prefix.contains(QRegularExpression("\\s"))) {
        m_current.clear();
        emit suggestions({});
        return;
    }
    try {
        auto tags = m_db->searchCache(m_provider->instance().id, m_prefix);
        tags.append(m_db->cachedTags(m_provider->instance().id, m_prefix));
        publish(tags);
    } catch (const std::exception &error) {
        emit warning(QString::fromUtf8(error.what()));
    }
    if (m_provider->remoteAutocomplete() && m_prefix.size() <= 256)
        m_debounce.start();
}

void Completion::setExcluded(QSet<QString> excluded)
{
    m_excluded = std::move(excluded);
    if (!m_prefix.isEmpty())
        publish(m_current);
}

void Completion::publish(QList<TagSuggestion> tags)
{
    m_current = tags;
    QSet<QString> seen;
    QList<TagSuggestion> filtered;
    for (const auto &tag : tags) {
        if (!seen.contains(tag.name) && !m_excluded.contains(tag.name)
            && (tag.name.startsWith(m_prefix) || (!tag.alias.isEmpty() && tag.alias.startsWith(m_prefix)))) {
            seen.insert(tag.name);
            filtered.append(tag);
        }
    }
    std::stable_sort(filtered.begin(), filtered.end(), [this](const auto &a, const auto &b) {
        const bool aExact = a.name == m_prefix || a.alias == m_prefix;
        const bool bExact = b.name == m_prefix || b.alias == m_prefix;
        return aExact != bExact ? aExact : false;
    });
    if (filtered.size() > 30)
        filtered.resize(30);
    emit suggestions(filtered);
}

}
