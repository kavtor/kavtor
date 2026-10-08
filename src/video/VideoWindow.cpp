#include "VideoWindow.h"
#include "VlcPlayer.h"

#include <QtGui/QPainter>
#include <QtGui/QPaintEvent>
#include <QtWidgets/QSizePolicy>
#include <QtCore/QDebug>

VideoWindow::VideoWindow(QWidget* parent)
    : QWidget(parent)
{
    m_player = new VlcPlayer(this);
    m_borderColor = Qt::transparent;
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

VideoWindow::~VideoWindow() = default;

void VideoWindow::setStreamPort(int port)
{
    if (m_streamPort == port)
        return;
    m_streamPort = port;
    if (port > 0) {
        m_player->playUdpStream(port);
    } else {
        m_player->stop();
    }
}

void VideoWindow::setBorderColor(const QColor& color)
{
    if (m_borderColor == color)
        return;
    m_borderColor = color;
    update();
}

void VideoWindow::setAspectRatio(const QString& ratio)
{
    m_player->setAspectRatio(ratio);
}

void VideoWindow::setCropRatio(const QString& ratio)
{
    m_player->setCropRatio(ratio);
}

bool VideoWindow::isPlaying() const
{
    return m_player->isPlaying();
}

void VideoWindow::play()
{
    if (m_streamPort > 0) {
        m_player->playUdpStream(m_streamPort);
    }
}

void VideoWindow::stop()
{
    m_player->stop();
}

void VideoWindow::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    // Draw border if color is not transparent
    if (m_borderColor.alpha() > 0) {
        QPen pen(m_borderColor);
        pen.setWidth(m_borderWidth);
        painter.setPen(pen);
        painter.drawRect(rect().adjusted(m_borderWidth/2, m_borderWidth/2, -m_borderWidth/2, -m_borderWidth/2));
    }

    QWidget::paintEvent(event);
}

void VideoWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // Adjust player geometry to be inside border
    const int margin = m_borderWidth;
    m_player->setGeometry(margin, margin, width() - 2*margin, height() - 2*margin);
}
