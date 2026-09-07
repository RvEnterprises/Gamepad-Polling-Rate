#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "stats.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } \
    else { printf("ok: %s\n", msg); } \
} while (0)

static void test_basic_stats(void) {
    gpr_stats_t s;
    gpr_stats_init(&s);
    for (int i = 0; i < 100; i++)
        assert(gpr_stats_add(&s, 4.0) == 0);
    CHECK(gpr_stats_count(&s) == 100, "count == 100");
    CHECK(fabs(gpr_stats_mean(&s) - 4.0) < 1e-9, "mean == 4.0ms");
    CHECK(fabs(gpr_stats_median(&s) - 4.0) < 1e-9, "median == 4.0ms");
    CHECK(fabs(gpr_stats_stddev(&s)) < 1e-9, "stddev == 0");
    CHECK(fabs(gpr_stats_estimated_hz(&s) - 250.0) < 1e-6, "250Hz estimate from 4ms");
    CHECK(gpr_snap_rate(240.0) == 250, "snap 240Hz -> 250Hz");
    gpr_stats_free(&s);
}

static void test_percentiles(void) {
    gpr_stats_t s;
    gpr_stats_init(&s);
    for (int i = 1; i <= 100; i++)
        assert(gpr_stats_add(&s, (double)i) == 0);
    CHECK(fabs(gpr_stats_median(&s) - 50.5) < 1e-9, "median of 1..100 is 50.5");
    CHECK(fabs(gpr_stats_percentile(&s, 0.0) - 1.0) < 1e-9, "p0 == min");
    CHECK(fabs(gpr_stats_percentile(&s, 100.0) - 100.0) < 1e-9, "p100 == max");
    gpr_stats_free(&s);
}

static void test_snap_table(void) {
    CHECK(gpr_snap_rate(1000.0) == 1000, "snap 1000");
    CHECK(gpr_snap_rate(980.0) == 1000, "snap 980 -> 1000");
    CHECK(gpr_snap_rate(505.0) == 500, "snap 505 -> 500");
    CHECK(gpr_snap_rate(130.0) == 125, "snap 130 -> 125");
    CHECK(gpr_snap_rate(0.0) == 0, "snap 0 -> 0");
    CHECK(gpr_snap_rate(-5.0) == 0, "snap negative -> 0");
}

static void test_too_few_samples(void) {
    gpr_stats_t s;
    gpr_stats_init(&s);
    for (int i = 0; i < 9; i++)
        assert(gpr_stats_add(&s, 1.0) == 0);
    CHECK(gpr_stats_estimated_hz(&s) == 0.0, "estimate needs >= 10 samples");
    assert(gpr_stats_add(&s, 1.0) == 0);
    CHECK(fabs(gpr_stats_estimated_hz(&s) - 1000.0) < 1e-6, "10x1ms -> 1000Hz");
    gpr_stats_free(&s);
}

static void test_jittery_1000hz(void) {
    /* Simulated 1000 Hz poll with +-0.3ms scheduling jitter. Median must hold. */
    gpr_stats_t s;
    gpr_stats_init(&s);
    double vals[] = { 1.0, 0.8, 1.2, 0.9, 1.1, 1.0, 0.7, 1.3, 1.0, 1.0,
                      0.9, 1.1, 1.0, 0.8, 1.2, 1.0, 1.0, 0.9, 1.1, 1.0 };
    for (unsigned i = 0; i < sizeof(vals) / sizeof(vals[0]); i++)
        assert(gpr_stats_add(&s, vals[i]) == 0);
    CHECK(fabs(gpr_stats_median(&s) - 1.0) < 1e-9, "jittered median still 1ms");
    CHECK(gpr_snap_rate(gpr_stats_estimated_hz(&s)) == 1000, "jittered snaps to 1000Hz");
    CHECK(gpr_stats_mode_bin(&s) >= 0, "histogram has a mode bin");
    gpr_stats_free(&s);
}

int main(void) {
    test_basic_stats();
    test_percentiles();
    test_snap_table();
    test_too_few_samples();
    test_jittery_1000hz();
    if (failures == 0) {
        printf("\nAll tests passed.\n");
        return 0;
    }
    printf("\n%d test(s) FAILED.\n", failures);
    return 1;
}
