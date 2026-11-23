#pragma once

#include <QObject>
#include <QByteArray>
#include <QJsonArray>
#include <QList>
#include <QString>

namespace ModelBench {

// Tracks write_file / edit_file changes so the UI can offer "undo last agent
// edit". Undo is conservative: if the file changed since the recorded edit,
// the undo is refused instead of clobbering newer user work.
class FileEditHistory : public QObject {
    Q_OBJECT
public:
    explicit FileEditHistory(QObject *parent = nullptr);
    ~FileEditHistory() override;

    void record(const QString &path, const QByteArray &before,
                const QByteArray &after, bool existedBefore = true);

    bool canUndo() const { return !m_entries.isEmpty(); }
    int count() const { return m_entries.size(); }
    int limit() const { return m_limit; }
    void setLimit(int count);

    bool undoLast(QString *error = nullptr);
    void clear();

    QJsonArray journal() const;

signals:
    void entryRecorded(const QString &path);
    void entryUndone(const QString &path);
    void historyCleared();

private:
    struct Entry {
        QString path;
        QByteArray before;
        QByteArray after;
        bool existedBefore = true;
    };
    QList<Entry> m_entries;
    int m_limit = 100;
};

} // namespace ModelBench
