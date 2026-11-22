#pragma once

#include <QString>
#include <QJsonObject>
#include <QList>

namespace ModelFit {

struct InstalledModel {
    QString id;
    QString path;
    QJsonObject metadata;
    qint64 installedAt = 0;
};

class ModelRegistry {
public:
    void add(const InstalledModel &m);
    void remove(const QString &id);
    QList<InstalledModel> all() const;
    InstalledModel find(const QString &id) const;
private:
    QList<InstalledModel> m_models;
};

} // namespace ModelFit
