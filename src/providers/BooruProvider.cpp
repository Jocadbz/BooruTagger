#include "BooruProvider.h"
#include "DanbooruProvider.h"
#include "ShimmieProvider.h"

#include <QJsonDocument>
#include <algorithm>

namespace bt {

BooruProvider::BooruProvider(Instance instance, QObject *parent)
    : QObject(parent), m_http(std::move(instance), this)
{
    m_writeTimer.setSingleShot(true);
    connect(&m_writeTimer, &QTimer::timeout, this, [this] {
        m_nextWrite = QDateTime::currentDateTimeUtc().addMSecs(1100);
        auto operation = std::move(m_write);
        if (operation)
            operation();
    });
}

void BooruProvider::writeLater(std::function<void()> operation)
{
    m_write = std::move(operation);
    auto when = qMax(m_nextWrite, m_http.retryAt());
    const auto delay = std::clamp<qint64>(QDateTime::currentDateTimeUtc().msecsTo(when), 0, 24 * 60 * 60 * 1000);
    if (delay > 0)
        emit status(tr("Waiting for the server's write limit…"));
    m_writeTimer.start(static_cast<int>(delay));
}

void BooruProvider::cancel()
{
    ++m_generation;
    m_writeTimer.stop();
    m_write = {};
    m_http.cancelAll();
}

BooruProvider *createProvider(const Instance &instance, QObject *parent)
{
    if (instance.kind == ProviderKind::Danbooru)
        return new DanbooruProvider(instance, parent);
    return new ShimmieProvider(instance, parent);
}

Result<QJsonDocument> parseJson(const QByteArray &body)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || document.isNull())
        return Result<QJsonDocument>::failure({ErrorKind::InvalidData, QStringLiteral("The server returned invalid JSON.")});
    return Result<QJsonDocument>::success(document);
}

qint64 jsonId(const QJsonValue &value)
{
    return value.isString() ? value.toString().toLongLong() : value.toInteger();
}

}
