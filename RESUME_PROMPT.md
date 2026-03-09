# Resume Prompt — Fargo DTC4500e Direct USB Printing

Copy and paste this into Claude Code to resume:

---

I'm working on `fargo-print-pdf.py` which converts PDFs to Fargo Raster Language (FRL) and sends them to a Fargo DTC4500e ID card printer via direct USB. The printer currently rejects our generated data with "job data error #106".

Deep analysis of the known-good `fargo-driver/reference/DTC4500e_K_STD_Tst.prn` file found **4 bugs** in our script. Read `nextsteps.md` for the full context, then fix all 4 bugs in `fargo-print-pdf.py`:

**BUG 1: Missing panel descriptor.** The first 512-byte data strip must start with a 40-byte panel descriptor (type=15, sub_type=22, block_type=16, panel_id, height=1009), followed by 472 bytes of RLE data. Currently we send pure RLE in all strips.

**BUG 2: Wrong line format.** Each line should be 768 raw pixel bytes. Remove the `\x00\x00` prefix and `\x00` suffix — no escape bytes, no trailer.

**BUG 3: Wrong height.** Change `CARD_HEIGHT` from 1011 to 1009.

**BUG 4: Last chunk handling.** The final RLE chunk should be sent as `Fg(remainder_length)`, NOT zero-padded to 512. The "82-byte panel footer" (`build_fg_panel_footer_k()`) is actually just the last RLE data chunk from the test pattern — remove it entirely.

After fixing, generate a test PRN with `--dry-run --save-prn test_output.prn` using any PDF, then use the analysis script at `fargo-driver/test/analyze_prn_final.py` to verify our output matches the known-good structure. Then I'll send it to the printer.
