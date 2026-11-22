#pragma once

#include <QString>
#include <QJsonObject>

namespace ModelFit {

struct ModelSpec {
    QString id;
    QString name;
    double paramsBillion = 0;       // parameters in billions
    QString quantization;           // e.g. Q4_K_M
    int contextLength = 4096;
    bool supportsToolCalling = false;
    double fileSizeGB = 0;
};

struct HardwareSpec {
    int cpuCores = 0;
    int cpuThreads = 0;
    quint64 ramBytes = 0;
    quint64 vramBytes = 0;
    QString gpuVendor;
    bool hasGpu = false;
};

struct ModelFitResult {
    bool fitsRam = false;
    bool fitsVram = false;
    bool recommended = false;
    double estimatedRamGB = 0;
    double estimatedVramGB = 0;
    double estimatedTokPerSec = 0;   // predicted, not measured
    QString backend;                // CPU / CUDA / Metal / Vulkan
    QString reason;                 // human-readable explanation
    int recommendedGpuLayers = 0;
    int recommendedThreads = 0;
    int recommendedContext = 0;
};

class PerformanceEstimator {
public:
    // Predict whether a model can run on the given hardware and with what settings.
    static ModelFitResult estimate(const HardwareSpec &hw, const ModelSpec &model);

    // Rank a list of models by fit quality (best first).
    static QList<ModelFitResult> rank(const HardwareSpec &hw, const QList<ModelSpec> &models);

    // Rough bytes-per-parameter for a quantization.
    static double bytesPerParam(const QString &quantization);
};

} // namespace ModelFit
