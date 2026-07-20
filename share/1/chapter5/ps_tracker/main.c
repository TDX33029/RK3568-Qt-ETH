#include "tracker_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *exe_name) {
    printf(
        "Usage: %s [--single|--multi3] [--tdoa|--toa|--aoa|--rss] "
        "[--scene straight|climb|turn] [--steps N] [--seed N] [--csv PATH]\n",
        exe_name
    );
    printf("Only one measurement modality is used in each run. Default mode: single target + TDOA.\n");
}

static int write_csv(const char *path, const TrackerSimResult *result) {
    FILE *file = fopen(path, "w");
    size_t k;
    size_t target_index;

    if (file == NULL) {
        return -1;
    }

    if (result->target_count == 1) {
        fprintf(file, "step,truth_x,truth_y,truth_z,est_x,est_y,est_z,truth_vx,truth_vy,truth_vz,est_vx,est_vy,est_vz\n");
        for (k = 0; k < result->steps; ++k) {
            const double *truth = tracker_result_truth_at(result, 0, k);
            const double *est = tracker_result_estimate_at(result, 0, k);
            fprintf(
                file,
                "%llu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                (unsigned long long) (k + 1),
                truth[0], truth[1], truth[2],
                est[0], est[1], est[2],
                truth[3], truth[4], truth[5],
                est[3], est[4], est[5]
            );
        }
    } else {
        fprintf(file, "target,step,truth_x,truth_y,truth_z,est_x,est_y,est_z,truth_vx,truth_vy,truth_vz,est_vx,est_vy,est_vz\n");
        for (target_index = 0; target_index < result->target_count; ++target_index) {
            for (k = 0; k < result->steps; ++k) {
                const double *truth = tracker_result_truth_at(result, target_index, k);
                const double *est = tracker_result_estimate_at(result, target_index, k);
                fprintf(
                    file,
                    "%llu,%llu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                    (unsigned long long) (target_index + 1),
                    (unsigned long long) (k + 1),
                    truth[0], truth[1], truth[2],
                    est[0], est[1], est[2],
                    truth[3], truth[4], truth[5],
                    est[3], est[4], est[5]
                );
            }
        }
    }

    fclose(file);
    return 0;
}

int main(int argc, char **argv) {
    TrackerSimOptions options;
    TrackerSimResult result;
    const char *csv_path = NULL;
    size_t k;
    size_t target_index;
    int status = 0;

    tracker_sim_default_options(&options);

    for (k = 1; k < (size_t) argc; ++k) {
        if (strcmp(argv[k], "--tdoa") == 0) {
            options.modality = MODALITY_TDOA;
        } else if (strcmp(argv[k], "--toa") == 0) {
            options.modality = MODALITY_TOA;
        } else if (strcmp(argv[k], "--aoa") == 0) {
            options.modality = MODALITY_AOA;
        } else if (strcmp(argv[k], "--rss") == 0) {
            options.modality = MODALITY_RSS;
        } else if (strcmp(argv[k], "--single") == 0) {
            options.target_mode = TARGET_MODE_SINGLE;
        } else if (strcmp(argv[k], "--multi3") == 0 || strcmp(argv[k], "--multi") == 0) {
            options.target_mode = TARGET_MODE_MULTI3;
        } else if (strcmp(argv[k], "--targets") == 0 && k + 1 < (size_t) argc) {
            ++k;
            if (tracker_parse_target_mode(argv[k], &options.target_mode) != 0) {
                fprintf(stderr, "Unknown target mode: %s\n", argv[k]);
                return 1;
            }
        } else if (strcmp(argv[k], "--scene") == 0 && k + 1 < (size_t) argc) {
            ++k;
            if (tracker_parse_scene(argv[k], &options.scene) != 0) {
                fprintf(stderr, "Unknown scene: %s\n", argv[k]);
                return 1;
            }
        } else if (strcmp(argv[k], "--steps") == 0 && k + 1 < (size_t) argc) {
            ++k;
            options.steps = (size_t) strtoull(argv[k], NULL, 10);
        } else if (strcmp(argv[k], "--seed") == 0 && k + 1 < (size_t) argc) {
            ++k;
            options.seed = (unsigned int) strtoul(argv[k], NULL, 10);
        } else if (strcmp(argv[k], "--csv") == 0 && k + 1 < (size_t) argc) {
            ++k;
            csv_path = argv[k];
        } else if (strcmp(argv[k], "--help") == 0 || strcmp(argv[k], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[k]);
            print_usage(argv[0]);
            return 1;
        }
    }

    status = tracker_run_simulation(&options, &result);
    if (status != 0) {
        fprintf(stderr, "Simulation failed with status %d\n", status);
        return 2;
    }

    printf("3D PS tracker demo\n");
    printf("Scene: %s\n", tracker_scene_name(options.scene));
    printf("Target mode: %s\n", tracker_target_mode_name(options.target_mode));
    printf("Target count: %llu\n", (unsigned long long) result.target_count);
    printf("Seed: %u\n", options.seed);
    printf("Measurement modality: %s\n", tracker_modality_name(options.modality));
    printf("Measurement dimension per target per frame: %llu\n", (unsigned long long) result.measurement_dim);
    printf("Running %llu steps...\n", (unsigned long long) result.steps);

    for (k = 0; k < result.steps; ++k) {
        if ((k + 1) % 20 == 0 || k + 1 == result.steps) {
            printf("Step %llu\n", (unsigned long long) (k + 1));
            for (target_index = 0; target_index < result.target_count; ++target_index) {
                const double *truth = tracker_result_truth_at(&result, target_index, k);
                const double *est = tracker_result_estimate_at(&result, target_index, k);
                printf(
                    "  T%llu | truth=(%.2f, %.2f, %.2f) | est=(%.2f, %.2f, %.2f)\n",
                    (unsigned long long) (target_index + 1),
                    truth[0], truth[1], truth[2],
                    est[0], est[1], est[2]
                );
            }
        }
    }

    printf("\nFinal estimate summary:\n");
    for (target_index = 0; target_index < result.target_count; ++target_index) {
        const double *final_est = tracker_result_final_estimate_at(&result, target_index);
        printf(
            "T%llu Position: [%.3f, %.3f, %.3f] m\n",
            (unsigned long long) (target_index + 1),
            final_est[0], final_est[1], final_est[2]
        );
        printf(
            "T%llu Velocity: [%.3f, %.3f, %.3f] m/s\n",
            (unsigned long long) (target_index + 1),
            final_est[3], final_est[4], final_est[5]
        );
        printf(
            "T%llu Position RMSE: %.4f m | Velocity RMSE: %.4f m/s\n",
            (unsigned long long) (target_index + 1),
            result.target_pos_rmse[target_index],
            result.target_vel_rmse[target_index]
        );
    }
    printf("Average Position RMSE: %.4f m\n", result.pos_rmse);
    printf("Average Velocity RMSE: %.4f m/s\n", result.vel_rmse);
    printf("Total runtime: %.3f ms\n", result.elapsed_ms);
    printf("Average step time: %.3f ms\n", result.avg_step_ms);

    if (csv_path != NULL) {
        if (write_csv(csv_path, &result) != 0) {
            fprintf(stderr, "Failed to save CSV: %s\n", csv_path);
            tracker_free_result(&result);
            return 3;
        }
        printf("Saved CSV: %s\n", csv_path);
    }

    tracker_free_result(&result);
    return 0;
}
