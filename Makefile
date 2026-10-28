CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -pedantic -O2
BIN     := bin
CORE    := src/core

.PHONY: all clean test

all: $(BIN)/banker

$(BIN)/banker: $(CORE)/main.c $(CORE)/banker.c $(CORE)/banker.h | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(CORE)/main.c $(CORE)/banker.c

$(BIN)/unit_tests: tests/unit_tests.c $(CORE)/banker.c $(CORE)/banker.h | $(BIN)
	$(CC) $(CFLAGS) -o $@ tests/unit_tests.c $(CORE)/banker.c

test: $(BIN)/banker $(BIN)/unit_tests
	./$(BIN)/unit_tests
	./tests/run_cli_tests.sh

$(BIN):
	mkdir -p $(BIN)

clean:
	rm -rf $(BIN)
