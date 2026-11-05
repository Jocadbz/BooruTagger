#pragma once

#include "core/Types.h"
#include <QHash>
#include <QObject>

namespace bt {

class Credentials : public QObject {
    Q_OBJECT
  public:
    explicit Credentials(QObject *parent = nullptr) : QObject(parent) {}
    void load(const QString &instanceId, Callback<QString> callback);
    void store(const QString &instanceId, const QString &secret, bool persist, Callback<bool> callback);
    void forget(const QString &instanceId, Callback<bool> callback);
    static bool secureStorageBuiltIn();

  private:
    QHash<QString, QString> m_memory;
};

} // namespace bt
