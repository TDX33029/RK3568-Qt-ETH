#ifndef SIMULATION_CONTROLLER_H
#define SIMULATION_CONTROLLER_H

#include "tracker_app.h"

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QVector>
#include <QStringList>

class SimulationController : public QObject {
    Q_OBJECT

public:
    explicit SimulationController(QObject *parent = nullptr);
    ~SimulationController() override;

    // Configuration
    int sceneIndex() const { return static_cast<int>(m_opts.scene); }
    void setSceneIndex(int idx);

    int targetMode() const { return static_cast<int>(m_opts.target_mode); }
    void setTargetMode(int m);

    int modalityIndex() const { return static_cast<int>(m_opts.modality); }
    void setModalityIndex(int idx);

    int steps() const { return static_cast<int>(m_opts.steps); }
    void setSteps(int s);

    int seed() const { return static_cast<int>(m_opts.seed); }
    void setSeed(int s);

    int measurementDimEstimate() const;

    QStringList sceneNames() const;
    QStringList targetModeNames() const;
    QStringList modalityNames() const;

    // Result access
    bool hasResult() const { return m_hasResult; }
    int targetCount() const;
    int stepCount() const;
    double posRmse() const;
    double velRmse() const;
    double elapsedMs() const;
    double avgStepMs() const;
    int measurementDim() const;
    double targetPosRmse(int ti) const;
    double targetVelRmse(int ti) const;

    // Trajectory data for chart views: 0=XY, 1=XZ, 2=3D-iso
    QVector<QPointF> truthTrajectory(int targetIndex, int viewType) const;
    QVector<QPointF> estimateTrajectory(int targetIndex, int viewType) const;
    QRectF dataBounds(int viewType) const;

    static unsigned int truthColor(int ti);
    static unsigned int estimateColor(int ti);

public slots:
    void runSimulation();

signals:
    void configChanged();
    void resultReady();

private:
    void projectIso(const double s[TRACKER3D_STATE_DIM], double *px, double *py) const;

    TrackerSimOptions m_opts;
    TrackerSimResult m_res;
    bool m_hasResult = false;
};

#endif
