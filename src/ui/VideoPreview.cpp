#include "ui/VideoPreview.h"

#include <QPainter>

namespace bcs {

VideoPreview::VideoPreview(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(320, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent);
    clear();
}

void VideoPreview::setImage(const QImage& img)
{
    m_image = img;
    update();
}

void VideoPreview::setBadge(const QString& text)
{
    if (text == m_badge)
        return;
    m_badge = text;
    update();
}

void VideoPreview::clear(const QString& message)
{
    m_image = QImage();
    m_message = message.isEmpty() ? tr("Preview — the degraded output appears here") : message;
    update();
}

void VideoPreview::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(18, 18, 20));
    if (m_image.isNull()) {
        p.setPen(QColor(150, 150, 155));
        p.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap, m_message);
        return;
    }
    QSize s = m_image.size().scaled(size(), Qt::KeepAspectRatio);
    QRect target(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);
    // Nearest-neighbour when enlarging, so codec blocks are shown honestly
    // instead of being smoothed away by the preview.
    p.setRenderHint(QPainter::SmoothPixmapTransform, s.width() < m_image.width());
    p.drawImage(target, m_image);

    if (!m_badge.isEmpty()) {
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        const QRect text = p.fontMetrics().boundingRect(m_badge);
        const QRect box(target.topLeft() + QPoint(10, 10), text.size() + QSize(16, 8));
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(200, 30, 30, 220));
        p.drawRoundedRect(box, 4, 4);
        p.setPen(Qt::white);
        p.drawText(box, Qt::AlignCenter, m_badge);
    }
}

} // namespace bcs
