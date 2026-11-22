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

    QString installRoot() const;
    QString enginePath() const;
    bool isInstalled() const;
    QString installedVersion() const;

    // Fetch latest release metadata from GitHub and select an asset URL.
    void checkLatest();
    // Download + extract the selected asset. Emits progress.
    void install();
    void uninstall();

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
