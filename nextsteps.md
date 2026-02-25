# Fargo DTC4500e Native macOS Driver — Next Steps

**Project Goal:** Build a native macOS ARM64 CUPS filter for the HID Fargo DTC4500e ID card printer.
**Status:** ✅ COMPLETE AND TESTED — Driver installed and printing successfully.

---

## What Was Accomplished

### Phase 1: Protocol Reverse Engineering (COMPLETE)
- Extracted and analyzed the 2014 Mac pkg installer (32-bit i386 — incompatible with macOS 26)
- Extracted and analyzed the Linux ARM64 driver (ELF binary — cannot run on macOS)
- Deep-analyzed all 15 PRN test files to decode the **Fargo Raster Language (FRL)** protocol
- Captured live USB traffic with USBPcap/Wireshark from Windows driver
- **Confirmed all critical protocol values** — see `fargo-driver/protocol-analysis/PROTOCOL_SPEC.md`

### Phase 2: Core C Driver Development (COMPLETE)
- Implemented all protocol packet builders in `fargo_protocol.c`
- Implemented libusb USB backend in `fargo_usb.c`
- Implemented CUPS filter main loop in `rastertofargo.c`
- Fixed all 20 bugs identified in CODE_REVIEW.md including BUG-18 and BUG-14

### Phase 3: Installation & Testing (COMPLETE)
- ✅ Compiled successfully with zero errors (ARM64, clang, AddressSanitizer clean)
- ✅ Fixed BUG-18: Multi-panel ribbon detection now uses panel count check
- ✅ Fixed BUG-14: Dual-sided config packets now properly sized (50 bytes)
- ✅ Installed to `/usr/libexec/cups/filter/rastertofargo-macos`
- ✅ Installed PPD to `/Library/Printers/PPDs/Contents/Resources/`
- ✅ Tested black ribbon print ✓
- ✅ Tested color ribbon print ✓
- ✅ Committed fixes to git

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

## Remaining Work (Optional Enhancements)

### High Priority: Further Testing
✅ **DONE:** Black ribbon test
✅ **DONE:** Color ribbon test

**TODO:** Test all ribbon types with their respective test files:
```bash
lp -d FargoDTC4500e ~/Ai/fargo-dtc4500e-macos-driver/fargo-driver/reference/DTC4500e_BO_Tst.prn
lp -d FargoDTC4500e ~/Ai/fargo-dtc4500e-macos-driver/fargo-driver/reference/DTC4500e_KO_Tst.prn
lp -d FargoDTC4500e ~/Ai/fargo-dtc4500e-macos-driver/fargo-driver/reference/DTC4500e_NONE_Tst.prn
lp -d FargoDTC4500e ~/Ai/fargo-dtc4500e-macos-driver/fargo-driver/reference/DTC4500e_YMCFKO_Tst.prn
lp -d FargoDTC4500e ~/Ai/fargo-dtc4500e-macos-driver/fargo-driver/reference/DTC4500e_YMCKOK_Tst.prn
lp -d FargoDTC4500e ~/Ai/fargo-dtc4500e-macos-driver/fargo-driver/reference/DTC4500e_YMCKK_Tst.prn
```

### Medium Priority: Real-World Testing with Inkscape
1. Install Inkscape from https://inkscape.org/ (native macOS ARM64 available)
2. Create a badge design at exactly 2.125" × 3.375" (CR80 card size)
3. Print to FargoDTC4500e CUPS printer
4. Verify colors match expected output
5. Adjust color profile settings in PPD if needed (`/Library/Printers/PPDs/Contents/Resources/DTC4500e-macos.ppd`)

### Low Priority: Code Cleanup
- T-05: Implement inter-panel boundary packets (currently deferred — format unknown)
- Review and update any TODOs in fargo_protocol.c for dual-sided flag layout verification
- Add comprehensive logging/debugging mode for troubleshooting

### How to Uninstall
If needed, remove the driver:
```bash
sudo rm /usr/libexec/cups/filter/rastertofargo-macos
sudo rm /Library/Printers/PPDs/Contents/Resources/DTC4500e-macos.ppd
sudo launchctl kickstart -kp system/org.cups.cupsd
lpadmin -x FargoDTC4500e
```

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

## Current Installation & Status
**Machine:** macOS ARM64
**Printer:** HID Global Fargo DTC4500e (VID=0x09b0 PID=0xbf0c)
**Filter Binary:** `/usr/libexec/cups/filter/rastertofargo-macos` (53KB)
**PPD File:** `/Library/Printers/PPDs/Contents/Resources/DTC4500e-macos.ppd`
**Last Tested:** Feb 25, 2026 — Both K and YMCKO prints successful

## Resume This Work Next Session

The driver is **fully operational**. To resume:

1. **Verify printer is still installed:**
   ```bash
   lpstat -p FargoDTC4500e
   ```

2. **Print a test file:**
   ```bash
   lp -d FargoDTC4500e ~/Ai/fargo-dtc4500e-macos-driver/fargo-driver/reference/DTC4500e_K_STD_Tst.prn
   ```

3. **For further testing** — See "Remaining Work" section above for ribbon type tests and real-world Inkscape testing

4. **To rebuild after code changes:**
   ```bash
   cd ~/Ai/fargo-dtc4500e-macos-driver/fargo-driver
   make clean && make
   sudo make install
   ```
