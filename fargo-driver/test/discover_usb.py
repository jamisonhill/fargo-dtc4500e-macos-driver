#!/usr/bin/env python3
"""
discover_usb.py - Scan for Fargo DTC4500e printers on USB.

Searches for devices under both VIDs:
    - 0x09B0 (legacy Fargo Electronics)
    - 0x076B (HID Global, post-acquisition)

Prints complete USB descriptor information including all endpoints,
serial number, and interface details.

Requirements:
    pip3 install pyusb

Usage:
    python3 discover_usb.py             # Scan and display info
    python3 discover_usb.py --all       # Show ALL USB devices (not just Fargo)
"""

import argparse
import sys

# Known Fargo VID/PID values
FARGO_VID = 0x09B0
FARGO_PID = 0xBF0C      # CONFIRMED DTC4500e PID
HID_VID = 0x076B
HID_PID_MIN = 0xBF00
HID_PID_MAX = 0xBFFF


def is_fargo_device(vid, pid):
    """Check if a VID/PID pair could be a Fargo DTC4500e."""
    if vid == FARGO_VID:
        return True
    if vid == HID_VID and HID_PID_MIN <= pid <= HID_PID_MAX:
        return True
    return False


def print_device_info(dev, indent=""):
    """Print detailed USB descriptor info for a device."""
    import usb.util

    print(f"{indent}VID: 0x{dev.idVendor:04X}")
    print(f"{indent}PID: 0x{dev.idProduct:04X}")
    print(f"{indent}Bus: {dev.bus}")
    print(f"{indent}Address: {dev.address}")
    print(f"{indent}Device Class: 0x{dev.bDeviceClass:02X}")
    print(f"{indent}Device SubClass: 0x{dev.bDeviceSubClass:02X}")
    print(f"{indent}Device Protocol: 0x{dev.bDeviceProtocol:02X}")
    print(f"{indent}Max Packet Size (EP0): {dev.bMaxPacketSize0}")
    print(f"{indent}USB Version: {dev.bcdUSB >> 8}.{(dev.bcdUSB >> 4) & 0xF}{dev.bcdUSB & 0xF}")
    print(f"{indent}Num Configurations: {dev.bNumConfigurations}")

    # Try to read string descriptors (may require root on macOS)
    try:
        mfr = dev.manufacturer
        print(f"{indent}Manufacturer: {mfr}")
    except Exception:
        print(f"{indent}Manufacturer: (unable to read -- try sudo)")

    try:
        prod = dev.product
        print(f"{indent}Product: {prod}")
    except Exception:
        print(f"{indent}Product: (unable to read -- try sudo)")

    try:
        serial = dev.serial_number
        print(f"{indent}Serial Number: {serial}")
    except Exception:
        print(f"{indent}Serial Number: (unable to read -- try sudo)")

    # Enumerate configurations, interfaces, and endpoints
    for cfg in dev:
        print(f"\n{indent}Configuration {cfg.bConfigurationValue}:")
        print(f"{indent}  Total Power: {cfg.bMaxPower * 2}mA")
        print(f"{indent}  Num Interfaces: {cfg.bNumInterfaces}")

        for intf in cfg:
            cls_name = {
                0x01: "Audio",
                0x02: "CDC",
                0x03: "HID",
                0x07: "Printer",
                0x08: "Mass Storage",
                0x09: "Hub",
                0x0B: "Smart Card",
                0xFF: "Vendor Specific",
            }.get(intf.bInterfaceClass, f"0x{intf.bInterfaceClass:02X}")

            print(f"\n{indent}  Interface {intf.bInterfaceNumber} "
                  f"(Alt {intf.bAlternateSetting}):")
            print(f"{indent}    Class: {cls_name} (0x{intf.bInterfaceClass:02X})")
            print(f"{indent}    SubClass: 0x{intf.bInterfaceSubClass:02X}")
            print(f"{indent}    Protocol: 0x{intf.bInterfaceProtocol:02X}")
            print(f"{indent}    Num Endpoints: {intf.bNumEndpoints}")

            for ep in intf:
                direction = "IN" if usb.util.endpoint_direction(ep.bEndpointAddress) == usb.util.ENDPOINT_IN else "OUT"
                ep_type_map = {
                    0: "Control",
                    1: "Isochronous",
                    2: "Bulk",
                    3: "Interrupt",
                }
                ep_type = ep_type_map.get(
                    usb.util.endpoint_type(ep.bmAttributes), "Unknown")

                print(f"{indent}    Endpoint 0x{ep.bEndpointAddress:02X}:")
                print(f"{indent}      Direction: {direction}")
                print(f"{indent}      Type: {ep_type}")
                print(f"{indent}      Max Packet Size: {ep.wMaxPacketSize}")
                print(f"{indent}      Interval: {ep.bInterval}")


def scan_fargo(show_all=False):
    """Scan USB for Fargo devices and print info."""
    try:
        import usb.core
        import usb.util
    except ImportError:
        print("ERROR: pyusb not installed.")
        print("  Install with: pip3 install pyusb")
        print("  On macOS you also need: brew install libusb")
        sys.exit(1)

    print("Scanning USB devices for Fargo DTC4500e...\n")
    print(f"Looking for:")
    print(f"  VID=0x{FARGO_VID:04X} PID=0x{FARGO_PID:04X} (Fargo DTC4500e, confirmed)")
    print(f"  VID=0x{FARGO_VID:04X} (any Fargo device)")
    print(f"  VID=0x{HID_VID:04X} PID=0x{HID_PID_MIN:04X}-0x{HID_PID_MAX:04X} (HID Global range)")
    print()

    all_devices = list(usb.core.find(find_all=True))
    print(f"Total USB devices found: {len(all_devices)}\n")

    fargo_found = 0
    for dev in all_devices:
        if is_fargo_device(dev.idVendor, dev.idProduct):
            fargo_found += 1
            match_type = ""
            if dev.idVendor == FARGO_VID and dev.idProduct == FARGO_PID:
                match_type = " (EXACT MATCH - DTC4500e)"
            elif dev.idVendor == FARGO_VID:
                match_type = " (Fargo VID, unknown PID)"
            else:
                match_type = " (HID Global VID)"

            print(f"=== FARGO DEVICE FOUND{match_type} ===")
            print_device_info(dev, indent="  ")
            print()
        elif show_all:
            try:
                prod = dev.product or "(unknown)"
            except Exception:
                prod = "(unreadable)"
            print(f"  Other: VID=0x{dev.idVendor:04X} PID=0x{dev.idProduct:04X} {prod}")

    if fargo_found == 0:
        print("No Fargo printers found.")
        print("\nTroubleshooting:")
        print("  1. Is the printer connected via USB and powered on?")
        print("  2. Try running with sudo: sudo python3 discover_usb.py")
        print("  3. Check System Information > USB for the device")
        print("  4. The printer may need a moment to initialise after power-on")
    else:
        print(f"\nFound {fargo_found} Fargo device(s).")


def main():
    parser = argparse.ArgumentParser(
        description="Scan for Fargo DTC4500e printers on USB."
    )
    parser.add_argument('--all', '-a', action='store_true',
                        help='Show all USB devices, not just Fargo')

    args = parser.parse_args()
    scan_fargo(show_all=args.all)


if __name__ == '__main__':
    main()
