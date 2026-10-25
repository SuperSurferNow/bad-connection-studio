#include "ui/LevelMeter.h"

#include <QPainter>

#include <algorithm>

namespace bcs {

namespace {
constexpr float kFloorDb = -60.f;
float toFraction(float db) { return std::clamp((db - kFloorDb) / -kFloorDb, 0.f, 1.f); }
} // namespace

LevelMeter::LevelMeter(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(12);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_decayTimer.start();
    m_holdTimer.start();
}

void LevelMeter::pushPeakDb(float db)
{
    const float dt = float(m_decayTimer.restart()) / 1000.f;
    m_level = std::max(db, m_level - 30.f * dt); // 30 dB/s fall-back
    if (db >= m_hold || m_holdTimer.elapsed() > 1500) {
        m_hold = db;
        m_holdTimer.restart();
    }
    update();
}

void LevelMeter::reset()
{
    m_level = m_hold = -100;
    update();
}

void LevelMeter::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    QRect r = rect().adjusted(0, 1, -1, -1);
    p.fillRect(r, QColor(40, 40, 44));
    const int w = int(r.width() * toFraction(m_level));
    QLinearGradient g(r.topLeft(), r.topRight());
    g.setColorAt(0.0, QColor(60, 180, 90));
    g.setColorAt(toFraction(-12.f), QColor(60, 180, 90));
    g.setColorAt(toFraction(-6.f), QColor(220, 190, 40));
    g.setColorAt(1.0, QColor(220, 60, 50));
    p.fillRect(QRect(r.left(), r.top(), w, r.height()), g);
    if (m_hold > kFloorDb) {
        int x = r.left() + int(r.width() * toFraction(m_hold));
        p.fillRect(QRect(x - 1, r.top(), 2, r.height()), QColor(240, 240, 240));
    }
    p.setPen(QColor(90, 90, 95));
    p.drawRect(r);
}

} // namespace bcs
