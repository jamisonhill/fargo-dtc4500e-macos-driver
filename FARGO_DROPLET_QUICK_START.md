# Fargo PDF Print Droplet — Quick Start

**Drag & drop PDFs onto your desktop icon to print them to your Fargo DTC4500e printer.**

## One-Time Setup (5 minutes)

### 1. Install Dependencies

```bash
# Install Ghostscript (converts PDF to raster)
brew install ghostscript

# Install Python packages
pip3 install pyusb pillow
```

### 2. Create the Droplet App

1. Open **Script Editor** (Cmd+Space → "Script Editor")
2. **File → Open** → Select `fargo-print-droplet.applescript`
3. **File → Export**
   - File Format: **Application**
   - Save to: Desktop (or Applications folder)
   - Name: `Fargo Print`
4. Click **Save**

You now have a `Fargo Print.app` on your desktop! 🎉

### 3. Optional: Add a Custom Icon

1. Find or create a printer icon (PNG or ICNS format)
2. Right-click `Fargo Print.app` → **Get Info**
3. Drag your icon image into the top-left corner of the info window
4. Close the window

## How to Use

**Simply drag a PDF onto the `Fargo Print` icon.** The app will:
- ✓ Convert the PDF to printer-ready format
- ✓ Connect to your Fargo printer via USB
- ✓ Send the print job
- ✓ Show you a notification when done

**First time only:** You'll be prompted for your Mac password (needed for USB access). Click "OK" to proceed.

## Troubleshooting

### "Printer not found"
- Make sure your Fargo printer is connected via USB
- Check that it's powered on
- Try restarting the printer

### "Permission denied"
- The droplet will ask for your password the first time
- Make sure you enter your Mac login password (not printer password)

### "Ghostscript not found"
```bash
brew install ghostscript
```

### "pyusb not installed"
```bash
pip3 install pyusb pillow
```

## Technical Details

The droplet uses **direct USB printing** and bypasses CUPS entirely:

1. PDF → Raster conversion (using Ghostscript)
2. Raster → FRL encoding (Fargo Raster Language)
3. Direct USB transmission to printer

No printer queues, no CUPS filters, no network needed.

## FAQ

**Q: Can I use this on a network printer?**
A: No, this only works for USB-connected Fargo printers.

**Q: Does it support color printing?**
A: Yes, if your Fargo printer supports it. The script auto-detects ribbon type.

**Q: Can I drag multiple PDFs at once?**
A: Yes! The app will print them in order.

**Q: How do I uninstall?**
A: Just drag `Fargo Print.app` to Trash. Dependencies can stay installed.

---

**Need help?** Check the full setup guide: `FARGO_PRINT_DROPLET_SETUP.md`
