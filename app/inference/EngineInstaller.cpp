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
#include <QRegularExpression>
#include <QDebug>

namespace ModelBench {

EngineInstaller::EngineInstaller(QObject *parent) : QObject(parent) {}

QString EngineInstaller::installRoot() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/engine";
}

QString EngineInstaller::enginePath() const {
    return installRoot() + "/llama.cpp";
}

bool EngineInstaller::isInstalled() const {
    // Official Windows binaries ship llama.dll (not libllama.dll).
    QFileInfo info(enginePath() + "/llama.dll");
    if (!info.exists()) info.setFile(enginePath() + "/libllama.dll");
#ifdef Q_OS_MACOS
    info.setFile(enginePath() + "/libllama.dylib");
    if (!info.exists()) info.setFile(enginePath() + "/llama.dylib");
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
    QNetworkRequest req{QUrl("https://api.github.com/repos/ggml-org/llama.cpp/releases/latest")};
    req.setHeader(QNetworkRequest::UserAgentHeader, "ModelBench/0.1");
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
    QJsonArray assets = o.value("assets").toArray();

    // The latest stable release (v1.0.0) contains only a nightly-tag.txt.
    // Binaries live in the nightly release whose tag is in the release body.
    QString nightlyTag;
    QString body = o.value("body").toString();
    QRegularExpression re(QStringLiteral("releases/tag/(b\\d+)"));
    auto m = re.match(body);
    if (m.hasMatch()) nightlyTag = m.captured(1);

    if (nightlyTag.isEmpty()) {
        // Fall back to the selected asset from this release if present.
        m_latestVersion = o.value("tag_name").toString();
        m_latestAssetUrl = selectAsset(assets);
        m_latestAssetName = m_latestAssetUrl.section('/', -1);
        if (m_latestAssetUrl.isEmpty()) {
            emit latestCheckFailed("No compatible release asset found for this platform");
            return;
        }
        emit latestChecked(m_latestVersion, m_latestAssetUrl, m_latestAssetName);
        return;
    }

    // Fetch the nightly release that actually has platform binaries.
    QNetworkRequest req{QUrl("https://api.github.com/repos/ggml-org/llama.cpp/releases/tags/" + nightlyTag)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "ModelBench/0.1");
    req.setRawHeader("Accept", "application/vnd.github+json");
    QNetworkReply *nr = m_nam.get(req);
    connect(nr, &QNetworkReply::finished, this, [this, nr, nightlyTag]() {
        if (nr->error() != QNetworkReply::NoError) {
            emit latestCheckFailed(nr->errorString());
            nr->deleteLater();
            return;
        }
        QJsonDocument ndoc = QJsonDocument::fromJson(nr->readAll());
        nr->deleteLater();
        QJsonArray nassets = ndoc.object().value("assets").toArray();
        m_latestVersion = nightlyTag;
        m_latestAssetUrl = selectAsset(nassets);
        m_latestAssetName = m_latestAssetUrl.section('/', -1);
        if (m_latestAssetUrl.isEmpty()) {
            emit latestCheckFailed("No compatible release asset found for this platform");
            return;
        }
        emit latestChecked(m_latestVersion, m_latestAssetUrl, m_latestAssetName);
    });
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

    // Prefer a CPU build for broad compatibility (zip on Windows, tar.gz elsewhere).
    const QStringList exts = (osToken == "win") ? QStringList{"zip"} : QStringList{"tar.gz", "zip"};
    for (const QString &ext : exts) {
        for (const auto &v : assets) {
            QJsonObject a = v.toObject();
            QString name = a.value("name").toString().toLower();
            if (name.contains(osToken) && name.contains(archToken) &&
                name.contains("cpu") && name.endsWith("." + ext)) {
                return a.value("browser_download_url").toString();
            }
        }
    }
    // Fallback: any archive matching os+arch.
    for (const QString &ext : exts) {
        for (const auto &v : assets) {
            QJsonObject a = v.toObject();
            QString name = a.value("name").toString().toLower();
            if (name.contains(osToken) && name.contains(archToken) && name.endsWith("." + ext)) {
                return a.value("browser_download_url").toString();
            }
        }
    }
    return {};
}

void EngineInstaller::install() {
    if (m_latestAssetUrl.isEmpty()) { checkLatest(); return; }
    QDir().mkpath(installRoot());
    m_downloadDest = installRoot() + "/" + m_latestAssetName;
    QNetworkRequest req{QUrl(m_latestAssetUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "ModelBench/0.1");
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
    // Use tar for .tar.gz, unzip for .zip.
    QProcess p;
    if (archivePath.endsWith(".tar.gz"))
        p.start("tar", {"-xzf", archivePath, "-C", destDir});
    else
        p.start("unzip", {"-o", archivePath, "-d", destDir});
    if (!p.waitForFinished(120000)) return false;
    return p.exitCode() == 0;
#endif
}

void EngineInstaller::uninstall() {
    QDir(installRoot()).removeRecursively();
    emit statusChanged();
}

} // namespace ModelBench
