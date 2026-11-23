#include "Application.h"

#include "core/Paths.h"
#include "core/Logging.h"
#include "core/Updater.h"
#include "storage/Database.h"
#include "inference/EngineInstaller.h"
#include "billing/SubscriptionManager.h"
#include "hardware/HardwareDetector.h"

#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <QQmlContext>

namespace ModelBench {

Application::Application(QObject *parent) : QObject(parent) {}

Application::~Application() = default;

bool Application::initialize() {
    if (!initPaths()) return false;
    if (!initLogging()) return false;
    if (!initDatabase()) return false;
    m_engineInstaller = new EngineInstaller(this);
    m_theme = new Theme(this);
    m_account = new AccountManager(this);
    m_inference = new InferenceManager(this);
    m_conversations = new ConversationManager(this);
    m_hfClient = new HuggingFaceClient(this);
    m_downloader = new ModelDownloader(this);
    m_mcpServers = new MCPServerManager(this);
    m_skills = new SkillsManager(this);
    m_subscription = new SubscriptionManager(this);
    m_updater = new Updater(this);
    m_agent = new AgentOrchestrator(nullptr, nullptr, this);
    m_terminalSession = new TerminalSession(this);
    m_codeRunner = new CodeRunner(this);
    m_fileWatcher = new FileWatcher(this);
    m_clipboard = new Clipboard(this);
    m_notifier = new Notifier(this);
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
    qInfo() << "ModelBench" << "data:" << m_dataDir;
    return true;
}

bool Application::initDatabase() {
    Database db(m_dataDir + "/modelfit.db");
    return db.openAndMigrate();
}

void Application::registerQmlTypes(QQmlApplicationEngine &engine) {
    QQmlContext *ctx = engine.rootContext();
    ctx->setContextProperty("engineInstaller", m_engineInstaller);
    ctx->setContextProperty("Theme", m_theme);
    ctx->setContextProperty("account", m_account);
    ctx->setContextProperty("inference", m_inference);
    ctx->setContextProperty("conversations", m_conversations);
    ctx->setContextProperty("hfClient", m_hfClient);
    ctx->setContextProperty("downloader", m_downloader);
    ctx->setContextProperty("mcpServers", m_mcpServers);
    ctx->setContextProperty("skills", m_skills);
    ctx->setContextProperty("subscription", m_subscription);
    ctx->setContextProperty("updater", m_updater);
    // Credits balance is provided by the backend; expose a placeholder until a
    // dedicated credits service is wired up so QML references resolve safely.
    ctx->setContextProperty("credits", nullptr);
    ctx->setContextProperty("agent", m_agent);
    ctx->setContextProperty("terminalSession", m_terminalSession);
    ctx->setContextProperty("codeRunner", m_codeRunner);
    ctx->setContextProperty("fileWatcher", m_fileWatcher);
    ctx->setContextProperty("clipboard", m_clipboard);
    ctx->setContextProperty("notifier", m_notifier);
    ctx->setContextProperty("app", this);
}

QString Application::dataDirectory() const { return m_dataDir; }
QString Application::modelsDirectory() const { return m_modelsDir; }
QString Application::logsDirectory() const { return m_logsDir; }

QVariantMap Application::hardwareInfo() const {
    return HardwareDetector::toJson(HardwareDetector::detect()).toVariantMap();
}

} // namespace ModelBench
