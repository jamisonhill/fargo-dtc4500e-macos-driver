#!/bin/bash
# Fargo PDF Print — Converts PDF to FRL and sends to printer via USB
# Usage: fargo-print-wrapper.sh <file.pdf> [ribbon]
#   ribbon: K_STD (default), YMCKO, KO, K_PRM, YMCKOK

PDF_FILE="$1"
RIBBON="${2:-K_STD}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRINT_SCRIPT="$SCRIPT_DIR/fargo-print-pdf.py"

if [ -z "$PDF_FILE" ]; then
    echo "Usage: $(basename "$0") <file.pdf> [ribbon]"
    echo "  Ribbons: K_STD (default), YMCKO, KO"
    exit 1
fi

if [ ! -f "$PDF_FILE" ]; then
    echo "ERROR: File not found: $PDF_FILE"
    exit 1
fi

python3 "$PRINT_SCRIPT" --ribbon "$RIBBON" "$PDF_FILE"
