#include "SessionEngine.h"

#include <QRegularExpression>
#include <QTimer>

namespace bt {

SessionEngine::SessionEngine(Database *database, QObject *parent) : QObject(parent), m_db(database) {}

SessionEngine::~SessionEngine()
{
    detach();
}

void SessionEngine::safely(const std::function<void()> &operation)
{
    if (m_blocked)
        return;
    try {
        operation();
    } catch (const std::exception &error) {
        m_blocked = true;
        m_ready = false;
        m_state = SubmissionState::Failed;
        emit stateChanged(m_state);
        emit databaseError(tr("Local storage failed. Editing is paused to protect the draft.\n%1").arg(QString::fromUtf8(error.what())));
    }
}

bool SessionEngine::canUse(BooruProvider *provider, const Session &session)
{
    if (!provider || provider->instance().id != session.instanceId || provider->instance().username != session.account) {
        emit error({ErrorKind::Permission, tr("This session belongs to a different instance or account.")});
        return false;
    }
    if (!provider->authenticated()) {
        emit error({ErrorKind::Authentication, tr("Sign in to this session's account first.")});
        return false;
    }
    return true;
}

void SessionEngine::detach()
{
    ++m_epoch;
    ++m_loadToken;
    m_building = false;
    m_discovering = false;
    m_ready = false;
    if (m_provider)
        m_provider->cancel();
    m_provider.clear();
    m_prefetched.clear();
    m_post = {};
    m_draft = {};
    m_state = SubmissionState::Editing;
}

void SessionEngine::deactivate()
{
    if (busy())
        return;
    detach();
    m_session = {};
    emit loadingChanged(false);
    emit sessionChanged(m_session);
}

void SessionEngine::create(Session session, BooruProvider *provider)
{
    if (busy() || !canUse(provider, session))
        return;
    if (session.source == Source::Range && idRange(session.firstId, session.lastId).isEmpty()) {
        emit error({ErrorKind::Other, tr("Use positive IDs and a range of at most 100,000 posts.")});
        return;
    }
    if (session.source == Source::Search && session.query.trimmed().isEmpty()) {
        emit error({ErrorKind::Other, tr("Enter a search query.")});
        return;
    }
    if (provider->instance().kind == ProviderKind::Danbooru && session.source == Source::Search
        && QRegularExpression("(^|\\s)order:").match(session.query).hasMatch()) {
        emit error({ErrorKind::Other, tr("Remove the order: term. Session discovery uses stable descending ID order.")});
        return;
    }
    safely([&] {
        detach();
        m_provider = provider;
        m_session = m_db->createSession(session);
        m_building = true;
        m_lastPage.clear();
        emit sessionChanged(m_session);
        emit loadingChanged(true);
        if (session.source == Source::Single) {
            m_db->appendQueue(m_session.id, {session.firstId});
            finishDiscovery();
        } else if (session.source == Source::Range && provider->instance().kind == ProviderKind::Shimmie) {
            m_db->appendQueue(m_session.id, idRange(session.firstId, session.lastId));
            finishDiscovery();
        } else {
            discover();
        }
    });
}

void SessionEngine::activate(const QString &sessionId, BooruProvider *provider)
{
    if (busy())
        return;
    safely([&] {
        const auto session = m_db->session(sessionId);
        if (!session || !canUse(provider, *session))
            return;
        detach();
        m_provider = provider;
        m_session = *session;
        emit sessionChanged(m_session);
        if (m_session.status == "building") {
            m_building = true;
            m_lastPage.clear();
            if (provider->instance().kind == ProviderKind::Shimmie)
                m_session.offset = 0;
            emit loadingChanged(true);
            if (m_session.source == Source::Single) {
                m_db->appendQueue(m_session.id, {m_session.firstId});
                finishDiscovery();
            } else if (m_session.source == Source::Range && provider->instance().kind == ProviderKind::Shimmie) {
                m_db->appendQueue(m_session.id, idRange(m_session.firstId, m_session.lastId));
                finishDiscovery();
            } else {
                discover();
            }
        } else {
            loadCurrent();
        }
    });
}

void SessionEngine::discover()
{
    if (!m_provider || !m_building || m_discovering)
        return;
    const int count = m_db->queueSize(m_session.id);
    const int target = m_session.source == Source::Range ? idRange(m_session.firstId, m_session.lastId).size() : m_session.limit;
    if (count >= target) {
        finishDiscovery();
        return;
    }
    SearchRequest request;
    request.query = m_session.source == Source::Range
        ? QStringLiteral("id:%1..%2").arg(qMin(m_session.firstId, m_session.lastId)).arg(qMax(m_session.firstId, m_session.lastId))
        : m_session.query;
    request.limit = 100;
    request.cursor = m_session.cursor;
    request.offset = m_session.offset;
    const auto epoch = m_epoch;
    m_discovering = true;
    emit status(tr("Discovering queue… %1 / %2 IDs stored. Editing starts after discovery.").arg(count).arg(target));
    m_provider->searchPosts(request, [this, epoch, target](Result<SearchPage> result) {
        if (epoch != m_epoch || !m_building)
            return;
        m_discovering = false;
        safely([&] {
            if (!result) {
                emit loadingChanged(false);
                emit error(result.error);
                emit status(tr("Discovery paused. Retry the session or use the IDs already discovered."));
                return;
            }
            QList<qint64> ids;
            QStringList observed;
            QStringList fingerprint;
            for (const auto &post : result.value->posts) {
                fingerprint.append(QString::number(post.id));
                if (m_session.source == Source::Range && (post.id < qMin(m_session.firstId, m_session.lastId)
                    || post.id > qMax(m_session.firstId, m_session.lastId))) {
                    emit error({ErrorKind::InvalidData, tr("The server ignored the requested ID range. Discovery stopped.")});
                    emit loadingChanged(false);
                    return;
                }
                ids.append(post.id);
                observed.append(post.tags);
            }
            const auto pageKey = fingerprint.join(',');
            if (!pageKey.isEmpty() && pageKey == m_lastPage) {
                emit error({ErrorKind::InvalidData, tr("The server repeated a search page. Discovery stopped rather than looping.")});
                emit loadingChanged(false);
                return;
            }
            m_lastPage = pageKey;
            int remaining = target - m_db->queueSize(m_session.id);
            remaining -= m_db->appendQueue(m_session.id, uniqueIds(ids), remaining);
            observed.removeDuplicates();
            m_db->observeTags(m_session.instanceId, observed);
            if (m_provider->instance().kind == ProviderKind::Danbooru && result.value->more
                && (result.value->nextCursor.isEmpty() || result.value->nextCursor == m_session.cursor)) {
                emit error({ErrorKind::InvalidData, tr("The server did not provide a progressing ID cursor.")});
                emit loadingChanged(false);
                return;
            }
            m_session.cursor = result.value->nextCursor;
            m_session.offset += result.value->posts.size();
            m_db->updateSession(m_session);
            progress();
            if (!result.value->more || remaining < 1) {
                finishDiscovery();
            } else {
                QTimer::singleShot(80, this, [this, epoch] {
                    if (epoch == m_epoch)
                        safely([this] { discover(); });
                });
            }
        });
    });
}

void SessionEngine::useDiscoveredQueue()
{
    if (!m_building || busy())
        return;
    safely([&] {
        ++m_epoch;
        m_discovering = false;
        if (m_provider)
            m_provider->cancel();
        finishDiscovery();
    });
}

void SessionEngine::finishDiscovery()
{
    m_building = false;
    if (m_session.source == Source::Range)
        m_db->sortQueue(m_session.id, m_session.firstId <= m_session.lastId);
    m_session.status = m_db->queueSize(m_session.id) > 0 ? "ready" : "finished";
    m_db->updateSession(m_session);
    emit sessionChanged(m_session);
    if (m_session.source == Source::Range && m_provider->instance().kind == ProviderKind::Danbooru) {
        const auto total = idRange(m_session.firstId, m_session.lastId).size();
        const auto found = m_db->queueSize(m_session.id);
        emit status(tr("Range queued: %1 posts; %2 missing, deleted, or inaccessible IDs were not returned.").arg(found).arg(total - found));
    }
    loadCurrent();
}

void SessionEngine::loadCurrent()
{
    if (!m_provider)
        return;
    ++m_loadToken;
    const auto entry = m_db->queueEntry(m_session.id, m_session.current);
    m_ready = false;
    m_post = {};
    emit loadingChanged(true);
    progress();
    if (!entry) {
        emit loadingChanged(false);
        emit status(tr("End of queue. Previous or Jump can reopen posts and preserved skipped drafts."));
        emit finished();
        return;
    }
    m_draft = m_db->draft(m_session.id, entry->postId).value_or(Draft{});
    if (auto *cached = m_prefetched.take(entry->postId)) {
        const auto post = *cached;
        delete cached;
        acceptPost(post);
        return;
    }
    const auto token = m_loadToken;
    const auto epoch = m_epoch;
    emit status(tr("Loading post #%1…").arg(entry->postId));
    m_provider->fetchPost(entry->postId, [this, token, epoch, entry](Result<Post> result) {
        if (token != m_loadToken || epoch != m_epoch)
            return;
        safely([&] {
            if (!result) {
                emit loadingChanged(false);
                if ((result.error.kind == ErrorKind::Missing || result.error.kind == ErrorKind::Permission)
                    && !m_draft.changes.dirty() && !m_draft.hasIntended) {
                    m_db->markQueue(m_session.id, entry->postId, "skipped", result.error.message);
                    emit status(tr("Skipped #%1: %2").arg(entry->postId).arg(result.error.message));
                    m_session.current = m_db->nextPending(m_session.id, m_session.current);
                    m_db->updateSession(m_session);
                    QTimer::singleShot(0, this, [this, epoch] { if (epoch == m_epoch) safely([this] { loadCurrent(); }); });
                } else {
                    emit error(result.error);
                }
                return;
            }
            if (result.value->id != entry->postId) {
                emit error({ErrorKind::InvalidData, tr("Ignored a response for another post.")});
                emit loadingChanged(false);
                return;
            }
            acceptPost(*result.value);
        });
    });
}

void SessionEngine::acceptPost(const Post &post)
{
    m_post = post;
    if (post.deleted && !m_draft.changes.dirty() && !m_draft.hasIntended) {
        m_db->markQueue(m_session.id, post.id, "skipped", "deleted");
        emit status(tr("Skipped deleted post #%1.").arg(post.id));
        m_session.current = m_db->nextPending(m_session.id, m_session.current);
        m_db->updateSession(m_session);
        const auto epoch = m_epoch;
        QTimer::singleShot(0, this, [this, epoch] { if (epoch == m_epoch) safely([this] { loadCurrent(); }); });
        return;
    }
    if (!m_db->draft(m_session.id, post.id))
        m_draft.changes.original = tagSet(post.tags);
    if (submissionBusy(m_draft.state))
        m_draft.state = m_draft.hasIntended ? SubmissionState::Uncertain : SubmissionState::Editing;
    m_ready = true;
    m_state = m_draft.state;
    m_db->saveDraft(m_session.id, post.id, m_draft);
    m_db->observeTags(m_session.instanceId, post.tags);
    emit postReady(m_post, m_draft.changes, m_draft.sufficient);
    emit stateChanged(m_state);
    emit loadingChanged(false);
    if (m_state == SubmissionState::Uncertain)
        emit status(tr("A previous submission needs verification. Press Save to check it before editing."));
    prefetch();
}

void SessionEngine::prefetch()
{
    const auto epoch = m_epoch;
    for (const auto &entry : m_db->queueEntries(m_session.id, m_session.current + 1, 3)) {
        if (m_prefetched.contains(entry.postId))
            continue;
        m_provider->fetchPost(entry.postId, [this, epoch](Result<Post> result) {
            if (epoch != m_epoch || !result)
                return;
            safely([&] {
                m_prefetched.insert(result.value->id, new Post(*result.value));
                m_db->observeTags(m_session.instanceId, result.value->tags);
                emit previewAvailable(*result.value);
            });
        });
    }
}

void SessionEngine::edit(const TagChanges &changes, bool sufficient)
{
    if (!ready() || busy() || m_state == SubmissionState::Uncertain)
        return;
    safely([&] {
        if (changes.original != m_draft.changes.original) {
            emit error({ErrorKind::InvalidData, tr("The editor snapshot does not match the active post.")});
            return;
        }
        m_draft.changes = changes;
        m_draft.sufficient = sufficient;
        m_draft.hasIntended = false;
        m_draft.error.clear();
        m_db->observeTags(m_session.instanceId, sortedTags(changes.added), true);
        transition(SubmissionState::Editing);
    });
}

void SessionEngine::transition(SubmissionState state)
{
    m_draft.state = state;
    m_db->saveDraft(m_session.id, m_post.id, m_draft);
    m_state = state;
    emit stateChanged(state);
}

void SessionEngine::progress()
{
    const auto total = m_db->queueSize(m_session.id);
    emit progressChanged(qMin(m_session.current + 1, total), total, m_db->doneCount(m_session.id));
}

void SessionEngine::previous() { jump(qMax(0, m_session.current - 1)); }

void SessionEngine::skip()
{
    if (busy() || m_building || m_session.id.isEmpty())
        return;
    safely([&] {
        const auto entry = m_db->queueEntry(m_session.id, m_session.current);
        if (entry && entry->status != "completed")
            m_db->markQueue(m_session.id, entry->postId, "skipped");
        jump(m_db->nextPending(m_session.id, m_session.current));
    });
}

void SessionEngine::jump(int position)
{
    if (busy() || m_building || m_session.id.isEmpty())
        return;
    safely([&] {
        if (position < 0 || position > m_db->queueSize(m_session.id))
            return;
        m_session.current = position;
        m_session.status = position < m_db->queueSize(m_session.id) ? "ready" : "finished";
        m_db->updateSession(m_session);
        loadCurrent();
    });
}

void SessionEngine::reload()
{
    if (busy() || m_session.id.isEmpty())
        return;
    safely([&] {
        if (m_building)
            discover();
        else
            loadCurrent();
    });
}

void SessionEngine::save(bool advance)
{
    if (!ready() || busy() || !m_provider)
        return;
    safely([&] {
        m_advance = advance;
        if (m_state == SubmissionState::Uncertain && m_draft.hasIntended) {
            transition(SubmissionState::Confirming);
            verify(true);
            return;
        }
        if (m_session.removeTagme && m_draft.sufficient)
            m_draft.changes.remove("tagme");
        m_draft.error.clear();
        transition(SubmissionState::Pending);
        const auto epoch = m_epoch;
        emit status(tr("Checking the latest tags before saving…"));
        m_provider->fetchPost(m_post.id, [this, epoch](Result<Post> result) {
            if (epoch != m_epoch)
                return;
            safely([&] {
                if (!result) {
                    fail(result.error);
                    return;
                }
                if (result.value->id != m_post.id || result.value->deleted) {
                    fail({ErrorKind::Permission, tr("The post changed identity or is deleted. No write was sent.")});
                    return;
                }
                m_latest = *result.value;
                m_merge = mergeTags(m_draft.changes, tagSet(m_latest.tags));
                if (m_merge.needsReview) {
                    transition(SubmissionState::Reviewing);
                    emit conflict(m_latest, m_draft.changes, m_merge);
                } else {
                    submitMerged();
                }
            });
        });
    });
}

void SessionEngine::resolveConflict(bool applyMerged)
{
    if (m_state != SubmissionState::Reviewing || m_draft.hasIntended)
        return;
    safely([&] {
        if (applyMerged)
            submitMerged();
        else
            transition(SubmissionState::Editing);
    });
}

void SessionEngine::submitMerged()
{
    m_draft.baseline = tagSet(m_latest.tags);
    m_draft.intended = m_merge.tags;
    m_draft.hasIntended = true;
    if (m_draft.intended == m_draft.baseline) {
        complete(m_latest);
        return;
    }
    transition(SubmissionState::Submitting);
    emit status(tr("Submitting tags for #%1…").arg(m_post.id));
    const auto epoch = m_epoch;
    m_provider->updateTags(m_post.id, m_draft.intended, m_draft.baseline, [this, epoch](Result<bool> result) {
        if (epoch != m_epoch)
            return;
        safely([&] {
            if (!result || !*result.value) {
                const auto error = result ? Error{ErrorKind::Other, tr("The provider did not confirm the write.")} : result.error;
                if (error.uncertain) {
                    m_draft.error = error.message;
                    transition(SubmissionState::Confirming);
                    emit status(tr("The write response was interrupted. Checking the post before any retry…"));
                    verify(true);
                } else {
                    fail(error);
                }
                return;
            }
            transition(SubmissionState::Confirming);
            verify(false);
        });
    });
}

void SessionEngine::verify(bool uncertainWrite)
{
    const auto epoch = m_epoch;
    m_provider->fetchPost(m_post.id, [this, epoch, uncertainWrite](Result<Post> result) {
        if (epoch != m_epoch)
            return;
        safely([&] {
            if (!result) {
                fail(result.error, true);
                return;
            }
            if (result.value->id != m_post.id) {
                fail({ErrorKind::InvalidData, tr("Verification returned a different post.")}, true);
                return;
            }
            if (tagSet(result.value->tags) == m_draft.intended) {
                complete(*result.value);
            } else {
                m_latest = *result.value;
                transition(SubmissionState::Reviewing);
                emit verificationMismatch(m_draft.intended, m_latest, uncertainWrite);
            }
        });
    });
}

void SessionEngine::resolveVerification(VerificationDecision decision)
{
    if (m_state != SubmissionState::Reviewing || !m_draft.hasIntended)
        return;
    safely([&] {
        if (decision == VerificationDecision::AcceptServer) {
            complete(m_latest);
        } else if (decision == VerificationDecision::AllowRetry) {
            m_draft.hasIntended = false;
            transition(SubmissionState::Editing);
            emit status(tr("Draft kept. Review the tags, then Save to make a new attempt using a fresh server snapshot."));
        } else {
            transition(SubmissionState::Uncertain);
            emit status(tr("Submission remains uncertain. The draft is safe; no write will be repeated automatically."));
        }
    });
}

void SessionEngine::complete(const Post &actual)
{
    m_db->complete(m_session, m_post.id, m_draft, tagSet(actual.tags), m_advance);
    m_post = actual;
    m_draft = {};
    m_draft.changes.original = tagSet(actual.tags);
    m_draft.state = SubmissionState::Completed;
    m_state = SubmissionState::Completed;
    emit stateChanged(m_state);
    emit status(tr("Verified and saved #%1.").arg(actual.id));
    emit sessionChanged(m_session);
    progress();
    if (m_advance) {
        loadCurrent();
    } else {
        emit postReady(m_post, m_draft.changes, false);
    }
}

void SessionEngine::fail(Error error, bool uncertain)
{
    m_draft.error = error.message;
    if (!uncertain)
        m_draft.hasIntended = false;
    transition(uncertain ? SubmissionState::Uncertain : SubmissionState::Failed);
    m_db->markQueue(m_session.id, m_post.id, "failed", error.message);
    m_db->recordHistory(m_session, m_post.id, m_draft, {}, uncertain ? "uncertain" : "failed");
    emit this->error(error);
    emit status(uncertain ? tr("Submission is uncertain. Save checks the server before retrying; the draft is preserved.")
                          : tr("Save failed. The draft is preserved and the queue has not advanced."));
}

}
