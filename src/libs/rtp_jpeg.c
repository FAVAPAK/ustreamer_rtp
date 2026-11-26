
#include "rtp_jpeg.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#define RTP_MTU       1472
#define RTP_HEADER    12
#define JPEG_HEADER   8
#define JPEG_Q_HEADER 4

#define RTP_PT_JPEG   26
#define RTP_CLOCK     90000
#define DEFAULT_FPS   25

struct us_rtp_jpeg_s {
    int fd;
    struct sockaddr_in addr;

    uint16_t seq;
    uint32_t timestamp;
    uint32_t ssrc;
};

typedef struct {
    uint8_t table[2][64];
    bool present[2];
} jpeg_quant_tables_t;

static uint16_t be16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put24(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 16);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/*
 * Parse JPEG DQT segments.
 *
 * RFC 2435 transmits the 64 quantization values for each table.
 * The JPEG DQT descriptor byte is NOT transmitted.
 *
 * This implementation supports 8-bit quantization tables 0 and 1.
 */
static bool parse_dqt(
    const uint8_t *data,
    size_t length,
    jpeg_quant_tables_t *qt
) {
    size_t pos = 0;

    while (pos < length) {
        uint8_t descriptor = data[pos++];

        unsigned int precision = descriptor >> 4;
        unsigned int id = descriptor & 0x0f;

        if (precision != 0 || id > 1)
            return false;

        if (pos + 64 > length)
            return false;

        memcpy(qt->table[id], data + pos, 64);
        qt->present[id] = true;

        pos += 64;
    }

    return true;
}

/*
 * Parse baseline JPEG SOF0.
 *
 * Supported:
 *   Type 0: Y 2x1, Cb 1x1, Cr 1x1 (4:2:2)
 *   Type 1: Y 2x2, Cb 1x1, Cr 1x1 (4:2:0)
 */
static bool parse_sof0(
    const uint8_t *data,
    size_t length,
    unsigned int *type
) {
    if (length < 15)
        return false;

    if (data[0] != 8)
        return false;

    unsigned int components = data[5];

    if (components != 3 || length < 6 + components * 3)
        return false;

    unsigned int y_sampling = data[7];
    unsigned int cb_sampling = data[10];
    unsigned int cr_sampling = data[13];

    if (cb_sampling != 0x11 || cr_sampling != 0x11)
        return false;

    if (y_sampling == 0x21) {
        *type = 0;
        return true;
    }

    if (y_sampling == 0x22) {
        *type = 1;
        return true;
    }

    return false;
}

/*
 * Find the JPEG entropy-coded scan.
 *
 * FF00 byte stuffing is retained.
 * Restart markers and additional scan markers are rejected because
 * this implementation does not implement RFC 2435 restart headers.
 *
 * EOI is excluded from the RTP payload.
 * A missing EOI is accepted because some V4L2 MJPEG cameras omit it.
 */
static bool find_scan_end(
    const uint8_t *jpeg,
    size_t size,
    size_t scan_start,
    size_t *scan_end
) {
    size_t p = scan_start;

    while (p < size) {
        if (jpeg[p] != 0xff) {
            p++;
            continue;
        }

        if (p + 1 >= size)
            return false;

        uint8_t marker = jpeg[p + 1];

        if (marker == 0x00) {
            /* Entropy-coded byte stuffing. */
            p += 2;
            continue;
        }

        if (marker >= 0xd0 && marker <= 0xd7) {
            /* Restart markers require RFC 2435 restart headers. */
            return false;
        }

        if (marker == 0xd9) {
            *scan_end = p;
            return true;
        }

        /* Multiple scans and unexpected markers are unsupported. */
        return false;
    }

    *scan_end = size;
    return true;
}

/*
 * Open multicast RTP/JPEG output.
 */
us_rtp_jpeg_s *us_rtp_jpeg_open(
    const char *ip,
    uint16_t port,
    unsigned int ttl
) {
    if (!ip || port == 0 || ttl < 1 || ttl > 255)
        return NULL;

    us_rtp_jpeg_s *r = calloc(1, sizeof(*r));

    if (!r)
        return NULL;

    r->fd = -1;

    r->fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (r->fd < 0)
        goto fail;

    unsigned char multicast_ttl = (unsigned char)ttl;

    if (setsockopt(
            r->fd,
            IPPROTO_IP,
            IP_MULTICAST_TTL,
            &multicast_ttl,
            sizeof(multicast_ttl)
        ) < 0)
        goto fail;

    memset(&r->addr, 0, sizeof(r->addr));

    r->addr.sin_family = AF_INET;
    r->addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &r->addr.sin_addr) != 1)
        goto fail;

    if (!IN_MULTICAST(ntohl(r->addr.sin_addr.s_addr)))
        goto fail;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    r->seq = (uint16_t)(ts.tv_nsec ^ getpid());
    r->ssrc = (uint32_t)(ts.tv_sec ^ ts.tv_nsec ^ getpid());
    r->timestamp = (uint32_t)(ts.tv_nsec + ts.tv_sec);

    return r;

fail:
    if (r->fd >= 0)
        close(r->fd);

    free(r);
    return NULL;
}

/*
 * Send one baseline JPEG frame using RFC 2435 RTP/JPEG.
 *
 * Assumes 25 FPS:
 *   90000 / 25 = 3600 timestamp units per frame.
 */
void us_rtp_jpeg_send(
    us_rtp_jpeg_s *r,
    const uint8_t *jpeg,
    size_t size,
    unsigned int width,
    unsigned int height
) {
    if (!r || !jpeg || size < 4)
        return;

    if (width == 0 || height == 0 ||
        width > 2040 || height > 2040)
        return;

    if (jpeg[0] != 0xff || jpeg[1] != 0xd8)
        return;

    jpeg_quant_tables_t qt;
    memset(&qt, 0, sizeof(qt));

    unsigned int type = 0;
    bool found_sof = false;
    bool found_sos = false;

    size_t scan_start = 0;
    size_t p = 2;

    /*
     * Parse JPEG markers until SOS.
     */
    while (p + 2 <= size) {
        if (jpeg[p] != 0xff)
            return;

        while (p < size && jpeg[p] == 0xff)
            p++;

        if (p >= size)
            return;

        uint8_t marker = jpeg[p++];

        if (marker == 0xd9 || marker == 0xd8 ||
            marker == 0x01 ||
            (marker >= 0xd0 && marker <= 0xd7))
            continue;

        if (p + 2 > size)
            return;

        unsigned int len = be16(jpeg + p);

        if (len < 2 || p + len > size)
            return;

        const uint8_t *data = jpeg + p + 2;
        size_t data_len = len - 2;

        if (marker == 0xdb) {
            if (!parse_dqt(data, data_len, &qt))
                return;
        }
        else if (marker == 0xc0) {
            if (!parse_sof0(data, data_len, &type))
                return;

            found_sof = true;
        }
        else if (marker == 0xc2) {
            /* Progressive JPEG is not supported. */
            return;
        }
        else if (marker == 0xdd) {
            /*
             * DRI indicates restart intervals.
             * Restart marker packetization is not implemented.
             */
            if (data_len != 2 || be16(data) != 0)
                return;
        }
        else if (marker == 0xda) {
            if (!found_sof || data_len < 6)
                return;

            /*
             * Baseline sequential scan:
             * Ss = 0, Se = 63, Ah = 0, Al = 0.
             */
            unsigned int components = data[0];

            if (components != 3 || data_len < 1 + components * 2 + 3)
                return;

            size_t spectral = 1 + components * 2;

            if (data[spectral] != 0 ||
                data[spectral + 1] != 63 ||
                data[spectral + 2] != 0)
                return;

            scan_start = p + len;
            found_sos = true;
            break;
        }

        p += len;
    }

    if (!found_sof || !found_sos)
        return;

    /*
     * Table 0 is required for luminance.
     */
    if (!qt.present[0])
        return;

    /*
     * Build RFC 2435 quantization-table data.
     *
     * The dynamic quantization header carries raw table values,
     * without JPEG DQT descriptor bytes.
     */
    uint8_t qdata[128];
    unsigned int qlen = 64;

    memcpy(qdata, qt.table[0], 64);

    if (qt.present[1]) {
        memcpy(qdata + 64, qt.table[1], 64);
        qlen = 128;
    }

    size_t scan_end;

    if (!find_scan_end(jpeg, size, scan_start, &scan_end))
        return;

    if (scan_end <= scan_start)
        return;

    size_t scan_size = scan_end - scan_start;

    /*
     * RFC 2435 fragment offset is 24 bits.
     */
    if (scan_size > 0xFFFFFF)
        return;

    /*
     * Dynamic quantization tables use Q = 128.
     */
    const uint8_t qid = 128;

    const size_t quant_header_size = JPEG_Q_HEADER + qlen;

    uint8_t packet[RTP_MTU];

    size_t offset = 0;

    /*
     * All fragments of one JPEG frame use the same RTP timestamp.
     */
    uint32_t frame_timestamp = r->timestamp;

    while (offset < scan_size) {
        size_t extra = (offset == 0) ? quant_header_size : 0;

        size_t room =
            RTP_MTU - RTP_HEADER - JPEG_HEADER - extra;

        if (room == 0)
            return;

        size_t chunk = scan_size - offset;

        if (chunk > room)
            chunk = room;

        bool last = (offset + chunk == scan_size);

        memset(packet, 0, RTP_HEADER + JPEG_HEADER);

        /*
         * RTP header.
         */
        packet[0] = 0x80;
        packet[1] = RTP_PT_JPEG | (last ? 0x80 : 0);

        put16(packet + 2, r->seq++);
        put32(packet + 4, frame_timestamp);
        put32(packet + 8, r->ssrc);

        /*
         * RFC 2435 JPEG header.
         */
        uint8_t *jh = packet + RTP_HEADER;

        jh[0] = 0;              /* Type-specific */
        put24(jh + 1, (uint32_t)offset);
        jh[4] = (uint8_t)type;
        jh[5] = qid;

        jh[6] = (uint8_t)((width + 7) / 8);
        jh[7] = (uint8_t)((height + 7) / 8);

        size_t pos = RTP_HEADER + JPEG_HEADER;

        /*
         * Dynamic quantization-table header:
         * MBZ = 0
         * Precision = 0 (8-bit tables)
         * Length = 64 or 128
         */
        if (offset == 0) {
            packet[pos++] = 0;
            packet[pos++] = 0;

            put16(packet + pos, (uint16_t)qlen);
            pos += 2;

            memcpy(packet + pos, qdata, qlen);
            pos += qlen;
        }

        /*
         * JPEG entropy-coded scan fragment.
         */
        memcpy(
            packet + pos,
            jpeg + scan_start + offset,
            chunk
        );

        ssize_t sent = sendto(
            r->fd,
            packet,
            pos + chunk,
            0,
            (struct sockaddr *)&r->addr,
            sizeof(r->addr)
        );

        if (sent != (ssize_t)(pos + chunk))
            return;

        offset += chunk;
    }

    /*
     * Advance timestamp once per frame, not once per packet.
     */
    r->timestamp += RTP_CLOCK / DEFAULT_FPS;
}

/*
 * Close RTP output.
 */
void us_rtp_jpeg_close(us_rtp_jpeg_s *r) {
    if (!r)
        return;

    if (r->fd >= 0)
        close(r->fd);

    free(r);
}
