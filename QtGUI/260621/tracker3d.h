#ifndef TRACKER3D_H
#define TRACKER3D_H

#include <cstddef>
#include <cstdint>

constexpr size_t TRACKER3D_STATE_DIM = 6;
constexpr size_t TRACKER3D_MAX_ANCHORS = 8;
constexpr size_t TRACKER3D_MAX_MEAS_DIM =
    TRACKER3D_MAX_ANCHORS + (TRACKER3D_MAX_ANCHORS - 1) +
    2 * TRACKER3D_MAX_ANCHORS + TRACKER3D_MAX_ANCHORS;

struct Tracker3DVec3 { double x = 0, y = 0, z = 0; };

struct Tracker3DConfig {
    size_t anchor_count = 0;
    size_t ref_anchor = 0;
    Tracker3DVec3 anchors[TRACKER3D_MAX_ANCHORS];

    uint8_t enable_tdoa[TRACKER3D_MAX_ANCHORS] = {};
    uint8_t enable_toa[TRACKER3D_MAX_ANCHORS] = {};
    uint8_t enable_aoa[TRACKER3D_MAX_ANCHORS] = {};
    uint8_t enable_rss[TRACKER3D_MAX_ANCHORS] = {};

    double light_speed = 299792458.0;
    double process_accel_std = 0.8;
    double tdoa_std_sec = 2.0e-9;
    double toa_std_sec = 2.0e-9;
    double aoa_std_rad = 1.7453292519943295e-2;
    double rss_std_db = 2.0;
    double rss_ref_dbm = -35.0;
    double rss_path_loss_exp = 2.0;
};

struct Tracker3DState {
    double state[TRACKER3D_STATE_DIM] = {};
    double covariance[TRACKER3D_STATE_DIM * TRACKER3D_STATE_DIM] = {};
};

struct Tracker3DMeasurement {
    double tdoa[TRACKER3D_MAX_ANCHORS] = {};
    uint8_t has_tdoa[TRACKER3D_MAX_ANCHORS] = {};
    double toa[TRACKER3D_MAX_ANCHORS] = {};
    uint8_t has_toa[TRACKER3D_MAX_ANCHORS] = {};
    double aoa_az[TRACKER3D_MAX_ANCHORS] = {};
    double aoa_el[TRACKER3D_MAX_ANCHORS] = {};
    uint8_t has_aoa[TRACKER3D_MAX_ANCHORS] = {};
    double rss[TRACKER3D_MAX_ANCHORS] = {};
    uint8_t has_rss[TRACKER3D_MAX_ANCHORS] = {};
};

void tracker3d_default_config(Tracker3DConfig *config);
void tracker3d_set_all_modalities(Tracker3DConfig *config, int use_tdoa, int use_toa, int use_aoa, int use_rss);
void tracker3d_clear_measurement(Tracker3DMeasurement *measurement);
void tracker3d_init_state(Tracker3DState *tracker, const double initial_state[TRACKER3D_STATE_DIM], double pos_var, double vel_var);
size_t tracker3d_measurement_dim(const Tracker3DConfig *config, const Tracker3DMeasurement *measurement);
int tracker3d_predict(Tracker3DState *tracker, const Tracker3DConfig *config, double dt);
int tracker3d_update(Tracker3DState *tracker, const Tracker3DConfig *config, const Tracker3DMeasurement *measurement);
int tracker3d_step(Tracker3DState *tracker, const Tracker3DConfig *config, const Tracker3DMeasurement *measurement, double dt);
void tracker3d_simulate_measurement(const Tracker3DConfig *config, const double truth[TRACKER3D_STATE_DIM], Tracker3DMeasurement *m);

#endif
