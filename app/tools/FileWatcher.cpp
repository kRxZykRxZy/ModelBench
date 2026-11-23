#include "FileWatcher.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

namespace ModelBench {
namespace {

QString normalizePath(const QString &path) {
    if (path.isEmpty()) return QString();
    return QDir::fromNativeSeparators(QDir::cleanPath(QDir().absoluteFilePath(path)));
}

} // namespace

FileWatcher::FileWatcher(QObject *parent) : QObject(parent) {
    m_watcher = new QFileSystemWatcher(this);
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(m_debounceMs);

    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, &FileWatcher::onFileChanged);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &FileWatcher::onDirectoryChanged);
    connect(m_timer, &QTimer::timeout, this, &FileWatcher::flushPending);
}

FileWatcher::~FileWatcher() = default;

bool FileWatcher::watch(const QString &path) {
    const QString target = normalizePath(path);
    if (target.isEmpty()) return false;

    const QFileInfo info(target);
    if (info.isDir()) {
        if (!m_directories.contains(target)) {
            m_watcher->addPath(target);
            m_directories.insert(target);
        }
        return true;
    }

    if (!m_hashes.contains(target)) {
        m_hashes.insert(target, info.exists() ? hashOf(target) : QByteArray());
        if (!info.exists()) m_missing.insert(target, true);
    }
    if (info.exists()) m_watcher->addPath(target);
    watchDirectoryOf(info.absolutePath());
    return true;
}

bool FileWatcher::unwatch(const QString &path) {
    const QString target = normalizePath(path);
    if (!m_hashes.contains(target) && !m_directories.contains(target)) return false;

    m_hashes.remove(target);
    m_missing.remove(target);
    m_pending.remove(target);
    m_watcher->removePath(target);

    // Drop parent directories that are no longer needed by any watched file.
    QStringList parents;
    for (auto it = m_hashes.constBegin(); it != m_hashes.constEnd(); ++it)
        parents.append(QFileInfo(it.key()).absolutePath());
    const QList<QString> directories = m_directories.values();
    for (const QString &directory : directories) {
        if (directory == target || !parents.contains(directory)) {
            m_watcher->removePath(directory);
            m_directories.remove(directory);
        }
    }
    return true;
}

void FileWatcher::clear() {
    m_timer->stop();
    const QStringList paths = m_watcher->files();
    if (!paths.isEmpty()) m_watcher->removePaths(paths);
    const QStringList directories = m_watcher->directories();
    if (!directories.isEmpty()) m_watcher->removePaths(directories);
    m_hashes.clear();
    m_missing.clear();
    m_pending.clear();
    m_directories.clear();
}

QStringList FileWatcher::watchedFiles() const { return m_hashes.keys(); }

bool FileWatcher::contains(const QString &path) const {
    return m_hashes.contains(normalizePath(path)) ||
           m_directories.contains(normalizePath(path));
}

void FileWatcher::watchDirectoryOf(const QString &path) {
    const QString directory = QFileInfo(path).absolutePath();
    if (directory.isEmpty() || !QFileInfo(directory).isDir()) return;
    if (m_directories.contains(directory)) return;
    m_watcher->addPath(directory);
    m_directories.insert(directory);
}

QByteArray FileWatcher::hashOf(const QString &path) const {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QByteArray();
    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(&file);
    return hash.result();
}

void FileWatcher::onFileChanged(const QString &rawPath) {
    if (!m_enabled) return;
    const QString path = normalizePath(rawPath);
    if (!m_hashes.contains(path)) return;

    const QFileInfo info(path);
    if (!info.exists()) {
        m_missing.insert(path, true);
        m_pending.remove(path);
        emit fileRemoved(path);
        watchDirectoryOf(path);
        return;
    }
    if (m_missing.take(path)) emit fileAdded(path);

    const QByteArray hash = hashOf(path);
    if (m_hashes.value(path) != hash) {
        m_pending.insert(path, true);
        if (!m_timer->isActive()) m_timer->start(m_debounceMs);
    }
}

void FileWatcher::onDirectoryChanged(const QString &rawPath) {
    if (!m_enabled) return;
    const QString directory = normalizePath(rawPath);
    QStringList revived;
    for (auto it = m_missing.constBegin(); it != m_missing.constEnd(); ++it) {
        const QFileInfo info(it.key());
        if (info.absolutePath() == directory && info.exists()) revived.append(it.key());
    }
    for (const QString &path : revived) {
        m_missing.remove(path);
        m_hashes[path] = hashOf(path);
        m_watcher->addPath(path);
        m_pending.insert(path, true);
        emit fileAdded(path);
    }
    if (!revived.isEmpty() && !m_timer->isActive()) m_timer->start(m_debounceMs);
}

void FileWatcher::flushPending() {
    const QStringList paths = m_pending.keys();
    m_pending.clear();
    for (const QString &path : paths) {
        if (!m_hashes.contains(path)) continue;
        m_hashes[path] = hashOf(path);
        emit fileChanged(path);
    }
}

void FileWatcher::rescan() {
    const QStringList paths = m_hashes.keys();
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (info.exists()) {
            if (m_missing.take(path)) emit fileAdded(path);
            m_watcher->addPath(path);
            const QByteArray hash = hashOf(path);
            if (m_hashes.value(path) != hash) {
                m_hashes[path] = hash;
                m_pending.insert(path, true);
            }
            watchDirectoryOf(path);
        } else {
            m_missing.insert(path, true);
        }
    }
    if (!m_pending.isEmpty() && !m_timer->isActive()) m_timer->start(m_debounceMs);
}

void FileWatcher::setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    if (enabled) {
        rescan();
    } else {
        m_timer->stop();
        m_pending.clear();
    }
    emit enabledChanged(enabled);
}

void FileWatcher::setDebounceMs(int ms) {
    ms = qBound(0, ms, 5000);
    if (m_debounceMs == ms) return;
    m_debounceMs = ms;
    m_timer->setInterval(ms);
    emit debounceMsChanged(ms);
}

} // namespace ModelBench
