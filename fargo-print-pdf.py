#!/usr/bin/env python3
"""
fargo-print-pdf.py — Print a PDF to the Fargo DTC4500e via direct USB.

Converts PDF → raster → FRL (Fargo Raster Language) → USB bulk transfer.
Bypasses CUPS entirely. Uses the same protocol as the test PRN files.

Requirements:
    brew install ghostscript
    pip3 install pyusb pillow

Usage:
    python3 fargo-print-pdf.py <file.pdf>
    python3 fargo-print-pdf.py --ribbon K_STD <file.pdf>   # Black ribbon
    python3 fargo-print-pdf.py --ribbon YMCKO <file.pdf>   # Color ribbon
    python3 fargo-print-pdf.py --dry-run <file.pdf>        # Build PRN, don't send
"""

import argparse
import os
import struct
import subprocess
import sys
import tempfile

# ---------------------------------------------------------------------------
# FRL Protocol Constants
# ---------------------------------------------------------------------------

# Card geometry (CR80 at 300 DPI)
PRINTHEAD_WIDTH = 768       # pixels per line (fixed by hardware)
CARD_HEIGHT = 1009          # lines per panel (confirmed from known-good PRN)

# FRL packet constants
FRL_DATA_PAYLOAD = 512      # each data strip is 512 bytes of RLE data
FRL_VERSION = 0x0001        # version for print data packets

# Ribbon IDs (config packet offset 14-15)
RIBBON_IDS = {
    "K_STD": 1,
    "K_PRM": 2,
    "KO": 5,
    "YMCKO": 7,
    "YMCKOK": 8,
}

# Panel sequences per ribbon type
PANEL_SEQUENCES = {
    "K_STD": [4],               # Black only
    "K_PRM": [4],
    "KO": [4, 7],              # Black + Overlay
    "YMCKO": [1, 2, 3, 4, 7],  # Yellow, Magenta, Cyan, Black, Overlay
}

# USB identifiers
FARGO_VID = 0x09B0
FARGO_PID = 0xBF0C
BULK_OUT_EP = 0x01
USB_TIMEOUT = 5000


# ---------------------------------------------------------------------------
# RLE Compression
# ---------------------------------------------------------------------------

def rle_compress(data: bytes) -> bytes:
    """Compress data using Fargo's byte-pair RLE: (count-1, value) pairs."""
    if not data:
        return b""
    output = bytearray()
    i = 0
    while i < len(data):
        current = data[i]
        run = 1
        # Count consecutive identical bytes, max 256
        while run < 256 and i + run < len(data) and data[i + run] == current:
            run += 1
        output.append(run - 1)  # count-1
        output.append(current)  # value
        i += run
    return bytes(output)


# ---------------------------------------------------------------------------
# FRL Packet Builders
# ---------------------------------------------------------------------------

def build_fs_start() -> bytes:
    """Fs start packet (0-byte payload) + eP terminator."""
    # Fs magic + version 0x0001 + length 0
    return b"Fs" + struct.pack("<HH", FRL_VERSION, 0) + b"eP"


def build_fs_init() -> bytes:
    """Fs init packet (8-byte payload) + eP terminator."""
    return b"Fs" + struct.pack("<HH", FRL_VERSION, 8) + (b"\x00" * 8) + b"eP"


def build_fg_config(ribbon_id: int, thickness: int = 30) -> bytes:
    """Fg config packet (48-byte payload) + eP terminator.

    Field layout CONFIRMED from known-good PRN files:
      [0-3]   uint32 LE  copies (1)
      [4-7]   uint32 LE  card_size (40=CR80)
      [8-9]   uint16 LE  card_height_code (34=CR80)
      [10-11] uint16 LE  card_type (1=CR80)
      [14-15] uint16 LE  ribbon device_id
      [22-23] uint16 LE  print_line_width (768=0x0300)
      [24]    uint8      flag (0x01 observed in all PRN files)
      [40-43] uint32 LE  card_thickness_mils (30=0x1E)
    """
    payload = bytearray(48)
    struct.pack_into("<I", payload, 0, 1)           # copies = 1
    struct.pack_into("<I", payload, 4, 40)          # card_size = 40 (CR80)
    struct.pack_into("<H", payload, 8, 34)          # card height code
    struct.pack_into("<H", payload, 10, 1)          # card type
    struct.pack_into("<H", payload, 14, ribbon_id)  # ribbon device_id
    struct.pack_into("<H", payload, 22, PRINTHEAD_WIDTH)  # print line width = 768
    payload[24] = 0x01                              # flag (observed in all PRN files)
    struct.pack_into("<I", payload, 40, thickness)  # card thickness (mils)
    return b"Fg" + struct.pack("<HH", FRL_VERSION, len(payload)) + bytes(payload) + b"eP"


def build_fg_data(compressed_chunk: bytes) -> bytes:
    """Fg data strip packet (always 512-byte payload) + eP terminator.

    All RLE data strips MUST be exactly 512 bytes. The last strip is
    zero-padded to 512. The printer stops decoding RLE after the expected
    number of decoded bytes (height * 768).
    """
    if len(compressed_chunk) < FRL_DATA_PAYLOAD:
        compressed_chunk = compressed_chunk + b"\x00" * (FRL_DATA_PAYLOAD - len(compressed_chunk))
    elif len(compressed_chunk) > FRL_DATA_PAYLOAD:
        compressed_chunk = compressed_chunk[:FRL_DATA_PAYLOAD]
    return b"Fg" + struct.pack("<HH", FRL_VERSION, FRL_DATA_PAYLOAD) + compressed_chunk + b"eP"


def build_fg_panel_footer_k() -> bytes:
    """Fg panel footer for K (black resin) panels — 82-byte payload + eP.

    This fixed-size packet is REQUIRED after all RLE data strips and before
    the EOJ. The printer uses the non-512 size (82 bytes) to distinguish it
    from data strips. Content is identical across all K-ribbon PRN files.
    """
    payload = bytes.fromhex(
        "020080ff020080ff020080ff020080ff020080ff"
        "270080ff270012ff06001000000000000000"
        "01007e0013ff7f007f007f007f00"
        "520012ff06001000000000000000"
        "15007e007fff7fff7fff7fff79ff0600"
    )
    return b"Fg" + struct.pack("<HH", FRL_VERSION, len(payload)) + payload + b"eP"


def build_fg_eoj() -> bytes:
    """Fg end-of-job packet (14-byte payload) + eP terminator."""
    payload = bytearray(14)
    struct.pack_into("<H", payload, 0, 0x0063)  # EOJ marker
    struct.pack_into("<H", payload, 4, 0x0006)  # section type
    return b"Fg" + struct.pack("<HH", FRL_VERSION, 14) + bytes(payload) + b"eP"


# ---------------------------------------------------------------------------
# Image Processing
# ---------------------------------------------------------------------------

def pdf_to_grayscale(pdf_path: str, width: int, height: int) -> bytes:
    """Convert PDF to raw grayscale pixels using Ghostscript.

    Returns width*height bytes, one byte per pixel (0=white, 255=black).
    """
    with tempfile.NamedTemporaryFile(suffix=".pgm", delete=False) as tmp:
        pgm_path = tmp.name

    try:
        # Convert PDF to PGM (portable graymap) at 300 DPI
        # We specify exact pixel dimensions to match card size
        cmd = [
            "gs", "-q", "-dNOPAUSE", "-dBATCH", "-dSAFER",
            "-sDEVICE=pgmraw",
            "-r300",
            f"-g{width}x{height}",    # exact pixel dimensions
            "-dFIXEDMEDIA",
            "-dPDFFitPage",            # scale PDF to fit the card
            f"-sOutputFile={pgm_path}",
            pdf_path,
        ]
        result = subprocess.run(cmd, capture_output=True, timeout=30)
        if result.returncode != 0:
            print(f"ERROR: Ghostscript failed: {result.stderr.decode()}", file=sys.stderr)
            return None

        # Parse PGM file (format: P5\nwidth height\nmaxval\n<raw bytes>)
        with open(pgm_path, "rb") as f:
            magic = f.readline().strip()
            if magic != b"P5":
                print(f"ERROR: Unexpected PGM magic: {magic}", file=sys.stderr)
                return None
            # Skip comment lines
            line = f.readline()
            while line.startswith(b"#"):
                line = f.readline()
            dims = line.strip().split()
            img_w, img_h = int(dims[0]), int(dims[1])
            maxval = int(f.readline().strip())
            pixels = f.read()

        if len(pixels) != img_w * img_h:
            print(f"ERROR: PGM size mismatch: expected {img_w * img_h}, got {len(pixels)}", file=sys.stderr)
            return None

        # PGM: 0=black, 255=white. Fargo K panel: 0x00=no ink, 0xFF=full ink.
        # So we need to invert: white areas → no ink, dark areas → ink.
        inverted = bytes(255 - b for b in pixels)
        return inverted

    finally:
        try:
            os.unlink(pgm_path)
        except OSError:
            pass


def pdf_to_color_planes(pdf_path: str, width: int, height: int):
    """Convert PDF to YMCKO color planes.

    Returns dict of panel_id → bytes (one byte per pixel, 0=no ink, 255=full).
    """
    with tempfile.NamedTemporaryFile(suffix=".ppm", delete=False) as tmp:
        ppm_path = tmp.name

    try:
        # Convert PDF to PPM (color) at 300 DPI
        cmd = [
            "gs", "-q", "-dNOPAUSE", "-dBATCH", "-dSAFER",
            "-sDEVICE=ppmraw",
            "-r300",
            f"-g{width}x{height}",
            "-dFIXEDMEDIA",
            "-dPDFFitPage",
            f"-sOutputFile={ppm_path}",
            pdf_path,
        ]
        result = subprocess.run(cmd, capture_output=True, timeout=30)
        if result.returncode != 0:
            print(f"ERROR: Ghostscript failed: {result.stderr.decode()}", file=sys.stderr)
            return None

        # Parse PPM (P6 format: RGB)
        with open(ppm_path, "rb") as f:
            magic = f.readline().strip()
            if magic != b"P6":
                print(f"ERROR: Unexpected PPM magic: {magic}", file=sys.stderr)
                return None
            line = f.readline()
            while line.startswith(b"#"):
                line = f.readline()
            dims = line.strip().split()
            img_w, img_h = int(dims[0]), int(dims[1])
            _maxval = int(f.readline().strip())
            rgb_data = f.read()

        if len(rgb_data) != img_w * img_h * 3:
            print(f"ERROR: PPM size mismatch", file=sys.stderr)
            return None

        # Convert RGB to YMCK planes
        # Y = 255 - Blue,  M = 255 - Green,  C = 255 - Red,  K = min(C,M,Y)
        # Under-color removal: subtract K from C, M, Y
        num_pixels = img_w * img_h
        y_plane = bytearray(num_pixels)
        m_plane = bytearray(num_pixels)
        c_plane = bytearray(num_pixels)
        k_plane = bytearray(num_pixels)
        o_plane = bytearray(num_pixels)

        for i in range(num_pixels):
            r = rgb_data[i * 3]
            g = rgb_data[i * 3 + 1]
            b = rgb_data[i * 3 + 2]

            # CMY from RGB (inverted)
            ci = 255 - r
            mi = 255 - g
            yi = 255 - b

            # Under-color removal
            k = min(ci, mi, yi)
            ci = max(0, ci - k)
            mi = max(0, mi - k)
            yi = max(0, yi - k)

            y_plane[i] = yi
            m_plane[i] = mi
            c_plane[i] = ci
            k_plane[i] = k
            # Overlay: full coverage everywhere there's any ink or content
            o_plane[i] = 0xFF if (r < 255 or g < 255 or b < 255) else 0x00

        return {
            1: bytes(y_plane),   # Yellow
            2: bytes(m_plane),   # Magenta
            3: bytes(c_plane),   # Cyan
            4: bytes(k_plane),   # Black
            7: bytes(o_plane),   # Overlay
        }

    finally:
        try:
            os.unlink(ppm_path)
        except OSError:
            pass


# ---------------------------------------------------------------------------
# FRL Job Builder
# ---------------------------------------------------------------------------

def build_panel_descriptor(panel_id: int, panel_seq: int, height: int) -> bytes:
    """Build the 40-byte panel descriptor that starts the first data strip.

    Fields confirmed from known-good PRN analysis:
      [0-3]   uint32 LE  type = 15 (0x0f)
      [4-7]   uint32 LE  sub_type = 22 (0x16)
      [8-11]  uint32 LE  block_type = 16 (0x10)
      [12-13] uint16 LE  panel sequence number (1-based)
      [14-15] uint16 LE  panel type (4=K, 1=Y, 2=M, 3=C, 7=O)
      [16-19] zeros
      [20-21] uint16 LE  height in lines (1009)
      [22-23] uint16 LE  0x1034 (unknown constant)
      [24-27] uint32 LE  4 (unknown)
      [28-31] uint32 LE  0x00100000 (unknown)
      [32-35] zeros
      [36-39] uint32 LE  0x00120000 (unknown)
    """
    desc = bytearray(40)
    struct.pack_into("<I", desc, 0, 15)           # type
    struct.pack_into("<I", desc, 4, 22)           # sub_type
    struct.pack_into("<I", desc, 8, 16)           # block_type
    struct.pack_into("<H", desc, 12, panel_seq)   # panel sequence (1-based)
    struct.pack_into("<H", desc, 14, panel_id)    # panel type
    struct.pack_into("<H", desc, 20, height)      # height in lines
    struct.pack_into("<H", desc, 22, 0x1034)      # unknown constant
    struct.pack_into("<I", desc, 24, 4)           # unknown
    struct.pack_into("<I", desc, 28, 0x00100000)  # unknown
    struct.pack_into("<I", desc, 36, 0x00120000)  # unknown
    return bytes(desc)


def build_panel_data(pixel_data: bytes, width: int, height: int,
                     panel_id: int, panel_seq: int) -> bytes:
    """Convert a single panel's pixel data into RLE-compressed FRL data strips.

    pixel_data: width*height bytes, one byte per pixel.
    The first 512-byte strip contains a 40-byte panel descriptor + 472 bytes RLE.
    Subsequent strips are 512 bytes of pure RLE.
    The final strip uses its actual length (no zero-padding).
    Returns concatenated Fg data strip packets.
    """
    output = bytearray()

    # Build raw line data — 768 raw pixels per line, no escape/trailer bytes
    raw_lines = bytearray()
    for y in range(height):
        row_start = y * width
        row_pixels = pixel_data[row_start:row_start + width]

        # Pad or truncate to printhead width (768)
        if len(row_pixels) < PRINTHEAD_WIDTH:
            row_pixels = row_pixels + b"\x00" * (PRINTHEAD_WIDTH - len(row_pixels))
        else:
            row_pixels = row_pixels[:PRINTHEAD_WIDTH]

        raw_lines.extend(row_pixels)

    # RLE compress the entire panel
    compressed = rle_compress(bytes(raw_lines))

    # Build the 40-byte panel descriptor for the first strip
    descriptor = build_panel_descriptor(panel_id, panel_seq, height)

    # First strip: 40 bytes descriptor + up to 472 bytes RLE = 512 total
    first_rle_size = FRL_DATA_PAYLOAD - len(descriptor)  # 472
    first_chunk = descriptor + compressed[:first_rle_size]
    output.extend(build_fg_data(first_chunk))

    # Remaining strips: 512 bytes each, last strip uses actual length
    offset = first_rle_size
    while offset < len(compressed):
        chunk = compressed[offset:offset + FRL_DATA_PAYLOAD]
        output.extend(build_fg_data(chunk))
        offset += FRL_DATA_PAYLOAD

    return bytes(output)


def build_frl_job(pdf_path: str, ribbon: str = "K_STD") -> bytes:
    """Build a complete FRL print job from a PDF file.

    Returns the raw bytes ready to send via USB bulk OUT.
    """
    ribbon_id = RIBBON_IDS.get(ribbon, 1)
    panels = PANEL_SEQUENCES.get(ribbon, [4])  # default to K

    width = PRINTHEAD_WIDTH  # 768 pixels wide
    height = CARD_HEIGHT     # 1009 lines tall

    print(f"  Ribbon: {ribbon} (id={ribbon_id})")
    print(f"  Panels: {len(panels)}")
    print(f"  Card: {width}x{height} pixels @ 300 DPI")

    # Step 1: Rasterize the PDF
    if len(panels) == 1 and panels[0] == 4:
        # Black-only: use grayscale conversion
        print("  Converting PDF to grayscale...")
        pixel_data = pdf_to_grayscale(pdf_path, width, height)
        if pixel_data is None:
            return None
        panel_data = {4: pixel_data}
    else:
        # Color: use YMCKO conversion
        print("  Converting PDF to YMCKO color planes...")
        panel_data = pdf_to_color_planes(pdf_path, width, height)
        if panel_data is None:
            return None

    # Step 2: Build FRL job
    job = bytearray()

    # Preamble: Fs start + Fs init + Fg config
    print("  Building FRL job...")
    job.extend(build_fs_start())
    job.extend(build_fs_init())
    job.extend(build_fg_config(ribbon_id))

    # Image data: one panel at a time
    for panel_seq, panel_id in enumerate(panels, start=1):
        pdata = panel_data.get(panel_id)
        if pdata is None:
            # Skip panels we don't have data for
            print(f"  WARNING: No data for panel {panel_id}, sending blank")
            pdata = b"\x00" * (width * height)

        print(f"  Encoding panel {panel_id} (seq={panel_seq})...", end=" ", flush=True)
        panel_bytes = build_panel_data(pdata, width, height, panel_id, panel_seq)
        strip_count = panel_bytes.count(b"Fg")
        print(f"{strip_count} strips, {len(panel_bytes)} bytes")
        job.extend(panel_bytes)

    # Panel footer (required for K panels — signals end of image data)
    # TODO: Add footers for other panel types (YMCKO) when needed
    job.extend(build_fg_panel_footer_k())

    # End of job
    job.extend(build_fg_eoj())

    print(f"  Total job: {len(job)} bytes")
    return bytes(job)


# ---------------------------------------------------------------------------
# USB Send
# ---------------------------------------------------------------------------

def send_to_printer(data: bytes) -> bool:
    """Send FRL data to the Fargo DTC4500e via USB bulk OUT."""
    try:
        import usb.core
        import usb.util
    except ImportError:
        print("ERROR: pyusb not installed. Run: pip3 install pyusb")
        return False

    # Find printer
    dev = usb.core.find(idVendor=FARGO_VID, idProduct=FARGO_PID)
    if not dev:
        print(f"ERROR: No Fargo printer found (VID=0x{FARGO_VID:04x} PID=0x{FARGO_PID:04x})")
        return False

    print(f"  Found printer: VID=0x{dev.idVendor:04x} PID=0x{dev.idProduct:04x}")

    # Detach kernel driver (may fail, that's OK)
    try:
        if dev.is_kernel_driver_active(0):
            dev.detach_kernel_driver(0)
    except Exception:
        pass  # Normal on macOS

    # Claim interface
    try:
        usb.util.claim_interface(dev, 0)
    except Exception as e:
        print(f"  WARNING: Cannot claim interface: {e}")
        # Continue anyway — may still work on macOS

    try:
        # Send the entire job via bulk OUT
        print(f"  Sending {len(data)} bytes via bulk OUT...")
        chunk_size = 65536
        sent = 0
        while sent < len(data):
            chunk = data[sent:sent + chunk_size]
            written = dev.write(BULK_OUT_EP, chunk, USB_TIMEOUT)
            sent += written

        print(f"  Complete: {sent} bytes sent")
        return True

    except Exception as e:
        print(f"  ERROR: Send failed: {e}")
        return False

    finally:
        try:
            usb.util.release_interface(dev, 0)
        except Exception:
            pass


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Print a PDF to the Fargo DTC4500e via direct USB (no CUPS)")
    parser.add_argument("pdf", help="PDF file to print")
    parser.add_argument("--ribbon", default="K_STD",
                        choices=list(RIBBON_IDS.keys()),
                        help="Ribbon type (default: K_STD)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Build the PRN file but don't send to printer")
    parser.add_argument("--save-prn", metavar="FILE",
                        help="Save the generated PRN file to disk")
    args = parser.parse_args()

    if not os.path.exists(args.pdf):
        print(f"ERROR: File not found: {args.pdf}")
        sys.exit(1)

    # Check for Ghostscript
    try:
        subprocess.run(["gs", "--version"], capture_output=True, check=True)
    except (FileNotFoundError, subprocess.CalledProcessError):
        print("ERROR: Ghostscript not installed. Run: brew install ghostscript")
        sys.exit(1)

    print(f"Processing: {args.pdf}")
    job_data = build_frl_job(args.pdf, ribbon=args.ribbon)
    if job_data is None:
        print("ERROR: Failed to build FRL job")
        sys.exit(1)

    # Save PRN if requested
    if args.save_prn:
        with open(args.save_prn, "wb") as f:
            f.write(job_data)
        print(f"Saved PRN to: {args.save_prn}")

    if args.dry_run:
        print("Dry run — not sending to printer.")
        sys.exit(0)

    # Send to printer
    print("\nSending to printer...")
    if send_to_printer(job_data):
        print("\nPrint job sent successfully!")
        sys.exit(0)
    else:
        print("\nPrint job failed.")
        sys.exit(1)


if __name__ == "__main__":
    main()
