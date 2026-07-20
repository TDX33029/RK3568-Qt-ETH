#ifndef TRACKER_APP_H
#define TRACKER_APP_H

#include "tracker3d.h"
#include <cstddef>

constexpr size_t TRACKER_APP_MAX_TARGETS = 3;

enum DemoScene { SCENE_STRAIGHT = 0, SCENE_CLIMB = 1, SCENE_TURN = 2 };
enum TrackerModality { MODALITY_TDOA = 0, MODALITY_TOA = 1, MODALITY_AOA = 2, MODALITY_RSS = 3 };
enum TrackerTargetMode { TARGET_MODE_SINGLE = 0, TARGET_MODE_MULTI3 = 1 };

struct TrackerSimOptions {
    DemoScene scene = SCENE_STRAIGHT;
    TrackerTargetMode target_mode = TARGET_MODE_SINGLE;
    TrackerModality modality = MODALITY_TDOA;
    unsigned int seed = 1;
    size_t steps = 80;
    double dt = 0.1;
};

struct TrackerSimResult {
    Tracker3DConfig config;
    TrackerTargetMode target_mode = TARGET_MODE_SINGLE;
    size_t target_count = 0;
    TrackerModality modality = MODALITY_TDOA;
    size_t measurement_dim = 0;
    size_t steps = 0;
    double dt = 0.0;
    double elapsed_ms = 0.0;
    double avg_step_ms = 0.0;
    double pos_rmse = 0.0;
    double vel_rmse = 0.0;
    double target_pos_rmse[TRACKER_APP_MAX_TARGETS] = {};
    double target_vel_rmse[TRACKER_APP_MAX_TARGETS] = {};
    double final_truth[TRACKER_APP_MAX_TARGETS * TRACKER3D_STATE_DIM] = {};
    double final_estimate[TRACKER_APP_MAX_TARGETS * TRACKER3D_STATE_DIM] = {};
    double *truth_history = nullptr;
    double *estimate_history = nullptr;
};

void tracker_sim_default_options(TrackerSimOptions *options);
const char *tracker_scene_name(DemoScene scene);
const char *tracker_target_mode_name(TrackerTargetMode mode);
const char *tracker_modality_name(TrackerModality modality);
size_t tracker_target_count_for_mode(TrackerTargetMode mode);
size_t tracker_expected_measurement_dim(const Tracker3DConfig *config);

const double *tracker_result_truth_at(const TrackerSimResult *result, size_t target_index, size_t step_index);
const double *tracker_result_estimate_at(const TrackerSimResult *result, size_t target_index, size_t step_index);

int tracker_run_simulation(const TrackerSimOptions *options, TrackerSimResult *result);
void tracker_free_result(TrackerSimResult *result);

#endif
