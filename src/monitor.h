#ifndef GPR_MONITOR_H
#define GPR_MONITOR_H

#include <stdbool.h>

typedef struct {
    const char *device_path;
    double duration_sec;      /* 0 = run until Ctrl+C */
    int refresh_ms;           /* live display refresh, default 200 */
    const char *csv_path;     /* per-report CSV log, NULL = off (unless csv_auto) */
    bool csv_auto;            /* auto-generate a timestamped CSV filename */
    bool json_output;         /* print final report as JSON */
    const char *json_path;    /* optional file to also save the JSON report */
    bool no_live;             /* suppress live updating line */
} gpr_monitor_opts_t;

/* Monitor an evdev device and print a polling-rate report.
 * Returns 0 on success, nonzero on error. */
int gpr_monitor(const gpr_monitor_opts_t *opts);

#endif
