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
    QSize sizeHint() const override { return {640, 360}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QImage m_image;
    QString m_message;
};

} // namespace bcs
