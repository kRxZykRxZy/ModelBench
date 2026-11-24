#include "Navigation.h"

namespace ModelBench {

Navigation::Navigation(QObject *parent) : QObject(parent) {}

QString Navigation::currentView() const { return m_currentView; }

void Navigation::setCurrentView(const QString &view)
{
    if (m_currentView == view)
        return;
    m_currentView = view;
    emit currentViewChanged();
}

void Navigation::go(const QString &view)
{
    setCurrentView(view);
}

} // namespace ModelBench
