CC ?= gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS = -lm

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

SRC = src/main.c src/device.c src/stats.c src/monitor.c
OBJ = $(SRC:.c=.o)
BIN = gpr

TEST_SRC = tests/test_stats.c src/stats.c
TEST_BIN = tests/test_stats

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L -Isrc -c $< -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRC) src/stats.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_stats.c src/stats.c $(LDFLAGS) $(LDLIBS)

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	install -Dm644 udev/99-gamepad-polling-rate.rules $(DESTDIR)/usr/lib/udev/rules.d/99-gamepad-polling-rate.rules || true

clean:
	rm -f $(OBJ) $(BIN) $(TEST_BIN)

.PHONY: all test install clean
