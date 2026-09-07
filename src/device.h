#ifndef GPR_DEVICE_H
#define GPR_DEVICE_H

#include <stdbool.h>
#include <stddef.h>

#define GPR_MAX_DEVICES 64
#define GPR_PATH_LEN 272
#define GPR_NAME_LEN 256

typedef struct {
    char path[GPR_PATH_LEN];
    char name[GPR_NAME_LEN];
    char phys[GPR_NAME_LEN];
    bool is_gamepad;
    bool has_abs_xy;
    unsigned bustype;
    unsigned vendor;
    unsigned product;
} gpr_device_t;

typedef struct {
    gpr_device_t items[GPR_MAX_DEVICES];
    size_t count;
} gpr_device_list_t;

/* Scan /dev/input/event* and fill list. Returns 0 on success. */
int gpr_scan_devices(gpr_device_list_t *out);

/* Print device list as a human-readable table. */
void gpr_print_devices(const gpr_device_list_t *list);

/* Open an evdev device read-only, non-blocking.
 * On failure prints a helpful message to stderr and returns -1. */
int gpr_open_device(const char *path);

/* Interactive prompt: list devices and ask the user to pick one.
 * Returns 0 on success with chosen path in out_path. */
int gpr_prompt_device(char *out_path, size_t out_len);

#endif
