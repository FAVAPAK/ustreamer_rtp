#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct us_rtp_jpeg_s us_rtp_jpeg_s;

us_rtp_jpeg_s *us_rtp_jpeg_open(
    const char *multicast_ip,
    uint16_t port,
    unsigned int ttl
);

void us_rtp_jpeg_send(
    us_rtp_jpeg_s *rtp,
    const uint8_t *jpeg,
    size_t jpeg_size,
    unsigned int width,
    unsigned int height
);

void us_rtp_jpeg_close(us_rtp_jpeg_s *rtp);