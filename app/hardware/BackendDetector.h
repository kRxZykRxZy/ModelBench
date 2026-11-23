#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace ModelBench {

struct BackendAvailability {
    QString name;          // CPU, CUDA, Vulkan, Metal, ...
    bool available = false;
    bool gpu = false;
    QString libraryPath;   // set when a backend shared library was found
    QString detail;        // human-readable information
};

/**
 * Detects which ggml compute backends can be used.
 *
 * Detection combines two sources:
 *  - ggml's runtime registry/device list (when built with HAVE_LLAMA_CPP)
 *  - probing for ggml-cuda / ggml-vulkan / ggml-metal shared libraries
 *
 * CPU is always reported as available.
 */
class BackendDetector {
public:
    static QList<BackendAvailability> detect();
    static QStringList availableBackendNames();
    static QJsonObject toJson(const QList<BackendAvailability> &backends);

    /// Try to load a ggml backend library by base name (e.g. "ggml-cuda").
    static bool probeLibrary(const QString &baseName, QString *pathOut = nullptr);
};

} // namespace ModelBench
