#include "tracker3d.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TRACKER3D_EPS 1e-9
#define TRACKER3D_PI 3.14159265358979323846

typedef enum {
    OBS_TDOA = 0,
    OBS_TOA = 1,
    OBS_AOA_AZ = 2,
    OBS_AOA_EL = 3,
    OBS_RSS = 4
} ObservationKind;

typedef struct {
    ObservationKind kind;
    size_t anchor;
} ObservationDescriptor;

static double wrap_angle(double angle) {
    while (angle > TRACKER3D_PI) {
        angle -= 2.0 * TRACKER3D_PI;
    }
    while (angle < -TRACKER3D_PI) {
        angle += 2.0 * TRACKER3D_PI;
    }
    return angle;
}

static double gaussian_noise(double stddev) {
    double u1 = ((double) rand() + 1.0) / ((double) RAND_MAX + 2.0);
    double u2 = ((double) rand() + 1.0) / ((double) RAND_MAX + 2.0);
    double mag = sqrt(-2.0 * log(u1));
    return stddev * mag * cos(2.0 * TRACKER3D_PI * u2);
}

static void set_identity6(double out[TRACKER3D_STATE_DIM * TRACKER3D_STATE_DIM]) {
    size_t i;
    memset(out, 0, sizeof(double) * TRACKER3D_STATE_DIM * TRACKER3D_STATE_DIM);
    for (i = 0; i < TRACKER3D_STATE_DIM; ++i) {
        out[i * TRACKER3D_STATE_DIM + i] = 1.0;
    }
}

static void mat6_mul(const double *a, const double *b, double *out) {
    size_t r;
    size_t c;
    size_t k;
    for (r = 0; r < TRACKER3D_STATE_DIM; ++r) {
        for (c = 0; c < TRACKER3D_STATE_DIM; ++c) {
            double sum = 0.0;
            for (k = 0; k < TRACKER3D_STATE_DIM; ++k) {
                sum += a[r * TRACKER3D_STATE_DIM + k] * b[k * TRACKER3D_STATE_DIM + c];
            }
            out[r * TRACKER3D_STATE_DIM + c] = sum;
        }
    }
}

static void mat6_transpose(const double *in, double *out) {
    size_t r;
    size_t c;
    for (r = 0; r < TRACKER3D_STATE_DIM; ++r) {
        for (c = 0; c < TRACKER3D_STATE_DIM; ++c) {
            out[c * TRACKER3D_STATE_DIM + r] = in[r * TRACKER3D_STATE_DIM + c];
        }
    }
}

static void compute_anchor_geometry(
    const double state[TRACKER3D_STATE_DIM],
    const Tracker3DVec3 *anchor,
    double *dx,
    double *dy,
    double *dz,
    double *rho,
    double *range
) {
    *dx = state[0] - anchor->x;
    *dy = state[1] - anchor->y;
    *dz = state[2] - anchor->z;
    *rho = sqrt((*dx) * (*dx) + (*dy) * (*dy));
    *range = sqrt((*rho) * (*rho) + (*dz) * (*dz));

    if (*rho < TRACKER3D_EPS) {
        *rho = TRACKER3D_EPS;
    }
    if (*range < TRACKER3D_EPS) {
        *range = TRACKER3D_EPS;
    }
}

static size_t build_descriptors(
    const Tracker3DConfig *config,
    const Tracker3DMeasurement *measurement,
    ObservationDescriptor *descriptors
) {
    size_t count = 0;
    size_t i;

    for (i = 0; i < config->anchor_count; ++i) {
        if (i == config->ref_anchor) {
            continue;
        }
        if (config->enable_tdoa[i] && measurement->has_tdoa[i]) {
            descriptors[count].kind = OBS_TDOA;
            descriptors[count].anchor = i;
            ++count;
        }
    }

    for (i = 0; i < config->anchor_count; ++i) {
        if (config->enable_toa[i] && measurement->has_toa[i]) {
            descriptors[count].kind = OBS_TOA;
            descriptors[count].anchor = i;
            ++count;
        }
    }

    for (i = 0; i < config->anchor_count; ++i) {
        if (config->enable_aoa[i] && measurement->has_aoa[i]) {
            descriptors[count].kind = OBS_AOA_AZ;
            descriptors[count].anchor = i;
            ++count;

            descriptors[count].kind = OBS_AOA_EL;
            descriptors[count].anchor = i;
            ++count;
        }
    }

    for (i = 0; i < config->anchor_count; ++i) {
        if (config->enable_rss[i] && measurement->has_rss[i]) {
            descriptors[count].kind = OBS_RSS;
            descriptors[count].anchor = i;
            ++count;
        }
    }

    return count;
}

static void fill_single_measurement_model(
    const Tracker3DConfig *config,
    const Tracker3DMeasurement *measurement,
    const double state[TRACKER3D_STATE_DIM],
    const ObservationDescriptor *descriptor,
    double *z,
    double *h,
    double h_row[TRACKER3D_STATE_DIM],
    double *variance
) {
    size_t anchor_idx = descriptor->anchor;
    const Tracker3DVec3 *anchor = &config->anchors[anchor_idx];
    double dx = 0.0;
    double dy = 0.0;
    double dz = 0.0;
    double rho = 0.0;
    double range = 0.0;

    memset(h_row, 0, sizeof(double) * TRACKER3D_STATE_DIM);
    compute_anchor_geometry(state, anchor, &dx, &dy, &dz, &rho, &range);

    switch (descriptor->kind) {
        case OBS_TDOA: {
            const Tracker3DVec3 *ref_anchor = &config->anchors[config->ref_anchor];
            double ref_dx = 0.0;
            double ref_dy = 0.0;
            double ref_dz = 0.0;
            double ref_rho = 0.0;
            double ref_range = 0.0;

            compute_anchor_geometry(state, ref_anchor, &ref_dx, &ref_dy, &ref_dz, &ref_rho, &ref_range);

            *z = measurement->tdoa[anchor_idx] * config->light_speed;
            *h = range - ref_range;
            h_row[0] = dx / range - ref_dx / ref_range;
            h_row[1] = dy / range - ref_dy / ref_range;
            h_row[2] = dz / range - ref_dz / ref_range;
            *variance = (config->tdoa_std_sec * config->light_speed) * (config->tdoa_std_sec * config->light_speed);
            break;
        }

        case OBS_TOA:
            *z = measurement->toa[anchor_idx] * config->light_speed;
            *h = range;
            h_row[0] = dx / range;
            h_row[1] = dy / range;
            h_row[2] = dz / range;
            *variance = (config->toa_std_sec * config->light_speed) * (config->toa_std_sec * config->light_speed);
            break;

        case OBS_AOA_AZ:
            *z = measurement->aoa_az[anchor_idx];
            *h = atan2(dy, dx);
            h_row[0] = -dy / (rho * rho);
            h_row[1] = dx / (rho * rho);
            h_row[2] = 0.0;
            *variance = config->aoa_std_rad * config->aoa_std_rad;
            break;

        case OBS_AOA_EL: {
            double range_sq = range * range;
            *z = measurement->aoa_el[anchor_idx];
            *h = atan2(dz, rho);
            h_row[0] = -(dx * dz) / (rho * range_sq);
            h_row[1] = -(dy * dz) / (rho * range_sq);
            h_row[2] = rho / range_sq;
            *variance = config->aoa_std_rad * config->aoa_std_rad;
            break;
        }

        case OBS_RSS: {
            double coeff = -10.0 * config->rss_path_loss_exp / log(10.0);
            *z = measurement->rss[anchor_idx];
            *h = config->rss_ref_dbm - 10.0 * config->rss_path_loss_exp * log10(range);
            h_row[0] = coeff * dx / (range * range);
            h_row[1] = coeff * dy / (range * range);
            h_row[2] = coeff * dz / (range * range);
            *variance = config->rss_std_db * config->rss_std_db;
            break;
        }
    }
}

void tracker3d_default_config(Tracker3DConfig *config) {
    memset(config, 0, sizeof(*config));

    config->anchor_count = 0;
    config->ref_anchor = 0;
    config->light_speed = 299792458.0;
    config->process_accel_std = 0.8;

    config->tdoa_std_sec = 2.0e-9;
    config->toa_std_sec = 2.0e-9;
    config->aoa_std_rad = 1.0 * TRACKER3D_PI / 180.0;
    config->rss_std_db = 2.0;

    config->rss_ref_dbm = -35.0;
    config->rss_path_loss_exp = 2.0;
}

void tracker3d_set_all_modalities(Tracker3DConfig *config, int use_tdoa, int use_toa, int use_aoa, int use_rss) {
    size_t i;
    for (i = 0; i < config->anchor_count; ++i) {
        config->enable_tdoa[i] = (uint8_t) (use_tdoa ? 1 : 0);
        config->enable_toa[i] = (uint8_t) (use_toa ? 1 : 0);
        config->enable_aoa[i] = (uint8_t) (use_aoa ? 1 : 0);
        config->enable_rss[i] = (uint8_t) (use_rss ? 1 : 0);
    }

    if (config->ref_anchor < config->anchor_count) {
        config->enable_tdoa[config->ref_anchor] = 0;
    }
}

void tracker3d_clear_measurement(Tracker3DMeasurement *measurement) {
    memset(measurement, 0, sizeof(*measurement));
}

void tracker3d_init_state(Tracker3DState *tracker, const double initial_state[TRACKER3D_STATE_DIM], double pos_var, double vel_var) {
    size_t i;

    memcpy(tracker->state, initial_state, sizeof(double) * TRACKER3D_STATE_DIM);
    memset(tracker->covariance, 0, sizeof(tracker->covariance));

    for (i = 0; i < 3; ++i) {
        tracker->covariance[i * TRACKER3D_STATE_DIM + i] = pos_var;
        tracker->covariance[(i + 3) * TRACKER3D_STATE_DIM + (i + 3)] = vel_var;
    }
}

size_t tracker3d_measurement_dim(const Tracker3DConfig *config, const Tracker3DMeasurement *measurement) {
    ObservationDescriptor descriptors[TRACKER3D_MAX_MEAS_DIM];
    return build_descriptors(config, measurement, descriptors);
}

int tracker3d_predict(Tracker3DState *tracker, const Tracker3DConfig *config, double dt) {
    double f[TRACKER3D_STATE_DIM * TRACKER3D_STATE_DIM];
    double ft[TRACKER3D_STATE_DIM * TRACKER3D_STATE_DIM];
    double temp[TRACKER3D_STATE_DIM * TRACKER3D_STATE_DIM];
    double predicted_cov[TRACKER3D_STATE_DIM * TRACKER3D_STATE_DIM];
    double q[TRACKER3D_STATE_DIM * TRACKER3D_STATE_DIM];
    double predicted_state[TRACKER3D_STATE_DIM];
    double sigma_a_sq = config->process_accel_std * config->process_accel_std;
    double dt2 = dt * dt;
    double dt3 = dt2 * dt;
    double dt4 = dt2 * dt2;
    size_t axis;
    size_t i;

    set_identity6(f);
    f[0 * TRACKER3D_STATE_DIM + 3] = dt;
    f[1 * TRACKER3D_STATE_DIM + 4] = dt;
    f[2 * TRACKER3D_STATE_DIM + 5] = dt;

    memset(q, 0, sizeof(q));
    for (axis = 0; axis < 3; ++axis) {
        size_t pos_idx = axis;
        size_t vel_idx = axis + 3;
        q[pos_idx * TRACKER3D_STATE_DIM + pos_idx] = 0.25 * dt4 * sigma_a_sq;
        q[pos_idx * TRACKER3D_STATE_DIM + vel_idx] = 0.5 * dt3 * sigma_a_sq;
        q[vel_idx * TRACKER3D_STATE_DIM + pos_idx] = 0.5 * dt3 * sigma_a_sq;
        q[vel_idx * TRACKER3D_STATE_DIM + vel_idx] = dt2 * sigma_a_sq;
    }

    for (i = 0; i < TRACKER3D_STATE_DIM; ++i) {
        predicted_state[i] =
            f[i * TRACKER3D_STATE_DIM + 0] * tracker->state[0] +
            f[i * TRACKER3D_STATE_DIM + 1] * tracker->state[1] +
            f[i * TRACKER3D_STATE_DIM + 2] * tracker->state[2] +
            f[i * TRACKER3D_STATE_DIM + 3] * tracker->state[3] +
            f[i * TRACKER3D_STATE_DIM + 4] * tracker->state[4] +
            f[i * TRACKER3D_STATE_DIM + 5] * tracker->state[5];
    }

    mat6_transpose(f, ft);
    mat6_mul(f, tracker->covariance, temp);
    mat6_mul(temp, ft, predicted_cov);

    for (i = 0; i < TRACKER3D_STATE_DIM * TRACKER3D_STATE_DIM; ++i) {
        tracker->covariance[i] = predicted_cov[i] + q[i];
    }

    memcpy(tracker->state, predicted_state, sizeof(predicted_state));
    return 0;
}

int tracker3d_update(Tracker3DState *tracker, const Tracker3DConfig *config, const Tracker3DMeasurement *measurement) {
    ObservationDescriptor descriptors[TRACKER3D_MAX_MEAS_DIM];
    size_t dim;
    size_t r;
    size_t c;
    size_t k;

    dim = build_descriptors(config, measurement, descriptors);
    if (dim == 0) {
        return 0;
    }

    for (k = 0; k < dim; ++k) {
        double z = 0.0;
        double h = 0.0;
        double h_row[TRACKER3D_STATE_DIM];
        double variance = 0.0;
        double pht[TRACKER3D_STATE_DIM];
        double k_gain[TRACKER3D_STATE_DIM];
        double kh[TRACKER3D_STATE_DIM * TRACKER3D_STATE_DIM];
        double a_mat[TRACKER3D_STATE_DIM * TRACKER3D_STATE_DIM];
        double temp6[TRACKER3D_STATE_DIM * TRACKER3D_STATE_DIM];
        double temp6b[TRACKER3D_STATE_DIM * TRACKER3D_STATE_DIM];
        double krkt[TRACKER3D_STATE_DIM * TRACKER3D_STATE_DIM];
        double innovation = 0.0;
        double s = 0.0;

        fill_single_measurement_model(
            config,
            measurement,
            tracker->state,
            &descriptors[k],
            &z,
            &h,
            h_row,
            &variance
        );

        innovation = z - h;
        if (descriptors[k].kind == OBS_AOA_AZ || descriptors[k].kind == OBS_AOA_EL) {
            innovation = wrap_angle(innovation);
        }

        for (r = 0; r < TRACKER3D_STATE_DIM; ++r) {
            double sum = 0.0;
            for (c = 0; c < TRACKER3D_STATE_DIM; ++c) {
                sum += tracker->covariance[r * TRACKER3D_STATE_DIM + c] * h_row[c];
            }
            pht[r] = sum;
        }

        s = variance;
        for (r = 0; r < TRACKER3D_STATE_DIM; ++r) {
            s += h_row[r] * pht[r];
        }

        if (!isfinite(s) || s < 1e-12) {
            return -3;
        }

        for (r = 0; r < TRACKER3D_STATE_DIM; ++r) {
            k_gain[r] = pht[r] / s;
            tracker->state[r] += k_gain[r] * innovation;
        }

        memset(kh, 0, sizeof(kh));
        for (r = 0; r < TRACKER3D_STATE_DIM; ++r) {
            for (c = 0; c < TRACKER3D_STATE_DIM; ++c) {
                kh[r * TRACKER3D_STATE_DIM + c] = k_gain[r] * h_row[c];
            }
        }

        set_identity6(a_mat);
        for (r = 0; r < TRACKER3D_STATE_DIM * TRACKER3D_STATE_DIM; ++r) {
            a_mat[r] -= kh[r];
        }

        mat6_mul(a_mat, tracker->covariance, temp6);
        {
            double a_transpose[TRACKER3D_STATE_DIM * TRACKER3D_STATE_DIM];
            mat6_transpose(a_mat, a_transpose);
            mat6_mul(temp6, a_transpose, temp6b);
        }

        for (r = 0; r < TRACKER3D_STATE_DIM; ++r) {
            for (c = 0; c < TRACKER3D_STATE_DIM; ++c) {
                krkt[r * TRACKER3D_STATE_DIM + c] = k_gain[r] * variance * k_gain[c];
            }
        }

        for (r = 0; r < TRACKER3D_STATE_DIM * TRACKER3D_STATE_DIM; ++r) {
            tracker->covariance[r] = temp6b[r] + krkt[r];
        }

        for (r = 0; r < TRACKER3D_STATE_DIM; ++r) {
            for (c = r + 1; c < TRACKER3D_STATE_DIM; ++c) {
                double sym = 0.5 * (tracker->covariance[r * TRACKER3D_STATE_DIM + c] + tracker->covariance[c * TRACKER3D_STATE_DIM + r]);
                tracker->covariance[r * TRACKER3D_STATE_DIM + c] = sym;
                tracker->covariance[c * TRACKER3D_STATE_DIM + r] = sym;
            }
        }
    }

    return 0;
}

int tracker3d_step(Tracker3DState *tracker, const Tracker3DConfig *config, const Tracker3DMeasurement *measurement, double dt) {
    if (tracker3d_predict(tracker, config, dt) != 0) {
        return -1;
    }
    return tracker3d_update(tracker, config, measurement);
}

void tracker3d_simulate_measurement(
    const Tracker3DConfig *config,
    const double truth_state[TRACKER3D_STATE_DIM],
    Tracker3DMeasurement *measurement
) {
    size_t i;
    double ref_dx = 0.0;
    double ref_dy = 0.0;
    double ref_dz = 0.0;
    double ref_rho = 0.0;
    double ref_range = 0.0;

    tracker3d_clear_measurement(measurement);
    compute_anchor_geometry(truth_state, &config->anchors[config->ref_anchor], &ref_dx, &ref_dy, &ref_dz, &ref_rho, &ref_range);

    for (i = 0; i < config->anchor_count; ++i) {
        double dx = 0.0;
        double dy = 0.0;
        double dz = 0.0;
        double rho = 0.0;
        double range = 0.0;

        compute_anchor_geometry(truth_state, &config->anchors[i], &dx, &dy, &dz, &rho, &range);

        if (config->enable_tdoa[i] && i != config->ref_anchor) {
            measurement->has_tdoa[i] = 1;
            measurement->tdoa[i] = (range - ref_range) / config->light_speed + gaussian_noise(config->tdoa_std_sec);
        }

        if (config->enable_toa[i]) {
            measurement->has_toa[i] = 1;
            measurement->toa[i] = range / config->light_speed + gaussian_noise(config->toa_std_sec);
        }

        if (config->enable_aoa[i]) {
            measurement->has_aoa[i] = 1;
            measurement->aoa_az[i] = atan2(dy, dx) + gaussian_noise(config->aoa_std_rad);
            measurement->aoa_el[i] = atan2(dz, rho) + gaussian_noise(config->aoa_std_rad);
        }

        if (config->enable_rss[i]) {
            measurement->has_rss[i] = 1;
            measurement->rss[i] = config->rss_ref_dbm - 10.0 * config->rss_path_loss_exp * log10(range) + gaussian_noise(config->rss_std_db);
        }
    }
}
