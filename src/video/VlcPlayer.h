#pragma once

#include <QtWidgets/QWidget>

#include <vlc/vlc.h>

class VlcPlayer : public QWidget
{
    Q_OBJECT

public:
    explicit VlcPlayer(QWidget* parent = nullptr);
    ~VlcPlayer() override;

    bool playUdpStream(int port);
    void stop();
    bool isPlaying() const;

    void setAspectRatio(const QString& ratio); // "16:9", "4:3", etc.
    void setCropRatio(const QString& ratio);

signals:
    void started();
    void stopped();
    void error(const QString& message);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    bool initVlc();
    void cleanupVlc();

    libvlc_instance_t* m_vlcInstance = nullptr;
    libvlc_media_t* m_vlcMedia = nullptr;
    libvlc_media_player_t* m_vlcMediaPlayer = nullptr;
    int m_currentPort = 0;
    bool m_playing = false;
};
