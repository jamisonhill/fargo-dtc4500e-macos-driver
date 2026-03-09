# Fargo DTC4500e Native macOS Driver — Next Steps

**Project Goal:** Build a native macOS ARM64 CUPS filter for the HID Fargo DTC4500e ID card printer.
**Status:** CUPS driver installed. Direct USB Python printing (`fargo-print-pdf.py`) has 4 bugs to fix.

---

## Current Focus: Fix fargo-print-pdf.py (Direct USB Printing)

The CUPS driver is installed but the Python direct-USB script (`fargo-print-pdf.py`) is the active workstream. The printer gives "job data error #106" because of 4 bugs found by deep analysis of the known-good K_STD PRN file.

### 4 Bugs to Fix (in priority order)

**BUG 1: Missing panel descriptor in first data strip** (ROOT CAUSE)
- `build_panel_data()` sends pure RLE in all 512-byte strips
- Printer expects first 40 bytes of first strip to be a panel descriptor
- Fix: Build 40-byte descriptor and prepend to first strip

**BUG 2: Wrong line format**
- Current: `\x00\x00` + 768 pixels + `\x00` = 771 bytes/line
- Correct: 768 raw pixels per line (no escape prefix, no trailer suffix)
- Fix: Remove escape/trailer from `build_panel_data()`

**BUG 3: Wrong height**
- Current: `CARD_HEIGHT = 1011`
- Correct: `CARD_HEIGHT = 1009` (matches known-good PRN)

**BUG 4: Last RLE chunk handling**
- Current: Last chunk zero-padded to 512 bytes
- Correct: Send remainder as Fg(actual_length) — a non-512 Fg packet
- The "82-byte panel footer" we hardcoded IS the last RLE data chunk
- Fix: Don't pad; remove `build_fg_panel_footer_k()` entirely

### Analysis Script
`fargo-driver/test/analyze_prn_final.py` — Parses and decodes the known-good PRN; use to verify fixes.

---

## What Was Accomplished

### Phase 1: Protocol Reverse Engineering (COMPLETE)
- Extracted and analyzed vendor drivers (32-bit Mac, Linux ARM64, Windows)
- Deep-analyzed all 15 PRN test files to decode the **Fargo Raster Language (FRL)** protocol
- Captured live USB traffic with USBPcap/Wireshark from Windows driver
- See `fargo-driver/protocol-analysis/PROTOCOL_SPEC.md`

### Phase 2: Core C Driver Development (COMPLETE)
- Implemented protocol packet builders in `fargo_protocol.c`
- Implemented libusb USB backend in `fargo_usb.c`
- Implemented CUPS filter main loop in `rastertofargo.c`
- Fixed all 20 bugs from CODE_REVIEW.md

### Phase 3: Installation & Testing (COMPLETE)
- Compiled ARM64, installed to CUPS
- Black and color ribbon prints tested via CUPS

### Phase 4: Direct USB Python Printing (IN PROGRESS)
- `fargo-print-pdf.py` converts PDF -> grayscale -> FRL -> USB
- Config packet (48 bytes) matches known-good byte-for-byte
- Preamble (Fs start + Fs init) matches known-good
- RLE compression algorithm confirmed correct
- 4 bugs identified, ready to fix

---

## Confirmed Protocol Facts
| Item | Value |
|------|-------|
| USB Vendor ID | 0x09b0 (FARGO Electronics) |
| USB Product ID | 0xbf0c (DTC4500e) |
| Bulk OUT endpoint | 0x01 |
| Card size | 768 x 1009 pixels @ 300 DPI (CR80) |
| Line format | 768 raw pixels (NO escape bytes, NO trailer) |
| RLE scheme | Byte-pair (count-1, value) → repeat value (count) times |
| First data strip | 40-byte panel descriptor + 472 bytes RLE |
| Subsequent strips | 512 bytes pure RLE |
| Last strip | Fg(remainder) — NOT padded to 512 |
| Panel descriptor | type=15, sub_type=22, block_type=16, panel_id, height=1009 |

---

## Key Files
```
fargo-print-pdf.py              ← Direct USB print script (FIX THIS)
fargo-print-wrapper.sh          ← Shell wrapper for the Python script
fargo-driver/
├── src/
│   ├── rastertofargo.c         ← CUPS filter (working)
│   ├── fargo_protocol.c/h      ← C protocol implementation
│   └── fargo_usb.c/h           ← C USB layer
├── test/
│   ├── send_prn.py             ← Send raw PRN via USB (WORKS)
│   ├── analyze_prn_final.py    ← PRN analysis tool
│   └── discover_usb.py         ← USB device discovery
├── reference/
│   └── *.prn                   ← 15 known-good test print files
└── protocol-analysis/
    └── PROTOCOL_SPEC.md        ← Full protocol spec
```

---

## Resume Prompt

See `RESUME_PROMPT.md` for the exact prompt to use when continuing this work.
