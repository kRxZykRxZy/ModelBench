#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace ModelFit {

class EngineInstaller : public QObject {
    Q_OBJECT
public:
    explicit EngineInstaller(QObject *parent = nullptr);

    Q_INVOKABLE bool isInstalled() const;
    Q_INVOKABLE QString installedVersion() const;
    QString installRoot() const;
    QString enginePath() const;
    Q_INVOKABLE QString getEnginePath() const { return enginePath(); }
    Q_INVOKABLE bool getIsInstalled() const { return isInstalled(); }
    Q_INVOKABLE QString getInstalledVersion() const { return installedVersion(); }

    // Fetch latest release metadata from GitHub and select an asset URL.
    Q_INVOKABLE void checkLatest();
    // Download + extract the selected asset. Emits progress.
    Q_INVOKABLE void install();
    Q_INVOKABLE void uninstall();

signals:
    void latestChecked(const QString &version, const QString &assetUrl, const QString &assetName);
    void latestCheckFailed(const QString &error);
    void progress(qint64 received, qint64 total);
    void finished(const QString &version, const QString &path);
    void failed(const QString &error);
    void statusChanged();

private:
    void onLatestReply(QNetworkReply *reply);
    void onDownloadProgress(qint64 rx, qint64 total);
    void onDownloadFinished(QNetworkReply *reply);
    QString selectAsset(const QJsonArray &assets) const;
    bool extractArchive(const QString &archivePath, const QString &destDir) const;

    QNetworkAccessManager m_nam;
    QString m_latestVersion;
    QString m_latestAssetUrl;
    QString m_latestAssetName;
    QString m_downloadDest;
};

} // namespace ModelFit
