#ifndef TRACKER3D_H
#define TRACKER3D_H

#include <stddef.h>
#include <stdint.h>

#define TRACKER3D_STATE_DIM 6
#define TRACKER3D_MAX_ANCHORS 8
#define TRACKER3D_MAX_MEAS_DIM (TRACKER3D_MAX_ANCHORS + (TRACKER3D_MAX_ANCHORS - 1) + 2 * TRACKER3D_MAX_ANCHORS + TRACKER3D_MAX_ANCHORS)

typedef struct {
    double x;
    double y;
    double z;
} Tracker3DVec3;

typedef struct {
    size_t anchor_count;
    size_t ref_anchor;
    Tracker3DVec3 anchors[TRACKER3D_MAX_ANCHORS];

    uint8_t enable_tdoa[TRACKER3D_MAX_ANCHORS];
    uint8_t enable_toa[TRACKER3D_MAX_ANCHORS];
    uint8_t enable_aoa[TRACKER3D_MAX_ANCHORS];
    uint8_t enable_rss[TRACKER3D_MAX_ANCHORS];

    double light_speed;
    double process_accel_std;

    double tdoa_std_sec;
    double toa_std_sec;
    double aoa_std_rad;
    double rss_std_db;

    double rss_ref_dbm;
    double rss_path_loss_exp;
} Tracker3DConfig;

typedef struct {
    double state[TRACKER3D_STATE_DIM];
    double covariance[TRACKER3D_STATE_DIM * TRACKER3D_STATE_DIM];
} Tracker3DState;

typedef struct {
    double tdoa[TRACKER3D_MAX_ANCHORS];
    uint8_t has_tdoa[TRACKER3D_MAX_ANCHORS];

    double toa[TRACKER3D_MAX_ANCHORS];
    uint8_t has_toa[TRACKER3D_MAX_ANCHORS];

    double aoa_az[TRACKER3D_MAX_ANCHORS];
    double aoa_el[TRACKER3D_MAX_ANCHORS];
    uint8_t has_aoa[TRACKER3D_MAX_ANCHORS];

    double rss[TRACKER3D_MAX_ANCHORS];
    uint8_t has_rss[TRACKER3D_MAX_ANCHORS];
} Tracker3DMeasurement;

void tracker3d_default_config(Tracker3DConfig *config);
void tracker3d_set_all_modalities(Tracker3DConfig *config, int use_tdoa, int use_toa, int use_aoa, int use_rss);
void tracker3d_clear_measurement(Tracker3DMeasurement *measurement);
void tracker3d_init_state(Tracker3DState *tracker, const double initial_state[TRACKER3D_STATE_DIM], double pos_var, double vel_var);

size_t tracker3d_measurement_dim(const Tracker3DConfig *config, const Tracker3DMeasurement *measurement);
int tracker3d_predict(Tracker3DState *tracker, const Tracker3DConfig *config, double dt);
int tracker3d_update(Tracker3DState *tracker, const Tracker3DConfig *config, const Tracker3DMeasurement *measurement);
int tracker3d_step(Tracker3DState *tracker, const Tracker3DConfig *config, const Tracker3DMeasurement *measurement, double dt);

void tracker3d_simulate_measurement(
    const Tracker3DConfig *config,
    const double truth_state[TRACKER3D_STATE_DIM],
    Tracker3DMeasurement *measurement
);

#endif
