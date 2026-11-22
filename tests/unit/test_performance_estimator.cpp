#include <QtTest>
#include "app/hardware/PerformanceEstimator.h"

using namespace ModelFit;

class TestPerformanceEstimator : public QObject {
    Q_OBJECT
private slots:
    void bytesPerParam_q4() {
        QCOMPARE(PerformanceEstimator::bytesPerParam("Q4_K_M"), 0.5);
    }
    void estimate_fitsRam() {
        HardwareSpec hw; hw.cpuThreads = 8; hw.ramBytes = 16ULL*1024*1024*1024; hw.hasGpu = false;
        ModelSpec m; m.paramsBillion = 7; m.quantization = "Q4_K_M"; m.contextLength = 4096;
        auto r = PerformanceEstimator::estimate(hw, m);
        QVERIFY(r.fitsRam);
        QVERIFY(r.estimatedRamGB > 0);
        QCOMPARE(r.backend, QString("CPU"));
    }
    void estimate_tooBig() {
        HardwareSpec hw; hw.ramBytes = 4ULL*1024*1024*1024; hw.hasGpu = false;
        ModelSpec m; m.paramsBillion = 70; m.quantization = "Q4_K_M";
        auto r = PerformanceEstimator::estimate(hw, m);
        QVERIFY(!r.fitsRam);
    }
};

QTEST_MAIN(TestPerformanceEstimator)
#include "test_performance_estimator.moc"
