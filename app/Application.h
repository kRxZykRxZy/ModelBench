#pragma once

#include <QObject>
#include <QQmlApplicationEngine>
#include <QString>
#include "inference/EngineInstaller.h"
#include "inference/LlamaBackend.h"
#include "core/Theme.h"
#include "core/AccountManager.h"
#include "core/Updater.h"
#include "chat/ConversationManager.h"
#include "models/HuggingFaceClient.h"
#include "models/ModelDownloader.h"
#include "billing/SubscriptionManager.h"
#include "mcp/MCPServerManager.h"
#include "skills/SkillsManager.h"
#include "agent/AgentOrchestrator.h"
#include "tools/TerminalTool.h"
#include "tools/CodeRunner.h"
#include "tools/FileWatcher.h"
#include "tools/SystemIntegration.h"

namespace ModelBench {

class Application : public QObject {
    Q_OBJECT
public:
    explicit Application(QObject *parent = nullptr);
    ~Application() override;

    bool initialize();
    void registerQmlTypes(QQmlApplicationEngine &engine);

    QString dataDirectory() const;
    QString modelsDirectory() const;
    QString logsDirectory() const;

    EngineInstaller *engineInstaller() const { return m_engineInstaller; }
    Theme *theme() const { return m_theme; }
    AccountManager *account() const { return m_account; }
    InferenceManager *inference() const { return m_inference; }
    ConversationManager *conversations() const { return m_conversations; }
    HuggingFaceClient *hfClient() const { return m_hfClient; }
    ModelDownloader *downloader() const { return m_downloader; }
    SubscriptionManager *subscription() const { return m_subscription; }
    MCPServerManager *mcpServers() const { return m_mcpServers; }
    SkillsManager *skills() const { return m_skills; }
    Updater *updater() const { return m_updater; }
    Q_INVOKABLE QVariantMap hardwareInfo() const;

    // Agent & tooling accessors
    AgentOrchestrator *agent() const { return m_agent; }
    TerminalSession *terminalSession() const { return m_terminalSession; }
    CodeRunner *codeRunner() const { return m_codeRunner; }
    FileWatcher *fileWatcher() const { return m_fileWatcher; }
    Clipboard *clipboard() const { return m_clipboard; }
    Notifier *notifier() const { return m_notifier; }

signals:
    void initialized();

private:
    bool initPaths();
    bool initDatabase();
    bool initLogging();

    QString m_dataDir;
    QString m_modelsDir;
    QString m_logsDir;
    EngineInstaller *m_engineInstaller = nullptr;
    Theme *m_theme = nullptr;
    AccountManager *m_account = nullptr;
    InferenceManager *m_inference = nullptr;
    ConversationManager *m_conversations = nullptr;
    HuggingFaceClient *m_hfClient = nullptr;
    ModelDownloader *m_downloader = nullptr;
    SubscriptionManager *m_subscription = nullptr;
    MCPServerManager *m_mcpServers = nullptr;
    SkillsManager *m_skills = nullptr;
    Updater *m_updater = nullptr;
    AgentOrchestrator *m_agent = nullptr;
    TerminalSession *m_terminalSession = nullptr;
    CodeRunner *m_codeRunner = nullptr;
    FileWatcher *m_fileWatcher = nullptr;
    Clipboard *m_clipboard = nullptr;
    Notifier *m_notifier = nullptr;
};

} // namespace ModelBench
