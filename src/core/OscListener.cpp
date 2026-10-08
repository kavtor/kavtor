#include "OscListener.h"

#include <QtNetwork/QUdpSocket>
#include <QtNetwork/QHostAddress>
#include <QtCore/QtEndian>
#include <QtCore/QDebug>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QStringList>
#include <cmath>
#include <cstring>
#include <limits>

namespace {

int oscPaddedLength(int size)
{
    return (size + 3) & ~3;
}

bool readOscString(const QByteArray& packet, int* offset, QByteArray* out)
{
    const int start = *offset;
    if (start < 0 || start >= packet.size()) {
        return false;
    }
    const int end = packet.indexOf('\0', start);
    if (end < 0) {
        return false;
    }
    *out = packet.mid(start, end - start);
    *offset = oscPaddedLength(end - start + 1) + start;
    return *offset <= packet.size();
}

bool readOscFloat(const QByteArray& packet, int* offset, float* out)
{
    if (*offset + 4 > packet.size()) {
        return false;
    }
    quint32 bits = 0;
    memcpy(&bits, packet.constData() + *offset, 4);
    bits = qFromBigEndian(bits);
    memcpy(out, &bits, 4);
    *offset += 4;
    return true;
}

bool readOscInt32(const QByteArray& packet, int* offset, qint32* out)
{
    if (*offset + 4 > packet.size()) {
        return false;
    }
    qint32 value = 0;
    memcpy(&value, packet.constData() + *offset, 4);
    *out = qFromBigEndian(value);
    *offset += 4;
    return true;
}

bool parseFileTimeAddress(const QByteArray& address, int* channel, int* layer)
{
    const QStringList parts = QString::fromLatin1(address).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() < 7 || parts.at(0) != QLatin1String("channel") || parts.at(2) != QLatin1String("stage")
        || parts.at(3) != QLatin1String("layer") || parts.at(parts.size() - 2) != QLatin1String("file")
        || parts.at(parts.size() - 1) != QLatin1String("time")) {
        return false;
    }
    bool channelOk = false;
    bool layerOk = false;
    *channel = parts.at(1).toInt(&channelOk);
    *layer = parts.at(4).toInt(&layerOk);
    return channelOk && layerOk && *channel > 0 && *layer > 0;
}

bool parseAudioAddress(const QByteArray& address, int* channel)
{
    const QStringList parts = QString::fromLatin1(address).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() != 5 || parts.at(0) != QLatin1String("channel") || parts.at(2) != QLatin1String("mixer")
        || parts.at(3) != QLatin1String("audio") || parts.at(4) != QLatin1String("volume")) {
        return false;
    }
    bool channelOk = false;
    *channel = parts.at(1).toInt(&channelOk);
    return channelOk && *channel > 0;
}

struct OscScan {
    QVector<OscPlayback> playback;
    QVector<OscFileTime> times;
    QVector<OscAudioPeak> peaks;
};

void parseOscMessage(const QByteArray& packet, int start, int end, OscScan* out)
{
    int offset = start;
    QByteArray address;
    if (!readOscString(packet, &offset, &address) || offset > end) {
        return;
    }
    int channel = 0;
    int layer = 0;
    const auto parts=QString::fromLatin1(address).split('/',Qt::SkipEmptyParts);
    if(parts.size()>=7&&parts[0]=="channel"&&parts[2]=="stage"&&parts[3]=="layer"&&parts[5]=="foreground"){
        bool channelOk=false,layerOk=false;OscPlayback data;data.channel=parts[1].toInt(&channelOk);data.layer=parts[4].toInt(&layerOk);
        if(channelOk&&layerOk&&data.channel>0&&data.layer>0){
            QByteArray types;
            if(parts.size()==7&&parts[6]=="paused"){
                if(!readOscString(packet,&offset,&types))return;
                if(types==",T"||types==",F"){data.hasPaused=true;data.paused=types==",T";}
                else if(types==",i"){qint32 v;if(readOscInt32(packet,&offset,&v)&&offset<=end&&(v==0||v==1)){data.hasPaused=true;data.paused=v==1;}}
                if(data.hasPaused)out->playback.append(data);return;
            }
            if(parts.size()==10&&parts[6]=="file"&&parts[7]=="streams"&&parts[9]=="fps"){
                qint32 num=0,den=0;if(readOscString(packet,&offset,&types)&&types==",ii"&&readOscInt32(packet,&offset,&num)&&readOscInt32(packet,&offset,&den)&&offset<=end&&num>0&&den>0&&double(num)/den<=240){data.nativeFps=double(num)/den;out->playback.append(data);}return;
            }
        }
    }
    if (parseAudioAddress(address, &channel)) {
        QByteArray types;
        if (!readOscString(packet, &offset, &types) || offset > end || !types.startsWith(',')) {
            return;
        }
        QVector<qint32> values;
        for (int i = 1; i < types.size(); ++i) {
            if (types.at(i) != 'i') {
                return;
            }
            qint32 value = 0;
            if (!readOscInt32(packet, &offset, &value) || offset > end) {
                return;
            }
            values.append(value);
        }
        if (values.isEmpty()) {
            return;
        }
        OscAudioPeak peak;
        peak.channel = channel;
        peak.left = values.at(0);
        peak.right = values.size() > 1 ? values.at(1) : values.at(0);
        out->peaks.append(peak);
        return;
    }
    if (!parseFileTimeAddress(address, &channel, &layer)) {
        return;
    }

    QByteArray types;
    if (!readOscString(packet, &offset, &types) || offset > end || !types.startsWith(',')) {
        return;
    }

    OscFileTime time;
    time.channel = channel;
    time.layer = layer;
    int floats = 0;
    for (int i = 1; i < types.size() && floats < 2; ++i) {
        if (types.at(i) != 'f') {
            return;
        }
        float value = 0;
        if (!readOscFloat(packet, &offset, &value) || offset > end) {
            return;
        }
        if (floats == 0) {
            time.elapsed = value;
        } else {
            time.duration = value;
        }
        ++floats;
    }
    if (floats == 2) {
        out->times.append(time);
    }
}

void parseOscElement(const QByteArray& packet, int start, int end, OscScan* out);

void parseOscBundle(const QByteArray& packet, int start, int end, OscScan* out)
{
    int offset = start;
    QByteArray header;
    if (!readOscString(packet, &offset, &header) || header != QByteArrayLiteral("#bundle")) {
        return;
    }
    offset += 8; // timetag
    while (offset + 4 <= end) {
        qint32 size = 0;
        if (!readOscInt32(packet, &offset, &size) || size < 0 || offset + size > end) {
            return;
        }
        parseOscElement(packet, offset, offset + size, out);
        offset += size;
    }
}

void parseOscElement(const QByteArray& packet, int start, int end, OscScan* out)
{
    if (start >= end) {
        return;
    }
    if (end - start >= 8 && memcmp(packet.constData() + start, "#bundle", 7) == 0) {
        parseOscBundle(packet, start, end, out);
        return;
    }
    parseOscMessage(packet, start, end, out);
}

} // namespace

OscDatagram parseOscDatagram(const QByteArray& packet)
{
    OscScan scan;
    parseOscElement(packet, 0, packet.size(), &scan);
    OscDatagram datagram;
    datagram.times = scan.times;
    datagram.peaks = scan.peaks;datagram.playback=scan.playback;
    return datagram;
}

QVector<OscFileTime> parseOscFileTimes(const QByteArray& packet)
{
    return parseOscDatagram(packet).times;
}

int meterUnit(qint32 peak)
{
    if (peak <= 0) {
        return 0;
    }
    const double full = double(std::numeric_limits<qint32>::max());
    const double db = 20.0 * std::log10(std::max(double(peak) / full, 1e-8));
    return qBound(0, qRound(((db + 60.0) / 60.0) * 1000.0), 1000);
}

QString formatClipClock(double seconds)
{
    int total = qMax(0, int(seconds));
    const int hours = total / 3600;
    total %= 3600;
    const int minutes = total / 60;
    const int secs = total % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(secs, 2, 10, QLatin1Char('0'));
}

OscListener::OscListener(QObject* parent)
    : QObject(parent)
{
    m_socket = new QUdpSocket(this);
    connect(m_socket, &QUdpSocket::readyRead, this, &OscListener::onReadyRead);
}

OscListener::~OscListener()
{
    stop();
}

bool OscListener::start(quint16 port)
{
    if (m_socket->state() == QAbstractSocket::BoundState && m_socket->localPort() == port) {
        return true;
    }
    stop();
    if (!m_socket->bind(QHostAddress::Any, port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        qWarning() << "OscListener: cannot bind UDP port" << port << m_socket->errorString();
        return false;
    }
    return true;
}

void OscListener::stop()
{
    m_socket->close();
}

bool OscListener::isListening() const
{
    return m_socket->state() == QAbstractSocket::BoundState;
}

quint16 OscListener::port() const
{
    return m_socket->localPort();
}

void OscListener::onReadyRead()
{
    while (m_socket->hasPendingDatagrams()) {
        QByteArray packet;
        packet.resize(int(m_socket->pendingDatagramSize()));
        m_socket->readDatagram(packet.data(), packet.size());
        const OscDatagram datagram = parseOscDatagram(packet);
        for(const auto& playback:datagram.playback)emit playbackReceived(playback);
        for (const OscFileTime& time : datagram.times) {
            emit fileTimeReceived(time.channel, time.layer, time.elapsed, time.duration);
        }
        if (!datagram.peaks.isEmpty()) {
            emit audioPeaksReceived(datagram.peaks);
        }
    }
}
