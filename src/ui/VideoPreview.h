#pragma once

#include <QImage>
#include <QWidget>

namespace bcs {

// Shows the degraded output exactly as it is sent to the virtual camera
// (decoded frames, scaled for display only).
class VideoPreview : public QWidget {
    Q_OBJECT
public:
    explicit VideoPreview(QWidget* parent = nullptr);
    void setImage(const QImage& img);
    void clear(const QString& message = {});
    // Small label drawn over the top-left corner of the image ("LOOP").
    void setBadge(const QString& text);
    QSize sizeHint() const override { return {640, 360}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QImage m_image;
    QString m_message;
    QString m_badge;
};

} // namespace bcs
