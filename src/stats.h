#ifndef GPR_STATS_H
#define GPR_STATS_H

#include <stddef.h>

#define GPR_HIST_BINS 10

/* Standard USB polling rates we snap the estimate to. */
static const int GPR_STANDARD_RATES[] = { 1000, 500, 333, 250, 200, 125, 100, 60, 30 };
static const int GPR_NUM_STANDARD_RATES = 9;

/* Bin edges in milliseconds for the interval histogram.
 * Bin i covers [edges[i], edges[i+1]). Last bin is [edges[N], +inf). */
extern const double GPR_HIST_EDGES[GPR_HIST_BINS + 1];

typedef struct {
    double *intervals_ms;   /* inter-report intervals */
    size_t count;
    size_t capacity;
    double sum;
    double sum_sq;
    double min;
    double max;
    size_t hist[GPR_HIST_BINS];
} gpr_stats_t;

void gpr_stats_init(gpr_stats_t *s);
void gpr_stats_free(gpr_stats_t *s);

/* Returns 0 on success, -1 on allocation failure (sample is dropped). */
int gpr_stats_add(gpr_stats_t *s, double interval_ms);

size_t gpr_stats_count(const gpr_stats_t *s);
double gpr_stats_mean(const gpr_stats_t *s);
double gpr_stats_stddev(const gpr_stats_t *s);
double gpr_stats_min(const gpr_stats_t *s);
double gpr_stats_max(const gpr_stats_t *s);
double gpr_stats_percentile(gpr_stats_t *s, double q); /* q in [0,100], sorts in place */
double gpr_stats_median(gpr_stats_t *s);

/* Dominant histogram bin index, or -1 if empty. */
int gpr_stats_mode_bin(const gpr_stats_t *s);

/* Estimate polling rate in Hz from the median interval.
 * Returns 0 if there are too few samples (< 10). */
double gpr_stats_estimated_hz(gpr_stats_t *s);

/* Snap a raw Hz value to the nearest standard rate. Returns 0 if hz <= 0. */
int gpr_snap_rate(double hz);

#endif
