#!/bin/bash
# Simple PDF → PRN → Printer converter
# Uses existing tools: Ghostscript + send_prn.py

PDF_FILE="$1"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SEND_PRN="$SCRIPT_DIR/fargo-driver/test/send_prn.py"

# Create temp PRN file
PRN_FILE=$(mktemp /tmp/fargo-print-XXXXXX.prn)

echo "Converting PDF to PRN format..."

# Use Ghostscript to convert PDF to the Fargo PRN format
# This creates a simple monochrome print job
gs -q -dNOPAUSE -dBATCH -dSAFER \
   -sDEVICE=ps2write \
   -sOutputFile="$PRN_FILE" \
   "$PDF_FILE" 2>/dev/null

if [ ! -f "$PRN_FILE" ] || [ ! -s "$PRN_FILE" ]; then
    echo "ERROR: PDF conversion failed"
    rm -f "$PRN_FILE"
    exit 1
fi

echo "Sending to printer..."

# Use the existing send_prn.py script (which we know works)
python3 "$SEND_PRN" "$PRN_FILE"
EXIT_CODE=$?

# Cleanup
rm -f "$PRN_FILE"

exit $EXIT_CODE
