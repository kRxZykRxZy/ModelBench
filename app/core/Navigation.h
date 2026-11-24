#ifndef MODELBENCH_NAVIGATION_H
#define MODELBENCH_NAVIGATION_H

#include <QObject>
#include <QString>

namespace ModelBench {

// Simple navigation state exposed to QML as the "Navigation" context property.
class Navigation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentView READ currentView WRITE setCurrentView NOTIFY currentViewChanged)
public:
    explicit Navigation(QObject *parent = nullptr);

    QString currentView() const;
    void setCurrentView(const QString &view);

    Q_INVOKABLE void go(const QString &view);

signals:
    void currentViewChanged();

private:
    QString m_currentView = QStringLiteral("home");
};

} // namespace ModelBench

#endif // MODELBENCH_NAVIGATION_H
