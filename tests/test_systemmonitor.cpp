#include <QtTest>
#include "core/SystemMonitor.h"
class TestSystemMonitor:public QObject {
    Q_OBJECT
private slots:
    void lifecycle(){for(int i=0;i<4;++i){SystemMonitor monitor;monitor.start();}}
    void cpu(){auto c=parseCpuCounters("cpu 10 20 30 40 50 60 70 80 900 900\n");QVERIFY(c.valid);QCOMPARE(c.total,quint64(360));QCOMPARE(c.idle,quint64(90));QVERIFY(!parseCpuCounters("cpu x 0 0 0").valid);}
    void ram(){auto m=parseSystemMemory("MemTotal: 8192 kB\nMemAvailable: 2048 kB\nMemFree: 512 kB\n");QCOMPARE(m["usedMiB"].toDouble(),6.0);QCOMPARE(m["totalMiB"].toDouble(),8.0);QVERIFY(parseSystemMemory("MemTotal: 10 kB").isEmpty());}
    void gpu(){auto g=parseNvidiaMetrics("0, NVIDIA RTX, 0000:01:00.0, 67, 2534, 16380\n1, Other GPU, 0000:02:00.0, N/A, N/A, N/A\n");QCOMPARE(g.size(),2);QCOMPARE(g[0].toObject()["busy"].toInt(),67);QCOMPARE(g[0].toObject()["usedMiB"].toInt(),2534);QVERIFY(!g[1].toObject().contains("busy"));QVERIFY(parseNvidiaMetrics("driver failed").isEmpty());}
};
QTEST_GUILESS_MAIN(TestSystemMonitor)
#include "test_systemmonitor.moc"
