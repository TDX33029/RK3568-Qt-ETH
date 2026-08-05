#include "tracker_app.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>

#define APP_PI 3.14159265358979323846

static void configure_demo_anchors(Tracker3DConfig *config) {
    config->anchor_count = 5;
    config->ref_anchor = 0;
    config->anchors[0] = (Tracker3DVec3){0.0, 0.0, 0.0};
    config->anchors[1] = (Tracker3DVec3){30.0, 0.0, 4.0};
    config->anchors[2] = (Tracker3DVec3){30.0, 30.0, 0.0};
    config->anchors[3] = (Tracker3DVec3){0.0, 30.0, 5.0};
    config->anchors[4] = (Tracker3DVec3){15.0, 15.0, 12.0};
}

static void set_base_truth_state(DemoScene scene, double state[TRACKER3D_STATE_DIM]) {
    switch (scene) {
        case SCENE_STRAIGHT:
            state[0]=8.0; state[1]=10.0; state[2]=6.0;
            state[3]=1.20; state[4]=0.75; state[5]=0.20; break;
        case SCENE_CLIMB:
            state[0]=6.0; state[1]=8.0; state[2]=4.0;
            state[3]=1.05; state[4]=0.65; state[5]=0.35; break;
        case SCENE_TURN:
            state[0]=10.0; state[1]=7.0; state[2]=5.5;
            state[3]=1.10; state[4]=0.25; state[5]=0.10; break;
    }
}

static void set_base_guess_state(DemoScene scene, double state[TRACKER3D_STATE_DIM]) {
    switch (scene) {
        case SCENE_STRAIGHT:
            state[0]=6.5; state[1]=8.0; state[2]=4.5;
            state[3]=0.9; state[4]=0.4; state[5]=0.0; break;
        case SCENE_CLIMB:
            state[0]=4.5; state[1]=6.5; state[2]=2.5;
            state[3]=0.8; state[4]=0.3; state[5]=0.1; break;
        case SCENE_TURN:
            state[0]=8.0; state[1]=6.0; state[2]=4.0;
            state[3]=0.9; state[4]=0.1; state[5]=0.0; break;
    }
}

static void apply_target_variant(size_t ti, double s[TRACKER3D_STATE_DIM], int is_guess) {
    static const double po[3][3] = {{0,0,0},{5.5,-3.0,1.2},{-4.0,4.5,-0.9}};
    static const double vo[3][3] = {{0,0,0},{-0.18,0.22,0.06},{0.16,-0.12,0.10}};
    double gs = is_guess ? 1.15 : 1.0;
    s[0] += po[ti][0]*gs; s[1] += po[ti][1]*gs; s[2] += po[ti][2]*gs;
    s[3] += vo[ti][0]; s[4] += vo[ti][1]; s[5] += vo[ti][2];
}

static void init_truth_state(DemoScene sc, size_t ti, double s[TRACKER3D_STATE_DIM]) {
    set_base_truth_state(sc, s); apply_target_variant(ti, s, 0);
}

static void init_guess_state(DemoScene sc, size_t ti, double s[TRACKER3D_STATE_DIM]) {
    set_base_guess_state(sc, s); apply_target_variant(ti, s, 1);
}

static void propagate_truth(double st[TRACKER3D_STATE_DIM], double dt, size_t si, size_t ts, DemoScene sc, size_t ti) {
    double ph = (2.0*APP_PI*(double)si)/(double)(ts>1?ts-1:1) + (double)ti*0.85;
    double asc = 1.0+0.12*(double)ti;
    double ax=0, ay=0, az=0;
    switch (sc) {
        case SCENE_STRAIGHT: ax=asc*0.15*cos(ph); ay=asc*0.10*sin(0.8*ph); az=asc*0.06*cos(1.2*ph); break;
        case SCENE_CLIMB:    ax=asc*0.08*cos(0.6*ph); ay=asc*0.05*sin(ph); az=0.14+asc*0.04*cos(ph); break;
        case SCENE_TURN:     ax=-asc*0.18*sin(ph); ay=asc*0.20*cos(ph); az=asc*0.03*cos(1.5*ph); break;
    }
    st[3] += ax*dt; st[4] += ay*dt; st[5] += az*dt;
    st[0] += st[3]*dt; st[1] += st[4]*dt; st[2] += st[5]*dt;
}

static void apply_modality(Tracker3DConfig *c, TrackerModality m) {
    tracker3d_set_all_modalities(c,0,0,0,0);
    switch (m) {
        case MODALITY_TDOA: tracker3d_set_all_modalities(c,1,0,0,0); break;
        case MODALITY_TOA:  tracker3d_set_all_modalities(c,0,1,0,0); break;
        case MODALITY_AOA:  tracker3d_set_all_modalities(c,0,0,1,0); break;
        case MODALITY_RSS:  tracker3d_set_all_modalities(c,0,0,0,1); break;
    }
}

static double app_now_ms() {
    static auto const epoch = std::chrono::high_resolution_clock::now();
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double,std::milli>(now-epoch).count();
}

static size_t hoff(const TrackerSimResult *r, size_t ti, size_t si) {
    return ((si*r->target_count)+ti)*TRACKER3D_STATE_DIM;
}

static size_t foff(size_t ti) { return ti*TRACKER3D_STATE_DIM; }

void tracker_sim_default_options(TrackerSimOptions *o) {
    memset(o,0,sizeof(*o));
    o->scene=SCENE_STRAIGHT; o->target_mode=TARGET_MODE_SINGLE;
    o->modality=MODALITY_TDOA; o->seed=(unsigned)time(NULL);
    o->steps=80; o->dt=0.1;
}

const char *tracker_scene_name(DemoScene s) {
    switch(s){case SCENE_STRAIGHT:return"straight";case SCENE_CLIMB:return"climb";case SCENE_TURN:return"turn";default:return"unknown";}
}

int tracker_parse_scene(const char *t, DemoScene *s) {
    if(strcmp(t,"straight")==0){*s=SCENE_STRAIGHT;return 0;}
    if(strcmp(t,"climb")==0){*s=SCENE_CLIMB;return 0;}
    if(strcmp(t,"turn")==0){*s=SCENE_TURN;return 0;}
    return -1;
}

const char *tracker_target_mode_name(TrackerTargetMode m) {
    switch(m){case TARGET_MODE_SINGLE:return"single";case TARGET_MODE_MULTI3:return"multi3";default:return"unknown";}
}

int tracker_parse_target_mode(const char *t, TrackerTargetMode *m) {
    if(strcmp(t,"single")==0||strcmp(t,"--single")==0){*m=TARGET_MODE_SINGLE;return 0;}
    if(strcmp(t,"multi3")==0||strcmp(t,"multi")==0||strcmp(t,"--multi3")==0||strcmp(t,"--multi")==0){*m=TARGET_MODE_MULTI3;return 0;}
    return -1;
}

size_t tracker_target_count_for_mode(TrackerTargetMode m) { return (m==TARGET_MODE_MULTI3)?3u:1u; }

const char *tracker_modality_name(TrackerModality m) {
    switch(m){case MODALITY_TDOA:return"TDOA";case MODALITY_TOA:return"TOA";case MODALITY_AOA:return"AOA";case MODALITY_RSS:return"RSS";default:return"UNKNOWN";}
}

int tracker_parse_modality(const char *t, TrackerModality *m) {
    if(strcmp(t,"tdoa")==0||strcmp(t,"--tdoa")==0){*m=MODALITY_TDOA;return 0;}
    if(strcmp(t,"toa")==0||strcmp(t,"--toa")==0){*m=MODALITY_TOA;return 0;}
    if(strcmp(t,"aoa")==0||strcmp(t,"--aoa")==0){*m=MODALITY_AOA;return 0;}
    if(strcmp(t,"rss")==0||strcmp(t,"--rss")==0){*m=MODALITY_RSS;return 0;}
    return -1;
}

size_t tracker_expected_measurement_dim_for_modality(TrackerModality m) {
    Tracker3DConfig c; tracker3d_default_config(&c); configure_demo_anchors(&c); apply_modality(&c,m);
    return tracker_expected_measurement_dim(&c);
}

size_t tracker_expected_measurement_dim(const Tracker3DConfig *c) {
    size_t d=0;
    for(size_t i=0;i<c->anchor_count;++i){
        if(i!=c->ref_anchor&&c->enable_tdoa[i])++d;
        if(c->enable_toa[i])++d;
        if(c->enable_aoa[i])d+=2;
        if(c->enable_rss[i])++d;
    }
    return d;
}

const double *tracker_result_truth_at(const TrackerSimResult *r, size_t ti, size_t si) {
    if(!r||ti>=r->target_count||si>=r->steps||!r->truth_history)return NULL;
    return &r->truth_history[hoff(r,ti,si)];
}

const double *tracker_result_estimate_at(const TrackerSimResult *r, size_t ti, size_t si) {
    if(!r||ti>=r->target_count||si>=r->steps||!r->estimate_history)return NULL;
    return &r->estimate_history[hoff(r,ti,si)];
}

const double *tracker_result_final_truth_at(const TrackerSimResult *r, size_t ti) {
    if(!r||ti>=r->target_count)return NULL;
    return &r->final_truth[foff(ti)];
}

const double *tracker_result_final_estimate_at(const TrackerSimResult *r, size_t ti) {
    if(!r||ti>=r->target_count)return NULL;
    return &r->final_estimate[foff(ti)];
}

int tracker_run_simulation(const TrackerSimOptions *opts, TrackerSimResult *r) {
    Tracker3DState trackers[TRACKER_APP_MAX_TARGETS];
    Tracker3DMeasurement meas[TRACKER_APP_MAX_TARGETS];
    double truths[TRACKER_APP_MAX_TARGETS][TRACKER3D_STATE_DIM];
    double guesses[TRACKER_APP_MAX_TARGETS][TRACKER3D_STATE_DIM];
    double pse[TRACKER_APP_MAX_TARGETS]={0}, vse[TRACKER_APP_MAX_TARGETS]={0};
    double avg_pr=0, avg_vr=0;

    if(opts->steps==0)return -3;
    memset(r,0,sizeof(*r));
    tracker3d_default_config(&r->config);
    configure_demo_anchors(&r->config);
    r->target_mode=opts->target_mode;
    r->target_count=tracker_target_count_for_mode(opts->target_mode);
    r->modality=opts->modality;
    apply_modality(&r->config,opts->modality);
    r->measurement_dim=tracker_expected_measurement_dim(&r->config);
    r->steps=opts->steps; r->dt=opts->dt;
    r->truth_history=(double*)calloc(opts->steps*r->target_count*TRACKER3D_STATE_DIM,sizeof(double));
    r->estimate_history=(double*)calloc(opts->steps*r->target_count*TRACKER3D_STATE_DIM,sizeof(double));
    if(!r->truth_history||!r->estimate_history){tracker_free_result(r);return -1;}

    srand(opts->seed);
    for(size_t ti=0;ti<r->target_count;++ti){
        init_truth_state(opts->scene,ti,truths[ti]);
        init_guess_state(opts->scene,ti,guesses[ti]);
        tracker3d_init_state(&trackers[ti],guesses[ti],16.0,1.0);
    }

    double start=app_now_ms();
    for(size_t k=0;k<opts->steps;++k){
        for(size_t ti=0;ti<r->target_count;++ti){
            propagate_truth(truths[ti],opts->dt,k,opts->steps,opts->scene,ti);
            tracker3d_simulate_measurement(&r->config,truths[ti],&meas[ti]);
            if(tracker3d_step(&trackers[ti],&r->config,&meas[ti],opts->dt)!=0){
                tracker_free_result(r);return -2;
            }
            size_t off=hoff(r,ti,k);
            memcpy(&r->truth_history[off],truths[ti],sizeof(double)*TRACKER3D_STATE_DIM);
            memcpy(&r->estimate_history[off],trackers[ti].state,sizeof(double)*TRACKER3D_STATE_DIM);
            pse[ti]+=(trackers[ti].state[0]-truths[ti][0])*(trackers[ti].state[0]-truths[ti][0])
                    +(trackers[ti].state[1]-truths[ti][1])*(trackers[ti].state[1]-truths[ti][1])
                    +(trackers[ti].state[2]-truths[ti][2])*(trackers[ti].state[2]-truths[ti][2]);
            vse[ti]+=(trackers[ti].state[3]-truths[ti][3])*(trackers[ti].state[3]-truths[ti][3])
                    +(trackers[ti].state[4]-truths[ti][4])*(trackers[ti].state[4]-truths[ti][4])
                    +(trackers[ti].state[5]-truths[ti][5])*(trackers[ti].state[5]-truths[ti][5]);
        }
    }
    double end=app_now_ms();

    for(size_t ti=0;ti<r->target_count;++ti){
        memcpy(&r->final_truth[foff(ti)],truths[ti],sizeof(double)*TRACKER3D_STATE_DIM);
        memcpy(&r->final_estimate[foff(ti)],trackers[ti].state,sizeof(double)*TRACKER3D_STATE_DIM);
        r->target_pos_rmse[ti]=sqrt(pse[ti]/(3.0*(double)opts->steps));
        r->target_vel_rmse[ti]=sqrt(vse[ti]/(3.0*(double)opts->steps));
        avg_pr+=r->target_pos_rmse[ti]; avg_vr+=r->target_vel_rmse[ti];
    }
    r->pos_rmse=avg_pr/(double)r->target_count;
    r->vel_rmse=avg_vr/(double)r->target_count;
    r->elapsed_ms=end-start;
    r->avg_step_ms=r->elapsed_ms/(double)opts->steps;
    return 0;
}

void tracker_free_result(TrackerSimResult *r) {
    if(r->truth_history){free(r->truth_history);r->truth_history=NULL;}
    if(r->estimate_history){free(r->estimate_history);r->estimate_history=NULL;}
}

/* ───────────────────────── 实时模式 ───────────────────────── */

static Tracker3DState g_live_trackers[TRACKER_APP_MAX_TARGETS]; /* 实时单目标用 [0] */

int tracker_live_init(TrackerSimResult *r, const TrackerSimOptions *opts) {
    double guess[TRACKER3D_STATE_DIM];
    if(!opts) return -4;
    memset(r, 0, sizeof(*r));
    tracker3d_default_config(&r->config);
    configure_demo_anchors(&r->config);
    r->target_mode  = TARGET_MODE_SINGLE;            /* 实时模式固定单目标 */
    r->target_count = 1;
    r->modality     = opts->modality;
    apply_modality(&r->config, opts->modality);
    r->measurement_dim = tracker_expected_measurement_dim(&r->config);
    r->steps = 0;
    r->dt    = (opts->dt > 0.0 ? opts->dt : 0.1);
    r->truth_history    = NULL;                      /* 实时无真值 */
    r->estimate_history = (double*)calloc((size_t)TRACKER_LIVE_MAX_STEPS * r->target_count * TRACKER3D_STATE_DIM,
                                         sizeof(double));
    if(!r->estimate_history) { tracker_free_result(r); return -1; }

    /* 初值: 用某个合理猜测(中心附近), 真实系统可改成上一次已知位置 */
    srand(opts->seed ? opts->seed : (unsigned)time(NULL));
    init_guess_state(opts->scene, 0, guess);
    tracker3d_init_state(&g_live_trackers[0], guess, 16.0, 1.0);
    return 0;
}

int tracker_live_step(TrackerSimResult *r, const Tracker3DMeasurement *meas, double dt) {
    if(!r || !meas) return -4;
    if(dt <= 0.0) dt = r->dt;

    if(tracker3d_predict(&g_live_trackers[0], &r->config, dt) != 0) return -1;
    /* update 失败(测量数值异常)不中断跟踪: 预测仍有效, 等待下一好帧即可 */
    (void)tracker3d_update(&g_live_trackers[0], &r->config, meas);

    /* 满则滚动: 丢弃最旧一半, 保持近期轨迹 */
    if(r->steps >= TRACKER_LIVE_MAX_STEPS) {
        size_t half = TRACKER_LIVE_MAX_STEPS / 2;
        size_t cnt  = (TRACKER_LIVE_MAX_STEPS - half) * r->target_count * TRACKER3D_STATE_DIM;
        memmove(r->estimate_history, r->estimate_history + half * r->target_count * TRACKER3D_STATE_DIM,
                cnt * sizeof(double));
        r->steps = half;
    }

    size_t off = hoff(r, 0, r->steps);
    memcpy(&r->estimate_history[off], g_live_trackers[0].state, sizeof(double) * TRACKER3D_STATE_DIM);
    memcpy(&r->final_estimate[foff(0)], g_live_trackers[0].state, sizeof(double) * TRACKER3D_STATE_DIM);
    r->steps++;
    r->dt = dt;
    r->elapsed_ms += dt * 1000.0;
    return 0;
}
