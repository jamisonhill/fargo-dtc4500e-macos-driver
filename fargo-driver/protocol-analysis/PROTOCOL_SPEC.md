# Fargo DTC4500e Protocol Specification

**Document Version:** 1.0
**Date:** 2026-02-24
**Status:** Reverse-engineered from binary analysis; not vendor-verified
**Sources:** PRN test files (Mac driver v1.3.2), Linux binary (rastertofargo-3.3.4), PPD files, DTC4500e.xml

---

## Table of Contents

1. [Overview](#1-overview)
2. [USB Communication](#2-usb-communication)
3. [Packet Envelope Format](#3-packet-envelope-format)
4. [Packet Types](#4-packet-types)
5. [Job Configuration Packet](#5-job-configuration-packet)
6. [Panel Descriptor Packet](#6-panel-descriptor-packet)
7. [Image Data Packets](#7-image-data-packets)
8. [RLE Compression](#8-rle-compression)
9. [Printer Command Packets](#9-printer-command-packets)
10. [End-of-Job Packet](#10-end-of-job-packet)
11. [Ribbon Types and Panel Sequences](#11-ribbon-types-and-panel-sequences)
12. [Card Geometry](#12-card-geometry)
13. [Job Sequence](#13-job-sequence)
14. [PPD Comparison](#14-ppd-comparison)
15. [Linux Binary Analysis](#15-linux-binary-analysis)
16. [DTC4500e.xml Summary](#16-dtc4500exml-summary)
17. [Unknowns and TODOs](#17-unknowns-and-todos)

---

## 1. Overview

The HID Fargo DTC4500e uses a proprietary protocol called "Fargo Raster Language" (FRL) to receive print jobs over USB. The protocol sends RLE-compressed monochrome panel data for each color plane (Y, M, C, K, O, etc.) along with configuration and control packets.

The protocol was reverse-engineered from:
- **18 PRN test files** extracted from the Mac driver installer package (v1.3.2)
- **Linux ARM64 binary** `rastertofargo-3.3.4` (string and symbol analysis)
- **Mac PPD** `DTC4500e_mac_v1.3.2.7.ppd` and **Linux PPD** `DTC4500e_linux_v1.0.0.4.ppd`
- **DTC4500e.xml** printer model definition file

---

## 2. USB Communication

### Vendor/Product IDs

| VID | Vendor | Notes |
|-----|--------|-------|
| `0x09b0` | Fargo Electronics (legacy) | Original pre-HID acquisition |
| `0x076b` | HID Global | Post-acquisition, PIDs `0xbf00`-`0xbfff` |

**TODO:** Verify exact Product ID for DTC4500e from a live USB enumeration.

### Endpoints

The protocol writes FRL packets to the printer's bulk OUT endpoint. The Linux driver writes to stdout (CUPS backend handles USB transport). The Mac driver uses IOKit USB interfaces directly.

| Parameter | Assumed Value | Status |
|-----------|--------------|--------|
| Interface | 0 | TODO: Verify |
| Bulk OUT EP | 0x02 | TODO: Verify |
| Bulk IN EP | 0x81 | TODO: Verify |

### CUPS Filter

- **Mac:** `application/vnd.cups-raster 50 /usr/libexec/cups/filter/rastertofargo-1.3.2`
- **Linux:** `application/vnd.cups-raster 50 rastertofargo-3.3.4`

The filter reads CUPS raster input, converts it to FRL packets, and writes them to stdout.

---

## 3. Packet Envelope Format

Every FRL packet follows this structure:

```
+--------+--------+---------+---------+    +------+------+
| Marker | Version| Length  | Payload |    | 'e'  | 'P'  |
| 2 bytes| 2 bytes| 2 bytes | N bytes |    | 0x65 | 0x50 |
+--------+--------+---------+---------+    +------+------+
      Fg/Fs packet (6 + N bytes)         eP terminator (2 bytes)
```

### Header Fields

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 2 | Magic | `0x46 0x67` ("Fg") or `0x46 0x73` ("Fs") |
| 2 | 2 | Version | Always `0x01 0x00` (LE 0x0001) |
| 4 | 2 | Length | Payload length in bytes, 16-bit little-endian |
| 6 | N | Payload | N bytes of packet data |

### eP Terminator

After each Fg or Fs packet, a separate 2-byte write sends the end-of-packet marker:

```
0x65 0x50  ("eP")
```

**CONFIRMED:** Every packet in all 18 PRN files is followed by an eP marker. The eP is NOT counted in the payload length.

### Key Constants

- `Fs` marker: `0x46 0x73` (LE uint16 = `0x7346`)
- `Fg` marker: `0x46 0x67` (LE uint16 = `0x6746`)
- `eP` marker: `0x65 0x50` (LE uint16 = `0x5065`)

---

## 4. Packet Types

Packet type is determined by the marker (Fs/Fg) and payload length:

| Marker | Length | Type | Description |
|--------|--------|------|-------------|
| Fs | 0 | Session Start | Initiates/resets communication |
| Fs | 8 | Session Setup | 8 bytes of zeros |
| Fg | 32 | Command | Printer maintenance (clean, calibrate) |
| Fg | 48 | Config (single) | Job configuration for single-sided |
| Fg | 50 | Config (dual) | Job configuration for dual-sided |
| Fg | 512 | Data Strip | RLE-compressed image data |
| Fg | Variable | Panel Section | Inter-panel boundary data |
| Fg | 126 | Panel Descriptor | Standard panel geometry + heat |
| Fg | 134 | Panel Descriptor | ReWritable ribbon variant |
| Fg | 190 | Panel Descriptor | Fluorescent panel variant |
| Fg | 14 | End of Job | Job completion signal |

---

## 5. Job Configuration Packet

**Payload size:** 48 bytes (single-sided) or 50 bytes (dual-sided)

### Hex Example (YMCKO, single-sided):

```
Fg header: 46 67 01 00 30 00
Payload:   01 00 00 00 28 00 00 00 22 00 01 00 00 00 07 00
           00 00 00 00 00 00 00 03 01 00 00 00 00 00 00 00
           00 00 00 00 00 00 00 00 1e 00 00 00 00 00 00 00
eP:        65 50
```

### Field Map (CONFIRMED by cross-referencing all PRN files):

| Offset | Size | NONE | K_STD | K_PRM | K_CLR | KO | BO | YMCKO | YMCKK | YMCKOK | YMCFKO | YMCKO_Half | YMCFKOK |
|--------|------|------|-------|-------|-------|----|----|-------|-------|--------|--------|------------|---------|
| 0-1 | u16 | 0x0001 | 0x0001 | 0x0001 | 0x0001 | 0x0001 | 0x0001 | 0x0001 | 0x0001 | 0x0001 | 0x0001 | 0x0001 | 0x0001 |
| 4-5 | u16 | 0x0028 | 0x0028 | 0x0028 | 0x0028 | 0x0028 | 0x0028 | 0x0028 | **0x002A** | **0x002A** | 0x0028 | 0x0028 | 0x0028 |
| 8-9 | u16 | 0x0022 | 0x0022 | 0x0022 | 0x0022 | 0x0022 | 0x0022 | 0x0022 | **0x0024** | **0x0024** | 0x0022 | 0x0022 | 0x0022 |
| 10-11 | u16 | 0x0001 | 0x0001 | 0x0001 | 0x0001 | 0x0001 | 0x0001 | 0x0001 | 0x0001 | 0x0001 | 0x0001 | 0x0001 | 0x0001 |
| **14-15** | u16 | **0** | **1** | **2** | **3** | **5** | **9** | **7** | **6** | **8** | **19** | **13** | **20** |
| 16-17 | u16 | 0x0000 | 0x0000 | 0x0000 | 0x0000 | 0x0000 | 0x0000 | 0x0000 | **0x0001** | **0x0001** | 0x0000 | 0x0000 | 0x0000 |
| 22-23 | u16 | 0x0300 | 0x0300 | 0x0300 | 0x0300 | 0x0300 | 0x0300 | 0x0300 | 0x0300 | 0x0300 | 0x0300 | 0x0300 | 0x0300 |
| 40-41 | u16 | 0x001E | 0x001E | 0x001E | 0x001E | 0x001E | 0x001E | 0x001E | 0x001E | 0x001E | 0x001E | 0x001E | 0x001E |

### Key Findings:

- **Offset 14-15: Ribbon Type** -- This is the `device_id` from the XML ribbon definition. CONFIRMED across all 12 ribbon types.
- **Offset 22-23: Print Line Width** -- Always 0x0300 (768). CONFIRMED.
- **Offset 40-41: Card Thickness** -- Always 0x001E (30 mils). CONFIRMED.
- **Offset 4-5: Card Type** -- 0x0028 (40) for single-sided, 0x002A (42) for dual-sided configs (YMCKK, YMCKOK).
- **Offset 8-9: Card Sub-type** -- 0x0022 (34) for single, 0x0024 (36) for dual.
- **Offset 16-17: Dual-sided Flag** -- 0x0001 for YMCKK and YMCKOK (the only ribbons with back-side K panels).
- **Payload size:** 50 bytes (not 48) for YMCKK and YMCKOK, with extra 2 bytes.

---

## 6. Panel Descriptor Packet

Panel descriptors appear near the end of the job, after all image data strips. They contain image geometry and heat settings.

### Common Header (first 24 bytes, CONFIRMED):

```
Offset  Bytes          Meaning
0-1     0F 00          Descriptor type = 15 (always)
2-3     00 00          Reserved
4-5     16 00          Sub-type = 22 (always)
6-7     00 00          Reserved
8-9     10 00          Block type = 16 (always)
10-11   XX 00          Panel count for this side
12-13   01 00          Card count = 1 (always)
14-15   XX 00          Panel device_id
16-19   00 00 00 00    Reserved
20-21   XX XX          Image height in lines
22-23   XX XX          Height parameter 2
```

### Examples:

**YMCKO (126 bytes):**
```
Panel count: 4 (Y,M,C,K -- overlay handled separately)
Device ID: 7 (Overlay)
Height: 0x03F1 = 1009
```

**NONE (134 bytes):**
```
Panel count: 1
Device ID: 11 (ReWritable erase)
Height: 0x03F3 = 1011
```

**YMCKOK (126 bytes):**
```
Panel count: 4 (front side Y,M,C,K)
Device ID: 7 (Overlay)
Height: 0x03F3 = 1011
```

**YMCFKOK (190 bytes):**
```
Panel count: 3 (TODO: verify meaning)
Device ID: 9 (Fluorescent)
Height: 0x005E = 94 (TODO: this is a panel-specific parameter)
```

---

## 7. Image Data Packets

### Format

Image data is sent as a series of Fg packets with 512-byte payloads, each containing RLE-compressed pixel data.

```
46 67 01 00 00 02 [512 bytes RLE data] 65 50
 Fg   ver   len            payload       eP
```

### Data Stream Layout

The RLE data across all 512-byte packets forms a continuous byte stream. When decoded, this stream contains the pixel data for all panels on one side of the card.

**Decoded line structure (from XML `PrintLine*` properties):**
```
[2 escape bytes] [768 pixel bytes] [1 extra byte] = 771 bytes per line
```

- **Escape bytes (2):** Per-line header, likely contains heat/timing info
- **Pixel bytes (768):** One byte per pixel, printhead width
- **Extra byte (1):** Per-line trailer, purpose unknown

**TODO:** Verify exact per-line structure from USB capture.

### Panel Data Organization

For multi-panel ribbons (e.g., YMCKO), the panels are sent sequentially in ribbon order. Between panels, a variable-length Fg packet marks the panel boundary.

**YMCKO data breakdown (CONFIRMED):**

| Panel | Data Packets | Boundary Packet | Notes |
|-------|-------------|-----------------|-------|
| Yellow | 223 | 8 bytes | First panel after config |
| Magenta | 227 | 155 bytes | |
| Cyan | 228 | 39 bytes | |
| Black (K) | 23 | 367 bytes | Highly compressed (mostly solid) |
| Overlay | 0 | 126-byte descriptor | Described by panel descriptor |

**K_STD data breakdown (single panel, CONFIRMED):**

| Panel | Data Packets | Boundary Packet | Notes |
|-------|-------------|-----------------|-------|
| Black (K) | 512 | 82 bytes | Then 14-byte EOJ |

**Total decoded bytes per ribbon type:**

| Ribbon | Data Packets | Decoded Bytes | Notes |
|--------|-------------|---------------|-------|
| NONE | 142 | 2,749,781 | ReWritable card |
| K_STD | 512 | 14,830,066 | Single K panel |
| K_CLR | 512 | 14,830,066 | Same test pattern |
| K_PRM | 512 | 14,830,066 | Same test pattern |
| KO | 512 | (similar) | K panel data + overlay desc |
| YMCKO | 701 | (Y+M+C+K) | Split across 4 data sections |

---

## 8. RLE Compression

### Algorithm

Simple byte-pair Run-Length Encoding:

```
Input:  (count, value) pairs
Output: value repeated (count + 1) times

count = 0x00 -> 1 byte
count = 0x7F -> 128 bytes
count = 0xFF -> 256 bytes
```

### Encoding Rules

1. Each pair is exactly 2 bytes: `[count_byte] [value_byte]`
2. `count_byte` ranges from 0x00 to 0xFF
3. The pair produces `(count_byte + 1)` copies of `value_byte`
4. Pairs are always complete within each 512-byte packet (512 is even)
5. The decoded data stream is continuous across packet boundaries
6. There is NO literal-run mode (unlike PackBits)
7. No escape sequences or special count values

### Example

```
Raw RLE:    7F 00  02 FF  00 AB
Decoded:    [128 x 0x00] [3 x 0xFF] [1 x 0xAB]
            = 128 zeros, then 3 FFs, then 1 AB
            = 132 bytes decoded from 6 bytes
```

### Verification

Round-trip encoding was verified on multiple data packets from the NONE test file. The simple count+1 interpretation produces consistent results across all PRN files.

**CONFIRMED:** This is the correct RLE interpretation.

---

## 9. Printer Command Packets

### Format

32-byte Fg payload for maintenance operations:

```
Offset  Size  Description
0-1     u16   Command version (always 0x0001)
2-11    10B   Reserved (zeros)
12-13   u16   Command ID
14-15   u16   Command flags
16-31   16B   Reserved (zeros)
```

### Known Commands (CONFIRMED from PRN files):

| Command | ID (offset 12) | Flags (offset 14) | PRN File |
|---------|----------------|-------------------|----------|
| Clean Printer | 0x0014 | 0x0008 | CleanPrinter.prn |
| Calibrate Ribbon | 0x0011 | 0x0000 | CalibrateRibbon.prn |
| Calibrate Lamination | 0x0013 | 0x0000 | CalibrateLamination.prn |

### Hex Examples:

**CleanPrinter.prn (complete file, 40 bytes):**
```
46 67 00 00 20 00              Fg, version=0, length=32
01 00 00 00 00 00 00 00 00 00  version=1, reserved
14 00 08 00                    cmd=0x0014(clean), flags=0x0008
00 00 00 00 00 00 00 00 00 00  reserved
00 00 00 00 00 00              reserved
65 50                          eP
```

**Note:** Command packets use version=0x0000 in the packet header (not 0x0001 like data packets).

---

## 10. End-of-Job Packet

### Format

14-byte Fg payload, identical across all image-bearing PRN files:

```
63 00 00 00 06 00 00 00 00 00 00 00 00 00
```

| Offset | Size | Value | Meaning |
|--------|------|-------|---------|
| 0-1 | u16 | 0x0063 (99) | EOJ marker |
| 2-3 | u16 | 0x0000 | Reserved |
| 4-5 | u16 | 0x0006 (6) | Section type |
| 6-13 | 8B | zeros | Reserved |

**CONFIRMED:** Identical in all PRN files that contain image data.

---

## 11. Ribbon Types and Panel Sequences

### Ribbon Device IDs (from XML + PRN cross-reference)

| Name | device_id | Config[14-15] | Grade | Panels | PRN File |
|------|-----------|---------------|-------|--------|----------|
| None | 0 | 0x0000 | professional | RW_Erase, RW_Black | NONE |
| KStandard | 1 | 0x0001 | professional | K(resin) | K_STD |
| KPremium | 2 | 0x0002 | professional | K(resin) | K_PRM |
| MonoColor | 3 | 0x0003 | professional | K(color resin) | K_CLR |
| Metalic | 4 | 0x0004 | professional | K(color resin) | (none) |
| KO | 5 | 0x0005 | professional | K(resin), O | KO |
| YMCKK | 6 | 0x0006 | professional | Y, M, C, K, K(split) | YMCKK |
| YMCKO | 7 | 0x0007 | professional | Y, M, C, K(split), O | YMCKO |
| YMCKOK | 8 | 0x0008 | professional | Y, M, C, K, O, K(split) | YMCKOK |
| BO | 9 | 0x0009 | professional | B(dyesub), O | BO |
| YMCKO_Half | 13 | 0x000D | professional | Y(half), M(half), C(half), K(split), O | YMCKO_Half |
| YMCFKO | 19 | 0x0013 | professional | Y, M, C, F, K(split), O | YMCFKO |
| YMCFKOK | 20 | 0x0014 | professional | Y, M, C, F, K, O, K(split) | YMCFKOK |

### Panel Device IDs (from XML)

| Panel | device_id | Composition | Color |
|-------|-----------|-------------|-------|
| Yellow | 1 | DyeSub | Yellow |
| Magenta | 2 | DyeSub | Magenta |
| Cyan | 3 | DyeSub | Cyan |
| Black (Resin) | 4 | Resin | Black |
| Black (DyeSub) | 5 | DyeSub | Black |
| Overlay | 7 | Overlay | None |
| Fluorescent | 9 | Resin | Fluorescent |
| RW Erase | 11 | ReWritable | None |
| RW Black | 12 | ReWritable | Black |

### Panel Options

| Option | Description | Applied To |
|--------|-------------|------------|
| Split | K panel extraction for text/barcodes | K panels in YMCKO, YMCFKO, etc. |
| Half | Half-panel printing (1.34" length) | Y/M/C in YMCKO_Half |

---

## 12. Card Geometry

### CR80 Card (Standard ID Card)

| Parameter | Microns | Pixels (300 DPI) | Points (72 DPI) |
|-----------|---------|-------------------|-----------------|
| Page Width | 53,975 | ~638 | 152 |
| Page Height | 85,725 | ~1,013 | 242 |
| Imageable Left | 138 | ~2 | - |
| Imageable Right | 53,838 | ~636 | - |
| Imageable Top | 163 | ~2 | - |
| Imageable Bottom | 85,563 | ~1,010 | - |

### CR79 Card

| Parameter | Microns | Pixels (300 DPI) | Points (72 DPI) |
|-----------|---------|-------------------|-----------------|
| Page Width | 52,388 | ~619 | 148 |
| Page Height | 84,138 | ~993 | 238 |

### Printhead Parameters (from XML)

| Parameter | Value | Notes |
|-----------|-------|-------|
| PrintLineWidthStandard | 768 | Pixels per line (printhead width) |
| PrintLineRightOverhang | 5 | Extra pixels past card edge |
| PrintLineWidthExtension | 0 | Additional width extension |
| PrintLineEscByteCount | 2 | Header bytes per decoded line |
| PrintLineExtraByteCount | 1 | Trailer byte per decoded line |
| TopOfPageMargin | 5 | Blank lines before image |
| Resolution X | 300 | DPI horizontal |
| Resolution Y | 300 | DPI vertical |

---

## 13. Job Sequence

### Single-Sided Print (e.g., YMCKO)

```
Step  Marker  Length  Description
----  ------  ------  -----------
1     Fs      0       Session start
      eP
2     Fs      0       Session start (2nd, in some files)
      eP
3     Fs      8       Session setup (8 zeros)
      eP
4     Fg      48      Job config (ribbon=7, width=768, thickness=30)
      eP
5     Fg      512     Y panel data strip #1
      eP
...   Fg      512     Y panel data strips (223 total for YMCKO test)
      eP
6     Fg      8       Inter-panel boundary (Y->M transition)
      eP
7     Fg      512     M panel data strips (227 total)
      eP
...
8     Fg      155     Inter-panel boundary (M->C transition)
      eP
9     Fg      512     C panel data strips (228 total)
      eP
...
10    Fg      39      Inter-panel boundary (C->K transition)
      eP
11    Fg      512     K panel data strips (23 total -- highly compressed)
      eP
...
12    Fg      367     K panel footer / overlay section header
      eP
13    Fg      126     Panel descriptor (overlay + heat settings)
      eP
14    Fg      14      End of job
      eP
```

### Dual-Sided Print (e.g., YMCKOK)

```
Steps 1-3: Session start/setup
Step 4:    Fg config (50 bytes, ribbon=8, dual=1)
           eP
Steps 5-12: Front side panels (Y, M, C, K, O) with inter-panel boundaries
Step 13:   Fg panel descriptor (front side, 126 bytes)
           eP
Steps 14+: Back side K panel data (144 data packets in test file)
Step N:    Fg panel footer (back K, 205 bytes)
           eP
Step N+1:  Fg end of job (14 bytes)
           eP
```

### Command-Only Job (e.g., CleanPrinter)

```
Step 1:  Fg command (32 bytes, cmd=0x0014, flags=0x0008)
         eP
```

No session start/setup is needed for command-only jobs.

---

## 14. PPD Comparison

### Mac PPD (v1.3.2.7) vs Linux PPD (v1.0.0.4)

| Feature | Mac | Linux |
|---------|-----|-------|
| cupsModelNumber | 1 | 5 |
| Filter path | `/usr/libexec/cups/filter/rastertofargo-1.3.2` | `rastertofargo-3.3.4` |
| Ribbon types | 12 options | 13 options (adds Metalic) |
| Rotation | RotateImageFront/Back | RotateFront180/Back180 |
| KPanel naming | KPanelFrontApply/BackApply | KPanelApplyFront/ApplyBack |
| KPanel threshold | (not present) | KPanelResinThreshold |
| Hopper naming | CardHopper | InputHopper |
| Color model | (implied) | Explicit ColorModel option |
| Auto-detect | RibbonAutoDetect, LamAutoDetect | (not present) |
| Visual Security | Area, Type, Orientation options | Area, Type only |
| Sharpness/Contrast/Gamma | 3 separate options | (not present in OpenUI) |

### Shared Ribbon Options

Both PPDs support: YMCKO, YMCKO_Half, YMCKOK, YMCKK, YMCFKO, YMCFKOK, KStandard, KPremium, MonoColor, KO, BO, None

Linux adds: Metalic

### Card Sizes (both PPDs)

- CR80: 152 x 242 points
- CR79: 148 x 238 points

---

## 15. Linux Binary Analysis

### Key Classes (from demangled C++ symbols)

| Class | Purpose |
|-------|---------|
| `SPacket` | Packet builder -- constructs FRL packets, manages output stream |
| `CommandLanguage` | High-level job orchestration -- sends panel data, ribbons, sections |
| `Panel` | Single color panel data (pixels, lines, heat settings) |
| `PanelSet` | Collection of panels for one card side |
| `PanelLine` / `PrintLine` | Single line of pixel data |
| `Ribbon` | Ribbon definition with panel sequence |
| `RibbonSupport` / `ParseXmlRibbon` | XML ribbon parsing |
| `CardSupport` / `ParseXmlCard` | Card size and imageable area |
| `HeatHistory` | Thermal head heat compensation (dye-sub and resin) |
| `DyeSubLinearization` | Dye-sub color linearization curves |
| `WrinkleCompensation` | Anti-wrinkle heat adjustments |
| `ColorCorrection` | Color matching and correction |
| `PrinterModel` | XML model parser (uses Xerces-C++ XML library) |
| `Properties` / `PrintingSettings` | Job configuration properties |

### Key Functions

```
SPacket::writePacket(unsigned char*, int, bool)
SPacket::sendPanelData(...)
SPacket::sendStartOfPrintJob()
SPacket::sendEndofJobSection()
SPacket::sendLaminationSection()
SPacket::sendMagEncodeSection(MagneticEncoder*)
SPacket::flushPacketBuffer()
CommandLanguage::sendPanelSections(int, int)
CommandLanguage::blendPrintLines(int)
CommandLanguage::processRibbon(...)
write_prn(void*, const void*, unsigned long)  -- raw data output
```

### Key Global Variables

```
gEnumRibbonTypeStrings      -- ribbon type name lookup
gEnumPanelColorStrings      -- panel color name lookup
gEnumPanelCompositionStrings -- panel composition name lookup
gEnumPanelOptionStrings     -- panel option name lookup
gEnumCardTypeStrings        -- card type name lookup
abLinearizationTableY/M/C   -- dye-sub linearization curves
```

### Error Messages Found

```
ERROR_END_OF_PANEL
ERROR_PANEL_MEDIA_UNKNOWN
ERROR_PANEL_NOT_FOUND
ERROR_PROP_RIBBON_NOT_FOUND
ERROR_RIBBON_NOT_SUPPORTED
ERROR: rastertofargo job-id user title copies options [file]
ERROR: Failed to open Panel commandSecondFilter pipe
ERROR: Failed to process F-Panel data!
```

### Linked Libraries

- `libcupsimage.so.2` -- CUPS raster reading
- `libcups.so.2` -- CUPS API (PPD parsing, options)
- `libstdc++.so.6` -- C++ runtime
- `libc.so.6` -- Standard C library
- Statically linked: Xerces-C++ 3.x (XML parsing), LittleCMS 2.x (color management)

---

## 16. DTC4500e.xml Summary

### Top-Level Structure

```xml
<printer schemaVersion="1.0">
  <model name="DTC4500e Card Printer">
    <properties>...</properties>
    <media>
      <ribbon_types>...</ribbon_types>
      <card_types>...</card_types>
      <laminate_types>...</laminate_types>
    </media>
    <color>
      <algebraic>
        <coefficient>...</coefficient>
        <curveTable>...</curveTable>
      </algebraic>
    </color>
  </model>
</printer>
```

### Property Categories

| Category | Key Properties |
|----------|---------------|
| BlackThreshold | R=2, G=2, B=2 |
| WhiteLevel | R=253, G=253, B=253 |
| Card | Copies(1-99), Size(CR80), Thickness(30), Hopper(FirstAvailable) |
| DeviceOptions | ResX=300, ResY=300, RibbonType(YMCKO), DualSided, SplitRibbon, InvertFPanel, KPanelApply, YMCunderK, Printmode(Standard), etc. |
| Hidden | PrintLineWidthStandard=768, PrintLineEscByteCount=2, PrintLineExtraByteCount=1, PrintLineRightOverhang=5, CompressionSupport=RLE, TopOfPageMargin=5, EraseHeatValue=255, OverlayHeatValue=128, HeatSealHeatValue=105, Technology=DirectToCard, TestPattern=2039583, PanelTOFSupport=true |
| ImageColor | ColorMatching(None), ResinDither(graphics), DyeSubIntensity(0), Heat offsets, Color balance |
| ImagePosition | Vertical(0), Horizontal(0) |
| ImageTransfer | Temperature(178), DwellTime(23) |
| Lamination | Position(0), DwellTime(2.0), Side(None) |
| MagneticEncoding | Coercivity(2750), 3 tracks with ISO settings |
| WrinkleCompensation | Level=200, PixelCount=96, Support=false |

### Color Curve Table

The XML includes a 256-byte gamma/linearization curve table in hex:
```
000000...01010101...020202...030303...FF
```
This maps input values 0-255 to output values, used for dye-sublimation color correction. The curve is a gentle S-curve with gamma approximately 2.2.

---

## 17. Unknowns and TODOs

### Critical Unknowns

1. **Exact USB endpoints and interface number** -- Need a USB capture from a working printer
2. **Per-line escape bytes** -- The 2 header bytes and 1 trailer byte per decoded line need to be characterized from a capture
3. **Bidirectional communication** -- Unknown if/how the printer sends status back (likely bulk IN endpoint)
4. **Panel descriptor heat data** -- The bytes after the 24-byte common header in panel descriptors need full decoding
5. **Inter-panel boundary packets** -- These variable-length packets between data strip sequences need full characterization

### Secondary Unknowns

6. **Overlay panel data** -- Overlay panels appear to have their data in the panel descriptor itself (no separate 512-byte data strips)
7. **Fluorescent panel layout** -- The 190-byte descriptor for YMCFKO has a different height parameter (94 vs 1011)
8. **Dual-pass printing** -- The XML mentions DualPass option but no test files exercise it
9. **Magnetic encoding** -- The `SPacket::sendMagEncodeSection` function exists but no PRN files contain mag data
10. **Smart card / prox encoding** -- Functions exist but no test data available
11. **Lamination protocol** -- `SPacket::sendLaminationSection` exists but not captured
12. **Encryption** -- `SPacket::sendStartEncryption` function exists; EncryptJobData option in XML
13. **cupsModelNumber differences** -- Mac=1, Linux=5; unclear what protocol changes this implies
14. **Image orientation** -- Both PPDs have rotation options but test files appear to be in a fixed orientation

### Verified Facts

- Packet envelope format (magic + version + length + payload + eP)
- RLE compression algorithm (count+1, value pairs)
- Ribbon type field in config packet (offset 14-15)
- PrintLine width = 768 (offset 22-23 in config)
- Card thickness field (offset 40-41 in config)
- Panel descriptor common header (first 24 bytes)
- End-of-job packet format (always `63 00 00 00 06 00 ...`)
- Command packet format (clean, calibrate ribbon, calibrate lamination)
- All ribbon device_id values (cross-referenced XML with PRN files)
- Complete job sequence structure for single and dual sided
