#include "Repositories.h"

#include <QSqlDatabase>
#include <QSqlQuery>

namespace ModelBench {

Repositories::Repositories(const QString &dbPath) : m_dbPath(dbPath) {}

bool Repositories::setSetting(const QString &key, const QString &value) {
    QSqlDatabase db = QSqlDatabase::database("modelfit");
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO app_settings (key, value) VALUES (?, ?);");
    q.addBindValue(key);
    q.addBindValue(value);
    return q.exec();
}

QString Repositories::getSetting(const QString &key, const QString &defaultValue) const {
    QSqlDatabase db = QSqlDatabase::database("modelfit");
    QSqlQuery q(db);
    q.prepare("SELECT value FROM app_settings WHERE key=?;");
    q.addBindValue(key);
    if (q.exec() && q.next()) return q.value(0).toString();
    return defaultValue;
}

} // namespace ModelBench
