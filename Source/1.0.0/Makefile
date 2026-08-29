.PHONY: all build install clean help

CC := gcc
CFLAGS := -Wall -O2
PYTHON := python3
PIP := pip3

BIN_DIR := /usr/bin
MIME_DIR := /usr/share/mime/applications
INSTALL_PREFIX ?= /

all: build

build: edi_runner
	@echo "Build complete"

edi_runner: runner.c
	$(CC) $(CFLAGS) -o edi_runner runner.c
	@echo "Compiled edi_runner"

install: build
	@if [ "$$(id -u)" != "0" ]; then \
		echo "Error: Please run 'sudo make install'"; \
		exit 1; \
	fi
	@echo "Installing PyQt6..."
	$(PIP) install PyQt6
	@echo "Installing binaries..."
	install -Dm755 edi_runner "$(INSTALL_PREFIX)$(BIN_DIR)/edi_runner"
	install -Dm755 edi_compiler "$(INSTALL_PREFIX)$(BIN_DIR)/edi_compiler"
	install -Dm644 edi.xml "$(INSTALL_PREFIX)$(MIME_DIR)/edi.xml"
	@echo "Done! edi_runner and edi_compiler are installed."

clean:
	rm -f edi_runner
	@echo "Cleaned"

help:
	@echo "EDI Build"
	@echo ""
	@echo "Usage: make [target]"
	@echo ""
	@echo "Targets:"
	@echo "  build      - Compile edi_runner (default)"
	@echo "  install    - Build and install (requires sudo)"
	@echo "  clean      - Remove compiled binaries"
	@echo "  help       - Show this message"
	@echo ""
	@echo "Example:"
	@echo "  make build"
	@echo "  sudo make install"
