/*
 * rastertofargo.c -- CUPS raster filter for the HID Fargo DTC4500e
 *
 * Entry point for the CUPS filter chain. Reads CUPS raster data from stdin
 * (or a file), converts it to Fargo Raster Language (FRL) packets, and
 * sends them to the printer over USB.
 *
 * CUPS filter calling convention:
 *   rastertofargo job-id user title copies options [filename]
 *
 * The filter must:
 *   - Exit 0 on success.
 *   - Exit non-zero on error (CUPS marks the job as failed).
 *   - Write DEBUG/INFO/ERROR lines to stderr (CUPS scheduler reads these).
 *   - Write nothing to stdout (that is reserved for the next filter in the
 *     chain; since we talk USB directly there is no next filter here).
 *
 * Protocol flow (CONFIRMED from live USB capture):
 *   1. Poll status via vendor control transfers until printer ready
 *   2. Bulk OUT EP=0x01: Send job handshake + config + image data as one stream
 *   3. Poll status again during/after printing
 *
 * Build:
 *   cc -arch arm64 -o rastertofargo-macos rastertofargo.c fargo_protocol.c fargo_usb.c \
 *       $(cups-config --cflags --libs) $(pkg-config --cflags --libs libusb-1.0)
 *
 * Install path on macOS:
 *   /usr/libexec/cups/filter/rastertofargo-macos
 */

#include <cups/cups.h>
#include <cups/raster.h>

/* ppd.h was deprecated in macOS 12 / CUPS 2.3.3 but is still needed for
 * reading PPD options in older-style printer drivers. Suppress the warning. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <cups/ppd.h>
#pragma clang diagnostic pop

#include "fargo_protocol.h"
#include "fargo_usb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

/* ---------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------------*/

/* Maximum size for a single RLE-compressed row (worst case) */
#define MAX_ROW_RLE_BYTES(width)    ((width) * 2)

/* Number of bytes in one Fg data strip payload */
#define STRIP_SIZE                  FARGO_PKT_FG_DATA_LEN  /* 512 */

/* Buffer size for one complete Fg data packet on the wire */
#define FG_DATA_WIRE_BUF            FARGO_WIRE_FG_DATA     /* 520 */

/* Maximum job buffer size (preamble + generous image data estimate).
 * A YMCKO job has ~5 panels x ~1011 lines x 768 pixels, RLE-compressed.
 * Worst case (no compression): 5 * 1011 * 768 * 2 = ~7.8 MB of RLE data,
 * plus ~520 bytes per 512-byte strip = ~16 MB of wire data.
 * We dynamically grow the buffer as needed. */
#define INITIAL_JOB_BUF_SIZE        (16 * 1024 * 1024)  /* 16 MB */

/* ---------------------------------------------------------------------------
 * PPD Option Names (CONFIRMED from reference PPD: DTC4500e_mac_v1.3.2.7.ppd)
 * ---------------------------------------------------------------------------
 * The PPD uses these exact option names and choice strings:
 *
 *   *OpenUI *Ribbon/Ribbon Type: PickOne
 *   Choices: YMCKO, YMCKO_Half, YMCKOK, YMCKK, YMCFKO, YMCFKOK,
 *            KStandard, KPremium, MonoColor, KO, BO, None
 *
 *   *OpenUI *CardThickness/Card Thickness: PickOne
 *   Choices: 10, 20, 30, 40
 *
 *   *OpenUI *PrintBothSides/Print Both Sides: Boolean
 *   Choices: true, false
 *
 *   *OpenUI *PageSize/Card Size: PickOne
 *   Choices: CR80, CR79
 */
#define PPD_OPTION_RIBBON       "Ribbon"
#define PPD_OPTION_THICKNESS    "CardThickness"
#define PPD_OPTION_DUPLEX       "PrintBothSides"
#define PPD_OPTION_PAGESIZE     "PageSize"

/* ---------------------------------------------------------------------------
 * Static helper: ppd_ribbon_type()
 * ---------------------------------------------------------------------------
 * Look up the chosen ribbon type from the PPD and map it to a
 * fargo_ribbon_type_t enum value.
 *
 * PPD option: "*Ribbon" with choices like "YMCKO", "KStandard", etc.
 * CONFIRMED from reference/DTC4500e_mac_v1.3.2.7.ppd
 */
static fargo_ribbon_type_t ppd_ribbon_type(ppd_file_t *ppd)
{
    if (!ppd) {
        fprintf(stderr, "DEBUG: ppd_ribbon_type: no PPD, defaulting to YMCKO\n");
        return FARGO_RIBBON_YMCKO;
    }

    ppd_choice_t *choice = ppdFindMarkedChoice(ppd, PPD_OPTION_RIBBON);
    if (!choice) {
        fprintf(stderr, "DEBUG: ppd_ribbon_type: '%s' option not found, defaulting YMCKO\n",
                PPD_OPTION_RIBBON);
        return FARGO_RIBBON_YMCKO;
    }

    const char *val = choice->choice;
    fprintf(stderr, "DEBUG: ppd_ribbon_type: PPD choice = \"%s\"\n", val);

    /* Map PPD choice strings to ribbon type enum.
     * These strings are CONFIRMED from the reference PPD file. */
    if      (strcmp(val, "YMCKO")     == 0) return FARGO_RIBBON_YMCKO;
    else if (strcmp(val, "YMCKO_Half")== 0) return FARGO_RIBBON_YMCKO_HALF;
    else if (strcmp(val, "YMCKOK")    == 0) return FARGO_RIBBON_YMCKOK;
    else if (strcmp(val, "YMCKK")     == 0) return FARGO_RIBBON_YMCKK;
    else if (strcmp(val, "YMCFKO")    == 0) return FARGO_RIBBON_YMCFKO;
    else if (strcmp(val, "YMCFKOK")   == 0) return FARGO_RIBBON_YMCFKOK;
    else if (strcmp(val, "KStandard") == 0) return FARGO_RIBBON_K_STD;
    else if (strcmp(val, "KPremium")  == 0) return FARGO_RIBBON_K_PRM;
    else if (strcmp(val, "MonoColor") == 0) return FARGO_RIBBON_MONO_COLOR;
    else if (strcmp(val, "KO")        == 0) return FARGO_RIBBON_KO;
    else if (strcmp(val, "BO")        == 0) return FARGO_RIBBON_BO;
    else if (strcmp(val, "None")      == 0) return FARGO_RIBBON_NONE;
    else {
        fprintf(stderr, "DEBUG: ppd_ribbon_type: unknown choice \"%s\", "
                "defaulting to YMCKO\n", val);
        return FARGO_RIBBON_YMCKO;
    }
}

/* ---------------------------------------------------------------------------
 * Static helper: ppd_card_thickness()
 * ---------------------------------------------------------------------------
 * Get the card thickness in mils from the PPD.
 * PPD option: "*CardThickness" with choices "10", "20", "30", "40"
 */
static uint16_t ppd_card_thickness(ppd_file_t *ppd)
{
    if (!ppd) return FARGO_CARD_THICKNESS_DEFAULT;

    ppd_choice_t *choice = ppdFindMarkedChoice(ppd, PPD_OPTION_THICKNESS);
    if (!choice) return FARGO_CARD_THICKNESS_DEFAULT;

    int val = atoi(choice->choice);
    if (val >= 10 && val <= 50) {
        return (uint16_t)val;
    }
    return FARGO_CARD_THICKNESS_DEFAULT;
}

/* ---------------------------------------------------------------------------
 * Static helper: ppd_duplex()
 * ---------------------------------------------------------------------------
 * Returns 1 if duplex (both-sides) printing is enabled in the PPD.
 * PPD option: "*PrintBothSides" with choices "true", "false"
 */
static int ppd_duplex(ppd_file_t *ppd)
{
    if (!ppd) return 0;

    ppd_choice_t *choice = ppdFindMarkedChoice(ppd, PPD_OPTION_DUPLEX);
    if (!choice) return 0;

    return (strcmp(choice->choice, "true") == 0) ? 1 : 0;
}

/* ---------------------------------------------------------------------------
 * Static helper: is_color_ribbon()
 * ---------------------------------------------------------------------------
 * Returns 1 if the ribbon type produces a multi-panel colour job,
 * 0 for monochrome (K-only) or ReWritable jobs.
 */
static int is_color_ribbon(fargo_ribbon_type_t r)
{
    switch (r) {
        case FARGO_RIBBON_YMCKO:
        case FARGO_RIBBON_YMCKO_HALF:
        case FARGO_RIBBON_YMCKOK:
        case FARGO_RIBBON_YMCKK:
        case FARGO_RIBBON_YMCFKO:
        case FARGO_RIBBON_YMCFKOK:
            return 1;
        default:
            return 0;
    }
}

/* ---------------------------------------------------------------------------
 * Dynamic buffer for building the complete job
 * ---------------------------------------------------------------------------
 * We build the entire print job in memory, then send it as one bulk write
 * via fargo_usb_send_job(). This matches the confirmed USB protocol where
 * all Fg packets are sent as ONE contiguous bulk stream.
 */
typedef struct {
    uint8_t *data;      /* Heap-allocated buffer */
    size_t   used;      /* Bytes currently in the buffer */
    size_t   capacity;  /* Allocated size */
} job_buffer_t;

/* Ensure the buffer has at least 'needed' additional bytes of capacity */
static int job_buf_ensure(job_buffer_t *jb, size_t needed)
{
    if (jb->used + needed <= jb->capacity) {
        return 0; /* Already have enough space */
    }

    /* Grow by doubling or to the required size, whichever is larger */
    size_t new_cap = jb->capacity * 2;
    if (new_cap < jb->used + needed) {
        new_cap = jb->used + needed;
    }

    uint8_t *new_data = realloc(jb->data, new_cap);
    if (!new_data) {
        fprintf(stderr, "ERROR: job_buf_ensure: out of memory (need %zu bytes)\n", new_cap);
        return -1;
    }

    jb->data = new_data;
    jb->capacity = new_cap;
    return 0;
}

/* Append raw bytes to the job buffer */
static int job_buf_append(job_buffer_t *jb, const uint8_t *data, size_t len)
{
    if (job_buf_ensure(jb, len) < 0) return -1;
    memcpy(jb->data + jb->used, data, len);
    jb->used += len;
    return 0;
}

/* ---------------------------------------------------------------------------
 * append_image_data_for_plane()
 * ---------------------------------------------------------------------------
 * Given a single colour plane, RLE-compress it and append the resulting
 * Fg data packets to the job buffer.
 *
 * Returns 0 on success, -1 on error.
 */
static int append_image_data_for_plane(job_buffer_t *jb,
                                       const uint8_t *plane_data,
                                       uint32_t width, uint32_t height)
{
    /* Staging buffer for one 512-byte strip */
    uint8_t strip[STRIP_SIZE];
    size_t  strip_used = 0;

    /* Wire buffer for one Fg data packet */
    uint8_t wire_buf[FG_DATA_WIRE_BUF];

    /* Temp RLE output for one row (worst case: width * 2 bytes) */
    size_t rle_row_max = (size_t)width * 2;
    uint8_t *rle_row = malloc(rle_row_max);
    if (!rle_row) {
        fprintf(stderr, "ERROR: append_image_data_for_plane: out of memory\n");
        return -1;
    }

    /* Macro to flush the current strip as a Fg data packet into the job buffer */
#define FLUSH_STRIP()  do {                                             \
        if (strip_used < STRIP_SIZE) {                                  \
            memset(strip + strip_used, 0x00, STRIP_SIZE - strip_used);  \
        }                                                               \
        int _len = fargo_build_fg_data(wire_buf, sizeof(wire_buf), strip); \
        if (_len < 0) {                                                 \
            free(rle_row);                                              \
            return -1;                                                  \
        }                                                               \
        if (job_buf_append(jb, wire_buf, (size_t)_len) < 0) {          \
            free(rle_row);                                              \
            return -1;                                                  \
        }                                                               \
        strip_used = 0;                                                 \
    } while (0)

    for (uint32_t row = 0; row < height; row++) {
        const uint8_t *row_ptr = plane_data + (size_t)row * width;

        /* RLE-compress this row */
        int rle_len = fargo_rle_compress(row_ptr, width, rle_row, rle_row_max);
        if (rle_len < 0) {
            fprintf(stderr, "ERROR: append_image_data_for_plane: "
                    "RLE failed on row %u\n", row);
            free(rle_row);
            return -1;
        }

        /* Copy compressed data into strip, flushing when full */
        size_t rle_pos = 0;
        while ((size_t)rle_len - rle_pos > 0) {
            size_t space = STRIP_SIZE - strip_used;
            size_t copy  = (size_t)rle_len - rle_pos;
            if (copy > space) copy = space;

            memcpy(strip + strip_used, rle_row + rle_pos, copy);
            strip_used += copy;
            rle_pos    += copy;

            if (strip_used == STRIP_SIZE) {
                FLUSH_STRIP();
            }
        }
    }

    /* Flush final partial strip */
    if (strip_used > 0) {
        FLUSH_STRIP();
    }

#undef FLUSH_STRIP

    free(rle_row);
    return 0;
}

/* ---------------------------------------------------------------------------
 * process_monochrome_page()
 * ---------------------------------------------------------------------------
 * Handle a single-panel (K-only) print job.
 * Reads raster data, compresses it, and appends to the job buffer.
 */
static int process_monochrome_page(job_buffer_t *jb,
                                   cups_raster_t *raster,
                                   const cups_page_header2_t *header)
{
    uint32_t width  = header->cupsWidth;
    uint32_t height = header->cupsHeight;
    uint32_t bpr    = header->cupsBytesPerLine;

    fprintf(stderr, "DEBUG: process_monochrome_page: %ux%u px, %u B/row\n",
            width, height, bpr);

    /* Read the entire page into a buffer */
    uint8_t *row_buf = malloc(bpr);
    if (!row_buf) {
        fprintf(stderr, "ERROR: process_monochrome_page: out of memory\n");
        return -1;
    }

    size_t page_bytes = (size_t)width * height;
    uint8_t *page_buf = malloc(page_bytes);
    if (!page_buf) {
        fprintf(stderr, "ERROR: process_monochrome_page: out of memory for page\n");
        free(row_buf);
        return -1;
    }

    for (uint32_t row = 0; row < height; row++) {
        if (cupsRasterReadPixels(raster, row_buf, bpr) == 0) {
            fprintf(stderr, "ERROR: process_monochrome_page: read failed at row %u\n", row);
            free(page_buf);
            free(row_buf);
            return -1;
        }
        memcpy(page_buf + (size_t)row * width, row_buf,
               (width < bpr) ? width : bpr);
    }

    free(row_buf);

    /* Append as a single panel to job buffer */
    int rc = append_image_data_for_plane(jb, page_buf, width, height);
    free(page_buf);

    return rc;
}

/* ---------------------------------------------------------------------------
 * process_color_page()
 * ---------------------------------------------------------------------------
 * Handle a multi-panel colour job (YMCKO etc).
 * Reads the CUPS raster, separates into colour planes, and appends
 * each panel's data to the job buffer.
 *
 * TODO: The RGB-to-YMCKO mapping should use proper ICC profiles.
 *       For now we do simple inverse (RGB -> CMY) plus empty K and full O.
 */
static int process_color_page(job_buffer_t *jb,
                               cups_raster_t *raster,
                               const cups_page_header2_t *header,
                               fargo_ribbon_type_t ribbon)
{
    uint32_t width    = header->cupsWidth;
    uint32_t height   = header->cupsHeight;
    uint32_t bpr      = header->cupsBytesPerLine;
    uint32_t channels = header->cupsNumColors;

    fprintf(stderr, "DEBUG: process_color_page: %ux%u px, %u channels, %u B/row\n",
            width, height, channels, bpr);

    int num_panels = fargo_ribbon_panel_count(ribbon);
    fprintf(stderr, "DEBUG: process_color_page: ribbon %d -> %d panels\n",
            (int)ribbon, num_panels);

    size_t plane_pixels = (size_t)width * height;

    /* Allocate one plane per panel */
    uint8_t **planes = calloc((size_t)num_panels, sizeof(uint8_t *));
    if (!planes) {
        fprintf(stderr, "ERROR: process_color_page: out of memory\n");
        return -1;
    }
    for (int p = 0; p < num_panels; p++) {
        planes[p] = calloc(plane_pixels, 1);
        if (!planes[p]) {
            fprintf(stderr, "ERROR: process_color_page: out of memory for plane %d\n", p);
            for (int q = 0; q < p; q++) free(planes[q]);
            free(planes);
            return -1;
        }
    }

    uint8_t *row_buf = malloc(bpr);
    if (!row_buf) {
        for (int p = 0; p < num_panels; p++) free(planes[p]);
        free(planes);
        return -1;
    }

    /* Read and de-interleave the raster image */
    for (uint32_t row = 0; row < height; row++) {
        if (cupsRasterReadPixels(raster, row_buf, bpr) == 0) {
            fprintf(stderr, "ERROR: process_color_page: read failed at row %u\n", row);
            free(row_buf);
            for (int p = 0; p < num_panels; p++) free(planes[p]);
            free(planes);
            return -1;
        }

        for (uint32_t px = 0; px < width; px++) {
            size_t raster_base = (size_t)px * channels;
            size_t dest_offset = (size_t)row * width + px;

            /*
             * Map CUPS RGB channels to Fargo panel order.
             *
             * Assumption: CUPS delivers RGB (channels=3) and we need to send
             * Y, M, C, K, O panels. We convert:
             *   Y = 255 - R   (yellow = inverse red)
             *   M = 255 - G   (magenta = inverse green)
             *   C = 255 - B   (cyan = inverse blue)
             *   K = 0         (no K extraction -- printer composites)
             *   O = 255       (full overlay coverage)
             *
             * TODO: Use ICC profiles for accurate colour reproduction.
             * TODO: Implement black extraction for text/barcodes.
             */
            if (channels >= 3) {
                uint8_t r = row_buf[raster_base + 0];
                uint8_t g = row_buf[raster_base + 1];
                uint8_t b = row_buf[raster_base + 2];

                if (num_panels >= 1) planes[0][dest_offset] = 255 - r; /* Y */
                if (num_panels >= 2) planes[1][dest_offset] = 255 - g; /* M */
                if (num_panels >= 3) planes[2][dest_offset] = 255 - b; /* C */
                if (num_panels >= 4) planes[3][dest_offset] = 0x00;    /* K */
                if (num_panels >= 5) planes[4][dest_offset] = 0xFF;    /* O */
            } else if (channels == 1) {
                if (num_panels >= 1) planes[0][dest_offset] = row_buf[raster_base];
            }
        }
    }

    free(row_buf);

    /* Append each panel's data to the job buffer */
    int rc = 0;
    for (int p = 0; p < num_panels && rc == 0; p++) {
        fprintf(stderr, "DEBUG: process_color_page: appending panel %d/%d\n",
                p + 1, num_panels);
        rc = append_image_data_for_plane(jb, planes[p], width, height);
    }

    for (int p = 0; p < num_panels; p++) free(planes[p]);
    free(planes);

    return rc;
}

/* ---------------------------------------------------------------------------
 * main() -- CUPS filter entry point
 * ---------------------------------------------------------------------------
 * CUPS calls this with exactly 5 or 6 arguments:
 *   argv[0] = filter name (rastertofargo-macos)
 *   argv[1] = job ID
 *   argv[2] = user name
 *   argv[3] = job title
 *   argv[4] = number of copies
 *   argv[5] = job options (key=value pairs)
 *   argv[6] = input file path (optional; if absent, read from stdin)
 *
 * Return 0 on success, 1 on error.
 */
int main(int argc, char *argv[])
{
    /* Validate argument count */
    if (argc < 6 || argc > 7) {
        fprintf(stderr, "ERROR: rastertofargo: Usage: rastertofargo-macos "
                "job-id user title copies options [file]\n");
        return 1;
    }

    const char *job_id    = argv[1];
    const char *user      = argv[2];
    const char *title     = argv[3];
    int         copies    = atoi(argv[4]);
    const char *filename  = (argc == 7) ? argv[6] : NULL;

    fprintf(stderr, "DEBUG: rastertofargo: job=%s user=%s title=\"%s\" "
            "copies=%d\n", job_id, user, title, copies);

    /* Clamp copies to a sane range */
    if (copies < 1)  copies = 1;
    if (copies > 99) copies = 99;

    /* -----------------------------------------------------------------------
     * Open the input raster stream
     * ----------------------------------------------------------------------- */
    int input_fd;
    if (filename) {
        input_fd = open(filename, O_RDONLY);
        if (input_fd < 0) {
            fprintf(stderr, "ERROR: rastertofargo: cannot open \"%s\": %s\n",
                    filename, strerror(errno));
            return 1;
        }
    } else {
        input_fd = STDIN_FILENO;
    }

    cups_raster_t *raster = cupsRasterOpen(input_fd, CUPS_RASTER_READ);
    if (!raster) {
        fprintf(stderr, "ERROR: rastertofargo: cupsRasterOpen failed\n");
        if (filename) close(input_fd);
        return 1;
    }

    /* -----------------------------------------------------------------------
     * Open the PPD to read job options
     * ----------------------------------------------------------------------- */
    ppd_file_t *ppd = NULL;
    const char *ppd_path = getenv("PPD");
    if (ppd_path) {
        ppd = ppdOpenFile(ppd_path);
        if (!ppd) {
            fprintf(stderr, "DEBUG: rastertofargo: could not open PPD \"%s\" "
                    "-- using defaults\n", ppd_path);
        } else {
            ppdMarkDefaults(ppd);
            cups_option_t *options     = NULL;
            int            num_options = cupsParseOptions(argv[5], 0, &options);
            cupsMarkOptions(ppd, num_options, options);
            cupsFreeOptions(num_options, options);
        }
    } else {
        fprintf(stderr, "DEBUG: rastertofargo: PPD env var not set, using defaults\n");
    }

    /* Determine job settings from PPD */
    fargo_ribbon_type_t ribbon    = ppd_ribbon_type(ppd);
    uint16_t            thickness = ppd_card_thickness(ppd);
    int                 duplex    = ppd_duplex(ppd);

    fprintf(stderr, "DEBUG: rastertofargo: ribbon=%d thickness=%u duplex=%d\n",
            (int)ribbon, thickness, duplex);

    /* -----------------------------------------------------------------------
     * Open the USB printer
     * ----------------------------------------------------------------------- */
    fargo_device_t *dev = fargo_usb_open();
    if (!dev) {
        fprintf(stderr, "ERROR: rastertofargo: failed to open USB printer\n");
        if (ppd) ppdClose(ppd);
        cupsRasterClose(raster);
        if (filename) close(input_fd);
        return 1;
    }

    /* -----------------------------------------------------------------------
     * Wait for printer to be ready (poll status via vendor control transfers)
     * ----------------------------------------------------------------------- */
    fprintf(stderr, "INFO: rastertofargo: waiting for printer to be ready...\n");
    if (fargo_usb_wait_ready(dev, 30) < 0) {
        fprintf(stderr, "ERROR: rastertofargo: printer not ready (timeout or error)\n");
        fargo_usb_close(dev);
        if (ppd) ppdClose(ppd);
        cupsRasterClose(raster);
        if (filename) close(input_fd);
        return 1;
    }

    /* -----------------------------------------------------------------------
     * Main page loop
     * ----------------------------------------------------------------------- */
    int page_count = 0;
    int filter_rc  = 0;

    cups_page_header2_t header;

    while (cupsRasterReadHeader2(raster, &header)) {
        page_count++;
        fprintf(stderr, "INFO: rastertofargo: processing page %d\n", page_count);

        /* Build the job descriptor */
        fargo_job_t job = {
            .ribbon         = ribbon,
            .copies         = copies,
            .dual_sided     = (duplex != 0),
            .card_thickness = thickness,
        };

        /* Use default card dimensions if CUPS reports zero */
        uint32_t page_width  = header.cupsWidth;
        uint32_t page_height = header.cupsHeight;
        if (page_width  == 0) page_width  = FARGO_CR80_WIDTH_PIXELS;
        if (page_height == 0) page_height = FARGO_CR80_HEIGHT_PIXELS;

        /* ---- Allocate dynamic job buffer ---- */
        job_buffer_t jb = {0};
        jb.data = malloc(INITIAL_JOB_BUF_SIZE);
        if (!jb.data) {
            fprintf(stderr, "ERROR: rastertofargo: out of memory for job buffer\n");
            filter_rc = 1;
            break;
        }
        jb.capacity = INITIAL_JOB_BUF_SIZE;
        jb.used = 0;

        /* ---- Step 1: Build job preamble (handshake + config) ---- */
        {
            /* Build preamble into a temp buffer, then append to job buffer */
            uint8_t preamble[128]; /* 80 bytes needed, 128 for safety */
            int preamble_len = fargo_build_job_preamble(preamble, sizeof(preamble), &job);
            if (preamble_len < 0) {
                fprintf(stderr, "ERROR: rastertofargo: failed to build job preamble\n");
                free(jb.data);
                filter_rc = 1;
                break;
            }
            if (job_buf_append(&jb, preamble, (size_t)preamble_len) < 0) {
                free(jb.data);
                filter_rc = 1;
                break;
            }
            fprintf(stderr, "DEBUG: rastertofargo: preamble built (%d bytes)\n", preamble_len);
        }

        /* ---- Step 2: Append image data ---- */
        int page_rc;
        if (is_color_ribbon(ribbon)) {
            page_rc = process_color_page(&jb, raster, &header, ribbon);
        } else {
            page_rc = process_monochrome_page(&jb, raster, &header);
        }

        if (page_rc < 0) {
            fprintf(stderr, "ERROR: rastertofargo: image data failed on page %d\n",
                    page_count);
            free(jb.data);
            filter_rc = 1;
            break;
        }

        /* ---- Step 3: Append end-of-job packet ---- */
        {
            uint8_t eoj_buf[FARGO_WIRE_FG_EOJ]; /* 22 bytes */
            int eoj_len = fargo_build_fg_eoj(eoj_buf, sizeof(eoj_buf));
            if (eoj_len < 0) {
                fprintf(stderr, "ERROR: rastertofargo: failed to build EOJ\n");
                free(jb.data);
                filter_rc = 1;
                break;
            }
            if (job_buf_append(&jb, eoj_buf, (size_t)eoj_len) < 0) {
                free(jb.data);
                filter_rc = 1;
                break;
            }
        }

        /* ---- Step 4: Send entire job as one bulk write ---- */
        fprintf(stderr, "INFO: rastertofargo: sending job (%zu bytes)\n", jb.used);

        if (fargo_usb_send_job(dev, jb.data, jb.used) < 0) {
            fprintf(stderr, "ERROR: rastertofargo: send_job failed on page %d\n",
                    page_count);
            free(jb.data);
            filter_rc = 1;
            break;
        }

        free(jb.data);

        /* ---- Step 5: Wait for printer to finish ---- */
        fprintf(stderr, "INFO: rastertofargo: page %d sent, waiting for completion...\n",
                page_count);
        if (fargo_usb_wait_ready(dev, 60) < 0) {
            fprintf(stderr, "ERROR: rastertofargo: printer not ready after page %d\n",
                    page_count);
            /* Don't abort -- the print may still complete */
        }

        fprintf(stderr, "INFO: rastertofargo: page %d complete\n", page_count);
    }

    if (page_count == 0) {
        fprintf(stderr, "ERROR: rastertofargo: no pages in raster stream\n");
        filter_rc = 1;
    }

    /* -----------------------------------------------------------------------
     * Cleanup
     * ----------------------------------------------------------------------- */
    fargo_usb_close(dev);

    if (ppd) {
        ppdClose(ppd);
    }

    cupsRasterClose(raster);

    if (filename) {
        close(input_fd);
    }

    if (filter_rc == 0) {
        fprintf(stderr, "INFO: rastertofargo: job complete (%d page(s))\n", page_count);
    }

    return filter_rc;
}
