#pragma once

#include <QObject>
#include <QString>

class QSystemTrayIcon;

namespace ModelBench {

// Thin QML-facing wrapper over QClipboard. Works in a headless/test process
// too (all calls simply become no-ops when there is no QGuiApplication).
class Clipboard : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)

public:
    explicit Clipboard(QObject *parent = nullptr);

    QString text() const;

public slots:
    void setText(const QString &text);
    void clear();

signals:
    void textChanged(const QString &text);

private slots:
    void onSystemClipboardChanged();
};

// Desktop notifications. Native delivery uses QSystemTrayIcon::showMessage
// (system tray on Windows, notification centre on macOS, freedesktop.org
// notifications on Linux). notificationRequested is always emitted as well so
// the UI can render an in-app toast when native notifications are disabled.
class Notifier : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool supported READ isSupported CONSTANT)

public:
    explicit Notifier(QObject *parent = nullptr);
    ~Notifier() override;

    bool isSupported() const;

    Q_INVOKABLE void notify(const QString &title, const QString &message,
                            int durationMs = 5000);

signals:
    void notificationRequested(const QString &title, const QString &message);

private:
    void ensureTray();

    QSystemTrayIcon *m_tray = nullptr;
};

} // namespace ModelBench
