#include "Updater.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QUrl>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>
#include <QProcess>
#include <QDebug>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QDesktopServices>

#ifdef HAVE_OPENSSL
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#endif

namespace ModelBench {

static const char *kDefaultManifestUrl =
    "https://github.com/kRxZykRxZy/ModelBench/releases/latest/download/update-manifest.json";

Updater::Updater(QObject *parent) : QObject(parent) {
    m_currentVersion = QCoreApplication::applicationVersion();
    if (m_currentVersion.isEmpty()) m_currentVersion = QStringLiteral("0.1.0");
    m_manifestUrl = QUrl(QString::fromLatin1(kDefaultManifestUrl));
}

void Updater::setManifestUrl(const QUrl &url) {
    if (!isHttps(url)) {
        setError(QStringLiteral("Manifest URL must use HTTPS"));
        return;
    }
    m_manifestUrl = url;
}

bool Updater::isHttps(const QUrl &url) const {
    return url.isValid() && url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0
           && !url.host().isEmpty();
}

QString Updater::platformKey() const {
    QString osToken, archToken;
#ifdef Q_OS_WIN
    osToken = QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    osToken = QStringLiteral("macos");
#else
    osToken = QStringLiteral("linux");
#endif
#if defined(Q_PROCESSOR_X86_64)
    archToken = QStringLiteral("x86_64");
#elif defined(Q_PROCESSOR_ARM_64)
    archToken = QStringLiteral("arm64");
#else
    archToken = QStringLiteral("x86_64");
#endif
    return osToken + QStringLiteral("-") + archToken;
}

void Updater::setState(UpdateState s) {
    if (m_state == s) return;
    m_state = s;
    emit stateChanged();
}

void Updater::setError(const QString &msg) {
    m_lastError = msg;
    setState(Error);
    emit lastErrorChanged();
}

bool Updater::compareVersions(const QString &a, const QString &b) {
    // returns true if a < b
    QRegularExpression re(QStringLiteral("(\\d+)"));
    auto va = a.split('.');
    auto vb = b.split('.');
    for (int i = 0; i < qMax(va.size(), vb.size()); ++i) {
        int na = (i < va.size()) ? va[i].toInt() : 0;
        int nb = (i < vb.size()) ? vb[i].toInt() : 0;
        if (na != nb) return na < nb;
    }
    return false;
}

void Updater::checkForUpdates() {
    if (!isHttps(m_manifestUrl)) {
        setError(QStringLiteral("Manifest URL must use HTTPS"));
        emit checkFailed(m_lastError);
        return;
    }
    setState(Checking);
    m_lastError.clear();
    emit lastErrorChanged();

    QNetworkRequest req(m_manifestUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("ModelBench/%1").arg(m_currentVersion));
    req.setRawHeader("Accept", "application/json");
    m_activeReply = m_nam.get(req);
    connect(m_activeReply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *r = m_activeReply;
        m_activeReply = nullptr;
        onManifestReply(r);
    });
}

void Updater::onManifestReply(QNetworkReply *reply) {
    if (!reply) return;
    if (reply->error() != QNetworkReply::NoError) {
        setError(reply->errorString());
        emit checkFailed(m_lastError);
        reply->deleteLater();
        return;
    }
    // Enforce HTTPS on final URL (redirects).
    if (!isHttps(reply->url())) {
        setError(QStringLiteral("Manifest redirected to non-HTTPS URL"));
        emit checkFailed(m_lastError);
        reply->deleteLater();
        return;
    }
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        setError(QStringLiteral("Invalid manifest JSON"));
        emit checkFailed(m_lastError);
        return;
    }
    QJsonObject o = doc.object();
    const QString version = o.value(QStringLiteral("version")).toString();
    if (version.isEmpty()) {
        setError(QStringLiteral("Manifest missing version"));
        emit checkFailed(m_lastError);
        return;
    }
    const QString notes = o.value(QStringLiteral("notes")).toString();
    QJsonObject platforms = o.value(QStringLiteral("platforms")).toObject();
    QJsonObject plat = platforms.value(platformKey()).toObject();
    if (plat.isEmpty()) {
        setError(QStringLiteral("No update asset for this platform"));
        emit checkFailed(m_lastError);
        return;
    }
    const QString url = plat.value(QStringLiteral("url")).toString();
    if (!isHttps(QUrl(url))) {
        setError(QStringLiteral("Update URL must use HTTPS"));
        emit checkFailed(m_lastError);
        return;
    }

    m_latestVersion = version;
    m_releaseNotes = notes;
    m_downloadUrl = url;
    m_expectedSha256 = plat.value(QStringLiteral("sha256")).toString().toLower();
    m_expectedSignature = plat.value(QStringLiteral("signature")).toString();
    m_expectedSize = plat.value(QStringLiteral("size")).toVariant().toLongLong();
    emit latestVersionChanged();
    emit releaseNotesChanged();

    const bool newer = compareVersions(m_currentVersion, m_latestVersion);
    m_updateAvailable = newer;
    emit updateAvailableChanged();
    if (newer) {
        setState(Available);
    } else {
        setState(Idle);
    }
}

void Updater::downloadUpdate() {
    if (!m_updateAvailable || m_downloadUrl.isEmpty()) {
        setError(QStringLiteral("No update available"));
        emit downloadFailed(m_lastError);
        return;
    }
    if (!isHttps(QUrl(m_downloadUrl))) {
        setError(QStringLiteral("Update URL must use HTTPS"));
        emit downloadFailed(m_lastError);
        return;
    }
    setState(Downloading);
    m_progress = 0.0;
    emit progressChanged();

    const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir().mkpath(base + QStringLiteral("/ModelBench-updates"));
    const QString fileName = QUrl(m_downloadUrl).fileName();
    m_downloadPath = base + QStringLiteral("/ModelBench-updates/") + (fileName.isEmpty() ? QStringLiteral("update.bin") : fileName);

    QNetworkRequest req{QUrl(m_downloadUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("ModelBench/%1").arg(m_currentVersion));
    m_activeReply = m_nam.get(req);
    connect(m_activeReply, &QNetworkReply::downloadProgress, this, &Updater::onDownloadProgress);
    connect(m_activeReply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *r = m_activeReply;
        m_activeReply = nullptr;
        onDownloadFinished(r);
    });
}

void Updater::onDownloadProgress(qint64 rx, qint64 total) {
    if (total > 0) {
        m_progress = static_cast<double>(rx) / static_cast<double>(total);
        emit progressChanged();
    }
}

void Updater::onDownloadFinished(QNetworkReply *reply) {
    if (!reply) return;
    if (reply->error() != QNetworkReply::NoError) {
        setError(reply->errorString());
        emit downloadFailed(m_lastError);
        reply->deleteLater();
        return;
    }
    if (!isHttps(reply->url())) {
        setError(QStringLiteral("Download redirected to non-HTTPS URL"));
        emit downloadFailed(m_lastError);
        reply->deleteLater();
        return;
    }
    QFile f(m_downloadPath);
    if (!f.open(QIODevice::WriteOnly)) {
        setError(QStringLiteral("Cannot write download file"));
        emit downloadFailed(m_lastError);
        reply->deleteLater();
        return;
    }
    f.write(reply->readAll());
    f.close();
    reply->deleteLater();

    setState(Verifying);
    // Size check.
    if (m_expectedSize > 0) {
        QFileInfo fi(m_downloadPath);
        if (fi.size() != m_expectedSize) {
            setError(QStringLiteral("Downloaded file size mismatch"));
            emit verificationFailed(m_lastError);
            QFile::remove(m_downloadPath);
            return;
        }
    }
    // SHA-256 check.
    if (!m_expectedSha256.isEmpty() && !verifySha256(m_downloadPath, m_expectedSha256)) {
        setError(QStringLiteral("SHA-256 verification failed"));
        emit verificationFailed(m_lastError);
        QFile::remove(m_downloadPath);
        return;
    }
    // Signature check.
    QFile df(m_downloadPath);
    if (!df.open(QIODevice::ReadOnly)) {
        setError(QStringLiteral("Cannot read downloaded file"));
        emit verificationFailed(m_lastError);
        return;
    }
    const QByteArray data = df.readAll();
    df.close();
    if (!m_expectedSignature.isEmpty()) {
        if (!verifySignature(data, m_expectedSignature)) {
            setError(QStringLiteral("Signature verification failed"));
            emit verificationFailed(m_lastError);
            QFile::remove(m_downloadPath);
            return;
        }
    } else {
        // Unsigned updates are not allowed.
        setError(QStringLiteral("Manifest missing signature"));
        emit verificationFailed(m_lastError);
        QFile::remove(m_downloadPath);
        return;
    }

    m_progress = 1.0;
    emit progressChanged();
    setState(Ready);
    emit updateReady(m_downloadPath);
}

bool Updater::verifySha256(const QString &filePath, const QString &expected) const {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f)) return false;
    f.close();
    const QString actual = QString::fromLatin1(hash.result().toHex());
    return actual.compare(expected, Qt::CaseInsensitive) == 0;
}

bool Updater::verifySignature(const QByteArray &data, const QString &signatureB64) const {
#ifdef HAVE_OPENSSL
    const QByteArray sig = QByteArray::fromBase64(signatureB64.toLatin1());
    if (sig.isEmpty()) return false;

    const char *keyPem = publicKeyPem();
    BIO *bio = BIO_new_mem_buf(keyPem, static_cast<int>(qstrlen(keyPem)));
    if (!bio) return false;
    EVP_PKEY *pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) return false;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    bool ok = false;
    if (ctx) {
        if (EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) == 1) {
            if (EVP_DigestVerifyUpdate(ctx, data.constData(), static_cast<size_t>(data.size())) == 1) {
                int rc = EVP_DigestVerifyFinal(ctx,
                    reinterpret_cast<const unsigned char *>(sig.constData()),
                    static_cast<size_t>(sig.size()));
                ok = (rc == 1);
            }
        }
        EVP_MD_CTX_free(ctx);
    }
    EVP_PKEY_free(pkey);
    return ok;
#else
    Q_UNUSED(data);
    Q_UNUSED(signatureB64);
    return false; // fail closed when OpenSSL is unavailable
#endif
}

void Updater::installUpdate() {
    if (m_state != Ready || m_downloadPath.isEmpty()) {
        setError(QStringLiteral("No verified update ready to install"));
        emit installFailed(m_lastError);
        return;
    }
    setState(Installing);
    const QString path = m_downloadPath;
    bool launched = false;
#ifdef Q_OS_WIN
    // Launch installer; support .exe/.msi.
    if (path.endsWith(QStringLiteral(".msi"), Qt::CaseInsensitive)) {
        launched = QProcess::startDetached(QStringLiteral("msiexec"), {QStringLiteral("/i"), path});
    } else {
        launched = QProcess::startDetached(path, {});
    }
#elif defined(Q_OS_MACOS)
    if (path.endsWith(QStringLiteral(".dmg"), Qt::CaseInsensitive)) {
        launched = QProcess::startDetached(QStringLiteral("open"), {path});
    } else if (path.endsWith(QStringLiteral(".pkg"), Qt::CaseInsensitive)) {
        launched = QProcess::startDetached(QStringLiteral("open"), {path});
    } else {
        launched = QProcess::startDetached(QStringLiteral("open"), {path});
    }
#else
    launched = QProcess::startDetached(path, {});
#endif
    if (!launched) {
        setError(QStringLiteral("Failed to launch installer"));
        emit installFailed(m_lastError);
        return;
    }
    // Quit so the installer can replace files.
    QCoreApplication::quit();
}

void Updater::cancel() {
    if (m_activeReply) {
        m_activeReply->abort();
        m_activeReply->deleteLater();
        m_activeReply = nullptr;
    }
    if (m_state == Downloading) {
        QFile::remove(m_downloadPath);
    }
    m_progress = 0.0;
    emit progressChanged();
    if (m_state != Idle) setState(Idle);
}

const char *Updater::publicKeyPem() {
    // PLACEHOLDER: replace with the real release signing public key (SPKI PEM).
    // Generate: openssl genpkey -algorithm Ed25519 -out private.pem
    //           openssl pkey -in private.pem -pubout -out public.pem
    // Then paste public.pem contents here.
    return "-----BEGIN PUBLIC KEY-----\n"
           "MCowBQYDK2VwAyEA" /* placeholder */ "\n"
           "-----END PUBLIC KEY-----\n";
}

} // namespace ModelBench