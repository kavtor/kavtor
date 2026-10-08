#pragma once
#include <QtCore/QObject>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QTimer>
#include <QtCore/QProcess>

struct CpuCounters { quint64 total=0, idle=0; bool valid=false; };
CpuCounters parseCpuCounters(const QByteArray& data);
QJsonObject parseSystemMemory(const QByteArray& data);
QJsonArray parseNvidiaMetrics(const QByteArray& data);

// Local host telemetry only; this does not measure a remote CasparCG server.
class SystemMonitor : public QObject {
    Q_OBJECT
public:
    explicit SystemMonitor(QObject* parent=nullptr);
    ~SystemMonitor() override;
    void start();
    void stop();
signals:
    void changed(const QJsonObject& metrics);
private:
    void sample();
    void publish();
    QTimer timer, watchdog;
    QProcess gpu;
    CpuCounters previous;
    QJsonObject current;
    QJsonArray sysfsGpus;
    QString nvidia;
};
