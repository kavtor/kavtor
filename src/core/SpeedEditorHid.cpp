#include "SpeedEditorHid.h"

#include <QtCore/QThread>
#include <QtCore/QMutex>
#include <QtCore/QElapsedTimer>
#include <QtCore/QSet>
#include <QtCore/QDebug>

#include <hidapi/hidapi.h>

#include <cstring>
#include <atomic>

namespace {

constexpr quint16 kVid = 0x1edb;
constexpr quint16 kPid = 0xda0e;

quint64 rol8(quint64 v)
{
    return ((v << 56) | (v >> 8)) & 0xffffffffffffffffULL;
}

quint64 rol8n(quint64 v, int n)
{
    for (int i = 0; i < n; ++i) {
        v = rol8(v);
    }
    return v;
}

quint64 bmdKbdAuth(quint64 challenge)
{
    static const quint64 evenTbl[] = {
        0x3ae1206f97c10bc8ULL, 0x2a9ab32bebf244c6ULL, 0x20a6f8b8df9adf0aULL, 0xaf80ece52cfc1719ULL,
        0xec2ee2f7414fd151ULL, 0xb055adfd73344a15ULL, 0xa63d2e3059001187ULL, 0x751bf623f42e0ddeULL
    };
    static const quint64 oddTbl[] = {
        0x3e22b34f502e7fdeULL, 0x24656b981875ab1cULL, 0xa17f3456df7bf8c3ULL, 0x6df72e1941aef698ULL,
        0x72226f011e66ab94ULL, 0x3831a3c606296b42ULL, 0xfd7ff81881332c89ULL, 0x61a3f6474ff236c6ULL
    };
    constexpr quint64 mask = 0xa79a63f585d37bf0ULL;
    const int n = int(challenge & 7);
    quint64 v = rol8n(challenge, n);
    quint64 k = 0;
    if ((v & 1) == ((0x78 >> n) & 1)) {
        k = evenTbl[n];
    } else {
        v = v ^ rol8(v);
        k = oddTbl[n];
    }
    return v ^ (rol8(v) & mask) ^ k;
}

quint64 readLe64(const unsigned char* data)
{
    quint64 value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= quint64(data[i]) << (8 * i);
    }
    return value;
}

void writeLe64(unsigned char* data, quint64 value)
{
    for (int i = 0; i < 8; ++i) {
        data[i] = static_cast<unsigned char>((value >> (8 * i)) & 0xff);
    }
}

} // namespace

class SpeedEditorHid::Worker : public QObject
{
    Q_OBJECT

public:
    explicit Worker(QObject* parent = nullptr)
        : QObject(parent)
    {
        m_run.store(true);
    }

    ~Worker() override
    {
        closeDevice();
    }

    void requestStop()
    {
        m_run.store(false);
    }

    void setArmedSource(int sourceId)
    {
        QMutexLocker lock(&m_mutex);
        m_armedSource = sourceId;
        m_ledsDirty = true;
    }

    void setTransLed(bool on)
    {
        QMutexLocker lock(&m_mutex);
        m_transLed = on;
        m_ledsDirty = true;
    }

    void setJogMode(SpeedEditorHid::JogMode mode)
    {
        QMutexLocker lock(&m_mutex);
        m_jogMode = mode;
        m_jogDirty = true;
    }

public slots:
    void run()
    {
        hid_init();
        QElapsedTimer authTimer;
        while (m_run.load()) {
            if (!m_dev) {
                m_dev = hid_open(kVid, kPid, nullptr);
                if (!m_dev) {
                    QThread::msleep(500);
                    continue;
                }
                if (!authenticate()) {
                    closeDevice();
                    QThread::msleep(1000);
                    continue;
                }
                authTimer.restart();
                m_heldKeys.clear();
                m_ledsDirty = true;
                m_jogDirty = true;
                emit connectedChanged(true);
            }

            flushOutputs();
            unsigned char buf[64] = {};
            const int n = hid_read_timeout(m_dev, buf, sizeof(buf), 80);
            if (n < 0) {
                emit connectedChanged(false);
                closeDevice();
                continue;
            }
            if (n > 0) {
                parseReport(buf, n);
            }
            if (authTimer.elapsed() > 480000) {
                if (!authenticate()) {
                    emit connectedChanged(false);
                    closeDevice();
                    continue;
                }
                authTimer.restart();
            }
        }
        if (m_dev) {
            emit connectedChanged(false);
        }
        closeDevice();
        hid_exit();
    }

signals:
    void connectedChanged(bool connected);
    void keyPressed(quint16 key);
    void jogMoved(int value, quint8 mode);

private:
    bool authenticate()
    {
        unsigned char buf[10] = {};
        buf[0] = 0x06;
        if (hid_send_feature_report(m_dev, buf, 10) < 0) {
            return false;
        }
        std::memset(buf, 0, sizeof(buf));
        buf[0] = 0x06;
        if (hid_get_feature_report(m_dev, buf, 10) < 2 || buf[0] != 0x06 || buf[1] != 0x00) {
            return false;
        }
        const quint64 challenge = readLe64(buf + 2);

        std::memset(buf, 0, sizeof(buf));
        buf[0] = 0x06;
        buf[1] = 0x01;
        if (hid_send_feature_report(m_dev, buf, 10) < 0) {
            return false;
        }
        std::memset(buf, 0, sizeof(buf));
        buf[0] = 0x06;
        if (hid_get_feature_report(m_dev, buf, 10) < 2 || buf[0] != 0x06 || buf[1] != 0x02) {
            return false;
        }

        std::memset(buf, 0, sizeof(buf));
        buf[0] = 0x06;
        buf[1] = 0x03;
        writeLe64(buf + 2, bmdKbdAuth(challenge));
        if (hid_send_feature_report(m_dev, buf, 10) < 0) {
            return false;
        }
        std::memset(buf, 0, sizeof(buf));
        buf[0] = 0x06;
        if (hid_get_feature_report(m_dev, buf, 10) < 2 || buf[0] != 0x06 || buf[1] != 0x04) {
            return false;
        }
        return true;
    }

    void flushOutputs()
    {
        int armed = -1;
        bool transLed = false;
        SpeedEditorHid::JogMode mode = SpeedEditorHid::Relative;
        bool ledsDirty = false;
        bool jogDirty = false;
        {
            QMutexLocker lock(&m_mutex);
            armed = m_armedSource;
            transLed = m_transLed;
            mode = m_jogMode;
            ledsDirty = m_ledsDirty;
            jogDirty = m_jogDirty;
            m_ledsDirty = false;
            m_jogDirty = false;
        }
        if (ledsDirty) {
            unsigned char buf[5] = {};
            buf[0] = 0x02;
            const quint32 leds = SpeedEditorHid::ledMask(armed, transLed);
            buf[1] = static_cast<unsigned char>(leds & 0xff);
            buf[2] = static_cast<unsigned char>((leds >> 8) & 0xff);
            buf[3] = static_cast<unsigned char>((leds >> 16) & 0xff);
            buf[4] = static_cast<unsigned char>((leds >> 24) & 0xff);
            hid_write(m_dev, buf, 5);
        }
        if (jogDirty) {
            unsigned char buf[7] = {0x03, static_cast<unsigned char>(mode), 0, 0, 0, 0, 255};
            hid_write(m_dev, buf, 7);
            unsigned char jogLed[2] = {0x04, 0};
            if (mode == SpeedEditorHid::Relative) {
                jogLed[1] = 0x01;
            } else if (mode == SpeedEditorHid::Shuttle) {
                jogLed[1] = 0x02;
            } else {
                jogLed[1] = 0x04;
            }
            hid_write(m_dev, jogLed, 2);
        }
    }

    void parseReport(const unsigned char* buf, int n)
    {
        if (n < 1) {
            return;
        }
        if (buf[0] == 0x04 && n >= 13) {
            QSet<quint16> held;
            for (int i = 0; i < 6; ++i) {
                const quint16 key = quint16(buf[1 + i * 2]) | (quint16(buf[2 + i * 2]) << 8);
                if (key != 0) {
                    held.insert(key);
                }
            }
            const QSet<quint16> pressed = held - m_heldKeys;
            m_heldKeys = held;
            for (quint16 key : pressed) {
                emit keyPressed(key);
            }
        } else if (buf[0] == 0x03 && n >= 6) {
            qint32 value = qint32(quint32(buf[2]) | (quint32(buf[3]) << 8)
                                  | (quint32(buf[4]) << 16) | (quint32(buf[5]) << 24));
            emit jogMoved(int(value), buf[1]);
        }
    }

    void closeDevice()
    {
        if (m_dev) {
            hid_close(m_dev);
            m_dev = nullptr;
        }
    }

    hid_device* m_dev = nullptr;
    QSet<quint16> m_heldKeys;
    QMutex m_mutex;
    int m_armedSource = -1;
    bool m_transLed = false;
    SpeedEditorHid::JogMode m_jogMode = SpeedEditorHid::Relative;
    bool m_ledsDirty = false;
    bool m_jogDirty = false;
    std::atomic<bool> m_run{true};
};

SpeedEditorHid::SpeedEditorHid(QObject* parent)
    : QObject(parent)
{
    m_thread = new QThread(this);
    m_worker = new Worker;
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::started, m_worker, &Worker::run);
    connect(m_worker, &Worker::connectedChanged, this, &SpeedEditorHid::connectedChanged);
    connect(m_worker, &Worker::keyPressed, this, &SpeedEditorHid::keyPressed);
    connect(m_worker, &Worker::jogMoved, this, &SpeedEditorHid::jogMoved);
    m_thread->start();
}

SpeedEditorHid::~SpeedEditorHid()
{
    m_worker->requestStop();
    m_thread->quit();
    if (!m_thread->wait(1500)) {
        m_thread->terminate();
        m_thread->wait(200);
    }
    delete m_worker;
}

int SpeedEditorHid::sourceIdForCamKey(quint16 key)
{
    if (key < Cam1 || key > Cam9) {
        return -1;
    }
    return int(key - Cam1);
}

quint32 SpeedEditorHid::camLedMask(int sourceId)
{
    switch (sourceId) {
    case 0:
        return 1u << 14;
    case 1:
        return 1u << 15;
    case 2:
        return 1u << 16;
    case 3:
        return 1u << 10;
    case 4:
        return 1u << 11;
    case 5:
        return 1u << 12;
    case 6:
        return 1u << 6;
    case 7:
        return 1u << 7;
    case 8:
        return 1u << 8;
    default:
        return 0;
    }
}

quint32 SpeedEditorHid::ledMask(int sourceId, bool transOn)
{
    quint32 mask = camLedMask(sourceId);
    if (transOn) {
        mask |= TransLed;
    }
    return mask;
}

void SpeedEditorHid::setArmedSource(int sourceId)
{
    m_worker->setArmedSource(sourceId);
}

void SpeedEditorHid::setTransLed(bool on)
{
    m_worker->setTransLed(on);
}

void SpeedEditorHid::setJogMode(SpeedEditorHid::JogMode mode)
{
    m_worker->setJogMode(mode);
}

#include "SpeedEditorHid.moc"
