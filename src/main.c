#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device.h"
#include "monitor.h"

#define GPR_VERSION "1.0.0"

static void print_usage(const char *prog) {
    printf("gpr %s - measure gamepad USB polling rate on Linux\n", GPR_VERSION);
    printf("\nUsage:\n");
    printf("  %s list [--json]\n", prog);
    printf("  %s monitor [-d DEVICE] [-t SECS] [--csv [FILE]] [--json [FILE]] [--no-live]\n", prog);
    printf("  %s benchmark [-d DEVICE] [-t SECS] [--csv [FILE]]\n", prog);
    printf("\nCommands:\n");
    printf("  list       show /dev/input/event* devices, flag likely gamepads\n");
    printf("  monitor    live polling-rate measurement (wiggle sticks while running)\n");
    printf("  benchmark  fixed 10 s measurement with countdown (default), same engine\n");
    printf("\nMonitor options:\n");
    printf("  -d, --device PATH   evdev node, e.g. /dev/input/event5 (else interactive)\n");
    printf("  -t, --time SECS     stop after SECS seconds (0 = until Ctrl+C, default 0)\n");
    printf("  -r, --rate MS       live refresh in ms (default 200)\n");
    printf("      --csv [FILE]    per-report timing log; auto-creates gpr-*.csv if omitted\n");
    printf("      --json [FILE]   final report as JSON on stdout, auto-saves gpr-*.json if omitted\n");
    printf("      --no-live       suppress the updating status line\n");
    printf("  -h, --help          show this help\n");
    printf("  -v, --version       show version\n");
    printf("\nExamples:\n");
    printf("  %s list\n", prog);
    printf("  sudo %s monitor -d /dev/input/event5 -t 10\n", prog);
    printf("  sudo %s benchmark -d /dev/input/event5 --csv run.csv\n", prog);
    printf("\nHow to read results: trust the median interval. 1 ms ~= 1000 Hz,\n"
           "2 ms ~= 500 Hz, 4 ms ~= 250 Hz, 8 ms ~= 125 Hz. Keep a stick moving;\n"
           "an idle pad sends nothing and the average will sag.\n");
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

static int cmd_list_json(void) {
    gpr_device_list_t list;
    if (gpr_scan_devices(&list) != 0)
        return 1;
    printf("{\"devices\":[");
    for (size_t i = 0; i < list.count; i++) {
        const gpr_device_t *d = &list.items[i];
        printf("%s{\"path\":", i ? "," : "");
        print_json_string(stdout, d->path);
        printf(",\"name\":");
        print_json_string(stdout, d->name);
        printf(",\"gamepad\":%s}", d->is_gamepad ? "true" : "false");
    }
    printf("]}\n");
    return 0;
}

int main(int argc, char **argv) {
    const char *prog = argc > 0 ? argv[0] : "gpr";

    if (argc < 2) {
        print_usage(prog);
        return 1;
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0 ||
        strcmp(argv[1], "help") == 0) {
        print_usage(prog);
        return 0;
    }
    if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0 ||
        strcmp(argv[1], "version") == 0) {
        printf("gpr %s\n", GPR_VERSION);
        return 0;
    }

    if (strcmp(argv[1], "list") == 0) {
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--json") == 0)
                return cmd_list_json();
            fprintf(stderr, "error: unknown list option '%s'\n", argv[i]);
            return 1;
        }
        gpr_device_list_t list;
        if (gpr_scan_devices(&list) != 0)
            return 1;
        gpr_print_devices(&list);
        return 0;
    }

    if (strcmp(argv[1], "monitor") == 0 || strcmp(argv[1], "benchmark") == 0) {
        bool is_bench = strcmp(argv[1], "benchmark") == 0;
        gpr_monitor_opts_t opts = {
            .device_path = NULL,
            .duration_sec = is_bench ? 10.0 : 0.0,
            .refresh_ms = 200,
            .csv_path = NULL,
            .csv_auto = false,
            .json_output = false,
            .json_path = NULL,
            .no_live = false,
        };
        for (int i = 2; i < argc; i++) {
            if ((strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--device") == 0) && i + 1 < argc) {
                opts.device_path = argv[++i];
            } else if ((strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--time") == 0) && i + 1 < argc) {
                const char *arg = argv[++i];
                char *end = NULL;
                errno = 0;
                double v = strtod(arg, &end);
                if (errno != 0 || end == arg || *end != '\0' || !isfinite(v) ||
                    v < 0) {
                    fprintf(stderr, "error: --time must be a number >= 0 (got '%s')\n",
                            arg);
                    return 1;
                }
                opts.duration_sec = v;
            } else if ((strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--rate") == 0) && i + 1 < argc) {
                const char *arg = argv[++i];
                char *end = NULL;
                errno = 0;
                long v = strtol(arg, &end, 10);
                if (errno != 0 || end == arg || *end != '\0' || v < 50 || v > 2000) {
                    fprintf(stderr,
                            "error: --rate must be an integer 50..2000 ms (got '%s')\n",
                            arg);
                    return 1;
                }
                opts.refresh_ms = (int)v;
            } else if (strcmp(argv[i], "--csv") == 0) {
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    opts.csv_path = argv[++i];
                } else {
                    opts.csv_auto = true;
                }
            } else if (strcmp(argv[i], "--json") == 0) {
                opts.json_output = true;
                opts.no_live = true;
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    opts.json_path = argv[++i];
                }
            } else if (strcmp(argv[i], "--no-live") == 0) {
                opts.no_live = true;
            } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
                print_usage(prog);
                return 0;
            } else {
                fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
                print_usage(prog);
                return 1;
            }
        }
        return gpr_monitor(&opts);
    }

    fprintf(stderr, "error: unknown command '%s'\n", argv[1]);
    print_usage(prog);
    return 1;
}
