#include "VlcPlayer.h"

#include <QtCore/QDebug>
#include <QtGui/QPaintEvent>
#include <QtGui/QPainter>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

VlcPlayer::VlcPlayer(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

VlcPlayer::~VlcPlayer()
{
    stop();
    cleanupVlc();
}

bool VlcPlayer::initVlc()
{
    if (m_vlcInstance) {
        return true;
    }

    const char* vlcArgs[] = {
        "--ignore-config",
        "--deinterlace=-1",
        "--deinterlace-mode=yadif",
        "--video-filter=deinterlace",
        "--network-caching=300",
        "--no-audio",
    };
    int argCount = sizeof(vlcArgs) / sizeof(vlcArgs[0]);

    m_vlcInstance = libvlc_new(argCount, vlcArgs);
    if (!m_vlcInstance) {
        qWarning() << "Failed to create libvlc instance";
        return false;
    }

    m_vlcMediaPlayer = libvlc_media_player_new(m_vlcInstance);
    if (!m_vlcMediaPlayer) {
        qWarning() << "Failed to create libvlc media player";
        cleanupVlc();
        return false;
    }

    return true;
}

void VlcPlayer::cleanupVlc()
{
    if (m_vlcMediaPlayer) {
        libvlc_media_player_release(m_vlcMediaPlayer);
        m_vlcMediaPlayer = nullptr;
    }
    if (m_vlcInstance) {
        libvlc_release(m_vlcInstance);
        m_vlcInstance = nullptr;
    }
    m_vlcMedia = nullptr;
}

bool VlcPlayer::playUdpStream(int port)
{
    if (m_playing) {
        stop();
    }

    if (!initVlc()) {
        emit error(tr("Could not initialize VLC"));
        return false;
    }

    QString url = QString("udp://@0.0.0.0:%1").arg(port);
    m_vlcMedia = libvlc_media_new_location(m_vlcInstance, url.toUtf8().constData());
    if (!m_vlcMedia) {
        emit error(tr("Could not create media for UDP stream"));
        return false;
    }

    libvlc_media_player_set_media(m_vlcMediaPlayer, m_vlcMedia);
    libvlc_media_release(m_vlcMedia); // player holds reference

    // Set render target to this widget's native window ID
#if defined(Q_OS_WIN)
    libvlc_media_player_set_hwnd(m_vlcMediaPlayer, (void*)winId());
#elif defined(Q_OS_MAC)
    libvlc_media_player_set_nsobject(m_vlcMediaPlayer, (void*)winId());
#else
    // Linux / X11
    libvlc_media_player_set_xwindow(m_vlcMediaPlayer, winId());
#endif

    if (libvlc_media_player_play(m_vlcMediaPlayer) == -1) {
        emit error(tr("Failed to start playback"));
        return false;
    }

    m_currentPort = port;
    m_playing = true;
    emit started();
    return true;
}

void VlcPlayer::stop()
{
    if (m_vlcMediaPlayer && m_playing) {
        libvlc_media_player_stop(m_vlcMediaPlayer);
        m_playing = false;
        emit stopped();
    }
}

bool VlcPlayer::isPlaying() const
{
    return m_playing;
}

void VlcPlayer::setAspectRatio(const QString& ratio)
{
    if (m_vlcMediaPlayer) {
        libvlc_video_set_aspect_ratio(m_vlcMediaPlayer, ratio.toUtf8().constData());
    }
}

void VlcPlayer::setCropRatio(const QString& ratio)
{
    (void)ratio;
    // Not implemented in this version of libvlc
}

void VlcPlayer::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (!m_playing) {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, tr("No stream"));
    }
    QWidget::paintEvent(event);
}

void VlcPlayer::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // VLC will automatically adjust video to widget size
}
