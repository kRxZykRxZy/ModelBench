#include "HardwareDetector.h"

#include "BackendDetector.h"

#include <QFile>
#include <QJsonArray>
#include <QMetaType>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QSysInfo>
#include <QThread>

#include <algorithm>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace ModelBench {

namespace {

QString runProcess(const QString &program, const QStringList &arguments, int timeoutMs = 3000)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(500);
        return {};
    }
    return QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
}

quint64 parseMemorySize(const QString &text)
{
    static const QRegularExpression re(
        QStringLiteral("([0-9]+(?:\\.[0-9]+)?)\\s*(GB|GiB|MB|MiB|KB|KiB)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(text);
    if (!match.hasMatch()) {
        return 0;
    }
    const double value = match.captured(1).toDouble();
    const QString unit = match.captured(2).toLower();
    if (unit.startsWith(QLatin1Char('g'))) {
        return static_cast<quint64>(value * 1024.0 * 1024.0 * 1024.0);
    }
    if (unit.startsWith(QLatin1Char('m'))) {
        return static_cast<quint64>(value * 1024.0 * 1024.0);
    }
    return static_cast<quint64>(value * 1024.0);
}

QString normalizeCpuVendor(const QString &vendor, const QString &model)
{
    const QString combined = vendor + QLatin1Char(' ') + model;
    if (combined.contains(QStringLiteral("GenuineIntel"), Qt::CaseInsensitive)
        || combined.contains(QStringLiteral("Intel"), Qt::CaseInsensitive)) {
        return QStringLiteral("Intel");
    }
    if (combined.contains(QStringLiteral("AuthenticAMD"), Qt::CaseInsensitive)
        || combined.contains(QStringLiteral("AMD"), Qt::CaseInsensitive)) {
        return QStringLiteral("AMD");
    }
    if (combined.contains(QStringLiteral("Apple"), Qt::CaseInsensitive)) {
        return QStringLiteral("Apple");
    }
    if (combined.contains(QStringLiteral("ARM"), Qt::CaseInsensitive)
        || combined.contains(QStringLiteral("aarch64"), Qt::CaseInsensitive)) {
        return QStringLiteral("ARM");
    }
    if (!vendor.trimmed().isEmpty()) {
        return vendor.trimmed();
    }
    return model.section(QLatin1Char(' '), 0, 0);
}

QString vendorForGpu(const QString &model)
{
    if (model.contains(QStringLiteral("NVIDIA"), Qt::CaseInsensitive)
        || model.contains(QStringLiteral("GeForce"), Qt::CaseInsensitive)
        || model.contains(QStringLiteral("Quadro"), Qt::CaseInsensitive)
        || model.contains(QStringLiteral("RTX"), Qt::CaseInsensitive)) {
        return QStringLiteral("NVIDIA");
    }
    if (model.contains(QStringLiteral("AMD"), Qt::CaseInsensitive)
        || model.contains(QStringLiteral("Radeon"), Qt::CaseInsensitive)) {
        return QStringLiteral("AMD");
    }
    if (model.contains(QStringLiteral("Intel"), Qt::CaseInsensitive)) {
        return QStringLiteral("Intel");
    }
    if (model.contains(QStringLiteral("Apple"), Qt::CaseInsensitive)) {
        return QStringLiteral("Apple");
    }
    return {};
}

int gpuVendorRank(const QString &model)
{
    const QString vendor = vendorForGpu(model);
    if (vendor == QLatin1String("NVIDIA")) {
        return 0;
    }
    if (vendor == QLatin1String("AMD")) {
        return 1;
    }
    if (vendor == QLatin1String("Apple")) {
        return 2;
    }
    if (vendor == QLatin1String("Intel")) {
        return 3;
    }
    return 4;
}

void detectNvidiaSmi(HardwareInfo &info)
{
    const QStringList arguments = {
        QStringLiteral("--query-gpu=name,memory.total,driver_version"),
        QStringLiteral("--format=csv,noheader,nounits"),
    };
    QString output = runProcess(QStringLiteral("nvidia-smi"), arguments);
    if (output.isEmpty()) {
        output = runProcess(QStringLiteral("C:/Windows/System32/nvidia-smi.exe"), arguments);
    }
    if (output.isEmpty()) {
        output = runProcess(
            QStringLiteral("C:/Program Files/NVIDIA Corporation/NVSMI/nvidia-smi.exe"), arguments);
    }
    if (output.isEmpty()) {
        return;
    }

    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.isEmpty()) {
        return;
    }
    const QStringList parts = lines.first().split(QLatin1Char(','));
    if (parts.size() < 2) {
        return;
    }

    bool ok = false;
    const quint64 mib = parts.at(1).trimmed().toULongLong(&ok);
    info.gpuModel = parts.at(0).trimmed();
    if (ok && mib > 0) {
        info.vramBytes = mib * 1024ULL * 1024ULL;
    }
    if (parts.size() >= 3) {
        info.gpuDriver = parts.at(2).trimmed();
    }
    info.gpuVendor = QStringLiteral("NVIDIA");
    info.gpuCount = std::max(1, static_cast<int>(lines.size()));
}

#if defined(Q_OS_WIN)
void detectWindowsGpu(HardwareInfo &info)
{
    detectNvidiaSmi(info);

    struct Candidate {
        QString name;
        QString driver;
        quint64 vram = 0;
    };
    QList<Candidate> candidates;

    QSettings display(
        QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}"),
        QSettings::NativeFormat);
    for (const QString &group : display.childGroups()) {
        const QString name = display.value(group + QStringLiteral("/DriverDesc")).toString().trimmed();
        if (name.isEmpty()
            || name.contains(QStringLiteral("Basic Display"), Qt::CaseInsensitive)
            || name.contains(QStringLiteral("Remote Display"), Qt::CaseInsensitive)) {
            continue;
        }
        quint64 vram = display.value(group + QStringLiteral("/HardwareInformation.qwMemorySize")).toULongLong();
        if (vram == 0) {
            const QVariant raw = display.value(group + QStringLiteral("/HardwareInformation.MemorySize"));
            if (raw.typeId() == QMetaType::QByteArray) {
                const QByteArray bytes = raw.toByteArray();
                if (bytes.size() >= 4) {
                    vram = static_cast<quint64>(static_cast<quint8>(bytes[0]))
                        | (static_cast<quint64>(static_cast<quint8>(bytes[1])) << 8)
                        | (static_cast<quint64>(static_cast<quint8>(bytes[2])) << 16)
                        | (static_cast<quint64>(static_cast<quint8>(bytes[3])) << 24);
                }
            }
        }
        Candidate candidate;
        candidate.name = name;
        candidate.driver = display.value(group + QStringLiteral("/DriverVersion")).toString().trimmed();
        candidate.vram = vram;
        candidates.append(candidate);
    }

    if (!candidates.isEmpty()) {
        std::stable_sort(candidates.begin(), candidates.end(),
                         [](const Candidate &a, const Candidate &b) {
                             if (a.vram != b.vram) {
                                 return a.vram > b.vram;
                             }
                             return gpuVendorRank(a.name) < gpuVendorRank(b.name);
                         });
        const Candidate &best = candidates.first();
        if (info.gpuModel.isEmpty() || best.vram > info.vramBytes) {
            info.gpuModel = best.name;
            info.vramBytes = best.vram;
            info.gpuDriver = best.driver;
            info.gpuVendor = vendorForGpu(best.name);
        }
        info.gpuCount = std::max(info.gpuCount, static_cast<int>(candidates.size()));
    }

    if (info.gpuModel.isEmpty()) {
        const QString output = runProcess(
            QStringLiteral("wmic"),
            {QStringLiteral("path"), QStringLiteral("win32_VideoController"),
             QStringLiteral("get"), QStringLiteral("Name,AdapterRAM"), QStringLiteral("/value")});
        for (const QString &line : output.split(QLatin1Char('\n'))) {
            const QString trimmed = line.trimmed();
            if (trimmed.startsWith(QStringLiteral("Name=")) && info.gpuModel.isEmpty()) {
                info.gpuModel = trimmed.mid(5).trimmed();
            } else if (trimmed.startsWith(QStringLiteral("AdapterRAM=")) && info.vramBytes == 0) {
                info.vramBytes = trimmed.mid(11).trimmed().toULongLong();
            }
        }
        if (!info.gpuModel.isEmpty()) {
            info.gpuVendor = vendorForGpu(info.gpuModel);
            info.gpuCount = std::max(info.gpuCount, 1);
        }
    }

    if (info.gpuVendor.isEmpty()) {
        info.gpuVendor = QStringLiteral("Unknown");
    }
    info.gpuIntegrated = info.gpuModel.contains(QStringLiteral("Intel"), Qt::CaseInsensitive)
        || (info.vramBytes > 0 && info.vramBytes < 512ULL * 1024ULL * 1024ULL);
}
#endif // Q_OS_WIN

void detectLinux(HardwareInfo &info)
{
    QFile cpuInfo(QStringLiteral("/proc/cpuinfo"));
    if (cpuInfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        int processors = 0;
        while (!cpuInfo.atEnd()) {
            const QString line = QString::fromLocal8Bit(cpuInfo.readLine()).trimmed();
            if (line.startsWith(QStringLiteral("processor"))) {
                ++processors;
            } else if (line.startsWith(QStringLiteral("model name"))) {
                if (info.cpuModel.isEmpty()) {
                    info.cpuModel = line.section(QLatin1Char(':'), 1).trimmed();
                }
            } else if (line.startsWith(QStringLiteral("vendor_id"))) {
                if (info.cpuManufacturer.isEmpty()) {
                    info.cpuManufacturer = line.section(QLatin1Char(':'), 1).trimmed();
                }
            }
        }
        info.cpuThreads = processors;
        info.cpuCores = processors;
    }

    QFile memInfo(QStringLiteral("/proc/meminfo"));
    if (memInfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!memInfo.atEnd()) {
            const QString line = QString::fromLocal8Bit(memInfo.readLine()).trimmed();
            if (line.startsWith(QStringLiteral("MemTotal:"))) {
                const QString value = line.section(QLatin1Char(':'), 1).trimmed();
                const QString number = value.section(QLatin1Char(' '), 0, 0);
                info.ramBytes = number.toULongLong() * 1024ULL;
                break;
            }
        }
    }

    detectNvidiaSmi(info);
    if (info.gpuModel.isEmpty()) {
        const QString output = runProcess(QStringLiteral("lspci"), {});
        for (const QString &line : output.split(QLatin1Char('\n'))) {
            if (line.contains(QStringLiteral("VGA compatible controller"), Qt::CaseInsensitive)
                || line.contains(QStringLiteral("3D controller"), Qt::CaseInsensitive)
                || line.contains(QStringLiteral("Display controller"), Qt::CaseInsensitive)) {
                const QString model = line.section(QStringLiteral(": "), 1).trimmed();
                if (!model.isEmpty()) {
                    info.gpuModel = model;
                    info.gpuVendor = vendorForGpu(model);
                    info.gpuCount = std::max(info.gpuCount, 1);
                    break;
                }
            }
        }
    }
}

void detectMac(HardwareInfo &info)
{
    info.cpuModel = runProcess(QStringLiteral("sysctl"), {QStringLiteral("-n"), QStringLiteral("machdep.cpu.brand_string")});
    if (info.cpuModel.isEmpty()) {
        info.cpuModel = runProcess(QStringLiteral("sysctl"), {QStringLiteral("-n"), QStringLiteral("hw.model")});
    }
    info.cpuManufacturer = normalizeCpuVendor({}, info.cpuModel);
    info.ramBytes = runProcess(QStringLiteral("sysctl"), {QStringLiteral("-n"), QStringLiteral("hw.memsize")}).toULongLong();
    info.cpuThreads = runProcess(QStringLiteral("sysctl"), {QStringLiteral("-n"), QStringLiteral("hw.logicalcpu")}).toInt();
    info.cpuCores = runProcess(QStringLiteral("sysctl"), {QStringLiteral("-n"), QStringLiteral("hw.physicalcpu")}).toInt();

    const QString output = runProcess(
        QStringLiteral("system_profiler"), {QStringLiteral("SPDisplaysDataType")}, 8000);
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QStringLiteral("Chipset Model:"))) {
            info.gpuModel = trimmed.section(QLatin1Char(':'), 1).trimmed();
        } else if (trimmed.startsWith(QStringLiteral("Vendor:"))) {
            info.gpuVendor = trimmed.section(QLatin1Char(':'), 1).trimmed();
        } else if (trimmed.startsWith(QStringLiteral("VRAM (Total):"))
                   || trimmed.startsWith(QStringLiteral("VRAM (Dynamic, Max):"))) {
            info.vramBytes = parseMemorySize(trimmed.section(QLatin1Char(':'), 1));
        }
    }
    if (!info.gpuModel.isEmpty()) {
        info.gpuCount = std::max(info.gpuCount, 1);
        if (info.gpuVendor.isEmpty()) {
            info.gpuVendor = vendorForGpu(info.gpuModel);
        }
        info.gpuIntegrated = info.gpuModel.contains(QStringLiteral("Apple"), Qt::CaseInsensitive);
    }
}

} // namespace

HardwareInfo HardwareDetector::detect()
{
    HardwareInfo info;
    info.architecture = QSysInfo::currentCpuArchitecture();

#if defined(Q_OS_WIN)
    QSettings cpu(
        QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"),
        QSettings::NativeFormat);
    info.cpuModel = cpu.value(QStringLiteral("ProcessorNameString")).toString().trimmed();
    info.cpuManufacturer = cpu.value(QStringLiteral("VendorIdentifier")).toString().trimmed();
    info.cpuCores = cpu.value(QStringLiteral("NumberOfCores")).toInt();
    info.cpuThreads = cpu.value(QStringLiteral("NumberOfLogicalProcessors")).toInt();

    MEMORYSTATUSEX memoryStatus{};
    memoryStatus.dwLength = sizeof(memoryStatus);
    if (GlobalMemoryStatusEx(&memoryStatus)) {
        info.ramBytes = memoryStatus.ullTotalPhys;
    }

    detectWindowsGpu(info);
#elif defined(Q_OS_MACOS)
    detectMac(info);
#elif defined(Q_OS_LINUX)
    detectLinux(info);
#endif

    if (info.cpuThreads <= 0) {
        info.cpuThreads = QThread::idealThreadCount();
    }
    if (info.cpuThreads <= 0) {
        info.cpuThreads = 1;
    }
    if (info.cpuCores <= 0) {
        info.cpuCores = info.cpuThreads;
    }
    if (info.cpuManufacturer.isEmpty()) {
        info.cpuManufacturer = normalizeCpuVendor({}, info.cpuModel);
    }
    if (info.cpuManufacturer.isEmpty()) {
        info.cpuManufacturer = QStringLiteral("Unknown");
    }
    if (info.gpuVendor.isEmpty()) {
        info.gpuVendor = QStringLiteral("Unknown");
    }

    info.supportedBackends = BackendDetector::availableBackendNames();
    return info;
}

QJsonObject HardwareDetector::toJson(const HardwareInfo &info)
{
    QJsonObject o;
    o[QStringLiteral("cpuManufacturer")] = info.cpuManufacturer;
    o[QStringLiteral("cpuModel")] = info.cpuModel;
    o[QStringLiteral("cpuCores")] = info.cpuCores;
    o[QStringLiteral("cpuThreads")] = info.cpuThreads;
    o[QStringLiteral("architecture")] = info.architecture;
    o[QStringLiteral("ramBytes")] = static_cast<double>(info.ramBytes);
    o[QStringLiteral("gpuVendor")] = info.gpuVendor;
    o[QStringLiteral("gpuModel")] = info.gpuModel;
    o[QStringLiteral("vramBytes")] = static_cast<double>(info.vramBytes);
    o[QStringLiteral("gpuDriver")] = info.gpuDriver;
    o[QStringLiteral("gpuCount")] = info.gpuCount;
    o[QStringLiteral("gpuIntegrated")] = info.gpuIntegrated;

    QJsonArray backends;
    for (const QString &backend : info.supportedBackends) {
        backends.append(backend);
    }
    o[QStringLiteral("supportedBackends")] = backends;
    return o;
}

} // namespace ModelBench
