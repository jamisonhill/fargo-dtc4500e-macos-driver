/*
 * fargo_usb.c -- libusb-1.0 USB backend for the HID Fargo DTC4500e
 *
 * Implements device discovery, kernel driver detachment, interface claiming,
 * bulk transfer send/receive, and vendor control transfer status queries.
 *
 * CONFIRMED USB protocol from live Wireshark capture:
 *   - Bulk OUT endpoint: 0x01 (NOT 0x02)
 *   - Status queries use vendor control transfers, not bulk IN
 *   - Status query: bmRequestType=0x41, bRequest=0x00, 43-byte data
 *   - Status response: bmRequestType=0xC1, bRequest=0x00, up to 520 bytes
 *
 * Compile (example):
 *   cc -arch arm64 -o fargo_usb.o -c fargo_usb.c \
 *       $(pkg-config --cflags libusb-1.0)
 */

#include "fargo_usb.h"
#include "fargo_protocol.h"   /* for VID/PID/endpoint/status constants */

/* libusb header location varies by platform and install method.
 * Homebrew on macOS ARM64: /opt/homebrew/include/libusb-1.0/libusb.h
 * Homebrew on macOS Intel: /usr/local/include/libusb-1.0/libusb.h
 * pkg-config: use $(pkg-config --cflags libusb-1.0) to get the right path.
 * The Makefile adds the correct -I flag; this include should just work. */
#include <libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>   /* for sleep() */

/* ---------------------------------------------------------------------------
 * Internal (private) device structure
 * ---------------------------------------------------------------------------
 * This is the concrete definition of the opaque fargo_device_t type declared
 * in fargo_usb.h. Keeping it here prevents callers from poking at libusb
 * internals directly.
 */
struct fargo_device {
    libusb_context        *ctx;              /* libusb session context */
    libusb_device_handle  *handle;           /* open device handle */
    int                    kernel_detached;  /* non-zero if we detached the kernel driver */
    int                    interface_claimed; /* non-zero if we have claimed interface 0 */
};

/* ---------------------------------------------------------------------------
 * Static helper: is_fargo_device()
 * ---------------------------------------------------------------------------
 * Returns non-zero if the libusb device descriptor matches any known
 * Fargo DTC4500e identifier.
 */
static int is_fargo_device(const struct libusb_device_descriptor *desc)
{
    /* Best match: legacy FARGO VID + confirmed DTC4500e PID */
    if (desc->idVendor == FARGO_VID_FARGO &&
        desc->idProduct == FARGO_PID_DTC4500E)
    {
        return 2; /* exact match, highest priority */
    }

    /* Legacy FARGO VID with any PID (stay flexible for firmware variants) */
    if (desc->idVendor == FARGO_VID_FARGO) {
        return 1;
    }

    /* HID Global VID with PID in the documented range */
    if (desc->idVendor == FARGO_VID_HID &&
        desc->idProduct >= FARGO_PID_HID_MIN &&
        desc->idProduct <= FARGO_PID_HID_MAX)
    {
        return 1;
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * fargo_usb_open()
 * ---------------------------------------------------------------------------
 * Steps:
 *   1. Initialise a libusb context.
 *   2. Get the device list.
 *   3. Iterate, match by VID/PID (prefer exact DTC4500e PID match).
 *   4. Open the device.
 *   5. Detach kernel driver from interface 0 if active.
 *   6. Claim interface 0.
 *   7. Return the populated fargo_device_t.
 *
 * Errors are logged to stderr using CUPS-style "ERROR: " prefix so the CUPS
 * scheduler can surface them.
 */
fargo_device_t *fargo_usb_open(void)
{
    /* Allocate the opaque handle -- caller owns this and must free via
     * fargo_usb_close(). */
    fargo_device_t *dev = calloc(1, sizeof(fargo_device_t));
    if (!dev) {
        fprintf(stderr, "ERROR: fargo_usb_open: out of memory\n");
        return NULL;
    }

    /* Step 1: Initialise libusb. */
    int rc = libusb_init(&dev->ctx);
    if (rc < 0) {
        fprintf(stderr, "ERROR: fargo_usb_open: libusb_init failed: %s\n",
                libusb_strerror((enum libusb_error)rc));
        free(dev);
        return NULL;
    }

    /* Enable debug output during development */
#ifdef FARGO_USB_DEBUG
    libusb_set_option(dev->ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_INFO);
#endif

    /* Step 2: Enumerate all USB devices */
    libusb_device **device_list = NULL;
    ssize_t device_count = libusb_get_device_list(dev->ctx, &device_list);
    if (device_count < 0) {
        fprintf(stderr, "ERROR: fargo_usb_open: libusb_get_device_list failed: %s\n",
                libusb_strerror((enum libusb_error)device_count));
        libusb_exit(dev->ctx);
        free(dev);
        return NULL;
    }

    /* Step 3: Find a matching Fargo device (prefer exact PID match) */
    libusb_device *found = NULL;
    int best_score = 0;
    for (ssize_t i = 0; i < device_count; i++) {
        struct libusb_device_descriptor desc;
        rc = libusb_get_device_descriptor(device_list[i], &desc);
        if (rc < 0) {
            continue; /* Skip devices we can't query */
        }

        int score = is_fargo_device(&desc);
        if (score > best_score) {
            best_score = score;
            found = device_list[i];
            fprintf(stderr, "DEBUG: fargo_usb_open: found Fargo device "
                    "VID=0x%04x PID=0x%04x (score=%d)\n",
                    desc.idVendor, desc.idProduct, score);
        }
    }

    if (!found) {
        fprintf(stderr, "ERROR: fargo_usb_open: no Fargo DTC4500e printer found on USB\n");
        fprintf(stderr, "ERROR: fargo_usb_open: looked for VID=0x%04x PID=0x%04x "
                "and VID=0x%04x PID range 0x%04x-0x%04x\n",
                FARGO_VID_FARGO, FARGO_PID_DTC4500E,
                FARGO_VID_HID, FARGO_PID_HID_MIN, FARGO_PID_HID_MAX);
        libusb_free_device_list(device_list, 1);
        libusb_exit(dev->ctx);
        free(dev);
        return NULL;
    }

    /* Step 4: Open the device */
    rc = libusb_open(found, &dev->handle);
    libusb_free_device_list(device_list, 1);

    if (rc < 0) {
        fprintf(stderr, "ERROR: fargo_usb_open: libusb_open failed: %s\n",
                libusb_strerror((enum libusb_error)rc));
        libusb_exit(dev->ctx);
        free(dev);
        return NULL;
    }

    /* Step 5: Detach kernel driver from interface 0 if one is attached.
     * On macOS the IOUSBHIDDriver or AppleUSBHostHIDDevice kext may have
     * claimed the printer. On Linux it might be usblp. */
    rc = libusb_kernel_driver_active(dev->handle, FARGO_USB_INTERFACE);
    if (rc == 1) {
        fprintf(stderr, "DEBUG: fargo_usb_open: detaching kernel driver from interface %d\n",
                FARGO_USB_INTERFACE);
        rc = libusb_detach_kernel_driver(dev->handle, FARGO_USB_INTERFACE);
        if (rc < 0) {
            if (rc == LIBUSB_ERROR_NOT_SUPPORTED) {
                /* On macOS, libusb_detach_kernel_driver() always returns
                 * LIBUSB_ERROR_NOT_SUPPORTED. This is normal and expected;
                 * log it at DEBUG level, not ERROR. */
                fprintf(stderr, "DEBUG: fargo_usb_open: kernel driver detach not supported "
                        "(normal on macOS) -- proceeding\n");
            } else {
                fprintf(stderr, "ERROR: fargo_usb_open: libusb_detach_kernel_driver failed: %s\n",
                        libusb_strerror((enum libusb_error)rc));
            }
            /* Don't abort -- attempt the claim anyway */
        } else {
            dev->kernel_detached = 1;
        }
    } else if (rc < 0) {
        fprintf(stderr, "DEBUG: fargo_usb_open: kernel_driver_active check: %s\n",
                libusb_strerror((enum libusb_error)rc));
    }

    /* Step 6: Claim interface 0 */
    rc = libusb_claim_interface(dev->handle, FARGO_USB_INTERFACE);
    if (rc < 0) {
        fprintf(stderr, "WARNING: fargo_usb_open: libusb_claim_interface(%d) failed: %s\n",
                FARGO_USB_INTERFACE, libusb_strerror((enum libusb_error)rc));
        fprintf(stderr, "WARNING: fargo_usb_open: Proceeding anyway (interface may already be in use)\n");
        /* Don't abort - try to send anyway. On macOS, the device may already be
         * accessible even if we can't explicitly claim the interface. */
    } else {
        dev->interface_claimed = 1;
    }

    fprintf(stderr, "DEBUG: fargo_usb_open: printer opened and interface claimed\n");
    return dev;
}

/* ---------------------------------------------------------------------------
 * fargo_usb_close()
 * ---------------------------------------------------------------------------
 * Clean up in reverse order of acquisition.
 */
void fargo_usb_close(fargo_device_t *dev)
{
    if (!dev) {
        return;
    }

    if (dev->handle) {
        if (dev->interface_claimed) {
            int rc = libusb_release_interface(dev->handle, FARGO_USB_INTERFACE);
            if (rc < 0) {
                fprintf(stderr, "DEBUG: fargo_usb_close: release_interface: %s\n",
                        libusb_strerror((enum libusb_error)rc));
            }
        }

        if (dev->kernel_detached) {
            libusb_attach_kernel_driver(dev->handle, FARGO_USB_INTERFACE);
        }

        libusb_close(dev->handle);
        dev->handle = NULL;
    }

    if (dev->ctx) {
        libusb_exit(dev->ctx);
        dev->ctx = NULL;
    }

    free(dev);
}

/* ---------------------------------------------------------------------------
 * fargo_usb_send()
 * ---------------------------------------------------------------------------
 * Perform a single bulk OUT transfer to FARGO_BULK_OUT_EP (0x01, CONFIRMED).
 */
int fargo_usb_send(fargo_device_t *dev, const uint8_t *data, size_t len)
{
    if (!dev || !dev->handle) {
        fprintf(stderr, "ERROR: fargo_usb_send: device not open\n");
        return -1;
    }

    int transferred = 0;

    int rc = libusb_bulk_transfer(
        dev->handle,
        FARGO_BULK_OUT_EP,          /* 0x01 -- CONFIRMED from USB capture */
        (unsigned char *)data,      /* libusb wants non-const; cast is safe */
        (int)len,
        &transferred,
        FARGO_USB_TIMEOUT_MS
    );

    if (rc < 0) {
        fprintf(stderr, "ERROR: fargo_usb_send: bulk transfer failed: %s\n",
                libusb_strerror((enum libusb_error)rc));
        return -1;
    }

    if ((size_t)transferred != len) {
        fprintf(stderr, "ERROR: fargo_usb_send: short write: sent %zu, "
                "transferred %d\n", len, transferred);
        return -1;
    }

    fprintf(stderr, "DEBUG: fargo_usb_send: sent %d bytes to EP 0x%02x\n",
            transferred, FARGO_BULK_OUT_EP);
    return 0;
}

/* ---------------------------------------------------------------------------
 * fargo_usb_recv()
 * ---------------------------------------------------------------------------
 * Perform a single bulk IN transfer from FARGO_BULK_IN_EP.
 * Note: Status queries do NOT use this -- they use vendor control transfers.
 * This function is kept for potential future use.
 */
int fargo_usb_recv(fargo_device_t *dev, uint8_t *buf, size_t maxlen)
{
    if (!dev || !dev->handle) {
        fprintf(stderr, "ERROR: fargo_usb_recv: device not open\n");
        return -1;
    }

    int transferred = 0;

    int rc = libusb_bulk_transfer(
        dev->handle,
        FARGO_BULK_IN_EP,
        buf,
        (int)maxlen,
        &transferred,
        FARGO_USB_TIMEOUT_MS
    );

    if (rc == LIBUSB_ERROR_TIMEOUT) {
        fprintf(stderr, "DEBUG: fargo_usb_recv: timeout (no data)\n");
        return 0;
    }

    if (rc < 0) {
        fprintf(stderr, "ERROR: fargo_usb_recv: bulk transfer failed: %s\n",
                libusb_strerror((enum libusb_error)rc));
        return -1;
    }

    fprintf(stderr, "DEBUG: fargo_usb_recv: received %d bytes from EP 0x%02x\n",
            transferred, FARGO_BULK_IN_EP);
    return transferred;
}

/* ---------------------------------------------------------------------------
 * fargo_usb_query_status()
 * ---------------------------------------------------------------------------
 * Perform a status query via USB vendor control transfers.
 *
 * CONFIRMED protocol from live USB capture:
 *
 * Step 1 - Control OUT (send 43-byte status query):
 *   bmRequestType: 0x41 (Vendor | Host-to-Device | Interface)
 *   bRequest:      0x00
 *   wValue:        0x0000
 *   wIndex:        0x0000
 *   wLength:       43
 *   Data:          00 00 00 + Fg(version=0,len=32,payload=32zeros) + eP
 *
 * Step 2 - Control IN (receive up to 520-byte status response):
 *   bmRequestType: 0xC1 (Vendor | Device-to-Host | Interface)
 *   bRequest:      0x00
 *   wValue:        0x0000
 *   wIndex:        0x0000
 *   wLength:       520
 *   Response:      Fg packet containing ASCII status string
 *
 * Returns 0 on success (status struct populated), -1 on error.
 */
int fargo_usb_query_status(fargo_device_t *dev, fargo_status_t *status)
{
    if (!dev || !dev->handle || !status) {
        fprintf(stderr, "ERROR: fargo_usb_query_status: invalid arguments\n");
        return -1;
    }

    /* Build the 43-byte status query data */
    uint8_t query_data[FARGO_CTRL_STATUS_OUT_LEN];
    int query_len = fargo_build_status_query(query_data, sizeof(query_data));
    if (query_len < 0) {
        fprintf(stderr, "ERROR: fargo_usb_query_status: failed to build status query\n");
        return -1;
    }

    /* Step 1: Send status query via vendor control OUT transfer.
     * libusb_control_transfer() handles the setup packet automatically.
     * We just provide the data phase bytes. */
    int rc = libusb_control_transfer(
        dev->handle,
        FARGO_CTRL_OUT_REQTYPE,     /* 0x41: Vendor, Host-to-Device, Interface */
        FARGO_CTRL_REQUEST,         /* 0x00 */
        FARGO_CTRL_VALUE,           /* 0x0000 */
        FARGO_CTRL_INDEX,           /* 0x0000 */
        query_data,                 /* 43 bytes of query data */
        (uint16_t)query_len,
        FARGO_CTRL_TIMEOUT_MS
    );

    if (rc < 0) {
        fprintf(stderr, "ERROR: fargo_usb_query_status: control OUT failed: %s\n",
                libusb_strerror((enum libusb_error)rc));
        return -1;
    }

    fprintf(stderr, "DEBUG: fargo_usb_query_status: sent %d-byte status query\n", rc);

    /* Step 2: Read status response via vendor control IN transfer. */
    uint8_t response[FARGO_CTRL_STATUS_IN_LEN];
    memset(response, 0, sizeof(response));

    rc = libusb_control_transfer(
        dev->handle,
        FARGO_CTRL_IN_REQTYPE,      /* 0xC1: Vendor, Device-to-Host, Interface */
        FARGO_CTRL_REQUEST,         /* 0x00 */
        FARGO_CTRL_VALUE,           /* 0x0000 */
        FARGO_CTRL_INDEX,           /* 0x0000 */
        response,                   /* buffer for response */
        (uint16_t)sizeof(response), /* up to 520 bytes */
        FARGO_CTRL_TIMEOUT_MS
    );

    if (rc < 0) {
        fprintf(stderr, "ERROR: fargo_usb_query_status: control IN failed: %s\n",
                libusb_strerror((enum libusb_error)rc));
        return -1;
    }

    fprintf(stderr, "DEBUG: fargo_usb_query_status: received %d-byte response\n", rc);

    /* Parse the response into the status structure */
    return fargo_parse_status_response(response, (size_t)rc, status);
}

/* ---------------------------------------------------------------------------
 * fargo_usb_wait_ready()
 * ---------------------------------------------------------------------------
 * Poll the printer status repeatedly until it reports ready.
 * "Ready" means the response contains "USR_CMD_SUCCESSFUL" and the
 * door is closed (DH:1).
 *
 * Polls once per second up to timeout_sec seconds.
 * Returns 0 if ready, -1 on timeout or error.
 */
int fargo_usb_wait_ready(fargo_device_t *dev, int timeout_sec)
{
    if (!dev || !dev->handle) {
        fprintf(stderr, "ERROR: fargo_usb_wait_ready: device not open\n");
        return -1;
    }

    fprintf(stderr, "DEBUG: fargo_usb_wait_ready: polling status (timeout=%ds)\n",
            timeout_sec);

    for (int elapsed = 0; elapsed < timeout_sec; elapsed++) {
        fargo_status_t status;
        memset(&status, 0, sizeof(status));

        int rc = fargo_usb_query_status(dev, &status);
        if (rc < 0) {
            /* Query failed -- could be transient, retry after a delay */
            fprintf(stderr, "DEBUG: fargo_usb_wait_ready: query failed at %ds, retrying...\n",
                    elapsed);
            sleep(1);
            continue;
        }

        /* Check if the printer is ready:
         * - Command was successful
         * - Door/hood is closed (DH:1)
         * - Not out of cards (PE:0)
         */
        if (status.cmd_successful && status.door_hood == 1 && status.paper_empty == 0) {
            fprintf(stderr, "DEBUG: fargo_usb_wait_ready: printer ready after %ds\n",
                    elapsed);
            return 0;
        }

        /* Log why the printer isn't ready yet */
        if (!status.cmd_successful) {
            fprintf(stderr, "DEBUG: fargo_usb_wait_ready: waiting -- command not successful\n");
        }
        if (status.door_hood != 1) {
            fprintf(stderr, "DEBUG: fargo_usb_wait_ready: waiting -- door/hood open (DH=%d)\n",
                    status.door_hood);
        }
        if (status.paper_empty != 0) {
            fprintf(stderr, "ERROR: fargo_usb_wait_ready: printer out of cards (PE=%d)\n",
                    status.paper_empty);
        }

        sleep(1);
    }

    fprintf(stderr, "ERROR: fargo_usb_wait_ready: timeout after %d seconds\n", timeout_sec);
    return -1;
}

/* ---------------------------------------------------------------------------
 * fargo_usb_send_job()
 * ---------------------------------------------------------------------------
 * Send a complete job buffer to the printer as one (or more) bulk OUT writes.
 *
 * CONFIRMED from USB capture: The entire print job (handshake + config +
 * image data packets) is sent as one contiguous bulk OUT stream to EP 0x01.
 * There is no need to send eP markers separately -- they are embedded in
 * the buffer.
 *
 * For large jobs (hundreds of KB), we send in 64KB chunks to avoid
 * overwhelming the USB controller. The printer accepts continuous streaming.
 *
 * Returns 0 on success, -1 on error.
 */
int fargo_usb_send_job(fargo_device_t *dev, const uint8_t *buf, size_t len)
{
    if (!dev || !dev->handle) {
        fprintf(stderr, "ERROR: fargo_usb_send_job: device not open\n");
        return -1;
    }
    if (!buf || len == 0) {
        fprintf(stderr, "ERROR: fargo_usb_send_job: empty buffer\n");
        return -1;
    }

    fprintf(stderr, "DEBUG: fargo_usb_send_job: sending %zu bytes total\n", len);

    /* Send in chunks of up to 64KB to be safe with the USB controller.
     * The printer handles continuous bulk streaming fine. */
    const size_t chunk_size = 65536;
    size_t sent = 0;

    while (sent < len) {
        size_t remaining = len - sent;
        size_t this_chunk = (remaining > chunk_size) ? chunk_size : remaining;

        int transferred = 0;
        int rc = libusb_bulk_transfer(
            dev->handle,
            FARGO_BULK_OUT_EP,
            (unsigned char *)(buf + sent),
            (int)this_chunk,
            &transferred,
            FARGO_USB_TIMEOUT_MS
        );

        if (rc < 0) {
            fprintf(stderr, "ERROR: fargo_usb_send_job: bulk transfer failed at offset "
                    "%zu: %s\n", sent, libusb_strerror((enum libusb_error)rc));
            return -1;
        }

        if ((size_t)transferred != this_chunk) {
            fprintf(stderr, "ERROR: fargo_usb_send_job: short write at offset %zu: "
                    "expected %zu, got %d\n", sent, this_chunk, transferred);
            return -1;
        }

        sent += (size_t)transferred;
        fprintf(stderr, "DEBUG: fargo_usb_send_job: sent %zu/%zu bytes\n", sent, len);
    }

    fprintf(stderr, "DEBUG: fargo_usb_send_job: complete (%zu bytes)\n", len);
    return 0;
}

/* ---------------------------------------------------------------------------
 * fargo_usb_reset()
 * ---------------------------------------------------------------------------
 * Issues a USB-level reset to the device. The device will re-enumerate,
 * so the caller should close and re-open after a reset.
 */
int fargo_usb_reset(fargo_device_t *dev)
{
    if (!dev || !dev->handle) {
        fprintf(stderr, "ERROR: fargo_usb_reset: device not open\n");
        return -1;
    }

    fprintf(stderr, "DEBUG: fargo_usb_reset: resetting USB device\n");

    int rc = libusb_reset_device(dev->handle);
    if (rc < 0) {
        fprintf(stderr, "ERROR: fargo_usb_reset: libusb_reset_device failed: %s\n",
                libusb_strerror((enum libusb_error)rc));
        return -1;
    }

    return 0;
}
