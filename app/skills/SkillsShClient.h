#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>
#include <QString>

namespace ModelBench {

struct SkillListing {
    QString id;
    QString name;
    QString description;
    QString publisher;
    QString repository;
    QString version;
    qint64 installs = 0;
};

// Client for the skills.sh skills directory. The Vercel OIDC token is
// optional and short-lived; endpoints that require it respond 401/403 and
// surface a clear error message.
class SkillsShClient : public QObject {
    Q_OBJECT
public:
    explicit SkillsShClient(QObject *parent = nullptr);
    void setBaseUrl(const QString &url);
    void setVercelOidcToken(const QString &token); // optional, short-lived

    void search(const QString &query, int limit = 20);
    void fetchDetails(const QString &skillId);
    // Retrieve the SKILL.md content of a skill.
    void fetchSkillContent(const QString &skillId);

signals:
    void searchFinished(const QList<SkillListing> &results);
    void searchFailed(const QString &error);
    void detailsFinished(const QString &skillId, const QJsonObject &details);
    void detailsFailed(const QString &skillId, const QString &error);
    void skillContentFinished(const QString &skillId, const QString &content);
    void skillContentFailed(const QString &skillId, const QString &error);

private:
    QNetworkRequest makeRequest(const QString &url) const;
    QString httpFailure(QNetworkReply *reply,
                        const QString &context) const;
    QNetworkAccessManager m_nam;
    QString m_baseUrl = "https://skills.sh";
    QString m_token;
};

} // namespace ModelBench
