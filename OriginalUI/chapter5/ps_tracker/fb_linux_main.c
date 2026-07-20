#include "tracker_app.h"
#include "ui_draw.h"

#ifdef __linux__

#include <fcntl.h>
#include <linux/fb.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

typedef struct {
    int fd;
    int width;
    int height;
    int bpp;
    int line_length;
    size_t map_size;
    unsigned char *map_ptr;
    uint32_t *backbuffer;
} FbDisplay;

typedef enum {
    PAGE_MENU = 0,
    PAGE_RESULT = 1
} UiPage;

enum {
    ITEM_SCENE = 0,
    ITEM_TARGETS = 1,
    ITEM_MODALITY = 2,
    ITEM_STEPS = 3,
    ITEM_SEED = 4,
    ITEM_RUN = 5,
    ITEM_QUIT = 6,
    ITEM_COUNT = 7
};

typedef struct {
    TrackerSimOptions options;
    TrackerSimResult result;
    int has_result;
    int selected;
    UiPage page;
    int quit_requested;
} UiState;

static uint32_t truth_color_for_target(size_t target_index) {
    static const uint32_t colors[TRACKER_APP_MAX_TARGETS] = {
        0xFF8DBBEA,
        0xFF9BD6A8,
        0xFFF0C27A
    };
    return colors[target_index % TRACKER_APP_MAX_TARGETS];
}

static uint32_t est_color_for_target(size_t target_index) {
    static const uint32_t colors[TRACKER_APP_MAX_TARGETS] = {
        0xFF2A75BB,
        0xFF2E8B57,
        0xFFC44E35
    };
    return colors[target_index % TRACKER_APP_MAX_TARGETS];
}

static int fb_open(FbDisplay *display, const char *path) {
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    memset(display, 0, sizeof(*display));
    display->fd = open(path, O_RDWR);
    if (display->fd < 0) {
        return -1;
    }

    if (ioctl(display->fd, FBIOGET_FSCREENINFO, &finfo) != 0 || ioctl(display->fd, FBIOGET_VSCREENINFO, &vinfo) != 0) {
        close(display->fd);
        return -1;
    }

    display->width = (int) vinfo.xres;
    display->height = (int) vinfo.yres;
    display->bpp = (int) vinfo.bits_per_pixel;
    display->line_length = (int) finfo.line_length;
    display->map_size = (size_t) display->line_length * (size_t) display->height;
    display->map_ptr = (unsigned char *) mmap(NULL, display->map_size, PROT_READ | PROT_WRITE, MAP_SHARED, display->fd, 0);
    if (display->map_ptr == MAP_FAILED) {
        close(display->fd);
        return -1;
    }

    display->backbuffer = (uint32_t *) calloc((size_t) display->width * (size_t) display->height, sizeof(uint32_t));
    if (display->backbuffer == NULL) {
        munmap(display->map_ptr, display->map_size);
        close(display->fd);
        return -1;
    }

    return 0;
}

static void fb_close(FbDisplay *display) {
    if (display->backbuffer != NULL) {
        free(display->backbuffer);
        display->backbuffer = NULL;
    }
    if (display->map_ptr != NULL && display->map_ptr != MAP_FAILED) {
        munmap(display->map_ptr, display->map_size);
        display->map_ptr = NULL;
    }
    if (display->fd >= 0) {
        close(display->fd);
        display->fd = -1;
    }
}

static void fb_flush(FbDisplay *display) {
    int x;
    int y;

    for (y = 0; y < display->height; ++y) {
        unsigned char *row_ptr = display->map_ptr + y * display->line_length;
        for (x = 0; x < display->width; ++x) {
            uint32_t argb = display->backbuffer[y * display->width + x];
            uint8_t r = (uint8_t) ((argb >> 16) & 0xFF);
            uint8_t g = (uint8_t) ((argb >> 8) & 0xFF);
            uint8_t b = (uint8_t) (argb & 0xFF);

            if (display->bpp == 32) {
                uint32_t pixel = (r << 16) | (g << 8) | b;
                ((uint32_t *) row_ptr)[x] = pixel;
            } else if (display->bpp == 16) {
                uint16_t pixel565 = (uint16_t) (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
                ((uint16_t *) row_ptr)[x] = pixel565;
            }
        }
    }
}

static void plot_series(
    UiCanvas *canvas,
    int x,
    int y,
    int w,
    int h,
    const TrackerSimResult *result,
    int coord_x,
    int coord_y
) {
    size_t i;
    size_t target_index;
    double min_x = 1e30;
    double max_x = -1e30;
    double min_y = 1e30;
    double max_y = -1e30;
    double span_x;
    double span_y;

    ui_draw_rect(canvas, x, y, w, h, 0xFF6A7A8C);

    for (target_index = 0; target_index < result->target_count; ++target_index) {
        for (i = 0; i < result->steps; ++i) {
            const double *truth = tracker_result_truth_at(result, target_index, i);
            const double *est = tracker_result_estimate_at(result, target_index, i);

            if (truth[coord_x] < min_x) min_x = truth[coord_x];
            if (truth[coord_x] > max_x) max_x = truth[coord_x];
            if (truth[coord_y] < min_y) min_y = truth[coord_y];
            if (truth[coord_y] > max_y) max_y = truth[coord_y];
            if (est[coord_x] < min_x) min_x = est[coord_x];
            if (est[coord_x] > max_x) max_x = est[coord_x];
            if (est[coord_y] < min_y) min_y = est[coord_y];
            if (est[coord_y] > max_y) max_y = est[coord_y];
        }
    }

    span_x = (max_x - min_x);
    span_y = (max_y - min_y);
    if (span_x < 1e-6) span_x = 1.0;
    if (span_y < 1e-6) span_y = 1.0;

    for (target_index = 0; target_index < result->target_count; ++target_index) {
        const uint32_t truth_color = truth_color_for_target(target_index);
        const uint32_t est_color = est_color_for_target(target_index);

        for (i = 1; i < result->steps; ++i) {
            const double *truth_prev = tracker_result_truth_at(result, target_index, i - 1);
            const double *truth_curr = tracker_result_truth_at(result, target_index, i);
            const double *est_prev = tracker_result_estimate_at(result, target_index, i - 1);
            const double *est_curr = tracker_result_estimate_at(result, target_index, i);
            int tx0 = x + 6 + (int) ((truth_prev[coord_x] - min_x) / span_x * (double) (w - 12));
            int ty0 = y + h - 6 - (int) ((truth_prev[coord_y] - min_y) / span_y * (double) (h - 12));
            int tx1 = x + 6 + (int) ((truth_curr[coord_x] - min_x) / span_x * (double) (w - 12));
            int ty1 = y + h - 6 - (int) ((truth_curr[coord_y] - min_y) / span_y * (double) (h - 12));
            int ex0 = x + 6 + (int) ((est_prev[coord_x] - min_x) / span_x * (double) (w - 12));
            int ey0 = y + h - 6 - (int) ((est_prev[coord_y] - min_y) / span_y * (double) (h - 12));
            int ex1 = x + 6 + (int) ((est_curr[coord_x] - min_x) / span_x * (double) (w - 12));
            int ey1 = y + h - 6 - (int) ((est_curr[coord_y] - min_y) / span_y * (double) (h - 12));

            ui_draw_line(canvas, tx0, ty0, tx1, ty1, truth_color);
            ui_draw_line(canvas, ex0, ey0, ex1, ey1, est_color);
        }
    }
}

static void project_iso_point(const double state[TRACKER3D_STATE_DIM], double *proj_x, double *proj_y) {
    const double iso_cos = 0.8660254037844386;
    const double iso_sin = 0.5;

    *proj_x = (state[0] - state[1]) * iso_cos;
    *proj_y = state[2] - (state[0] + state[1]) * iso_sin;
}

static void plot_series_3d(
    UiCanvas *canvas,
    int x,
    int y,
    int w,
    int h,
    const TrackerSimResult *result
) {
    size_t i;
    size_t target_index;
    double min_x = 1e30;
    double max_x = -1e30;
    double min_y = 1e30;
    double max_y = -1e30;
    double span_x;
    double span_y;

    ui_draw_rect(canvas, x, y, w, h, 0xFF6A7A8C);

    for (target_index = 0; target_index < result->target_count; ++target_index) {
        for (i = 0; i < result->steps; ++i) {
            const double *truth = tracker_result_truth_at(result, target_index, i);
            const double *est = tracker_result_estimate_at(result, target_index, i);
            double truth_px;
            double truth_py;
            double est_px;
            double est_py;

            project_iso_point(truth, &truth_px, &truth_py);
            project_iso_point(est, &est_px, &est_py);

            if (truth_px < min_x) min_x = truth_px;
            if (truth_px > max_x) max_x = truth_px;
            if (truth_py < min_y) min_y = truth_py;
            if (truth_py > max_y) max_y = truth_py;
            if (est_px < min_x) min_x = est_px;
            if (est_px > max_x) max_x = est_px;
            if (est_py < min_y) min_y = est_py;
            if (est_py > max_y) max_y = est_py;
        }
    }

    span_x = (max_x - min_x);
    span_y = (max_y - min_y);
    if (span_x < 1e-6) span_x = 1.0;
    if (span_y < 1e-6) span_y = 1.0;

    for (target_index = 0; target_index < result->target_count; ++target_index) {
        const uint32_t truth_color = truth_color_for_target(target_index);
        const uint32_t est_color = est_color_for_target(target_index);

        for (i = 1; i < result->steps; ++i) {
            const double *truth_prev = tracker_result_truth_at(result, target_index, i - 1);
            const double *truth_curr = tracker_result_truth_at(result, target_index, i);
            const double *est_prev = tracker_result_estimate_at(result, target_index, i - 1);
            const double *est_curr = tracker_result_estimate_at(result, target_index, i);
            double tpx0;
            double tpy0;
            double tpx1;
            double tpy1;
            double epx0;
            double epy0;
            double epx1;
            double epy1;
            int tx0;
            int ty0;
            int tx1;
            int ty1;
            int ex0;
            int ey0;
            int ex1;
            int ey1;

            project_iso_point(truth_prev, &tpx0, &tpy0);
            project_iso_point(truth_curr, &tpx1, &tpy1);
            project_iso_point(est_prev, &epx0, &epy0);
            project_iso_point(est_curr, &epx1, &epy1);

            tx0 = x + 6 + (int) ((tpx0 - min_x) / span_x * (double) (w - 12));
            ty0 = y + h - 6 - (int) ((tpy0 - min_y) / span_y * (double) (h - 12));
            tx1 = x + 6 + (int) ((tpx1 - min_x) / span_x * (double) (w - 12));
            ty1 = y + h - 6 - (int) ((tpy1 - min_y) / span_y * (double) (h - 12));
            ex0 = x + 6 + (int) ((epx0 - min_x) / span_x * (double) (w - 12));
            ey0 = y + h - 6 - (int) ((epy0 - min_y) / span_y * (double) (h - 12));
            ex1 = x + 6 + (int) ((epx1 - min_x) / span_x * (double) (w - 12));
            ey1 = y + h - 6 - (int) ((epy1 - min_y) / span_y * (double) (h - 12));

            ui_draw_line(canvas, tx0, ty0, tx1, ty1, truth_color);
            ui_draw_line(canvas, ex0, ey0, ex1, ey1, est_color);
        }
    }
}

static void draw_menu(UiCanvas *canvas, const UiState *state) {
    char line[128];
    size_t dim = tracker_expected_measurement_dim_for_modality(state->options.modality);
    int i;
    const char *labels[ITEM_COUNT] = {
        "SCENE",
        "TARGETS",
        "MODALITY",
        "STEPS",
        "SEED",
        "RUN",
        "QUIT"
    };

    ui_clear(canvas, 0xFFF5EEDF);
    ui_fill_rect(canvas, 0, 0, canvas->width, 54, 0xFF1F4E5F);
    ui_draw_text(canvas, 24, 16, "PS TRACKER UI", 3, 0xFFFFFFFF);
    ui_draw_text(canvas, 24, 70, "W S MOVE   A D CHANGE   ENTER RUN   Q QUIT", 2, 0xFF30424E);

    for (i = 0; i < ITEM_COUNT; ++i) {
        int top = 120 + i * 44;
        uint32_t bg = (i == state->selected) ? 0xFFD9E6EA : 0xFFF5EEDF;
        ui_fill_rect(canvas, 40, top, canvas->width - 80, 36, bg);
        ui_draw_text(canvas, 52, top + 10, labels[i], 2, 0xFF1B2C34);

        if (i == ITEM_SCENE) {
            snprintf(line, sizeof(line), "%s", tracker_scene_name(state->options.scene));
            ui_draw_text(canvas, canvas->width - 220, top + 10, line, 2, 0xFF1B2C34);
        } else if (i == ITEM_TARGETS) {
            snprintf(line, sizeof(line), "%s", tracker_target_mode_name(state->options.target_mode));
            ui_draw_text(canvas, canvas->width - 220, top + 10, line, 2, 0xFF1B2C34);
        } else if (i == ITEM_MODALITY) {
            snprintf(line, sizeof(line), "%s", tracker_modality_name(state->options.modality));
            ui_draw_text(canvas, canvas->width - 220, top + 10, line, 2, 0xFF1B2C34);
        } else if (i == ITEM_STEPS) {
            snprintf(line, sizeof(line), "%llu", (unsigned long long) state->options.steps);
            ui_draw_text(canvas, canvas->width - 160, top + 10, line, 2, 0xFF1B2C34);
        } else if (i == ITEM_SEED) {
            snprintf(line, sizeof(line), "%u", state->options.seed);
            ui_draw_text(canvas, canvas->width - 180, top + 10, line, 2, 0xFF1B2C34);
        }
    }

    if (tracker_target_count_for_mode(state->options.target_mode) > 1) {
        snprintf(
            line,
            sizeof(line),
            "DIM %llu X %llu",
            (unsigned long long) dim,
            (unsigned long long) tracker_target_count_for_mode(state->options.target_mode)
        );
    } else {
        snprintf(line, sizeof(line), "DIM %llu", (unsigned long long) dim);
    }
    ui_draw_text(canvas, canvas->width - 210, 74, line, 2, 0xFF30424E);
}

static void draw_target_legend(UiCanvas *canvas, int x, int y, size_t target_index) {
    char line[8];
    uint32_t truth_color = truth_color_for_target(target_index);
    uint32_t est_color = est_color_for_target(target_index);

    snprintf(line, sizeof(line), "T%llu", (unsigned long long) (target_index + 1));
    ui_draw_text(canvas, x, y, line, 2, est_color);
    ui_draw_line(canvas, x + 34, y + 8, x + 56, y + 8, truth_color);
    ui_draw_line(canvas, x + 62, y + 8, x + 84, y + 8, est_color);
}

static void draw_target_rmse(UiCanvas *canvas, int x, int y, const UiState *state, size_t target_index) {
    char line[64];
    snprintf(
        line,
        sizeof(line),
        "T%llu P%.3f V%.3f",
        (unsigned long long) (target_index + 1),
        state->result.target_pos_rmse[target_index],
        state->result.target_vel_rmse[target_index]
    );
    ui_draw_text(canvas, x, y, line, 1, est_color_for_target(target_index));
}

static void draw_result(UiCanvas *canvas, const UiState *state) {
    char line[128];
    const int plot_left = 20;
    const int plot_gap = 12;
    const int plot_top = 294;
    const int plot_width = (canvas->width - plot_left * 2 - plot_gap * 2) / 3;
    const int plot_height = canvas->height - plot_top - 18;
    const int second_plot_x = plot_left + plot_width + plot_gap;
    const int third_plot_x = second_plot_x + plot_width + plot_gap;
    size_t target_index;

    ui_clear(canvas, 0xFFF9F7F2);
    ui_fill_rect(canvas, 0, 0, canvas->width, 54, 0xFF23403B);
    ui_draw_text(canvas, 24, 16, "TRACK RESULT", 3, 0xFFFFFFFF);
    ui_draw_text(canvas, 24, 70, "M MENU   R RERUN   Q QUIT", 2, 0xFF30424E);

    snprintf(line, sizeof(line), "SCENE %s", tracker_scene_name(state->options.scene));
    ui_draw_text(canvas, 24, 108, line, 2, 0xFF30424E);
    snprintf(line, sizeof(line), "TARGETS %s", tracker_target_mode_name(state->result.target_mode));
    ui_draw_text(canvas, 24, 136, line, 2, 0xFF30424E);
    snprintf(line, sizeof(line), "MODALITY %s", tracker_modality_name(state->result.modality));
    ui_draw_text(canvas, 24, 164, line, 2, 0xFF30424E);
    snprintf(line, sizeof(line), "STEPS %llu", (unsigned long long) state->result.steps);
    ui_draw_text(canvas, 24, 192, line, 2, 0xFF30424E);
    if (state->result.target_count > 1) {
        snprintf(
            line,
            sizeof(line),
            "DIM %llu X %llu",
            (unsigned long long) state->result.measurement_dim,
            (unsigned long long) state->result.target_count
        );
    } else {
        snprintf(line, sizeof(line), "DIM %llu", (unsigned long long) state->result.measurement_dim);
    }
    ui_draw_text(canvas, 24, 220, line, 2, 0xFF30424E);

    if (state->result.target_count > 1) {
        snprintf(line, sizeof(line), "AVG POS %.4f", state->result.pos_rmse);
    } else {
        snprintf(line, sizeof(line), "POS RMSE %.4f", state->result.pos_rmse);
    }
    ui_draw_text(canvas, 360, 108, line, 2, 0xFF30424E);
    if (state->result.target_count > 1) {
        snprintf(line, sizeof(line), "AVG VEL %.4f", state->result.vel_rmse);
    } else {
        snprintf(line, sizeof(line), "VEL RMSE %.4f", state->result.vel_rmse);
    }
    ui_draw_text(canvas, 360, 136, line, 2, 0xFF30424E);
    snprintf(line, sizeof(line), "TOTAL %.3f ms", state->result.elapsed_ms);
    ui_draw_text(canvas, 360, 164, line, 2, 0xFF30424E);
    snprintf(line, sizeof(line), "STEP %.3f ms", state->result.avg_step_ms);
    ui_draw_text(canvas, 360, 192, line, 2, 0xFF30424E);
    snprintf(line, sizeof(line), "LIGHT TRUTH  DARK EST");
    ui_draw_text(canvas, 360, 220, line, 1, 0xFF30424E);

    for (target_index = 0; target_index < state->result.target_count; ++target_index) {
        draw_target_rmse(canvas, 24 + (int) target_index * 250, 236, state, target_index);
        draw_target_legend(canvas, 24 + (int) target_index * 250, 252, target_index);
    }

    ui_draw_text(canvas, plot_left + 4, 272, "XY VIEW", 2, 0xFF1B2C34);
    plot_series(canvas, plot_left, plot_top, plot_width, plot_height, &state->result, 0, 1);

    ui_draw_text(canvas, second_plot_x + 4, 272, "XZ VIEW", 2, 0xFF1B2C34);
    plot_series(canvas, second_plot_x, plot_top, plot_width, plot_height, &state->result, 0, 2);

    ui_draw_text(canvas, third_plot_x + 4, 272, "3D VIEW", 2, 0xFF1B2C34);
    plot_series_3d(canvas, third_plot_x, plot_top, plot_width, plot_height, &state->result);
}

static void draw_page(UiCanvas *canvas, const UiState *state) {
    if (state->page == PAGE_RESULT && state->has_result) {
        draw_result(canvas, state);
    } else {
        draw_menu(canvas, state);
    }
}

static int read_key_nonblocking(void) {
    fd_set readfds;
    struct timeval tv;
    unsigned char ch = 0;

    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    tv.tv_sec = 0;
    tv.tv_usec = 100000;

    if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv) > 0) {
        if (read(STDIN_FILENO, &ch, 1) == 1) {
            return (int) ch;
        }
    }
    return -1;
}

static void run_current_simulation(UiState *state) {
    if (state->has_result) {
        tracker_free_result(&state->result);
        state->has_result = 0;
    }

    if (tracker_run_simulation(&state->options, &state->result) == 0) {
        state->has_result = 1;
        state->page = PAGE_RESULT;
    }
}

static void handle_menu_key(UiState *state, int key) {
    switch (key) {
        case 'w':
        case 'W':
            state->selected = (state->selected + ITEM_COUNT - 1) % ITEM_COUNT;
            break;
        case 's':
        case 'S':
            state->selected = (state->selected + 1) % ITEM_COUNT;
            break;
        case 'a':
        case 'A':
        case 'd':
        case 'D':
            if (state->selected == ITEM_SCENE) {
                int dir = (key == 'a' || key == 'A') ? -1 : 1;
                int next = ((int) state->options.scene + dir + 3) % 3;
                state->options.scene = (DemoScene) next;
            } else if (state->selected == ITEM_TARGETS) {
                int dir = (key == 'a' || key == 'A') ? -1 : 1;
                int next = ((int) state->options.target_mode + dir + 2) % 2;
                state->options.target_mode = (TrackerTargetMode) next;
            } else if (state->selected == ITEM_MODALITY) {
                int dir = (key == 'a' || key == 'A') ? -1 : 1;
                int next = ((int) state->options.modality + dir + 4) % 4;
                state->options.modality = (TrackerModality) next;
            } else if (state->selected == ITEM_STEPS) {
                long delta = (key == 'a' || key == 'A') ? -10 : 10;
                long next_steps = (long) state->options.steps + delta;
                if (next_steps < 20) next_steps = 20;
                state->options.steps = (size_t) next_steps;
            } else if (state->selected == ITEM_SEED) {
                long delta = (key == 'a' || key == 'A') ? -1 : 1;
                state->options.seed = (unsigned int) ((long) state->options.seed + delta);
            }
            break;
        case '\r':
        case '\n':
            if (state->selected == ITEM_RUN) {
                run_current_simulation(state);
            } else if (state->selected == ITEM_QUIT) {
                state->quit_requested = 1;
            }
            break;
        case 'q':
        case 'Q':
            state->quit_requested = 1;
            break;
    }
}

static void handle_result_key(UiState *state, int key) {
    switch (key) {
        case 'm':
        case 'M':
            state->page = PAGE_MENU;
            break;
        case 'r':
        case 'R':
            run_current_simulation(state);
            break;
        case 'q':
        case 'Q':
            state->quit_requested = 1;
            break;
    }
}

int main(void) {
    FbDisplay display;
    UiCanvas canvas;
    UiState state;
    struct termios oldt;
    struct termios newt;

    memset(&state, 0, sizeof(state));
    tracker_sim_default_options(&state.options);
    state.selected = ITEM_RUN;
    state.page = PAGE_MENU;

    if (fb_open(&display, "/dev/fb0") != 0) {
        fprintf(stderr, "Failed to open /dev/fb0\n");
        return 1;
    }

    canvas.width = display.width;
    canvas.height = display.height;
    canvas.pixels = display.backbuffer;

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
    newt.c_cc[VMIN] = 0;
    newt.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    while (!state.quit_requested) {
        int key = read_key_nonblocking();
        if (key >= 0) {
            if (state.page == PAGE_MENU) {
                handle_menu_key(&state, key);
            } else {
                handle_result_key(&state, key);
            }
        }

        draw_page(&canvas, &state);
        fb_flush(&display);
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    if (state.has_result) {
        tracker_free_result(&state.result);
    }
    fb_close(&display);
    return 0;
}

#else

#include <stdio.h>

int main(void) {
    fprintf(stderr, "fb_linux_main.c must be built on Linux.\n");
    return 1;
}

#endif
