#include "SystemIntegration.h"

#include <QClipboard>
#include <QColor>
#include <QGuiApplication>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSystemTrayIcon>

namespace ModelBench {

Clipboard::Clipboard(QObject *parent) : QObject(parent) {
    if (QGuiApplication::instance()) {
        connect(QGuiApplication::clipboard(), &QClipboard::dataChanged,
                this, &Clipboard::onSystemClipboardChanged);
    }
}

QString Clipboard::text() const {
    QClipboard *clipboard = QGuiApplication::clipboard();
    return clipboard ? clipboard->text() : QString();
}

void Clipboard::setText(const QString &text) {
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard) clipboard->setText(text, QClipboard::Clipboard);
}

void Clipboard::clear() {
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard) clipboard->clear(QClipboard::Clipboard);
}

void Clipboard::onSystemClipboardChanged() { emit textChanged(text()); }

Notifier::Notifier(QObject *parent) : QObject(parent) {}

Notifier::~Notifier() = default;

bool Notifier::isSupported() const {
    return QGuiApplication::instance() && QSystemTrayIcon::supportsMessages();
}

void Notifier::ensureTray() {
    if (m_tray || !QGuiApplication::instance()) return;
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;

    // A generated icon keeps showMessage working on platforms that refuse an
    // empty icon (notably macOS) without adding an image asset dependency.
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor(QStringLiteral("#5B6CFF")));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(pixmap.rect().adjusted(2, 2, -2, -2), 8, 8);
    painter.end();

    m_tray = new QSystemTrayIcon(QIcon(pixmap), this);
    m_tray->setToolTip(QStringLiteral("ModelBench"));
    m_tray->show();
}

void Notifier::notify(const QString &title, const QString &message, int durationMs) {
    // Always let the UI know: an in-app toast is the fallback for systems with
    // notifications disabled or no system tray at all.
    emit notificationRequested(title, message);
    if (!isSupported()) return;
    ensureTray();
    if (m_tray)
        m_tray->showMessage(title, message, QSystemTrayIcon::Information,
                            qBound(1000, durationMs, 30000));
}

} // namespace ModelBench
