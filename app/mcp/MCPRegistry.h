#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QList>
#include <QNetworkAccessManager>

namespace ModelBench {

struct MCPServerListing {
    QString id;
    QString name;
    QString description;
    QString publisher;
    QString repository;
    QString transport; // stdio | sse | streamable-http
    QString installCommand;
    QStringList requiredEnv;
};

class IMCPRegistryProvider : public QObject {
    Q_OBJECT
public:
    explicit IMCPRegistryProvider(QObject *parent = nullptr) : QObject(parent) {}
    virtual void searchServers(const QString &query) = 0;
    virtual void fetchServerDetails(const QString &serverId) = 0;
signals:
    void searchFinished(const QList<MCPServerListing> &results);
    void searchFailed(const QString &error);
    void detailsFinished(const QString &serverId, const QJsonObject &details);
    void detailsFailed(const QString &serverId, const QString &error);
};

// Official MCP Registry provider (no API key required for public read).
class OfficialMCPRegistryProvider : public IMCPRegistryProvider {
    Q_OBJECT
public:
    explicit OfficialMCPRegistryProvider(QObject *parent = nullptr);
    void setBaseUrl(const QString &url);
    void searchServers(const QString &query) override;
    void fetchServerDetails(const QString &serverId) override;
private:
    QNetworkRequest makeRequest(const QString &url) const;
    QNetworkAccessManager *m_nam = nullptr;
    QString m_baseUrl = "https://registry.modelcontextprotocol.io";
};

// Third-party registry provider (BYOK). Endpoint and API key must be
// configured explicitly; no unverified third-party API is ever contacted
// by default.
class ThirdPartyMCPRegistryProvider : public IMCPRegistryProvider {
    Q_OBJECT
public:
    explicit ThirdPartyMCPRegistryProvider(QObject *parent = nullptr);

    void setBaseUrl(const QString &url);
    void setApiKey(const QString &apiKey);
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    void searchServers(const QString &query) override;
    void fetchServerDetails(const QString &serverId) override;

signals:
    void configurationChanged();

private:
    QNetworkRequest authedRequest(const QString &url) const;
    QNetworkAccessManager *m_nam = nullptr;
    QString m_baseUrl;
    QString m_apiKey;
    bool m_enabled = false;
};

} // namespace ModelBench
