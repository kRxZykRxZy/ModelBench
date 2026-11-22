#include "Application.h"

#include "core/Paths.h"
#include "core/Logging.h"
#include "storage/Database.h"
#include "inference/EngineInstaller.h"

#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <QQmlContext>

namespace ModelFit {

Application::Application(QObject *parent) : QObject(parent) {}

Application::~Application() = default;

bool Application::initialize() {
    if (!initPaths()) return false;
    if (!initLogging()) return false;
    if (!initDatabase()) return false;
    m_engineInstaller = new EngineInstaller(this);
    emit initialized();
    return true;
}

bool Application::initPaths() {
    m_dataDir = Paths::dataDirectory();
    m_modelsDir = Paths::modelsDirectory();
    m_logsDir = Paths::logsDirectory();
    QDir().mkpath(m_dataDir);
    QDir().mkpath(m_modelsDir);
    QDir().mkpath(m_logsDir);
    return true;
}

bool Application::initLogging() {
    Logging::install(m_logsDir);
    qInfo() << "ModelFit" << "data:" << m_dataDir;
    return true;
}

bool Application::initDatabase() {
    Database db(m_dataDir + "/modelfit.db");
    return db.openAndMigrate();
}

void Application::registerQmlTypes(QQmlApplicationEngine &engine) {
    engine.rootContext()->setContextProperty("engineInstaller", m_engineInstaller);
}

QString Application::dataDirectory() const { return m_dataDir; }
QString Application::modelsDirectory() const { return m_modelsDir; }
QString Application::logsDirectory() const { return m_logsDir; }

} // namespace ModelFit
