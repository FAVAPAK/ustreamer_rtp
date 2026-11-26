
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include <event2/event.h>

#include "../libs/types.h"
#include "../libs/queue.h"
#include "../libs/ring.h"
#include "../libs/frame.h"
#include "../libs/memsink.h"
#include "../libs/capture.h"
#include "../libs/fpsi.h"
#include "../libs/rtp_jpeg.h"

#ifdef WITH_V4P
#include "drm.h"
#endif

#include "blank.h"
#include "encoder.h"
#include "m2m.h"

/*
 * HTTP stream runtime state
 */
typedef struct {
#ifdef WITH_V4P
    atomic_bool drm_live;
    us_fpsi_s *drm_fpsi;
#endif

    atomic_bool h264_online;
    us_fpsi_s *h264_fpsi;

    struct event *jpeg_refresher;
    us_ring_s *jpeg_ring;

    atomic_bool has_clients;
    atomic_uint snapshot_requested;
    atomic_ullong last_req_ts;

    us_fpsi_s *captured_fpsi;
} us_stream_http_s;

/*
 * Stream runtime state
 */
typedef struct {
    us_stream_http_s *http;

    us_m2m_encoder_s *h264_enc;
    us_frame_s *h264_tmp_src;
    us_frame_s *h264_dest;
    bool h264_key_requested;

    us_blank_s *blank;
    us_fpsi_meta_s notify_meta;

    atomic_bool stop;
} us_stream_runtime_s;

/*
 * Main stream configuration and state
 */
typedef struct {
    us_capture_s *cap;
    us_encoder_s *enc;

    uint desired_fps;
    bool notify_parent;
    bool slowdown;
    uint error_delay;
    bool exit_on_device_error;
    uint exit_on_no_clients;

    char *governor_idle;
    char *governor_active;

    us_memsink_s *jpeg_sink;
    us_memsink_s *raw_sink;
    us_memsink_s *h264_sink;

    uint h264_bitrate;
    uint h264_gop;
    char *h264_m2m_path;
    bool h264_boost;

#ifdef WITH_V4P
    us_drm_s *drm;
#endif

    /* RTP/JPEG multicast */
    char *rtp_multicast;
    uint rtp_ttl;
    us_rtp_jpeg_s *rtp_jpeg;

    us_stream_runtime_s *run;
} us_stream_s;

/*
 * Public stream functions
 */
us_stream_s *us_stream_init(us_capture_s *cap, us_encoder_s *enc);

void us_stream_update_blank(
    us_stream_s *stream,
    const us_capture_s *cap
);

void us_stream_destroy(us_stream_s *stream);

void us_stream_loop(us_stream_s *stream);

void us_stream_loop_break(us_stream_s *stream);
