#include "Database.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QUuid>
#include <stdexcept>

namespace bt {
namespace {
Session readSession(const QSqlQuery &q)
{
    Session s;
    s.id = q.value("id").toString();
    s.name = q.value("name").toString();
    s.instanceId = q.value("instance_id").toString();
    s.account = q.value("account").toString();
    s.source = static_cast<Source>(q.value("source").toInt());
    s.query = q.value("query").toString();
    s.firstId = q.value("first_id").toLongLong();
    s.lastId = q.value("last_id").toLongLong();
    s.limit = q.value("result_limit").toInt();
    s.removeTagme = q.value("remove_tagme").toBool();
    s.current = q.value("current_position").toInt();
    s.status = q.value("status").toString();
    s.cursor = q.value("cursor").toString();
    s.offset = q.value("discovery_offset").toInt();
    return s;
}
QueueEntry readQueue(const QSqlQuery &q)
{
    return {q.value("post_id").toLongLong(), q.value("position").toInt(), q.value("status").toString(), q.value("error").toString()};
}
QString text(const QString &s) { return s.isNull() ? QStringLiteral("") : s; }
}

Database::Database(const QString &path)
    : m_connection(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    if (path != ":memory:" && !QDir().mkpath(QFileInfo(path).absolutePath()))
        throw std::runtime_error("Could not create the application data directory.");
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connection);
    m_db.setDatabaseName(path);
    if (!m_db.open())
        throw std::runtime_error(m_db.lastError().text().toStdString());
    if (path != ":memory:")
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    run("PRAGMA foreign_keys=ON");
    run("PRAGMA journal_mode=WAL");
    run("PRAGMA synchronous=FULL");
    run("PRAGMA busy_timeout=3000");
    auto version = run("PRAGMA user_version");
    version.next();
    if (version.value(0).toInt() > 1)
        throw std::runtime_error("This database was created by a newer BooruTagger version.");
    transaction([this] {
        run("CREATE TABLE IF NOT EXISTS instances (id TEXT PRIMARY KEY, name TEXT NOT NULL, kind INTEGER NOT NULL CHECK(kind IN (0,1)), "
            "base_url TEXT NOT NULL, username TEXT NOT NULL, allow_http INTEGER NOT NULL)");
        run("CREATE TABLE IF NOT EXISTS sessions (id TEXT PRIMARY KEY, name TEXT NOT NULL, instance_id TEXT NOT NULL REFERENCES instances(id), "
            "account TEXT NOT NULL, source INTEGER NOT NULL, query TEXT NOT NULL, first_id INTEGER NOT NULL, last_id INTEGER NOT NULL, "
            "result_limit INTEGER NOT NULL, remove_tagme INTEGER NOT NULL, current_position INTEGER NOT NULL DEFAULT 0, "
            "status TEXT NOT NULL DEFAULT 'building', cursor TEXT NOT NULL DEFAULT '', discovery_offset INTEGER NOT NULL DEFAULT 0, updated INTEGER NOT NULL)");
        run("CREATE TABLE IF NOT EXISTS queue (session_id TEXT NOT NULL REFERENCES sessions(id) ON DELETE CASCADE, post_id INTEGER NOT NULL CHECK(post_id>0), "
            "position INTEGER NOT NULL, status TEXT NOT NULL DEFAULT 'todo', error TEXT NOT NULL DEFAULT '', "
            "PRIMARY KEY(session_id,post_id), UNIQUE(session_id,position))");
        run("CREATE TABLE IF NOT EXISTS drafts (session_id TEXT NOT NULL, post_id INTEGER NOT NULL, original TEXT NOT NULL, added TEXT NOT NULL, "
            "removed TEXT NOT NULL, state TEXT NOT NULL, intended TEXT NOT NULL, baseline TEXT NOT NULL, has_intended INTEGER NOT NULL, "
            "sufficient INTEGER NOT NULL, error TEXT NOT NULL, updated INTEGER NOT NULL, PRIMARY KEY(session_id,post_id), "
            "FOREIGN KEY(session_id,post_id) REFERENCES queue(session_id,post_id) ON DELETE CASCADE)");
        run("CREATE TABLE IF NOT EXISTS history (id INTEGER PRIMARY KEY, session_id TEXT NOT NULL REFERENCES sessions(id), post_id INTEGER NOT NULL, "
            "original TEXT NOT NULL, baseline TEXT NOT NULL, intended TEXT NOT NULL, result TEXT NOT NULL, outcome TEXT NOT NULL, error TEXT NOT NULL, created INTEGER NOT NULL)");
        run("CREATE TABLE IF NOT EXISTS tag_cache (instance_id TEXT NOT NULL REFERENCES instances(id), name TEXT NOT NULL, category INTEGER, post_count INTEGER, "
            "deprecated INTEGER NOT NULL DEFAULT 0, alias TEXT NOT NULL DEFAULT '', used_at INTEGER NOT NULL DEFAULT 0, seen_at INTEGER NOT NULL, "
            "PRIMARY KEY(instance_id,name))");
        run("CREATE TABLE IF NOT EXISTS search_cache (instance_id TEXT NOT NULL REFERENCES instances(id), prefix TEXT NOT NULL, tags TEXT NOT NULL, "
            "updated INTEGER NOT NULL, PRIMARY KEY(instance_id,prefix))");
        run("CREATE TABLE IF NOT EXISTS presets (instance_id TEXT NOT NULL REFERENCES instances(id), name TEXT NOT NULL, tags TEXT NOT NULL, PRIMARY KEY(instance_id,name))");
        run("CREATE TRIGGER IF NOT EXISTS fixed_instance BEFORE UPDATE OF base_url,kind,username ON instances "
            "WHEN EXISTS(SELECT 1 FROM sessions WHERE instance_id=OLD.id) AND (OLD.base_url!=NEW.base_url OR OLD.kind!=NEW.kind OR OLD.username!=NEW.username) "
            "BEGIN SELECT RAISE(ABORT,'Create a new instance profile to change its server or account.'); END");
        run("CREATE TRIGGER IF NOT EXISTS session_account BEFORE INSERT ON sessions "
            "WHEN NEW.account!=(SELECT username FROM instances WHERE id=NEW.instance_id) "
            "BEGIN SELECT RAISE(ABORT,'The session account does not match its instance.'); END");
        run("CREATE TRIGGER IF NOT EXISTS fixed_session BEFORE UPDATE OF instance_id,account ON sessions "
            "WHEN OLD.instance_id!=NEW.instance_id OR OLD.account!=NEW.account "
            "BEGIN SELECT RAISE(ABORT,'A session cannot change its instance or account.'); END");
        run("PRAGMA user_version=1");
    });
}

Database::~Database()
{
    m_db.close();
    m_db = {};
    QSqlDatabase::removeDatabase(m_connection);
}

QSqlQuery Database::run(const QString &sql, const QVariantList &values) const
{
    QSqlQuery query(m_db);
    if (!query.prepare(sql))
        throw std::runtime_error(query.lastError().text().toStdString());
    for (const auto &value : values)
        query.addBindValue(value);
    if (!query.exec())
        throw std::runtime_error(query.lastError().text().toStdString());
    return query;
}

void Database::transaction(const std::function<void()> &operation)
{
    if (!m_db.transaction())
        throw std::runtime_error(m_db.lastError().text().toStdString());
    try {
        operation();
        if (!m_db.commit())
            throw std::runtime_error(m_db.lastError().text().toStdString());
    } catch (...) {
        m_db.rollback();
        throw;
    }
}

void Database::saveInstance(const Instance &i)
{
    const auto error = validateInstance(i);
    if (!error.isEmpty() || i.id.isEmpty())
        throw std::runtime_error(error.isEmpty() ? "Missing instance ID." : error.toStdString());
    run("INSERT INTO instances(id,name,kind,base_url,username,allow_http) VALUES(?,?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET name=excluded.name,kind=excluded.kind,base_url=excluded.base_url,username=excluded.username,allow_http=excluded.allow_http",
        {i.id, i.name, static_cast<int>(i.kind), i.baseUrl.toString(), i.username, i.allowHttp});
}

QList<Instance> Database::instances() const
{
    auto q = run("SELECT * FROM instances ORDER BY name,id");
    QList<Instance> instances;
    while (q.next())
        instances.append({q.value("id").toString(), q.value("name").toString(), static_cast<ProviderKind>(q.value("kind").toInt()),
                          QUrl(q.value("base_url").toString()), q.value("username").toString(), q.value("allow_http").toBool()});
    return instances;
}

std::optional<Instance> Database::instance(const QString &id) const
{
    for (const auto &i : instances()) {
        if (i.id == id)
            return i;
    }
    return {};
}

Session Database::createSession(Session s)
{
    if (s.id.isEmpty())
        s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (s.name.trimmed().isEmpty() || s.limit < 1 || s.limit > 100000 || s.firstId < 1 || s.lastId < 1)
        throw std::runtime_error("Invalid session settings.");
    run("INSERT INTO sessions(id,name,instance_id,account,source,query,first_id,last_id,result_limit,remove_tagme,updated) VALUES(?,?,?,?,?,?,?,?,?,?,?)",
        {s.id, s.name, s.instanceId, s.account, static_cast<int>(s.source), text(s.query), s.firstId, s.lastId, s.limit, s.removeTagme, QDateTime::currentSecsSinceEpoch()});
    return s;
}

void Database::updateSession(const Session &s)
{
    run("UPDATE sessions SET current_position=?,status=?,cursor=?,discovery_offset=?,remove_tagme=?,updated=? WHERE id=? AND instance_id=? AND account=?",
        {s.current, s.status, text(s.cursor), s.offset, s.removeTagme, QDateTime::currentSecsSinceEpoch(), s.id, s.instanceId, s.account});
}

std::optional<Session> Database::session(const QString &id) const
{
    auto q = run("SELECT * FROM sessions WHERE id=?", {id});
    return q.next() ? std::optional(readSession(q)) : std::nullopt;
}

QList<Session> Database::sessions(const QString &instanceId) const
{
    auto q = instanceId.isEmpty() ? run("SELECT * FROM sessions ORDER BY updated DESC,id")
                                 : run("SELECT * FROM sessions WHERE instance_id=? ORDER BY updated DESC,id", {instanceId});
    QList<Session> sessions;
    while (q.next())
        sessions.append(readSession(q));
    return sessions;
}

int Database::appendQueue(const QString &sessionId, const QList<qint64> &ids, int maximumNew)
{
    int added = 0;
    transaction([&] {
        int next = queueSize(sessionId);
        for (const auto id : ids) {
            if (added >= maximumNew)
                break;
            if (id < 1)
                continue;
            auto query = run("INSERT OR IGNORE INTO queue(session_id,post_id,position) VALUES(?,?,?)", {sessionId, id, next});
            if (query.numRowsAffected() == 1) {
                ++next;
                ++added;
            }
        }
    });
    return added;
}

void Database::sortQueue(const QString &sessionId, bool ascending)
{
    transaction([&] {
        auto q = run(QStringLiteral("SELECT post_id FROM queue WHERE session_id=? ORDER BY post_id %1").arg(ascending ? "ASC" : "DESC"), {sessionId});
        QList<qint64> ids;
        while (q.next())
            ids.append(q.value(0).toLongLong());
        run("UPDATE queue SET position=-position-1 WHERE session_id=?", {sessionId});
        int position = 0;
        for (auto id : ids)
            run("UPDATE queue SET position=? WHERE session_id=? AND post_id=?", {position++, sessionId, id});
    });
}

int Database::queueSize(const QString &sessionId) const
{
    auto q = run("SELECT COUNT(*) FROM queue WHERE session_id=?", {sessionId});
    q.next();
    return q.value(0).toInt();
}

int Database::doneCount(const QString &sessionId) const
{
    auto q = run("SELECT COUNT(*) FROM queue WHERE session_id=? AND status IN ('completed','skipped')", {sessionId});
    q.next();
    return q.value(0).toInt();
}

std::optional<QueueEntry> Database::queueEntry(const QString &sessionId, int position) const
{
    auto q = run("SELECT * FROM queue WHERE session_id=? AND position=?", {sessionId, position});
    return q.next() ? std::optional(readQueue(q)) : std::nullopt;
}

QList<QueueEntry> Database::queueEntries(const QString &sessionId, int position, int limit) const
{
    auto q = run("SELECT * FROM queue WHERE session_id=? AND position>=? ORDER BY position LIMIT ?", {sessionId, position, limit});
    QList<QueueEntry> entries;
    while (q.next())
        entries.append(readQueue(q));
    return entries;
}

void Database::markQueue(const QString &sessionId, qint64 postId, const QString &status, const QString &error)
{
    run("UPDATE queue SET status=?,error=? WHERE session_id=? AND post_id=?", {status, text(error), sessionId, postId});
}

int Database::nextPending(const QString &sessionId, int after) const
{
    auto q = run("SELECT position FROM queue WHERE session_id=? AND position>? AND status IN ('todo','failed') ORDER BY position LIMIT 1", {sessionId, after});
    return q.next() ? q.value(0).toInt() : queueSize(sessionId);
}

void Database::saveDraft(const QString &sessionId, qint64 postId, const Draft &d)
{
    run("INSERT INTO drafts(session_id,post_id,original,added,removed,state,intended,baseline,has_intended,sufficient,error,updated) VALUES(?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(session_id,post_id) DO UPDATE SET original=excluded.original,added=excluded.added,removed=excluded.removed,state=excluded.state,"
        "intended=excluded.intended,baseline=excluded.baseline,has_intended=excluded.has_intended,sufficient=excluded.sufficient,error=excluded.error,updated=excluded.updated",
        {sessionId, postId, tagsJson(d.changes.original), tagsJson(d.changes.added), tagsJson(d.changes.removed), stateName(d.state),
         tagsJson(d.intended), tagsJson(d.baseline), d.hasIntended, d.sufficient, text(d.error), QDateTime::currentSecsSinceEpoch()});
}

std::optional<Draft> Database::draft(const QString &sessionId, qint64 postId) const
{
    auto q = run("SELECT * FROM drafts WHERE session_id=? AND post_id=?", {sessionId, postId});
    if (!q.next())
        return {};
    Draft d;
    d.changes = {tagsFromJson(q.value("original").toString()), tagsFromJson(q.value("added").toString()), tagsFromJson(q.value("removed").toString())};
    d.state = stateFromName(q.value("state").toString());
    d.intended = tagsFromJson(q.value("intended").toString());
    d.baseline = tagsFromJson(q.value("baseline").toString());
    d.hasIntended = q.value("has_intended").toBool();
    d.sufficient = q.value("sufficient").toBool();
    d.error = q.value("error").toString();
    return d;
}

bool Database::hasPendingDrafts(const QString &sessionId) const
{
    auto q = run("SELECT 1 FROM drafts WHERE session_id=? AND (added!='[]' OR removed!='[]' OR has_intended=1 OR sufficient=1) LIMIT 1", {sessionId});
    return q.next();
}

void Database::complete(Session &s, qint64 postId, const Draft &d, const QSet<QString> &actual, bool advance)
{
    auto next = s;
    transaction([&] {
        markQueue(s.id, postId, "completed");
        recordHistory(s, postId, d, actual, "completed");
        run("DELETE FROM drafts WHERE session_id=? AND post_id=?", {s.id, postId});
        if (advance) {
            next.current = nextPending(s.id, s.current);
            next.status = next.current >= queueSize(s.id) ? "finished" : "ready";
        }
        updateSession(next);
    });
    s = next;
}

void Database::recordHistory(const Session &s, qint64 postId, const Draft &d, const QSet<QString> &actual, const QString &outcome)
{
    run("INSERT INTO history(session_id,post_id,original,baseline,intended,result,outcome,error,created) VALUES(?,?,?,?,?,?,?,?,?)",
        {s.id, postId, tagsJson(d.changes.original), tagsJson(d.baseline), tagsJson(d.intended), tagsJson(actual), outcome, text(d.error), QDateTime::currentSecsSinceEpoch()});
}

QList<HistoryEntry> Database::history(const QString &instanceId, int limit) const
{
    auto q = run("SELECT h.* FROM history h JOIN sessions s ON h.session_id=s.id WHERE s.instance_id=? ORDER BY h.id DESC LIMIT ?", {instanceId, limit});
    QList<HistoryEntry> entries;
    while (q.next())
        entries.append({q.value("post_id").toLongLong(), q.value("session_id").toString(), tagsFromJson(q.value("original").toString()),
                        tagsFromJson(q.value("result").toString()), q.value("outcome").toString(), q.value("error").toString(),
                        QDateTime::fromSecsSinceEpoch(q.value("created").toLongLong())});
    return entries;
}

void Database::pruneTags(const QString &instanceId)
{
    run("DELETE FROM tag_cache WHERE instance_id=? AND name IN (SELECT name FROM tag_cache WHERE instance_id=? ORDER BY used_at DESC,seen_at DESC LIMIT -1 OFFSET 50000)",
        {instanceId, instanceId});
}

void Database::observeTags(const QString &instanceId, const QStringList &tags, bool recentlyUsed)
{
    transaction([&] {
        const auto now = QDateTime::currentSecsSinceEpoch();
        for (const auto &tag : tags) {
            run("INSERT INTO tag_cache(instance_id,name,used_at,seen_at) VALUES(?,?,?,?) ON CONFLICT(instance_id,name) DO UPDATE SET "
                "seen_at=excluded.seen_at,used_at=MAX(tag_cache.used_at,excluded.used_at)", {instanceId, tag, recentlyUsed ? now : 0, now});
        }
        pruneTags(instanceId);
    });
}

void Database::cacheTags(const QString &instanceId, const QList<TagSuggestion> &tags)
{
    transaction([&] {
        for (const auto &tag : tags)
            run("INSERT INTO tag_cache(instance_id,name,category,post_count,deprecated,alias,seen_at) VALUES(?,?,?,?,?,?,?) "
                "ON CONFLICT(instance_id,name) DO UPDATE SET category=excluded.category,post_count=excluded.post_count,deprecated=excluded.deprecated,alias=excluded.alias,seen_at=excluded.seen_at",
                {instanceId, tag.name, tag.category ? QVariant(*tag.category) : QVariant(), tag.postCount ? QVariant(*tag.postCount) : QVariant(),
                 tag.deprecated, text(tag.alias), QDateTime::currentSecsSinceEpoch()});
        pruneTags(instanceId);
    });
}

QList<TagSuggestion> Database::cachedTags(const QString &instanceId, const QString &prefix, int limit) const
{
    QString escaped = prefix;
    escaped.replace('!', "!!").replace('%', "!%").replace('_', "!_");
    auto q = run("SELECT * FROM tag_cache WHERE instance_id=? AND (name LIKE ? ESCAPE '!' OR alias LIKE ? ESCAPE '!') "
                 "ORDER BY (name=?) DESC,used_at DESC,post_count DESC,name LIMIT ?", {instanceId, escaped + '%', escaped + '%', prefix, limit * 4});
    QList<TagSuggestion> tags;
    while (q.next() && tags.size() < limit) {
        TagSuggestion tag;
        tag.name = q.value("name").toString();
        tag.alias = q.value("alias").toString();
        if (!tag.name.startsWith(prefix) && !tag.alias.startsWith(prefix))
            continue;
        if (!q.value("category").isNull())
            tag.category = q.value("category").toInt();
        if (!q.value("post_count").isNull())
            tag.postCount = q.value("post_count").toLongLong();
        tag.deprecated = q.value("deprecated").toBool();
        tags.append(tag);
    }
    return tags;
}

void Database::saveSearchCache(const QString &instanceId, const QString &prefix, const QList<TagSuggestion> &tags)
{
    QJsonArray array;
    for (const auto &tag : tags) {
        QJsonObject object{{"name", tag.name}, {"alias", tag.alias}, {"deprecated", tag.deprecated}};
        if (tag.category)
            object.insert("category", *tag.category);
        if (tag.postCount)
            object.insert("count", *tag.postCount);
        array.append(object);
    }
    transaction([&] {
        run("INSERT INTO search_cache(instance_id,prefix,tags,updated) VALUES(?,?,?,?) ON CONFLICT(instance_id,prefix) DO UPDATE SET tags=excluded.tags,updated=excluded.updated",
            {instanceId, prefix, QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)), QDateTime::currentSecsSinceEpoch()});
        run("DELETE FROM search_cache WHERE instance_id=? AND prefix IN (SELECT prefix FROM search_cache WHERE instance_id=? ORDER BY updated DESC LIMIT -1 OFFSET 200)", {instanceId, instanceId});
    });
}

QList<TagSuggestion> Database::searchCache(const QString &instanceId, const QString &prefix) const
{
    auto q = run("SELECT tags FROM search_cache WHERE instance_id=? AND prefix=?", {instanceId, prefix});
    QList<TagSuggestion> tags;
    if (q.next()) {
        for (const auto &value : QJsonDocument::fromJson(q.value(0).toByteArray()).array()) {
            const auto object = value.toObject();
            TagSuggestion tag{object.value("name").toString(), {}, {}, object.value("deprecated").toBool(), object.value("alias").toString()};
            if (object.contains("category"))
                tag.category = object.value("category").toInt();
            if (object.contains("count"))
                tag.postCount = object.value("count").toInteger();
            tags.append(tag);
        }
    }
    return tags;
}

void Database::savePreset(const QString &instanceId, const QString &name, const QStringList &tags)
{
    if (name.trimmed().isEmpty() || tags.isEmpty())
        throw std::runtime_error("A preset needs a name and at least one tag.");
    run("INSERT INTO presets(instance_id,name,tags) VALUES(?,?,?) ON CONFLICT(instance_id,name) DO UPDATE SET tags=excluded.tags", {instanceId, name, tagsJson(tagSet(tags))});
}

QMap<QString, QStringList> Database::presets(const QString &instanceId) const
{
    auto q = run("SELECT name,tags FROM presets WHERE instance_id=? ORDER BY name", {instanceId});
    QMap<QString, QStringList> result;
    while (q.next())
        result.insert(q.value(0).toString(), sortedTags(tagsFromJson(q.value(1).toString())));
    return result;
}

void Database::deletePreset(const QString &instanceId, const QString &name)
{
    run("DELETE FROM presets WHERE instance_id=? AND name=?", {instanceId, name});
}

}
