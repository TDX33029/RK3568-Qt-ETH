#ifndef TRACKER_APP_H
#define TRACKER_APP_H

#include "tracker3d.h"

#include <stddef.h>

#define TRACKER_APP_MAX_TARGETS 3

typedef enum {
    SCENE_STRAIGHT = 0,
    SCENE_CLIMB = 1,
    SCENE_TURN = 2
} DemoScene;

typedef enum {
    MODALITY_TDOA = 0,
    MODALITY_TOA = 1,
    MODALITY_AOA = 2,
    MODALITY_RSS = 3
} TrackerModality;

typedef enum {
    TARGET_MODE_SINGLE = 0,
    TARGET_MODE_MULTI3 = 1
} TrackerTargetMode;

typedef struct {
    DemoScene scene;
    TrackerTargetMode target_mode;
    TrackerModality modality;
    unsigned int seed;
    size_t steps;
    double dt;
} TrackerSimOptions;

typedef struct {
    Tracker3DConfig config;
    TrackerTargetMode target_mode;
    size_t target_count;
    TrackerModality modality;
    size_t measurement_dim;
    size_t steps;
    double dt;
    double elapsed_ms;
    double avg_step_ms;
    double pos_rmse;
    double vel_rmse;
    double target_pos_rmse[TRACKER_APP_MAX_TARGETS];
    double target_vel_rmse[TRACKER_APP_MAX_TARGETS];
    double final_truth[TRACKER_APP_MAX_TARGETS * TRACKER3D_STATE_DIM];
    double final_estimate[TRACKER_APP_MAX_TARGETS * TRACKER3D_STATE_DIM];
    double *truth_history;
    double *estimate_history;
} TrackerSimResult;

void tracker_sim_default_options(TrackerSimOptions *options);
const char *tracker_scene_name(DemoScene scene);
int tracker_parse_scene(const char *text, DemoScene *scene);
const char *tracker_target_mode_name(TrackerTargetMode mode);
int tracker_parse_target_mode(const char *text, TrackerTargetMode *mode);
size_t tracker_target_count_for_mode(TrackerTargetMode mode);
const char *tracker_modality_name(TrackerModality modality);
int tracker_parse_modality(const char *text, TrackerModality *modality);
size_t tracker_expected_measurement_dim_for_modality(TrackerModality modality);
size_t tracker_expected_measurement_dim(const Tracker3DConfig *config);
const double *tracker_result_truth_at(const TrackerSimResult *result, size_t target_index, size_t step_index);
const double *tracker_result_estimate_at(const TrackerSimResult *result, size_t target_index, size_t step_index);
const double *tracker_result_final_truth_at(const TrackerSimResult *result, size_t target_index);
const double *tracker_result_final_estimate_at(const TrackerSimResult *result, size_t target_index);
int tracker_run_simulation(const TrackerSimOptions *options, TrackerSimResult *result);
void tracker_free_result(TrackerSimResult *result);

#endif
