#include "BackendDetector.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QLibrary>

#include <algorithm>
#include <mutex>

#if defined(HAVE_LLAMA_CPP) && HAVE_LLAMA_CPP
#include <ggml-backend.h>
#endif

namespace ModelFit {

namespace {

QString normalizeBackendName(const QString &raw)
{
    const QString trimmed = raw.trimmed();
    const QString lower = trimmed.toLower();
    if (lower.contains(QLatin1String("cuda"))) {
        return QStringLiteral("CUDA");
    }
    if (lower.contains(QLatin1String("vulkan"))) {
        return QStringLiteral("Vulkan");
    }
    if (lower.contains(QLatin1String("metal"))) {
        return QStringLiteral("Metal");
    }
    if (lower.contains(QLatin1String("cpu"))) {
        return QStringLiteral("CPU");
    }
    return trimmed;
}

bool isKnownBackend(const QString &name)
{
    return name == QLatin1String("CPU") || name == QLatin1String("CUDA")
        || name == QLatin1String("Vulkan") || name == QLatin1String("Metal");
}

QString backendForDevice(const QString &deviceName, const QString &description)
{
    const QString fromName = normalizeBackendName(deviceName);
    if (isKnownBackend(fromName)) {
        return fromName;
    }
    const QString fromDescription = normalizeBackendName(description);
    if (isKnownBackend(fromDescription)) {
        return fromDescription;
    }
    return {};
}

int backendRank(const QString &name)
{
    if (name == QLatin1String("CPU")) {
        return 0;
    }
    if (name == QLatin1String("CUDA")) {
        return 1;
    }
    if (name == QLatin1String("Vulkan")) {
        return 2;
    }
    if (name == QLatin1String("Metal")) {
        return 3;
    }
    return 10;
}

QStringList libraryCandidates(const QString &baseName)
{
#if defined(Q_OS_WIN)
    return {baseName, baseName + QStringLiteral(".dll")};
#elif defined(Q_OS_MACOS)
    return {baseName,
            QStringLiteral("lib") + baseName + QStringLiteral(".dylib"),
            baseName + QStringLiteral(".dylib")};
#else
    return {baseName,
            QStringLiteral("lib") + baseName + QStringLiteral(".so"),
            baseName + QStringLiteral(".so")};
#endif
}

QStringList searchDirectories()
{
    QStringList dirs;
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        dirs << appDir;
        dirs << QDir(appDir).filePath(QStringLiteral("ggml"));
        dirs << QDir(appDir).filePath(QStringLiteral("backends"));
        dirs << QDir(appDir).filePath(QStringLiteral("../lib"));
        dirs << QDir(appDir).filePath(QStringLiteral("../lib/ggml"));
    }
    dirs << QDir::currentPath();
    return dirs;
}

} // namespace

bool BackendDetector::probeLibrary(const QString &baseName, QString *pathOut)
{
    if (baseName.isEmpty()) {
        return false;
    }

    const QStringList candidates = libraryCandidates(baseName);
    const QStringList directories = searchDirectories();

    for (const QString &dir : directories) {
        for (const QString &candidate : candidates) {
            const QString fullPath = QDir(dir).filePath(candidate);
            if (!QFileInfo::exists(fullPath)) {
                continue;
            }
            QLibrary library(fullPath);
            if (library.load()) {
                if (pathOut) {
                    *pathOut = library.fileName();
                }
                library.unload();
                return true;
            }
        }
    }

    // Fall back to the platform's library search path.
    for (const QString &candidate : candidates) {
        QLibrary library(candidate);
        if (library.load()) {
            if (pathOut) {
                *pathOut = library.fileName();
            }
            library.unload();
            return true;
        }
    }

    return false;
}

QList<BackendAvailability> BackendDetector::detect()
{
    QList<BackendAvailability> result;

    auto upsert = [&result](const BackendAvailability &incoming) {
        for (BackendAvailability &existing : result) {
            if (existing.name.compare(incoming.name, Qt::CaseInsensitive) == 0) {
                existing.available = existing.available || incoming.available;
                existing.gpu = existing.gpu || incoming.gpu;
                if (existing.libraryPath.isEmpty()) {
                    existing.libraryPath = incoming.libraryPath;
                }
                if (!incoming.detail.isEmpty()
                    && !existing.detail.contains(incoming.detail)) {
                    if (existing.detail.isEmpty()) {
                        existing.detail = incoming.detail;
                    } else {
                        existing.detail += QStringLiteral("; ") + incoming.detail;
                    }
                }
                return;
            }
        }
        result.append(incoming);
    };

    upsert(BackendAvailability{QStringLiteral("CPU"), true, false, {},
                               QStringLiteral("Always available")});

#if defined(HAVE_LLAMA_CPP) && HAVE_LLAMA_CPP
    static std::once_flag loadBackends;
    std::call_once(loadBackends, [] { ggml_backend_load_all(); });

    for (size_t i = 0; i < ggml_backend_reg_count(); ++i) {
        ggml_backend_reg_t reg = ggml_backend_reg_get(i);
        if (!reg) {
            continue;
        }
        const char *rawName = ggml_backend_reg_name(reg);
        const QString name = normalizeBackendName(QString::fromUtf8(rawName ? rawName : ""));
        if (name.isEmpty()) {
            continue;
        }
        upsert(BackendAvailability{name, true, name != QLatin1String("CPU"), {},
                                   QStringLiteral("Registered by ggml")});
    }

    for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
        ggml_backend_dev_t device = ggml_backend_dev_get(i);
        if (!device) {
            continue;
        }
        const char *rawName = ggml_backend_dev_name(device);
        const char *rawDescription = ggml_backend_dev_description(device);
        const QString deviceName = QString::fromUtf8(rawName ? rawName : "");
        const QString description = QString::fromUtf8(rawDescription ? rawDescription : "");
        const QString name = backendForDevice(deviceName, description);
        if (name.isEmpty()) {
            continue;
        }

        const ggml_backend_dev_type type = ggml_backend_dev_type(device);
        const bool gpu = type == GGML_BACKEND_DEVICE_TYPE_GPU
            || type == GGML_BACKEND_DEVICE_TYPE_IGPU
            || type == GGML_BACKEND_DEVICE_TYPE_ACCEL;

        size_t freeBytes = 0;
        size_t totalBytes = 0;
        ggml_backend_dev_memory(device, &freeBytes, &totalBytes);

        QString detail = description;
        if (totalBytes > 0) {
            const double gib = static_cast<double>(totalBytes) / (1024.0 * 1024.0 * 1024.0);
            detail += QStringLiteral(" (%1 GiB)").arg(gib, 0, 'f', 1);
        }
        upsert(BackendAvailability{name, true, gpu, {}, detail});
    }
#endif // HAVE_LLAMA_CPP

    struct Probe {
        const char *name;
        const char *library;
        bool gpu;
    };
    const Probe probes[] = {
        {"CUDA", "ggml-cuda", true},
        {"Vulkan", "ggml-vulkan", true},
        {"Metal", "ggml-metal", true},
    };
    for (const Probe &probe : probes) {
        QString path;
        if (probeLibrary(QString::fromLatin1(probe.library), &path)) {
            upsert(BackendAvailability{QString::fromLatin1(probe.name), true, probe.gpu, path,
                                       QStringLiteral("Backend library found")});
        }
    }

#ifdef Q_OS_MACOS
    upsert(BackendAvailability{QStringLiteral("Metal"), true, true, {},
                               QStringLiteral("Metal is built into macOS")});
#endif

    std::stable_sort(result.begin(), result.end(),
                     [](const BackendAvailability &a, const BackendAvailability &b) {
                         const int rankA = backendRank(a.name);
                         const int rankB = backendRank(b.name);
                         if (rankA != rankB) {
                             return rankA < rankB;
                         }
                         return a.name < b.name;
                     });

    return result;
}

QStringList BackendDetector::availableBackendNames()
{
    QStringList names;
    for (const BackendAvailability &backend : detect()) {
        if (backend.available) {
            names << backend.name;
        }
    }
    if (!names.contains(QStringLiteral("CPU"))) {
        names.prepend(QStringLiteral("CPU"));
    }
    return names;
}

QJsonObject BackendDetector::toJson(const QList<BackendAvailability> &backends)
{
    QJsonArray array;
    for (const BackendAvailability &backend : backends) {
        QJsonObject item;
        item[QStringLiteral("name")] = backend.name;
        item[QStringLiteral("available")] = backend.available;
        item[QStringLiteral("gpu")] = backend.gpu;
        item[QStringLiteral("libraryPath")] = backend.libraryPath;
        item[QStringLiteral("detail")] = backend.detail;
        array.append(item);
    }

    QJsonObject root;
    root[QStringLiteral("backends")] = array;
    return root;
}

} // namespace ModelFit
