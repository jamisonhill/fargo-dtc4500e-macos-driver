# Code Review: rastertofargo-macos

**Reviewer:** Senior C / USB / CUPS specialist
**Date:** 2026-02-24
**Files reviewed:**
- `src/fargo_protocol.h`
- `src/fargo_protocol.c`
- `src/fargo_usb.h`
- `src/fargo_usb.c`
- `src/rastertofargo.c`
- `Makefile`
- `protocol-analysis/PROTOCOL_SPEC.md`
- `reference/DTC4500e_mac_v1.3.2.7.ppd`

**Verdict: Will NOT compile as written.** There are approximately 25 undefined symbols that will cause linker or compiler errors before a single byte is sent to a printer. These are itemised below with exact locations and required fixes.

---

## 1. Compilation Blockers (will prevent `make` from succeeding)

### BUG-01 — `fargo_protocol.h` exports `FARGO_RIBBON_*` names; `rastertofargo.c` uses `RIBBON_*` names (type alias mismatch)

**File:** `src/rastertofargo.c`, lines 83, 88, 99, 105–119, 156–166
**Severity:** Compilation failure (undefined identifiers)

`fargo_protocol.h` defines the ribbon enum type as `fargo_ribbon_type_t` with members named `FARGO_RIBBON_YMCKO`, `FARGO_RIBBON_YMCKOK`, etc.

`rastertofargo.c` uses a different type name (`fargo_ribbon_t`) and different member names (`RIBBON_YMCKO`, `RIBBON_YMCKOK`, `RIBBON_K_STD`, `RIBBON_K_CLR`, `RIBBON_K_PRM`, `RIBBON_KO`, `RIBBON_BO`, `RIBBON_YMCKK`, `RIBBON_YMCFKO`, `RIBBON_YMCFKOK`, `RIBBON_NONE`). None of these exist anywhere in the codebase.

Every reference to `fargo_ribbon_t` or any `RIBBON_*` constant in `rastertofargo.c` is an undefined identifier. This produces roughly 20 compiler errors on its own.

**Fix:** Either:
- Add `typedef fargo_ribbon_type_t fargo_ribbon_t;` and the corresponding short-name constants to `fargo_protocol.h`, or
- Change all `rastertofargo.c` references to the canonical `fargo_ribbon_type_t` / `FARGO_RIBBON_*` names.

The second option requires no new declarations and is less error-prone.

---

### BUG-02 — `fargo_ribbon_num_panels()` is called but never declared or defined

**File:** `src/rastertofargo.c`, lines 388, 634
**Severity:** Compilation failure (implicit declaration) and linker failure (undefined symbol)

`rastertofargo.c` calls `fargo_ribbon_num_panels(job->ribbon)` at two points. `fargo_protocol.h` declares `fargo_ribbon_panel_count()` (line 701), not `fargo_ribbon_num_panels()`. Neither version is implemented anywhere in the codebase — `fargo_protocol.c` ends at `fargo_send_job_start()` with no panel helper functions.

This produces both a compile-time warning (implicit declaration in C99; error in C11) and a linker error.

**Fix:** Either rename the call sites to `fargo_ribbon_panel_count()` and implement that function, or rename the declaration in the header to `fargo_ribbon_num_panels()` and implement it. Either way, the implementation is missing entirely.

**Minimum stub implementation needed in `fargo_protocol.c`:**
```c
int fargo_ribbon_panel_count(fargo_ribbon_type_t ribbon) {
    switch (ribbon) {
        case FARGO_RIBBON_NONE:       return 2;  /* RW_erase + RW_black */
        case FARGO_RIBBON_K_STD:      return 1;
        case FARGO_RIBBON_K_PRM:      return 1;
        case FARGO_RIBBON_MONO_COLOR: return 1;
        case FARGO_RIBBON_METALIC:    return 1;
        case FARGO_RIBBON_KO:         return 2;
        case FARGO_RIBBON_YMCKK:      return 5;
        case FARGO_RIBBON_YMCKO:      return 5;
        case FARGO_RIBBON_YMCKOK:     return 6;
        case FARGO_RIBBON_BO:         return 2;
        case FARGO_RIBBON_YMCKO_HALF: return 5;
        case FARGO_RIBBON_YMCFKO:     return 6;
        case FARGO_RIBBON_YMCFKOK:    return 7;
        default:                      return 1;
    }
}
```

---

### BUG-03 — Functions declared in `fargo_protocol.h` but never implemented

**File:** `src/fargo_protocol.h`, lines 701–707; `src/fargo_protocol.c` (entirely absent)
**Severity:** Linker failure (undefined symbols)

Three functions are declared in the header but have no implementation in `fargo_protocol.c`:
- `fargo_ribbon_panel_count()` (see BUG-02)
- `fargo_ribbon_panel_id()`
- `fargo_ribbon_is_dual_config()`

Additionally, the header declares `fargo_build_fs_setup()` and `fargo_build_fg_eoj()` and `fargo_build_ep()` (lines 659–677), but `fargo_protocol.c` implements `fargo_build_fs_init()` and `fargo_build_fg_data()` with no `fargo_build_fs_setup()`, no `fargo_build_fg_eoj()`, and no standalone `fargo_build_ep()` (only an internal `write_ep_marker()` helper). The `fargo_rle_decompress()` declared at line 691 is also not implemented.

The linker will fail on any of these if they are called. `rastertofargo.c` calls `fargo_build_fg_config()` and `fargo_build_fg_header()` which ARE implemented, but those implementations use constants (`CFG_OFF_CARD_WIDTH`, `HDR_OFF_PANEL_WIDTH`, etc.) that are never defined — see BUG-04.

---

### BUG-04 — Offset constants referenced in `fargo_protocol.c` are never defined

**File:** `src/fargo_protocol.c`, lines 177, 180, 184, 189, 193, 247, 250, 253
**Severity:** Compilation failure (undeclared identifiers)

`fargo_build_fg_config()` uses:
- `CFG_OFF_CARD_WIDTH`
- `CFG_OFF_CARD_HEIGHT`
- `CFG_OFF_RIBBON_TYPE`
- `CFG_OFF_COPIES`
- `CFG_OFF_DUPLEX`

`fargo_build_fg_header()` uses:
- `HDR_OFF_PANEL_WIDTH`
- `HDR_OFF_PANEL_HEIGHT`
- `HDR_OFF_NUM_PANELS`

None of these constants are defined anywhere in the codebase — not in `fargo_protocol.h` and not locally in `fargo_protocol.c`. Every reference is an undeclared identifier.

**Fix:** Define these in `fargo_protocol.h` or at the top of `fargo_protocol.c`. Based on `fargo_build_fg_config()`'s own comment block, the intended values are:

```c
/* Config packet (fargo_build_fg_config) payload offsets */
#define CFG_OFF_CARD_WIDTH    0x00
#define CFG_OFF_CARD_HEIGHT   0x04
#define CFG_OFF_RIBBON_TYPE   0x08
#define CFG_OFF_COPIES        0x0C
#define CFG_OFF_DUPLEX        0x10
```

However, see BUG-05 below — these offsets and the field widths (uint32 vs uint16) conflict with the confirmed protocol spec.

---

### BUG-05 — Wire-size constants referenced in `fargo_protocol.c` and `rastertofargo.c` are never defined

**File:** `src/fargo_protocol.c`, lines 88, 118, 157, 229, 352; `src/rastertofargo.c`, lines 59–68
**Severity:** Compilation failure (undeclared identifiers)

The following constants are used extensively but defined nowhere:
- `FARGO_WIRE_FS_START`
- `FARGO_WIRE_FS_INIT`
- `FARGO_WIRE_FG_CONFIG`
- `FARGO_WIRE_FG_HEADER`
- `FARGO_WIRE_FG_DATA`
- `FARGO_PKT_FS_START_LEN`
- `FARGO_PKT_FS_INIT_LEN`
- `FARGO_PKT_FG_CONFIG_LEN`
- `FARGO_PKT_FG_HEADER_LEN`
- `FARGO_PKT_FG_DATA_LEN`

`fargo_protocol.h` defines `FARGO_PAYLOAD_FS_START` (= 0), `FARGO_PAYLOAD_FS_SETUP` (= 8), `FARGO_PAYLOAD_CONFIG_STD` (= 48), `FARGO_PAYLOAD_DATA` (= 512), but the code uses a completely different naming convention (`FARGO_PKT_*_LEN`/`FARGO_WIRE_*`) that is not linked to the payload constants.

**Fix:** Add to `fargo_protocol.h`:

```c
#define FARGO_PKT_FS_START_LEN    FARGO_PAYLOAD_FS_START   /* 0 */
#define FARGO_PKT_FS_INIT_LEN     FARGO_PAYLOAD_FS_SETUP   /* 8 */
#define FARGO_PKT_FG_CONFIG_LEN   FARGO_PAYLOAD_CONFIG_STD /* 48 */
#define FARGO_PKT_FG_HEADER_LEN   134                       /* panel descriptor, ReWritable variant */
#define FARGO_PKT_FG_DATA_LEN     FARGO_PAYLOAD_DATA        /* 512 */

#define FARGO_WIRE_FS_START  (FARGO_PKT_HDR_SIZE + FARGO_PKT_FS_START_LEN + FARGO_PKT_EP_SIZE)   /*   8 */
#define FARGO_WIRE_FS_INIT   (FARGO_PKT_HDR_SIZE + FARGO_PKT_FS_INIT_LEN  + FARGO_PKT_EP_SIZE)   /*  16 */
#define FARGO_WIRE_FG_CONFIG (FARGO_PKT_HDR_SIZE + FARGO_PKT_FG_CONFIG_LEN + FARGO_PKT_EP_SIZE)  /*  56 */
#define FARGO_WIRE_FG_HEADER (FARGO_PKT_HDR_SIZE + FARGO_PKT_FG_HEADER_LEN + FARGO_PKT_EP_SIZE)  /* 142 */
#define FARGO_WIRE_FG_DATA   (FARGO_PKT_HDR_SIZE + FARGO_PKT_FG_DATA_LEN   + FARGO_PKT_EP_SIZE)  /* 520 */
```

---

### BUG-06 — `fargo_job_t` struct fields don't match what `rastertofargo.c` populates

**File:** `src/fargo_protocol.h`, lines 627–633; `src/rastertofargo.c`, lines 622–628
**Severity:** Compilation failure (no member named 'duplex', 'card_width', 'card_height')

`fargo_protocol.h` defines `fargo_job_t` with fields:
```c
fargo_ribbon_type_t ribbon;
int                 copies;
bool                dual_sided;
uint16_t            card_thickness;
```

`rastertofargo.c` populates it with a designated initializer using:
```c
.ribbon      = ribbon,
.copies      = copies,
.duplex      = duplex,        /* field does not exist */
.card_width  = header.cupsWidth,   /* field does not exist */
.card_height = header.cupsHeight,  /* field does not exist */
```

The field names `duplex`, `card_width`, and `card_height` do not exist in the struct. The existing field name is `dual_sided`. `card_width` and `card_height` are not in the struct at all.

Additionally, `fargo_protocol.c`'s `fargo_build_fg_config()` accesses `job->ribbon`, `job->copies`, and `job->duplex` — none of which match either. (`job->ribbon` is common to both; `job->copies` exists; but `job->duplex` does not exist in the header definition.)

**Fix:** Reconcile the struct. Add the missing fields:
```c
typedef struct {
    fargo_ribbon_type_t ribbon;
    int                 copies;
    bool                dual_sided;    /* keep this name OR rename to duplex */
    uint16_t            card_thickness;
    uint32_t            card_width;
    uint32_t            card_height;
} fargo_job_t;
```
Then either rename `.duplex` to `.dual_sided` in both `.c` files, or vice versa — pick one and be consistent.

---

### BUG-07 — `fargo_image_info_t` struct fields don't match what `fargo_build_fg_header()` uses

**File:** `src/fargo_protocol.h`, lines 635–639; `src/fargo_protocol.c`, lines 247–253
**Severity:** Compilation failure (no member named 'panel_width', 'panel_height', 'num_panels')

`fargo_protocol.h` defines `fargo_image_info_t` with fields `width`, `height`, `panel_count`.
`fargo_protocol.c` accesses `info->panel_width`, `info->panel_height`, `info->num_panels`.
`rastertofargo.c` populates `.panel_width`, `.panel_height`, `.num_panels`.

`fargo_protocol.h`'s field names (`width`, `height`, `panel_count`) match neither the implementation nor the caller. All three disagree.

**Fix:** Align the struct declaration in `fargo_protocol.h` to use the names actually used in `.c`:
```c
typedef struct {
    uint32_t panel_width;
    uint32_t panel_height;
    uint32_t num_panels;
} fargo_image_info_t;
```

---

### BUG-08 — `CARD_WIDTH_PIXELS` and `CARD_HEIGHT_PIXELS` are undefined

**File:** `src/rastertofargo.c`, lines 638–639
**Severity:** Compilation failure (undeclared identifiers)

```c
if (job.card_width  == 0) job.card_width  = CARD_WIDTH_PIXELS;
if (job.card_height == 0) job.card_height = CARD_HEIGHT_PIXELS;
```

`fargo_protocol.h` defines `FARGO_CR80_WIDTH_PIXELS` (638) and `FARGO_CR80_HEIGHT_PIXELS` (1013) but not the names used here.

**Fix:** Change to use the defined constants:
```c
if (job.card_width  == 0) job.card_width  = FARGO_CR80_WIDTH_PIXELS;
if (job.card_height == 0) job.card_height = FARGO_CR80_HEIGHT_PIXELS;
```

---

## 2. Protocol Correctness Bugs (will compile after fixes above, but produce wrong output)

### BUG-09 — Wrong bulk OUT endpoint: 0x02 instead of confirmed 0x01

**File:** `src/fargo_protocol.h`, line 47; `src/fargo_usb.c`, line 278
**Severity:** Critical — printer will never receive data

```c
#define FARGO_BULK_OUT_EP       0x02    /* TODO: Verify -- bulk OUT endpoint */
```

Per the review brief's confirmed facts: Bulk OUT EP = 0x01, Bulk IN EP = 0x81. The code uses 0x02. The eP field in `fargo_usb.c` and the comment in `fargo_usb.h` (line 63) both say `0x02 — TODO: verify`. This is now confirmed incorrect.

**Fix:** Change line 47 of `fargo_protocol.h`:
```c
#define FARGO_BULK_OUT_EP       0x01    /* CONFIRMED: Bulk OUT endpoint */
```

---

### BUG-10 — Status mechanism is entirely wrong: bulk IN is used where vendor control transfers are required

**File:** `src/fargo_usb.h` (lines 74–86); `src/fargo_usb.c` (lines 318–351); `src/fargo_protocol.h` (lines 46–49)
**Severity:** Critical — status polling will not work; the stub returns 0 unconditionally

Per the confirmed protocol facts:
- Status OUT: `libusb_control_transfer` with `bmRequestType=0x41`, `bRequest=0x00`, `wLength=43`
- Status IN: `libusb_control_transfer` with `bmRequestType=0xc1`, `bRequest=0x00`, `wLength=520`
- Status data uses version `0x0000` in the packet header (not 0x0001)

The code has `fargo_usb_recv()` doing a bulk IN transfer to EP 0x81 and `fargo_usb_get_status()` as a stub returning 0. Neither performs a vendor control transfer. Bulk IN transfers to 0x81 for status will not elicit any response from this printer.

**Fix required:** Add `fargo_usb_control_out()` and `fargo_usb_control_in()` functions using `libusb_control_transfer()`, and rewrite `fargo_usb_get_status()` to use them. The status request and response packets use FRL envelope format with version=0x0000 instead of 0x0001.

Note: The PROTOCOL_SPEC.md (section 2) still lists endpoints as "TODO: Verify" — this is now out of date with confirmed facts. Update the spec too.

---

### BUG-11 — `fargo_build_fg_config()` uses wrong field layout (uint32 fields at wrong offsets)

**File:** `src/fargo_protocol.c`, lines 176–193
**Severity:** Critical — config packet will be malformed; printer will not start print job

The confirmed config packet layout (from `fargo_protocol.h` struct `fargo_config_payload_t` and the PROTOCOL_SPEC.md confirmed table) uses **uint16** fields at specific offsets:
- Offset 0-1 (u16): job_type = 0x0001
- Offset 4-5 (u16): card_type = 0x0028 or 0x002A
- Offset 8-9 (u16): card_subtype = 0x0022 or 0x0024
- Offset 10-11 (u16): copies = 0x0001
- Offset 14-15 (u16): ribbon_type (CONFIRMED)
- Offset 22-23 (u16): printline_width = 0x0300 (768)
- Offset 40-41 (u16): card_thickness = 0x001E (30)

`fargo_build_fg_config()` instead writes `write_le32()` calls (4-byte writes) at offsets via `CFG_OFF_CARD_WIDTH` (0x00), `CFG_OFF_CARD_HEIGHT` (0x04), `CFG_OFF_RIBBON_TYPE` (0x08), `CFG_OFF_COPIES` (0x0C), `CFG_OFF_DUPLEX` (0x10). This layout is completely different from the confirmed wire format. The ribbon type is written at byte offset 8 with `write_le32`, but the confirmed ribbon type field is at byte offset 14-15 as a uint16.

The `fargo_config_payload_t` struct in `fargo_protocol.h` is the correct reference. The implementation in `fargo_build_fg_config()` ignores it entirely and invents a different layout. **One of them is wrong — the struct is confirmed correct; the builder function must be rewritten to match it.**

The fix is to replace the `write_le32()` calls with use of the `fargo_config_payload_t` struct:
```c
fargo_config_payload_t *cfg = (fargo_config_payload_t *)(buf + pos);
memset(cfg, 0, sizeof(*cfg));
cfg->job_type       = htole16(0x0001);
cfg->card_type      = htole16(dual ? 0x002A : 0x0028);
cfg->card_subtype   = htole16(dual ? 0x0024 : 0x0022);
cfg->copies         = htole16((uint16_t)job->copies);
cfg->ribbon_type    = htole16((uint16_t)job->ribbon);
cfg->dual_sided     = htole16(dual ? 0x0001 : 0x0000);
cfg->printline_width = htole16(768);
cfg->card_thickness = htole16(job->card_thickness ? job->card_thickness : 30);
pos += sizeof(*cfg);
```

---

### BUG-12 — `fargo_build_fg_header()` is a completely speculative packet that has no basis in the confirmed protocol

**File:** `src/fargo_protocol.c`, lines 227–268
**Severity:** High — sends an invalid 134-byte packet that the printer does not expect in this position

The confirmed job sequence (PROTOCOL_SPEC.md section 13) is:
1. Fs start × 2 + Fs setup
2. Fg config (48 bytes)
3. Fg 512-byte data strips (per panel, with inter-panel boundary packets between them)
4. Fg panel descriptor (126/134/190 bytes)
5. Fg end-of-job (14 bytes)

There is no 134-byte "image header" packet between the config and the data strips. `rastertofargo.c` sends this speculative packet at step 3 of the main loop (lines 665–680), which will cause the printer to receive unexpected data and likely abort or jam.

The 134-byte panel descriptor IS a real packet type, but it appears AFTER all image data, not before it.

**Fix:** Remove the `fargo_build_fg_header()` call from the main print loop. The panel descriptor is sent after image data, not before.

---

### BUG-13 — RLE max run is capped at 128 instead of confirmed 255

**File:** `src/fargo_protocol.c`, line 308
**Severity:** Medium — compressor is inefficient and produces larger-than-optimal output; no data corruption, but K panel data (highly repetitive) will expand up to 2x unnecessarily

```c
size_t run = 1;
while (run < 128 &&   /* <-- wrong cap */
```

The confirmed protocol spec (PROTOCOL_SPEC.md section 8, and `fargo_protocol.h` line 531) states count ranges 0x00 to 0xFF, meaning maximum repetitions = 256 (count 0xFF = 255+1 = 256 copies). The compressor comment at line 282 also incorrectly says "Maximum run length per pair: 128 bytes."

`FARGO_RLE_MAX_COUNT` is already defined as 0xFF in the header (line 532). The compressor should use it:
```c
while (run < (size_t)(FARGO_RLE_MAX_COUNT + 1) &&
```

Note: The comment block at line 289 in `fargo_protocol.c` says `Maximum run length per pair: 128 bytes (count-1 = 0x7F = 127, so count=128)`. This is wrong. The confirmed spec says count 0xFF = 256 copies. The header constant `FARGO_RLE_MAX_RUN` is already defined as 256. The comment and the cap value are both wrong.

---

### BUG-14 — `fargo_build_fg_config()` builds a 48-byte config for all ribbons; dual-sided ribbons require 50 bytes

**File:** `src/fargo_protocol.c`, lines 155–205; `src/fargo_protocol.h`, lines 120–125
**Severity:** High — YMCKK and YMCKOK will produce malformed config packets

Per the confirmed protocol, YMCKK (ribbon_type=6) and YMCKOK (ribbon_type=8) use a 50-byte config payload (`FARGO_PAYLOAD_CONFIG_DUAL = 50`) with extra bytes at offsets 44-45 and 48-49. `FARGO_WIRE_FG_CONFIG` as currently calculated is 56 bytes (6+48+2), which will be wrong for dual-sided jobs.

The function prototype in `fargo_protocol.h` (line 663) declares `fargo_build_fg_config()` returning `int` with no dual-sided awareness; the function needs to return different sizes based on `job->dual_sided`.

---

### BUG-15 — `send_job_end()` is a no-op; the confirmed protocol requires an Fg EOJ packet

**File:** `src/rastertofargo.c`, lines 498–504
**Severity:** High — without the EOJ packet the printer will not release the card

The confirmed end-of-job packet is 14 bytes: `63 00 00 00 06 00 00 00 00 00 00 00 00 00`. `fargo_protocol.h` defines `fargo_eoj_payload_t` and declares `fargo_build_fg_eoj()`. Neither is implemented and `send_job_end()` does nothing.

---

### BUG-16 — Command packets use version 0x0000 in packet header, not 0x0001

**File:** `src/fargo_protocol.h` (line 93); `src/fargo_protocol.c`, `write_pkt_hdr()` (line 36)
**Severity:** Medium — maintenance commands (clean, calibrate) will fail

PROTOCOL_SPEC.md section 9, hex example for `CleanPrinter.prn`, shows:
```
46 67 00 00 20 00   Fg, version=0x0000, length=32
```

The packet header version is `0x0000` for command packets, not `0x0001`. `write_pkt_hdr()` always writes `FARGO_PKT_VERSION` (= 0x0001). The builder function has no way to specify version 0x0000.

`fargo_protocol.h` line 93 defines `FARGO_PKT_VERSION = 0x0001` with no constant for the command-packet version.

**Fix:** Add a second constant `FARGO_PKT_VERSION_CMD = 0x0000` and modify `write_pkt_hdr()` to accept the version as a parameter, or add a separate `fargo_build_fg_command()` that writes 0x0000 directly.

---

### BUG-17 — PPD ribbon option name mismatch: code looks for `"HIDFargoRibbonType"` but PPD uses `"Ribbon"`

**File:** `src/rastertofargo.c`, line 92
**Severity:** High — ribbon type will ALWAYS default to YMCKO regardless of user selection

```c
ppd_choice_t *choice = ppdFindMarkedChoice(ppd, "HIDFargoRibbonType");
```

The reference PPD (`DTC4500e_mac_v1.3.2.7.ppd`, line 310) defines the option as:
```
*OpenUI *Ribbon/Ribbon Type: PickOne
```

The PPD option name is `Ribbon`, not `HIDFargoRibbonType`. `ppdFindMarkedChoice()` will always return NULL, so `ppd_ribbon_type()` will always return `RIBBON_YMCKO` (the default fallback), making ribbon selection non-functional.

**Fix:**
```c
ppd_choice_t *choice = ppdFindMarkedChoice(ppd, "Ribbon");
```

Also, the PPD choice values use `KStandard`, `KPremium`, `MonoColor` (PPD lines 319–321), but `rastertofargo.c` compares against `"K_STD"`, `"K_PRM"`, `"K_CLR"` (lines 111–113). These do not match.

**Fix for the comparison strings:**
```c
/* PPD choice values from DTC4500e_mac_v1.3.2.7.ppd */
if      (strcmp(val, "YMCKO")      == 0) return FARGO_RIBBON_YMCKO;
else if (strcmp(val, "YMCKOK")     == 0) return FARGO_RIBBON_YMCKOK;
else if (strcmp(val, "YMCKK")      == 0) return FARGO_RIBBON_YMCKK;
else if (strcmp(val, "YMCFKO")     == 0) return FARGO_RIBBON_YMCFKO;
else if (strcmp(val, "YMCFKOK")    == 0) return FARGO_RIBBON_YMCFKOK;
else if (strcmp(val, "KO")         == 0) return FARGO_RIBBON_KO;
else if (strcmp(val, "KStandard")  == 0) return FARGO_RIBBON_K_STD;   /* PPD name */
else if (strcmp(val, "MonoColor")  == 0) return FARGO_RIBBON_MONO_COLOR;
else if (strcmp(val, "KPremium")   == 0) return FARGO_RIBBON_K_PRM;   /* PPD name */
else if (strcmp(val, "BO")         == 0) return FARGO_RIBBON_BO;
else if (strcmp(val, "None")       == 0) return FARGO_RIBBON_NONE;
else if (strcmp(val, "YMCKO_Half") == 0) return FARGO_RIBBON_YMCKO_HALF;
```

---

### BUG-18 — `is_color_ribbon()` is incomplete: BO, KO, NONE (2-panel) are not accounted for

**File:** `src/rastertofargo.c`, lines 156–168
**Severity:** Medium — BO and KO will be processed as monochrome single-panel; NONE (ReWritable) also needs two panels

`is_color_ribbon()` returns 1 for YMCKO/YMCKOK/YMCKK/YMCFKO/YMCFKOK and 0 for everything else. But KO has 2 panels (K + Overlay), BO has 2 panels (B_dyesub + Overlay), and NONE has 2 panels (RW_erase + RW_black). All of these will be incorrectly processed as single-panel monochrome jobs.

The broader issue is that the two-code-path design (monochrome vs color) is too coarse — the real driver (based on the Linux binary analysis) has a full `Panel`/`PanelSet` abstraction per ribbon type.

---

## 3. macOS-Specific Issues

### BUG-19 — `libusb_detach_kernel_driver()` on macOS returns `LIBUSB_ERROR_NOT_SUPPORTED`, not treated as graceful

**File:** `src/fargo_usb.c`, lines 170–179
**Severity:** Low — the code does continue on failure, but the error message misleads

The code logs the failure as `ERROR:` (line 174) and continues, which is correct behavior. However, on macOS, `libusb_kernel_driver_active()` itself also returns `LIBUSB_ERROR_NOT_SUPPORTED` (not 0 or 1). This case is handled on line 182-184, but logged as `DEBUG:` without distinguishing the NOT_SUPPORTED case from actual errors.

More importantly: on macOS with Apple Silicon and modern IOKit, `libusb_detach_kernel_driver()` always returns `LIBUSB_ERROR_NOT_SUPPORTED`. The log message `"libusb_detach_kernel_driver failed"` is alarming when it is actually normal. Change to:

```c
if (rc == LIBUSB_ERROR_NOT_SUPPORTED) {
    fprintf(stderr, "DEBUG: fargo_usb_open: kernel driver detach not supported "
            "(normal on macOS) — proceeding\n");
} else {
    fprintf(stderr, "ERROR: fargo_usb_open: libusb_detach_kernel_driver failed: %s\n",
            libusb_strerror((enum libusb_error)rc));
}
```

---

### BUG-20 — `libusb` include path is Linux-style and will not work on macOS Homebrew installs

**File:** `src/fargo_usb.c`, line 15
**Severity:** Compilation failure on some macOS setups

```c
#include <libusb-1.0/libusb.h>
```

On macOS with Homebrew, the correct path depends on the install, but `pkg-config --cflags libusb-1.0` emits the right `-I` flag. The nested path `<libusb-1.0/libusb.h>` works when the include root contains the `libusb-1.0/` subdirectory, which is not always the case.

The Makefile correctly passes `$(LIBUSB_CFLAGS)` to the compiler, which includes the path. The safer include is simply:
```c
#include <libusb.h>
```
which will work when the `-I` path from `pkg-config` is in effect. The current `<libusb-1.0/libusb.h>` form works on many systems but fails on some Homebrew configurations.

---

### Note: Makefile is correct for macOS

The Makefile correctly uses:
- `CUPS_FILTER_DIR := /usr/libexec/cups/filter` (correct for macOS)
- `-arch arm64` flag (correct for Apple Silicon)
- Homebrew fallback paths for libusb

No bugs found in the Makefile itself.

---

## 4. Security Issues

### SEC-01 — Buffer overflow potential in RLE compression when compressing across panel boundaries

**File:** `src/rastertofargo.c`, lines 250–262
**Severity:** Low (bounded by allocation) — worth noting

`rle_row_max` is allocated as `width * 2` bytes. The compressor's worst case is exactly `width * 2` bytes (every pixel is a run of 1, outputting 2 bytes per input byte). The check `if (out_pos + 2 > output_max)` in `fargo_rle_compress()` will catch any overflow and return -1. The caller checks for -1. So the overflow is not actually exploitable here — but the worst case is exact, not conservative. A width of `UINT32_MAX / 2` would overflow the `size_t rle_row_max` calculation itself.

For the actual card widths (638 or 768 pixels), this is safe.

### SEC-02 — `atoi(argv[4])` for copies count with no length check

**File:** `src/rastertofargo.c`, line 533
**Severity:** Very low — clamped immediately after

```c
int copies = atoi(argv[4]);
```

`atoi` silently returns 0 on non-numeric input and does not detect overflow. For a CUPS filter this is acceptable since CUPS controls the arguments. The clamp on lines 541–542 handles the 0 case. Not a practical vulnerability in this context.

---

## 5. TODO Inventory

### Critical TODOs (blocking correct operation after compilation fixes)

| ID | Location | TODO | Why Critical |
|----|----------|------|--------------|
| T-01 | `fargo_protocol.c:fargo_build_fg_config()` | Implement config packet using confirmed struct layout | Current layout is wrong; printer will reject the job |
| T-02 | `fargo_protocol.c` (missing) | Implement `fargo_build_fg_eoj()` | Without EOJ the card is never released |
| T-03 | `fargo_protocol.c` (missing) | Implement `fargo_ribbon_panel_count()` | Called at two points; currently a linker error |
| T-04 | `fargo_usb.c:fargo_usb_get_status()` | Implement vendor control transfers for status | Required for knowing when a job is done vs. failed |
| T-05 | `fargo_protocol.c` (missing) | Implement inter-panel boundary packets | Without these the printer does not know where one panel ends and the next begins |
| T-06 | `rastertofargo.c:send_job_end()` | Send confirmed EOJ packet | Card will not eject without it |
| T-07 | `rastertofargo.c:process_color_page()` | Verify RGB→YMCK channel mapping | Speculative conversion; wrong mapping = wrong colors on card |
| T-08 | `rastertofargo.c:process_monochrome_page()` | Handle 1-bit raster input | PPD may select 1-bit mode; 8-bit assumption may fail |

### Non-Critical TODOs (cosmetic or post-first-print)

| ID | Location | TODO |
|----|----------|------|
| T-09 | `fargo_protocol.h:47` | Update "TODO: Verify" comment now that EP=0x01 is confirmed |
| T-10 | `fargo_usb.c:97–101` | Gate debug log behind compile flag |
| T-11 | `rastertofargo.c:542` | Verify max copies limit from printer spec |
| T-12 | `fargo_protocol.h:633` | Add SplitRibbon, KPanelApply fields to fargo_job_t |
| T-13 | `fargo_protocol.c:196–197` | Identify reserved config fields from USB capture |
| T-14 | `rastertofargo.c:126–144` | Verify duplex PPD option handling (printer may not support hardware duplex) |
| T-15 | `fargo_usb.c:263` | Verify if multi-transfer chunking is needed |
| T-16 | `fargo_protocol.c:fargo_build_fg_header()` | Entire function is speculative — either remove or verify its role |
| T-17 | `rastertofargo.c:78` | Verify actual PPD option name (now confirmed as "Ribbon" — already known) |
| T-18 | `fargo_protocol.h:36` | Add `#define FARGO_PID_DTC4500E 0xbf0c` now that PID is confirmed |
| T-19 | Various | Update PROTOCOL_SPEC.md sections 2 and 17 to mark endpoints as CONFIRMED |

---

## 6. Overall Assessment

### Will this compile?
**No.** There are at minimum 8 categories of compilation errors:
1. `fargo_ribbon_t` / `RIBBON_*` constants undefined (BUG-01)
2. `fargo_ribbon_num_panels()` undeclared and undefined (BUG-02)
3. Wire-size constants undefined (`FARGO_WIRE_*`, `FARGO_PKT_*_LEN`) (BUG-05)
4. Offset constants undefined (`CFG_OFF_*`, `HDR_OFF_*`) (BUG-04)
5. Struct field mismatches in `fargo_job_t` (BUG-06)
6. Struct field mismatches in `fargo_image_info_t` (BUG-07)
7. `CARD_WIDTH_PIXELS` / `CARD_HEIGHT_PIXELS` undefined (BUG-08)
8. Multiple functions declared in header but never implemented, called by the linker (BUG-03)

A rough estimate: the compiler will emit 30–40 errors before stopping on the first translation unit.

### Will this produce correct output after compilation fixes?
**No.** Even after fixing all compilation errors, the following protocol bugs would produce incorrect behavior:
- Wrong bulk OUT endpoint (0x02 vs 0x01) — printer receives nothing (BUG-09)
- Config packet layout is wrong — printer rejects job (BUG-11)
- Spurious image-header packet sent before data strips — printer confused (BUG-12)
- EOJ packet never sent — card never released (BUG-15)
- Status uses bulk IN instead of vendor control transfer — no status information (BUG-10)
- PPD option name wrong — ribbon selection broken (BUG-17)
- PPD choice strings wrong — all ribbons become YMCKO (BUG-17)

---

## 7. Recommended Fix Priority Order

**Phase 1 — Make it compile (fix all of these before doing anything else):**

1. **BUG-05** — Define `FARGO_PKT_*_LEN` and `FARGO_WIRE_*` constants in `fargo_protocol.h`
2. **BUG-04** — Define `CFG_OFF_*` and `HDR_OFF_*` constants
3. **BUG-01** — Align `fargo_ribbon_t` type name and `RIBBON_*` constants (add aliases or rename all references)
4. **BUG-06** — Fix `fargo_job_t` field names (`duplex` vs `dual_sided`, add `card_width`/`card_height`)
5. **BUG-07** — Fix `fargo_image_info_t` field names
6. **BUG-08** — Change `CARD_WIDTH_PIXELS` to `FARGO_CR80_WIDTH_PIXELS`
7. **BUG-02/03** — Implement stub `fargo_ribbon_panel_count()` in `fargo_protocol.c`

**Phase 2 — Make it send correct protocol data:**

8. **BUG-09** — Fix bulk OUT endpoint from 0x02 to 0x01
9. **BUG-11** — Rewrite `fargo_build_fg_config()` to use `fargo_config_payload_t` struct with confirmed field layout
10. **BUG-17** — Fix PPD option name (`"Ribbon"` not `"HIDFargoRibbonType"`) and choice strings (`"KStandard"` not `"K_STD"`)
11. **BUG-12** — Remove the `fargo_build_fg_header()` call from the main loop (it sends a packet that does not belong at that position in the job sequence)
12. **BUG-15** — Implement `fargo_build_fg_eoj()` and call it from `send_job_end()`
13. **BUG-14** — Handle 50-byte dual-sided config for YMCKK and YMCKOK

**Phase 3 — Critical missing protocol pieces (required to print):**

14. **T-05** — Implement inter-panel boundary packets (variable-length Fg packets between data strips)
15. **BUG-10** — Implement vendor control transfers for status polling
16. **BUG-13** — Fix RLE compressor max run from 128 to 255 (use `FARGO_RLE_MAX_COUNT`)
17. **BUG-16** — Fix command packet header version to 0x0000
18. **BUG-18** — Handle BO/KO/NONE as 2-panel ribbons

**Phase 4 — Quality / correctness:**

19. **T-07** — Verify RGB→YMCK color conversion
20. **BUG-19** — Improve macOS kernel driver detach error message
21. **BUG-20** — Simplify libusb include path
22. Remaining non-critical TODOs

---

## 8. Summary Table

| Bug ID | File | Severity | Category |
|--------|------|----------|----------|
| BUG-01 | rastertofargo.c | Compile error | Type naming mismatch |
| BUG-02 | rastertofargo.c, fargo_protocol.c | Compile + link error | Missing function |
| BUG-03 | fargo_protocol.c | Link error | Missing implementations |
| BUG-04 | fargo_protocol.c | Compile error | Undefined constants |
| BUG-05 | fargo_protocol.h | Compile error | Undefined constants |
| BUG-06 | fargo_protocol.h, rastertofargo.c | Compile error | Struct field mismatch |
| BUG-07 | fargo_protocol.h, fargo_protocol.c | Compile error | Struct field mismatch |
| BUG-08 | rastertofargo.c | Compile error | Undefined constant |
| BUG-09 | fargo_protocol.h, fargo_usb.c | Critical protocol | Wrong endpoint |
| BUG-10 | fargo_usb.c | Critical protocol | Wrong transfer type for status |
| BUG-11 | fargo_protocol.c | Critical protocol | Wrong config packet layout |
| BUG-12 | fargo_protocol.c, rastertofargo.c | Critical protocol | Spurious packet in wrong position |
| BUG-13 | fargo_protocol.c | Medium protocol | RLE compressor cap wrong |
| BUG-14 | fargo_protocol.c | High protocol | Dual-sided config size wrong |
| BUG-15 | rastertofargo.c | High protocol | EOJ packet not sent |
| BUG-16 | fargo_protocol.c | Medium protocol | Command packet version wrong |
| BUG-17 | rastertofargo.c | High protocol | PPD option name/values wrong |
| BUG-18 | rastertofargo.c | Medium protocol | Multi-panel non-color ribbons mishandled |
| BUG-19 | fargo_usb.c | Low macOS | Misleading error message |
| BUG-20 | fargo_usb.c | Low macOS | libusb include path |
| SEC-01 | rastertofargo.c | Low security | Bounded RLE allocation |
| SEC-02 | rastertofargo.c | Very low security | atoi for copies |
