#ifndef TRAJECTORY_CHART_H
#define TRAJECTORY_CHART_H

#include <QWidget>
#include <QVector>
#include <QPointF>
#include <QRectF>

class SimulationController;

class TrajectoryChart : public QWidget {
    Q_OBJECT
public:
    enum ViewType { XY = 0, XZ = 1, Iso3D = 2 };

    explicit TrajectoryChart(ViewType viewType, QWidget *parent = nullptr);

    void setController(SimulationController *ctrl);
    void refresh();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    ViewType m_viewType;
    SimulationController *m_ctrl = nullptr;
};

#endif
