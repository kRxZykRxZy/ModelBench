#include <QtTest>
#include "app/hardware/HardwareDetector.h"

using namespace ModelBench;

class TestHardware : public QObject {
    Q_OBJECT
private slots:
    void detect_doesNotCrash() {
        auto info = HardwareDetector::detect();
        QVERIFY(!info.architecture.isEmpty());
    }
    void toJson_hasKeys() {
        auto info = HardwareDetector::detect();
        auto o = HardwareDetector::toJson(info);
        QVERIFY(o.contains("cpuCores"));
        QVERIFY(o.contains("ramBytes"));
    }
};

QTEST_MAIN(TestHardware)
#include "test_hardware.moc"
