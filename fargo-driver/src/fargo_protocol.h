/*
 * fargo_protocol.h -- Fargo Raster Language (FRL) protocol definitions
 *
 * Defines constants, structs, and enums for communicating with the
 * HID Fargo DTC4500e ID card printer over USB.
 *
 * Protocol reverse-engineered from:
 *   - PRN file binary analysis (Mac driver v1.3.2 test patterns)
 *   - Linux ARM64 binary strings/symbols (rastertofargo-3.3.4)
 *   - Mac PPD (v1.3.2.7) and Linux PPD (v1.0.0.4)
 *   - DTC4500e.xml printer model definition
 *   - LIVE USB CAPTURE (Wireshark URB analysis)
 *
 * Fields marked TODO require further verification.
 * Fields marked CONFIRMED have been validated against live USB captures.
 */

#ifndef FARGO_PROTOCOL_H
#define FARGO_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* =========================================================================
 * USB Vendor / Product IDs
 * =========================================================================
 * The DTC4500e may enumerate under either the legacy FARGO VID or the
 * HID Global VID (post-acquisition).
 */
#define FARGO_VID_FARGO         0x09b0  /* Legacy Fargo Electronics VID */
#define FARGO_VID_HID           0x076b  /* HID Global VID */
#define FARGO_PID_DTC4500E      0xbf0c  /* CONFIRMED from live USB capture */
#define FARGO_PID_HID_MIN       0xbf00  /* Lower bound of HID Global PID range */
#define FARGO_PID_HID_MAX       0xbfff  /* Upper bound of HID Global PID range */

/* =========================================================================
 * USB Endpoint Definitions (CONFIRMED from live USB capture)
 * =========================================================================
 * The printer uses USB Printer Class (0x07) on Interface 0.
 * Print data goes via Bulk OUT. Status uses vendor control transfers,
 * NOT the Bulk IN endpoint.
 */
#define FARGO_USB_INTERFACE     0       /* USB interface number to claim */
#define FARGO_BULK_OUT_EP       0x01    /* CONFIRMED: Bulk OUT endpoint */
#define FARGO_BULK_IN_EP        0x81    /* Bulk IN endpoint (NOT used for status) */
#define FARGO_USB_TIMEOUT_MS    5000    /* Bulk transfer timeout in milliseconds */
#define FARGO_USB_MAX_PACKET    512     /* Max USB packet size */

/* =========================================================================
 * FRL Version Constants (CONFIRMED from live USB capture)
 * =========================================================================
 * Print data packets (Fs handshake + Fg image data) use version 0x0001.
 * Status/command packets (vendor control transfers) use version 0x0000.
 */
#define FRL_VERSION_PRINT       0x0001  /* For Fs/Fg packets in print stream */
#define FRL_VERSION_STATUS      0x0000  /* For status query/response via control */

/* =========================================================================
 * Vendor Control Transfer Constants (CONFIRMED from live USB capture)
 * =========================================================================
 * The printer uses vendor-specific USB control transfers for status polling.
 * These do NOT go through the bulk endpoints.
 */
#define FARGO_CTRL_OUT_REQTYPE  0x41    /* Vendor | Host-to-Device | Interface */
#define FARGO_CTRL_IN_REQTYPE   0xC1    /* Vendor | Device-to-Host | Interface */
#define FARGO_CTRL_REQUEST      0x00    /* bRequest value (always 0) */
#define FARGO_CTRL_VALUE        0x0000  /* wValue (always 0) */
#define FARGO_CTRL_INDEX        0x0000  /* wIndex (always 0) */
#define FARGO_CTRL_STATUS_OUT_LEN   43  /* Bytes in status query (control OUT) */
#define FARGO_CTRL_STATUS_IN_LEN   520  /* Max bytes in status response (control IN) */
#define FARGO_CTRL_TIMEOUT_MS    5000   /* Control transfer timeout */

/* =========================================================================
 * FRL Packet Magic Bytes
 * =========================================================================
 * Every FRL packet begins with a 2-byte ASCII marker:
 *   "Fs" (0x46 0x73) = Session/handshake control
 *   "Fg" (0x46 0x67) = Data/command/image packets
 *   "eP" (0x65 0x50) = End-of-packet terminator
 *
 * CONFIRMED: Consistent across all 18 analyzed PRN files + live capture.
 */
#define FARGO_MARKER_FS_0       0x46    /* 'F' */
#define FARGO_MARKER_FS_1       0x73    /* 's' */
#define FARGO_MARKER_FG_0       0x46    /* 'F' */
#define FARGO_MARKER_FG_1       0x67    /* 'g' */
#define FARGO_MARKER_EP_0       0x65    /* 'e' */
#define FARGO_MARKER_EP_1       0x50    /* 'P' */

/* As little-endian uint16 values for fast comparison */
#define FARGO_MAGIC_FS          0x7346  /* "Fs" as LE uint16 */
#define FARGO_MAGIC_FG          0x6746  /* "Fg" as LE uint16 */
#define FARGO_MAGIC_EP          0x5065  /* "eP" as LE uint16 */

/* =========================================================================
 * Packet Envelope Structure
 * =========================================================================
 * Wire format for Fg and Fs packets:
 *
 *   Offset  Size  Field
 *   ------  ----  -----
 *   0       2     Magic marker ("Fg" or "Fs")
 *   2       2     Version (LE): 0x0001 for print, 0x0000 for status
 *   4       2     Payload length in bytes, 16-bit LE
 *   6       N     Payload (N = payload length)
 *
 * Followed by a 2-byte "eP" (0x65 0x50) terminator.
 * In print stream, packets + eP are packed contiguously in one bulk write.
 * In status queries, the Fg+eP is wrapped in vendor control transfer data.
 */
#define FARGO_PKT_VERSION       FRL_VERSION_PRINT  /* Default for print packets */
#define FARGO_PKT_HDR_SIZE      6       /* bytes: magic(2) + version(2) + length(2) */
#define FARGO_PKT_EP_SIZE       2       /* bytes: "eP" terminator */

/* =========================================================================
 * Packet Payload Sizes
 * =========================================================================
 */
#define FARGO_PKT_FS_START_LEN        0       /* Fs session start: no payload */
#define FARGO_PKT_FS_INIT_LEN         8       /* Fs session setup: 8 zero bytes */
#define FARGO_PKT_FG_CONFIG_LEN       48      /* Fg job config (single-sided) */
#define FARGO_PKT_FG_CONFIG_DUAL_LEN  50      /* Fg job config (dual-sided) */
#define FARGO_PKT_FG_CMD_LEN          32      /* Fg printer command */
#define FARGO_PKT_FG_DATA_LEN         512     /* Fg RLE image data strip */
#define FARGO_PKT_FG_EOJ_LEN          14      /* Fg end-of-job */
#define FARGO_PKT_FG_STATUS_LEN       32      /* Fg status query payload (inside control) */

/* Panel descriptor sizes (vary by ribbon type) */
#define FARGO_PKT_PANEL_DESC_STD      126     /* Standard (YMCKO, KO, etc.) */
#define FARGO_PKT_PANEL_DESC_RW       134     /* ReWritable (NONE ribbon) */
#define FARGO_PKT_PANEL_DESC_FL       190     /* Fluorescent (YMCFKO*) */

/* =========================================================================
 * Wire Size Helpers
 * =========================================================================
 * Total bytes on the wire for a packet: header + payload + eP terminator
 */
#define FARGO_WIRE_SIZE(payload_len) \
    (FARGO_PKT_HDR_SIZE + (payload_len) + FARGO_PKT_EP_SIZE)

#define FARGO_WIRE_FS_START     FARGO_WIRE_SIZE(FARGO_PKT_FS_START_LEN)   /* 8 */
#define FARGO_WIRE_FS_INIT      FARGO_WIRE_SIZE(FARGO_PKT_FS_INIT_LEN)    /* 16 */
#define FARGO_WIRE_FG_CONFIG    FARGO_WIRE_SIZE(FARGO_PKT_FG_CONFIG_LEN)   /* 56 */
#define FARGO_WIRE_FG_DATA      FARGO_WIRE_SIZE(FARGO_PKT_FG_DATA_LEN)    /* 520 */
#define FARGO_WIRE_FG_EOJ       FARGO_WIRE_SIZE(FARGO_PKT_FG_EOJ_LEN)     /* 22 */

/* Job handshake: Fs(v=1,len=0)+eP + Fs(v=1,len=8,payload=8zeros)+eP = 24 bytes */
#define FARGO_JOB_HANDSHAKE_LEN  (FARGO_WIRE_FS_START + FARGO_WIRE_FS_INIT)  /* 24 */

/* =========================================================================
 * Card / Image Geometry
 * =========================================================================
 * CR80 standard ID card at 300 DPI.
 */
#define FARGO_DPI                     300

/* Printhead line width (pixels) */
#define FARGO_PRINTLINE_WIDTH         768     /* CONFIRMED: from XML + all config packets */
#define FARGO_PRINTLINE_OVERHANG      5       /* Extra pixels past card right edge */

/* Per-line overhead bytes in the decoded data stream */
#define FARGO_LINE_ESC_BYTES          2       /* Header bytes before pixel data each line */
#define FARGO_LINE_EXTRA_BYTES        1       /* Trailer byte after pixel data each line */
#define FARGO_LINE_TOTAL_BYTES        (FARGO_LINE_ESC_BYTES + FARGO_PRINTLINE_WIDTH + FARGO_LINE_EXTRA_BYTES) /* 771 */

/* Image heights observed in panel descriptors */
#define FARGO_IMAGE_HEIGHT_STD        1011    /* Standard height */
#define FARGO_IMAGE_HEIGHT_YMCKO      1009    /* YMCKO standard */

/* CR80 card physical pixels at 300 DPI */
#define FARGO_CR80_WIDTH_PIXELS       638     /* 54.0mm / 25.4 * 300 */
#define FARGO_CR80_HEIGHT_PIXELS      1013    /* 85.6mm / 25.4 * 300 */

/* CR79 card physical pixels at 300 DPI */
#define FARGO_CR79_WIDTH_PIXELS       619
#define FARGO_CR79_HEIGHT_PIXELS      993

/* PPD page dimensions in points (1/72 inch) */
#define FARGO_CR80_WIDTH_PT           152
#define FARGO_CR80_HEIGHT_PT          242
#define FARGO_CR79_WIDTH_PT           148
#define FARGO_CR79_HEIGHT_PT          238

/* Card thickness (from XML, in mils; default 30 = 0.030") */
#define FARGO_CARD_THICKNESS_DEFAULT  30      /* CONFIRMED: offset 40-43 in config = 0x0000001E */

/* Top-of-page margin in lines */
#define FARGO_TOP_MARGIN_LINES        5       /* From XML TopOfPageMargin */

/* =========================================================================
 * Config Packet Field Offsets (CONFIRMED from live USB capture)
 * =========================================================================
 * The 48-byte config payload uses UINT32 LE fields, NOT uint16 as
 * previously assumed. Confirmed from captured K_STD config packet:
 *
 *   01 00 00 00  28 00 00 00  22 00 01 00  00 00 01 00
 *   00 00 00 00  00 00 00 00  03 01 00 00  00 00 00 00
 *   00 00 00 00  00 00 00 00  1e 00 00 00  00 00 00 00
 *
 * Byte offset  Size   Observed       Meaning
 * -----------  ----   --------       -------
 * 0-3          u32    0x00000001     Number of copies (LE)
 * 4-7          u32    0x00000028=40  Card size indicator (CR80=40)
 * 8-9          u16    0x0022=34      Card height code
 * 10-11        u16    0x0001         Card type (1 = CR80)
 * 12-13        u16    0x0000         Reserved
 * 14-15        u16    varies         Ribbon device_id (LE)
 * 16-23        8B     zeros          Reserved (duplex flags etc.)
 * 24           u8     0x03           Hopper selector or flag
 * 25           u8     0x01           Unknown flag
 * 26-39        14B    zeros          Reserved
 * 40-43        u32    0x0000001E=30  Card thickness in mils (LE)
 * 44-47        4B     zeros          Reserved
 */
#define CFG_OFF_COPIES          0   /* uint32 LE: number of copies */
#define CFG_OFF_CARD_SIZE       4   /* uint32 LE: card size indicator (40=CR80) */
#define CFG_OFF_CARD_HEIGHT     8   /* uint16 LE: card height code (34=CR80) */
#define CFG_OFF_CARD_TYPE       10  /* uint16 LE: card type (1=CR80) */
#define CFG_OFF_RESERVED_12     12  /* uint16 LE: reserved */
#define CFG_OFF_RIBBON_TYPE     14  /* uint16 LE: ribbon device_id */
#define CFG_OFF_DUPLEX_FLAGS    16  /* 8 bytes: duplex-related flags */
#define CFG_OFF_HOPPER          24  /* uint8: hopper selector (0x03 observed) */
#define CFG_OFF_FLAG_25         25  /* uint8: unknown flag (0x01 observed) */
#define CFG_OFF_THICKNESS       40  /* uint32 LE: card thickness in mils */

/* Card size indicator values */
#define CFG_CARD_SIZE_CR80      40  /* 0x28 */
#define CFG_CARD_SIZE_CR79      38  /* TODO: verify from CR79 capture */

/* Card height code values */
#define CFG_CARD_HEIGHT_CR80    34  /* 0x22 */
#define CFG_CARD_HEIGHT_CR79    32  /* TODO: verify from CR79 capture */

/* Card type values */
#define CFG_CARD_TYPE_CR80      1
#define CFG_CARD_TYPE_CR79      2   /* TODO: verify */

/* =========================================================================
 * Ribbon Type IDs (CONFIRMED from PRN + XML + live capture)
 * =========================================================================
 */
typedef enum {
    FARGO_RIBBON_NONE       = 0,    /* No ribbon / ReWritable card */
    FARGO_RIBBON_K_STD      = 1,    /* Standard Resin Black (KStandard) */
    FARGO_RIBBON_K_PRM      = 2,    /* Premium Resin Black (KPremium) */
    FARGO_RIBBON_MONO_COLOR = 3,    /* Colored Resin (MonoColor) */
    FARGO_RIBBON_METALIC    = 4,    /* Metallic Resin */
    FARGO_RIBBON_KO         = 5,    /* K + Overlay */
    FARGO_RIBBON_YMCKK      = 6,    /* YMCKK (dual K, no overlay) */
    FARGO_RIBBON_YMCKO      = 7,    /* YMCKO (full color + overlay) */
    FARGO_RIBBON_YMCKOK     = 8,    /* YMCKOK (full color + overlay + back K) */
    FARGO_RIBBON_BO         = 9,    /* Dye-Sub Black + Overlay */
    FARGO_RIBBON_YMCKO_HALF = 13,   /* YMCKO Half Panel */
    FARGO_RIBBON_YMCFKO     = 19,   /* YMCFKO (with Fluorescent) */
    FARGO_RIBBON_YMCFKOK    = 20,   /* YMCFKOK (Fluorescent + back K) */
} fargo_ribbon_type_t;

/* =========================================================================
 * Panel Color / Composition IDs
 * =========================================================================
 */
typedef enum {
    FARGO_PANEL_YELLOW       = 1,   /* DyeSub Yellow */
    FARGO_PANEL_MAGENTA      = 2,   /* DyeSub Magenta */
    FARGO_PANEL_CYAN         = 3,   /* DyeSub Cyan */
    FARGO_PANEL_BLACK_RESIN  = 4,   /* Resin Black */
    FARGO_PANEL_BLACK_DYESUB = 5,   /* DyeSub Black (for BO ribbon) */
    FARGO_PANEL_OVERLAY      = 7,   /* Clear Overlay */
    FARGO_PANEL_FLUORESCENT  = 9,   /* Fluorescent Resin (F panel) */
    FARGO_PANEL_RW_ERASE     = 11,  /* ReWritable Erase */
    FARGO_PANEL_RW_BLACK     = 12,  /* ReWritable Black */
} fargo_panel_id_t;

/* Panel composition types */
typedef enum {
    FARGO_COMP_DYESUB      = 0,
    FARGO_COMP_RESIN       = 1,
    FARGO_COMP_OVERLAY     = 2,
    FARGO_COMP_REWRITABLE  = 3,
} fargo_panel_composition_t;

/* Panel options */
typedef enum {
    FARGO_PANEL_OPT_NONE   = 0,
    FARGO_PANEL_OPT_SPLIT  = 1,    /* K panel split (text extraction) */
    FARGO_PANEL_OPT_HALF   = 2,    /* Half-panel (YMCKO_Half Y/M/C) */
} fargo_panel_option_t;

/* =========================================================================
 * Printer Command IDs
 * =========================================================================
 */
typedef enum {
    FARGO_CMD_CALIBRATE_RIBBON     = 0x0011, /* CONFIRMED */
    FARGO_CMD_CALIBRATE_LAMINATION = 0x0013, /* CONFIRMED */
    FARGO_CMD_CLEAN_PRINTER        = 0x0014, /* CONFIRMED */
} fargo_command_t;

/* =========================================================================
 * Heat / Intensity Constants (from DTC4500e.xml)
 * =========================================================================
 */
#define FARGO_HEAT_ERASE_DEFAULT      255
#define FARGO_HEAT_SEAL_DEFAULT       105
#define FARGO_HEAT_OVERLAY_DEFAULT    128
#define FARGO_HEAT_OVERLAMINATE       255

/* Luminance weights for RGB-to-grayscale conversion */
#define FARGO_LUMIN_RED               77
#define FARGO_LUMIN_GREEN             151
#define FARGO_LUMIN_BLUE              28

/* Black extraction thresholds */
#define FARGO_BLACK_THRESHOLD_RED     2
#define FARGO_BLACK_THRESHOLD_GREEN   2
#define FARGO_BLACK_THRESHOLD_BLUE    2

/* White level thresholds */
#define FARGO_WHITE_LEVEL_RED         253
#define FARGO_WHITE_LEVEL_GREEN       253
#define FARGO_WHITE_LEVEL_BLUE        253

/* =========================================================================
 * Laminate Types
 * =========================================================================
 */
typedef enum {
    FARGO_LAM_NONE              = -1,
    FARGO_LAM_CLEAR_FILM        = 0,
    FARGO_LAM_POLYGUARD_06      = 3,
    FARGO_LAM_POLYGUARD_10      = 4,
    FARGO_LAM_POLYGUARD_ALT     = 5,
    FARGO_LAM_REGISTERED_FILM   = 6,
    FARGO_LAM_HOLOGRAPHIC_FILM  = 7,
    FARGO_LAM_UNKNOWN           = 8,
} fargo_laminate_type_t;

/* =========================================================================
 * cupsModelNumber Values
 * =========================================================================
 */
#define FARGO_MODEL_MAC               1
#define FARGO_MODEL_LINUX             5

/* =========================================================================
 * Printer Status Structure (parsed from vendor control transfer response)
 * =========================================================================
 * The status response is an ASCII string inside an Fg packet:
 *   "USR_CMD_SUCCESSFUL\0...DH:1;IH:0;LM:0;FL:1;MG:0;SM:0;SG:0;DS:0;SC:0;MF:0;HD:0;IC:0;PE:0;\0"
 *
 * Flag meanings (CONFIRMED from live capture):
 *   DH = Door/Hood (1=closed, 0=open)
 *   IH = Input Hopper (0=empty, 1=has cards)
 *   LM = Laminator active
 *   FL = Feed Laminator (1=ready)
 *   PE = Paper/Card Empty (0=no, 1=yes)
 *   MG = Mag stripe busy
 *   SM = Smart card module
 *   SG = ?
 *   DS = ?
 *   SC = ?
 *   MF = ?
 *   HD = ?
 *   IC = ?
 */
typedef struct {
    bool cmd_successful;    /* "USR_CMD_SUCCESSFUL" present in response */
    int  door_hood;         /* DH: 1=closed, 0=open */
    int  input_hopper;      /* IH: 0=empty, 1=has cards */
    int  laminator;         /* LM: 0=inactive */
    int  feed_laminator;    /* FL: 1=ready */
    int  mag_busy;          /* MG: 0=idle */
    int  smart_card;        /* SM: 0=idle */
    int  paper_empty;       /* PE: 0=no, 1=out of cards */
    /* Additional flags stored as raw values */
    int  sg_flag;           /* SG */
    int  ds_flag;           /* DS */
    int  sc_flag;           /* SC */
    int  mf_flag;           /* MF */
    int  hd_flag;           /* HD */
    int  ic_flag;           /* IC */
    /* Raw response string for debugging */
    char raw_status[256];
} fargo_status_t;

/* =========================================================================
 * Packed Packet Structures
 * =========================================================================
 * These match the exact wire format. Use __attribute__((packed)) to
 * prevent compiler padding.
 */

/* Generic packet header (6 bytes) */
typedef struct __attribute__((packed)) {
    uint16_t magic;     /* FARGO_MAGIC_FS or FARGO_MAGIC_FG (LE) */
    uint16_t version;   /* FRL_VERSION_PRINT or FRL_VERSION_STATUS (LE) */
    uint16_t length;    /* Payload length in bytes (LE) */
} fargo_pkt_hdr_t;

/*
 * Printer Command Packet (32-byte payload)
 */
typedef struct __attribute__((packed)) {
    uint16_t cmd_version;       /* [0-1]   Always 0x0001 */
    uint8_t  reserved_02[10];   /* [2-11]  All zeros */
    uint16_t command_id;        /* [12-13] See fargo_command_t */
    uint16_t command_flags;     /* [14-15] e.g., 0x0008 for clean */
    uint8_t  reserved_16[16];   /* [16-31] All zeros */
} fargo_cmd_payload_t;          /* 32 bytes total */

/*
 * End-of-Job Packet (14-byte payload)
 * CONFIRMED: 63 00 00 00 06 00 00 00 00 00 00 00 00 00
 */
typedef struct __attribute__((packed)) {
    uint16_t eoj_marker;        /* [0-1]   Always 0x0063 */
    uint16_t reserved_02;       /* [2-3]   Always 0x0000 */
    uint16_t section_type;      /* [4-5]   Always 0x0006 */
    uint8_t  reserved_06[8];    /* [6-13]  All zeros */
} fargo_eoj_payload_t;          /* 14 bytes total */

/* =========================================================================
 * RLE Compression
 * =========================================================================
 * Format: (count, value) pairs
 * Decode: repeat 'value' exactly (count + 1) times
 * Range:  count 0x00-0xFF -> 1 to 256 repetitions
 *
 * CONFIRMED: Verified by round-trip encode/decode on multiple PRN files.
 */
#define FARGO_RLE_MAX_RUN             256
#define FARGO_RLE_MAX_COUNT           0xFF
#define FARGO_RLE_PAIR_SIZE           2

/* Worst-case RLE output size (no compression at all) */
#define FARGO_RLE_WORST_CASE(n)       ((n) * 2)

/* =========================================================================
 * Data Stream Layout
 * =========================================================================
 */
#define FARGO_BYTES_PER_LINE          FARGO_LINE_TOTAL_BYTES /* 771 */
#define FARGO_PIXELS_PER_LINE         FARGO_PRINTLINE_WIDTH  /* 768 */

/* Maximum number of panels in any ribbon type */
#define FARGO_MAX_PANELS              7       /* YMCFKOK has 7 */

/* =========================================================================
 * High-Level Job Descriptor
 * =========================================================================
 */
typedef struct {
    fargo_ribbon_type_t ribbon;
    int                 copies;
    bool                dual_sided;
    uint16_t            card_thickness;  /* mils, default 30 */
} fargo_job_t;

typedef struct {
    uint32_t width;         /* Always 768 */
    uint32_t height;        /* Lines per panel (~1011) */
    uint32_t panel_count;   /* Number of panels for this side */
} fargo_image_info_t;

/* =========================================================================
 * Function Prototypes (implemented in fargo_protocol.c)
 * =========================================================================
 */

/* Forward-declare the USB device handle type (defined in fargo_usb.h) */
typedef struct fargo_device fargo_device_t;

/* Build an Fs start packet (zero-length payload). Returns bytes written or -1. */
int fargo_build_fs_start(uint8_t *buf, size_t bufsize);

/* Build an Fs setup/init packet (8-byte zero payload). Returns bytes written or -1. */
int fargo_build_fs_init(uint8_t *buf, size_t bufsize);

/* Build a Fg config packet for the given job. Returns bytes written or -1. */
int fargo_build_fg_config(uint8_t *buf, size_t bufsize, const fargo_job_t *job);

/* Build a Fg printer command packet. Returns bytes written or -1. */
int fargo_build_fg_command(uint8_t *buf, size_t bufsize,
                           fargo_command_t cmd, uint16_t flags);

/* Build a Fg data strip packet from 512 bytes of RLE data. Returns bytes written or -1. */
int fargo_build_fg_data(uint8_t *buf, size_t bufsize,
                        const uint8_t *rle_data);

/* Build a Fg end-of-job packet. Returns bytes written or -1. */
int fargo_build_fg_eoj(uint8_t *buf, size_t bufsize);

/* Build the 2-byte "eP" terminator. Returns 2 or -1. */
int fargo_build_ep(uint8_t *buf, size_t bufsize);

/*
 * Build the 43-byte vendor control transfer data for a status query.
 * Format: 00 00 00 + Fg(version=0, len=32, payload=32 zeros) + eP
 * Returns bytes written (43) or -1 on error.
 */
int fargo_build_status_query(uint8_t *buf, size_t bufsize);

/*
 * Parse the status response from a vendor control transfer.
 * The response is an Fg packet containing ASCII status strings.
 * Returns 0 on success, -1 on parse error.
 */
int fargo_parse_status_response(const uint8_t *data, size_t len,
                                fargo_status_t *status);

/*
 * Build the complete job buffer for a single bulk write.
 * Includes: handshake (Fs start + Fs init) + config packet + eP markers.
 * The caller should append image data packets after this.
 *
 * job_buf:     output buffer (caller-allocated)
 * job_bufsize: size of output buffer
 * job:         job configuration
 *
 * Returns total bytes written, or -1 on error.
 */
int fargo_build_job_preamble(uint8_t *buf, size_t bufsize, const fargo_job_t *job);

/*
 * RLE compress: encode input_len bytes from input.
 * Output format: (count, value) pairs where count = repetitions - 1.
 * Returns bytes written to output, or -1 if output_max is too small.
 */
int fargo_rle_compress(const uint8_t *input, size_t input_len,
                       uint8_t *output, size_t output_max);

/*
 * RLE decompress: decode rle_len bytes from rle_data.
 * Returns bytes written to output, or -1 if output_max is too small.
 */
int fargo_rle_decompress(const uint8_t *rle_data, size_t rle_len,
                         uint8_t *output, size_t output_max);

/* Send a pre-built packet buffer via USB. Returns 0 on success, -1 on error. */
int fargo_send_packet(fargo_device_t *dev, const uint8_t *buf, size_t len);

/* Get the number of front-side panels for a ribbon type */
int fargo_ribbon_panel_count(fargo_ribbon_type_t ribbon);

/* Get the panel device_id for a given ribbon and panel index */
fargo_panel_id_t fargo_ribbon_panel_id(fargo_ribbon_type_t ribbon, int index);

/* Check if a ribbon type requires dual-sided config (50-byte config) */
bool fargo_ribbon_is_dual_config(fargo_ribbon_type_t ribbon);

#endif /* FARGO_PROTOCOL_H */
