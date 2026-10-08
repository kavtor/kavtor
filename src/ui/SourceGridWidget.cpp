#include "SourceGridWidget.h"
#include "../video/VideoWindow.h"

#include <QtWidgets/QGridLayout>

SourceGridWidget::SourceGridWidget(QWidget* parent)
    : QWidget(parent)
{
    QGridLayout* layout = new QGridLayout(this);
    layout->setSpacing(4);
    layout->setContentsMargins(2, 2, 2, 2);

    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 4; ++col) {
            VideoWindow* window = new VideoWindow(this);
            layout->addWidget(window, row, col);
            m_windows.append(window);
        }
    }
    setLayout(layout);
}

VideoWindow* SourceGridWidget::windowAt(int sourceIndex) const
{
    if (sourceIndex >= 0 && sourceIndex < m_windows.size()) {
        return m_windows.at(sourceIndex);
    }
    return nullptr;
}

void SourceGridWidget::setStreamPort(int sourceIndex, int port)
{
    VideoWindow* window = windowAt(sourceIndex);
    if (window) {
        window->setStreamPort(port);
    }
}

void SourceGridWidget::setBorderColor(int sourceIndex, const QColor& color)
{
    VideoWindow* window = windowAt(sourceIndex);
    if (window) {
        window->setBorderColor(color);
    }
}
