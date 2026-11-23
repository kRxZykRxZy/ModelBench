#pragma once

#include <QObject>
#include <QQmlApplicationEngine>
#include <QString>
#include "inference/EngineInstaller.h"
#include "inference/LlamaBackend.h"
#include "core/Theme.h"
#include "core/AccountManager.h"
#include "chat/ConversationManager.h"
#include "models/HuggingFaceClient.h"
#include "models/ModelDownloader.h"
#include "billing/SubscriptionManager.h"
#include "mcp/MCPServerManager.h"
#include "skills/SkillsManager.h"

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
    Q_INVOKABLE QVariantMap hardwareInfo() const;

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
};

} // namespace ModelBench
