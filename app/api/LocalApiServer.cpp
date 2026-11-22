#include "LocalApiServer.h"

#include <QTcpServer>
#include <QTcpSocket>

namespace ModelFit {

LocalApiServer::LocalApiServer(QObject *parent) : QObject(parent) {}

bool LocalApiServer::start(const QString &host, quint16 port) {
    // Boundary: full OpenAI-compatible handler to be wired to InferenceManager.
    m_endpoint = QString("http://%1:%2/v1").arg(host).arg(port);
    m_running = true;
    emit started(m_endpoint);
    return true;
}

void LocalApiServer::stop() {
    m_running = false;
    emit stopped();
}

} // namespace ModelFit
