CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -pedantic -O2
BIN     := bin
CORE    := src/core

.PHONY: all clean test ui docs

all: $(BIN)/banker

$(BIN)/banker: $(CORE)/main.c $(CORE)/banker.c $(CORE)/json.c $(CORE)/banker.h \
               $(CORE)/json.h | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(CORE)/main.c $(CORE)/banker.c $(CORE)/json.c

$(BIN)/unit_tests: tests/unit_tests.c $(CORE)/banker.c $(CORE)/banker.h | $(BIN)
	$(CC) $(CFLAGS) -o $@ tests/unit_tests.c $(CORE)/banker.c

test: $(BIN)/banker $(BIN)/unit_tests
	./$(BIN)/unit_tests
	./tests/run_cli_tests.sh
	python3 -m unittest -v tests/test_ui_server.py

ui: $(BIN)/banker
	python3 src/ui/server.py --open

DOCS := docs/V-1-13_Manual.pdf docs/V-1-13_BD1_Report.pdf

docs: $(DOCS)

docs/V-1-13_Manual.pdf: docs/MANUAL.md docs/tools/md2pdf.py
	python3 docs/tools/md2pdf.py $< $@

docs/V-1-13_BD1_Report.pdf: docs/REPORT.md docs/tools/md2pdf.py
	python3 docs/tools/md2pdf.py $< $@

$(BIN):
	mkdir -p $(BIN)

clean:
	rm -rf $(BIN)
