#include "device.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define BITS_PER_LONG (sizeof(unsigned long) * 8)
#define NBITS(x) ((((x) - 1) / BITS_PER_LONG) + 1)
#define TEST_BIT(bit, array) \
    (((array)[(bit) / BITS_PER_LONG] >> ((bit) % BITS_PER_LONG)) & 1UL)

static bool has_gamepad_button(const unsigned long *key_bits) {
    /* Joystick trigger/base block 0x120-0x12f (BTN_TRIGGER..BTN_DEAD).
     * Deliberately excludes 0x140-0x14f (BTN_TOOL_PEN..BTN_TOUCH etc.),
     * which touchpads/tablets expose and which a wider BTN_JOYSTICK+64
     * check would misclassify as gamepads. */
    for (int b = BTN_TRIGGER; b <= BTN_DEAD; b++) {
        if (b < KEY_MAX && TEST_BIT(b, key_bits))
            return true;
    }
    for (int b = BTN_GAMEPAD; b <= BTN_THUMBR; b++) {
        if (b < KEY_MAX && TEST_BIT(b, key_bits))
            return true;
    }
    /* D-pad hat buttons sometimes exposed as gamepad range aliases. */
    if (TEST_BIT(BTN_DPAD_UP, key_bits) || TEST_BIT(BTN_DPAD_DOWN, key_bits) ||
        TEST_BIT(BTN_DPAD_LEFT, key_bits) || TEST_BIT(BTN_DPAD_RIGHT, key_bits))
        return true;
    return false;
}

static int read_one_device(const char *path, gpr_device_t *out) {
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        return -1;

    memset(out, 0, sizeof(*out));
    snprintf(out->path, sizeof(out->path), "%s", path);

    char name[GPR_NAME_LEN] = "unknown";
    if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0) {
        snprintf(out->name, sizeof(out->name), "%s", name);
    } else {
        snprintf(out->name, sizeof(out->name), "unknown");
    }

    char phys[GPR_NAME_LEN] = "";
    if (ioctl(fd, EVIOCGPHYS(sizeof(phys) - 1), phys) >= 0)
        snprintf(out->phys, sizeof(out->phys), "%s", phys);

    struct input_id iid = { 0, 0, 0, 0 };
    if (ioctl(fd, EVIOCGID, &iid) == 0) {
        out->bustype = iid.bustype;
        out->vendor = iid.vendor;
        out->product = iid.product;
    }

    unsigned long ev_bits[NBITS(EV_MAX)] = { 0 };
    unsigned long key_bits[NBITS(KEY_MAX)] = { 0 };
    unsigned long abs_bits[NBITS(ABS_MAX)] = { 0 };
    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) >= 0) {
        if (TEST_BIT(EV_KEY, ev_bits) &&
            ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) >= 0) {
            out->is_gamepad = has_gamepad_button(key_bits);
        }
        if (TEST_BIT(EV_ABS, ev_bits) &&
            ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) >= 0) {
            out->has_abs_xy = TEST_BIT(ABS_X, abs_bits) && TEST_BIT(ABS_Y, abs_bits);
            /* A device with sticks/hats plus buttons is almost surely a pad. */
            if (out->has_abs_xy && out->is_gamepad)
                out->is_gamepad = true;
        }
    }

    /* Name-based fallback for drivers that expose odd capabilities. */
    if (!out->is_gamepad) {
        char lower[GPR_NAME_LEN];
        size_t n = strlen(out->name);
        if (n >= sizeof(lower))
            n = sizeof(lower) - 1;
        for (size_t i = 0; i < n; i++) {
            char c = out->name[i];
            lower[i] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        lower[n] = '\0';
        if (strstr(lower, "gamepad") || strstr(lower, "controller") ||
            strstr(lower, "joystick") || strstr(lower, "xbox") ||
            strstr(lower, "playstation") || strstr(lower, "dualshock") ||
            strstr(lower, "dualsense") || strstr(lower, "nintendo") ||
            strstr(lower, "switch pro") || strstr(lower, "8bitdo") ||
            strstr(lower, "game pad") || strstr(lower, "steam deck"))
            out->is_gamepad = true;
    }

    close(fd);
    return 0;
}

static int cmp_event_path(const void *a, const void *b) {
    const gpr_device_t *da = (const gpr_device_t *)a;
    const gpr_device_t *db = (const gpr_device_t *)b;
    /* Sort event9 after event10 numerically, not lexicographically. */
    int na = atoi(da->path + 16); /* strlen("/dev/input/event") == 16 */
    int nb = atoi(db->path + 16);
    return (na > nb) - (na < nb);
}

int gpr_scan_devices(gpr_device_list_t *out) {
    memset(out, 0, sizeof(*out));

    DIR *d = opendir("/dev/input");
    if (!d) {
        fprintf(stderr, "error: cannot open /dev/input: %s\n", strerror(errno));
        return -1;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, "event", 5) != 0)
            continue;
        if (out->count >= GPR_MAX_DEVICES)
            break;
        char path[GPR_PATH_LEN];
        snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
        /* Skip unreadable nodes here; open() in monitor explains EACCES. */
        if (read_one_device(path, &out->items[out->count]) == 0)
            out->count++;
    }
    closedir(d);

    qsort(out->items, out->count, sizeof(out->items[0]), cmp_event_path);
    return 0;
}

void gpr_print_devices(const gpr_device_list_t *list) {
    if (list->count == 0) {
        printf("No evdev devices found under /dev/input.\n");
        return;
    }
    printf("%-4s %-20s %-45s %s\n", "ID", "DEVICE", "NAME", "TYPE");
    for (size_t i = 0; i < list->count; i++) {
        const gpr_device_t *dev = &list->items[i];
        printf("%-4zu %-20s %-45.45s %s\n", i, dev->path, dev->name,
               dev->is_gamepad ? "gamepad?" : "-");
    }
}

int gpr_open_device(const char *path) {
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        if (errno == EACCES) {
            fprintf(stderr,
                    "error: permission denied opening %s\n"
                    "  You need read access to the evdev node. Try:\n"
                    "    sudo usermod -aG input \"$USER\"   # then log out/in\n"
                    "  or run once with sudo for a quick test:\n"
                    "    sudo gpr monitor -d %s\n"
                    "  A udev rule is provided in udev/99-gamepad-polling-rate.rules.\n",
                    path, path);
        } else if (errno == ENOENT) {
            fprintf(stderr, "error: device %s does not exist. See `list`.\n", path);
        } else {
            fprintf(stderr, "error: cannot open %s: %s\n", path, strerror(errno));
        }
        return -1;
    }
    return fd;
}

int gpr_prompt_device(char *out_path, size_t out_len) {
    gpr_device_list_t list;
    if (gpr_scan_devices(&list) != 0)
        return -1;
    gpr_print_devices(&list);
    if (list.count == 0)
        return -1;

    /* Preselect the first gamepad candidate to save typing. */
    size_t def = 0;
    for (size_t i = 0; i < list.count; i++) {
        if (list.items[i].is_gamepad) {
            def = i;
            break;
        }
    }

    printf("\nSelect device [default %zu]: ", def);
    fflush(stdout);

    char line[64] = "";
    if (!fgets(line, sizeof(line), stdin)) {
        fprintf(stderr, "error: failed to read selection.\n");
        return -1;
    }
    if (line[0] == '\n' || line[0] == '\0') {
        snprintf(out_path, out_len, "%s", list.items[def].path);
        return 0;
    }
    char *end = NULL;
    long idx = strtol(line, &end, 10);
    if (end == line || idx < 0 || (size_t)idx >= list.count) {
        fprintf(stderr, "error: invalid selection.\n");
        return -1;
    }
    snprintf(out_path, out_len, "%s", list.items[(size_t)idx].path);
    return 0;
}
