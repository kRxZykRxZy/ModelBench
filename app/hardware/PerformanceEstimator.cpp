#include "PerformanceEstimator.h"

#include <QtMath>
#include <algorithm>

namespace ModelFit {

double PerformanceEstimator::bytesPerParam(const QString &quantization) {
    QString q = quantization.toUpper();
    if (q.contains("F16")) return 2.0;
    if (q.contains("Q8")) return 1.0;
    if (q.contains("Q6")) return 0.8;
    if (q.contains("Q5")) return 0.65;
    if (q.contains("Q4")) return 0.5;
    if (q.contains("Q3")) return 0.4;
    if (q.contains("Q2")) return 0.3;
    return 0.5; // default Q4-ish
}

ModelFitResult PerformanceEstimator::estimate(const HardwareSpec &hw, const ModelSpec &model) {
    ModelFitResult r;
    double bpp = bytesPerParam(model.quantization);
    double params = model.paramsBillion * 1e9;
    double weightBytes = params * bpp;
    // KV cache estimate: ~2 * layers * hidden * context * 2 bytes (rough)
    double kvBytes = (model.contextLength * 4096.0 * 2.0); // very rough
    r.estimatedRamGB = (weightBytes + kvBytes) / (1024.0 * 1024.0 * 1024.0);
    r.estimatedVramGB = r.estimatedRamGB * 0.9; // if fully offloaded

    quint64 ramBytes = hw.ramBytes > 0 ? hw.ramBytes : (8ULL * 1024 * 1024 * 1024);
    double ramGB = ramBytes / (1024.0 * 1024.0 * 1024.0);
    double vramGB = hw.vramBytes / (1024.0 * 1024.0 * 1024.0);

    // Determine backend
    if (hw.hasGpu && hw.gpuVendor == "NVIDIA") r.backend = "CUDA";
    else if (hw.hasGpu && hw.gpuVendor.contains("Apple", Qt::CaseInsensitive)) r.backend = "Metal";
    else if (hw.hasGpu) r.backend = "Vulkan";
    else r.backend = "CPU";

    r.fitsRam = r.estimatedRamGB <= ramGB * 0.85; // leave headroom
    r.fitsVram = (vramGB > 0) ? (r.estimatedRamGB <= vramGB * 0.9) : false;

    // Recommended threads
    r.recommendedThreads = qMax(1, qMin(hw.cpuThreads > 0 ? hw.cpuThreads : 4, 8));
    // Context: don't exceed model default or 8192 for low RAM
    r.recommendedContext = qMin(model.contextLength, (ramGB < 16) ? 4096 : 8192);
    // GPU layers: if fits in VRAM, offload all; else partial based on VRAM
    if (r.fitsVram && vramGB > 0) r.recommendedGpuLayers = 999;
    else if (vramGB > 0) r.recommendedGpuLayers = qMax(0, (int)((vramGB * 0.9) / (r.estimatedRamGB / 30.0)));
    else r.recommendedGpuLayers = 0;

    r.recommended = r.fitsRam;

    // Predicted tok/s (rough heuristic, clearly labeled as prediction)
    double base = (r.backend == "CPU") ? 5.0 : 25.0;
    base *= (double)r.recommendedThreads / 4.0;
    base /= qMax(1.0, model.paramsBillion / 7.0);
    if (model.quantization.contains("Q4", Qt::CaseInsensitive)) base *= 1.0;
    if (model.quantization.contains("Q8", Qt::CaseInsensitive)) base *= 0.7;
    r.estimatedTokPerSec = base;

    if (!r.fitsRam) r.reason = "Insufficient RAM: needs ~" + QString::number(r.estimatedRamGB, 'f', 1) + " GB, have " + QString::number(ramGB, 'f', 1) + " GB.";
    else if (r.fitsVram) r.reason = "Fits in VRAM; full GPU offload recommended.";
    else if (vramGB > 0) r.reason = "Partial GPU offload possible; remainder on CPU.";
    else r.reason = "CPU-only inference.";

    return r;
}

QList<ModelFitResult> PerformanceEstimator::rank(const HardwareSpec &hw, const QList<ModelSpec> &models) {
    QList<ModelFitResult> results;
    for (const auto &m : models) results << estimate(hw, m);
    std::sort(results.begin(), results.end(), [](const ModelFitResult &a, const ModelFitResult &b) {
        if (a.recommended != b.recommended) return a.recommended > b.recommended;
        return a.estimatedTokPerSec > b.estimatedTokPerSec;
    });
    return results;
}

} // namespace ModelFit
