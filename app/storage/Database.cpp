#include "Database.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QDebug>

namespace ModelBench {

Database::Database(const QString &path) : m_path(path) {}

Database::~Database() {
    if (m_db.isOpen()) m_db.close();
}

bool Database::openAndMigrate() {
    m_db = QSqlDatabase::addDatabase("QSQLITE", "modelfit");
    m_db.setDatabaseName(m_path);
    if (!m_db.open()) {
        qCritical() << "DB open failed:" << m_db.lastError().text();
        return false;
    }
    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode=WAL;");
    q.exec("PRAGMA foreign_keys=ON;");
    return runMigrations();
}

bool Database::runMigrations() {
    QSqlQuery q(m_db);
    if (!q.exec("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER PRIMARY KEY);")) return false;
    q.exec("INSERT OR IGNORE INTO schema_version (version) VALUES (0);");
    int version = 0;
    QSqlQuery vq(m_db);
    if (vq.exec("SELECT version FROM schema_version LIMIT 1;") && vq.next()) version = vq.value(0).toInt();

    // Migration 1: core tables
    if (version < 1) {
        QSqlQuery t(m_db);
        t.exec("CREATE TABLE IF NOT EXISTS app_settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);");
        t.exec("CREATE TABLE IF NOT EXISTS conversations (id TEXT PRIMARY KEY, title TEXT, model_id TEXT, created_at INTEGER, updated_at INTEGER);");
        t.exec("CREATE TABLE IF NOT EXISTS messages (id TEXT PRIMARY KEY, conversation_id TEXT, role TEXT, content TEXT, created_at INTEGER, FOREIGN KEY(conversation_id) REFERENCES conversations(id) ON DELETE CASCADE);");
        t.exec("CREATE TABLE IF NOT EXISTS installed_models (id TEXT PRIMARY KEY, path TEXT, metadata TEXT, installed_at INTEGER);");
        t.exec("UPDATE schema_version SET version=1;");
        version = 1;
    }
    qInfo() << "Database migrated to version" << version;
    return true;
}

} // namespace ModelBench
