CC ?= gcc
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L
LDFLAGS ?=
LDLIBS ?= -lm -lpthread

ROOT_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
INC := -I$(ROOT_DIR)/firmware/include
BUILD := $(ROOT_DIR)/build
FW_SRC := $(wildcard $(ROOT_DIR)/firmware/src/*.c)

.PHONY: all test run simulate decode clean format-check

all: $(BUILD)/host_sim $(BUILD)/dexhand_tests

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/host_sim: $(FW_SRC) $(ROOT_DIR)/examples/host_sim/main.c | $(BUILD)
	$(CC) $(CFLAGS) $(INC) $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD)/dexhand_tests: $(FW_SRC) $(ROOT_DIR)/tests/test_main.c | $(BUILD)
	$(CC) $(CFLAGS) $(INC) $^ -o $@ $(LDFLAGS) $(LDLIBS)

test: $(BUILD)/dexhand_tests
	$(BUILD)/dexhand_tests
	python3 $(ROOT_DIR)/tools/test_dexlog_decode.py

simulate: $(BUILD)/host_sim
	mkdir -p $(ROOT_DIR)/examples/out
	$(BUILD)/host_sim --cycles 1200 --joints 20 --out $(ROOT_DIR)/examples/out/session.dexlog

run: simulate
	python3 $(ROOT_DIR)/tools/dexlog_decode.py $(ROOT_DIR)/examples/out/session.dexlog --strict

decode:
	@test -n "$(FILE)" || { echo "Usage: make decode FILE=path/to/session.dexlog"; exit 1; }
	python3 $(ROOT_DIR)/tools/dexlog_decode.py $(FILE) --strict

clean:
	rm -rf $(BUILD) $(ROOT_DIR)/examples/out
