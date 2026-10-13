# Usage:
#   make                                   native build (x86_64)
#   make CROSS_COMPILE=aarch64-linux-gnu-  cross build for Raspberry Pi 5
#   make deploy CROSS_COMPILE=aarch64-linux-gnu- LDFLAGS=-static
#   make clean
#
# Why LDFLAGS=-static for the Pi: the cross-compiler on the HOST has a newer
# glibc (2.43) than the TARGET (2.41), and cfsetispeed/cfsetospeed need
# GLIBC_2.42. A dynamic binary would not start on the Pi.

CROSS_COMPILE ?=
CC := $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Wextra -O2 -g
LDFLAGS ?=

TARGET_USER ?= roman
TARGET_HOST ?= pi5.local
TARGET_DIR ?= ~/

BIN := mycom
SRC := main.c

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f $(BIN) *.o

deploy: $(BIN)
	scp $(BIN) $(TARGET_USER)@$(TARGET_HOST):$(TARGET_DIR)

.PHONY: all clean deploy
