#pragma once

#include <QString>
#include <QSqlDatabase>

namespace ModelBench {

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

} // namespace ModelBench
