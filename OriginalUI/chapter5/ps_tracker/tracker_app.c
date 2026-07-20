#include "tracker_app.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define APP_PI 3.14159265358979323846

static void configure_demo_anchors(Tracker3DConfig *config) {
    config->anchor_count = 5;
    config->ref_anchor = 0;

    config->anchors[0] = (Tracker3DVec3) {0.0, 0.0, 0.0};
    config->anchors[1] = (Tracker3DVec3) {30.0, 0.0, 4.0};
    config->anchors[2] = (Tracker3DVec3) {30.0, 30.0, 0.0};
    config->anchors[3] = (Tracker3DVec3) {0.0, 30.0, 5.0};
    config->anchors[4] = (Tracker3DVec3) {15.0, 15.0, 12.0};
}

static void set_base_truth_state(DemoScene scene, double state[TRACKER3D_STATE_DIM]) {
    switch (scene) {
        case SCENE_STRAIGHT:
        // 目标参数（真值）
            state[0] = 8.0;  state[1] = 10.0; state[2] = 6.0;
            state[3] = 1.20; state[4] = 0.75; state[5] = 0.20;
            break;
        case SCENE_CLIMB:
            state[0] = 6.0;  state[1] = 8.0;  state[2] = 4.0;
            state[3] = 1.05; state[4] = 0.65; state[5] = 0.35;
            break;
        case SCENE_TURN:
            state[0] = 10.0; state[1] = 7.0;  state[2] = 5.5;
            state[3] = 1.10; state[4] = 0.25; state[5] = 0.10;
            break;
    }
}

static void set_base_guess_state(DemoScene scene, double state[TRACKER3D_STATE_DIM]) {
    switch (scene) {
        // 目标参数（初始值）
        case SCENE_STRAIGHT:
            state[0] = 6.5; state[1] = 8.0; state[2] = 4.5;
            state[3] = 0.9; state[4] = 0.4; state[5] = 0.0;
            break;
        case SCENE_CLIMB:
            state[0] = 4.5; state[1] = 6.5; state[2] = 2.5;
            state[3] = 0.8; state[4] = 0.3; state[5] = 0.1;
            break;
        case SCENE_TURN:
            state[0] = 8.0; state[1] = 6.0; state[2] = 4.0;
            state[3] = 0.9; state[4] = 0.1; state[5] = 0.0;
            break;
    }
}

static void apply_target_variant(size_t target_index, double state[TRACKER3D_STATE_DIM], int is_guess) {
    static const double pos_offsets[TRACKER_APP_MAX_TARGETS][3] = {
        {0.0, 0.0, 0.0},
        {5.5, -3.0, 1.2},
        {-4.0, 4.5, -0.9}
    };
    static const double vel_offsets[TRACKER_APP_MAX_TARGETS][3] = {
        {0.00, 0.00, 0.00},
        {-0.18, 0.22, 0.06},
        {0.16, -0.12, 0.10}
    };
    const double guess_scale = is_guess ? 1.15 : 1.0;

    state[0] += pos_offsets[target_index][0] * guess_scale;
    state[1] += pos_offsets[target_index][1] * guess_scale;
    state[2] += pos_offsets[target_index][2] * guess_scale;

    state[3] += vel_offsets[target_index][0];
    state[4] += vel_offsets[target_index][1];
    state[5] += vel_offsets[target_index][2];
}

static void init_truth_state(DemoScene scene, size_t target_index, double state[TRACKER3D_STATE_DIM]) {
    set_base_truth_state(scene, state);
    apply_target_variant(target_index, state, 0);
}

static void init_guess_state(DemoScene scene, size_t target_index, double state[TRACKER3D_STATE_DIM]) {
    set_base_guess_state(scene, state);
    apply_target_variant(target_index, state, 1);
}

static void propagate_truth(
    double state[TRACKER3D_STATE_DIM],
    double dt,
    size_t step_idx,
    size_t total_steps,
    DemoScene scene,
    size_t target_index
) {
    double phase = (2.0 * APP_PI * (double) step_idx) / (double) (total_steps > 1 ? (total_steps - 1) : 1);
    double phase_bias = (double) target_index * 0.85;
    double accel_scale = 1.0 + 0.12 * (double) target_index;
    double ax = 0.0;
    double ay = 0.0;
    double az = 0.0;

    phase += phase_bias;

    switch (scene) {
        case SCENE_STRAIGHT:
            ax = accel_scale * 0.15 * cos(phase);
            ay = accel_scale * 0.10 * sin(0.8 * phase);
            az = accel_scale * 0.06 * cos(1.2 * phase);
            break;
        case SCENE_CLIMB:
            ax = accel_scale * 0.08 * cos(0.6 * phase);
            ay = accel_scale * 0.05 * sin(phase);
            az = 0.14 + accel_scale * 0.04 * cos(phase);
            break;
        case SCENE_TURN:
            ax = -accel_scale * 0.18 * sin(phase);
            ay = accel_scale * 0.20 * cos(phase);
            az = accel_scale * 0.03 * cos(1.5 * phase);
            break;
    }

    state[3] += ax * dt;
    state[4] += ay * dt;
    state[5] += az * dt;

    state[0] += state[3] * dt;
    state[1] += state[4] * dt;
    state[2] += state[5] * dt;
}

static void apply_modality_to_config(Tracker3DConfig *config, TrackerModality modality) {
    tracker3d_set_all_modalities(config, 0, 0, 0, 0);
    switch (modality) {
        case MODALITY_TDOA:
            tracker3d_set_all_modalities(config, 1, 0, 0, 0);
            break;
        case MODALITY_TOA:
            tracker3d_set_all_modalities(config, 0, 1, 0, 0);
            break;
        case MODALITY_AOA:
            tracker3d_set_all_modalities(config, 0, 0, 1, 0);
            break;
        case MODALITY_RSS:
            tracker3d_set_all_modalities(config, 0, 0, 0, 1);
            break;
    }
}

static double app_now_ms(void) {
#ifdef _WIN32
    LARGE_INTEGER freq;
    LARGE_INTEGER counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return 1000.0 * (double) counter.QuadPart / (double) freq.QuadPart;
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return 1000.0 * (double) ts.tv_sec + (double) ts.tv_nsec / 1.0e6;
    }
    return 1000.0 * (double) clock() / (double) CLOCKS_PER_SEC;
#else
    return 1000.0 * (double) clock() / (double) CLOCKS_PER_SEC;
#endif
}

static size_t history_offset(const TrackerSimResult *result, size_t target_index, size_t step_index) {
    return ((step_index * result->target_count) + target_index) * TRACKER3D_STATE_DIM;
}

static size_t final_offset(size_t target_index) {
    return target_index * TRACKER3D_STATE_DIM;
}

void tracker_sim_default_options(TrackerSimOptions *options) {
    memset(options, 0, sizeof(*options));
    options->scene = SCENE_STRAIGHT;
    options->target_mode = TARGET_MODE_SINGLE;
    options->modality = MODALITY_TDOA;
    options->seed = (unsigned int) time(NULL);
    options->steps = 80;
    options->dt = 0.1;
}

const char *tracker_scene_name(DemoScene scene) {
    switch (scene) {
        case SCENE_STRAIGHT:
            return "straight";
        case SCENE_CLIMB:
            return "climb";
        case SCENE_TURN:
            return "turn";
        default:
            return "unknown";
    }
}

int tracker_parse_scene(const char *text, DemoScene *scene) {
    if (strcmp(text, "straight") == 0) {
        *scene = SCENE_STRAIGHT;
        return 0;
    }
    if (strcmp(text, "climb") == 0) {
        *scene = SCENE_CLIMB;
        return 0;
    }
    if (strcmp(text, "turn") == 0) {
        *scene = SCENE_TURN;
        return 0;
    }
    return -1;
}

const char *tracker_target_mode_name(TrackerTargetMode mode) {
    switch (mode) {
        case TARGET_MODE_SINGLE:
            return "single";
        case TARGET_MODE_MULTI3:
            return "multi3";
        default:
            return "unknown";
    }
}

int tracker_parse_target_mode(const char *text, TrackerTargetMode *mode) {
    if (strcmp(text, "single") == 0 || strcmp(text, "--single") == 0) {
        *mode = TARGET_MODE_SINGLE;
        return 0;
    }
    if (strcmp(text, "multi3") == 0 || strcmp(text, "multi") == 0 || strcmp(text, "--multi3") == 0 || strcmp(text, "--multi") == 0) {
        *mode = TARGET_MODE_MULTI3;
        return 0;
    }
    return -1;
}

size_t tracker_target_count_for_mode(TrackerTargetMode mode) {
    return (mode == TARGET_MODE_MULTI3) ? 3u : 1u;
}

const char *tracker_modality_name(TrackerModality modality) {
    switch (modality) {
        case MODALITY_TDOA:
            return "TDOA";
        case MODALITY_TOA:
            return "TOA";
        case MODALITY_AOA:
            return "AOA";
        case MODALITY_RSS:
            return "RSS";
        default:
            return "UNKNOWN";
    }
}

int tracker_parse_modality(const char *text, TrackerModality *modality) {
    if (strcmp(text, "tdoa") == 0 || strcmp(text, "--tdoa") == 0) {
        *modality = MODALITY_TDOA;
        return 0;
    }
    if (strcmp(text, "toa") == 0 || strcmp(text, "--toa") == 0) {
        *modality = MODALITY_TOA;
        return 0;
    }
    if (strcmp(text, "aoa") == 0 || strcmp(text, "--aoa") == 0) {
        *modality = MODALITY_AOA;
        return 0;
    }
    if (strcmp(text, "rss") == 0 || strcmp(text, "--rss") == 0) {
        *modality = MODALITY_RSS;
        return 0;
    }
    return -1;
}

size_t tracker_expected_measurement_dim_for_modality(TrackerModality modality) {
    Tracker3DConfig config;

    tracker3d_default_config(&config);
    configure_demo_anchors(&config);
    apply_modality_to_config(&config, modality);
    return tracker_expected_measurement_dim(&config);
}

size_t tracker_expected_measurement_dim(const Tracker3DConfig *config) {
    size_t dim = 0;
    size_t i;

    for (i = 0; i < config->anchor_count; ++i) {
        if (i != config->ref_anchor && config->enable_tdoa[i]) {
            ++dim;
        }
        if (config->enable_toa[i]) {
            ++dim;
        }
        if (config->enable_aoa[i]) {
            dim += 2;
        }
        if (config->enable_rss[i]) {
            ++dim;
        }
    }

    return dim;
}

const double *tracker_result_truth_at(const TrackerSimResult *result, size_t target_index, size_t step_index) {
    if (result == NULL || target_index >= result->target_count || step_index >= result->steps || result->truth_history == NULL) {
        return NULL;
    }
    return &result->truth_history[history_offset(result, target_index, step_index)];
}

const double *tracker_result_estimate_at(const TrackerSimResult *result, size_t target_index, size_t step_index) {
    if (result == NULL || target_index >= result->target_count || step_index >= result->steps || result->estimate_history == NULL) {
        return NULL;
    }
    return &result->estimate_history[history_offset(result, target_index, step_index)];
}

const double *tracker_result_final_truth_at(const TrackerSimResult *result, size_t target_index) {
    if (result == NULL || target_index >= result->target_count) {
        return NULL;
    }
    return &result->final_truth[final_offset(target_index)];
}

const double *tracker_result_final_estimate_at(const TrackerSimResult *result, size_t target_index) {
    if (result == NULL || target_index >= result->target_count) {
        return NULL;
    }
    return &result->final_estimate[final_offset(target_index)];
}

int tracker_run_simulation(const TrackerSimOptions *options, TrackerSimResult *result) {
    Tracker3DState trackers[TRACKER_APP_MAX_TARGETS];
    Tracker3DMeasurement measurements[TRACKER_APP_MAX_TARGETS];
    double truths[TRACKER_APP_MAX_TARGETS][TRACKER3D_STATE_DIM];
    double initial_guesses[TRACKER_APP_MAX_TARGETS][TRACKER3D_STATE_DIM];
    double position_se[TRACKER_APP_MAX_TARGETS];
    double velocity_se[TRACKER_APP_MAX_TARGETS];
    double avg_pos_rmse = 0.0;
    double avg_vel_rmse = 0.0;
    double start_ms;
    double end_ms;
    size_t k;
    size_t target_index;

    if (options->steps == 0) {
        return -3;
    }

    memset(result, 0, sizeof(*result));
    tracker3d_default_config(&result->config);
    configure_demo_anchors(&result->config);
    result->target_mode = options->target_mode;
    result->target_count = tracker_target_count_for_mode(options->target_mode);
    result->modality = options->modality;
    apply_modality_to_config(&result->config, options->modality);
    result->measurement_dim = tracker_expected_measurement_dim(&result->config);
    result->steps = options->steps;
    result->dt = options->dt;
    result->truth_history = (double *) calloc(options->steps * result->target_count * TRACKER3D_STATE_DIM, sizeof(double));
    result->estimate_history = (double *) calloc(options->steps * result->target_count * TRACKER3D_STATE_DIM, sizeof(double));
    if (result->truth_history == NULL || result->estimate_history == NULL) {
        tracker_free_result(result);
        return -1;
    }

    memset(position_se, 0, sizeof(position_se));
    memset(velocity_se, 0, sizeof(velocity_se));

    srand(options->seed);

    for (target_index = 0; target_index < result->target_count; ++target_index) {
        init_truth_state(options->scene, target_index, truths[target_index]);
        init_guess_state(options->scene, target_index, initial_guesses[target_index]);
        tracker3d_init_state(&trackers[target_index], initial_guesses[target_index], 16.0, 1.0);
    }

    start_ms = app_now_ms();
    for (k = 0; k < options->steps; ++k) {
        for (target_index = 0; target_index < result->target_count; ++target_index) {
            const size_t offset = history_offset(result, target_index, k);

            propagate_truth(truths[target_index], options->dt, k, options->steps, options->scene, target_index);
            tracker3d_simulate_measurement(&result->config, truths[target_index], &measurements[target_index]);

            if (tracker3d_step(&trackers[target_index], &result->config, &measurements[target_index], options->dt) != 0) {
                tracker_free_result(result);
                return -2;
            }

            memcpy(&result->truth_history[offset], truths[target_index], sizeof(double) * TRACKER3D_STATE_DIM);
            memcpy(&result->estimate_history[offset], trackers[target_index].state, sizeof(double) * TRACKER3D_STATE_DIM);

            position_se[target_index] +=
                (trackers[target_index].state[0] - truths[target_index][0]) * (trackers[target_index].state[0] - truths[target_index][0]) +
                (trackers[target_index].state[1] - truths[target_index][1]) * (trackers[target_index].state[1] - truths[target_index][1]) +
                (trackers[target_index].state[2] - truths[target_index][2]) * (trackers[target_index].state[2] - truths[target_index][2]);

            velocity_se[target_index] +=
                (trackers[target_index].state[3] - truths[target_index][3]) * (trackers[target_index].state[3] - truths[target_index][3]) +
                (trackers[target_index].state[4] - truths[target_index][4]) * (trackers[target_index].state[4] - truths[target_index][4]) +
                (trackers[target_index].state[5] - truths[target_index][5]) * (trackers[target_index].state[5] - truths[target_index][5]);
        }
    }
    end_ms = app_now_ms();

    for (target_index = 0; target_index < result->target_count; ++target_index) {
        memcpy(&result->final_truth[final_offset(target_index)], truths[target_index], sizeof(double) * TRACKER3D_STATE_DIM);
        memcpy(&result->final_estimate[final_offset(target_index)], trackers[target_index].state, sizeof(double) * TRACKER3D_STATE_DIM);

        result->target_pos_rmse[target_index] = sqrt(position_se[target_index] / (3.0 * (double) options->steps));
        result->target_vel_rmse[target_index] = sqrt(velocity_se[target_index] / (3.0 * (double) options->steps));

        avg_pos_rmse += result->target_pos_rmse[target_index];
        avg_vel_rmse += result->target_vel_rmse[target_index];
    }

    result->pos_rmse = avg_pos_rmse / (double) result->target_count;
    result->vel_rmse = avg_vel_rmse / (double) result->target_count;
    result->elapsed_ms = end_ms - start_ms;
    result->avg_step_ms = result->elapsed_ms / (double) options->steps;

    return 0;
}

void tracker_free_result(TrackerSimResult *result) {
    if (result->truth_history != NULL) {
        free(result->truth_history);
        result->truth_history = NULL;
    }
    if (result->estimate_history != NULL) {
        free(result->estimate_history);
        result->estimate_history = NULL;
    }
}
