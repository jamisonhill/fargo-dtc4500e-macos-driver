# Resume Prompt — Fargo DTC4500e Direct USB Printing

Copy and paste this into Claude Code to resume:

---

I'm working on `fargo-print-pdf.py` which converts PDFs to Fargo Raster Language (FRL) and sends them to a Fargo DTC4500e ID card printer via direct USB. Read `nextsteps.md` and the memory file at `~/.claude/projects/-Users-jamisonhill-Ai-fargo-dtc4500e-macos-driver/memory/debugging-session.md` for full context.

**STATUS:** The 4 structural bugs are FIXED. Error 106 persists. The K_STD ribbon is damaged and must be replaced before testing resumes.

**KEY FINDING:** Replacing ALL RLE content with 0xFF in an otherwise byte-for-byte identical known-good PRN (same 266,432 bytes, same 512 strips) causes error 106. Changing 1 byte in the padding region works fine. This means the printer validates actual image data content — not just packet structure.

**BLOCKER:** Ribbon is too damaged for reliable prints. Install a fresh K_STD ribbon first.

**RESUME CHECKLIST (when new ribbon is installed):**
1. Send unmodified known-good PRN via `send_prn.py` to confirm ribbon+printer work
2. Modify ONE byte in the ACTIVE RLE region (offset ~128, strip 1) of known-good via pyusb
   - If WORKS: printer doesn't checksum RLE data → problem is elsewhere in our generation
   - If FAILS: printer checksums/validates RLE → need to find the checksum algorithm
3. Try the C driver through CUPS: `lp -d HID_Global_DTC4500e_2 <pdf>` to see if C driver works
4. If C driver works, capture its CUPS output and compare byte-for-byte with our Python output
5. Consider Wireshark USB capture of a successful print vs our failed print

**CUPS DISCOVERY:** Two CUPS printers exist:
- `HID_Global_DTC4500e` → backend is `///dev/null` (BROKEN — sends nothing)
- `HID_Global_DTC4500e_2` → backend is `usb://HID%20Global/DTC4500e?serial=C1331076` (REAL)
- Raw mode works: `lp -d HID_Global_DTC4500e_2 -o raw <file.prn>`
