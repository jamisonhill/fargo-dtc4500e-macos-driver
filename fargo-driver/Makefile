# =============================================================================
# Makefile for rastertofargo-macos
# Native macOS ARM64 CUPS filter for the HID Fargo DTC4500e ID card printer
#
# Usage:
#   make              - build the filter (same as 'make all')
#   make all          - build the filter
#   make clean        - remove build artifacts
#   make install      - install the filter, PPD, and register the printer
#   make uninstall    - remove the filter, PPD, and unregister the printer
#   make check-deps   - verify all build dependencies are present
#   DEBUG=1 make all  - build with address sanitizer and debug symbols
# =============================================================================

# --- Compiler and architecture -----------------------------------------------

CC      := clang
ARCH    := -arch arm64

# --- Paths -------------------------------------------------------------------

# Where the built binary lives before install
BUILD_DIR := build

# The CUPS filter directory on macOS
CUPS_FILTER_DIR := /usr/libexec/cups/filter

# Where CUPS looks for PPD files
PPD_INSTALL_DIR := /usr/share/cups/model

# Where HID/Fargo puts its data files (ICM color profiles, XML, etc.)
DATA_INSTALL_DIR := /Library/Printers/HIDGlobal/fargo/DTC4500e

# The printer name that will appear in CUPS
PRINTER_NAME := FargoDTC4500e

# --- Source files ------------------------------------------------------------

SRCS := src/rastertofargo.c \
        src/fargo_usb.c \
        src/fargo_protocol.c

# The output binary name
TARGET := $(BUILD_DIR)/rastertofargo-macos

# --- Compiler flags ----------------------------------------------------------

# Ask CUPS for its required include paths and compile flags.
# Try cups-config first; if not found, try the system path.
CUPS_CFLAGS := $(shell cups-config --cflags 2>/dev/null || /usr/bin/cups-config --cflags 2>/dev/null)
CUPS_LIBS := $(shell cups-config --libs 2>/dev/null || /usr/bin/cups-config --libs 2>/dev/null)

# If cups-config gave us nothing, use manual fallbacks for macOS
ifeq ($(strip $(CUPS_LIBS)),)
    CUPS_CFLAGS :=
    CUPS_LIBS := -lcups -lcupsimage
endif

# cups-config --libs often omits -lcupsimage, which we need for cupsRaster*
CUPS_LIBS += -lcupsimage

# libusb: prefer ARM64 Homebrew (/opt/homebrew) over Intel Homebrew (/usr/local).
# This avoids linking the x86_64 libusb when building for arm64.
LIBUSB_CFLAGS := $(shell \
    if [ -d /opt/homebrew/include/libusb-1.0 ]; then \
        echo -I/opt/homebrew/include/libusb-1.0; \
    elif [ -d /usr/local/include/libusb-1.0 ]; then \
        echo -I/usr/local/include/libusb-1.0; \
    else \
        pkg-config --cflags libusb-1.0 2>/dev/null; \
    fi)
LIBUSB_LIBS := $(shell \
    if [ -f /opt/homebrew/lib/libusb-1.0.dylib ]; then \
        echo -L/opt/homebrew/lib -lusb-1.0; \
    elif [ -f /usr/local/lib/libusb-1.0.dylib ]; then \
        echo -L/usr/local/lib -lusb-1.0; \
    else \
        pkg-config --libs libusb-1.0 2>/dev/null; \
    fi)

# Base flags: optimize, warn aggressively, target arm64
CFLAGS := -O2 -Wall -Wextra $(ARCH) $(CUPS_CFLAGS) $(LIBUSB_CFLAGS)
LDFLAGS := $(ARCH) $(CUPS_LIBS) $(LIBUSB_LIBS)

# --- Debug build mode --------------------------------------------------------
# Usage: DEBUG=1 make
ifdef DEBUG
    CFLAGS  := -DDEBUG -DFARGO_USB_DEBUG -g -fsanitize=address -O0 -Wall -Wextra $(ARCH) $(CUPS_CFLAGS) $(LIBUSB_CFLAGS)
    LDFLAGS := -fsanitize=address $(ARCH) $(CUPS_LIBS) $(LIBUSB_LIBS)
    $(info Building in DEBUG mode with AddressSanitizer)
endif

# =============================================================================
# Targets
# =============================================================================

.PHONY: all clean install uninstall check-deps

# Default target
all: $(TARGET)

# --- Build target ------------------------------------------------------------

$(TARGET): $(SRCS) | $(BUILD_DIR)
	@echo "Building $(TARGET)..."
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)
	@echo "Build successful: $@"

# Create the build directory if it doesn't exist
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# --- Clean -------------------------------------------------------------------

clean:
	@echo "Removing build artifacts..."
	rm -rf $(BUILD_DIR)
	@echo "Clean complete."

# --- Dependency check --------------------------------------------------------

check-deps:
	@echo ""
	@echo "=== Checking build dependencies ==="
	@# Check for clang
	@if command -v clang >/dev/null 2>&1; then \
		echo "  [OK] clang: $$(clang --version | head -1)"; \
	else \
		echo "  [MISSING] clang not found."; \
		echo "            Install Xcode Command Line Tools: xcode-select --install"; \
		exit 1; \
	fi
	@# Check for cups-config
	@if command -v cups-config >/dev/null 2>&1 || command -v /usr/bin/cups-config >/dev/null 2>&1; then \
		echo "  [OK] cups-config: version $$(cups-config --version 2>/dev/null || /usr/bin/cups-config --version 2>/dev/null)"; \
	else \
		echo "  [MISSING] cups-config not found."; \
		echo "            CUPS headers should be built-in on macOS."; \
		echo "            Try: xcode-select --install"; \
		exit 1; \
	fi
	@# Check for libusb via pkg-config OR the Homebrew fallback path
	@if pkg-config --exists libusb-1.0 2>/dev/null; then \
		echo "  [OK] libusb-1.0: version $$(pkg-config --modversion libusb-1.0)"; \
	elif [ -f /opt/homebrew/lib/libusb-1.0.dylib ]; then \
		echo "  [OK] libusb-1.0: found at /opt/homebrew/lib (using fallback path)"; \
	elif [ -f /usr/local/lib/libusb-1.0.dylib ]; then \
		echo "  [OK] libusb-1.0: found at /usr/local/lib (Intel Homebrew)"; \
	else \
		echo "  [MISSING] libusb-1.0 not found."; \
		echo "            Install it with: brew install libusb"; \
		exit 1; \
	fi
	@# Check for CUPS headers
	@if [ -f /usr/include/cups/cups.h ] || [ -f "$$(xcrun --show-sdk-path 2>/dev/null)/usr/include/cups/cups.h" ]; then \
		echo "  [OK] CUPS headers found"; \
	else \
		echo "  [WARN] CUPS headers not found at expected path."; \
		echo "         cups-config should handle this, but if build fails try:"; \
		echo "         xcode-select --install"; \
	fi
	@echo "=== All dependencies satisfied ==="
	@echo ""

# --- Install -----------------------------------------------------------------
# Must be run as root (sudo make install).

install: $(TARGET)
	@echo ""
	@echo "=== Installing Fargo DTC4500e CUPS filter ==="
	@if [ "$$(id -u)" != "0" ]; then \
		echo "ERROR: install must be run as root. Use: sudo make install"; \
		exit 1; \
	fi

	@echo "Installing filter binary to $(CUPS_FILTER_DIR)..."
	install -m 755 $(TARGET) $(CUPS_FILTER_DIR)/rastertofargo-macos

	@echo "Installing PPD file to $(PPD_INSTALL_DIR)..."
	install -m 644 ppd/DTC4500e-macos.ppd $(PPD_INSTALL_DIR)/

	@echo "Creating data directory $(DATA_INSTALL_DIR)..."
	mkdir -p $(DATA_INSTALL_DIR)

	@echo "Installing data files (XML, color profiles)..."
	@if [ -f reference/DTC4500e.xml ]; then \
		install -m 644 reference/DTC4500e.xml $(DATA_INSTALL_DIR)/; \
		echo "  Installed DTC4500e.xml"; \
	else \
		echo "  WARNING: reference/DTC4500e.xml not found, skipping"; \
	fi
	@for icm in reference/*.icm; do \
		if [ -f "$$icm" ]; then \
			install -m 644 "$$icm" $(DATA_INSTALL_DIR)/; \
			echo "  Installed $$(basename $$icm)"; \
		fi; \
	done

	@echo "Restarting CUPS..."
	launchctl kickstart -kp system/org.cups.cupsd

	@echo "Waiting 3 seconds for CUPS to restart..."
	sleep 3

	@echo "Registering printer with CUPS..."
	@# NOTE: You may need to update the -v URI below to match your printer.
	@# Run: lpinfo -v | grep -i fargo   to find the exact URI.
	lpadmin \
		-p "$(PRINTER_NAME)" \
		-E \
		-v "usb://HID%20Global/DTC4500e" \
		-m "DTC4500e-macos.ppd" \
		-D "Fargo DTC4500e Card Printer" \
		-L "ID Card Printer"
	cupsenable $(PRINTER_NAME) || true
	cupsaccept $(PRINTER_NAME) || true

	@echo ""
	@echo "=== Installation complete! ==="
	@echo ""
	@echo "Next steps:"
	@echo "  1. Connect the Fargo DTC4500e via USB"
	@echo "  2. Run: lpinfo -v | grep -i fargo"
	@echo "     to find the exact USB URI for your printer"
	@echo "  3. Update the printer URI: lpadmin -p $(PRINTER_NAME) -v <uri>"
	@echo "  4. Verify: lpstat -p $(PRINTER_NAME)"
	@echo "  5. Test: lp -d $(PRINTER_NAME) reference/DTC4500e_YMCKO_Tst.prn"
	@echo ""

# --- Uninstall ---------------------------------------------------------------

uninstall:
	@echo ""
	@echo "=== Uninstalling Fargo DTC4500e CUPS filter ==="
	@if [ "$$(id -u)" != "0" ]; then \
		echo "ERROR: uninstall must be run as root. Use: sudo make uninstall"; \
		exit 1; \
	fi

	@echo "Removing printer from CUPS..."
	lpadmin -x "$(PRINTER_NAME)" 2>/dev/null || true

	@echo "Removing filter binary..."
	rm -f $(CUPS_FILTER_DIR)/rastertofargo-macos

	@echo "Removing PPD file..."
	rm -f $(PPD_INSTALL_DIR)/DTC4500e-macos.ppd

	@echo "NOTE: Data files in $(DATA_INSTALL_DIR) were NOT removed."
	@echo "      To remove manually: sudo rm -rf $(DATA_INSTALL_DIR)"

	@echo "Restarting CUPS..."
	launchctl kickstart -kp system/org.cups.cupsd

	@echo ""
	@echo "=== Uninstall complete ==="
	@echo ""
