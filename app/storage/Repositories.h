#pragma once

#include <QString>
#include <QVariantMap>

namespace ModelFit {

class Repositories {
public:
    explicit Repositories(const QString &dbPath);
    bool setSetting(const QString &key, const QString &value);
    QString getSetting(const QString &key, const QString &defaultValue = {}) const;
private:
    QString m_dbPath;
};

} // namespace ModelFit
