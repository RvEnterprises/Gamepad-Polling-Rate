#include "stats.h"

#include <math.h>
#include <stdlib.h>

/* Edges chosen around common USB poll intervals:
 * 1ms=1000Hz, 2ms=500Hz, 3ms=333Hz, 4ms=250Hz, 5ms=200Hz,
 * 8ms=125Hz, 10ms=100Hz, 16.7ms=60Hz. */
const double GPR_HIST_EDGES[GPR_HIST_BINS + 1] = {
    0.0, 0.75, 1.5, 2.5, 3.5, 4.5, 6.5, 9.0, 12.5, 20.0, 1e12
};

void gpr_stats_init(gpr_stats_t *s) {
    s->intervals_ms = NULL;
    s->count = 0;
    s->capacity = 0;
    s->sum = 0.0;
    s->sum_sq = 0.0;
    s->min = 0.0;
    s->max = 0.0;
    for (int i = 0; i < GPR_HIST_BINS; i++)
        s->hist[i] = 0;
}

void gpr_stats_free(gpr_stats_t *s) {
    free(s->intervals_ms);
    s->intervals_ms = NULL;
    s->count = 0;
    s->capacity = 0;
}

int gpr_stats_add(gpr_stats_t *s, double interval_ms) {
    if (!(interval_ms >= 0.0) || !(interval_ms < 60000.0))
        return 0; /* ignore clock glitches, keep count honest */
    if (s->count == s->capacity) {
        size_t ncap = s->capacity == 0 ? 1024 : s->capacity * 2;
        double *p = (double *)realloc(s->intervals_ms, ncap * sizeof(double));
        if (!p)
            return -1;
        s->intervals_ms = p;
        s->capacity = ncap;
    }
    s->intervals_ms[s->count++] = interval_ms;
    s->sum += interval_ms;
    s->sum_sq += interval_ms * interval_ms;
    if (s->count == 1) {
        s->min = interval_ms;
        s->max = interval_ms;
    } else {
        if (interval_ms < s->min)
            s->min = interval_ms;
        if (interval_ms > s->max)
            s->max = interval_ms;
    }
    for (int i = 0; i < GPR_HIST_BINS; i++) {
        if (interval_ms >= GPR_HIST_EDGES[i] && interval_ms < GPR_HIST_EDGES[i + 1]) {
            s->hist[i]++;
            break;
        }
    }
    return 0;
}

size_t gpr_stats_count(const gpr_stats_t *s) {
    return s->count;
}

double gpr_stats_mean(const gpr_stats_t *s) {
    return s->count ? s->sum / (double)s->count : 0.0;
}

double gpr_stats_stddev(const gpr_stats_t *s) {
    if (s->count < 2)
        return 0.0;
    double mean = gpr_stats_mean(s);
    double var = s->sum_sq / (double)s->count - mean * mean;
    return var > 0.0 ? sqrt(var) : 0.0;
}

double gpr_stats_min(const gpr_stats_t *s) {
    return s->count ? s->min : 0.0;
}

double gpr_stats_max(const gpr_stats_t *s) {
    return s->count ? s->max : 0.0;
}

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

double gpr_stats_percentile(gpr_stats_t *s, double q) {
    if (s->count == 0)
        return 0.0;
    if (q < 0.0)
        q = 0.0;
    if (q > 100.0)
        q = 100.0;
    qsort(s->intervals_ms, s->count, sizeof(double), cmp_double);
    if (s->count == 1)
        return s->intervals_ms[0];
    double rank = (q / 100.0) * (double)(s->count - 1);
    size_t lo = (size_t)rank;
    size_t hi = lo + 1;
    if (hi >= s->count)
        return s->intervals_ms[lo];
    double frac = rank - (double)lo;
    return s->intervals_ms[lo] * (1.0 - frac) + s->intervals_ms[hi] * frac;
}

double gpr_stats_median(gpr_stats_t *s) {
    return gpr_stats_percentile(s, 50.0);
}

int gpr_stats_mode_bin(const gpr_stats_t *s) {
    size_t best = 0;
    int idx = -1;
    for (int i = 0; i < GPR_HIST_BINS; i++) {
        if (s->hist[i] > best) {
            best = s->hist[i];
            idx = i;
        }
    }
    return best ? idx : -1;
}

int gpr_snap_rate(double hz) {
    if (!(hz > 0.0))
        return 0;
    int best = GPR_STANDARD_RATES[0];
    double best_err = 1e18;
    for (int i = 0; i < GPR_NUM_STANDARD_RATES; i++) {
        double err = fabs(hz - (double)GPR_STANDARD_RATES[i]);
        if (err < best_err) {
            best_err = err;
            best = GPR_STANDARD_RATES[i];
        }
    }
    return best;
}

double gpr_stats_estimated_hz(gpr_stats_t *s) {
    if (s->count < 10)
        return 0.0;
    double med = gpr_stats_median(s);
    if (!(med > 0.0))
        return 0.0;
    return 1000.0 / med;
}
