# Fargo DTC4500e macOS Driver — Deployment Guide

## Overview

This guide covers installing the Fargo DTC4500e native macOS CUPS driver on your Mac or sharing it with others.

---

## System Requirements

| Item | Requirement |
|------|-------------|
| **macOS Version** | 10.15+ (Catalina or later) |
| **Mac Architecture** | ARM64 (M1/M2/M3+) or Intel (x86_64) |
| **USB** | Standard USB 2.0+ (driver uses libusb-1.0) |
| **Printer** | Fargo DTC4500e (USB VID: 0x09b0, PID: 0xbf0c) |

---

## Installation Methods

### Method 1: Quick Install (ARM64 Only, Pre-compiled)

If you're on an **Apple Silicon Mac (M1/M2/M3/M4)** and want to use the pre-compiled binary:

```bash
# 1. Clone or copy the repo
git clone https://github.com/jamisonhill/fargo-dtc4500e-macos-driver.git
cd fargo-dtc4500e-macos-driver/fargo-driver

# 2. Install dependencies
brew install libusb

# 3. Install the driver
sudo make install

# 4. Verify installation
lpstat -p FargoDTC4500e
```

### Method 2: Compile & Install (ARM64 or Intel)

For **any Mac architecture** (or if you need to recompile for security/customization):

```bash
# 1. Prerequisites
brew install libusb

# 2. Clone/copy the repo
git clone https://github.com/jamisonhill/fargo-dtc4500e-macos-driver.git
cd fargo-dtc4500e-macos-driver/fargo-driver

# 3. Clean and build (automatically targets your Mac's architecture)
make clean
make

# 4. Install system-wide
sudo make install

# 5. Verify installation
lpstat -p FargoDTC4500e
```

The `Makefile` automatically detects your Mac's architecture (ARM64 or Intel) and compiles accordingly.

---

## Post-Installation: Add the Printer

After installing the driver, add the printer to CUPS:

### Via System Preferences (GUI)

1. **System Preferences** → **Printers & Scanners**
2. Click **+** to add a printer
3. Select **FargoDTC4500e** from the list
4. Click **Add**

The printer should now appear in your printer list.

### Via Command Line

```bash
# Discover the printer
lpinfo -m | grep -i fargo

# Add it (if not auto-discovered)
sudo lpadmin -p FargoDTC4500e \
  -m drv:///sample.drv/generic.ppd \
  -v usb://Fargo%20Electronics/DTC4500e
```

---

## Verification

### Check Driver Installation

```bash
# Verify the filter binary is installed
ls -la /usr/libexec/cups/filter/rastertofargo-macos

# Verify the PPD file is installed
ls -la /Library/Printers/PPDs/Contents/Resources/DTC4500e-macos.ppd
```

### Check Printer Configuration

```bash
# List all configured printers
lpstat -p

# Check if FargoDTC4500e is present
lpstat -p FargoDTC4500e
```

### Test Print

```bash
# Print a test file (from repo reference folder)
lp -d FargoDTC4500e ~/fargo-dtc4500e-macos-driver/fargo-driver/reference/DTC4500e_K_STD_Tst.prn
```

Check the CUPS error log if it fails:

```bash
tail -f /var/log/cups/error_log
```

---

## Sharing the Driver with Other Users

### Option A: Share Pre-compiled Binary (ARM64 Only)

If distributing to other **Apple Silicon Macs**:

1. Build the driver on your M1/M2/M3/M4 Mac
2. Archive the built binary:
   ```bash
   tar czf fargo-dtc4500e-arm64.tar.gz \
     /usr/libexec/cups/filter/rastertofargo-macos \
     /Library/Printers/PPDs/Contents/Resources/DTC4500e-macos.ppd
   ```
3. Share the archive via email, cloud storage, etc.
4. Recipient unpacks and installs:
   ```bash
   tar xzf fargo-dtc4500e-arm64.tar.gz -C /
   sudo launchctl kickstart -kp system/org.cups.cupsd
   ```

### Option B: Share Source Code (Recommended)

For **mixed environments** or security-conscious users:

1. Share the entire repo or just the `fargo-driver/` folder
2. Recipient follows **Method 2: Compile & Install** above
3. This builds the driver for their specific architecture

---

## Troubleshooting

### Printer Not Appearing in Printers & Scanners

1. **Restart CUPS:**
   ```bash
   sudo launchctl kickstart -kp system/org.cups.cupsd
   ```

2. **Verify libusb is installed:**
   ```bash
   brew list libusb
   ```
   If not: `brew install libusb`

3. **Check USB connectivity:**
   ```bash
   system_profiler SPUSBDataType | grep -i fargo
   ```
   Should show `Fargo Electronics` with PID `0xbf0c`

### Print Jobs Stuck in Queue

```bash
# Cancel all jobs for this printer
cancel -a FargoDTC4500e

# Restart CUPS
sudo launchctl kickstart -kp system/org.cups.cupsd
```

### Permission Denied Errors

If you see "Permission denied" during `make install`:

```bash
# Ensure you're using sudo
sudo make install

# Or check file permissions
sudo chmod 755 /usr/libexec/cups/filter/rastertofargo-macos
sudo chown root:_lp /Library/Printers/PPDs/Contents/Resources/DTC4500e-macos.ppd
```

### Driver Binary Not Found (Intel Mac)

If you copied a pre-compiled ARM64 binary to an Intel Mac:

1. Delete the ARM64 binary
2. Follow **Method 2: Compile & Install** to rebuild for Intel

---

## Uninstallation

To remove the driver completely:

```bash
# Remove the filter binary
sudo rm /usr/libexec/cups/filter/rastertofargo-macos

# Remove the PPD file
sudo rm /Library/Printers/PPDs/Contents/Resources/DTC4500e-macos.ppd

# Remove from printer list
sudo lpadmin -x FargoDTC4500e

# Restart CUPS
sudo launchctl kickstart -kp system/org.cups.cupsd
```

---

## Testing the Installation

After successful installation, test with each ribbon type:

```bash
# Black ribbon (K)
lp -d FargoDTC4500e ~/fargo-dtc4500e-macos-driver/fargo-driver/reference/DTC4500e_K_STD_Tst.prn

# Color (YMCKO)
lp -d FargoDTC4500e ~/fargo-dtc4500e-macos-driver/fargo-driver/reference/DTC4500e_YMCKO_STD_Tst.prn

# Black + Overlay (KO)
lp -d FargoDTC4500e ~/fargo-dtc4500e-macos-driver/fargo-driver/reference/DTC4500e_KO_Tst.prn
```

Check print status:

```bash
lpstat -o FargoDTC4500e
```

---

## Technical Details

### What Gets Installed

| File | Location | Purpose |
|------|----------|---------|
| `rastertofargo-macos` | `/usr/libexec/cups/filter/` | Main CUPS filter binary |
| `DTC4500e-macos.ppd` | `/Library/Printers/PPDs/Contents/Resources/` | Printer description file |

### Runtime Dependencies

- **libusb-1.0** — USB communication (installed via Homebrew)
- **CUPS** — Built into macOS
- **libc** — Built into macOS

### No Additional Requirements

The driver does **not** require:
- Additional kernel extensions
- Closed-source vendor software
- Network connectivity
- Specific system frameworks beyond standard macOS CUPS

---

## Feedback & Support

For issues or questions:

1. Check CUPS error log: `tail -f /var/log/cups/error_log`
2. Verify printer USB connection: `system_profiler SPUSBDataType | grep -i fargo`
3. Review the [PROTOCOL_SPEC.md](./protocol-analysis/PROTOCOL_SPEC.md) for technical details

---

## Version History

| Date | Version | Notes |
|------|---------|-------|
| 2026-02-25 | 1.0 | Initial release — ARM64 & Intel support |
| 2026-03-09 | 1.1 | Added comprehensive deployment guide |
