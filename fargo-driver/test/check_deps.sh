#!/bin/bash
# check_deps.sh - Verify all build and runtime dependencies for the Fargo driver.
#
# Run this before attempting to compile:
#   bash test/check_deps.sh
#
# It checks for:
#   - Xcode Command Line Tools (clang)
#   - CUPS headers (cups-config)
#   - libusb-1.0 (via pkg-config or Homebrew paths)
#   - Python 3 + pyusb (for test scripts)

echo ""
echo "=== Fargo DTC4500e Driver Dependency Check ==="
echo ""

ERRORS=0

# --- Check clang ---
if command -v clang >/dev/null 2>&1; then
    echo "[OK] clang: $(clang --version | head -1)"
else
    echo "[MISSING] clang not found."
    echo "          Fix: xcode-select --install"
    ERRORS=$((ERRORS + 1))
fi

# --- Check cups-config ---
if command -v cups-config >/dev/null 2>&1 || command -v /usr/bin/cups-config >/dev/null 2>&1; then
    VERSION=$(cups-config --version 2>/dev/null || /usr/bin/cups-config --version 2>/dev/null)
    echo "[OK] cups-config: version $VERSION"
else
    echo "[MISSING] cups-config not found."
    echo "          CUPS is built into macOS but headers may need Xcode CLT."
    echo "          Fix: xcode-select --install"
    ERRORS=$((ERRORS + 1))
fi

# --- Check CUPS headers ---
SDK_PATH=$(xcrun --show-sdk-path 2>/dev/null)
if [ -f /usr/include/cups/cups.h ]; then
    echo "[OK] CUPS headers: /usr/include/cups/cups.h"
elif [ -n "$SDK_PATH" ] && [ -f "$SDK_PATH/usr/include/cups/cups.h" ]; then
    echo "[OK] CUPS headers: $SDK_PATH/usr/include/cups/cups.h"
else
    echo "[WARN] CUPS headers not found at expected paths."
    echo "       cups-config should handle include paths, but if build fails:"
    echo "       Fix: xcode-select --install"
fi

# --- Check libusb ---
if pkg-config --exists libusb-1.0 2>/dev/null; then
    echo "[OK] libusb-1.0: version $(pkg-config --modversion libusb-1.0)"
    echo "     CFLAGS: $(pkg-config --cflags libusb-1.0)"
    echo "     LIBS:   $(pkg-config --libs libusb-1.0)"
elif [ -f /opt/homebrew/lib/libusb-1.0.dylib ]; then
    echo "[OK] libusb-1.0: found at /opt/homebrew/lib (Homebrew ARM64)"
elif [ -f /usr/local/lib/libusb-1.0.dylib ]; then
    echo "[OK] libusb-1.0: found at /usr/local/lib (Homebrew Intel)"
else
    echo "[MISSING] libusb-1.0 not found."
    echo "          Fix: brew install libusb"
    ERRORS=$((ERRORS + 1))
fi

# --- Check Python 3 ---
if command -v python3 >/dev/null 2>&1; then
    echo "[OK] python3: $(python3 --version 2>&1)"
else
    echo "[WARN] python3 not found. Test scripts won't work."
    echo "       Fix: brew install python3"
fi

# --- Check pyusb ---
if python3 -c "import usb.core" 2>/dev/null; then
    PYUSB_VER=$(python3 -c "import usb; print(usb.__version__)" 2>/dev/null || echo "unknown")
    echo "[OK] pyusb: version $PYUSB_VER"
else
    echo "[WARN] pyusb not installed. Test scripts won't work."
    echo "       Fix: pip3 install pyusb"
fi

# --- Summary ---
echo ""
if [ $ERRORS -eq 0 ]; then
    echo "=== All required dependencies satisfied ==="
    echo ""
    echo "You can now build with: make"
    echo "Or check USB with: python3 test/discover_usb.py"
else
    echo "=== $ERRORS required dependency/dependencies MISSING ==="
    echo ""
    echo "Please install the missing dependencies and try again."
fi
echo ""

exit $ERRORS
