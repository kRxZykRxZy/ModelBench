#pragma once

#include <QString>
#include <QSqlDatabase>

namespace ModelFit {

class Database {
public:
    explicit Database(const QString &path);
    ~Database();

    bool openAndMigrate();
    QSqlDatabase handle() const { return m_db; }

private:
    bool runMigrations();
    QString m_path;
    QSqlDatabase m_db;
};

} // namespace ModelFit
