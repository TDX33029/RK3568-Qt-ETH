#include "simulation_controller.h"
#include <QRectF>
#include <QPointF>
#include <cmath>
#include <cstring>

static void apply_modality_inline(Tracker3DConfig *c, TrackerModality m) {
    tracker3d_set_all_modalities(c, 0, 0, 0, 0);
    switch (m) {
    case MODALITY_TDOA: tracker3d_set_all_modalities(c, 1, 0, 0, 0); break;
    case MODALITY_TOA:  tracker3d_set_all_modalities(c, 0, 1, 0, 0); break;
    case MODALITY_AOA:  tracker3d_set_all_modalities(c, 0, 0, 1, 0); break;
    case MODALITY_RSS:  tracker3d_set_all_modalities(c, 0, 0, 0, 1); break;
    }
}

SimulationController::SimulationController(QObject *parent) : QObject(parent) {
    tracker_sim_default_options(&m_opts);
    m_opts.seed = 42;
    std::memset(&m_res, 0, sizeof(m_res));
}

SimulationController::~SimulationController() {
    if (m_hasResult) tracker_free_result(&m_res);
}

void SimulationController::setSceneIndex(int idx) {
    if (idx < 0 || idx > 2) return;
    if (m_opts.scene != static_cast<DemoScene>(idx)) { m_opts.scene = static_cast<DemoScene>(idx); emit configChanged(); }
}
void SimulationController::setTargetMode(int m) {
    if (m < 0 || m > 1) return;
    if (m_opts.target_mode != static_cast<TrackerTargetMode>(m)) { m_opts.target_mode = static_cast<TrackerTargetMode>(m); emit configChanged(); }
}
void SimulationController::setModalityIndex(int idx) {
    if (idx < 0 || idx > 3) return;
    if (m_opts.modality != static_cast<TrackerModality>(idx)) { m_opts.modality = static_cast<TrackerModality>(idx); emit configChanged(); }
}
void SimulationController::setSteps(int s) {
    if (s < 20) s = 20;
    if (static_cast<int>(m_opts.steps) != s) { m_opts.steps = static_cast<size_t>(s); emit configChanged(); }
}
void SimulationController::setSeed(int s) {
    if (static_cast<int>(m_opts.seed) != s) { m_opts.seed = static_cast<unsigned>(s); emit configChanged(); }
}

int SimulationController::measurementDimEstimate() const {
    Tracker3DConfig cfg;
    tracker3d_default_config(&cfg);
    cfg.anchor_count = 5; cfg.ref_anchor = 0;
    apply_modality_inline(&cfg, m_opts.modality);
    return static_cast<int>(tracker_expected_measurement_dim(&cfg));
}

QStringList SimulationController::sceneNames() const { return {"Straight", "Climb", "Turn"}; }
QStringList SimulationController::targetModeNames() const { return {"Single", "Multi-3"}; }
QStringList SimulationController::modalityNames() const { return {"TDOA", "TOA", "AOA", "RSS"}; }

int SimulationController::targetCount() const { return m_hasResult ? static_cast<int>(m_res.target_count) : 0; }
int SimulationController::stepCount() const { return m_hasResult ? static_cast<int>(m_res.steps) : 0; }
double SimulationController::posRmse() const { return m_hasResult ? m_res.pos_rmse : 0.0; }
double SimulationController::velRmse() const { return m_hasResult ? m_res.vel_rmse : 0.0; }
double SimulationController::elapsedMs() const { return m_hasResult ? m_res.elapsed_ms : 0.0; }
double SimulationController::avgStepMs() const { return m_hasResult ? m_res.avg_step_ms : 0.0; }
int SimulationController::measurementDim() const { return m_hasResult ? static_cast<int>(m_res.measurement_dim) : 0; }

double SimulationController::targetPosRmse(int ti) const {
    if (!m_hasResult || ti < 0 || static_cast<size_t>(ti) >= m_res.target_count) return 0.0;
    return m_res.target_pos_rmse[ti];
}
double SimulationController::targetVelRmse(int ti) const {
    if (!m_hasResult || ti < 0 || static_cast<size_t>(ti) >= m_res.target_count) return 0.0;
    return m_res.target_vel_rmse[ti];
}

void SimulationController::projectIso(const double st[TRACKER3D_STATE_DIM], double *px, double *py) const {
    constexpr double c = 0.8660254037844386, s = 0.5;
    *px = (st[0] - st[1]) * c;
    *py = st[2] - (st[0] + st[1]) * s;
}

QVector<QPointF> SimulationController::truthTrajectory(int ti, int vt) const {
    QVector<QPointF> pts;
    if (!m_hasResult || ti < 0 || static_cast<size_t>(ti) >= m_res.target_count) return pts;
    pts.reserve(static_cast<qsizetype>(m_res.steps));
    for (size_t k = 0; k < m_res.steps; ++k) {
        const double *s = tracker_result_truth_at(&m_res, static_cast<size_t>(ti), k);
        if (!s) continue;
        if (vt == 0) pts.append(QPointF(s[0], s[1]));
        else if (vt == 1) pts.append(QPointF(s[0], s[2]));
        else { double px, py; projectIso(s, &px, &py); pts.append(QPointF(px, py)); }
    }
    return pts;
}

QVector<QPointF> SimulationController::estimateTrajectory(int ti, int vt) const {
    QVector<QPointF> pts;
    if (!m_hasResult || ti < 0 || static_cast<size_t>(ti) >= m_res.target_count) return pts;
    pts.reserve(static_cast<qsizetype>(m_res.steps));
    for (size_t k = 0; k < m_res.steps; ++k) {
        const double *s = tracker_result_estimate_at(&m_res, static_cast<size_t>(ti), k);
        if (!s) continue;
        if (vt == 0) pts.append(QPointF(s[0], s[1]));
        else if (vt == 1) pts.append(QPointF(s[0], s[2]));
        else { double px, py; projectIso(s, &px, &py); pts.append(QPointF(px, py)); }
    }
    return pts;
}

QRectF SimulationController::dataBounds(int vt) const {
    if (!m_hasResult) return QRectF(0, 0, 1, 1);
    double mnx = 1e30, mxx = -1e30, mny = 1e30, mxy = -1e30;
    for (size_t ti = 0; ti < m_res.target_count; ++ti) {
        for (size_t k = 0; k < m_res.steps; ++k) {
            const double *t = tracker_result_truth_at(&m_res, ti, k);
            const double *e = tracker_result_estimate_at(&m_res, ti, k);
            if (!t || !e) continue;
            double tx, ty, ex, ey;
            if (vt == 0) { tx=t[0];ty=t[1]; ex=e[0];ey=e[1]; }
            else if (vt == 1) { tx=t[0];ty=t[2]; ex=e[0];ey=e[2]; }
            else { projectIso(t,&tx,&ty); projectIso(e,&ex,&ey); }
            if (tx<mnx) mnx=tx; if (tx>mxx) mxx=tx;
            if (ty<mny) mny=ty; if (ty>mxy) mxy=ty;
            if (ex<mnx) mnx=ex; if (ex>mxx) mxx=ex;
            if (ey<mny) mny=ey; if (ey>mxy) mxy=ey;
        }
    }
    double sx = mxx - mnx; if (sx < 1e-6) sx = 1.0;
    double sy = mxy - mny; if (sy < 1e-6) sy = 1.0;
    return QRectF(mnx - 0.1*sx, mny - 0.1*sy, sx*1.2, sy*1.2);
}

unsigned int SimulationController::truthColor(int ti) {
    static const unsigned int cs[] = {0xFF8DBBEA, 0xFF9BD6A8, 0xFFF0C27A};
    return cs[ti % 3];
}
unsigned int SimulationController::estimateColor(int ti) {
    static const unsigned int cs[] = {0xFF2A75BB, 0xFF2E8B57, 0xFFC44E35};
    return cs[ti % 3];
}

void SimulationController::runSimulation() {
    if (m_hasResult) { tracker_free_result(&m_res); m_hasResult = false; }
    std::memset(&m_res, 0, sizeof(m_res));
    if (tracker_run_simulation(&m_opts, &m_res) == 0) {
        m_hasResult = true;
        emit resultReady();
    }
}
