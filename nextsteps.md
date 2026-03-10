# Fargo DTC4500e Native macOS Driver — Next Steps

**Project Goal:** Build a native macOS ARM64 CUPS filter for the HID Fargo DTC4500e ID card printer.
**Status:** All 4 structural bugs fixed in `fargo-print-pdf.py`. Error 106 persists — printer appears to validate image data content. Ribbon damaged; need replacement before testing.

---

## BLOCKER: Ribbon Damaged

The K_STD ribbon is too damaged from repeated test prints and ribbon jams/breaks. Even the known-good PRN now produces bad prints (negative image, ribbon sticking to card). **Install a fresh ribbon before resuming.**

---

## Current Focus: Diagnose Error 106 Content Validation

The 4 structural bugs are fixed. The packet structure of `fargo-print-pdf.py` output now matches the known-good PRN. But error 106 still occurs because the printer validates actual image data content.

### Critical Evidence

| Test | Result |
|------|--------|
| Unmodified known-good PRN (266,432 bytes) via pyusb | PRINTS OK |
| Known-good with 1 byte changed in padding (offset 130000) | PRINTS OK |
| Known-good with ALL RLE replaced by 0xFF (same exact size) | ERROR 106 |
| Our all-black PRN (correct structure, different image) | ERROR 106 |
| Truncated known-good (12 strips of original data) | "Waiting for data..." (accepted) |
| Known-good via CUPS raw mode | PRINTS OK (ribbon damaged) |
| C-driver format PRN via CUPS | ERROR 106 |

### What This Means

The printer doesn't just check packet structure — it validates the RLE image data itself. Possible explanations:
1. **Hidden checksum**: The printer computes a checksum over the RLE stream and compares to something in the config/descriptor
2. **RLE decode validation**: Invalid RLE sequences (like runs that exceed line/image boundaries) trigger error 106
3. **Content reasonableness check**: The firmware rejects data that decodes to implausible patterns

### Resume Checklist (when new ribbon is installed)

1. **Confirm baseline**: Send unmodified known-good via `send_prn.py`
2. **1-byte active region test**: Modify ONE byte in active RLE (offset ~128, strip 1) of known-good
   - If WORKS → printer doesn't checksum individual bytes → problem is in our RLE generation
   - If FAILS → printer checksums RLE data → need to reverse-engineer the checksum
3. **Test C driver via CUPS**: `lp -d HID_Global_DTC4500e_2 <pdf>` — never verified if C driver actually works
4. **Capture & compare**: If C driver works, compare its output byte-for-byte with our Python output
5. **USB capture**: Use Wireshark to capture successful vs failed USB traffic

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

### Phase 4: Direct USB Python Printing (IN PROGRESS — BLOCKED)
- `fargo-print-pdf.py` converts PDF -> grayscale -> FRL -> USB
- All 4 structural bugs fixed:
  - BUG 1: Added 40-byte panel descriptor to first data strip
  - BUG 2: Changed to 768 raw pixels per line (removed escape/trailer)
  - BUG 3: Fixed CARD_HEIGHT from 1011 to 1009
  - BUG 4: Kept 82-byte footer and 512-byte zero-padding (BUG 4 fix was reverted — testing showed footer IS required and strips MUST be padded)
- Error 106 persists due to content validation (see above)
- Ribbon damaged — cannot test further until replaced

---

## CUPS Backend Discovery

Two CUPS printers are configured:
- `HID_Global_DTC4500e` → backend is `///dev/null` (**BROKEN** — sends nothing to printer)
- `HID_Global_DTC4500e_2` → backend is `usb://HID%20Global/DTC4500e?serial=C1331076` (**REAL**)
- Raw mode confirmed working: `lp -d HID_Global_DTC4500e_2 -o raw <file.prn>`

---

## C Driver vs Known-Good Structure

The C driver (`rastertofargo.c`) builds a SIMPLER job than the known-good Windows driver PRN:
```
C driver:     Fs(0) + Fs(8) + Fg(48) + Fg(512)×N + Fg(14-EOJ)
Known-good:   Fs(0) + Fs(8) + Fg(48) + Fg(512)×512 + Fg(82-footer) + Fg(14-EOJ)
                                        ^descriptor     ^panel footer
```
The C driver has NO panel descriptor and NO 82-byte footer. It was never verified to actually work.

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
| Subsequent strips | 512 bytes pure RLE (zero-padded) |
| Last strip | 82-byte Fg packet (final RLE data, NOT padded to 512) |
| Panel descriptor | type=15, sub_type=22, block_type=16, panel_id, height=1009 |

---

## Key Files
```
fargo-print-pdf.py              ← Direct USB print script (STRUCTURAL BUGS FIXED)
fargo-print-wrapper.sh          ← Shell wrapper for the Python script
fargo-driver/
├── src/
│   ├── rastertofargo.c         ← CUPS filter (untested with current ribbon)
│   ├── fargo_protocol.c/h      ← C protocol implementation
│   └── fargo_usb.c/h           ← C USB layer
├── test/
│   ├── send_prn.py             ← Send raw PRN via USB (CONFIRMED WORKING)
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
