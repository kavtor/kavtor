#include "SystemMonitor.h"
#include <QtCore/QFile>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>
#include <QtCore/QStandardPaths>

static QByteArray readFile(const QString& path) {
    QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray();
}
CpuCounters parseCpuCounters(const QByteArray& data) {
    CpuCounters result;const auto fields=data.split('\n').value(0).simplified().split(' ');
    if(fields.size()<5||fields[0]!="cpu")return result;
    // guest/guest_nice are already included in user/nice.
    for(int i=1;i<fields.size()&&i<=8;++i){bool ok=false;const auto value=fields[i].toULongLong(&ok);if(!ok)return {};result.total+=value;if(i==4||i==5)result.idle+=value;}
    result.valid=true;return result;
}
QJsonObject parseSystemMemory(const QByteArray& data) {
    qint64 total=-1,available=-1;
    for(const auto& line:data.split('\n')) {const auto fields=line.simplified().split(' ');if(fields.size()<2)continue;
        bool ok=false;const auto value=fields[1].toLongLong(&ok);if(!ok)continue;
        if(fields[0]=="MemTotal:")total=value;if(fields[0]=="MemAvailable:")available=value;
    }
    if(total<=0||available<0||available>total)return {};
    return {{"usedMiB",double(total-available)/1024},{"totalMiB",double(total)/1024}};
}
QJsonArray parseNvidiaMetrics(const QByteArray& data) {
    QJsonArray result;
    for(const auto& line:data.split('\n')) {const auto fields=line.split(',');if(fields.size()!=6)continue;
        QJsonObject item{{"name",QString::fromUtf8(fields[1].trimmed())},{"pci",QString::fromUtf8(fields[2].trimmed())}};
        for(int i=3;i<6;++i){bool ok=false;const double value=fields[i].trimmed().toDouble(&ok);
            if(ok&&value>=0&&(i!=3||value<=100))item.insert(i==3?"busy":i==4?"usedMiB":"totalMiB",value);
        }
        result.append(item);
    }return result;
}
SystemMonitor::SystemMonitor(QObject* parent):QObject(parent) {
    timer.setInterval(1000);watchdog.setSingleShot(true);watchdog.setInterval(1500);
    connect(&timer,&QTimer::timeout,this,&SystemMonitor::sample);
    connect(&watchdog,&QTimer::timeout,this,[this](){if(gpu.state()!=QProcess::NotRunning)gpu.kill();});
    connect(&gpu,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status){
        watchdog.stop();QJsonArray values=sysfsGpus;
        if(code==0&&status==QProcess::NormalExit)for(const auto& value:parseNvidiaMetrics(gpu.readAllStandardOutput()))values.append(value);
        current.insert("gpus",values);publish();
    });
    connect(&gpu,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart){watchdog.stop();current.insert("gpus",sysfsGpus);publish();}});
    nvidia=QStandardPaths::findExecutable("nvidia-smi");
}
SystemMonitor::~SystemMonitor(){
    // QProcess may emit finished while its destructor stops a running query.
    // Disconnect before JSON members are destroyed.
    QObject::disconnect(&gpu,nullptr,this,nullptr);
    stop();
}
void SystemMonitor::start(){previous={};timer.start();sample();}
void SystemMonitor::stop(){timer.stop();watchdog.stop();if(gpu.state()!=QProcess::NotRunning)gpu.kill();previous={};}
void SystemMonitor::publish(){if(timer.isActive())emit changed(current);}
void SystemMonitor::sample() {
    if(gpu.state()!=QProcess::NotRunning)return; // Never overlap probes or block the control thread.
    current={{"host","kavtor host"}};
#ifdef Q_OS_LINUX
    const auto cpu=parseCpuCounters(readFile("/proc/stat"));
    if(cpu.valid&&previous.valid&&cpu.total>previous.total&&cpu.idle>=previous.idle){const auto total=cpu.total-previous.total,idle=cpu.idle-previous.idle;if(idle<=total)current.insert("cpu",100.0*(total-idle)/total);}
    previous=cpu;current.insert("ram",parseSystemMemory(readFile("/proc/meminfo")));
#endif
    sysfsGpus={};
#ifdef Q_OS_LINUX
    for(const auto& card:QDir("/sys/class/drm").entryList(QDir::Dirs|QDir::NoDotAndDotDot)) {
        if(!QRegularExpression("^card[0-9]+$").match(card).hasMatch())continue;
        const QString device="/sys/class/drm/"+card+"/device/";
        if(readFile(device+"vendor").trimmed()!="0x1002")continue;
        QJsonObject item{{"name","AMD "+card},{"pci",QFileInfo(device).canonicalFilePath().section('/',-1)}};
        for(const auto& pair: {qMakePair("gpu_busy_percent","busy"),qMakePair("mem_info_vram_used","usedMiB"),qMakePair("mem_info_vram_total","totalMiB")}) {
            bool ok=false;const auto value=readFile(device+pair.first).trimmed().toDouble(&ok);if(ok)item.insert(pair.second,QString(pair.second)=="busy"?value:value/(1024*1024));
        }sysfsGpus.append(item);
    }
#endif
    current.insert("gpus",sysfsGpus);
    if(nvidia.isEmpty()){publish();return;}
    gpu.start(nvidia,{"--query-gpu=index,name,pci.bus_id,utilization.gpu,memory.used,memory.total","--format=csv,noheader,nounits"});watchdog.start();
}
