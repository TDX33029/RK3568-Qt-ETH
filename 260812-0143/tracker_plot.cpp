#include "tracker_plot.h"
#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <cmath>
#include <limits>
#include <algorithm>

TrackerPlot::TrackerPlot(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(180, 160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

static void projectPoint(const double s[6], TrackerPlot::ViewMode mode,
                         int coordX, int coordY, double &vx, double &vy) {
    if (mode == TrackerPlot::View3D) {
        static const double iso_cos = 0.8660254037844386;
        static const double iso_sin = 0.5;
        vx = (s[0] - s[1]) * iso_cos;
        vy = s[2] - (s[0] + s[1]) * iso_sin;
    } else {
        vx = s[coordX]; vy = s[coordY];
    }
}

void TrackerPlot::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int w = width(), h = height();
    const int margin = 8;
    const int plotW = w - 2 * margin, plotH = h - 2 * margin;

    /* 白底 + 浅灰边框 + 浅网格 */
    p.fillRect(rect(), Qt::white);
    p.setPen(QPen(QColor(0xCC, 0xCC, 0xCC), 1));
    p.drawRect(margin, margin, plotW, plotH);
    p.setPen(QPen(QColor(0xF0, 0xF0, 0xF0), 1, Qt::DotLine));
    for (int gx = margin + plotW / 4; gx < margin + plotW; gx += std::max(1, plotW / 4))
        p.drawLine(gx, margin, gx, margin + plotH);
    for (int gy = margin + plotH / 4; gy < margin + plotH; gy += std::max(1, plotH / 4))
        p.drawLine(margin, gy, margin + plotW, gy);

    if (!m_result) return;

    int coordX = 0, coordY = 1;
    if (m_mode == ViewXZ) { coordX = 0; coordY = 2; }

    /* 坐标范围: 估计 + 真值(若有) + 基站 */
    double minX =  std::numeric_limits<double>::max();
    double maxX = -std::numeric_limits<double>::max();
    double minY =  std::numeric_limits<double>::max();
    double maxY = -std::numeric_limits<double>::max();
    auto acc = [&](double vx, double vy) {
        minX = std::min(minX, vx); maxX = std::max(maxX, vx);
        minY = std::min(minY, vy); maxY = std::max(maxY, vy);
    };
    for (size_t t = 0; t < m_result->target_count; ++t)
        for (size_t i = 0; i < m_result->steps; ++i) {
            const double *es = tracker_result_estimate_at(m_result, t, i);
            if (es) { double vx,vy; projectPoint(es, m_mode, coordX, coordY, vx, vy); acc(vx, vy); }
            if (m_result->truth_history) {
                const double *tr = tracker_result_truth_at(m_result, t, i);
                if (tr) { double vx,vy; projectPoint(tr, m_mode, coordX, coordY, vx, vy); acc(vx, vy); }
            }
        }
    for (size_t a = 0; a < m_result->config.anchor_count; ++a) {
        const Tracker3DVec3 *an = &m_result->config.anchors[a];
        double s[6] = {an->x, an->y, an->z, 0,0,0};
        double vx, vy; projectPoint(s, m_mode, coordX, coordY, vx, vy); acc(vx, vy);
    }
    if (minX > maxX) { minX = 0; maxX = 1; minY = 0; maxY = 1; }  /* 无任何点 */
    double spanX = std::max(maxX - minX, 1e-6), spanY = std::max(maxY - minY, 1e-6);
    /* 留 5% 边距, 锚点标签不被裁切 */
    minX -= spanX * 0.05; maxX += spanX * 0.05;
    minY -= spanY * 0.05; maxY += spanY * 0.05;
    spanX = maxX - minX; spanY = maxY - minY;
    auto toScreen = [&](double vx, double vy) -> QPointF {
        return QPointF(margin + 4 + (vx - minX) / spanX * (plotW - 8),
                       margin + plotH - 4 - (vy - minY) / spanY * (plotH - 8));
    };

    /* 真值轨迹(仿真, 灰虚线; 实时无真值不画) */
    if (m_result->truth_history) {
        for (size_t t = 0; t < m_result->target_count; ++t) {
            QPainterPath path; bool first = true;
            for (size_t i = 0; i < m_result->steps; ++i) {
                const double *tr = tracker_result_truth_at(m_result, t, i);
                if (!tr) continue;
                double vx, vy; projectPoint(tr, m_mode, coordX, coordY, vx, vy);
                QPointF pt = toScreen(vx, vy);
                if (first) { path.moveTo(pt); first = false; } else path.lineTo(pt);
            }
            p.setPen(QPen(QColor(0x88, 0x88, 0x88), 1.5, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        }
    }

    /* 估计轨迹: 红色实线 (实时主显示) + 末端红点 */
    for (size_t t = 0; t < m_result->target_count; ++t) {
        QPainterPath path; bool first = true;
        for (size_t i = 0; i < m_result->steps; ++i) {
            const double *es = tracker_result_estimate_at(m_result, t, i);
            if (!es) continue;
            double vx, vy; projectPoint(es, m_mode, coordX, coordY, vx, vy);
            QPointF pt = toScreen(vx, vy);
            if (first) { path.moveTo(pt); first = false; } else path.lineTo(pt);
        }
        p.setPen(QPen(QColor(0xD0, 0x1F, 0x1F), 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        const double *cur = tracker_result_estimate_at(m_result, t, m_result->steps - 1);
        if (cur) {
            double vx, vy; projectPoint(cur, m_mode, coordX, coordY, vx, vy);
            QPointF pt = toScreen(vx, vy);
            p.setPen(Qt::NoPen); p.setBrush(QColor(0xD0, 0x1F, 0x1F));
            p.drawEllipse(pt, 3.5, 3.5);
        }
    }

    /* 基站锚点: 深色方块 + 标签 A0.. */
    p.setFont(QFont("sans-serif", 8, QFont::Bold));
    for (size_t a = 0; a < m_result->config.anchor_count; ++a) {
        const Tracker3DVec3 *an = &m_result->config.anchors[a];
        double s[6] = {an->x, an->y, an->z, 0,0,0};
        double vx, vy; projectPoint(s, m_mode, coordX, coordY, vx, vy);
        QPointF pt = toScreen(vx, vy);
        p.setPen(QPen(QColor(0x1B, 0x2C, 0x34), 1));
        p.setBrush(QColor(0x2A, 0x3A, 0x4A));
        p.drawRect(pt.x() - 4, pt.y() - 4, 8, 8);
        p.setPen(QColor(0x1B, 0x2C, 0x34));
        p.drawText(pt.x() + 7, pt.y() + 4, QStringLiteral("A%1").arg(a));
    }

    /* 图例 */
    p.setFont(QFont("sans-serif", 8));
    const char *viewName = (m_mode == ViewXY) ? "XY" : (m_mode == ViewXZ) ? "XZ" : "3D";
    int lx = margin + 6, ly = margin + 14;
    p.setPen(QColor(0x33, 0x33, 0x33)); p.drawText(lx, ly, QString::fromLatin1(viewName));
    ly += 12;
    p.setPen(QPen(QColor(0xD0, 0x1F, 0x1F), 2)); p.drawLine(lx, ly - 3, lx + 22, ly - 3);
    p.setPen(QColor(0x55, 0x55, 0x55)); p.drawText(lx + 28, ly, QStringLiteral("估计"));
    if (m_result->truth_history) {
        ly += 12;
        p.setPen(QPen(QColor(0x88, 0x88, 0x88), 1.5, Qt::DashLine)); p.drawLine(lx, ly - 3, lx + 22, ly - 3);
        p.setPen(QColor(0x55, 0x55, 0x55)); p.drawText(lx + 28, ly, QStringLiteral("真值"));
    }
    if (m_result->config.anchor_count > 0) {
        ly += 12;
        p.setBrush(QColor(0x2A, 0x3A, 0x4A)); p.setPen(Qt::NoPen);
        p.drawRect(lx + 8, ly - 6, 7, 7);
        p.setPen(QColor(0x55, 0x55, 0x55)); p.drawText(lx + 28, ly, QStringLiteral("基站"));
    }
}
