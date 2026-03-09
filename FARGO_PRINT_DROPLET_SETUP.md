# Fargo PDF Print Droplet — Setup Guide

This is a macOS desktop application that lets you drag and drop PDF files onto an icon to print them to your Fargo DTC4500e printer. Uses direct USB printing (no CUPS required).

## Prerequisites

### 1. Install Required Tools

```bash
# Install Ghostscript (for PDF rasterization)
brew install ghostscript

# Install Python dependencies
pip3 install pyusb pillow
```

## Quick Setup

### Step 1: Convert the Script to an App

1. Open **Script Editor** (search for it in Spotlight: Cmd+Space → "Script Editor")

2. Open the script file:
   - File → Open
   - Navigate to: `/Users/jamisonhill/Ai/fargo-dtc4500e-macos-driver/fargo-print-droplet.applescript`
   - Click Open

3. Save it as an Application:
   - File → Export
   - Choose a location (e.g., Desktop, Applications folder, or Documents)
   - **File Format:** Choose "Application" from the dropdown
   - **Name:** `Fargo Print` (or whatever you prefer)
   - Click Save

4. The app is now created! You should see a new icon on your desktop/in your chosen folder.

### Step 2: Add a Custom Icon (Optional but Recommended)

To make the app look nice:

1. Find or create an icon (16×16 up to 512×512 PNG or ICNS file)
   - You could use a printer icon, or create one with your logo

2. Right-click the `Fargo Print.app` → Get Info

3. Drag your icon image into the top-left corner of the info window

4. Close the info window — the icon is now customized!

## How to Use

Simply **drag any PDF file onto the Fargo Print icon**. The script will:
- ✅ Send the PDF to your Fargo printer
- ✅ Show a notification when printing starts
- ✅ Alert you if there's an error

## Troubleshooting

### "Permission denied" error
If you get a permission error, the printer queue may not be responding. Try:
```bash
sudo launchctl kickstart -kp system/org.cups.cupsd
```

### App doesn't respond
AppleScript needs permission to run. If macOS blocks it:
- System Preferences → Security & Privacy → General
- Click "Open Anyway" next to the blocked app

### "Fargo printer not found"
Verify your printer is installed:
```bash
lpstat -p FargoDTC4500e
```

If not installed, follow the main [DEPLOYMENT.md](./fargo-driver/DEPLOYMENT.md) guide.

## Advanced: Modify the Script

If you want to adjust settings, edit the AppleScript:

1. Right-click `Fargo Print.app` → Show Package Contents
2. Navigate: Contents → Resources → Scripts → main.scpt
3. Double-click `main.scpt` to open in Script Editor
4. Make changes and save

Common customizations:
- **Add paper size scaling:** Change the `lp` command to include `-o media=4x6`
- **Always ask for confirmation:** Add a dialog before printing
- **Queue multiple files:** The script already supports this (drag multiple PDFs at once)

## Technical Notes

- Uses the CUPS `lp` command to send PDFs
- Relies on your installed Fargo CUPS driver (`rastertofargo-macos`)
- Works entirely offline — no internet required
- Notifications via native macOS notification system

## Uninstalling

Just drag the `Fargo Print.app` to Trash, or delete it from wherever you saved it.

---

**Questions?** Check your printer setup:
```bash
# Verify driver is installed
ls -la /usr/libexec/cups/filter/rastertofargo-macos

# Check printer queue
lpstat -o FargoDTC4500e

# View CUPS errors if printing fails
tail -f /var/log/cups/error_log
```
