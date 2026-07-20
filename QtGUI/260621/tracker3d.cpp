#include "tracker3d.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

constexpr double EPS = 1e-9;
constexpr double PI = 3.14159265358979323846;

enum ObsKind { OBS_TDOA = 0, OBS_TOA, OBS_AOA_AZ, OBS_AOA_EL, OBS_RSS };

struct ObsDesc { ObsKind kind; size_t anchor; };

double wrap_angle(double a) {
    while (a > PI) a -= 2.0 * PI;
    while (a < -PI) a += 2.0 * PI;
    return a;
}

double gauss(double stddev) {
    double u1 = (static_cast<double>(rand()) + 1.0) / (static_cast<double>(RAND_MAX) + 2.0);
    double u2 = (static_cast<double>(rand()) + 1.0) / (static_cast<double>(RAND_MAX) + 2.0);
    return stddev * std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * PI * u2);
}

void set_identity6(double m[TRACKER3D_STATE_DIM * TRACKER3D_STATE_DIM]) {
    std::memset(m, 0, sizeof(double) * TRACKER3D_STATE_DIM * TRACKER3D_STATE_DIM);
    for (size_t i = 0; i < TRACKER3D_STATE_DIM; ++i) m[i * TRACKER3D_STATE_DIM + i] = 1.0;
}

void mat6_mul(const double *a, const double *b, double *o) {
    for (size_t r = 0; r < TRACKER3D_STATE_DIM; ++r)
        for (size_t c = 0; c < TRACKER3D_STATE_DIM; ++c) {
            double s = 0;
            for (size_t k = 0; k < TRACKER3D_STATE_DIM; ++k) s += a[r * TRACKER3D_STATE_DIM + k] * b[k * TRACKER3D_STATE_DIM + c];
            o[r * TRACKER3D_STATE_DIM + c] = s;
        }
}

void mat6_transpose(const double *in, double *out) {
    for (size_t r = 0; r < TRACKER3D_STATE_DIM; ++r)
        for (size_t c = 0; c < TRACKER3D_STATE_DIM; ++c)
            out[c * TRACKER3D_STATE_DIM + r] = in[r * TRACKER3D_STATE_DIM + c];
}

void anchor_geom(const double s[TRACKER3D_STATE_DIM], const Tracker3DVec3 *a,
                 double *dx, double *dy, double *dz, double *rho, double *range) {
    *dx = s[0] - a->x; *dy = s[1] - a->y; *dz = s[2] - a->z;
    *rho = std::sqrt((*dx)*(*dx) + (*dy)*(*dy));
    *range = std::sqrt((*rho)*(*rho) + (*dz)*(*dz));
    if (*rho < EPS) *rho = EPS;
    if (*range < EPS) *range = EPS;
}

size_t build_descriptors(const Tracker3DConfig *cfg, const Tracker3DMeasurement *m, ObsDesc *d) {
    size_t n = 0;
    for (size_t i = 0; i < cfg->anchor_count; ++i) {
        if (i != cfg->ref_anchor && cfg->enable_tdoa[i] && m->has_tdoa[i]) d[n++] = {OBS_TDOA, i};
    }
    for (size_t i = 0; i < cfg->anchor_count; ++i) {
        if (cfg->enable_toa[i] && m->has_toa[i]) d[n++] = {OBS_TOA, i};
    }
    for (size_t i = 0; i < cfg->anchor_count; ++i) {
        if (cfg->enable_aoa[i] && m->has_aoa[i]) {
            d[n++] = {OBS_AOA_AZ, i};
            d[n++] = {OBS_AOA_EL, i};
        }
    }
    for (size_t i = 0; i < cfg->anchor_count; ++i) {
        if (cfg->enable_rss[i] && m->has_rss[i]) d[n++] = {OBS_RSS, i};
    }
    return n;
}

void meas_model(const Tracker3DConfig *cfg, const Tracker3DMeasurement *m,
                const double st[TRACKER3D_STATE_DIM], const ObsDesc *desc,
                double *z, double *h, double h_row[TRACKER3D_STATE_DIM], double *var) {
    size_t ai = desc->anchor;
    const Tracker3DVec3 *an = &cfg->anchors[ai];
    double dx = 0, dy = 0, dz = 0, rho = 0, range = 0;
    std::memset(h_row, 0, sizeof(double) * TRACKER3D_STATE_DIM);
    anchor_geom(st, an, &dx, &dy, &dz, &rho, &range);

    switch (desc->kind) {
    case OBS_TDOA: {
        const Tracker3DVec3 *ra = &cfg->anchors[cfg->ref_anchor];
        double rdx=0,rdy=0,rdz=0,rrho=0,rr=0;
        anchor_geom(st, ra, &rdx, &rdy, &rdz, &rrho, &rr);
        *z = m->tdoa[ai] * cfg->light_speed;
        *h = range - rr;
        h_row[0] = dx/range - rdx/rr; h_row[1] = dy/range - rdy/rr; h_row[2] = dz/range - rdz/rr;
        *var = (cfg->tdoa_std_sec * cfg->light_speed) * (cfg->tdoa_std_sec * cfg->light_speed);
        break;
    }
    case OBS_TOA:
        *z = m->toa[ai] * cfg->light_speed;
        *h = range;
        h_row[0]=dx/range; h_row[1]=dy/range; h_row[2]=dz/range;
        *var = (cfg->toa_std_sec * cfg->light_speed) * (cfg->toa_std_sec * cfg->light_speed);
        break;
    case OBS_AOA_AZ:
        *z = m->aoa_az[ai];
        *h = std::atan2(dy, dx);
        h_row[0]=-dy/(rho*rho); h_row[1]=dx/(rho*rho);
        *var = cfg->aoa_std_rad * cfg->aoa_std_rad;
        break;
    case OBS_AOA_EL: {
        double rs = range*range;
        *z = m->aoa_el[ai];
        *h = std::atan2(dz, rho);
        h_row[0]=-(dx*dz)/(rho*rs); h_row[1]=-(dy*dz)/(rho*rs); h_row[2]=rho/rs;
        *var = cfg->aoa_std_rad * cfg->aoa_std_rad;
        break;
    }
    case OBS_RSS: {
        double coeff = -10.0 * cfg->rss_path_loss_exp / std::log(10.0);
        *z = m->rss[ai];
        *h = cfg->rss_ref_dbm - 10.0 * cfg->rss_path_loss_exp * std::log10(range);
        h_row[0]=coeff*dx/(range*range); h_row[1]=coeff*dy/(range*range); h_row[2]=coeff*dz/(range*range);
        *var = cfg->rss_std_db * cfg->rss_std_db;
        break;
    }
    }
}

} // namespace

void tracker3d_default_config(Tracker3DConfig *c) {
    std::memset(c, 0, sizeof(*c));
    c->light_speed = 299792458.0; c->process_accel_std = 0.8;
    c->tdoa_std_sec = 2.0e-9; c->toa_std_sec = 2.0e-9;
    c->aoa_std_rad = PI / 180.0; c->rss_std_db = 2.0;
    c->rss_ref_dbm = -35.0; c->rss_path_loss_exp = 2.0;
}

void tracker3d_set_all_modalities(Tracker3DConfig *c, int td, int to, int ao, int rs) {
    for (size_t i = 0; i < c->anchor_count; ++i) {
        c->enable_tdoa[i] = td ? 1u : 0u; c->enable_toa[i] = to ? 1u : 0u;
        c->enable_aoa[i] = ao ? 1u : 0u; c->enable_rss[i] = rs ? 1u : 0u;
    }
    if (c->ref_anchor < c->anchor_count) c->enable_tdoa[c->ref_anchor] = 0;
}

void tracker3d_clear_measurement(Tracker3DMeasurement *m) { std::memset(m, 0, sizeof(*m)); }

void tracker3d_init_state(Tracker3DState *t, const double initial[TRACKER3D_STATE_DIM], double pv, double vv) {
    std::memcpy(t->state, initial, sizeof(double)*TRACKER3D_STATE_DIM);
    std::memset(t->covariance, 0, sizeof(t->covariance));
    for (size_t i = 0; i < 3; ++i) {
        t->covariance[i*TRACKER3D_STATE_DIM+i] = pv;
        t->covariance[(i+3)*TRACKER3D_STATE_DIM+(i+3)] = vv;
    }
}

size_t tracker3d_measurement_dim(const Tracker3DConfig *c, const Tracker3DMeasurement *m) {
    ObsDesc d[TRACKER3D_MAX_MEAS_DIM]; return build_descriptors(c, m, d);
}

int tracker3d_predict(Tracker3DState *t, const Tracker3DConfig *c, double dt) {
    (void)c;
    double F[TRACKER3D_STATE_DIM*TRACKER3D_STATE_DIM], Ft[TRACKER3D_STATE_DIM*TRACKER3D_STATE_DIM];
    double tmp[TRACKER3D_STATE_DIM*TRACKER3D_STATE_DIM], pc[TRACKER3D_STATE_DIM*TRACKER3D_STATE_DIM];
    double Q[TRACKER3D_STATE_DIM*TRACKER3D_STATE_DIM], ps[TRACKER3D_STATE_DIM];
    double sa2 = c->process_accel_std * c->process_accel_std;
    double dt2=dt*dt, dt3=dt2*dt, dt4=dt2*dt2;

    set_identity6(F);
    F[0*TRACKER3D_STATE_DIM+3]=dt; F[1*TRACKER3D_STATE_DIM+4]=dt; F[2*TRACKER3D_STATE_DIM+5]=dt;

    std::memset(Q, 0, sizeof(Q));
    for (size_t ax = 0; ax < 3; ++ax) {
        size_t pi = ax, vi = ax+3;
        Q[pi*TRACKER3D_STATE_DIM+pi] = 0.25*dt4*sa2;
        Q[pi*TRACKER3D_STATE_DIM+vi] = 0.5*dt3*sa2;
        Q[vi*TRACKER3D_STATE_DIM+pi] = 0.5*dt3*sa2;
        Q[vi*TRACKER3D_STATE_DIM+vi] = dt2*sa2;
    }

    for (size_t i=0;i<TRACKER3D_STATE_DIM;++i) {
        ps[i]=F[i*TRACKER3D_STATE_DIM+0]*t->state[0]+F[i*TRACKER3D_STATE_DIM+1]*t->state[1]
             +F[i*TRACKER3D_STATE_DIM+2]*t->state[2]+F[i*TRACKER3D_STATE_DIM+3]*t->state[3]
             +F[i*TRACKER3D_STATE_DIM+4]*t->state[4]+F[i*TRACKER3D_STATE_DIM+5]*t->state[5];
    }
    mat6_transpose(F, Ft); mat6_mul(F, t->covariance, tmp); mat6_mul(tmp, Ft, pc);
    for (size_t i=0;i<TRACKER3D_STATE_DIM*TRACKER3D_STATE_DIM;++i) t->covariance[i]=pc[i]+Q[i];
    std::memcpy(t->state, ps, sizeof(ps));
    return 0;
}

int tracker3d_update(Tracker3DState *t, const Tracker3DConfig *c, const Tracker3DMeasurement *m) {
    ObsDesc d[TRACKER3D_MAX_MEAS_DIM];
    size_t dim = build_descriptors(c, m, d);
    if (dim == 0) return 0;

    for (size_t k = 0; k < dim; ++k) {
        double z=0,h=0,hr[TRACKER3D_STATE_DIM],vr=0;
        double pht[TRACKER3D_STATE_DIM],kg[TRACKER3D_STATE_DIM];
        double kh[TRACKER3D_STATE_DIM*TRACKER3D_STATE_DIM];
        double am[TRACKER3D_STATE_DIM*TRACKER3D_STATE_DIM];
        double t6[TRACKER3D_STATE_DIM*TRACKER3D_STATE_DIM];
        double t6b[TRACKER3D_STATE_DIM*TRACKER3D_STATE_DIM];
        double krkt[TRACKER3D_STATE_DIM*TRACKER3D_STATE_DIM];

        meas_model(c, m, t->state, &d[k], &z, &h, hr, &vr);
        double innov = z - h;
        if (d[k].kind == OBS_AOA_AZ || d[k].kind == OBS_AOA_EL) innov = wrap_angle(innov);

        for (size_t r=0;r<TRACKER3D_STATE_DIM;++r) {
            double s=0; for (size_t cc=0;cc<TRACKER3D_STATE_DIM;++cc) s+=t->covariance[r*TRACKER3D_STATE_DIM+cc]*hr[cc];
            pht[r]=s;
        }
        double s=vr; for (size_t r=0;r<TRACKER3D_STATE_DIM;++r) s+=hr[r]*pht[r];
        if (!std::isfinite(s) || s<1e-12) return -3;

        for (size_t r=0;r<TRACKER3D_STATE_DIM;++r) { kg[r]=pht[r]/s; t->state[r]+=kg[r]*innov; }

        std::memset(kh,0,sizeof(kh));
        for (size_t r=0;r<TRACKER3D_STATE_DIM;++r) for (size_t cc=0;cc<TRACKER3D_STATE_DIM;++cc) kh[r*TRACKER3D_STATE_DIM+cc]=kg[r]*hr[cc];
        set_identity6(am);
        for (size_t i=0;i<TRACKER3D_STATE_DIM*TRACKER3D_STATE_DIM;++i) am[i]-=kh[i];
        mat6_mul(am, t->covariance, t6);
        { double at[TRACKER3D_STATE_DIM*TRACKER3D_STATE_DIM]; mat6_transpose(am, at); mat6_mul(t6, at, t6b); }
        for (size_t r=0;r<TRACKER3D_STATE_DIM;++r) for (size_t cc=0;cc<TRACKER3D_STATE_DIM;++cc) krkt[r*TRACKER3D_STATE_DIM+cc]=kg[r]*vr*kg[cc];
        for (size_t i=0;i<TRACKER3D_STATE_DIM*TRACKER3D_STATE_DIM;++i) t->covariance[i]=t6b[i]+krkt[i];
        for (size_t r=0;r<TRACKER3D_STATE_DIM;++r)
            for (size_t cc=r+1;cc<TRACKER3D_STATE_DIM;++cc) {
                double sym=0.5*(t->covariance[r*TRACKER3D_STATE_DIM+cc]+t->covariance[cc*TRACKER3D_STATE_DIM+r]);
                t->covariance[r*TRACKER3D_STATE_DIM+cc]=sym; t->covariance[cc*TRACKER3D_STATE_DIM+r]=sym;
            }
    }
    return 0;
}

int tracker3d_step(Tracker3DState *t, const Tracker3DConfig *c, const Tracker3DMeasurement *m, double dt) {
    if (tracker3d_predict(t, c, dt) != 0) return -1;
    return tracker3d_update(t, c, m);
}

void tracker3d_simulate_measurement(const Tracker3DConfig *c, const double ts[TRACKER3D_STATE_DIM], Tracker3DMeasurement *m) {
    double rdx=0,rdy=0,rdz=0,rrho=0,rr=0;
    tracker3d_clear_measurement(m);
    anchor_geom(ts, &c->anchors[c->ref_anchor], &rdx, &rdy, &rdz, &rrho, &rr);
    for (size_t i=0;i<c->anchor_count;++i) {
        double dx=0,dy=0,dz=0,rho=0,range=0;
        anchor_geom(ts, &c->anchors[i], &dx, &dy, &dz, &rho, &range);
        if (c->enable_tdoa[i] && i!=c->ref_anchor) { m->has_tdoa[i]=1; m->tdoa[i]=(range-rr)/c->light_speed+gauss(c->tdoa_std_sec); }
        if (c->enable_toa[i]) { m->has_toa[i]=1; m->toa[i]=range/c->light_speed+gauss(c->toa_std_sec); }
        if (c->enable_aoa[i]) { m->has_aoa[i]=1; m->aoa_az[i]=std::atan2(dy,dx)+gauss(c->aoa_std_rad); m->aoa_el[i]=std::atan2(dz,rho)+gauss(c->aoa_std_rad); }
        if (c->enable_rss[i]) { m->has_rss[i]=1; m->rss[i]=c->rss_ref_dbm-10.0*c->rss_path_loss_exp*std::log10(range)+gauss(c->rss_std_db); }
    }
}
