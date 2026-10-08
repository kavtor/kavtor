#pragma once

#include <QtCore/QObject>
#include <QtCore/QByteArray>
#include <QtCore/QVector>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <chrono>
inline qint64 clipMonotonicMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}

class QUdpSocket;

struct OscFileTime {
    int channel = 0;
    int layer = 0;
    double elapsed = 0;
    double duration = 0;
    bool playbackKnown=false,paused=false;
    double nativeFps=0;
    int scrubDirection=0;
    qint64 scrubUntilMs=0;
};

struct OscAudioPeak {
    int channel = 0;
    qint32 left = 0;
    qint32 right = 0;
};

struct OscPlayback {
    int channel=0,layer=0;bool hasPaused=false,paused=false;double nativeFps=0;
};
struct OscDatagram {
    QVector<OscFileTime> times;
    QVector<OscAudioPeak> peaks;
    QVector<OscPlayback> playback;
};

OscDatagram parseOscDatagram(const QByteArray& packet);
QVector<OscFileTime> parseOscFileTimes(const QByteArray& packet);
int meterUnit(qint32 peak);
QString formatClipClock(double seconds);

class OscListener : public QObject
{
    Q_OBJECT

public:
    explicit OscListener(QObject* parent = nullptr);
    ~OscListener() override;

    bool start(quint16 port);
    void stop();
    bool isListening() const;
    quint16 port() const;

signals:
    void fileTimeReceived(int channel, int layer, double elapsed, double duration);
    void playbackReceived(const OscPlayback& playback);
    void audioPeaksReceived(const QVector<OscAudioPeak>& peaks);

private slots:
    void onReadyRead();

private:
    QUdpSocket* m_socket = nullptr;
};
