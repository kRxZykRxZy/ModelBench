#pragma once

#include <QString>
#include <QByteArray>

namespace ModelFit {

class ISecretStorage {
public:
    virtual ~ISecretStorage() = default;
    virtual bool store(const QString &service, const QString &account, const QByteArray &secret) = 0;
    virtual QByteArray load(const QString &service, const QString &account) = 0;
    virtual bool remove(const QString &service, const QString &account) = 0;
};

// Factory
ISecretStorage *createSecretStorage();

} // namespace ModelFit
