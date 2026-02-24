/*
 * fargo_protocol.c -- Fargo Raster Language (FRL) packet builder
 *
 * Implements all packet construction functions, the RLE compressor,
 * status query/response handling, and job buffer assembly for the
 * HID Fargo DTC4500e printer.
 *
 * CONFIRMED USB protocol from live Wireshark capture:
 *   - Print packets use FRL version 0x0001
 *   - Status packets use FRL version 0x0000
 *   - Config payload is 48 bytes with uint32 LE fields at offsets 0, 4, 40
 *   - Status query is 43 bytes: 00 00 00 + Fg(v=0,len=32,32zeros) + eP
 *
 * Compile (example):
 *   cc -arch arm64 -o fargo_protocol.o -c fargo_protocol.c
 */

#include "fargo_protocol.h"
#include "fargo_usb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Static helper: write_pkt_hdr()
 * ---------------------------------------------------------------------------
 * Writes a 6-byte packet header (magic, version, payload length) into buf.
 * Returns 6 (the number of bytes written).
 *
 * All multi-byte fields are little-endian, matching the FRL wire format.
 * We write byte-by-byte for clarity and portability.
 */
static int write_pkt_hdr(uint8_t *buf, uint16_t magic, uint16_t version, uint16_t payload_len)
{
    /* magic: 2 bytes LE */
    buf[0] = (uint8_t)(magic & 0xFF);
    buf[1] = (uint8_t)((magic >> 8) & 0xFF);

    /* version: 2 bytes LE */
    buf[2] = (uint8_t)(version & 0xFF);
    buf[3] = (uint8_t)((version >> 8) & 0xFF);

    /* payload length: 2 bytes LE */
    buf[4] = (uint8_t)(payload_len & 0xFF);
    buf[5] = (uint8_t)((payload_len >> 8) & 0xFF);

    return FARGO_PKT_HDR_SIZE; /* always 6 */
}

/* Convenience wrapper that uses the default print version (0x0001) */
static int write_pkt_hdr_print(uint8_t *buf, uint16_t magic, uint16_t payload_len)
{
    return write_pkt_hdr(buf, magic, FRL_VERSION_PRINT, payload_len);
}

/* ---------------------------------------------------------------------------
 * Static helper: write_ep_marker()
 * ---------------------------------------------------------------------------
 * Appends the 2-byte "eP" end-of-packet marker to buf. Returns 2.
 */
static int write_ep_marker(uint8_t *buf)
{
    buf[0] = FARGO_MARKER_EP_0;   /* 0x65 'e' */
    buf[1] = FARGO_MARKER_EP_1;   /* 0x50 'P' */
    return FARGO_PKT_EP_SIZE;
}

/* ---------------------------------------------------------------------------
 * Static helper: write_le16()
 * ---------------------------------------------------------------------------
 * Write a 16-bit unsigned integer in little-endian byte order. Returns 2.
 */
static int write_le16(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)(value & 0xFF);
    buf[1] = (uint8_t)((value >> 8) & 0xFF);
    return 2;
}

/* ---------------------------------------------------------------------------
 * Static helper: write_le32()
 * ---------------------------------------------------------------------------
 * Write a 32-bit unsigned integer in little-endian byte order. Returns 4.
 */
static int write_le32(uint8_t *buf, uint32_t value)
{
    buf[0] = (uint8_t)(value & 0xFF);
    buf[1] = (uint8_t)((value >> 8) & 0xFF);
    buf[2] = (uint8_t)((value >> 16) & 0xFF);
    buf[3] = (uint8_t)((value >> 24) & 0xFF);
    return 4;
}

/* ---------------------------------------------------------------------------
 * fargo_build_fs_start()
 * ---------------------------------------------------------------------------
 * Build an Fs start/reset packet with zero-length payload.
 *
 * Wire format (8 bytes):
 *   46 73 01 00 00 00   <- header (magic="Fs", version=0x0001, len=0)
 *   65 50               <- "eP" end-of-packet marker
 */
int fargo_build_fs_start(uint8_t *buf, size_t bufsize)
{
    size_t needed = FARGO_WIRE_FS_START; /* 8 bytes */
    if (bufsize < needed) {
        fprintf(stderr, "ERROR: fargo_build_fs_start: buffer too small "
                "(%zu < %zu)\n", bufsize, needed);
        return -1;
    }

    int pos = 0;
    pos += write_pkt_hdr_print(buf + pos, FARGO_MAGIC_FS, FARGO_PKT_FS_START_LEN);
    pos += write_ep_marker(buf + pos);

    return pos; /* 8 */
}

/* ---------------------------------------------------------------------------
 * fargo_build_fs_init()
 * ---------------------------------------------------------------------------
 * Build the Fs init packet: 8-byte zero payload.
 *
 * Wire format (16 bytes):
 *   46 73 01 00 08 00   <- header (magic="Fs", version=0x0001, len=8)
 *   00 00 00 00 00 00 00 00  <- 8 zero bytes payload
 *   65 50               <- "eP"
 */
int fargo_build_fs_init(uint8_t *buf, size_t bufsize)
{
    size_t needed = FARGO_WIRE_FS_INIT; /* 16 bytes */
    if (bufsize < needed) {
        fprintf(stderr, "ERROR: fargo_build_fs_init: buffer too small "
                "(%zu < %zu)\n", bufsize, needed);
        return -1;
    }

    int pos = 0;
    pos += write_pkt_hdr_print(buf + pos, FARGO_MAGIC_FS, FARGO_PKT_FS_INIT_LEN);

    /* Payload: 8 zero bytes */
    memset(buf + pos, 0x00, FARGO_PKT_FS_INIT_LEN);
    pos += FARGO_PKT_FS_INIT_LEN;

    pos += write_ep_marker(buf + pos);

    return pos; /* 16 */
}

/* ---------------------------------------------------------------------------
 * fargo_build_fg_config()
 * ---------------------------------------------------------------------------
 * Build the 48-byte Fg config packet that describes the print job.
 *
 * CONFIRMED field layout from live USB capture (K_STD, 1 copy, 30-mil):
 *
 *   01 00 00 00  28 00 00 00  22 00 01 00  00 00 01 00
 *   00 00 00 00  00 00 00 00  03 01 00 00  00 00 00 00
 *   00 00 00 00  00 00 00 00  1e 00 00 00  00 00 00 00
 *
 * Fields (all LE):
 *   [0-3]   uint32  copies (1)
 *   [4-7]   uint32  card_size (40=CR80)
 *   [8-9]   uint16  card_height_code (34=CR80)
 *   [10-11] uint16  card_type (1=CR80)
 *   [12-13] uint16  reserved (0)
 *   [14-15] uint16  ribbon_device_id
 *   [16-23] 8 bytes reserved/duplex
 *   [24]    uint8   hopper_flag (0x03 observed)
 *   [25]    uint8   unknown_flag (0x01 observed)
 *   [26-39] 14 bytes reserved
 *   [40-43] uint32  card_thickness_mils (30=0x1E)
 *   [44-47] 4 bytes reserved
 *
 * Wire format (56 bytes total):
 *   46 67 01 00 30 00   <- header (magic="Fg", version=0x0001, len=48)
 *   [48 bytes payload]
 *   65 50               <- "eP"
 */
int fargo_build_fg_config(uint8_t *buf, size_t bufsize, const fargo_job_t *job)
{
    size_t needed = FARGO_WIRE_FG_CONFIG; /* 56 bytes */
    if (bufsize < needed) {
        fprintf(stderr, "ERROR: fargo_build_fg_config: buffer too small "
                "(%zu < %zu)\n", bufsize, needed);
        return -1;
    }
    if (!job) {
        fprintf(stderr, "ERROR: fargo_build_fg_config: NULL job pointer\n");
        return -1;
    }

    int pos = 0;
    pos += write_pkt_hdr_print(buf + pos, FARGO_MAGIC_FG, FARGO_PKT_FG_CONFIG_LEN);

    /* Build the 48-byte payload in a zero-initialised buffer.
     * This guarantees all unknown/reserved fields default to zero. */
    uint8_t payload[FARGO_PKT_FG_CONFIG_LEN];
    memset(payload, 0x00, sizeof(payload));

    /* [0-3] Number of copies -- uint32 LE */
    write_le32(payload + CFG_OFF_COPIES, (uint32_t)(job->copies > 0 ? job->copies : 1));

    /* [4-7] Card size indicator -- uint32 LE (40=CR80, CONFIRMED) */
    write_le32(payload + CFG_OFF_CARD_SIZE, CFG_CARD_SIZE_CR80);

    /* [8-9] Card height code -- uint16 LE (34=CR80, CONFIRMED) */
    write_le16(payload + CFG_OFF_CARD_HEIGHT, CFG_CARD_HEIGHT_CR80);

    /* [10-11] Card type -- uint16 LE (1=CR80, CONFIRMED) */
    write_le16(payload + CFG_OFF_CARD_TYPE, CFG_CARD_TYPE_CR80);

    /* [12-13] Reserved -- already zero */

    /* [14-15] Ribbon device_id -- uint16 LE (CONFIRMED: matches XML device_id) */
    write_le16(payload + CFG_OFF_RIBBON_TYPE, (uint16_t)job->ribbon);

    /* [16-23] Duplex flags -- leave as zero for single-sided
     * TODO: Set appropriate bytes for dual-sided jobs */
    if (job->dual_sided) {
        /* TODO: Verify dual-sided flag byte layout from a dual-sided capture.
         * For now, set byte 16 to 1 as a reasonable guess. */
        payload[16] = 0x01;
    }

    /* [24] Hopper selector flag -- 0x03 observed in all captures */
    payload[CFG_OFF_HOPPER] = 0x03;

    /* [25] Unknown flag -- 0x01 observed in all captures */
    payload[CFG_OFF_FLAG_25] = 0x01;

    /* [26-39] Reserved -- already zero */

    /* [40-43] Card thickness in mils -- uint32 LE (30=0x1E, CONFIRMED) */
    uint16_t thickness = job->card_thickness;
    if (thickness == 0) thickness = FARGO_CARD_THICKNESS_DEFAULT;
    write_le32(payload + CFG_OFF_THICKNESS, (uint32_t)thickness);

    /* [44-47] Reserved -- already zero */

    memcpy(buf + pos, payload, sizeof(payload));
    pos += sizeof(payload);

    pos += write_ep_marker(buf + pos);

    return pos; /* 56 */
}

/* ---------------------------------------------------------------------------
 * fargo_build_fg_command()
 * ---------------------------------------------------------------------------
 * Build a Fg printer command packet (32-byte payload).
 * Used for maintenance: clean, calibrate ribbon, calibrate lamination.
 */
int fargo_build_fg_command(uint8_t *buf, size_t bufsize,
                           fargo_command_t cmd, uint16_t flags)
{
    size_t needed = FARGO_WIRE_SIZE(FARGO_PKT_FG_CMD_LEN); /* 6+32+2=40 */
    if (bufsize < needed) {
        fprintf(stderr, "ERROR: fargo_build_fg_command: buffer too small\n");
        return -1;
    }

    int pos = 0;
    pos += write_pkt_hdr_print(buf + pos, FARGO_MAGIC_FG, FARGO_PKT_FG_CMD_LEN);

    /* 32-byte payload, mostly zeros */
    uint8_t payload[FARGO_PKT_FG_CMD_LEN];
    memset(payload, 0, sizeof(payload));

    /* [0-1] Command version: always 0x0001 */
    write_le16(payload + 0, 0x0001);

    /* [12-13] Command ID */
    write_le16(payload + 12, (uint16_t)cmd);

    /* [14-15] Command flags */
    write_le16(payload + 14, flags);

    memcpy(buf + pos, payload, sizeof(payload));
    pos += sizeof(payload);

    pos += write_ep_marker(buf + pos);

    return pos; /* 40 */
}

/* ---------------------------------------------------------------------------
 * fargo_build_fg_data()
 * ---------------------------------------------------------------------------
 * Build a single Fg image data packet (512-byte payload).
 *
 * Wire format (520 bytes):
 *   46 67 01 00 00 02   <- header (magic="Fg", version=0x0001, len=512)
 *   [512 bytes payload] <- one strip of RLE-compressed image data
 *   65 50               <- "eP"
 */
int fargo_build_fg_data(uint8_t *buf, size_t bufsize, const uint8_t *compressed_data)
{
    size_t needed = FARGO_WIRE_FG_DATA; /* 520 bytes */
    if (bufsize < needed) {
        fprintf(stderr, "ERROR: fargo_build_fg_data: buffer too small "
                "(%zu < %zu)\n", bufsize, needed);
        return -1;
    }
    if (!compressed_data) {
        fprintf(stderr, "ERROR: fargo_build_fg_data: NULL compressed_data\n");
        return -1;
    }

    int pos = 0;
    pos += write_pkt_hdr_print(buf + pos, FARGO_MAGIC_FG, FARGO_PKT_FG_DATA_LEN);

    memcpy(buf + pos, compressed_data, FARGO_PKT_FG_DATA_LEN);
    pos += FARGO_PKT_FG_DATA_LEN;

    pos += write_ep_marker(buf + pos);

    return pos; /* 520 */
}

/* ---------------------------------------------------------------------------
 * fargo_build_fg_eoj()
 * ---------------------------------------------------------------------------
 * Build a Fg end-of-job packet (14-byte payload).
 *
 * CONFIRMED payload: 63 00 00 00 06 00 00 00 00 00 00 00 00 00
 */
int fargo_build_fg_eoj(uint8_t *buf, size_t bufsize)
{
    size_t needed = FARGO_WIRE_FG_EOJ; /* 22 bytes */
    if (bufsize < needed) {
        fprintf(stderr, "ERROR: fargo_build_fg_eoj: buffer too small\n");
        return -1;
    }

    int pos = 0;
    pos += write_pkt_hdr_print(buf + pos, FARGO_MAGIC_FG, FARGO_PKT_FG_EOJ_LEN);

    /* 14-byte EOJ payload */
    uint8_t payload[FARGO_PKT_FG_EOJ_LEN];
    memset(payload, 0, sizeof(payload));

    write_le16(payload + 0, 0x0063);  /* EOJ marker */
    write_le16(payload + 4, 0x0006);  /* Section type */

    memcpy(buf + pos, payload, sizeof(payload));
    pos += sizeof(payload);

    pos += write_ep_marker(buf + pos);

    return pos; /* 22 */
}

/* ---------------------------------------------------------------------------
 * fargo_build_ep()
 * ---------------------------------------------------------------------------
 * Build the standalone 2-byte "eP" terminator.
 */
int fargo_build_ep(uint8_t *buf, size_t bufsize)
{
    if (bufsize < FARGO_PKT_EP_SIZE) {
        return -1;
    }
    return write_ep_marker(buf);
}

/* ---------------------------------------------------------------------------
 * fargo_build_status_query()
 * ---------------------------------------------------------------------------
 * Build the 43-byte data payload for the vendor control OUT transfer
 * that queries printer status.
 *
 * CONFIRMED format from live USB capture:
 *   Byte 0-2:   00 00 00          (3-byte prefix, purpose unknown)
 *   Byte 3-8:   46 67 00 00 20 00 (Fg header: magic="Fg", version=0x0000, len=0x0020=32)
 *   Byte 9-40:  32 zero bytes     (Fg payload: status request -- all zeros)
 *   Byte 41-42: 65 50             (eP terminator)
 *
 * Total: 3 + 6 + 32 + 2 = 43 bytes
 */
int fargo_build_status_query(uint8_t *buf, size_t bufsize)
{
    if (bufsize < FARGO_CTRL_STATUS_OUT_LEN) {
        fprintf(stderr, "ERROR: fargo_build_status_query: buffer too small "
                "(%zu < %d)\n", bufsize, FARGO_CTRL_STATUS_OUT_LEN);
        return -1;
    }

    int pos = 0;

    /* 3-byte prefix (all zeros) */
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;

    /* Fg header with STATUS version (0x0000), payload length = 32 */
    pos += write_pkt_hdr(buf + pos, FARGO_MAGIC_FG, FRL_VERSION_STATUS,
                         FARGO_PKT_FG_STATUS_LEN);

    /* 32 zero bytes payload (the status request) */
    memset(buf + pos, 0x00, FARGO_PKT_FG_STATUS_LEN);
    pos += FARGO_PKT_FG_STATUS_LEN;

    /* eP terminator */
    pos += write_ep_marker(buf + pos);

    return pos; /* 43 */
}

/* ---------------------------------------------------------------------------
 * Static helper: parse_flag_value()
 * ---------------------------------------------------------------------------
 * Given a status string like "DH:1;IH:0;PE:0;", extract the integer value
 * for the given flag name (e.g., "DH").
 *
 * Returns the value (>= 0) if found, or -1 if the flag is not present.
 */
static int parse_flag_value(const char *status_str, const char *flag_name)
{
    /* Build the search pattern "XX:" */
    char pattern[8];
    snprintf(pattern, sizeof(pattern), "%s:", flag_name);

    const char *found = strstr(status_str, pattern);
    if (!found) {
        return -1; /* Flag not found in status string */
    }

    /* Skip past the "XX:" to get to the value */
    found += strlen(pattern);

    /* Parse the integer value (terminated by ';' or '\0') */
    return atoi(found);
}

/* ---------------------------------------------------------------------------
 * fargo_parse_status_response()
 * ---------------------------------------------------------------------------
 * Parse the status response from a vendor control IN transfer.
 *
 * CONFIRMED response format from live USB capture:
 *   The response is an Fg packet (version=0x0000) whose payload contains
 *   two null-terminated ASCII strings:
 *
 *   1. Command result: "USR_CMD_SUCCESSFUL"
 *   2. Status flags:   "DH:1;IH:0;LM:0;FL:1;MG:0;SM:0;SG:0;DS:0;SC:0;MF:0;HD:0;IC:0;PE:0;"
 *
 *   The Fg packet is followed by "eP" (0x65 0x50).
 *
 * Returns 0 on success, -1 on parse error.
 */
int fargo_parse_status_response(const uint8_t *data, size_t len,
                                fargo_status_t *status)
{
    if (!data || !status) {
        return -1;
    }

    /* Clear the status structure */
    memset(status, 0, sizeof(fargo_status_t));

    /* We need at least the Fg header (6 bytes) to parse */
    if (len < FARGO_PKT_HDR_SIZE) {
        fprintf(stderr, "ERROR: fargo_parse_status_response: response too short "
                "(%zu bytes)\n", len);
        return -1;
    }

    /* Verify the magic bytes are "Fg" (0x46 0x67) */
    if (data[0] != FARGO_MARKER_FG_0 || data[1] != FARGO_MARKER_FG_1) {
        fprintf(stderr, "ERROR: fargo_parse_status_response: bad magic "
                "(0x%02x 0x%02x), expected Fg\n", data[0], data[1]);
        return -1;
    }

    /* Extract payload length (bytes 4-5, LE) */
    uint16_t payload_len = (uint16_t)(data[4] | (data[5] << 8));

    /* Sanity check: payload_len + header + eP should not exceed response size */
    if ((size_t)(FARGO_PKT_HDR_SIZE + payload_len) > len) {
        fprintf(stderr, "DEBUG: fargo_parse_status_response: payload_len=%u but "
                "only %zu bytes available, parsing what we have\n",
                payload_len, len);
        payload_len = (uint16_t)(len - FARGO_PKT_HDR_SIZE);
    }

    /* The payload starts at offset 6 (after the 6-byte header) */
    const uint8_t *payload = data + FARGO_PKT_HDR_SIZE;

    /* Copy the payload as a raw string for debugging.
     * The payload contains null-terminated strings; we copy up to 255 chars. */
    size_t copy_len = (payload_len < sizeof(status->raw_status) - 1)
                      ? payload_len : sizeof(status->raw_status) - 1;
    memcpy(status->raw_status, payload, copy_len);
    status->raw_status[copy_len] = '\0';

    fprintf(stderr, "DEBUG: fargo_parse_status_response: raw payload (%u bytes)\n",
            payload_len);

    /* The first string is the command result (e.g., "USR_CMD_SUCCESSFUL") */
    const char *cmd_result = (const char *)payload;
    status->cmd_successful = (strstr(cmd_result, "USR_CMD_SUCCESSFUL") != NULL);

    /* Find the second string (status flags) by scanning past the first null.
     * The status flags look like: "DH:1;IH:0;LM:0;FL:1;..." */
    const char *flags_str = NULL;
    for (size_t i = 0; i < payload_len; i++) {
        if (payload[i] == '\0' && (i + 1) < payload_len) {
            /* Found the end of first string; next string starts after it */
            flags_str = (const char *)(payload + i + 1);
            break;
        }
    }

    if (flags_str) {
        fprintf(stderr, "DEBUG: fargo_parse_status_response: flags = \"%s\"\n",
                flags_str);

        /* Parse each known flag */
        int val;

        val = parse_flag_value(flags_str, "DH");
        status->door_hood = (val >= 0) ? val : 0;

        val = parse_flag_value(flags_str, "IH");
        status->input_hopper = (val >= 0) ? val : 0;

        val = parse_flag_value(flags_str, "LM");
        status->laminator = (val >= 0) ? val : 0;

        val = parse_flag_value(flags_str, "FL");
        status->feed_laminator = (val >= 0) ? val : 0;

        val = parse_flag_value(flags_str, "MG");
        status->mag_busy = (val >= 0) ? val : 0;

        val = parse_flag_value(flags_str, "SM");
        status->smart_card = (val >= 0) ? val : 0;

        val = parse_flag_value(flags_str, "PE");
        status->paper_empty = (val >= 0) ? val : 0;

        val = parse_flag_value(flags_str, "SG");
        status->sg_flag = (val >= 0) ? val : 0;

        val = parse_flag_value(flags_str, "DS");
        status->ds_flag = (val >= 0) ? val : 0;

        val = parse_flag_value(flags_str, "SC");
        status->sc_flag = (val >= 0) ? val : 0;

        val = parse_flag_value(flags_str, "MF");
        status->mf_flag = (val >= 0) ? val : 0;

        val = parse_flag_value(flags_str, "HD");
        status->hd_flag = (val >= 0) ? val : 0;

        val = parse_flag_value(flags_str, "IC");
        status->ic_flag = (val >= 0) ? val : 0;
    } else {
        fprintf(stderr, "DEBUG: fargo_parse_status_response: no flags string found\n");
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * fargo_build_job_preamble()
 * ---------------------------------------------------------------------------
 * Build the preamble portion of a print job into a single buffer.
 * This includes:
 *   1. Fs start packet (8 bytes)    -- wake/reset printer
 *   2. Fs init packet (16 bytes)    -- initialise session
 *   3. Fg config packet (56 bytes)  -- job configuration
 *
 * Total: 80 bytes (for single-sided)
 *
 * The caller should append image data packets and end-of-job after this.
 *
 * Returns total bytes written, or -1 on error.
 */
int fargo_build_job_preamble(uint8_t *buf, size_t bufsize, const fargo_job_t *job)
{
    /* We need: Fs start (8) + Fs init (16) + Fg config (56) = 80 bytes */
    size_t needed = FARGO_WIRE_FS_START + FARGO_WIRE_FS_INIT + FARGO_WIRE_FG_CONFIG;
    if (bufsize < needed) {
        fprintf(stderr, "ERROR: fargo_build_job_preamble: buffer too small "
                "(%zu < %zu)\n", bufsize, needed);
        return -1;
    }

    int pos = 0;
    int rc;

    /* Packet 1: Fs start (job begin / wake printer) */
    rc = fargo_build_fs_start(buf + pos, bufsize - (size_t)pos);
    if (rc < 0) return -1;
    pos += rc;

    /* Packet 2: Fs init (initialise session with 8 zero bytes) */
    rc = fargo_build_fs_init(buf + pos, bufsize - (size_t)pos);
    if (rc < 0) return -1;
    pos += rc;

    /* Packet 3: Fg config (job parameters) */
    rc = fargo_build_fg_config(buf + pos, bufsize - (size_t)pos, job);
    if (rc < 0) return -1;
    pos += rc;

    fprintf(stderr, "DEBUG: fargo_build_job_preamble: built %d bytes\n", pos);
    return pos;
}

/* ---------------------------------------------------------------------------
 * fargo_rle_compress()
 * ---------------------------------------------------------------------------
 * FRL run-length encoding compressor.
 *
 * Format: Each output PAIR of bytes is (count-1, value), which the printer
 * expands to (count-1 + 1) = count identical copies of value.
 *
 *   Input:  [0x00, 0x00, 0x00, 0xFF, 0xFF]
 *   Output: [0x02, 0x00,  0x01, 0xFF]
 *            ^^^3x0x00^^^  ^^^2x0xFF^^^
 *
 * Maximum run length per pair: 256 bytes (count byte = 0xFF = 255+1 = 256).
 * CONFIRMED: Max count is 0xFF (256 repetitions), not 0x7F (128).
 *
 * Returns number of bytes written to output, or -1 on overflow.
 */
int fargo_rle_compress(const uint8_t *input, size_t input_len,
                       uint8_t *output, size_t output_max)
{
    if (!input || !output) {
        return -1;
    }
    if (input_len == 0) {
        return 0;
    }

    size_t in_pos  = 0;
    size_t out_pos = 0;

    while (in_pos < input_len) {
        uint8_t current = input[in_pos];

        /* Count consecutive matching bytes, up to 256 max (count byte 0xFF) */
        size_t run = 1;
        while (run < FARGO_RLE_MAX_RUN &&
               (in_pos + run) < input_len &&
               input[in_pos + run] == current)
        {
            run++;
        }

        /* Need 2 bytes of output for this (count, value) pair */
        if (out_pos + 2 > output_max) {
            fprintf(stderr, "ERROR: fargo_rle_compress: output buffer overflow "
                    "at input offset %zu\n", in_pos);
            return -1;
        }

        output[out_pos++] = (uint8_t)(run - 1);  /* count-1 */
        output[out_pos++] = current;               /* value */

        in_pos += run;
    }

    return (int)out_pos;
}

/* ---------------------------------------------------------------------------
 * fargo_rle_decompress()
 * ---------------------------------------------------------------------------
 * Decode RLE-compressed data. Each pair (count, value) expands to
 * (count+1) copies of value.
 *
 * Returns bytes written to output, or -1 on overflow.
 */
int fargo_rle_decompress(const uint8_t *rle_data, size_t rle_len,
                         uint8_t *output, size_t output_max)
{
    if (!rle_data || !output) {
        return -1;
    }

    size_t in_pos  = 0;
    size_t out_pos = 0;

    while (in_pos + 1 < rle_len) {
        uint8_t count = rle_data[in_pos];       /* repetitions - 1 */
        uint8_t value = rle_data[in_pos + 1];   /* byte to repeat */
        size_t  run   = (size_t)count + 1;

        if (out_pos + run > output_max) {
            fprintf(stderr, "ERROR: fargo_rle_decompress: output overflow\n");
            return -1;
        }

        memset(output + out_pos, value, run);
        out_pos += run;
        in_pos  += 2;
    }

    return (int)out_pos;
}

/* ---------------------------------------------------------------------------
 * fargo_send_packet()
 * ---------------------------------------------------------------------------
 * Send a pre-built packet buffer (including header, payload, and eP marker)
 * over USB in a single bulk transfer.
 */
int fargo_send_packet(fargo_device_t *dev, const uint8_t *buf, size_t len)
{
    if (!buf || len == 0) {
        fprintf(stderr, "ERROR: fargo_send_packet: empty buffer\n");
        return -1;
    }

    if (fargo_usb_send(dev, buf, len) < 0) {
        fprintf(stderr, "ERROR: fargo_send_packet: USB send failed\n");
        return -1;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * fargo_ribbon_panel_count()
 * ---------------------------------------------------------------------------
 * Get the number of front-side panels for a ribbon type.
 * This determines how many colour planes the driver must generate.
 */
int fargo_ribbon_panel_count(fargo_ribbon_type_t ribbon)
{
    switch (ribbon) {
        case FARGO_RIBBON_NONE:       return 2;  /* RW_erase + RW_black */
        case FARGO_RIBBON_K_STD:      return 1;  /* K_resin */
        case FARGO_RIBBON_K_PRM:      return 1;  /* K_resin */
        case FARGO_RIBBON_MONO_COLOR: return 1;  /* K_color_resin */
        case FARGO_RIBBON_METALIC:    return 1;  /* K_color_resin */
        case FARGO_RIBBON_KO:         return 2;  /* K + O */
        case FARGO_RIBBON_BO:         return 2;  /* B + O */
        case FARGO_RIBBON_YMCKO:      return 5;  /* Y + M + C + K + O */
        case FARGO_RIBBON_YMCKO_HALF: return 5;  /* Y(half) + M(half) + C(half) + K + O */
        case FARGO_RIBBON_YMCKOK:     return 5;  /* Front: Y + M + C + K + O (back K separate) */
        case FARGO_RIBBON_YMCKK:      return 4;  /* Y + M + C + K (back K separate) */
        case FARGO_RIBBON_YMCFKO:     return 6;  /* Y + M + C + F + K + O */
        case FARGO_RIBBON_YMCFKOK:    return 6;  /* Front: Y + M + C + F + K + O (back K separate) */
        default:
            fprintf(stderr, "ERROR: fargo_ribbon_panel_count: unknown ribbon %d\n",
                    (int)ribbon);
            return 1;
    }
}

/* ---------------------------------------------------------------------------
 * fargo_ribbon_panel_id()
 * ---------------------------------------------------------------------------
 * Get the panel device_id for a given ribbon type and panel index.
 * Panel index 0 is the first panel printed.
 */
fargo_panel_id_t fargo_ribbon_panel_id(fargo_ribbon_type_t ribbon, int index)
{
    /* Define panel sequences for each ribbon type */
    static const fargo_panel_id_t ymcko_panels[] = {
        FARGO_PANEL_YELLOW, FARGO_PANEL_MAGENTA, FARGO_PANEL_CYAN,
        FARGO_PANEL_BLACK_RESIN, FARGO_PANEL_OVERLAY
    };
    static const fargo_panel_id_t ymcfko_panels[] = {
        FARGO_PANEL_YELLOW, FARGO_PANEL_MAGENTA, FARGO_PANEL_CYAN,
        FARGO_PANEL_FLUORESCENT, FARGO_PANEL_BLACK_RESIN, FARGO_PANEL_OVERLAY
    };

    switch (ribbon) {
        case FARGO_RIBBON_YMCKO:
        case FARGO_RIBBON_YMCKO_HALF:
        case FARGO_RIBBON_YMCKOK:
            if (index >= 0 && index < 5) return ymcko_panels[index];
            break;
        case FARGO_RIBBON_YMCKK:
            if (index >= 0 && index < 4) return ymcko_panels[index]; /* Y,M,C,K */
            break;
        case FARGO_RIBBON_YMCFKO:
        case FARGO_RIBBON_YMCFKOK:
            if (index >= 0 && index < 6) return ymcfko_panels[index];
            break;
        case FARGO_RIBBON_K_STD:
        case FARGO_RIBBON_K_PRM:
        case FARGO_RIBBON_METALIC:
            if (index == 0) return FARGO_PANEL_BLACK_RESIN;
            break;
        case FARGO_RIBBON_MONO_COLOR:
            if (index == 0) return FARGO_PANEL_BLACK_RESIN;
            break;
        case FARGO_RIBBON_KO:
            if (index == 0) return FARGO_PANEL_BLACK_RESIN;
            if (index == 1) return FARGO_PANEL_OVERLAY;
            break;
        case FARGO_RIBBON_BO:
            if (index == 0) return FARGO_PANEL_BLACK_DYESUB;
            if (index == 1) return FARGO_PANEL_OVERLAY;
            break;
        case FARGO_RIBBON_NONE:
            if (index == 0) return FARGO_PANEL_RW_ERASE;
            if (index == 1) return FARGO_PANEL_RW_BLACK;
            break;
        default:
            break;
    }

    fprintf(stderr, "ERROR: fargo_ribbon_panel_id: invalid ribbon=%d index=%d\n",
            (int)ribbon, index);
    return FARGO_PANEL_BLACK_RESIN; /* safe fallback */
}

/* ---------------------------------------------------------------------------
 * fargo_ribbon_is_dual_config()
 * ---------------------------------------------------------------------------
 * Returns true if the ribbon type requires a dual-sided (50-byte) config.
 * YMCKOK and YMCKK have back-side panels.
 */
bool fargo_ribbon_is_dual_config(fargo_ribbon_type_t ribbon)
{
    switch (ribbon) {
        case FARGO_RIBBON_YMCKOK:
        case FARGO_RIBBON_YMCKK:
        case FARGO_RIBBON_YMCFKOK:
            return true;
        default:
            return false;
    }
}
