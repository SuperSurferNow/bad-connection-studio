#pragma once

#include <QElapsedTimer>
#include <QWidget>

namespace bcs {

// Horizontal peak meter, -60..0 dBFS, with decay and a 1.5 s peak hold.
class LevelMeter : public QWidget {
    Q_OBJECT
public:
    explicit LevelMeter(QWidget* parent = nullptr);
    void pushPeakDb(float db);
    void reset();
    QSize sizeHint() const override { return {200, 14}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    float m_level = -100;
    float m_hold = -100;
    QElapsedTimer m_holdTimer;
    QElapsedTimer m_decayTimer;
};

} // namespace bcs
