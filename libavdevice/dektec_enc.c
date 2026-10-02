/*
 * DekTec hardware output
 * Copyright (c) 2022-2024 DekTec, Jeroen Steendam
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file dektec_enc.c
 * DekTec hardware output
 * @author Jeroen Steendam
 */

#include "avdevice.h"
#include "dektec_common.h"
#include "libavcodec/codec_desc.h"
#include "libavformat/avformat.h"
#include "libavformat/sdicommon.h"
#include "libavformat/mux.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/imgutils.h"
#include "libavutil/x86/cpu.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"

#include "cdtapi.h"
#include "cdtapi_avfifo.h"

#include "config.h"

#if CONFIG_LIBCDTAPI_NMOS
#include "dektec_nmos.h"
#endif
#if HAVE_INTRINSICS_SSE2
#include <emmintrin.h> // SSE2 intrinsics
#include <immintrin.h> // Other intrinsics
#if defined(__GNUC__)
#pragma GCC target("ssse3")
#endif
#endif

#define MAX_AUDIO_STREAMS 8
#define MAX_VIDEO_STREAMS 1
#define MAX_STREAMS MAX_AUDIO_STREAMS + MAX_VIDEO_STREAMS

typedef struct DekTecMuxContext {
    const AVClass *av_class;

    int port;
    int64_t serial_number;
    DtDevice* device;
    DtOutpChannel* output;
    int is_preloading;

    void (*copy)(const AVFrame *src, AvFifo_Frame *dst);
    void (*copy_interlaced)(const AVFrame *src, AvFifo_Frame *field0, AvFifo_Frame *field1);

    AvFifo_TxFifo **fifos;

    DtTimeOfDay start_tod;
    int64_t start_time[MAX_STREAMS];
    int64_t max_buffer_time;
    AVRational time_scale;

    char* option_standard;          ///< option sdi standard
    int aspp;                       // Audio samples per packet
    int pf;                         // Pixel format
    int pm;                         // Packing mode
    int ps;                         // RTP payload size
    int pt;                         // RTP payload type
    int sch;                        // Scheduling mode
    int threads;                    // The threads option: FF_DEKTEC_THREADS_AUTO, 1 or 2+
    char *url_v;                    // Video URL
    char *url_a[MAX_AUDIO_STREAMS]; // Audio URLS
    int is_interlaced;

    int avio_buffer_size;
    uint8_t *avio_buffer;
    AVIOContext *avio;

    struct SwsContext *scale_context;
    AVFrame *scale_frame;
    int dst_format;

    AVFormatContext *format_context;

    int (*write_header)(struct AVFormatContext *);
    int (*write_packet)(struct AVFormatContext *, AVPacket *pkt);
    int (*write_trailer)(struct AVFormatContext *);

#if CONFIG_LIBCDTAPI_NMOS
    char *nmos_registry;            // The NMOS registry, or "auto"; NULL or empty for none
    char *nmos_label;               // The NMOS node's label; NULL for the default
    char *nmos_host;                // The address the node's APIs are reached at
    int nmos_port;                  // The port of the node's APIs; 0 for any free one
    FFDektecNmos *nmos;             // The node, while the output is open
#endif
} DekTecMuxContext;

#define OFFSET(x) (int)offsetof(DekTecMuxContext, x)
#define AUDIO_ENC_FLAGS AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM
#define VIDEO_ENC_FLAGS AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
#define ENC_FLAGS VIDEO_ENC_FLAGS | AUDIO_ENC_FLAGS
#define St2110_PackingMode_Line 2
static const AVOption options[] = {
    { "sdi_standard", "", OFFSET(option_standard), AV_OPT_TYPE_STRING, {.str = ""}, 0, 0, AV_OPT_FLAG_ENCODING_PARAM, NULL},
    { "threads", "threads an SDI frame is coded over: auto, 1 for one, or more", OFFSET(threads), AV_OPT_TYPE_INT, { .i64 = FF_DEKTEC_THREADS_AUTO }, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, "threads"},
    { "auto", "4 threads, and as many pieces as the standard calls for", 0, AV_OPT_TYPE_CONST, { .i64 = FF_DEKTEC_THREADS_AUTO }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM, "threads"},

    { "aspp", "Number of audio samples per packet", OFFSET(aspp), AV_OPT_TYPE_INT, {.i64 = 48}, 1, INT_MAX, AUDIO_ENC_FLAGS, NULL},

    { "pf", "Pixel format", OFFSET(pf), AV_OPT_TYPE_INT, {.i64 = -1}, -1, 10, VIDEO_ENC_FLAGS, "pixel_format"},
    { "auto", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = -1}, 0, 0, VIDEO_ENC_FLAGS, "pixel_format"},
    { "8", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 8}, 0, 0, VIDEO_ENC_FLAGS, "pixel_format"},
    { "10", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 10}, 0, 0, VIDEO_ENC_FLAGS, "pixel_format"},
    
    { "pm", "Packing mode", OFFSET(pm), AV_OPT_TYPE_INT, {.i64 = St2110_PackingMode_Block}, 0, 2, ENC_FLAGS, "packing_mode"},
    { "block", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = St2110_PackingMode_Block}, 0, 0, ENC_FLAGS, "packing_mode"},
    { "line", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = St2110_PackingMode_Line}, 0, 0, ENC_FLAGS, "packing_mode"},
    { "general", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = St2110_PackingMode_General}, 0, 0, ENC_FLAGS, "packing_mode"},

    { "ps", "RTP payload size in bytes", OFFSET(ps), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, ENC_FLAGS, NULL},

    { "pt",   "RTP payload type", OFFSET(pt),    AV_OPT_TYPE_INT, {.i64 = 96}, 96, 127, ENC_FLAGS, NULL},

    { "sch",   "Scheduling mode", OFFSET(sch),    AV_OPT_TYPE_INT, {.i64 = St2110_Scheduling_Gapped}, 0, 1, ENC_FLAGS, "scheduling_mode"},
    { "gapped", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = St2110_Scheduling_Gapped}, 0, 0, ENC_FLAGS, "scheduling_mode"},
    { "linear", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = St2110_Scheduling_Linear}, 0, 0, ENC_FLAGS, "scheduling_mode"},

    { "url:v",   "Destination IP or hostname and port", OFFSET(url_v),    AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, VIDEO_ENC_FLAGS, NULL},
    { "url:a",   "Destination IP or hostname and port", OFFSET(url_a[0]), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AUDIO_ENC_FLAGS, NULL},
    { "url:a:0", "Destination IP or hostname and port", OFFSET(url_a[0]), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AUDIO_ENC_FLAGS, NULL},
    { "url:a:1", "Destination IP or hostname and port", OFFSET(url_a[1]), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AUDIO_ENC_FLAGS, NULL},
    { "url:a:2", "Destination IP or hostname and port", OFFSET(url_a[2]), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AUDIO_ENC_FLAGS, NULL},
    { "url:a:3", "Destination IP or hostname and port", OFFSET(url_a[3]), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AUDIO_ENC_FLAGS, NULL},
    { "url:a:4", "Destination IP or hostname and port", OFFSET(url_a[4]), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AUDIO_ENC_FLAGS, NULL},
    { "url:a:5", "Destination IP or hostname and port", OFFSET(url_a[5]), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AUDIO_ENC_FLAGS, NULL},
    { "url:a:6", "Destination IP or hostname and port", OFFSET(url_a[6]), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AUDIO_ENC_FLAGS, NULL},
    { "url:a:7", "Destination IP or hostname and port", OFFSET(url_a[7]), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AUDIO_ENC_FLAGS, NULL},
#if CONFIG_LIBCDTAPI_NMOS
    { "nmos_registry", "register the SMPTE 2110 streams with this NMOS registry, or auto to find one", OFFSET(nmos_registry), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, ENC_FLAGS, NULL},
    { "nmos_label", "the label of the NMOS node; ffmpeg-<serial>:<port> when not given", OFFSET(nmos_label), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, ENC_FLAGS, NULL},
    { "nmos_host", "the address at which controllers reach the NMOS node", OFFSET(nmos_host), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, ENC_FLAGS, NULL},
    { "nmos_port", "the port of the NMOS node's APIs, 0 for any free one", OFFSET(nmos_port), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 65535, ENC_FLAGS, NULL},
#endif

    { NULL },
};

#define TIMED_LOG(avcl, level, fmt, ...)                                       \
    do {                                                                       \
        struct tm ts;                                                          \
        char buf[80];                                                          \
        ts = *localtime(&(time_t){time(NULL)});                                \
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ts);                  \
        av_log(avcl, level, "[%s] " fmt, buf, ## __VA_ARGS__);                 \
    } while (0)

#define TIMED_LOG_US(avcl, level, fmt, ...)                                    \
    do {                                                                       \
        int64_t time = 0;                                                      \
        time = av_gettime();                                                   \
        av_log(avcl, level, "[%" PRId64 "] " fmt, time, ##__VA_ARGS__);        \
    } while (0)

static int write_packet(void *opaque, const uint8_t *buf, int buf_size)
{
    DekTecMuxContext *context = (DekTecMuxContext *)opaque;
    unsigned int result = DtOutpChannel_Write(context->output, buf, buf_size);
    if (result != DTAPI_OK) {
        av_log(NULL, AV_LOG_ERROR, "Could not write to DtOutpChannel: %s\n", DtapiResult2Str(result));
        av_log(NULL, AV_LOG_ERROR, "buf_size=%d\n", buf_size);
        return -1;
    }
    return 0;
}

#if HAVE_INTRINSICS_SSE2
static void pack_and_store(__m128i symbols, uint8_t* dst)
{
    #define Z (uint8_t)0x80
    __m128i multiplier = _mm_set_epi16(64, 16, 4, 1, 64, 16, 4, 1);
    __m128i shuffle_even = _mm_set_epi8(Z, Z, Z, Z, Z, Z, Z, 13, 12, 9, 8, Z, 5, 4, 1, 0);
    __m128i shuffle_odd = _mm_set_epi8(Z, Z, Z, Z, Z, Z, 15, 14, 11, 10, Z, 7, 6, 3, 2, Z);
    __m128i symbols_even, symbols_odd;

    symbols = _mm_mullo_epi16(symbols, multiplier);
    symbols_even = _mm_shuffle_epi8(symbols, shuffle_even);
    symbols_odd = _mm_shuffle_epi8(symbols, shuffle_odd);
    _mm_storeu_si128((__m128i*)dst, _mm_or_si128(symbols_even, symbols_odd));
    #undef Z
}
#endif

// Packs planes into a line of 10-bit symbols, least significant bit first in the order
// U Y V Y: with SSSE3 eight pixels at a time, and the rest, or all without SSSE3, two
// pixels, four symbols in five bytes, at a time.
static void write_10b_packed_line(const uint16_t *src_y, const uint16_t *src_u,
                                  const uint16_t *src_v, uint8_t *dst,
                                  int dst_size, int width)
{
#if HAVE_INTRINSICS_SSE2
    int cpu_flags = av_get_cpu_flags();
    if (X86_SSSE3(cpu_flags)) {
        while (width >= 8 && dst_size >= 26) {
            __m128i y_symbols = _mm_loadu_si128((__m128i*)src_y); // 8 symbols
            __m128i u_symbols = _mm_loadl_epi64((__m128i*)src_u); // 4 symbols
            __m128i v_symbols = _mm_loadl_epi64((__m128i*)src_v); // 4 symbols

            __m128i uv_symbols = _mm_unpacklo_epi16(u_symbols, v_symbols); // Interleave U and V

            __m128i uyvy_lo = _mm_unpacklo_epi16(uv_symbols, y_symbols); // Interleave low part of UV and Y
            __m128i uyvy_hi = _mm_unpackhi_epi16(uv_symbols, y_symbols); // Interleave hi part of UV and Y

            pack_and_store(uyvy_lo, dst);
            dst += 10;
            dst_size -= 10;
            pack_and_store(uyvy_hi, dst);
            dst += 10;
            dst_size -= 10;

            src_y += 8;
            src_u += 4;
            src_v += 4;
            width -= 8;
        }
        while (width >= 8 && dst_size >= 20) {
            uint8_t buffer[16];
            __m128i y_symbols = _mm_loadu_si128((__m128i*)src_y); // 8 symbols
            __m128i u_symbols = _mm_loadl_epi64((__m128i*)src_u); // 4 symbols
            __m128i v_symbols = _mm_loadl_epi64((__m128i*)src_v); // 4 symbols

            __m128i uv_symbols = _mm_unpacklo_epi16(u_symbols, v_symbols); // Interleave U and V

            __m128i uyvy_lo = _mm_unpacklo_epi16(uv_symbols, y_symbols); // Interleave low part of UV and Y
            __m128i uyvy_hi = _mm_unpackhi_epi16(uv_symbols, y_symbols); // Interleave hi part of UV and Y

            pack_and_store(uyvy_lo, dst);
            dst += 10;
            dst_size -= 10;
            pack_and_store(uyvy_hi, buffer);
            memcpy(dst, buffer, 10);
            dst += 10;
            dst_size -= 10;

            src_y += 8;
            src_u += 4;
            src_v += 4;
            width -= 8;
        }
    }
#endif
    while (width >= 2 && dst_size >= 5) {
        uint64_t bits = (uint64_t)(*src_u++ & 0x3FF) | (uint64_t)(src_y[0] & 0x3FF) << 10 |
                        (uint64_t)(*src_v++ & 0x3FF) << 20 | (uint64_t)(src_y[1] & 0x3FF) << 30;
        for (int i = 0; i < 5; i++)
            dst[i] = (uint8_t)(bits >> 8 * i);
        src_y += 2;
        dst += 5;
        dst_size -= 5;
        width -= 2;
    }
}

// Both the source and destination picture are AV_PIX_FMT_UYVY422
static void copy_8b(const AVFrame *src, AvFifo_Frame *dst)
{
    int src_linesize = src->linesize[0];
    uint8_t *src_data = src->data[0];
    int dst_linesize = src->width * 2;
    uint8_t *dst_data = dst->Data;
    for (int y = 0; y < src->height; y++) {
        memcpy(dst_data, src_data, dst_linesize);
        src_data += src_linesize;
        dst_data += dst_linesize;
    }
}

// Both the source and destination picture are AV_PIX_FMT_UYVY422
static void copy_interlaced_8b(const AVFrame *src, AvFifo_Frame *field0, AvFifo_Frame *field1)
{
    int dst_linesize[4] = {src->width * 2, 0, 0, 0};
    uint8_t *src_data = src->data[0];
    uint8_t *dst_data[2] = {field0->Data, field1->Data};
    int dst_size[2] = {(int)field0->Size, (int)field1->Size};
    int field_height = src->height / 2;
    for (int y = 0; y < field_height; y++) {
        for (int i = 0; i < 2; i++) {
            memcpy(dst_data[i], src_data, src->width * 2);
            src_data += src->linesize[0];
            dst_data[i] += dst_linesize[0];
            dst_size[i] -= dst_linesize[0];
        }
    }
}

// The source picture is AV_PIX_FMT_YUV422P10LE and the destination picture is
// YUV 422 10b packed
static void copy_10b(const AVFrame *src, AvFifo_Frame *dst)
{
    int dst_linesizes[4] = {(10 * src->width * 2) / 8, 0, 0, 0};
    uint8_t *src_y = src->data[0];
    uint8_t *src_u = src->data[1];
    uint8_t *src_v = src->data[2];
    uint8_t *dst_data = dst->Data;
    for (int y = 0; y < src->height; y++) {
        write_10b_packed_line((const uint16_t *)src_y, (const uint16_t *)src_u,
                              (const uint16_t *)src_v, dst_data,
                              dst_linesizes[0], src->width);
        src_y += src->linesize[0];
        src_u += src->linesize[1];
        src_v += src->linesize[2];
        dst_data += dst_linesizes[0];
    }
}

// The source picture is AV_PIX_FMT_YUV422P10LE and the destination picture is
// YUV 422 10b packed
static void copy_interlaced_10b(const AVFrame *src, AvFifo_Frame *field0, AvFifo_Frame *field1)
{
    int dst_linesizes[4] = {(10 * src->width * 2) / 8, 0, 0, 0};
    uint8_t *src_y = src->data[0];
    uint8_t *src_u = src->data[1];
    uint8_t *src_v = src->data[2];
    uint8_t *dst[2] = {field0->Data, field1->Data};
    int field_height = src->height / 2;
    for (int y = 0; y < field_height; y++) {
        for (int i = 0; i < 2; i++) {
            write_10b_packed_line(
                (const uint16_t *)src_y, (const uint16_t *)src_u,
                (const uint16_t *)src_v, dst[i], dst_linesizes[0], src->width);
            src_y += src->linesize[0];
            src_u += src->linesize[1];
            src_v += src->linesize[2];
            dst[i] += dst_linesizes[0];
        }
    }
}

static DtTimeOfDay avfifo_pts_to_tod(int64_t pts)
{
    DtTimeOfDay tod = {pts / 1000000000, pts % 1000000000};
    return tod;
}

static int64_t avfifo_tod_to_pts(const DtTimeOfDay *tod)
{
    return (((int64_t)tod->Seconds) * 1000000000) + tod->Nanoseconds;
}

static void trace_pts_ptp(void *ctx, const AvFifo_Frame *frame,
                          const DtDevice *device, int stream_idx)
{
    unsigned int result = 0;
    DtTimeOfDay ptp = {0};
    int64_t delta = 0;

    result = DtDevice_GetTimeOfDay(device, &ptp);
    if (result != DTAPI_OK) {
        const char *message = ff_dektec_avfifo_result_to_string(result);
        av_log(ctx, AV_LOG_ERROR, "Error getting time of day: %s\n", message);
    }
    delta = avfifo_tod_to_pts(&frame->ToD) - avfifo_tod_to_pts(&ptp);
    av_log(ctx, AV_LOG_TRACE,
           "frame_tod[%d]=(%" PRIu32 ", %" PRIu32 "), frame_rtp[%d]=%" PRIu32
           ", ptp=(%" PRIu32 ", %" PRIu32 "), delta=%" PRId64 "\n",
           stream_idx, frame->ToD.Seconds, frame->ToD.Nanoseconds, stream_idx,
           frame->RtpTime, ptp.Seconds, ptp.Nanoseconds, delta);
    if (delta < 0) {
        av_log(ctx, AV_LOG_WARNING, "Frame dropped!\n");
    }
}

/*
 * Waits until there is space in the FIFO. Returns 0 if there is space in the
 * FIFO and returns 1 on timeout. And -1 on error.
 */
static int wait_for_fifo_space(AVFormatContext *s, const AvFifo_TxFifo *fifo, int64_t pts)
{
    DekTecMuxContext* context = (DekTecMuxContext*)s->priv_data;
    int max_fifo_size = 0;

    max_fifo_size = AvFifo_TxFifo_GetMaxSize(fifo);
    while (AvFifo_TxFifo_GetFifoLoad(fifo) >= max_fifo_size) {
        unsigned int result = 0;
        DtTimeOfDay tod = {0};
        int64_t ptp = 0;

        result = DtDevice_GetTimeOfDay(context->device, &tod);
        if (result != DTAPI_OK) {
            const char *message = ff_dektec_avfifo_result_to_string(result);
            av_log(s, AV_LOG_ERROR, "Error getting time of day: %s\n", message);
            return -1;
        }
        ptp = (((int64_t)tod.Seconds) * 1000000000) + tod.Nanoseconds;
        if (pts >= ptp) {
            av_usleep(1000);
        } else {
            av_log(s, AV_LOG_ERROR,
                   "Dropping packet: FIFO full and pts < ptp\n");
            return 1;
        }
    }
    return 0;
}

static const char *get_audio_format_string(St2110_AudioFormat format)
{
    switch (format) {
    case St2110_AudioFormat_L16BE:
        return "l16be";
    case St2110_AudioFormat_L24BE:
        return "l24be";
    case St2110_AudioFormat_Raw:
        return "raw";
    default:
        return "invalid";
    }
}

/*
FIELDRATE!
 * Convert time of day to an aligned timestamp in nanoseconds.
 */
static int64_t avfifo_pts_to_grid_video(int64_t pts, AVRational rate)
{
    FrameRate field_rate = {rate.num, rate.den};
    DtTimeOfDay tod = {0};
    tod = avfifo_pts_to_tod(pts);
    tod = Tod2Grid_Video(&tod, &field_rate);
    return avfifo_tod_to_pts(&tod);
}

/*
FIELDRATE!
 * Convert time of day to an aligned timestamp in nanoseconds.
 */
static int64_t avfifo_pts_to_grid_audio(int64_t pts, int sample_rate)
{
    DtTimeOfDay tod = {0};
    tod = avfifo_pts_to_tod(pts);
    tod = Tod2Grid_Audio(&tod, sample_rate);
    return avfifo_tod_to_pts(&tod);
}

static void avfifo_write_pts_video(int64_t pts, AvFifo_Frame *frame,
                                   AVRational rate)
{
    DtTimeOfDay tod = {0};
    DtTimeOfDay aligned_tod = {0};
    FrameRate frame_rate = {rate.num, rate.den};
    uint32_t rtp_time = 0;

    tod = avfifo_pts_to_tod(pts);
    aligned_tod = Tod2Grid_Video(&tod, &frame_rate);
    rtp_time = Tod2Rtp_Video(&aligned_tod);

    frame->ToD = aligned_tod;
    frame->RtpTime = rtp_time;
}

static void avfifo_write_pts_audio(int64_t pts, AvFifo_Frame *frame,
                                   int sample_rate)
{
    DtTimeOfDay tod = {0};
    uint32_t rtp_time = 0;

    tod = avfifo_pts_to_tod(pts);
    rtp_time = Tod2Rtp_Audio(&tod, sample_rate);

    frame->ToD = tod;
    frame->RtpTime = rtp_time;
}

/**
 * Init scaling context if required
 * @param s   muxer context
 * @param ctx dekTec muxer context
 * @param st  video stream that should be scaled/converted
 * 
 * @return returns 0 if no scaling is required (input and output parameters are
 *         identical), 1 if the scaling context is initialized and <0 for error.
 */
static int init_scaler(AVFormatContext *s, DekTecMuxContext *ctx, AVStream *st)
{
    int src_width = st->codecpar->width;
    int src_height = st->codecpar->height;
    int src_format = st->codecpar->format;
    int dst_width = src_width;
    int dst_height = src_height;

    if (ctx->pf == -1) {
        if (src_format == AV_PIX_FMT_UYVY422)
            ctx->dst_format = AV_PIX_FMT_UYVY422;
        else
            ctx->dst_format = AV_PIX_FMT_YUV422P10LE;
    }
    else if (ctx->pf == 8)
        ctx->dst_format = AV_PIX_FMT_UYVY422;
    else if (ctx->pf == 10)
        ctx->dst_format = AV_PIX_FMT_YUV422P10LE;
    else {
        av_log(s, AV_LOG_ERROR, "Invalid pf argument: %d\n", ctx->pf);
        return AVERROR(EINVAL);
    }

    if (src_width == dst_width && src_height == dst_height &&
        src_format == ctx->dst_format)
        return 0;

    ctx->scale_context = sws_getContext(src_width, src_height, src_format,
                                        dst_width, dst_height, ctx->dst_format,
                                        SWS_BICUBIC, NULL, NULL, NULL);
    if (ctx->scale_context == NULL) {
        av_log(s, AV_LOG_ERROR, "Cannot initialize the swscale context\n");
        return AVERROR(EINVAL);
    } else {
        ctx->scale_frame = av_frame_alloc();
        ctx->scale_frame->format = ctx->dst_format;
        ctx->scale_frame->width = dst_width;
        ctx->scale_frame->height = dst_height;
        av_frame_get_buffer(ctx->scale_frame, 0);

        av_log(s, AV_LOG_DEBUG, "Auto scale from (%dx%d, %s) to (%dx%d, %s)\n",
               src_width, src_height, av_get_pix_fmt_name(src_format),
               dst_width, dst_height, av_get_pix_fmt_name(ctx->dst_format));
    }
    return 1;
}

static int outpchannel_write_header(AVFormatContext *s)
{
    DekTecMuxContext* context = (DekTecMuxContext*)s->priv_data;
    unsigned int result = 0;
    int ret = 0;
    const int tx_mode = DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B;
    AVStream *video_stream = NULL;
    int io_standard = 0;
    int sub_value = 0;
    StandardOption option;
    SdiFormat standard = 0;
    const struct SdiInfo *sdi_info;
    int vid_std;
    int sdi_frame_size;
    void* iter_state = NULL;
    const struct AVOutputFormat *oformat = NULL;
    AVDictionary *options = NULL;

    av_log(s, AV_LOG_INFO, "DekTec write header: %s\n", s->url);

    result = DtDevice_SetToOutput(context->device, context->port);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not set port %d to output\n", context->port);
        return -1;
    }

    context->output = DtOutpChannel_Alloc();
    if (!context->output) {
        av_log(s, AV_LOG_ERROR, "Could not allocate DtOutpChannel\n");
        return -1;
    }

    result = DtOutpChannel_AttachToPort(context->output, context->device, context->port);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not attach output channel to %"PRId64" port %d\n", context->serial_number, context->port);
        return -1;
    }

    result = DtOutpChannel_SetTxControl(context->output, DTAPI_TXCTRL_IDLE);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not set TX control to IDLE\n");
        return -1;
    }

    ret = ff_dektec_give_output_threads(s, context->output, context->threads);
    if (ret < 0)
        return ret;

    for (int i = 0; i < s->nb_streams; i++) {
        if (s->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (video_stream == NULL) {
                video_stream = s->streams[i];
                break;
            }
        }
    }

    ret = av_parse_standard_option(s, context->option_standard, &option);
    
    if (ret < 0)
        standard = av_find_matching_standard(s, NULL, video_stream);
    else
        standard = av_find_matching_standard(s, &option, video_stream);

    if (standard == SDI_FMT_NONE)
    {
        av_log(s, AV_LOG_ERROR, "Could not find matching standard\n");
        return -1;
    }

    sdi_info = av_sdi_info(standard);
    av_log(s, AV_LOG_DEBUG, "SDI standard: %s\n", sdi_info->name);

    vid_std = ff_dektec_get_vidstd(standard);
    av_log(s, AV_LOG_DEBUG, "vid_std=%d\n", vid_std);

    result = DtapiVidStd2IoStd(vid_std, ff_dektec_get_linkstd(standard), &io_standard,
                               &sub_value);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not get IO standard\n");
        return -1;
    }

    result = DtOutpChannel_SetIoConfig(context->output, 1, io_standard, sub_value, -1, -1);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not set IO config\n");
        return -1;
    }

    result = DtOutpChannel_SetTxMode(context->output, tx_mode, 0);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not set TX mode\n");
        return -1;
    }

    result = DtOutpChannel_ClearFifo(context->output);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not clear FIFO\n");
        return -1;
    }

    result = DtOutpChannel_SetTxControl(context->output, DTAPI_TXCTRL_HOLD);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not set TX control to HOLD\n");
        return -1;
    }
    context->is_preloading = 1;

    // Create avio context for device
    sdi_frame_size = sdi_info->nr_sdi_lines *
            (sdi_info->nr_hanc_symbols + sdi_info->nr_vanc_symbols) * 10 / 8;
    context->avio_buffer_size = (sdi_frame_size + 7) & ~7; // align to 8-byte
    context->avio_buffer = av_malloc(context->avio_buffer_size);
    context->avio =
        avio_alloc_context(context->avio_buffer, context->avio_buffer_size, 1,
                           context, NULL, &write_packet, NULL);
    if (!context->avio) {
        av_log(s, AV_LOG_ERROR, "Could not allocate avio context\n");
        return -1;
    }
    // The sdi muxer hands over a whole frame at a time, so let it go straight to the
    // channel instead of being copied through the buffer first.
    context->avio->direct = 1;
    
    context->format_context = avformat_alloc_context();
    if (!context->format_context) {
        av_log(s, AV_LOG_ERROR, "Could not allocate avformat context\n");
        return -1;
    }

    while (oformat = av_muxer_iterate(&iter_state)) {
        if (strcmp(oformat->name, "sdi") == 0)
            break;
    }
    if (strcmp(oformat->name, "sdi") != 0) {
        av_log(s, AV_LOG_ERROR, "Could not find SDI output format\n");
        return -1;
    }

    context->format_context->oformat = oformat;
    context->format_context->pb = context->avio;

    for (int i = 0; i < s->nb_streams; i++) {
        avformat_new_stream(context->format_context, NULL);
        context->format_context->streams[i]->time_base = s->streams[i]->time_base;
        context->format_context->streams[i]->avg_frame_rate = s->streams[i]->avg_frame_rate;
        avcodec_parameters_copy(context->format_context->streams[i]->codecpar, s->streams[i]->codecpar);
        // The device's default audio codec is 2110's big-endian L24; the sdi muxer
        // takes little-endian samples, which outpchannel_write_packet makes of them.
        if (s->streams[i]->codecpar->codec_id == AV_CODEC_ID_PCM_S24BE)
            context->format_context->streams[i]->codecpar->codec_id = AV_CODEC_ID_PCM_S24LE;
    }

    av_dict_set(&options, "no_header", "1", 0);
    av_dict_set(&options, "calc_crc", "0", 0); // HW will do CRC
    av_dict_set(&options, "sdi_standard", context->option_standard, 0);
    ret = avformat_write_header(context->format_context, &options);
    if (ret != 0)
        return ret;
    av_dict_free(&options);

    return 1;
}

static int outpchannel_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    DekTecMuxContext* context = (DekTecMuxContext*)s->priv_data;
    int fifo_size = 0;
    int min_fifo_load = 0;
    int fifo_load = 0;
    unsigned int result = 0;

    result = DtOutpChannel_GetFifoSize(context->output, &fifo_size);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not write to DtOutpChannel\n");
        return -1;
    }
    min_fifo_load = (fifo_size * 3) / 4;

    if (s->streams[pkt->stream_index]->codecpar->codec_id == AV_CODEC_ID_PCM_S24BE) {
        int ret = av_packet_make_writable(pkt);
        if (ret < 0)
            return ret;
        for (int i = 0; i + 2 < pkt->size; i += 3)
            FFSWAP(uint8_t, pkt->data[i], pkt->data[i + 2]);
    }

    av_write_frame(context->format_context, pkt);
    av_packet_unref(pkt);

    result = DtOutpChannel_GetFifoLoad(context->output, &fifo_load);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not get fifo load from DtOutpChannel\n");
        return -1;
    }

    if (fifo_load >= min_fifo_load && context->is_preloading)
    {
        result = DtOutpChannel_SetTxControl(context->output, DTAPI_TXCTRL_SEND);
        if (result != DTAPI_OK) {
            av_log(s, AV_LOG_ERROR, "Could not set TX control to SEND\n");
            return -1;
        }
        context->is_preloading = 0;
    }

    return 1;
}

static int outpchannel_write_trailer(AVFormatContext *s)
{
    DekTecMuxContext* context = (DekTecMuxContext*)s->priv_data;
    unsigned int result = 0;

    av_write_trailer(context->format_context);

    // A stream shorter than the preload has not started going out yet.
    if (context->is_preloading) {
        result = DtOutpChannel_SetTxControl(context->output, DTAPI_TXCTRL_SEND);
        if (result != DTAPI_OK) {
            av_log(s, AV_LOG_ERROR, "Could not set TX control to SEND\n");
            return -1;
        }
        context->is_preloading = 0;
    }

    // Sends what the FIFO still holds before the channel stops.
    result = DtOutpChannel_Detach(context->output, DTAPI_WAIT_UNTIL_SENT);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not detach from DtOutpChannel\n");
        return -1;
    }

    result = DtDevice_Detach(context->device);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not detach from DtDevice\n");
        return -1;
    }

    DtOutpChannel_Free(context->output);
    DtDevice_Free(context->device);

    avformat_free_context(context->format_context);
    avio_context_free(&context->avio);
    return 1;
}

static int avfifo_init_txfifo(AVFormatContext *s, AvFifo_TxFifo **fifo, char *url)
{
    DekTecMuxContext *context = (DekTecMuxContext *)s->priv_data;
    unsigned int result = 0;

    *fifo = AvFifo_TxFifo_Alloc();
    if (!*fifo) {
        av_log(s, AV_LOG_ERROR, "Could not allocate TxFifo\n");
        return -1;
    }
    result = AvFifo_TxFifo_Attach(*fifo, context->device, 1);
    if (result != DTAPI_OK) {
        const char *message = ff_dektec_avfifo_result_to_string(result);
        av_log(s, AV_LOG_ERROR, "Could not attach TxFifo: %s\n", message);
        return -1;
    }

    if (url) {
        AvFifo_IpPars ippars = {0};
        int ret = ff_dektec_parse_url(s, url, context->pt, &ippars);
        if (ret < 0)
            return ret;

        AvFifo_TxFifo_SetIpPars(*fifo, &ippars);
    } else {
        av_log(s, AV_LOG_ERROR, "Please provide url argument\n");
        return AVERROR(EINVAL);
    }

    return 1;
}

static int avfifo_configure_video(AVFormatContext *s, AvFifo_TxFifo *fifo, AVStream *st)
{
    DekTecMuxContext *context = (DekTecMuxContext *)s->priv_data;
    unsigned int result = 0;
    int ret = 0;
    AVCodecParameters *par = st->codecpar;
    AVRational field_rate;
    St2110_VideoTiming timing;
    St2110_VideoPacking packing;
    St2110_TxConfigVideo video_config;
    int max_fifo_size = 0;

    if (context->pm == St2110_PackingMode_Block) {
        packing.OneLinePerPacket = 0;
        packing.PackingMode = St2110_PackingMode_Block;
        packing.PayloadSize = -1;
    } else if (context->pm == St2110_PackingMode_Line) {
        packing.OneLinePerPacket = 1;
        packing.PackingMode = St2110_PackingMode_General;
        packing.PayloadSize = context->ps;
    } else if (context->pm == St2110_PackingMode_General) {
        packing.OneLinePerPacket = 0;
        packing.PackingMode = St2110_PackingMode_General;
        packing.PayloadSize = context->ps;
    } else {
        av_log(s, AV_LOG_ERROR, "Invalid packing mode\n");
        return AVERROR(EINVAL);
    }

    field_rate = st->avg_frame_rate;
    if (par->field_order != AV_FIELD_PROGRESSIVE) {
        field_rate.num *= 2;
    }
    timing.Scheduling = context->sch;
    if (par->field_order == AV_FIELD_UNKNOWN ||
        par->field_order == AV_FIELD_PROGRESSIVE) {
        timing.VideoScanning = St2110_VideoScanning_Progressive;
        context->is_interlaced = 0;
        timing.Rate.Numerator = field_rate.num;
        timing.Rate.Denominator = field_rate.den;
        
        if (field_rate.den == 1)
            av_log(s, AV_LOG_VERBOSE, "Standard: %dp%d\n",par->height, field_rate.num);
        else
            av_log(s, AV_LOG_VERBOSE, "Standard: %dp%.2f\n",par->height, field_rate.num / (double)field_rate.den);
    } else if (par->field_order == AV_FIELD_TT ||
               par->field_order == AV_FIELD_TB) {
        timing.VideoScanning = St2110_VideoScanning_Interlaced;
        context->is_interlaced = 1;
        timing.Rate.Numerator = field_rate.num;
        timing.Rate.Denominator = field_rate.den;
        
        if (field_rate.den == 1)
            av_log(s, AV_LOG_VERBOSE, "Standard: %di%d\n",par->height, field_rate.num);
        else
            av_log(s, AV_LOG_VERBOSE, "Standard: %di%.2f\n",par->height, field_rate.num / (double)field_rate.den);
    } else {
        const char *field_order = "";
        if (par->field_order == AV_FIELD_BB)
            field_order = "bottom first";
        else if (par->field_order == AV_FIELD_BT)
            field_order = "bottom coded first (swapped)";
        else
            field_order = "unknown";

        av_log(s, AV_LOG_ERROR, "Field order not supported: %s\n", field_order);
        return AVERROR(EINVAL);
    }

    ret = init_scaler(s, context, st);
    if (ret < 0)
        return ret;

    if (context->dst_format == AV_PIX_FMT_UYVY422) {
        video_config.Format = St2110_TxFrameFormat_Uyvy422_8b;
        context->copy = copy_8b;
        context->copy_interlaced = copy_interlaced_8b;
    } else if (context->dst_format == AV_PIX_FMT_YUV422P10LE) {
        video_config.Format = St2110_TxFrameFormat_Uyvy422_10b;
        context->copy = copy_10b;
        context->copy_interlaced = copy_interlaced_10b;
    } else {
        av_log(s, AV_LOG_ERROR, "Pixel format %s not supported\n",
               av_get_pix_fmt_name(context->dst_format));
        return -1;
    }
    video_config.Packing = packing;
    video_config.Resolution.Width = par->width;
    video_config.Resolution.Height = par->height;
    video_config.Timing = timing;

    result = AvFifo_TxFifo_ConfigureVideo(fifo, &video_config);
    if (result != DTAPI_OK) {
        const char *message = ff_dektec_avfifo_result_to_string(result);
        av_log(s, AV_LOG_ERROR, "Could not configure video for TxFifo: %s\n", message);
        return -1;
    }

    max_fifo_size =
        av_rescale_q_rnd(context->max_buffer_time, context->time_scale,
                         av_inv_q(field_rate), AV_ROUND_UP);
    if (par->field_order == AV_FIELD_TT) {
        max_fifo_size *= 2;
    }
    AvFifo_TxFifo_SetMaxSize(fifo, max_fifo_size);
    av_log(s, AV_LOG_DEBUG, "Video max fifo size is %d\n", max_fifo_size);

    return 1;
}

static int avfifo_configure_audio(AVFormatContext *s, AvFifo_TxFifo *fifo, AVStream *st)
{
    DekTecMuxContext *context = (DekTecMuxContext *)s->priv_data;
    unsigned int result = 0;
    AVCodecParameters *par = st->codecpar;
    int max_fifo_size = 0;
    St2110_TxConfigAudio audio_config;

    if (par->codec_id == AV_CODEC_ID_PCM_S16BE)
        audio_config.Format = St2110_AudioFormat_L16BE;
    else if (par->codec_id == AV_CODEC_ID_PCM_S24BE)
        audio_config.Format = St2110_AudioFormat_L24BE;
    else {
        av_log(s, AV_LOG_ERROR,
               "Audio format not supported: %s. Use either %s or %s\n",
               avcodec_descriptor_get(par->codec_id)->name,
               avcodec_descriptor_get(AV_CODEC_ID_PCM_S16BE)->name,
               avcodec_descriptor_get(AV_CODEC_ID_PCM_S24BE)->name);
        return AVERROR(EINVAL);
    }
    audio_config.NumChannels = par->ch_layout.nb_channels;
    audio_config.NumSamplesPerIpPacket = context->aspp;
    audio_config.SampleRate = par->sample_rate;

    av_log(s, AV_LOG_VERBOSE, "AudioConfig\n");
    av_log(s, AV_LOG_VERBOSE, "  Format=%s\n", get_audio_format_string(audio_config.Format));
    av_log(s, AV_LOG_VERBOSE, "  NumChannels=%d\n", audio_config.NumChannels);
    av_log(s, AV_LOG_VERBOSE, "  NumSamplesPerIpPacket=%d\n", audio_config.NumSamplesPerIpPacket);
    av_log(s, AV_LOG_VERBOSE, "  SampleRate=%d\n", audio_config.SampleRate);

    result = AvFifo_TxFifo_ConfigureAudio(fifo, &audio_config);
    if (result != DTAPI_OK) {
        const char *message = ff_dektec_avfifo_result_to_string(result);
        av_log(s, AV_LOG_ERROR, "Could not configure audio for TxFifo: %s\n",
               message);
        return -1;
    }

    max_fifo_size = av_rescale_q_rnd(
        context->max_buffer_time, context->time_scale,
        av_make_q(context->aspp, par->sample_rate), AV_ROUND_UP);
    AvFifo_TxFifo_SetMaxSize(fifo, max_fifo_size);
    av_log(s, AV_LOG_DEBUG, "Audio max fifo size is %d\n", max_fifo_size);

    return 1;
}

static int avfifo_write_video(AVFormatContext *s, AvFifo_TxFifo *fifo, AVPacket *pkt)
{
    DekTecMuxContext *context = (DekTecMuxContext *)s->priv_data;
    unsigned int result = 0;
    int max_size = 0;
    const AVFrame *src;
    AVStream *st = s->streams[pkt->stream_index];
    AVCodecParameters *par = st->codecpar;
    int linesize = 0;
    int frame_size = 0;
    int field_size = 0;
    AVRational field_rate = st->avg_frame_rate;

    if (context->scale_context) {
        int ret = 0;
        const AVFrame *input = (const AVFrame *)pkt->data;

        ret = sws_scale(context->scale_context, (const uint8_t * const *)input->data, input->linesize, 0,
                        input->height, context->scale_frame->data,
                        context->scale_frame->linesize);
        if (ret <= 0)
            return ret;

        ret = av_frame_copy_props(context->scale_frame, input);
        if (ret != 0)
            return ret;

        src = context->scale_frame;
    } else {
        src = (const AVFrame *)pkt->data;
    }

    if (par->field_order != AV_FIELD_PROGRESSIVE) {
        field_rate.num *= 2;
    }

    if (context->dst_format == AV_PIX_FMT_UYVY422) {
        linesize = av_image_get_linesize(context->dst_format, par->width, 0);
    } else if (context->dst_format == AV_PIX_FMT_YUV422P10LE) {
        linesize = (10 * par->width * 2) / 8;
    }
    frame_size = par->height * linesize;
    field_size = frame_size / 2;

    max_size = AvFifo_TxFifo_GetMaxSize(fifo);
    while (AvFifo_TxFifo_GetFifoLoad(fifo) >= (max_size - 2)) {
        av_usleep(1000);
    }

    if (context->is_interlaced) {
        AvFifo_Frame *fields[2] = {0};
        for (int i = 0; i < 2; i++) {
            int64_t pts;
            
            fields[i] = AvFifo_TxFifo_GetFromMemPool(fifo, field_size);
            if (!fields[i]) {
                const char *message = ff_dektec_avfifo_result_to_string(DTAPI_E_EXCEPTION);
                av_log(s, AV_LOG_ERROR, "Could not get field from mem pool: %s\n", message);
                return -1;
            }
            pts = context->start_time[pkt->stream_index] + av_rescale_q(pkt->pts, st->time_base, context->time_scale);
            avfifo_write_pts_video(pts, fields[i], field_rate);
            if (i == 1) {
                int field_duration = av_rescale_q(1, av_inv_q(field_rate), context->time_scale);
                pts += field_duration;
                avfifo_write_pts_video(pts, fields[i], field_rate);
            }
            fields[i]->NumValidBytes = field_size;
            fields[i]->Field = i;
        }

        context->copy_interlaced(src, fields[0], fields[1]);

        trace_pts_ptp(s, fields[0], context->device, st->index);
        for (int i = 0; i < 2; i++) {
            result = AvFifo_TxFifo_Write(fifo, fields[i]);
            if (result != DTAPI_OK) {
                const char *message = ff_dektec_avfifo_result_to_string(result);
                av_log(s, AV_LOG_ERROR, "Could not write to TxFifo: %s\n", message);
                return -1;
            }
        }
    } else {
        int64_t pts;
        AvFifo_Frame *frame =
            AvFifo_TxFifo_GetFromMemPool(fifo, frame_size);
        if (!frame) {
            const char *message = ff_dektec_avfifo_result_to_string(DTAPI_E_EXCEPTION);
            av_log(s, AV_LOG_ERROR, "Could not read frame: %s\n", message);
            return -1;
        }

        pts = context->start_time[pkt->stream_index] + av_rescale_q(pkt->pts, st->time_base, context->time_scale);
        avfifo_write_pts_video(pts, frame, field_rate);
        frame->NumValidBytes = frame_size;
        frame->Field = 0;

        context->copy(src, frame);

        trace_pts_ptp(s, frame, context->device, st->index);
        result = AvFifo_TxFifo_Write(fifo, frame);
        if (result != DTAPI_OK) {
            const char *message = ff_dektec_avfifo_result_to_string(result);
            av_log(s, AV_LOG_ERROR, "Could not write to TxFifo: %s\n", message);
            return -1;
        }
    }
    return 1;
}

static int avfifo_write_audio(AVFormatContext *s, AvFifo_TxFifo *fifo, AVPacket *pkt)
{
    DekTecMuxContext *context = (DekTecMuxContext *)s->priv_data;
    unsigned int result = 0;
    int ret = 0;
    AvFifo_Frame *frame = NULL;
    AVStream *st = s->streams[pkt->stream_index];
    AVCodecParameters *par = st->codecpar;
    int64_t pts = 0;

    pts = context->start_time[pkt->stream_index] +
                  av_rescale_q(pkt->pts, st->time_base, context->time_scale);

    ret = wait_for_fifo_space(s, fifo, pts);
    if (ret != 0)
        return ret;

    frame = AvFifo_TxFifo_GetFromMemPool(fifo, pkt->size);
    if (!frame) {
        const char *msg = ff_dektec_avfifo_result_to_string(DTAPI_E_EXCEPTION);
        av_log(s, AV_LOG_ERROR, "Could not get frame from pool: %s\n", msg);
        return -1;
    }

    avfifo_write_pts_audio(pts, frame, par->sample_rate);
    memcpy(frame->Data, pkt->data, FFMIN(pkt->size, frame->Size));
    frame->NumValidBytes = pkt->size;
    frame->Field = 0;

    trace_pts_ptp(s, frame, context->device, st->index);
    result = AvFifo_TxFifo_Write(fifo, frame);
    if (result != DTAPI_OK) {
        const char *message = ff_dektec_avfifo_result_to_string(result);
        av_log(s, AV_LOG_ERROR, "Could not write to TxFifo: %s\n", message);
        return -1;
    }

    return 1;
}

#if CONFIG_LIBCDTAPI_NMOS
/**
 * Open the NMOS node when nmos_registry is given, and register each stream's FIFO as a
 * sender: "video", and "audio 0", "audio 1" and so on.
 *
 * @return 0, or a negative AVERROR after logging why
 */
static int nmos_open(AVFormatContext *s)
{
    DekTecMuxContext *context = (DekTecMuxContext *)s->priv_data;
    FFDektecNmosOptions options = { context->nmos_registry, context->nmos_label,
                                    context->nmos_host, context->nmos_port };
    int audio = 0;
    int ret;

    if (!context->nmos_registry || !*context->nmos_registry)
        return 0;
    ret = ff_dektec_nmos_open(s, &options, context->device, context->serial_number,
                              context->port, &context->nmos);
    for (int i = 0; i < s->nb_streams && ret >= 0; i++) {
        char name[32];
        if (!context->fifos[i])
            continue;
        if (s->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            snprintf(name, sizeof(name), "video");
        else
            snprintf(name, sizeof(name), "audio %d", audio++);
        ret = ff_dektec_nmos_add_sender(context->nmos, context->fifos[i], i, name);
    }
    if (ret < 0)
        ff_dektec_nmos_close(&context->nmos);
    return ret;
}
#endif

static int avfifo_write_header(AVFormatContext *s)
{
    DekTecMuxContext* context = (DekTecMuxContext*)s->priv_data;
    unsigned int result = 0;
    int ret = 0;
    int audio_index = 0;
    int video_index = 0;

    context->start_tod.Seconds = 0;
    context->start_tod.Nanoseconds = 0;
    for (int i = 0; i < MAX_STREAMS; i++) {
        context->start_time[i] = -1;
    }
    context->max_buffer_time = 100000000;
    context->time_scale = av_make_q(1, 1000000000);

    context->fifos = av_malloc_array(s->nb_streams, sizeof(AvFifo_TxFifo*));
    for (int i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        AVCodecParameters *par = st->codecpar;
        if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (video_index >= MAX_VIDEO_STREAMS) {
                av_log(s, AV_LOG_WARNING, "Only %d video streams are supported\n", MAX_VIDEO_STREAMS);
                continue;
            }
            ret = avfifo_init_txfifo(s, &context->fifos[i], context->url_v);
            if (ret < 0) {
                return ret;
            }
            ret = avfifo_configure_video(s, context->fifos[i], st);
            if (ret < 0) {
                return ret;
            }
            video_index++;
        }
        else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (audio_index >= MAX_AUDIO_STREAMS) {
                av_log(s, AV_LOG_WARNING, "Only %d audio streams are supported\n", MAX_AUDIO_STREAMS);
                continue;
            }
            ret = avfifo_init_txfifo(s, &context->fifos[i], context->url_a[audio_index]);
            if (ret < 0) {
                return ret;
            }
            ret = avfifo_configure_audio(s, context->fifos[i], st);
            if (ret < 0) {
                return ret;
            }
            audio_index++;
        }
        else {
            av_log(s, AV_LOG_WARNING, "Unsupported stream type %s\n", av_get_media_type_string(par->codec_type));
        }

        if (context->fifos[i]) {
            result = AvFifo_TxFifo_Start(context->fifos[i]);
            if (result != DTAPI_OK) {
                const char *message = ff_dektec_avfifo_result_to_string(result);
                av_log(s, AV_LOG_ERROR, "Could not start TxFifo: %s\n", message);
                return -1;
            }
        }
    }

#if CONFIG_LIBCDTAPI_NMOS
    ret = nmos_open(s);
    if (ret < 0)
        return ret;
#endif
    return 1;
}

static int avfifo_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    DekTecMuxContext *context = (DekTecMuxContext *)s->priv_data;
    AvFifo_TxFifo *fifo = context->fifos[pkt->stream_index];
    AVStream *st = s->streams[pkt->stream_index];
    AVCodecParameters *par = st->codecpar;
    int ret = 0;

    if (!fifo) {
        return 1;
    }

#if CONFIG_LIBCDTAPI_NMOS
    // A sender that a controller disabled drops its packets until it is enabled again.
    ff_dektec_nmos_poll(context->nmos);
    if (!ff_dektec_nmos_sending(context->nmos, pkt->stream_index))
        return 0;
#endif

    if (context->start_tod.Seconds == 0 &&
        context->start_tod.Nanoseconds == 0) {
        int result = 0;

        result = DtDevice_GetTimeOfDay(context->device, &context->start_tod);
        if (result != DTAPI_OK) {
            const char *message = ff_dektec_avfifo_result_to_string(result);
            av_log(s, AV_LOG_ERROR, "Error getting time of day: %s\n", message);
            return AVERROR(EIO);
        }

        for (int i = 0; i < s->nb_streams; i++) {
            AVCodecParameters *par = s->streams[i]->codecpar;

            if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
                AVRational field_rate = s->streams[i]->avg_frame_rate;
                if (par->field_order != AV_FIELD_PROGRESSIVE) {
                    field_rate.num *= 2;
                }
                context->start_time[i] = avfifo_tod_to_pts(&context->start_tod);
                context->start_time[i] += context->max_buffer_time;
                context->start_time[i] = avfifo_pts_to_grid_video(
                    context->start_time[i], field_rate);
            } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
                context->start_time[i] = avfifo_tod_to_pts(&context->start_tod);
                context->start_time[i] += context->max_buffer_time;
                context->start_time[i] = avfifo_pts_to_grid_audio(
                    context->start_time[i], par->sample_rate);
            }
        }

        av_log(s, AV_LOG_TRACE, "start_tod=(%" PRIu32 ", %" PRIu32 ")\n",
            context->start_tod.Seconds, context->start_tod.Nanoseconds);
        for (int i = 0; i < s->nb_streams; i++) {
            DtTimeOfDay tod = {0};
            if (context->start_time[i] >= 0) {
                tod = avfifo_pts_to_tod(context->start_time[i]);
            }
            av_log(s, AV_LOG_TRACE,
                "start_time[%d]=(%" PRIu32 ", %" PRIu32 "), delta=(%" PRIu32
                ", %" PRIu32 ")\n",
                i, tod.Seconds, tod.Nanoseconds,
                tod.Seconds - context->start_tod.Seconds,
                tod.Nanoseconds - context->start_tod.Nanoseconds);
        }
    }

    av_log(s, AV_LOG_TRACE, "pts[%d]=%" PRId64 "\n", st->index,
                    av_rescale_q(pkt->pts, st->time_base, context->time_scale));
    if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
        ret = avfifo_write_video(s, fifo, pkt);
    } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
        ret = avfifo_write_audio(s, fifo, pkt);
    }

    return ret;
}

static int avfifo_write_trailer(AVFormatContext *s)
{
    DekTecMuxContext *context = (DekTecMuxContext *)s->priv_data;
    unsigned int result = 0;

#if CONFIG_LIBCDTAPI_NMOS
    // Closed before the FIFOs stop, so that no controller's request waits for them.
    ff_dektec_nmos_close(&context->nmos);
#endif

    for (int i = 0; i < s->nb_streams; i++) {
        result = AvFifo_TxFifo_Stop(context->fifos[i]);
        if (result != DTAPI_OK) {
            const char *message = ff_dektec_avfifo_result_to_string(result);
            av_log(s, AV_LOG_ERROR, "Error stopping TxFifo: %s\n", message);
            return -1;
        }
        AvFifo_TxFifo_Freep(&context->fifos[i]);
    }
    av_freep(&context->fifos);

    return 1;
}

static int ff_dektec_write_header(AVFormatContext *s)
{
    DekTecMuxContext* context = (DekTecMuxContext*)s->priv_data;
    char* colon = NULL;
    unsigned int result = 0;
    int ret = 0;
    int n_hw_funcs = 0;
    DtHwFuncDesc *hw_funcs = NULL;
    int valid_serial_number = 0;

    colon = strchr(s->url, ':');
    context->serial_number = strtoll(s->url, &colon, 10);
    context->port = strtol(colon + 1, NULL, 10);

    ret = ff_get_hw_funcs(&hw_funcs, &n_hw_funcs);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "Error listing hardware functions\n");
        return ret;
    }

    for (int i = 0; i < n_hw_funcs; i++) {
        if (hw_funcs[i].SerialNumber == context->serial_number) {
            valid_serial_number = 1;
            if (hw_funcs[i].IsAvFifo) {
                context->write_header = avfifo_write_header;
                context->write_packet = avfifo_write_packet;
                context->write_trailer = avfifo_write_trailer;
            } else {
                context->write_header = outpchannel_write_header;
                context->write_packet = outpchannel_write_packet;
                context->write_trailer = outpchannel_write_trailer;
            }
            break;
        }
    }
    if (!valid_serial_number) {
        av_log(s, AV_LOG_ERROR,
               "Unsupported device with serial number %" PRId64 "\n",
               context->serial_number);
        return -1;
    }

    context->device = DtDevice_Alloc();
    if (!context->device) {
        av_log(s, AV_LOG_ERROR, "Could not allocate DtDevice\n");
        return -1;
    }

    result = DtDevice_AttachToSerial(context->device, context->serial_number);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not attach to %"PRId64"\n", context->serial_number);
        return -1;
    }

    return context->write_header(s);
}

static int ff_dektec_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    DekTecMuxContext *context = (DekTecMuxContext *)s->priv_data;
    return context->write_packet(s, pkt);
}

static int ff_dektec_write_trailer(AVFormatContext *s)
{
    DekTecMuxContext *context = (DekTecMuxContext *)s->priv_data;
    return context->write_trailer(s);
}

static int ff_dektec_list_output_devices(AVFormatContext *s, struct AVDeviceInfoList *device_list)
{
    return ff_dektec_list_devices(device_list);
}

static const AVClass dektec_muxer_class = {
    .class_name = "DekTec outdev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT,
};

const FFOutputFormat ff_dektec_muxer = {
    .p.name           = "dektec",
    .p.long_name      = NULL_IF_CONFIG_SMALL("DekTec output"),
    .p.audio_codec    = AV_CODEC_ID_PCM_S24BE,
    .p.video_codec    = AV_CODEC_ID_WRAPPED_AVFRAME,
    .p.subtitle_codec = AV_CODEC_ID_NONE,
    .p.flags          = AVFMT_NOFILE,
    .p.priv_class     = &dektec_muxer_class,
    .priv_data_size   = sizeof(DekTecMuxContext),
    .write_header     = ff_dektec_write_header,
    .write_packet     = ff_dektec_write_packet,
    .write_trailer    = ff_dektec_write_trailer,
    .get_device_list  = ff_dektec_list_output_devices,
};
