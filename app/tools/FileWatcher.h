#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class QFileSystemWatcher;
class QTimer;

namespace ModelBench {

// Debounced file watcher for auto-reload of open files.
//
// Watches each file plus its parent directory. Watching the parent is what
// makes atomic saves (write temp file + rename) and delete/re-create work on
// every platform - QFileSystemWatcher drops a file path when it is removed.
// A content hash suppresses the duplicate notifications most editors emit,
// and bursts are coalesced into one fileChanged() per debounce window.
class FileWatcher : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(int debounceMs READ debounceMs WRITE setDebounceMs NOTIFY debounceMsChanged)

public:
    explicit FileWatcher(QObject *parent = nullptr);
    ~FileWatcher() override;

    bool isEnabled() const { return m_enabled; }
    int debounceMs() const { return m_debounceMs; }

    Q_INVOKABLE bool watch(const QString &path);
    Q_INVOKABLE bool unwatch(const QString &path);
    Q_INVOKABLE void clear();
    Q_INVOKABLE QStringList watchedFiles() const;
    Q_INVOKABLE bool contains(const QString &path) const;
    // Re-arm paths that vanished and have since come back.
    Q_INVOKABLE void rescan();

public slots:
    void setEnabled(bool enabled);
    void setDebounceMs(int ms);

signals:
    void fileChanged(const QString &path);
    void fileRemoved(const QString &path);
    void fileAdded(const QString &path);
    void enabledChanged(bool enabled);
    void debounceMsChanged(int ms);

private slots:
    void onFileChanged(const QString &path);
    void onDirectoryChanged(const QString &path);
    void flushPending();

private:
    QByteArray hashOf(const QString &path) const;
    void watchDirectoryOf(const QString &path);

    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_timer = nullptr;
    QHash<QString, QByteArray> m_hashes;
    QHash<QString, bool> m_missing;
    QHash<QString, bool> m_pending;
    QSet<QString> m_directories;
    bool m_enabled = true;
    int m_debounceMs = 150;
};

} // namespace ModelBench
