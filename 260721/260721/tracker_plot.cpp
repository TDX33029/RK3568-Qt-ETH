#include "tracker_plot.h"
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
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

void TrackerPlot::projectIso(const double state[TRACKER3D_STATE_DIM], double *px, double *py) const {
    static const double iso_cos = 0.8660254037844386;
    static const double iso_sin = 0.5;
    *px = (state[0] - state[1]) * iso_cos;
    *py = state[2] - (state[0] + state[1]) * iso_sin;
}

static QColor truthColor(int targetIndex) {
    static const QColor colors[] = {
        QColor(0x8D, 0xBB, 0xEA),
        QColor(0x9B, 0xD6, 0xA8),
        QColor(0xF0, 0xC2, 0x7A)
    };
    return colors[targetIndex % 3];
}

static QColor estColor(int targetIndex) {
    static const QColor colors[] = {
        QColor(0x2A, 0x75, 0xBB),
        QColor(0x2E, 0x8B, 0x57),
        QColor(0xC4, 0x4E, 0x35)
    };
    return colors[targetIndex % 3];
}

void TrackerPlot::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();
    int margin = 8;
    int plotW = w - 2 * margin;
    int plotH = h - 2 * margin;

    // background
    p.fillRect(rect(), QColor(0x6A, 0x7A, 0x8C));
    p.setPen(QPen(QColor(0x4A, 0x5A, 0x6C), 1));
    p.drawRect(margin, margin, plotW, plotH);

    if (!m_result || m_result->steps == 0) return;

    // Compute bounds
    double minX = std::numeric_limits<double>::max();
    double maxX = -std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxY = -std::numeric_limits<double>::max();

    int coordX = 0, coordY = 1;
    if (m_mode == ViewXZ) { coordX = 0; coordY = 2; }

    for (size_t t = 0; t < m_result->target_count; ++t) {
        for (size_t i = 0; i < m_result->steps; ++i) {
            const double *truth = tracker_result_truth_at(m_result, t, i);
            const double *est = tracker_result_estimate_at(m_result, t, i);
            double px_t, py_t, px_e, py_e;

            if (m_mode == View3D) {
                projectIso(truth, &px_t, &py_t);
                projectIso(est, &px_e, &py_e);
            } else {
                px_t = truth[coordX]; py_t = truth[coordY];
                px_e = est[coordX];   py_e = est[coordY];
            }

            minX = std::min({minX, px_t, px_e});
            maxX = std::max({maxX, px_t, px_e});
            minY = std::min({minY, py_t, py_e});
            maxY = std::max({maxY, py_t, py_e});
        }
    }

    // Include anchor (base station) positions in bounds
    for (size_t i = 0; i < m_result->config.anchor_count; ++i) {
        const auto &a = m_result->config.anchors[i];
        double px, py;
        if (m_mode == View3D) {
            double st[TRACKER3D_STATE_DIM] = {a.x, a.y, a.z, 0, 0, 0};
            projectIso(st, &px, &py);
        } else {
            px = a.x;
            py = (m_mode == ViewXY) ? a.y : a.z;
        }
        minX = std::min(minX, px);
        maxX = std::max(maxX, px);
        minY = std::min(minY, py);
        maxY = std::max(maxY, py);
    }
    // Pad bounds 5 % so markers near edges don t clip
    {
        double padX = (maxX - minX) * 0.05;
        double padY = (maxY - minY) * 0.05;
        if (padX < 0.01) padX = 0.5;
        if (padY < 0.01) padY = 0.5;
        minX -= padX;  maxX += padX;
        minY -= padY;  maxY += padY;
    }

    double spanX = maxX - minX;
    double spanY = maxY - minY;
    if (spanX < 1e-6) spanX = 1.0;
    if (spanY < 1e-6) spanY = 1.0;

    auto toScreen = [&](double vx, double vy) -> QPointF {
        double sx = margin + 4 + (vx - minX) / spanX * (plotW - 8);
        double sy = margin + plotH - 4 - (vy - minY) / spanY * (plotH - 8);
        return QPointF(sx, sy);
    };

    // Draw for each target
    for (size_t t = 0; t < m_result->target_count; ++t) {
        QColor tc = truthColor((int)t);
        QColor ec = estColor((int)t);

        // Truth trajectory
        QPainterPath truthPath;
        bool first = true;
        for (size_t i = 0; i < m_result->steps; ++i) {
            const double *tr = tracker_result_truth_at(m_result, t, i);
            double px, py;
            if (m_mode == View3D) projectIso(tr, &px, &py);
            else { px = tr[coordX]; py = tr[coordY]; }
            QPointF pt = toScreen(px, py);
            if (first) { truthPath.moveTo(pt); first = false; }
            else truthPath.lineTo(pt);
        }
        p.setPen(QPen(tc, 1.8));
        p.setBrush(Qt::NoBrush);
        p.drawPath(truthPath);

        // Estimate trajectory
        QPainterPath estPath;
        first = true;
        for (size_t i = 0; i < m_result->steps; ++i) {
            const double *es = tracker_result_estimate_at(m_result, t, i);
            double px, py;
            if (m_mode == View3D) projectIso(es, &px, &py);
            else { px = es[coordX]; py = es[coordY]; }
            QPointF pt = toScreen(px, py);
            if (first) { estPath.moveTo(pt); first = false; }
            else estPath.lineTo(pt);
        }
        p.setPen(QPen(ec, 1.8));
        p.drawPath(estPath);
    }

    // Legend in top-left corner of plot
    if (m_result->target_count > 1) {
        int lx = margin + 6;
        int ly = margin + 6;
        for (size_t t = 0; t < m_result->target_count; ++t) {
            QString label = QString("T%1").arg(t + 1);
            p.setPen(estColor((int)t));
            p.setFont(QFont("sans-serif", 8));
            p.drawText(lx, ly, label);
            p.setPen(QPen(truthColor((int)t), 2));
            p.drawLine(lx + 24, ly - 2, lx + 42, ly - 2);
            ly += 14;
        }
    }

    // ── Draw anchors (base stations) ──
    {
        QColor fill(0xBB, 0x44, 0xCC);
        QColor edge(0xDD, 0x88, 0xEE);
        p.setFont(QFont("sans-serif", 8));
        for (size_t i = 0; i < m_result->config.anchor_count; ++i) {
            const auto &a = m_result->config.anchors[i];
            double px, py;
            if (m_mode == View3D) {
                double st[TRACKER3D_STATE_DIM] = {a.x, a.y, a.z, 0, 0, 0};
                projectIso(st, &px, &py);
            } else {
                px = a.x;
                py = (m_mode == ViewXY) ? a.y : a.z;
            }
            QPointF pt = toScreen(px, py);

            // Filled diamond marker
            p.setPen(QPen(edge, 1.2));
            p.setBrush(fill);
            QPolygonF diamond;
            diamond << QPointF(pt.x(), pt.y() - 5)
                    << QPointF(pt.x() + 5, pt.y())
                    << QPointF(pt.x(), pt.y() + 5)
                    << QPointF(pt.x() - 5, pt.y());
            p.drawPolygon(diamond);

            // White label next to marker
            p.setPen(Qt::white);
            p.drawText(pt + QPointF(8, -5), QString("A%1").arg(i));
        }
    }
}
