#pragma once

#include <QtCore/QObject>

class QThread;

class SpeedEditorHid : public QObject
{
    Q_OBJECT

public:
    enum Key : quint16 {
        Shtl = 0x1c,
        Jog = 0x1d,
        Scrl = 0x1e,
        Cam1 = 0x33,
        Cam2 = 0x34,
        Cam3 = 0x35,
        Cam4 = 0x36,
        Cam5 = 0x37,
        Cam6 = 0x38,
        Cam7 = 0x39,
        Cam8 = 0x3a,
        Cam9 = 0x3b,
        StopPlay = 0x3c,
        Trans = 0x22
    };

    enum JogMode : quint8 {
        Relative = 0,
        Shuttle = 1,
        Scroll = 2
    };

    explicit SpeedEditorHid(QObject* parent = nullptr);
    ~SpeedEditorHid() override;

    static int sourceIdForCamKey(quint16 key);
    static quint32 camLedMask(int sourceId);
    static quint32 ledMask(int sourceId, bool transOn);
    static constexpr quint32 TransLed = 1u << 4;

public slots:
    void setArmedSource(int sourceId);
    void setTransLed(bool on);
    void setJogMode(SpeedEditorHid::JogMode mode);

signals:
    void connectedChanged(bool connected);
    void keyPressed(quint16 key);
    void jogMoved(int value, quint8 mode);

private:
    class Worker;
    QThread* m_thread = nullptr;
    Worker* m_worker = nullptr;
};
