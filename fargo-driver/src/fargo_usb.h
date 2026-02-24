/*
 * fargo_usb.h -- USB communication API for the HID Fargo DTC4500e
 *
 * Provides an opaque device handle and functions to open/close the printer,
 * send bulk data, query status via vendor control transfers, and wait for
 * the printer to become ready.
 *
 * Built on libusb-1.0. Do NOT use the deprecated libusb-0.1 API.
 *
 * macOS notes:
 *   - The IOUSBHIDDriver kernel extension may attach to the printer. We must
 *     call libusb_detach_kernel_driver() before claiming the interface.
 *   - On macOS 12+ (Monterey and later), USB kernel driver detachment via
 *     libusb may require SIP to be partially disabled or the process to run
 *     as root. The filter runs as root inside CUPS, so this should be fine.
 *   - libusb on macOS uses the IOKit backend -- no /dev/bus/usb nodes exist.
 */

#ifndef FARGO_USB_H
#define FARGO_USB_H

#include <stdint.h>
#include <stddef.h>
#include "fargo_protocol.h"   /* for fargo_status_t */

/* ---------------------------------------------------------------------------
 * Opaque device handle
 * ---------------------------------------------------------------------------
 * Callers only ever hold a pointer to fargo_device_t. The full struct is
 * defined in fargo_usb.c to keep libusb types out of the public API.
 */
/* fargo_device_t is forward-declared in fargo_protocol.h */

/* ---------------------------------------------------------------------------
 * fargo_usb_open()
 * ---------------------------------------------------------------------------
 * Initialise libusb, scan for a Fargo DTC4500e printer, detach any kernel
 * driver on interface 0, and claim the interface.
 *
 * Search order:
 *   1. VID 0x09b0 (legacy FARGO), PID 0xbf0c (DTC4500e)
 *   2. VID 0x09b0 (legacy FARGO), any PID
 *   3. VID 0x076b (HID Global), PID in range 0xbf00-0xbfff
 *
 * Returns a heap-allocated fargo_device_t on success, or NULL on failure.
 * On failure, an error message has already been logged to stderr.
 */
fargo_device_t *fargo_usb_open(void);

/* ---------------------------------------------------------------------------
 * fargo_usb_close()
 * ---------------------------------------------------------------------------
 * Release the USB interface, re-attach the kernel driver if we detached it,
 * close the device handle, and shut down the libusb context.
 *
 * Safe to call with dev == NULL (no-op).
 */
void fargo_usb_close(fargo_device_t *dev);

/* ---------------------------------------------------------------------------
 * fargo_usb_send()
 * ---------------------------------------------------------------------------
 * Send len bytes from data to the printer via bulk OUT endpoint
 * (FARGO_BULK_OUT_EP = 0x01, CONFIRMED).
 *
 * Returns 0 on success, -1 on libusb error (error already logged to stderr).
 */
int fargo_usb_send(fargo_device_t *dev, const uint8_t *data, size_t len);

/* ---------------------------------------------------------------------------
 * fargo_usb_recv()
 * ---------------------------------------------------------------------------
 * Read up to maxlen bytes from the printer via bulk IN endpoint
 * (FARGO_BULK_IN_EP = 0x81). Note: this is NOT used for status -- use
 * fargo_usb_query_status() instead for status queries.
 *
 * Returns the number of bytes actually received (>= 0), or -1 on error.
 */
int fargo_usb_recv(fargo_device_t *dev, uint8_t *buf, size_t maxlen);

/* ---------------------------------------------------------------------------
 * fargo_usb_query_status()
 * ---------------------------------------------------------------------------
 * Perform a single status query via USB vendor control transfers.
 *
 * This sends the 43-byte status query via a vendor OUT control transfer,
 * then reads the response via a vendor IN control transfer.
 * The response is an Fg packet containing ASCII status flags.
 *
 * Returns 0 on success (status populated), -1 on error.
 */
int fargo_usb_query_status(fargo_device_t *dev, fargo_status_t *status);

/* ---------------------------------------------------------------------------
 * fargo_usb_wait_ready()
 * ---------------------------------------------------------------------------
 * Poll the printer status repeatedly until it reports ready.
 * "Ready" means: USR_CMD_SUCCESSFUL, door closed, no errors.
 *
 * timeout_sec: maximum seconds to wait (e.g., 30).
 * Returns 0 if printer is ready, -1 on timeout or error.
 */
int fargo_usb_wait_ready(fargo_device_t *dev, int timeout_sec);

/* ---------------------------------------------------------------------------
 * fargo_usb_send_job()
 * ---------------------------------------------------------------------------
 * Send a complete job buffer to the printer as a single bulk OUT write.
 * The buffer should contain the full job data (handshake + config + image
 * data + end-of-job), built by the caller or by fargo_build_job_preamble()
 * plus image data plus end-of-job.
 *
 * For very large buffers, this function sends in chunks internally to
 * avoid overwhelming the USB stack.
 *
 * Returns 0 on success, -1 on error.
 */
int fargo_usb_send_job(fargo_device_t *dev, const uint8_t *buf, size_t len);

/* ---------------------------------------------------------------------------
 * fargo_usb_reset()
 * ---------------------------------------------------------------------------
 * Perform a USB-level device reset. Use this if the printer ends up in an
 * unknown state after a failed job.
 *
 * Returns 0 on success, -1 on error.
 */
int fargo_usb_reset(fargo_device_t *dev);

#endif /* FARGO_USB_H */
