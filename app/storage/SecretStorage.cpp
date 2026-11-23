#include "SecretStorage.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#include <dpapi.h>
#pragma comment(lib, "crypt32.lib")
#endif

#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>

namespace {

class WindowsSecretStorage : public ModelBench::ISecretStorage {
public:
    bool store(const QString &service, const QString &account, const QByteArray &secret) override {
#ifdef Q_OS_WIN
        DATA_BLOB in; in.cbData = secret.size(); in.pbData = (BYTE*)secret.data();
        DATA_BLOB out;
        if (!CryptProtectData(&in, (service + "/" + account).toStdWString().c_str(), nullptr, nullptr, nullptr, 0, &out))
            return false;
        QByteArray blob((const char*)out.pbData, out.cbData);
        LocalFree(out.pbData);
        // Persist to file in app data (encrypted blob; not plaintext)
        QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/secrets/" + service + "_" + account + ".bin";
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(blob);
        f.close();
        return true;
#else
        Q_UNUSED(service); Q_UNUSED(account); Q_UNUSED(secret);
        return false;
#endif
    }
    QByteArray load(const QString &service, const QString &account) override {
#ifdef Q_OS_WIN
        QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/secrets/" + service + "_" + account + ".bin";
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        QByteArray blob = f.readAll();
        f.close();
        DATA_BLOB in; in.cbData = blob.size(); in.pbData = (BYTE*)blob.data();
        DATA_BLOB out;
        if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) return {};
        QByteArray res((const char*)out.pbData, out.cbData);
        LocalFree(out.pbData);
        return res;
#else
        Q_UNUSED(service); Q_UNUSED(account);
        return {};
#endif
    }
    bool remove(const QString &service, const QString &account) override {
        QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/secrets/" + service + "_" + account + ".bin";
        return QFile::remove(path);
    }
};

} // namespace

namespace ModelBench {
ISecretStorage *createSecretStorage() {
    return new WindowsSecretStorage();
}
} // namespace ModelBench
