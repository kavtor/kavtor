#pragma once

#include <QtWidgets/QWidget>

class VideoWindow;

class SourceGridWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SourceGridWidget(QWidget* parent = nullptr);
    ~SourceGridWidget() override = default;

    VideoWindow* windowAt(int sourceIndex) const;
    void setStreamPort(int sourceIndex, int port);
    void setBorderColor(int sourceIndex, const QColor& color);

private:
    QList<VideoWindow*> m_windows;
};
