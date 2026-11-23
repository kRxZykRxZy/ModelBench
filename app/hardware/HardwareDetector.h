#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>

namespace ModelBench {

struct HardwareInfo {
    QString cpuManufacturer;
    QString cpuModel;
    int cpuCores = 0;
    int cpuThreads = 0;
    QString architecture;
    quint64 ramBytes = 0;
    QString gpuVendor;
    QString gpuModel;
    quint64 vramBytes = 0;
    QString gpuDriver;
    int gpuCount = 0;
    bool gpuIntegrated = false;
    QStringList supportedBackends;
};

class HardwareDetector {
public:
    static HardwareInfo detect();
    static QJsonObject toJson(const HardwareInfo &info);
};

} // namespace ModelBench
