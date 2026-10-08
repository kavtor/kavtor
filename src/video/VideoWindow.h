#pragma once

#include <QtWidgets/QWidget>
#include <QtGui/QColor>

class VlcPlayer;

class VideoWindow : public QWidget
{
    Q_OBJECT

public:
    explicit VideoWindow(QWidget* parent = nullptr);
    ~VideoWindow() override;

    void setStreamPort(int port);
    int streamPort() const { return m_streamPort; }

    void setBorderColor(const QColor& color);
    QColor borderColor() const { return m_borderColor; }

    void setAspectRatio(const QString& ratio);
    void setCropRatio(const QString& ratio);

    bool isPlaying() const;
    void play();
    void stop();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateBorder();

    VlcPlayer* m_player = nullptr;
    int m_streamPort = -1;
    QColor m_borderColor;
    int m_borderWidth = 4;
};
