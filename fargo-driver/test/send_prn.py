#!/usr/bin/env python3
"""
send_prn.py - Send a Fargo PRN file directly to the DTC4500e via USB.

Bypasses CUPS entirely -- use this to test USB connectivity and verify
that the printer accepts raw PRN data before the CUPS filter is complete.

Requirements:
    pip3 install pyusb

USB Protocol (CONFIRMED from live Wireshark capture):
    1. Poll status via vendor control transfers (OUT 0x41 / IN 0xC1)
    2. Send PRN data via Bulk OUT endpoint 0x01
    3. Poll status again after sending

Usage:
    python3 send_prn.py file.prn                  # Send a PRN file
    python3 send_prn.py --list-endpoints           # Show USB endpoints
    python3 send_prn.py --verbose file.prn         # Verbose output
    python3 send_prn.py --dry-run file.prn         # Parse only, no USB
    python3 send_prn.py --status-only              # Just query status
"""

import argparse
import struct
import sys
import time

# Fargo DTC4500e USB identifiers (CONFIRMED from live capture)
FARGO_VID = 0x09B0          # Legacy Fargo Electronics VID
FARGO_PID = 0xBF0C          # DTC4500e PID (CONFIRMED)
HID_VID = 0x076B            # HID Global VID (alternate)
HID_PID_MIN = 0xBF00        # HID Global PID range start
HID_PID_MAX = 0xBFFF        # HID Global PID range end

# Endpoints (CONFIRMED from live capture)
BULK_OUT_EP = 0x01           # Bulk OUT for print data
BULK_IN_EP = 0x81            # Bulk IN (NOT used for status)
USB_TIMEOUT = 5000           # milliseconds

# Vendor control transfer parameters (CONFIRMED from live capture)
CTRL_OUT_REQTYPE = 0x41      # Vendor | Host-to-Device | Interface
CTRL_IN_REQTYPE = 0xC1       # Vendor | Device-to-Host | Interface
CTRL_REQUEST = 0x00
CTRL_VALUE = 0x0000
CTRL_INDEX = 0x0000
CTRL_STATUS_OUT_LEN = 43     # Bytes in status query
CTRL_STATUS_IN_LEN = 520     # Max bytes in status response


def build_status_query():
    """
    Build the 43-byte status query data for the vendor control OUT transfer.

    Format (CONFIRMED from live capture):
        Bytes 0-2:   00 00 00                          (3-byte prefix)
        Bytes 3-8:   46 67 00 00 20 00                 (Fg header: magic="Fg", version=0, len=32)
        Bytes 9-40:  32 zero bytes                     (Fg payload: status request)
        Bytes 41-42: 65 50                             (eP terminator)
    """
    query = bytearray(43)
    # 3-byte prefix (zeros)
    query[0:3] = b'\x00\x00\x00'
    # Fg header: magic="Fg" (0x46 0x67), version=0x0000, length=0x0020 (32)
    query[3:9] = b'\x46\x67\x00\x00\x20\x00'
    # 32 zero bytes payload (already zero from bytearray init)
    # eP terminator
    query[41:43] = b'\x65\x50'
    return bytes(query)


def parse_status_response(data):
    """
    Parse the status response from a vendor control IN transfer.

    The response is an Fg packet (version=0x0000) containing ASCII strings:
        1. Command result: "USR_CMD_SUCCESSFUL"
        2. Status flags: "DH:1;IH:0;LM:0;FL:1;..."
    """
    if len(data) < 6:
        print(f"  Warning: response too short ({len(data)} bytes)")
        return {}

    # Verify Fg magic
    if data[0:2] != b'\x46\x67':
        print(f"  Warning: unexpected magic bytes: {data[0:2].hex()}")
        return {}

    # Extract payload length (bytes 4-5, LE)
    payload_len = struct.unpack_from('<H', data, 4)[0]
    payload = data[6:6 + payload_len]

    # Split on null bytes to get the ASCII strings
    parts = payload.split(b'\x00')
    parts = [p.decode('ascii', errors='replace') for p in parts if p]

    result = {}
    if parts:
        result['command'] = parts[0]

    # Parse the flags string (second part, "DH:1;IH:0;..." format)
    if len(parts) > 1:
        flags_str = parts[1]
        result['flags_raw'] = flags_str
        for pair in flags_str.split(';'):
            if ':' in pair:
                key, val = pair.split(':', 1)
                try:
                    result[key.strip()] = int(val.strip())
                except ValueError:
                    result[key.strip()] = val.strip()

    return result


def find_printer():
    """Find and return the Fargo DTC4500e USB device."""
    try:
        import usb.core
    except ImportError:
        print("ERROR: pyusb not installed. Run: pip3 install pyusb")
        sys.exit(1)

    # Try the confirmed VID/PID first
    dev = usb.core.find(idVendor=FARGO_VID, idProduct=FARGO_PID)
    if dev:
        return dev

    # Try any device on the FARGO VID
    dev = usb.core.find(idVendor=FARGO_VID)
    if dev:
        print(f"  Found Fargo device with alternate PID: 0x{dev.idProduct:04x}")
        return dev

    # Try HID Global VID in the expected range
    for dev in usb.core.find(find_all=True, idVendor=HID_VID):
        if HID_PID_MIN <= dev.idProduct <= HID_PID_MAX:
            print(f"  Found HID Global device: PID=0x{dev.idProduct:04x}")
            return dev

    return None


def list_endpoints():
    """List all USB endpoints for the Fargo printer."""
    import usb.core
    import usb.util

    dev = find_printer()
    if not dev:
        print("ERROR: No Fargo DTC4500e found on USB.")
        print(f"  Looked for VID=0x{FARGO_VID:04x} PID=0x{FARGO_PID:04x}")
        print(f"  Also tried VID=0x{HID_VID:04x} PID range 0x{HID_PID_MIN:04x}-0x{HID_PID_MAX:04x}")
        return

    print(f"Found: VID=0x{dev.idVendor:04x} PID=0x{dev.idProduct:04x}")
    try:
        print(f"  Manufacturer: {dev.manufacturer}")
        print(f"  Product: {dev.product}")
        print(f"  Serial: {dev.serial_number}")
    except Exception:
        print("  (Could not read string descriptors -- may need root)")

    print(f"  Class: 0x{dev.bDeviceClass:02x}")
    print(f"  Num Configurations: {dev.bNumConfigurations}")

    for cfg in dev:
        print(f"\n  Configuration {cfg.bConfigurationValue}:")
        for intf in cfg:
            print(f"    Interface {intf.bInterfaceNumber} "
                  f"Alt {intf.bAlternateSetting} "
                  f"Class=0x{intf.bInterfaceClass:02x}")
            for ep in intf:
                direction = "IN" if usb.util.endpoint_direction(ep.bEndpointAddress) == usb.util.ENDPOINT_IN else "OUT"
                ep_type = {0: "Control", 1: "Isochronous", 2: "Bulk", 3: "Interrupt"}.get(
                    usb.util.endpoint_type(ep.bmAttributes), "Unknown")
                print(f"      EP 0x{ep.bEndpointAddress:02x} {direction} "
                      f"Type={ep_type} MaxPacket={ep.wMaxPacketSize}")


def query_status(dev, verbose=False):
    """
    Query the printer status via vendor control transfers.

    Returns parsed status dict or None on error.
    """
    query_data = build_status_query()

    if verbose:
        print(f"  Sending {len(query_data)}-byte status query...")
        print(f"    Data: {query_data.hex()}")

    try:
        # Step 1: Send status query via vendor control OUT
        sent = dev.ctrl_transfer(
            CTRL_OUT_REQTYPE, CTRL_REQUEST, CTRL_VALUE, CTRL_INDEX,
            query_data, USB_TIMEOUT
        )
        if verbose:
            print(f"  Sent {sent} bytes via control OUT")

        # Step 2: Read status response via vendor control IN
        response = dev.ctrl_transfer(
            CTRL_IN_REQTYPE, CTRL_REQUEST, CTRL_VALUE, CTRL_INDEX,
            CTRL_STATUS_IN_LEN, USB_TIMEOUT
        )
        if verbose:
            print(f"  Received {len(response)} bytes via control IN")
            print(f"    Raw: {bytes(response).hex()}")

        return parse_status_response(bytes(response))

    except Exception as e:
        print(f"  ERROR: Status query failed: {e}")
        return None


def wait_ready(dev, timeout_sec=30, verbose=False):
    """Poll status until printer is ready or timeout."""
    print(f"Waiting for printer (timeout={timeout_sec}s)...")

    for elapsed in range(timeout_sec):
        status = query_status(dev, verbose=(verbose and elapsed == 0))
        if status is None:
            time.sleep(1)
            continue

        cmd = status.get('command', '')
        dh = status.get('DH', 0)
        pe = status.get('PE', 0)

        if 'USR_CMD_SUCCESSFUL' in cmd and dh == 1 and pe == 0:
            print(f"  Printer ready (after {elapsed}s)")
            return True

        if pe == 1:
            print(f"  WARNING: Printer out of cards (PE=1)")
        if dh != 1:
            print(f"  Waiting: door/hood open (DH={dh})")

        time.sleep(1)

    print(f"  TIMEOUT after {timeout_sec}s")
    return False


def parse_prn_packets(data, verbose=False):
    """
    Parse a PRN file into FRL packets for informational display.
    Returns the total number of packets found.
    """
    pos = 0
    pkt_num = 0

    while pos < len(data):
        if pos + 2 > len(data):
            break

        # Check for "eP" standalone (0x65 0x50)
        if data[pos] == 0x65 and data[pos + 1] == 0x50:
            if verbose:
                print(f"  Packet {pkt_num}: eP at offset {pos}")
            pos += 2
            pkt_num += 1
            continue

        # Check for Fs or Fg header (need at least 6 bytes)
        if pos + 6 > len(data):
            break

        magic = data[pos:pos + 2]
        if magic == b'\x46\x73':  # Fs
            marker = "Fs"
        elif magic == b'\x46\x67':  # Fg
            marker = "Fg"
        else:
            # Unknown byte, skip
            pos += 1
            continue

        version = struct.unpack_from('<H', data, pos + 2)[0]
        payload_len = struct.unpack_from('<H', data, pos + 4)[0]

        if verbose:
            print(f"  Packet {pkt_num}: {marker} version={version} "
                  f"payload_len={payload_len} at offset {pos}")

        pkt_num += 1
        pos += 6 + payload_len  # Skip header + payload

    return pkt_num


def send_prn(filename, verbose=False, dry_run=False):
    """Send a PRN file to the printer."""
    import usb.core
    import usb.util

    # Read the PRN file
    try:
        with open(filename, 'rb') as f:
            prn_data = f.read()
    except IOError as e:
        print(f"ERROR: Cannot read '{filename}': {e}")
        return False

    print(f"Loaded {len(prn_data)} bytes from {filename}")

    # Parse packets for info
    pkt_count = parse_prn_packets(prn_data, verbose=verbose)
    print(f"  Found {pkt_count} FRL packets")

    if dry_run:
        print("Dry run -- not sending to printer.")
        return True

    # Find and open the printer
    dev = find_printer()
    if not dev:
        print("ERROR: No Fargo DTC4500e found on USB.")
        return False

    print(f"Found printer: VID=0x{dev.idVendor:04x} PID=0x{dev.idProduct:04x}")

    # Detach kernel driver if active
    try:
        if dev.is_kernel_driver_active(0):
            print("  Detaching kernel driver from interface 0...")
            dev.detach_kernel_driver(0)
    except Exception as e:
        print(f"  Warning: kernel driver detach failed: {e}")

    # Claim interface 0
    try:
        usb.util.claim_interface(dev, 0)
        print("  Claimed interface 0")
    except Exception as e:
        print(f"ERROR: Cannot claim interface 0: {e}")
        print("  Try running with sudo, or check if another driver has the device")
        return False

    try:
        # Poll status before sending
        print("\n--- Pre-send status ---")
        status = query_status(dev, verbose=verbose)
        if status:
            for k, v in sorted(status.items()):
                if k != 'flags_raw':
                    print(f"  {k}: {v}")

        if not wait_ready(dev, timeout_sec=10, verbose=verbose):
            print("WARNING: Printer may not be ready. Sending anyway...")

        # Send the PRN data via bulk OUT
        print(f"\nSending {len(prn_data)} bytes to EP 0x{BULK_OUT_EP:02x}...")
        chunk_size = 65536
        sent = 0
        while sent < len(prn_data):
            chunk = prn_data[sent:sent + chunk_size]
            written = dev.write(BULK_OUT_EP, chunk, USB_TIMEOUT)
            sent += written
            if verbose:
                print(f"  Sent {sent}/{len(prn_data)} bytes")

        print(f"  Complete: {sent} bytes sent")

        # Poll status after sending
        print("\n--- Post-send status ---")
        time.sleep(1)
        status = query_status(dev, verbose=verbose)
        if status:
            for k, v in sorted(status.items()):
                if k != 'flags_raw':
                    print(f"  {k}: {v}")

        return True

    except Exception as e:
        print(f"ERROR: Send failed: {e}")
        return False

    finally:
        try:
            usb.util.release_interface(dev, 0)
        except Exception:
            pass


def status_only(verbose=False):
    """Just query and display printer status."""
    import usb.core
    import usb.util

    dev = find_printer()
    if not dev:
        print("ERROR: No Fargo DTC4500e found on USB.")
        return False

    print(f"Found printer: VID=0x{dev.idVendor:04x} PID=0x{dev.idProduct:04x}")

    try:
        if dev.is_kernel_driver_active(0):
            dev.detach_kernel_driver(0)
    except Exception:
        pass

    try:
        usb.util.claim_interface(dev, 0)
    except Exception as e:
        print(f"ERROR: Cannot claim interface: {e}")
        return False

    try:
        status = query_status(dev, verbose=verbose)
        if status:
            print("\nPrinter Status:")
            for k, v in sorted(status.items()):
                if k != 'flags_raw':
                    print(f"  {k}: {v}")

            # Interpret key flags
            print("\nInterpretation:")
            if 'USR_CMD_SUCCESSFUL' in status.get('command', ''):
                print("  Command: OK")
            print(f"  Door/Hood: {'Closed' if status.get('DH') == 1 else 'OPEN'}")
            print(f"  Input Hopper: {'Has cards' if status.get('IH') == 1 else 'Empty'}")
            print(f"  Card Empty: {'YES - out of cards!' if status.get('PE') == 1 else 'No'}")
            return True
        else:
            print("  Failed to get status")
            return False
    finally:
        try:
            usb.util.release_interface(dev, 0)
        except Exception:
            pass


def main():
    parser = argparse.ArgumentParser(
        description="Send a Fargo PRN file directly to the DTC4500e via USB."
    )
    parser.add_argument('file', nargs='?', help='PRN file to send')
    parser.add_argument('--list-endpoints', action='store_true',
                        help='List all USB endpoints for the printer')
    parser.add_argument('--verbose', '-v', action='store_true',
                        help='Verbose output')
    parser.add_argument('--dry-run', '-n', action='store_true',
                        help='Parse packets without sending to printer')
    parser.add_argument('--status-only', '-s', action='store_true',
                        help='Just query and display printer status')

    args = parser.parse_args()

    if args.list_endpoints:
        list_endpoints()
        return

    if args.status_only:
        success = status_only(verbose=args.verbose)
        sys.exit(0 if success else 1)

    if not args.file:
        parser.print_help()
        print("\nERROR: No PRN file specified.")
        sys.exit(1)

    success = send_prn(args.file, verbose=args.verbose, dry_run=args.dry_run)
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
