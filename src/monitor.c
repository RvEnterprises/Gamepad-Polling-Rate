#include "monitor.h"

#include <errno.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "device.h"
#include "stats.h"

static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig) {
    (void)sig;
    g_stop = 1;
}

static double now_monotonic_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void clear_line(void) {
    printf("\r\033[K");
    fflush(stdout);
}

static void print_live(double elapsed, unsigned long reports, unsigned long events,
                       double win_hz, double avg_hz, double last_interval_ms) {
    clear_line();
    printf("[%6.1fs] reports=%lu events=%lu | cur=%6.1f Hz avg=%6.1f Hz last_dt=%5.2f ms  (Ctrl+C to stop)",
           elapsed, reports, events, win_hz, avg_hz, last_interval_ms);
    fflush(stdout);
}

static const char *bin_label(int i, char *buf, size_t len) {
    double lo = GPR_HIST_EDGES[i];
    double hi = GPR_HIST_EDGES[i + 1];
    if (hi > 1e11)
        snprintf(buf, len, "%6.1f+ ms", lo);
    else
        snprintf(buf, len, "%5.2f-%5.2f", lo, hi);
    return buf;
}

static void print_histogram(const gpr_stats_t *s) {
    size_t peak = 0;
    for (int i = 0; i < GPR_HIST_BINS; i++)
        if (s->hist[i] > peak)
            peak = s->hist[i];
    if (peak == 0)
        return;
    printf("\nInterval histogram (inter-report dt):\n");
    for (int i = 0; i < GPR_HIST_BINS; i++) {
        char label[32];
        int width = (int)((s->hist[i] * 40 + peak - 1) / peak);
        printf("  %s ms : %7zu |", bin_label(i, label, sizeof(label)), s->hist[i]);
        for (int k = 0; k < width; k++)
            putchar('#');
        putchar('\n');
    }
}

static void print_report_human(const char *path, const char *name, double elapsed,
                               unsigned long reports, unsigned long total_events,
                               unsigned long abs_count, unsigned long key_count,
                               gpr_stats_t *st) {
    printf("\n\n===== Gamepad polling-rate report =====\n");
    printf("Device : %s (%s)\n", path, name);
    printf("Elapsed: %.2f s  reports: %lu  events: %lu (ABS=%lu KEY=%lu)\n", elapsed,
           reports, total_events, abs_count, key_count);

    if (reports < 2) {
        printf("Not enough data: no input reports received.\n"
               "Move a stick / press buttons during the test; idle pads send nothing.\n");
        return;
    }

    double avg_hz = elapsed > 0 ? (double)reports / elapsed : 0.0;
    double mean = gpr_stats_mean(st);
    double sd = gpr_stats_stddev(st);
    double p1 = gpr_stats_percentile(st, 1);
    double p5 = gpr_stats_percentile(st, 5);
    double med = gpr_stats_median(st);
    double p95 = gpr_stats_percentile(st, 95);
    double p99 = gpr_stats_percentile(st, 99);
    double est = gpr_stats_estimated_hz(st);
    int snapped = gpr_snap_rate(est);

    printf("Avg rate (reports/elapsed) : %8.1f Hz\n", avg_hz);
    printf("Intervals (n=%zu) min=%5.2f mean=%5.2f sd=%5.2f ms\n", gpr_stats_count(st),
           gpr_stats_min(st), mean, sd);
    printf("  p1=%5.2f p5=%5.2f median=%5.2f p95=%5.2f p99=%5.2f max=%5.2f ms\n", p1, p5,
           med, p95, p99, gpr_stats_max(st));
    if (est > 0.0)
        printf("Estimated polling rate (from median): %.0f Hz -> ~%d Hz\n", est, snapped);
    else
        printf("Estimated polling rate: n/a (need >= 10 reports while moving input)\n");

    print_histogram(st);

    printf("\nTip: wiggle an analog stick in full circles for the whole run.\n"
           "Buttons alone give sparse samples; sticks stream at the USB poll rate.\n"
           "Idle time lowers the average, the median is the rate to trust.\n");
}

static void print_json_string(FILE *out, const char *s) {
    putc('"', out);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"':
            fputs("\\\"", out);
            break;
        case '\\':
            fputs("\\\\", out);
            break;
        case '\b':
            fputs("\\b", out);
            break;
        case '\f':
            fputs("\\f", out);
            break;
        case '\n':
            fputs("\\n", out);
            break;
        case '\r':
            fputs("\\r", out);
            break;
        case '\t':
            fputs("\\t", out);
            break;
        default:
            if (*p < 0x20)
                fprintf(out, "\\u%04x", *p);
            else
                putc(*p, out);
            break;
        }
    }
    putc('"', out);
}

static void print_report_json(const char *path, const char *name, double elapsed,
                              unsigned long reports, unsigned long total_events,
                              gpr_stats_t *st, FILE *out, bool pretty) {
    double avg_hz = elapsed > 0 ? (double)reports / elapsed : 0.0;
    double est = gpr_stats_estimated_hz(st);
    double min = gpr_stats_min(st);
    double mean = gpr_stats_mean(st);
    double sd = gpr_stats_stddev(st);
    double p1 = gpr_stats_percentile(st, 1);
    double p5 = gpr_stats_percentile(st, 5);
    double med = gpr_stats_median(st);
    double p95 = gpr_stats_percentile(st, 95);
    double p99 = gpr_stats_percentile(st, 99);
    double max = gpr_stats_max(st);
    int snapped = gpr_snap_rate(est);
    if (!pretty) {
        fputs("{\"device\":", out);
        print_json_string(out, path);
        fputs(",\"name\":", out);
        print_json_string(out, name);
        fprintf(out, ",\"elapsed_s\":%.3f,"
               "\"reports\":%lu,\"events\":%lu,\"avg_hz\":%.2f,"
               "\"min_ms\":%.3f,\"mean_ms\":%.3f,\"stddev_ms\":%.3f,"
               "\"p1_ms\":%.3f,\"p5_ms\":%.3f,\"median_ms\":%.3f,"
               "\"p95_ms\":%.3f,\"p99_ms\":%.3f,\"max_ms\":%.3f,"
               "\"estimated_hz\":%.1f,\"nearest_standard_hz\":%d}\n",
               elapsed, reports, total_events, avg_hz, min,
               mean, sd, p1, p5, med, p95, p99, max, est, snapped);
        return;
    }
    fputs("{\n  \"device\": ", out);
    print_json_string(out, path);
    fputs(",\n  \"name\": ", out);
    print_json_string(out, name);
    fprintf(out,
            ",\n"
            "  \"elapsed_s\": %.3f,\n"
            "  \"reports\": %lu,\n"
            "  \"events\": %lu,\n"
            "  \"avg_hz\": %.2f,\n"
            "  \"min_ms\": %.3f,\n"
            "  \"mean_ms\": %.3f,\n"
            "  \"stddev_ms\": %.3f,\n"
            "  \"p1_ms\": %.3f,\n"
            "  \"p5_ms\": %.3f,\n"
            "  \"median_ms\": %.3f,\n"
            "  \"p95_ms\": %.3f,\n"
            "  \"p99_ms\": %.3f,\n"
            "  \"max_ms\": %.3f,\n"
            "  \"estimated_hz\": %.1f,\n"
            "  \"nearest_standard_hz\": %d\n"
            "}\n",
            elapsed, reports, total_events, avg_hz, min,
            mean, sd, p1, p5, med, p95, p99, max, est, snapped);
}

/* Build gpr-<device>-<timestamp>.<ext> so bare --csv/--json still save a file. */
static void build_auto_filename(char *buf, size_t len, const char *device_path,
                                const char *ext) {
    const char *base = strrchr(device_path, '/');
    base = base ? base + 1 : device_path;
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    snprintf(buf, len, "gpr-%s-%04d%02d%02d-%02d%02d%02d.%s", base,
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
             tm.tm_sec, ext);
}

int gpr_monitor(const gpr_monitor_opts_t *opts) {    char selected[GPR_PATH_LEN] = "";
    const char *path = opts->device_path;
    if (!path || !path[0]) {
        if (gpr_prompt_device(selected, sizeof(selected)) != 0)
            return 1;
        path = selected;
    }

    int fd = gpr_open_device(path);
    if (fd < 0)
        return 1;

    char devname[GPR_NAME_LEN] = "unknown";
    if (ioctl(fd, EVIOCGNAME(sizeof(devname) - 1), devname) < 0)
        snprintf(devname, sizeof(devname), "unknown");

    FILE *csv = NULL;
    char auto_csv[256] = "";
    const char *csv_path = opts->csv_path;
    if (!csv_path && opts->csv_auto) {
        build_auto_filename(auto_csv, sizeof(auto_csv), path, "csv");
        csv_path = auto_csv;
    }
    if (csv_path) {
        csv = fopen(csv_path, "w");
        if (!csv) {
            fprintf(stderr, "error: cannot write CSV %s: %s\n", csv_path,
                    strerror(errno));
            close(fd);
            return 1;
        }
        fprintf(csv, "t_s,dt_ms,reports,events\n");
        /* Keep stdout pure JSON when --json is active; notes go to stderr. */
        fprintf(opts->json_output ? stderr : stdout, "Logging CSV to %s\n", csv_path);
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    g_stop = 0;

    gpr_stats_t st;
    gpr_stats_init(&st);

    unsigned long reports = 0, total_events = 0, abs_count = 0, key_count = 0;
    double start = now_monotonic_sec();
    double last_report_t = -1.0;
    double last_interval_ms = 0.0;
    double last_draw = start;
    double win_start = start;
    unsigned long win_reports = 0;
    double win_hz = 0.0;
    double deadline = opts->duration_sec > 0 ? start + opts->duration_sec : -1.0;

    if (!opts->json_output && !opts->no_live) {
        printf("Monitoring %s (%s)\n", path, devname);
        printf("Wiggle the LEFT/RIGHT stick in circles now. Idle pads send no data.\n");
        if (deadline > 0)
            printf("Running for %.1f s...\n", opts->duration_sec);
        else
            printf("Running until Ctrl+C...\n");
    }

    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    while (!g_stop) {
        double now = now_monotonic_sec();
        if (deadline > 0 && now >= deadline)
            break;

        int timeout = opts->refresh_ms > 0 ? opts->refresh_ms : 200;
        if (deadline > 0) {
            double left_ms = (deadline - now) * 1000.0;
            if (left_ms < 0)
                break;
            if (left_ms < timeout)
                timeout = (int)left_ms;
        }

        int pr = poll(&pfd, 1, timeout);
        now = now_monotonic_sec();

        if (pr < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "\nerror: poll failed: %s\n", strerror(errno));
            break;
        }

        if (pr > 0 && (pfd.revents & POLLIN)) {
            struct input_event ev[64];
            for (;;) {
                ssize_t n = read(fd, ev, sizeof(ev));
                if (n < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;
                    if (errno == EINTR)
                        break;
                    fprintf(stderr, "\nerror: read failed: %s\n", strerror(errno));
                    g_stop = 1;
                    break;
                }
                if (n == 0)
                    break;
                size_t nev = (size_t)n / sizeof(struct input_event);
                double arrival = now_monotonic_sec();
                unsigned syn_count = 0;
                for (size_t i = 0; i < nev; i++) {
                    total_events++;
                    if (ev[i].type == EV_ABS)
                        abs_count++;
                    else if (ev[i].type == EV_KEY)
                        key_count++;
                    else if (ev[i].type == EV_SYN && ev[i].code == SYN_REPORT)
                        syn_count++;
                }
                /* One SYN_REPORT batch = one device report. This is the
                 * polling clock; raw event counts inflate with multi-axis moves.
                 * One read() can coalesce several reports, so count each SYN
                 * individually. Coalesced reports share one arrival timestamp,
                 * so split the elapsed time evenly instead of emitting a burst
                 * of 0 ms intervals (which would drag the median toward inf Hz)
                 * or collapsing to one report (which undercounts). */
                if (syn_count > 0) {
                    double step = 0.0;
                    if (last_report_t >= 0) {
                        step = (arrival - last_report_t) / (double)syn_count;
                        if (!(step >= 0.0))
                            step = 0.0;
                    }
                    for (unsigned s = 0; s < syn_count; s++) {
                        if (last_report_t >= 0) {
                            last_interval_ms = step * 1000.0;
                            gpr_stats_add(&st, last_interval_ms);
                            last_report_t += step;
                        } else {
                            last_report_t = arrival;
                            last_interval_ms = 0.0;
                        }
                        reports++;
                        win_reports++;
                        if (csv)
                            fprintf(csv, "%.6f,%.4f,%lu,%lu\n", last_report_t - start,
                                    last_interval_ms, reports, total_events);
                    }
                    last_report_t = arrival;
                }
                if ((size_t)n < sizeof(ev))
                    break;
            }
        }

        /* 1-second sliding window for the "current Hz" readout. */
        if (now - win_start >= 1.0) {
            win_hz = (double)win_reports / (now - win_start);
            win_start = now;
            win_reports = 0;
        }

        if (!opts->json_output && !opts->no_live && now - last_draw >= opts->refresh_ms / 1000.0) {
            double elapsed = now - start;
            double avg = elapsed > 0 ? (double)reports / elapsed : 0.0;
            print_live(elapsed, reports, total_events, win_hz, avg, last_interval_ms);
            last_draw = now;
        }
    }

    double elapsed = now_monotonic_sec() - start;
    if (csv)
        fclose(csv);
    close(fd);

    if (!opts->json_output && !opts->no_live) {
        clear_line();
        printf("Done after %.1f s: %lu reports, %lu events.\n", elapsed, reports,
               total_events);
    }

    if (opts->json_output) {
        print_report_json(path, devname, elapsed, reports, total_events, &st, stdout, false);
        char auto_json[256] = "";
        const char *json_path = opts->json_path;
        if (!json_path) {
            build_auto_filename(auto_json, sizeof(auto_json), path, "json");
            json_path = auto_json;
        }
        FILE *jf = fopen(json_path, "w");
        if (!jf) {
            fprintf(stderr, "warning: cannot write JSON %s: %s\n", json_path,
                    strerror(errno));
        } else {
            print_report_json(path, devname, elapsed, reports, total_events, &st, jf, true);
            fclose(jf);
            fprintf(stderr, "Saved JSON report to %s\n", json_path);
        }
    } else
        print_report_human(path, devname, elapsed, reports, total_events, abs_count,
                           key_count, &st);

    gpr_stats_free(&st);
    return 0;
}
