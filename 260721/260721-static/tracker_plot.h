#ifndef TRACKER_PLOT_H
#define TRACKER_PLOT_H

#include <QWidget>
#include "tracker_app.h"

class TrackerPlot : public QWidget {
    Q_OBJECT
public:
    enum ViewMode { ViewXY, ViewXZ, View3D };

    explicit TrackerPlot(QWidget *parent = nullptr);

    void setResult(const TrackerSimResult *result) { m_result = result; update(); }
    void setViewMode(ViewMode mode) { m_mode = mode; update(); }
    ViewMode viewMode() const { return m_mode; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void projectIso(const double state[TRACKER3D_STATE_DIM], double *px, double *py) const;

    const TrackerSimResult *m_result = nullptr;
    ViewMode m_mode = ViewXY;
};

#endif // TRACKER_PLOT_H
