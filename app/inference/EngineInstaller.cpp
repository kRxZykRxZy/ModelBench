#include "EngineInstaller.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QUrl>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QProcess>
#include <QFileInfo>
#include <QDebug>

namespace ModelFit {

EngineInstaller::EngineInstaller(QObject *parent) : QObject(parent) {}

QString EngineInstaller::installRoot() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/engine";
}

QString EngineInstaller::enginePath() const {
    return installRoot() + "/llama.cpp";
}

bool EngineInstaller::isInstalled() const {
    QFileInfo info(enginePath() + "/libllama.dll");
#ifdef Q_OS_MACOS
    info.setFile(enginePath() + "/libllama.dylib");
#endif
#ifdef Q_OS_LINUX
    info.setFile(enginePath() + "/libllama.so");
#endif
    return info.exists();
}

QString EngineInstaller::installedVersion() const {
    QFile f(installRoot() + "/version.txt");
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

void EngineInstaller::checkLatest() {
    QNetworkRequest req(QUrl("https://api.github.com/repos/ggml-org/llama.cpp/releases/latest"));
    req.setHeader(QNetworkRequest::UserAgentHeader, "ModelFit/0.1");
    req.setRawHeader("Accept", "application/vnd.github+json");
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onLatestReply(reply); });
}

void EngineInstaller::onLatestReply(QNetworkReply *reply) {
    if (reply->error() != QNetworkReply::NoError) {
        emit latestCheckFailed(reply->errorString());
        reply->deleteLater();
        return;
    }
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    reply->deleteLater();
    if (!doc.isObject()) { emit latestCheckFailed("Invalid release metadata"); return; }
    QJsonObject o = doc.object();
    m_latestVersion = o.value("tag_name").toString();
    QJsonArray assets = o.value("assets").toArray();
    m_latestAssetUrl = selectAsset(assets);
    m_latestAssetName = m_latestAssetUrl.section('/', -1);
    if (m_latestAssetUrl.isEmpty()) {
        emit latestCheckFailed("No compatible release asset found for this platform");
        return;
    }
    emit latestChecked(m_latestVersion, m_latestAssetUrl, m_latestAssetName);
}

QString EngineInstaller::selectAsset(const QJsonArray &assets) const {
    QString osToken, archToken;
#ifdef Q_OS_WIN
    osToken = "win";
#elif defined(Q_OS_MACOS)
    osToken = "macos";
#else
    osToken = "linux";
#endif
#if defined(Q_PROCESSOR_X86_64)
    archToken = "x64";
#elif defined(Q_PROCESSOR_ARM_64)
    archToken = "arm64";
#endif

    // Prefer a CPU build for broad compatibility.
    for (const auto &v : assets) {
        QJsonObject a = v.toObject();
        QString name = a.value("name").toString().toLower();
        if (name.contains(osToken) && name.contains(archToken) &&
            (name.contains("cpu") || name.contains("bin")) &&
            name.endsWith(".zip")) {
            return a.value("browser_download_url").toString();
        }
    }
    // Fallback: any zip matching os+arch.
    for (const auto &v : assets) {
        QJsonObject a = v.toObject();
        QString name = a.value("name").toString().toLower();
        if (name.contains(osToken) && name.contains(archToken) && name.endsWith(".zip")) {
            return a.value("browser_download_url").toString();
        }
    }
    return {};
}

void EngineInstaller::install() {
    if (m_latestAssetUrl.isEmpty()) { checkLatest(); return; }
    QDir().mkpath(installRoot());
    m_downloadDest = installRoot() + "/" + m_latestAssetName;
    QNetworkRequest req{QUrl(m_latestAssetUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "ModelFit/0.1");
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::downloadProgress, this, &EngineInstaller::onDownloadProgress);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onDownloadFinished(reply); });
}

void EngineInstaller::onDownloadProgress(qint64 rx, qint64 total) {
    emit progress(rx, total);
}

void EngineInstaller::onDownloadFinished(QNetworkReply *reply) {
    if (reply->error() != QNetworkReply::NoError) {
        emit failed(reply->errorString());
        reply->deleteLater();
        return;
    }
    QFile f(m_downloadDest);
    if (!f.open(QIODevice::WriteOnly)) {
        emit failed("Cannot write download file");
        reply->deleteLater();
        return;
    }
    f.write(reply->readAll());
    f.close();
    reply->deleteLater();

    // Extract.
    QDir().mkpath(enginePath());
    if (!extractArchive(m_downloadDest, enginePath())) {
        emit failed("Failed to extract engine archive");
        return;
    }
    // Write version.
    QFile vf(installRoot() + "/version.txt");
    if (vf.open(QIODevice::WriteOnly)) { vf.write(m_latestVersion.toUtf8()); vf.close(); }
    emit statusChanged();
    emit finished(m_latestVersion, enginePath());
}

bool EngineInstaller::extractArchive(const QString &archivePath, const QString &destDir) const {
#ifdef Q_OS_WIN
    // Use PowerShell Expand-Archive.
    QString cmd = "powershell";
    QStringList args;
    args << "-NoProfile" << "-Command"
         << QString("Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force")
                .arg(archivePath, destDir);
    QProcess p;
    p.start(cmd, args);
    if (!p.waitForFinished(120000)) return false;
    return p.exitCode() == 0;
#else
    // Use unzip.
    QProcess p;
    p.start("unzip", {"-o", archivePath, "-d", destDir});
    if (!p.waitForFinished(120000)) return false;
    return p.exitCode() == 0;
#endif
}

void EngineInstaller::uninstall() {
    QDir(installRoot()).removeRecursively();
    emit statusChanged();
}

} // namespace ModelFit
