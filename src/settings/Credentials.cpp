#include "Credentials.h"
#include <QSettings>

#ifdef BOORUTAGGER_KEYCHAIN
#include <qt6keychain/keychain.h>
#endif

namespace bt {

bool Credentials::secureStorageBuiltIn()
{
#ifdef BOORUTAGGER_KEYCHAIN
    return true;
#else
    return false;
#endif
}

void Credentials::load(const QString &instanceId, Callback<QString> callback)
{
    if (m_memory.contains(instanceId)) {
        QMetaObject::invokeMethod(this, [this, instanceId, callback] { callback(Result<QString>::success(m_memory.value(instanceId))); }, Qt::QueuedConnection);
        return;
    }
    if (!QSettings().value("credentials/remember/" + instanceId, false).toBool()) {
        QMetaObject::invokeMethod(this, [callback] { callback(Result<QString>::success({})); }, Qt::QueuedConnection);
        return;
    }
#ifdef BOORUTAGGER_KEYCHAIN
    auto *job = new QKeychain::ReadPasswordJob("BooruTagger", this);
    job->setKey(instanceId);
    job->setInsecureFallback(false);
    connect(job, &QKeychain::Job::finished, this, [this, job, instanceId, callback] {
        if (job->error() == QKeychain::NoError) {
            m_memory.insert(instanceId, job->textData());
            callback(Result<QString>::success(job->textData()));
        } else if (job->error() == QKeychain::EntryNotFound) {
            callback(Result<QString>::success({}));
        } else {
            callback(Result<QString>::failure({ErrorKind::Other, tr("The OS keychain is unavailable. You can sign in for this run only.")}));
        }
    });
    job->start();
#else
    QMetaObject::invokeMethod(this, [callback] { callback(Result<QString>::success({})); }, Qt::QueuedConnection);
#endif
}

void Credentials::store(const QString &instanceId, const QString &secret, bool persist, Callback<bool> callback)
{
    m_memory.insert(instanceId, secret);
    if (!persist) {
        if (!QSettings().value("credentials/remember/" + instanceId, false).toBool()) {
            QMetaObject::invokeMethod(this, [callback] { callback(Result<bool>::success(true)); }, Qt::QueuedConnection);
            return;
        }
        forget(instanceId, [this, instanceId, secret, callback](Result<bool> result) {
            m_memory.insert(instanceId, secret);
            callback(result);
        });
        return;
    }
#ifdef BOORUTAGGER_KEYCHAIN
    auto *job = new QKeychain::WritePasswordJob("BooruTagger", this);
    job->setKey(instanceId);
    job->setInsecureFallback(false);
    job->setTextData(secret);
    connect(job, &QKeychain::Job::finished, this, [job, callback] {
        if (job->error() == QKeychain::NoError)
            callback(Result<bool>::success(true));
        else
            callback(Result<bool>::failure({ErrorKind::Other, tr("The keychain could not save the credential. It is kept in memory for this run only.")}));
    });
    job->start();
#else
    QMetaObject::invokeMethod(this, [callback] {
        callback(Result<bool>::failure({ErrorKind::Other, tr("This build has no OS keychain support. Credentials are kept in memory only.")}));
    }, Qt::QueuedConnection);
#endif
}

void Credentials::forget(const QString &instanceId, Callback<bool> callback)
{
    m_memory.remove(instanceId);
#ifdef BOORUTAGGER_KEYCHAIN
    auto *job = new QKeychain::DeletePasswordJob("BooruTagger", this);
    job->setKey(instanceId);
    job->setInsecureFallback(false);
    connect(job, &QKeychain::Job::finished, this, [job, callback] {
        if (job->error() == QKeychain::NoError || job->error() == QKeychain::EntryNotFound)
            callback(Result<bool>::success(true));
        else
            callback(Result<bool>::failure({ErrorKind::Other, tr("Could not remove a previously saved keychain entry. Remove it in your OS keychain if necessary.")}));
    });
    job->start();
#else
    QMetaObject::invokeMethod(this, [callback] { callback(Result<bool>::success(true)); }, Qt::QueuedConnection);
#endif
}

}
