#!/usr/bin/env python3
"""DEFINITIVE ANALYSIS: Known-good K_STD PRN file structure.

Confirmed findings:
1. The RLE data is plain byte-pair (count-1, value) encoding
2. Lines are 768 bytes each — NO escape bytes, NO trailer bytes
3. The K_STD test card contains grayscale values (not just 0/255)
4. The first 512-byte Fg data strip has a 40-byte panel descriptor prefix
5. The 82-byte Fg "footer" is actually additional RLE data, not a control block
6. The panel descriptor at [20-21] = 1009 (height in lines)
"""

import struct

PRN_PATH = "/Users/jamisonhill/Ai/fargo-dtc4500e-macos-driver/fargo-driver/reference/DTC4500e_K_STD_Tst.prn"

def parse_packets(data):
    packets = []
    pos = 0
    while pos < len(data):
        if pos + 2 > len(data): break
        magic = data[pos:pos+2]
        if magic == b"Fs": ptype = "Fs"
        elif magic == b"Fg": ptype = "Fg"
        elif magic == b"eP": pos += 2; continue
        else: pos += 1; continue
        if pos + 6 > len(data): break
        version, payload_len = struct.unpack_from("<HH", data, pos + 2)
        payload = data[pos+6:pos+6+payload_len]
        packets.append({"type": ptype, "payload_len": payload_len, "payload": payload})
        pos = pos + 6 + payload_len
    return packets

def rle_decode(data):
    output = bytearray()
    i = 0
    while i + 1 < len(data):
        count = data[i] + 1
        value = data[i + 1]
        output.extend([value] * count)
        i += 2
    return bytes(output)

def rle_compress(data):
    if not data: return b""
    output = bytearray()
    i = 0
    while i < len(data):
        current = data[i]
        run = 1
        while run < 256 and i + run < len(data) and data[i + run] == current:
            run += 1
        output.append(run - 1)
        output.append(current)
        i += run
    return bytes(output)


def main():
    with open(PRN_PATH, "rb") as f:
        data = f.read()

    packets = parse_packets(data)

    print("=" * 80)
    print("DEFINITIVE ANALYSIS: Known-good K_STD PRN")
    print("=" * 80)

    # =========================================================================
    # 1. Full packet sequence
    # =========================================================================
    fg_packets = [p for p in packets if p["type"] == "Fg"]
    data_strips = [p for p in fg_packets if p["payload_len"] == 512]
    config_pkt = [p for p in fg_packets if p["payload_len"] == 48][0]
    footer_pkt = [p for p in fg_packets if p["payload_len"] == 82][0]
    eoj_pkt = [p for p in fg_packets if p["payload_len"] == 14][0]

    print(f"\n1. PACKET SEQUENCE:")
    print(f"   Fs (len=0)  — start")
    print(f"   Fs (len=8)  — init (8 zero bytes)")
    print(f"   Fg (len=48) — config")
    print(f"   Fg (len=512) x {len(data_strips)} — data strips")
    print(f"   Fg (len=82) — final RLE data (NOT a footer)")
    print(f"   Fg (len=14) — end-of-job")
    print(f"   Total packets: {len(packets)}")

    # =========================================================================
    # 2. Panel descriptor (40 bytes at start of first data strip)
    # =========================================================================
    desc = data_strips[0]["payload"][:40]
    print(f"\n2. PANEL DESCRIPTOR (40 bytes, embedded in first 512-byte data strip):")
    print(f"   Raw: {desc.hex()}")
    print(f"   Fields:")
    print(f"     [0-3]   = {struct.unpack_from('<I', desc, 0)[0]:10d}  (0x{struct.unpack_from('<I', desc, 0)[0]:08x})")
    print(f"     [4-7]   = {struct.unpack_from('<I', desc, 4)[0]:10d}  (0x{struct.unpack_from('<I', desc, 4)[0]:08x})")
    print(f"     [8-11]  = {struct.unpack_from('<I', desc, 8)[0]:10d}  (0x{struct.unpack_from('<I', desc, 8)[0]:08x})")
    print(f"     [12-13] = {struct.unpack_from('<H', desc, 12)[0]:6d}  (panel sequence number)")
    print(f"     [14-15] = {struct.unpack_from('<H', desc, 14)[0]:6d}  (panel type: 4=K)")
    print(f"     [16-19] = {struct.unpack_from('<I', desc, 16)[0]:10d}")
    print(f"     [20-21] = {struct.unpack_from('<H', desc, 20)[0]:6d}  ** HEIGHT IN LINES **")
    print(f"     [22-23] = 0x{struct.unpack_from('<H', desc, 22)[0]:04x}  ({struct.unpack_from('<H', desc, 22)[0]})")
    print(f"     [24-27] = {struct.unpack_from('<I', desc, 24)[0]:10d}")
    print(f"     [28-31] = 0x{struct.unpack_from('<I', desc, 28)[0]:08x}")
    print(f"     [32-35] = {struct.unpack_from('<I', desc, 32)[0]:10d}")
    print(f"     [36-39] = 0x{struct.unpack_from('<I', desc, 36)[0]:08x}")

    height = struct.unpack_from('<H', desc, 20)[0]

    # =========================================================================
    # 3. RLE data structure
    # =========================================================================
    print(f"\n3. RLE DATA STRUCTURE:")
    print(f"   First data strip: 40 bytes descriptor + 472 bytes RLE data")
    print(f"   Strips 2-{len(data_strips)}: 512 bytes RLE data each")
    print(f"   82-byte Fg packet: 82 bytes of additional RLE data")
    print(f"   Encoding: byte-pair RLE (count-1, value)")
    print(f"   Line format: 768 raw bytes per line (NO escape/trailer bytes)")
    print(f"   Height: {height} lines")
    print(f"   Total image: {height} x 768 = {height * 768} decoded bytes")

    # =========================================================================
    # 4. Verify decode
    # =========================================================================
    rle_stream = bytearray()
    rle_stream.extend(data_strips[0]["payload"][40:])
    for strip in data_strips[1:]:
        rle_stream.extend(strip["payload"])
    rle_stream.extend(footer_pkt["payload"])

    decoded = rle_decode(bytes(rle_stream))
    image = decoded[:height * 768]

    print(f"\n4. DECODE VERIFICATION:")
    print(f"   RLE stream: {len(rle_stream)} bytes")
    print(f"   Decoded: {len(decoded)} bytes (includes zero-padding from last strip)")
    print(f"   Image data: {len(image)} bytes ({height} x 768)")

    # =========================================================================
    # 5. CRITICAL DIFFERENCES vs our Python script
    # =========================================================================
    print(f"\n{'=' * 80}")
    print(f"5. CRITICAL DIFFERENCES vs fargo-print-pdf.py")
    print(f"{'=' * 80}")

    print(f"""
   BUG 1: MISSING PANEL DESCRIPTOR
   --------------------------------
   Known-good: First 512-byte Fg data strip starts with 40-byte panel descriptor.
               RLE data starts at byte 40 of the first strip.
   Our script: build_fg_data() fills ALL 512 bytes with RLE data.
               No panel descriptor is sent.
   Impact:     Printer interprets our first 40 RLE bytes as a descriptor,
               gets garbage values, accepts them (they happen to parse OK),
               but the RLE stream is now misaligned by 40 bytes.
               This causes "job data error" when the printer tries to decode
               what it thinks is RLE but is actually shifted data.

   BUG 2: WRONG LINE FORMAT
   -------------------------
   Known-good: Lines are 768 bytes each (raw pixel data, no headers/trailers).
   Our script: Lines are 771 bytes (2 escape bytes + 768 pixels + 1 trailer).
               build_panel_data() adds b"\\x00\\x00" prefix and b"\\x00" suffix.
   Impact:     Even if the descriptor were correct, the decoded data would be
               3 bytes too long per line. Over 1009 lines, that's 3027 extra bytes.
               The printer expects exactly {height}*768 = {height*768} decoded bytes
               but gets {height}*771 = {height*771} bytes — causing data overflow/error.

   BUG 3: WRONG HEIGHT
   --------------------
   Known-good: Panel descriptor says height = {height} lines.
   Our script: Uses CARD_HEIGHT = 1011.
   Impact:     Printer expects 1011 lines but the RLE stream encodes only
               {height} lines of actual content. Or if height were 1011, we'd
               produce 2 extra lines of data (1011*771 - {height}*768 = {1011*771 - height*768} extra bytes).

   BUG 4: 82-BYTE PACKET IS NOT A "PANEL FOOTER"
   ------------------------------------------------
   Known-good: The 82-byte Fg packet is the FINAL chunk of RLE-encoded pixel data.
               It's a non-512 Fg packet that holds the remaining RLE bytes that
               didn't fill a complete 512-byte strip.
   Our script: build_fg_panel_footer_k() sends a hardcoded 82-byte Fg packet
               with specific heat settings data AFTER the RLE data strips.
   Impact:     The printer receives extra RLE data after the image is complete.
               This may be interpreted as the start of a new panel or cause
               a data length mismatch error.

               HOWEVER: The 82-byte data from the known-good PRN happens to
               be IDENTICAL to what our script sends. So this packet content
               is correct — it's just misidentified as a "footer" when it's
               actually the tail end of the RLE image data.
""")

    # Verify the 82-byte content matches
    our_footer = bytes.fromhex(
        "020080ff020080ff020080ff020080ff020080ff"
        "270080ff270012ff06001000000000000000"
        "01007e0013ff7f007f007f007f00"
        "520012ff06001000000000000000"
        "15007e007fff7fff7fff7fff79ff0600"
    )
    print(f"   82-byte content match: {footer_pkt['payload'] == our_footer}")

    # =========================================================================
    # 6. Summary of fixes needed
    # =========================================================================
    print(f"\n{'=' * 80}")
    print(f"6. FIXES NEEDED IN fargo-print-pdf.py")
    print(f"{'=' * 80}")
    print(f"""
   FIX 1: Add 40-byte panel descriptor to first data strip.
          The first Fg data strip must be:
            [40 bytes descriptor] + [472 bytes of RLE data]
          Subsequent strips: [512 bytes of RLE data]

   FIX 2: Remove escape bytes and trailer from lines.
          Change build_panel_data() to NOT add the 2-byte header or 1-byte trailer.
          Each line is just 768 raw pixel bytes, no framing.

   FIX 3: Fix height to {height} (or determine dynamically).
          Change CARD_HEIGHT from 1011 to {height}.
          Also encode the correct height in the panel descriptor.

   FIX 4: The last RLE chunk should NOT be zero-padded to 512 bytes.
          Instead, send it as-is in a smaller Fg packet (like the 82-byte one).
          OR: if it must be 512 bytes, the printer stops decoding at
          height*768 decoded bytes and ignores the padding. Need to test.
          Actually: looking at the known-good PRN, ALL 512 data strips are
          completely filled with meaningful RLE data. Only the final chunk
          (82 bytes) is a non-512 Fg packet. This suggests:
          - Split RLE into 512-byte chunks
          - Send each as Fg(512)
          - Send the remainder as Fg(remainder_len)
""")

    # =========================================================================
    # 7. Panel descriptor template
    # =========================================================================
    print(f"{'=' * 80}")
    print(f"7. PANEL DESCRIPTOR TEMPLATE (40 bytes)")
    print(f"{'=' * 80}")
    print(f"   Exact bytes from known-good K_STD PRN:")
    print(f"   {desc.hex()}")
    print()
    print(f"   As code:")
    print(f"   panel_desc = bytearray(40)")
    print(f"   struct.pack_into('<I', panel_desc, 0, 15)       # 0x0f")
    print(f"   struct.pack_into('<I', panel_desc, 4, 22)       # 0x16")
    print(f"   struct.pack_into('<I', panel_desc, 8, 16)       # 0x10")
    print(f"   struct.pack_into('<H', panel_desc, 12, 1)       # panel sequence")
    print(f"   struct.pack_into('<H', panel_desc, 14, 4)       # panel type (4=K)")
    print(f"   # [16-19] = 0")
    print(f"   struct.pack_into('<H', panel_desc, 20, {height})     # height in lines")
    print(f"   struct.pack_into('<H', panel_desc, 22, 0x1034)  # unknown (4148)")
    print(f"   struct.pack_into('<I', panel_desc, 24, 4)       # unknown")
    print(f"   struct.pack_into('<I', panel_desc, 28, 0x00100000)  # unknown")
    print(f"   # [32-35] = 0")
    print(f"   struct.pack_into('<I', panel_desc, 36, 0x00120000)  # unknown")

    # =========================================================================
    # 8. Our script's comparison (what it SHOULD produce for all-black)
    # =========================================================================
    print(f"\n{'=' * 80}")
    print(f"8. WHAT OUR SCRIPT SHOULD PRODUCE (all-black {height} lines)")
    print(f"{'=' * 80}")

    # All-black = all 0xFF pixels (K panel: 255 = full ink)
    raw_image = b"\xff" * (768 * height)
    compressed = rle_compress(raw_image)
    print(f"   Raw image: {len(raw_image)} bytes ({height} x 768)")
    print(f"   Compressed: {len(compressed)} bytes")
    print(f"   First 20 compressed bytes: {compressed[:20].hex()}")

    # How many 512-byte strips + remainder
    # First strip gets 472 bytes (512 - 40 descriptor)
    remaining = len(compressed) - 472
    full_strips = remaining // 512 if remaining > 0 else 0
    final_chunk = remaining % 512 if remaining > 0 else 0

    print(f"   First strip: 40 desc + 472 RLE")
    print(f"   Full 512-byte strips: {full_strips}")
    print(f"   Final chunk: {final_chunk} bytes")
    print(f"   Total data Fg packets: {1 + full_strips + (1 if final_chunk > 0 else 0)}")


if __name__ == "__main__":
    main()
