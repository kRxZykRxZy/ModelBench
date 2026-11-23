#include "FileEditHistory.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QSaveFile>

namespace ModelBench {

FileEditHistory::FileEditHistory(QObject *parent) : QObject(parent) {}
FileEditHistory::~FileEditHistory() = default;

void FileEditHistory::setLimit(int count) {
    if (count <= 0) return;
    m_limit = count;
    while (m_entries.size() > m_limit) m_entries.removeFirst();
}

void FileEditHistory::record(const QString &path, const QByteArray &before,
                             const QByteArray &after, bool existedBefore) {
    Entry entry;
    entry.path = path;
    entry.before = before;
    entry.after = after;
    entry.existedBefore = existedBefore;
    m_entries.append(entry);
    while (m_entries.size() > m_limit) m_entries.removeFirst();
    emit entryRecorded(path);
}

bool FileEditHistory::undoLast(QString *error) {
    if (m_entries.isEmpty()) {
        if (error) *error = QStringLiteral("Nothing to undo");
        return false;
    }
    const Entry entry = m_entries.takeLast();

    QFile current(entry.path);
    const bool exists = QFileInfo::exists(entry.path);
    if (exists) {
        if (!current.open(QIODevice::ReadOnly)) {
            if (error) *error = current.errorString();
            return false;
        }
        const QByteArray actual = current.readAll();
        current.close();
        if (actual != entry.after) {
            if (error)
                *error = QStringLiteral(
                    "%1 changed since the recorded edit; refusing to overwrite")
                             .arg(entry.path);
            return false;
        }
    }

    if (!entry.existedBefore) {
        if (exists && !current.remove()) {
            if (error) *error = current.errorString();
            return false;
        }
    } else {
        QSaveFile file(entry.path);
        file.setDirectWriteFallback(true);
        if (!file.open(QIODevice::WriteOnly)) {
            if (error) *error = file.errorString();
            return false;
        }
        if (file.write(entry.before) != entry.before.size() || !file.commit()) {
            if (error) *error = file.errorString();
            return false;
        }
    }

    emit entryUndone(entry.path);
    return true;
}

void FileEditHistory::clear() {
    if (m_entries.isEmpty()) return;
    m_entries.clear();
    emit historyCleared();
}

QJsonArray FileEditHistory::journal() const {
    QJsonArray entries;
    for (const Entry &entry : m_entries) {
        QJsonObject o;
        o[QStringLiteral("path")] = entry.path;
        o[QStringLiteral("existedBefore")] = entry.existedBefore;
        o[QStringLiteral("beforeBytes")] = entry.before.size();
        o[QStringLiteral("afterBytes")] = entry.after.size();
        entries.append(o);
    }
    return entries;
}

} // namespace ModelBench
