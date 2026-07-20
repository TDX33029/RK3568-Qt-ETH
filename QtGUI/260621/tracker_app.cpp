#include "tracker_app.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

constexpr double APP_PI = 3.14159265358979323846;

void config_anchors(Tracker3DConfig *c) {
    c->anchor_count = 5; c->ref_anchor = 0;
    c->anchors[0] = {0.0, 0.0, 0.0};  c->anchors[1] = {30.0, 0.0, 4.0};
    c->anchors[2] = {30.0, 30.0, 0.0}; c->anchors[3] = {0.0, 30.0, 5.0};
    c->anchors[4] = {15.0, 15.0, 12.0};
}

void base_truth(DemoScene sc, double s[TRACKER3D_STATE_DIM]) {
    switch (sc) {
    case SCENE_STRAIGHT: s[0]=8.0;s[1]=10.0;s[2]=6.0; s[3]=1.20;s[4]=0.75;s[5]=0.20; break;
    case SCENE_CLIMB:    s[0]=6.0;s[1]=8.0;s[2]=4.0;  s[3]=1.05;s[4]=0.65;s[5]=0.35; break;
    case SCENE_TURN:     s[0]=10.0;s[1]=7.0;s[2]=5.5; s[3]=1.10;s[4]=0.25;s[5]=0.10; break;
    }
}

void base_guess(DemoScene sc, double s[TRACKER3D_STATE_DIM]) {
    switch (sc) {
    case SCENE_STRAIGHT: s[0]=6.5;s[1]=8.0;s[2]=4.5; s[3]=0.9;s[4]=0.4;s[5]=0.0; break;
    case SCENE_CLIMB:    s[0]=4.5;s[1]=6.5;s[2]=2.5; s[3]=0.8;s[4]=0.3;s[5]=0.1; break;
    case SCENE_TURN:     s[0]=8.0;s[1]=6.0;s[2]=4.0; s[3]=0.9;s[4]=0.1;s[5]=0.0; break;
    }
}

void appl_variant(size_t ti, double s[TRACKER3D_STATE_DIM], int is_guess) {
    static const double po[TRACKER_APP_MAX_TARGETS][3] = {{0,0,0},{5.5,-3.0,1.2},{-4.0,4.5,-0.9}};
    static const double vo[TRACKER_APP_MAX_TARGETS][3] = {{0,0,0},{-0.18,0.22,0.06},{0.16,-0.12,0.10}};
    double gs = is_guess ? 1.15 : 1.0;
    s[0]+=po[ti][0]*gs; s[1]+=po[ti][1]*gs; s[2]+=po[ti][2]*gs;
    s[3]+=vo[ti][0];    s[4]+=vo[ti][1];    s[5]+=vo[ti][2];
}

void init_truth(DemoScene sc, size_t ti, double s[TRACKER3D_STATE_DIM]) { base_truth(sc,s); appl_variant(ti,s,0); }
void init_guess(DemoScene sc, size_t ti, double s[TRACKER3D_STATE_DIM]) { base_guess(sc,s); appl_variant(ti,s,1); }

void propagate(double s[TRACKER3D_STATE_DIM], double dt, size_t step, size_t total, DemoScene sc, size_t ti) {
    double ph = (2.0*APP_PI*static_cast<double>(step))/(double)(total>1?total-1:1) + (double)ti*0.85;
    double as = 1.0 + 0.12*(double)ti;
    double ax=0,ay=0,az=0;
    switch(sc) {
    case SCENE_STRAIGHT: ax=as*0.15*std::cos(ph); ay=as*0.10*std::sin(0.8*ph); az=as*0.06*std::cos(1.2*ph); break;
    case SCENE_CLIMB:    ax=as*0.08*std::cos(0.6*ph); ay=as*0.05*std::sin(ph); az=0.14+as*0.04*std::cos(ph); break;
    case SCENE_TURN:     ax=-as*0.18*std::sin(ph); ay=as*0.20*std::cos(ph); az=as*0.03*std::cos(1.5*ph); break;
    }
    s[3]+=ax*dt;s[4]+=ay*dt;s[5]+=az*dt;
    s[0]+=s[3]*dt;s[1]+=s[4]*dt;s[2]+=s[5]*dt;
}

void apply_modality(Tracker3DConfig *c, TrackerModality m) {
    tracker3d_set_all_modalities(c,0,0,0,0);
    switch(m) { case MODALITY_TDOA: tracker3d_set_all_modalities(c,1,0,0,0); break;
                case MODALITY_TOA:  tracker3d_set_all_modalities(c,0,1,0,0); break;
                case MODALITY_AOA:  tracker3d_set_all_modalities(c,0,0,1,0); break;
                case MODALITY_RSS:  tracker3d_set_all_modalities(c,0,0,0,1); break; }
}

double now_ms() {
#ifdef _WIN32
    LARGE_INTEGER f,c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
    return 1000.0*(double)c.QuadPart/(double)f.QuadPart;
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return 1000.0*(double)ts.tv_sec+(double)ts.tv_nsec/1.0e6;
#else
    return 1000.0*(double)clock()/(double)CLOCKS_PER_SEC;
#endif
}

size_t hist_off(const TrackerSimResult *r, size_t ti, size_t si) {
    return ((si*r->target_count)+ti)*TRACKER3D_STATE_DIM;
}
size_t fin_off(size_t ti) { return ti*TRACKER3D_STATE_DIM; }

} // namespace

void tracker_sim_default_options(TrackerSimOptions *o) {
    std::memset(o,0,sizeof(*o));
    o->scene=SCENE_STRAIGHT; o->target_mode=TARGET_MODE_SINGLE; o->modality=MODALITY_TDOA;
    o->seed=(unsigned)std::time(nullptr); o->steps=80; o->dt=0.1;
}

const char *tracker_scene_name(DemoScene s) {
    switch(s) { case SCENE_STRAIGHT: return "Straight"; case SCENE_CLIMB: return "Climb"; case SCENE_TURN: return "Turn"; default: return "Unknown"; }
}
const char *tracker_target_mode_name(TrackerTargetMode m) {
    switch(m) { case TARGET_MODE_SINGLE: return "Single"; case TARGET_MODE_MULTI3: return "Multi-3"; default: return "Unknown"; }
}
const char *tracker_modality_name(TrackerModality m) {
    switch(m) { case MODALITY_TDOA: return "TDOA"; case MODALITY_TOA: return "TOA"; case MODALITY_AOA: return "AOA"; case MODALITY_RSS: return "RSS"; default: return "Unknown"; }
}

size_t tracker_target_count_for_mode(TrackerTargetMode m) { return m==TARGET_MODE_MULTI3 ? 3u : 1u; }

size_t tracker_expected_measurement_dim(const Tracker3DConfig *c) {
    size_t d=0;
    for (size_t i=0;i<c->anchor_count;++i) { if (i!=c->ref_anchor && c->enable_tdoa[i]) ++d; if (c->enable_toa[i]) ++d; if (c->enable_aoa[i]) d+=2; if (c->enable_rss[i]) ++d; }
    return d;
}

const double *tracker_result_truth_at(const TrackerSimResult *r, size_t ti, size_t si) {
    if (!r||ti>=r->target_count||si>=r->steps||!r->truth_history) return nullptr;
    return &r->truth_history[hist_off(r,ti,si)];
}
const double *tracker_result_estimate_at(const TrackerSimResult *r, size_t ti, size_t si) {
    if (!r||ti>=r->target_count||si>=r->steps||!r->estimate_history) return nullptr;
    return &r->estimate_history[hist_off(r,ti,si)];
}

int tracker_run_simulation(const TrackerSimOptions *opt, TrackerSimResult *res) {
    if (opt->steps==0) return -3;

    Tracker3DState trs[TRACKER_APP_MAX_TARGETS];
    Tracker3DMeasurement ms[TRACKER_APP_MAX_TARGETS];
    double ts[TRACKER_APP_MAX_TARGETS][TRACKER3D_STATE_DIM];
    double ig[TRACKER_APP_MAX_TARGETS][TRACKER3D_STATE_DIM];
    double pse[TRACKER_APP_MAX_TARGETS]={}, vse[TRACKER_APP_MAX_TARGETS]={};

    std::memset(res,0,sizeof(*res));
    tracker3d_default_config(&res->config); config_anchors(&res->config);
    res->target_mode=opt->target_mode; res->target_count=tracker_target_count_for_mode(opt->target_mode);
    res->modality=opt->modality; apply_modality(&res->config,opt->modality);
    res->measurement_dim=tracker_expected_measurement_dim(&res->config);
    res->steps=opt->steps; res->dt=opt->dt;

    size_t hb = opt->steps*res->target_count*TRACKER3D_STATE_DIM*sizeof(double);
    res->truth_history=(double*)std::calloc(1,hb);
    res->estimate_history=(double*)std::calloc(1,hb);
    if (!res->truth_history||!res->estimate_history) { tracker_free_result(res); return -1; }

    std::srand(opt->seed);
    for (size_t ti=0;ti<res->target_count;++ti) {
        init_truth(opt->scene,ti,ts[ti]); init_guess(opt->scene,ti,ig[ti]);
        tracker3d_init_state(&trs[ti],ig[ti],16.0,1.0);
    }

    double st = now_ms();
    for (size_t k=0;k<opt->steps;++k) {
        for (size_t ti=0;ti<res->target_count;++ti) {
            size_t off=hist_off(res,ti,k);
            propagate(ts[ti],opt->dt,k,opt->steps,opt->scene,ti);
            tracker3d_simulate_measurement(&res->config,ts[ti],&ms[ti]);
            if (tracker3d_step(&trs[ti],&res->config,&ms[ti],opt->dt)!=0) { tracker_free_result(res); return -2; }
            std::memcpy(&res->truth_history[off],ts[ti],sizeof(double)*TRACKER3D_STATE_DIM);
            std::memcpy(&res->estimate_history[off],trs[ti].state,sizeof(double)*TRACKER3D_STATE_DIM);
            pse[ti] += (trs[ti].state[0]-ts[ti][0])*(trs[ti].state[0]-ts[ti][0])
                     + (trs[ti].state[1]-ts[ti][1])*(trs[ti].state[1]-ts[ti][1])
                     + (trs[ti].state[2]-ts[ti][2])*(trs[ti].state[2]-ts[ti][2]);
            vse[ti] += (trs[ti].state[3]-ts[ti][3])*(trs[ti].state[3]-ts[ti][3])
                     + (trs[ti].state[4]-ts[ti][4])*(trs[ti].state[4]-ts[ti][4])
                     + (trs[ti].state[5]-ts[ti][5])*(trs[ti].state[5]-ts[ti][5]);
        }
    }
    double en = now_ms();

    double apr=0,avr=0;
    for (size_t ti=0;ti<res->target_count;++ti) {
        std::memcpy(&res->final_truth[fin_off(ti)],ts[ti],sizeof(double)*TRACKER3D_STATE_DIM);
        std::memcpy(&res->final_estimate[fin_off(ti)],trs[ti].state,sizeof(double)*TRACKER3D_STATE_DIM);
        res->target_pos_rmse[ti]=std::sqrt(pse[ti]/(3.0*(double)opt->steps));
        res->target_vel_rmse[ti]=std::sqrt(vse[ti]/(3.0*(double)opt->steps));
        apr+=res->target_pos_rmse[ti]; avr+=res->target_vel_rmse[ti];
    }
    res->pos_rmse=apr/(double)res->target_count; res->vel_rmse=avr/(double)res->target_count;
    res->elapsed_ms=en-st; res->avg_step_ms=res->elapsed_ms/(double)opt->steps;
    return 0;
}

void tracker_free_result(TrackerSimResult *r) {
    if (r->truth_history) { std::free(r->truth_history); r->truth_history=nullptr; }
    if (r->estimate_history) { std::free(r->estimate_history); r->estimate_history=nullptr; }
}
