# Gamepad Polling Rate

Measure the USB polling rate of any gamepad on Linux. Zero dependencies, plain C11, reads directly from evdev (`/dev/input/event*`).

It counts `SYN_REPORT` input batches per second while you move a stick, then reports average rate, interval percentiles, jitter, a histogram, and an estimated polling rate snapped to standard USB rates (1000 / 500 / 250 / 125 Hz, ...).

## Quick start

```sh
make
./gpr list
sudo ./gpr monitor -d /dev/input/event5 -t 10
```

While it runs, **wiggle an analog stick in full circles**. An idle pad sends no events, so there is nothing to measure.

Example output:

```
[   5.0s] reports=2512 events=7536 | cur= 502.4 Hz avg= 502.4 Hz last_dt= 1.99 ms  (Ctrl+C to stop)

===== Gamepad polling-rate report =====
Device : /dev/input/event5 (Xbox Wireless Controller)
Elapsed: 10.00 s  reports: 5021  events: 15063 (ABS=10042 KEY=0)
Avg rate (reports/elapsed) :    502.1 Hz
Intervals (n=5020) min= 1.12 mean= 1.99 sd= 0.41 ms
  p1= 1.20 p5= 1.45 median= 2.00 p95= 2.60 p99= 3.10 max= 8.40 ms
Estimated polling rate (from median): 500 Hz -> ~500 Hz
```

## How it works

USB gamepads are polled by the host at a fixed rate (common values: 125, 250, 500, 1000 Hz). On Linux each poll that changes state surfaces as one evdev report (a batch of events terminated by `SYN_REPORT`).

This tool timestamps every report arrival with `CLOCK_MONOTONIC`, computes inter-report intervals, and derives:

- **Average rate** = reports / elapsed seconds (sags if you pause mid-test)
- **Median interval** = the trustworthy estimator; `1000 / median_ms` is the polling rate
- **p1/p5/p95/p99, stddev** = jitter picture
- **Histogram** = shows whether the rate is stable or bimodal (e.g. 500 Hz with 1000 Hz bursts)

Trust the median, not the average. Pauses and the first/last partial second drag the average down.

## Commands

```
gpr list [--json]
gpr monitor [-d DEVICE] [-t SECS] [-r MS] [--csv [FILE]] [--json [FILE]] [--no-live]
gpr benchmark [-d DEVICE] [-t SECS] [--csv [FILE]]
```

| Option | Default | Meaning |
|---|---|---|
| `-d, --device PATH` | interactive prompt | evdev node, e.g. `/dev/input/event5` |
| `-t, --time SECS` | `0` (until Ctrl+C), `10` for benchmark | fixed run length |
| `-r, --rate MS` | `200` | live display refresh |
| `--csv [FILE]` | off | per-report log (`t_s,dt_ms,reports,events`); auto-creates `gpr-<device>-<timestamp>.csv` if FILE is omitted |
| `--json [FILE]` | off | one-line JSON final report on stdout, auto-saves pretty-printed `gpr-<device>-<timestamp>.json` if FILE is omitted (implies `--no-live`) |
| `--no-live` | off | suppress the updating status line |

More examples:

```sh
# Pick interactively (no -d flag)
sudo ./gpr monitor -t 10

# Script-friendly JSON
sudo ./gpr monitor -d /dev/input/event5 -t 5 --json

# Log intervals and plot them (filename optional, auto-created if omitted)
sudo ./gpr monitor -d /dev/input/event5 -t 10 --csv run.csv
sudo ./gpr monitor -d /dev/input/event5 -t 10 --csv
python3 -c "import csv,statistics; d=[float(r[1]) for r in list(csv.reader(open('run.csv')))[1:] if float(r[1])>0]; print(f'median={statistics.median(d):.2f}ms -> {1000/statistics.median(d):.0f}Hz')"
```

## Permissions (avoid sudo)

Reading evdev needs access to `/dev/input/event*`. Three options:

1. Quick test: run with `sudo`.
2. Persistent, recommended: add yourself to the `input` group once, then log out/in:
   ```sh
   sudo usermod -aG input "$USER"
   ```
3. Distro-friendly: install the provided udev rule:
   ```sh
   sudo cp udev/99-gamepad-polling-rate.rules /etc/udev/rules.d/
   sudo udevadm control --reload-rules && sudo udevadm trigger
   # or: sudo make install
   ```

## Build & install

Requirements: `gcc`, `make`, Linux headers. No SDL, no libevdev, no other dependencies.

```sh
make          # builds ./gpr
make test     # builds and runs the stats unit tests
sudo make install   # installs to /usr/local/bin + udev rule
make clean
```

Strict build is the default: `-std=c11 -O2 -Wall -Wextra -Wpedantic`.

## Getting a good measurement

1. `gpr list`, find your pad (flagged `gamepad?`).
2. Run `monitor -t 10` and rotate **both sticks continuously** at moderate speed.
3. Keep the window focused, close heavy background load for the run.
4. Read the **median / estimated rate**, check the histogram is a single sharp peak.
5. Repeat wired vs. wireless / 2.4 GHz dongle vs. Bluetooth; expect e.g. 1000 Hz wired, 250-500 Hz dongle, 125-250 Hz Bluetooth depending on the pad.

Caveats (these are system properties, not bugs):

- Bluetooth pads and some drivers batch or filter reports; what you see is the *effective* report rate, which is the number that matters for latency.
- `xbox`, `xpadneo`, `ds360` style drivers can alter timing vs. raw USB.
- VMs and compositor load add jitter; p95/p99 expose it.

gpr measures the effective report rate seen by the OS, not the raw USB bus rate, and that effective rate is the number that matters for game latency.
