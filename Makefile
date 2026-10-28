CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -pedantic -O2
BIN     := bin
CORE    := src/core

.PHONY: all clean

all: $(BIN)/banker

$(BIN)/banker: $(CORE)/main.c $(CORE)/banker.c $(CORE)/banker.h | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(CORE)/main.c $(CORE)/banker.c

$(BIN):
	mkdir -p $(BIN)

clean:
	rm -rf $(BIN)
