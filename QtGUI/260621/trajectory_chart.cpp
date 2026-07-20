#include "trajectory_chart.h"
#include "simulation_controller.h"

#include <QPainter>
#include <QPainterPath>

TrajectoryChart::TrajectoryChart(ViewType viewType, QWidget *parent)
    : QWidget(parent), m_viewType(viewType)
{
    setMinimumSize(200, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void TrajectoryChart::setController(SimulationController *ctrl) { m_ctrl = ctrl; }

void TrajectoryChart::refresh() { update(); }

void TrajectoryChart::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    int w = width(), h = height();
    int pad = 36, plotW = w - 2*pad, plotH = h - 2*pad;

    // Background
    p.fillRect(rect(), QColor("#F9F7F2"));

    if (!m_ctrl || !m_ctrl->hasResult() || plotW < 20 || plotH < 20) {
        p.fillRect(pad, pad, plotW, plotH, QColor("#F0F0F0"));
        p.setPen(QColor("#999"));
        p.drawText(QRect(pad, pad, plotW, plotH), Qt::AlignCenter, "No data");
        return;
    }

    QRectF bounds = m_ctrl->dataBounds(m_viewType);
    double mnx = bounds.left(), mxx = bounds.right();
    double mny = bounds.top(), mxy = bounds.bottom();

    auto tx = [&](double v) { return pad + (v - mnx) / (mxx - mnx) * plotW; };
    auto ty = [&](double v) { return h - pad - (v - mny) / (mxy - mny) * plotH; };

    // Plot area background
    p.fillRect(pad, pad, plotW, plotH, QColor("#FCFCFC"));

    // Grid
    p.setPen(QPen(QColor("#E5E5E5"), 0.5));
    int ng = 5;
    for (int g = 0; g <= ng; ++g) {
        int gx = pad + g * plotW / ng;
        int gy = pad + g * plotH / ng;
        p.drawLine(gx, pad, gx, h - pad);
        p.drawLine(pad, gy, w - pad, gy);
    }

    // Border
    p.setPen(QPen(QColor("#6A7A8C"), 1));
    p.drawRect(pad, pad, plotW, plotH);

    // Axis labels
    p.setPen(QColor("#666"));
    p.setFont(QFont("Sans", 8));
    for (int g = 0; g <= ng; ++g) {
        double vx = mnx + g * (mxx - mnx) / ng;
        double vy = mxy - g * (mxy - mny) / ng;
        p.drawText(QRect(pad + g*plotW/ng - 25, h - pad + 2, 50, 14), Qt::AlignHCenter, QString::number(vx, 'f', 1));
        p.drawText(QRect(0, pad + g*plotH/ng - 7, pad - 4, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number(vy, 'f', 1));
    }

    // Draw trajectories
    int nTargets = m_ctrl->targetCount();
    for (int ti = 0; ti < nTargets; ++ti) {
        QVector<QPointF> tpts = m_ctrl->truthTrajectory(ti, m_viewType);
        QVector<QPointF> epts = m_ctrl->estimateTrajectory(ti, m_viewType);

        // Truth — lighter color, thicker
        if (tpts.size() > 1) {
            QColor tc(SimulationController::truthColor(ti));
            p.setPen(QPen(tc, 2.0));
            QPainterPath path;
            path.moveTo(tx(tpts[0].x()), ty(tpts[0].y()));
            for (int i = 1; i < tpts.size(); ++i)
                path.lineTo(tx(tpts[i].x()), ty(tpts[i].y()));
            p.drawPath(path);

            // Start dot
            p.setBrush(tc);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(tx(tpts[0].x()), ty(tpts[0].y())), 2.5, 2.5);
        }

        // Estimate — darker color
        if (epts.size() > 1) {
            QColor ec(SimulationController::estimateColor(ti));
            p.setPen(QPen(ec, 2.0));
            p.setBrush(Qt::NoBrush);
            QPainterPath path;
            path.moveTo(tx(epts[0].x()), ty(epts[0].y()));
            for (int i = 1; i < epts.size(); ++i)
                path.lineTo(tx(epts[i].x()), ty(epts[i].y()));
            p.drawPath(path);

            // End dot
            p.setPen(Qt::NoPen);
            p.setBrush(ec);
            p.drawEllipse(QPointF(tx(epts.last().x()), ty(epts.last().y())), 2.5, 2.5);
        }
    }
}
