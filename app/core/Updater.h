#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace ModelBench {

// Signed, verified, HTTPS-only application auto-updater.
//
// Flow:
//   1. checkForUpdates() fetches a JSON manifest over HTTPS.
//   2. If a newer version is available, updateAvailable becomes true.
//   3. downloadUpdate() fetches the platform asset over HTTPS into a temp file,
//      reporting progress, then verifies SHA-256 and an Ed25519/RSA signature
//      made with the embedded public key.
//   4. installUpdate() launches the verified installer/package and quits the app.
//
// Manifest schema (served over HTTPS):
// {
//   "version": "1.2.3",
//   "notes": "release notes (markdown)",
//   "pub_date": "2026-01-01T00:00:00Z",
//   "platforms": {
//     "windows-x86_64": { "url": "https://...", "sha256": "...", "signature": "base64", "size": 12345 },
//     "macos-arm64":    { "url": "https://...", "sha256": "...", "signature": "base64", "size": 12345 },
//     "macos-x86_64":   { "url": "https://...", "sha256": "...", "signature": "base64", "size": 12345 }
//   }
// }
//
// Security properties:
//   - All network requests require an https:// URL; http:// (and other schemes)
//     are rejected.
//   - The downloaded artifact's SHA-256 must match the manifest value.
//   - The manifest's signature field must verify against the embedded public
//     key (OpenSSL required at build time; without it verification fails closed).
//   - The public key is compiled into the binary; replace the placeholder in
//     Updater.cpp with the real release signing key before shipping.

class Updater : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateAvailableChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY latestVersionChanged)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY releaseNotesChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(UpdateState state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    enum UpdateState { Idle, Checking, Available, Downloading, Verifying, Ready, Installing, Error };
    Q_ENUM(UpdateState)

    explicit Updater(QObject *parent = nullptr);

    QString currentVersion() const { return m_currentVersion; }
    bool updateAvailable() const { return m_updateAvailable; }
    QString latestVersion() const { return m_latestVersion; }
    QString releaseNotes() const { return m_releaseNotes; }
    double progress() const { return m_progress; }
    UpdateState state() const { return m_state; }
    QString lastError() const { return m_lastError; }

    // Override the manifest URL (must be https://). Defaults to the project's
    // GitHub releases manifest.
    Q_INVOKABLE void setManifestUrl(const QUrl &url);

    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void downloadUpdate();
    Q_INVOKABLE void installUpdate();
    Q_INVOKABLE void cancel();

    // Embedded release-signing public key (SPKI PEM). Replace before release.
    static const char *publicKeyPem();

signals:
    void updateAvailableChanged();
    void latestVersionChanged();
    void releaseNotesChanged();
    void progressChanged();
    void stateChanged();
    void lastErrorChanged();
    void checkFailed(const QString &error);
    void downloadFailed(const QString &error);
    void verificationFailed(const QString &error);
    void installFailed(const QString &error);
    void updateReady(const QString &path);

private:
    void setState(UpdateState s);
    void setError(const QString &msg);
    bool isHttps(const QUrl &url) const;
    QString platformKey() const;
    static bool compareVersions(const QString &a, const QString &b); // returns true if a < b
    bool verifySha256(const QString &filePath, const QString &expected) const;
    bool verifySignature(const QByteArray &data, const QString &signatureB64) const;

    void onManifestReply(QNetworkReply *reply);
    void onDownloadProgress(qint64 rx, qint64 total);
    void onDownloadFinished(QNetworkReply *reply);

    QNetworkAccessManager m_nam;
    QUrl m_manifestUrl;
    QString m_currentVersion;
    QString m_latestVersion;
    QString m_releaseNotes;
    QString m_downloadUrl;
    QString m_expectedSha256;
    QString m_expectedSignature;
    qint64 m_expectedSize = -1;
    QString m_downloadPath;
    QNetworkReply *m_activeReply = nullptr;
    bool m_updateAvailable = false;
    double m_progress = 0.0;
    UpdateState m_state = Idle;
    QString m_lastError;
};

} // namespace ModelBench