#!/bin/bash
#
# fargo-backend.sh - Simple CUPS backend for Fargo DTC4500e
# This backend reads FRL data from stdin and sends it directly to the printer via libusb
#

DEVICE_CLASS="0x09b0:0xbf0c"
DEVICE_URI="$6"

# Extract serial if provided in URI
SERIAL=""
if [[ "$DEVICE_URI" =~ serial=([A-Z0-9]+) ]]; then
    SERIAL="${BASH_REMATCH[1]}"
fi

# Read all data from stdin
DATA=$(cat)

if [ -z "$DATA" ]; then
    echo "ERROR: No data received from filter" >&2
    exit 1
fi

# For now, just succeed - the filter has already sent to USB
# This is a placeholder backend
exit 0
