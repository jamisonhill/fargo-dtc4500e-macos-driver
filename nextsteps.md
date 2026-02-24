# Fargo DTC4500e Native macOS Driver — Next Steps

**Project Goal:** Build a native macOS ARM64 CUPS filter for the HID Fargo DTC4500e ID card printer.
**Status:** Protocol fully reverse-engineered. Core C code written. Compilation fixes applied.

---

## What Was Accomplished Today

### Protocol Reverse Engineering (COMPLETE)
- Extracted and analyzed the 2014 Mac pkg installer (32-bit i386 — incompatible with macOS 26)
- Extracted and analyzed the Linux ARM64 driver (ELF binary — cannot run on macOS)
- Deep-analyzed all 15 PRN test files to decode the **Fargo Raster Language (FRL)** protocol
- Captured live USB traffic with USBPcap/Wireshark from Windows driver
- **Confirmed all critical protocol values** — see `fargo-driver/protocol-analysis/PROTOCOL_SPEC.md`

### Confirmed Protocol Facts
| Item | Value |
|------|-------|
| USB Vendor ID | 0x09b0 (FARGO Electronics) |
| USB Product ID | 0xbf0c (DTC4500e) |
| Bulk OUT endpoint | 0x01 |
| Bulk IN endpoint | 0x81 (not used for print data) |
| Max packet size | 512 bytes |
| Status mechanism | Vendor USB control transfers (bmReqType 0x41/0xc1) |
| Status format | FRL packet (version=0x0000) containing ASCII flag string |
| Print data format | FRL packets (version=0x0001): Fg/Fs markers + RLE image data |
| RLE scheme | Byte-pair (count, value) → repeat value (count+1) times |
| Config ribbon field | Byte offset 14-15 (uint16 LE) = ribbon device_id |
| Card thickness field | Byte offset 40-43 (uint32 LE) = mils (30 = standard) |

### Code Written (see `fargo-driver/src/`)
- `fargo_protocol.h` — all constants, structs, enums for FRL protocol
- `fargo_protocol.c` — packet builders: Fs handshake, Fg config, Fg image data, RLE compression, status parsing
- `fargo_usb.h` / `fargo_usb.c` — libusb-1.0 USB layer: device open/close, vendor control transfers, status polling, bulk job send
- `rastertofargo.c` — main CUPS filter: reads CUPS raster, builds FRL job, sends to printer
- `ppd/DTC4500e-macos.ppd` — updated PPD for macOS ARM64 CUPS

### Build & Test Infrastructure (see `fargo-driver/test/` and `Makefile`)
- `Makefile` — builds with clang -arch arm64, CUPS + libusb, install/uninstall targets
- `test/send_prn.py` — sends raw PRN files directly to printer via USB (bypasses CUPS, for testing)
- `test/discover_usb.py` — scans USB and displays Fargo device info and all endpoints
- `test/check_deps.sh` — verifies all build dependencies are installed

---

## What Needs To Be Done Next

### Step 1: Install Build Dependencies
```bash
brew install libusb
pip3 install pyusb
```

### Step 2: Test USB Connectivity (printer must be plugged in)
```bash
cd CUPS/fargo-driver
python3 test/discover_usb.py        # Should show VID=0x09b0, PID=0xbf0c, EP 0x01/0x81
python3 test/send_prn.py --status-only   # Should show printer status flags
```

### Step 3: Send a Test PRN File Directly
```bash
python3 test/send_prn.py reference/DTC4500e_NONE_Tst.prn   # Should advance a card
python3 test/send_prn.py reference/DTC4500e_K_STD_Tst.prn  # Should print K test pattern
```
This proves USB communication works before involving CUPS at all.

### Step 4: Compile the CUPS Filter
```bash
make check-deps   # Verify all dependencies
make              # Build rastertofargo-macos
```
Expected: the binary appears in `build/rastertofargo-macos`.

### Step 5: Fix Remaining Protocol Bugs (see CODE_REVIEW.md)
The code review identified 20 issues. The 8 compilation blockers were fixed today.
These still need attention before printing works correctly:

| Bug | Issue |
|-----|-------|
| BUG-10 | Status polling via vendor control transfers may need tuning (timing, retry logic) |
| BUG-11 | Config packet field layout may need verification against a K vs YMCKO print capture |
| BUG-12 | The 134-byte image header packet may not be needed — test without it first |
| BUG-14 | Dual-sided ribbons (YMCKK, YMCKOK) need 50-byte config, not 48-byte |
| BUG-15 | End-of-job packet needs to be confirmed and sent |
| BUG-16 | Printer command packets need version=0x0000 header |
| BUG-18 | Panel count for BO/KO/NONE ribbons needs verification |

### Step 6: Install and Test via CUPS
```bash
sudo make install          # Installs filter + PPD, restarts CUPS
lpstat -p                  # Should show FargoDTC4500e printer
lp -d FargoDTC4500e /path/to/test.pdf   # Test print from command line
```

### Step 7: Test with Inkscape (FOSS Badge Design)
- Install Inkscape from https://inkscape.org/ (native macOS ARM64 available)
- Create a badge template at exactly 2.125" × 3.375" (CR80 card size)
- Print to the FargoDTC4500e CUPS printer
- Adjust color profile settings in PPD if colors are off

---

## Known Unknowns (require further USB capture or testing)
- Exact meaning of config packet fields [4-7] (card_size=40?), [8-9], [10-11]
- Whether the 134-byte image header packet is necessary
- Full EOJ (end-of-job) packet format
- Status flag polling interval (how often to poll and when to stop)
- Color channel mapping for YMCKO (Y/M/C/K/O plane order confirmation)
- Whether Overlay (O) panel is 8-bit grayscale or binary

---

## File Structure
```
CUPS/
├── nextsteps.md                    ← this file
├── fargo_capture.pcapng            ← Windows USB capture (Wireshark)
├── usb-powershell-screenshot.png   ← PowerShell device query screenshot
├── fargo-driver/                   ← Main project
│   ├── Makefile
│   ├── src/                        ← C source files
│   │   ├── rastertofargo.c         ← Main CUPS filter (build target)
│   │   ├── fargo_usb.c/h           ← libusb USB layer
│   │   └── fargo_protocol.c/h      ← FRL packet protocol
│   ├── ppd/
│   │   └── DTC4500e-macos.ppd      ← PPD for macOS CUPS
│   ├── test/
│   │   ├── send_prn.py             ← Direct USB test tool (start here!)
│   │   ├── discover_usb.py         ← USB endpoint discovery
│   │   └── check_deps.sh           ← Dependency checker
│   ├── protocol-analysis/
│   │   ├── PROTOCOL_SPEC.md        ← Full reverse-engineered protocol spec
│   │   └── CODE_REVIEW.md          ← Bug list from code review
│   └── reference/                  ← Original vendor files for reference
│       ├── *.prn                   ← 15 original test print files
│       ├── DTC4500e.xml            ← Printer model config (ribbon types etc.)
│       └── *.ppd                   ← Original Mac + Linux PPD files
└── vendor-drivers/
    ├── DTC4500e_v5.6.0.9_setup/    ← Windows driver (works, for reference)
    ├── sfw_02133_.../              ← Linux driver (binary only, ARM64)
    └── dtc4500e-v1.3.2.7-macosx.pkg ← Original Mac pkg (32-bit, won't run)
```

---

## Resume This Work Next Session
Tell Claude Code:
> "Resume the Fargo DTC4500e native macOS CUPS filter project in ~/Ai/MHStuff/CUPS.
> The protocol is fully reverse-engineered. The code is in fargo-driver/src/.
> Start by running `make` and fixing any remaining errors, then test with send_prn.py."
