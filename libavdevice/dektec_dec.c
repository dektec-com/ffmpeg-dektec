/*
 * DekTec hardware input
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
 * @file dektec_dec.c
 * DekTec hardware input
 * @author Jeroen Steendam
 */

#include "avdevice.h"
#include "dektec_common.h"
#include "libavcodec/codec.h"
#include "libavformat/avformat.h"
#include "libavformat/demux.h"
#include "libavformat/internal.h"
#include "libavformat/sdicommon.h"
#include "libavformat/sdiframe.h"
#include "libavformat/url.h"
#include "libavutil/fifo.h"
#include "libavutil/frame.h"
#include "libavutil/internal.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/x86/cpu.h"
#include "libavutil/avstring.h"

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

#include <time.h>

#define MAX_AUDIO_STREAMS 8
#define MAX_VIDEO_STREAMS 1
#define MAX_STREAMS MAX_AUDIO_STREAMS + MAX_VIDEO_STREAMS

#define AD_FRAMES_AUDIO 1001

// The SDI frames an input reads at most to find whether they carry audio: a few, so
// that they are not latency, as FFmpeg's default of 5 seconds would be.
#define PROBE_FRAMES 5

typedef struct AudioFormat {
    int sample_rate;
    int bps;
    int n_channels;
    int aspp; // Audio samples per packet
} AudioFormat;

typedef struct DekTecDemuxContext {
    const AVClass *av_class;

    int port;
    int64_t serial_number;
    DtDevice* device;

    AvFifo_RxFifo **fifos;
    int is_interlaced;
    AvFifo_Frame *field0;
    int last_field;
    AVRational time_scale;
    RxStatistics statistics[MAX_STREAMS];
    int64_t last_pts;
    AudioFormat audio_format;
    void (*copy)(const AvFifo_Frame *src, AVPacket *dst_pkt, const AVCodecParameters *par);
    void (*copy_interlaced)(const AvFifo_Frame *field0, const AvFifo_Frame *field1, AVPacket *dst_pkt, const AVCodecParameters *par);
    DtTimeOfDay start_tod;
    int64_t start_time;
    AVFifo *audio_buffer;
    int audio_buffer_size;
    int64_t audio_buffer_pts;

    DtInpChannel* input;
    char *option_standard;
    int64_t timestamp_align;
    int64_t signal_timeout;         // How long to wait for a signal; negative: no limit
    int threads;                    // The threads option: FF_DEKTEC_THREADS_AUTO, 1 or 2+
    int has_signal;
    int64_t signal_lost_ts;
    DtDetVidStd detected_standard;

    SdiUnpacker *unpacker;          // Takes each SDI frame apart where the card wrote it
    int vidstd;                     // The SDI standard's DTAPI_VIDSTD_ code
    int64_t frame_number;           // The number of the next frame taken apart
    AVPacket *queued[2 * PROBE_FRAMES]; // The packets of the frames read to find the streams
    int nb_queued;
    int next_queued;

    const struct SdiInfo *sdi_info;

    char *url[MAX_STREAMS];
    int pt;

    int64_t frame_count;
    int64_t dropped;
    int64_t total_overflows;

    int (*read_header)(struct AVFormatContext *);
    int (*read_packet)(struct AVFormatContext *, AVPacket *pkt);
    int (*read_close)(struct AVFormatContext *);

#if CONFIG_LIBCDTAPI_NMOS
    char *nmos_registry;            // The NMOS registry, or "auto"; NULL or empty for none
    char *nmos_label;               // The NMOS node's label; NULL for the default
    char *nmos_host;                // The address the node's APIs are reached at
    int nmos_port;                  // The port of the node's APIs; 0 for any free one
    int64_t nmos_wait;              // How long to wait for the first connections
    FFDektecNmos *nmos;             // The node, while the input is open
    AvFifo_IpPars ippars[MAX_STREAMS]; // The stream of each FIFO, from its URL
#endif
} DekTecDemuxContext;

static AVRational frame_rates[] = {
    {60, 1},
    {60000, 1001},
    {50, 1},
    {30, 1},
    {30000, 1001},
    {25, 1},
    {24, 1},
    {24000, 1001},
};

#define TIMED_LOG(avcl, level, fmt, ...)                                       \
    do {                                                                       \
        struct tm ts;                                                          \
        char buf[80];                                                          \
        ts = *localtime(&(time_t){time(NULL)});                                \
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ts);                  \
        av_log(avcl, level, "[%s] " fmt, buf, ## __VA_ARGS__);                 \
    } while (0)

// Whether the application asked to stop a blocking read. libavformat's own check is
// not exported from its shared library.
static int interrupted(AVFormatContext *s)
{
    const AVIOInterruptCB *cb = &s->interrupt_callback;
    return cb->callback && cb->callback(cb->opaque);
}

// How long the input waits for a frame, in ms, before it looks at the signal and the
// application again.
#define LEND_TIMEOUT_MS 100

/*
 * Take apart the next frame where the card wrote it, and give the frame back. Its image
 * goes into pkt and its audio waits in the unpacker. Returns AVERROR(EAGAIN) when no
 * frame came in LEND_TIMEOUT_MS or the frame is of another standard, which is left out.
 */
static int take_frame(AVFormatContext *s, AVPacket *pkt)
{
    DekTecDemuxContext *context = (DekTecDemuxContext *)s->priv_data;
    DtSdiView *view = ff_sdi_unpacker_view(context->unpacker);
    int vidstd = 0;
    int ret;
    unsigned int result = DtInpChannel_AcquireFrame(context->input, view, LEND_TIMEOUT_MS,
                                                    NULL);

    if (result == DTAPI_E_TIMEOUT)
        return AVERROR(EAGAIN);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not take a frame from DtInpChannel: %s\n",
               DtapiResult2Str(result));
        return AVERROR(EIO);
    }
    // A signal of another standard does not fit the streams
    DtSdiView_GetFormat(view, &vidstd, NULL);
    if (vidstd != context->vidstd) {
        DtInpChannel_ReleaseFrame(context->input, view);
        TIMED_LOG(s, AV_LOG_WARNING, "A frame of another standard than %s is left out\n",
                  context->sdi_info->name);
        return AVERROR(EAGAIN);
    }
    ret = ff_sdi_unpacker_parse(context->unpacker, context->frame_number++, pkt);
    DtInpChannel_ReleaseFrame(context->input, view);
    return ret;
}

/*
 * Keep pkt, moved, to give it out before the frames that follow.
 */
static int queue_packet(DekTecDemuxContext *context, AVPacket *pkt)
{
    AVPacket *queued = av_packet_alloc();

    if (!queued)
        return AVERROR(ENOMEM);
    av_packet_move_ref(queued, pkt);
    context->queued[context->nb_queued++] = queued;
    return 0;
}

/*
 * Read the first frames, up to PROBE_FRAMES of them, until one carries audio, to add the
 * audio stream in its channels, and keep their packets to give out first. Audio that
 * starts later is left out.
 */
static int probe_frames(AVFormatContext *s)
{
    DekTecDemuxContext *context = (DekTecDemuxContext *)s->priv_data;
    AVPacket *pkt = av_packet_alloc();
    int64_t start = av_gettime_relative();
    int nb_frames = 0;
    int ret = 0;

    if (!pkt)
        return AVERROR(ENOMEM);
    while (nb_frames < PROBE_FRAMES) {
        int nb_channels;

        ret = take_frame(s, pkt);
        if (ret == AVERROR(EAGAIN)) {
            ret = 0;
            if (interrupted(s)) {
                ret = AVERROR_EXIT;
                break;
            }
            // A signal is there: the frames take no longer than this to come
            if (av_gettime_relative() - start > 2000000) {
                av_log(s, AV_LOG_ERROR, "No frames came on port %d\n", context->port);
                ret = AVERROR(EIO);
                break;
            }
            continue;
        }
        if (ret < 0)
            break;
        nb_frames++;
        ret = queue_packet(context, pkt);
        if (ret < 0)
            break;
        nb_channels = ff_sdi_unpacker_nb_channels(context->unpacker);
        if (nb_channels > 0) {
            ret = ff_sdi_unpacker_add_audio_stream(context->unpacker, s, nb_channels);
            if (ret >= 0 && ff_sdi_unpacker_audio(context->unpacker, pkt) > 0)
                ret = queue_packet(context, pkt);
            break;
        }
    }
    av_packet_free(&pkt);
    return ret;
}

#if HAVE_INTRINSICS_SSE2
// Load 8 packed 10-bit symbols (80 bits) and unpack each symbol to it's own
// 16-bit word (128 bits)
static __m128i load_and_unpack(const uint8_t *input)
{
    __m128i mask10_to16 = _mm_set_epi16(~0x3F, 0x3FF0, 0xFFC, 0x3FF, ~0x3F, 0x3FF0, 0xFFC, 0x3FF);
    __m128i mult10_to16 = _mm_set_epi16(1, 4, 16, 64, 1, 4, 16, 64);
    __m128i shuf10_to16 = _mm_set_epi8(9, 8, 8, 7, 7, 6, 6, 5, 4, 3, 3, 2, 2, 1, 1, 0);

    __m128i symbols = _mm_loadu_si128((__m128i*)input);
    symbols = _mm_shuffle_epi8(symbols, shuf10_to16);
    symbols = _mm_and_si128(symbols, mask10_to16);
    symbols = _mm_mullo_epi16(symbols, mult10_to16);
    symbols = _mm_srli_epi16(symbols, 6);
    return symbols;
}
#endif

// Unpacks a line of 10-bit symbols, packed least significant bit first in the order
// U Y V Y, into planes: with SSSE3 eight pixels at a time, and the rest, or all without
// SSSE3, two pixels, four symbols in five bytes, at a time.
static void read_10b_packed_line(const uint8_t *src, uint16_t *dst_y, uint16_t *dst_u, uint16_t *dst_v, int width)
{
#if HAVE_INTRINSICS_SSE2
    int cpu_flags = av_get_cpu_flags();
    if (X86_SSSE3(cpu_flags)) {
        while (width >= 8) {
            #define Z (uint8_t)0x80
            __m128i symbols1, symbols2, y_symbols, uv_symbols;

            symbols1 = load_and_unpack(src); // 8 symbols
            src += 10;
            symbols2 = load_and_unpack(src); // 8 symbols
            src += 10;

            y_symbols = _mm_or_si128(_mm_shuffle_epi8(symbols1, _mm_set_epi8(Z, Z, Z, Z, Z, Z, Z, Z, 15, 14, 11, 10, 7, 6, 3, 2)),
                                        _mm_shuffle_epi8(symbols2, _mm_set_epi8(15, 14, 11, 10, 7, 6, 3, 2, Z, Z, Z, Z, Z, Z, Z, Z)));
            uv_symbols = _mm_or_si128(_mm_shuffle_epi8(symbols1, _mm_set_epi8(Z, Z, Z, Z, 13, 12, 5, 4, Z, Z, Z, Z, 9, 8, 1, 0)),
                                            _mm_shuffle_epi8(symbols2, _mm_set_epi8(13, 12, 5, 4, Z, Z, Z, Z, 9, 8, 1, 0, Z, Z, Z, Z)));
            _mm_storeu_si128((__m128i*)dst_y, y_symbols);
            _mm_storel_epi64((__m128i*)dst_u, uv_symbols);
            _mm_storel_epi64((__m128i*)dst_v, _mm_srli_si128(uv_symbols, 8));

            width -= 8;
            dst_y += 8;
            dst_u += 4;
            dst_v += 4;

            #undef Z
        }
    }
#endif
    while (width >= 2) {
        uint64_t bits = (uint64_t)src[0] | (uint64_t)src[1] << 8 | (uint64_t)src[2] << 16 |
                        (uint64_t)src[3] << 24 | (uint64_t)src[4] << 32;
        *dst_u++ = bits & 0x3FF;
        *dst_y++ = bits >> 10 & 0x3FF;
        *dst_v++ = bits >> 20 & 0x3FF;
        *dst_y++ = bits >> 30 & 0x3FF;
        src += 5;
        width -= 2;
    }
}

// Both the source and destination picture are AV_PIX_FMT_UYVY422
static void copy_8b(const AvFifo_Frame *src, AVPacket *dst, const AVCodecParameters *par)
{
    memcpy(dst->data, src->Data, FFMIN(dst->size, src->NumValidBytes));
}

// Both the source and destination picture are AV_PIX_FMT_UYVY422
static void copy_interlaced_8b(const AvFifo_Frame *field0, const AvFifo_Frame *field1, AVPacket *dst, const AVCodecParameters *par)
{
    int linesize = av_image_get_linesize(par->format, par->width, 0);
    uint8_t *src_data[2] = {field0->Data, field1->Data};
    int src_size[2] = {(int)field0->NumValidBytes, (int)field1->NumValidBytes};
    uint8_t *dst_data = dst->data;
    int dst_size = dst->size;
    int field_height = par->height / 2;
    for (int y = 0; y < field_height; y++) {
        for (int i = 0; i < 2; i++) {
            memcpy(dst_data, src_data[i], FFMIN(dst_size, linesize));
            src_data[i] += linesize;
            src_size[i] -= linesize;
            dst_data += linesize;
            dst_size -= linesize;
        }
    }
}

// The source picture is YUV 422 10b packed and the destination picture is
// YUV422P10LE
static void copy_10b(const AvFifo_Frame *src, AVPacket *dst,
              const AVCodecParameters *par)
{
    int src_linesizes[4] = {(10 * par->width * 2) / 8, 0, 0, 0};
    int dst_linesizes[4] = {0};
    uint8_t *dst_data[AV_NUM_DATA_POINTERS] = {0};
    uint8_t *src_data = NULL;
    uint8_t *dst_y = NULL, *dst_u = NULL, *dst_v = NULL;
    av_image_fill_arrays(dst_data, dst_linesizes, dst->data, par->format,
                         par->width, par->height, 1);

    src_data = src->Data;
    dst_y = dst_data[0];
    dst_u = dst_data[1];
    dst_v = dst_data[2];
    for (int y = 0; y < par->height; y++) {
        read_10b_packed_line(src_data, (uint16_t *)dst_y, (uint16_t *)dst_u,
                             (uint16_t *)dst_v, par->width);
        src_data += src_linesizes[0];
        dst_y += dst_linesizes[0];
        dst_u += dst_linesizes[1];
        dst_v += dst_linesizes[2];
    }
}

// The source picture is YUV 422 10b packed and the destination picture is
// YUV422P10LE
static void copy_interlaced_10b(const AvFifo_Frame *field0, const AvFifo_Frame *field1,
                         AVPacket *dst, const AVCodecParameters *par)
{
    int src_linesizes[4] = {(10 * par->width * 2) / 8, 0, 0, 0};
    int dst_linesizes[4] = {0};
    uint8_t *dst_data[AV_NUM_DATA_POINTERS] = {0};
    uint8_t *src[2] = {0};
    uint8_t *dst_y = NULL, *dst_u = NULL, *dst_v = NULL;
    int field_height = 0;
    av_image_fill_arrays(dst_data, dst_linesizes, dst->data, par->format,
                         par->width, par->height, 1);

    src[0] = field0->Data;
    src[1] = field1->Data;
    dst_y = dst_data[0];
    dst_u = dst_data[1];
    dst_v = dst_data[2];
    field_height = par->height / 2;
    for (int y = 0; y < field_height; y++) {
        for (int i = 0; i < 2; i++) {
            read_10b_packed_line(src[i], (uint16_t *)dst_y, (uint16_t *)dst_u,
                                 (uint16_t *)dst_v, par->width);
            src[i] += src_linesizes[0];
            dst_y += dst_linesizes[0];
            dst_u += dst_linesizes[1];
            dst_v += dst_linesizes[2];
        }
    }
}

static int64_t avfifo_read_pts_video(const AvFifo_Frame *frame)
{
    DtTimeOfDay tod = {0};
    tod = Rtp2Tod_Video(frame->RtpTime, &frame->ToD);
    av_log(NULL, AV_LOG_TRACE, "[%us, %uns] = Rtp2Tod_Video(%u, [%us, %uns])\n",
           tod.Seconds, tod.Nanoseconds, frame->RtpTime, frame->ToD.Seconds,
           frame->ToD.Nanoseconds);
    return (((int64_t)tod.Seconds) * 1000000000) + tod.Nanoseconds;
}

static int64_t avfifo_read_pts_audio(const AvFifo_Frame *frame, int sample_rate)
{
    DtTimeOfDay tod = {0};
    tod = Rtp2Tod_Audio(frame->RtpTime, &frame->ToD, sample_rate);
    av_log(NULL, AV_LOG_TRACE,
           "[%us, %uns] = Rtp2Tod_Audio(%u, [%us, %uns], %d)\n", tod.Seconds,
           tod.Nanoseconds, frame->RtpTime, frame->ToD.Seconds,
           frame->ToD.Nanoseconds, sample_rate);
    return (((int64_t)tod.Seconds) * 1000000000) + tod.Nanoseconds;
}

static int inpchannel_read_header(AVFormatContext *s)
{
    DekTecDemuxContext* context = (DekTecDemuxContext*)s->priv_data;
    const int rx_mode = DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B;
    unsigned int result = 0;
    int ret;
    int io_standard = 0;
    int sub_value = 0;
    StandardOption option = {0};
    int vid_std, link_std;
    int max_fifo_size;

    result = DtDevice_SetToInput(context->device, context->port);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not set port %d to input [%s]\n", context->port, DtapiResult2Str(result));
        return -1;
    }

    // Waits a tenth of a second at a time, so that the application can stop it.
    int64_t wait_start = av_gettime_relative();
    for (;;) {
        result = DtDevice_WaitForSignalTimeout(context->device, context->port, 100,
                                               &context->detected_standard);
        if (result == DTAPI_OK)
            break;
        if (result != DTAPI_E_TIMEOUT) {
            av_log(s, AV_LOG_ERROR, "Could not detect a signal on port %d [%s]\n",
                   context->port, DtapiResult2Str(result));
            return AVERROR(EIO);
        }
        if (interrupted(s))
            return AVERROR_EXIT;
        if (context->signal_timeout >= 0 &&
            av_gettime_relative() - wait_start >= context->signal_timeout) {
            av_log(s, AV_LOG_ERROR, "No signal on port %d\n", context->port);
            return AVERROR(ETIMEDOUT);
        }
    }
    av_log(s, AV_LOG_DEBUG, "Detected standard:\n");
    av_log(s, AV_LOG_DEBUG, "  VidStd=%d\n", context->detected_standard.VidStd);
    av_log(s, AV_LOG_DEBUG, "  LinkStd=%d\n", context->detected_standard.LinkStd);
    av_log(s, AV_LOG_DEBUG, "  LinkNr=%d\n", context->detected_standard.LinkNr);
    av_log(s, AV_LOG_DEBUG, "  Vpid=%u\n", context->detected_standard.Vpid);
    av_log(s, AV_LOG_DEBUG, "  Vpid2=%u\n", context->detected_standard.Vpid2);
    av_log(s, AV_LOG_DEBUG, "  AspectRatio=%d\n", context->detected_standard.AspectRatio);
    av_log(s, AV_LOG_DEBUG, "  OriginalVidStd=%d\n", context->detected_standard.OriginalVidStd);
    av_log(s, AV_LOG_DEBUG, "  OriginalLinkStd=%d\n", context->detected_standard.OriginalLinkStd);
    context->has_signal = 1;

    context->input = DtInpChannel_Alloc();
    if (!context->input) {
        av_log(s, AV_LOG_ERROR, "Could not allocate DtInpChannel\n");
        return -1;
    }

    result = DtInpChannel_AttachToPort(context->input, context->device, context->port);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not attach input channel to %"PRId64"\n port %d\n", context->serial_number, context->port);
        return -1;
    }

    result = DtInpChannel_SetRxControl(context->input, DTAPI_RXCTRL_IDLE);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not set RX control to IDLE\n");
        return -1;
    }

    if (strlen(context->option_standard) > 0)
        result = av_parse_standard_option(s, context->option_standard, &option);
    context->sdi_info = av_sdi_info(av_sdi_get_fmt(&option));
    if (!context->sdi_info)
        context->sdi_info = ff_dektec_get_sdi_info(context->detected_standard.VidStd);
    if (!context->sdi_info) {
        av_log(s, AV_LOG_ERROR, "No SDI standard found\n");
        return -1;
    }

    av_log(s, AV_LOG_VERBOSE, "SDI standard: %s\n", context->sdi_info->name);

    vid_std = context->detected_standard.VidStd;
    link_std = context->detected_standard.LinkStd;
    av_log(s, AV_LOG_DEBUG, "vid_std=%d\n", vid_std);
    av_log(s, AV_LOG_DEBUG, "link_std=%d\n", link_std);

    result = DtapiVidStd2IoStd(vid_std, link_std, &io_standard, &sub_value);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not get IO standard: %s\n", DtapiResult2Str(result));
        return -1;
    }

    av_log(s, AV_LOG_DEBUG, "io_standard=%d\n", io_standard);
    av_log(s, AV_LOG_DEBUG, "sub_value=%d\n", sub_value);
    result = DtInpChannel_SetIoConfig(context->input, 1, io_standard, sub_value, -1, -1);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not set IO config: %s\n", DtapiResult2Str(result));
        return -1;
    }

    result = DtInpChannel_SetRxMode(context->input, rx_mode);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not set RX mode: %s\n", DtapiResult2Str(result));
        return -1;
    }

    max_fifo_size = 0;
    result = DtInpChannel_GetMaxFifoSize(context->input, &max_fifo_size);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not get max fifo size: %s\n", DtapiResult2Str(result));
        return -1;
    }
    av_log(s, AV_LOG_DEBUG, "max_fifo_size=%d\n", max_fifo_size);

    // CDTAPI's parser takes each frame apart where the card wrote it, without a copy;
    // the threads option gives it its threads, as the channel converts nothing.
    context->vidstd = av_sdi_vidstd(context->sdi_info);
    ret = ff_sdi_unpacker_alloc(&context->unpacker, s, context->sdi_info, context->threads);
    if (ret < 0)
        return ret;
    ret = ff_sdi_unpacker_add_video_stream(context->unpacker, s, 0);
    if (ret < 0)
        return ret;

    // Receiving starts once the parser is ready to take the frames: a 2160p frame fills
    // much of the card's buffer, which the time setting up the parser took would make
    // overflow.
    result = DtInpChannel_SetRxControl(context->input, DTAPI_RXCTRL_RCV);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not set RX control to RCV: %s\n", DtapiResult2Str(result));
        return -1;
    }

    ret = probe_frames(s);
    if (ret < 0)
        return ret;
    return 1;
}

// Waits for a frame and returns it, unless the application asked not to block or to stop.
static int inpchannel_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    DekTecDemuxContext *context = (DekTecDemuxContext *)s->priv_data;
    int flags = 0;
    int latched = 0;
    unsigned int result = 0;
    int value = 0;
    int sub_value = 0;
    int ret = 0;

    for (;;) {
        int has_packet = 0;

        if (context->has_signal) {
            // The packets of the frames read to find the streams go first, then a frame's
            // audio after its image; take_frame waits a moment for a frame, and gives
            // EAGAIN when none came.
            if (context->next_queued < context->nb_queued) {
                av_packet_move_ref(pkt, context->queued[context->next_queued]);
                av_packet_free(&context->queued[context->next_queued++]);
                ret = pkt->stream_index == 0 ? 0 : 1;
            } else {
                ret = ff_sdi_unpacker_audio(context->unpacker, pkt);
                if (ret == 0)
                    ret = take_frame(s, pkt);
            }
            if (ret == 1)
                return 0;
            if (ret < 0 && ret != AVERROR(EAGAIN))
                return ret;
            if (ret == 0) {
                has_packet = 1;
                if (context->frame_count == 0 && context->timestamp_align) {
                    AVRational remainder = av_make_q(av_gettime() % context->timestamp_align, 1000000);
                    AVRational frame_duration = av_inv_q(av_sdi_rate(context->sdi_info->picture_rate));
                    if (av_cmp_q(remainder, frame_duration) > 0) {
                        context->dropped++;
                        av_packet_unref(pkt);
                        has_packet = 0;
                    }
                }
                if (has_packet)
                    context->frame_count++;
            }

            result = DtInpChannel_GetFlags(context->input, &flags, &latched);
            if (result != DTAPI_OK) {
                av_log(s, AV_LOG_ERROR, "Could not get flags from DtInpChannel\n");
                return AVERROR(EIO);
            }
            if ((latched & DTAPI_RX_FIFO_OVF) != 0) {
                TIMED_LOG(s, AV_LOG_WARNING, "FIFO overflow detected\n");
                context->total_overflows++;
            }
            result = DtInpChannel_ClearFlags(context->input, latched);
            if (result != DTAPI_OK) {
                av_log(s, AV_LOG_ERROR, "Could not clear flags for DtInpChannel\n");
                return AVERROR(EIO);
            }
            // A frame that arrived shows the signal is there; the detection, which opens
            // the port's receiver anew, would only disturb it
            if (ret == 0) {
                if (has_packet)
                    return 0;
                continue;
            }
        }

        // No frame came in time: see whether the signal is still there
        result = DtInpChannel_DetectIoStd(context->input, &value, &sub_value);
        if (result == DTAPI_E_INVALID_VIDSTD && context->has_signal) {
            TIMED_LOG(s, AV_LOG_WARNING, "SDI signal lost\n");
            context->has_signal = 0;
            context->signal_lost_ts = av_gettime();
        }
        else if (result != DTAPI_E_INVALID_VIDSTD && !context->has_signal) {
            TIMED_LOG(s, AV_LOG_WARNING,
                      "SDI signal re-acquired after %" PRId64 "ms\n",
                      (av_gettime() - context->signal_lost_ts) / 1000);
            context->has_signal = 1;
        }

        if (s->flags & AVFMT_FLAG_NONBLOCK)
            return AVERROR(EAGAIN);
        if (interrupted(s))
            return AVERROR_EXIT;
        if (!context->has_signal)
            av_usleep(5000);
    }
}

static int inpchannel_read_close(AVFormatContext *s)
{
    DekTecDemuxContext *context = (DekTecDemuxContext *)s->priv_data;
    unsigned int result = 0;

    result = DtInpChannel_SetRxControl(context->input, DTAPI_RXCTRL_IDLE);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not set RX control to IDLE\n");
        return -1;
    }

    result = DtInpChannel_Detach(context->input, 1);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not detach from DtInpChannel\n");
        return -1;
    }

    result = DtDevice_Detach(context->device);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not detach from DtDevice\n");
        return -1;
    }

    DtInpChannel_Freep(&context->input);
    DtDevice_Freep(&context->device);

    ff_sdi_unpacker_free(&context->unpacker);
    for (int i = context->next_queued; i < context->nb_queued; i++)
        av_packet_free(&context->queued[i]);

    av_log(s, AV_LOG_INFO, "Total frames received: %"PRId64"\n", context->frame_count);
    av_log(s, AV_LOG_INFO, "Total overflows: %"PRId64"\n", context->total_overflows);
    return 1;
}

static int avfifo_init_rxfifo(AVFormatContext *s, AvFifo_RxFifo **fifo, char *url)
{
    DekTecDemuxContext* context = (DekTecDemuxContext*)s->priv_data;
    unsigned int result = 0;

    *fifo = AvFifo_RxFifo_Alloc();
    if (!*fifo) {
        av_log(s, AV_LOG_ERROR, "Could not allocate RxFifo\n");
        return -1;
    }
    result = AvFifo_RxFifo_Attach(*fifo, context->device, 1);
    if (result != DTAPI_OK) {
        const char *message = ff_dektec_avfifo_result_to_string(result);
        av_log(s, AV_LOG_ERROR, "Could not attach RxFifo: %s\n", message);
        return -1;
    }

    if (url && !strcmp(url, "nmos")) {
#if CONFIG_LIBCDTAPI_NMOS
        // NMOS gives the address, once a controller connects the stream's receiver.
        return 1;
#else
        av_log(s, AV_LOG_ERROR, "The URL nmos needs FFmpeg built with "
               "--enable-libcdtapi-nmos\n");
        return AVERROR(ENOSYS);
#endif
    } else if (url) {
        AvFifo_IpPars ippars = {0};
        int ret = ff_dektec_parse_url(s, url, context->pt, &ippars);
        if (ret < 0)
            return ret;

        AvFifo_RxFifo_SetIpPars(*fifo, &ippars);
#if CONFIG_LIBCDTAPI_NMOS
        // fifo points into context->fifos, at the stream's index.
        context->ippars[fifo - context->fifos] = ippars;
#endif
    } else {
        return AVERROR(EINVAL);
    }

    return 1;
}

static int avfifo_configure_video(AVFormatContext *s, AvFifo_RxFifo *fifo,
                                  int stream_id)
{
    DekTecDemuxContext* context = (DekTecDemuxContext*)s->priv_data;
    unsigned int result = 0;
    St2110_RxConfigVideo video_config;
    FrameProperties properties;
    int64_t pts[3] = {0};
    int64_t delta[3] = {INT64_MAX, INT64_MAX, INT64_MAX};
    int64_t min_delta[3] = {INT64_MAX, INT64_MAX, INT64_MAX};
    AVRational frame_rate[3] = {{0, 1}, {0, 1}, {0, 1}};
    int is_psf = 0;
    AVRational fps;
    const char *subsampling_str = NULL;
    AVStream *st = NULL;

    // Start auto-detect
    av_log(s, AV_LOG_DEBUG, "Start auto-detect\n");

    video_config.Format = St2110_RxFrameFormat_Raw;
    result = AvFifo_RxFifo_ConfigureVideo(fifo, &video_config);
    if (result != DTAPI_OK) {
        const char *message = ff_dektec_avfifo_result_to_string(result);
        av_log(s, AV_LOG_ERROR, "Could not configure video for RxFifo: %s\n", message);
        return -1;
    }

    AvFifo_RxFifo_SetMaxSize(fifo, 50);

    result = AvFifo_RxFifo_Start(fifo);
    if (result != DTAPI_OK) {
        const char *message = ff_dektec_avfifo_result_to_string(result);
        av_log(s, AV_LOG_ERROR, "Could not start RxFifo: %s\n", message);
        return -1;
    }

    for (int i = 0; i < 3; i++) {
        AvFifo_Frame *frame = NULL;
        while (AvFifo_RxFifo_GetFifoLoad(fifo) < 1) {
            av_usleep(10000);

            if (interrupted(s))
                return AVERROR_EXIT;
        }

        frame = AvFifo_RxFifo_Read(fifo);
        if (!frame) {
            const char *message = ff_dektec_avfifo_result_to_string(DTAPI_E_EXCEPTION);
            av_log(s, AV_LOG_ERROR, "Could not read frame: %s\n", message);
            return AVERROR(EINVAL);
        }
        av_log(s, AV_LOG_DEBUG, "Frame[%d]\n", i);
        av_log(s, AV_LOG_DEBUG, "  ToD=[%us, %uns]\n", frame->ToD.Seconds,
               frame->ToD.Nanoseconds);
        av_log(s, AV_LOG_DEBUG, "  RtpTime=%u\n", frame->RtpTime);
        av_log(s, AV_LOG_DEBUG, "  Size=%"PRId64"\n", frame->Size);
        av_log(s, AV_LOG_DEBUG, "  NumValidBytes=%d\n", frame->NumValidBytes);
        av_log(s, AV_LOG_DEBUG, "  Is420=%d\n", frame->Is420);
        av_log(s, AV_LOG_DEBUG, "  NumRows=%d\n", frame->NumRows);
        av_log(s, AV_LOG_DEBUG, "  Field=%d\n", frame->Field);

        pts[i] = avfifo_read_pts_video(frame);
        av_log(s, AV_LOG_DEBUG, "pts[%d]=%"PRId64"\n", i, pts[i]);

        if (i == 0) {
            if (GetFrameProperties(frame, &properties) != DTAPI_OK) {
                av_log(s, AV_LOG_ERROR, "Could not detect video standard\n");
                return AVERROR(EAGAIN);
            }
        }

        result = AvFifo_RxFifo_ReturnToMemPool(fifo, frame);
        if (result != DTAPI_OK) {
            const char *message = ff_dektec_avfifo_result_to_string(result);
            av_log(s, AV_LOG_ERROR, "Could not return to memory pool: %s\n", message);
            return -1;
        }
    }

    result = AvFifo_RxFifo_Stop(fifo);
    if (result != DTAPI_OK) {
        const char *message = ff_dektec_avfifo_result_to_string(result);
        av_log(s, AV_LOG_ERROR, "Could not stop RxFifo: %s\n", message);
        return -1;
    }

    delta[0] = FFABS(pts[1] - pts[0]);
    delta[1] = FFABS(pts[2] - pts[1]);
    delta[2] = FFABS(pts[2] - pts[0]) / 2;

    for (int i = 0; i < sizeof(frame_rates)/sizeof(AVRational); i++) {
        int frame_duration =
            av_rescale_q(1, av_inv_q(frame_rates[i]), context->time_scale);
        if (delta[0] > 0 && FFABS(delta[0] - frame_duration) <= min_delta[0]) {
            min_delta[0] = FFABS(delta[0] - frame_duration);
            frame_rate[0] = frame_rates[i];
        }
        if (delta[1] > 0 && FFABS(delta[1] - frame_duration) <= min_delta[1]) {
            min_delta[1] = FFABS(delta[1] - frame_duration);
            frame_rate[1] = frame_rates[i];
        }
        if (delta[2] > 0 && FFABS(delta[2] - frame_duration) <= min_delta[2]) {
            min_delta[2] = FFABS(delta[2] - frame_duration);
            frame_rate[2] = frame_rates[i];
        }
    }

    av_log(s, AV_LOG_DEBUG, "delta[0]=%"PRId64", min_delta[0]=%"PRId64", fps=%d/%d\n", delta[0], min_delta[0], frame_rate[0].num, frame_rate[0].den);
    av_log(s, AV_LOG_DEBUG, "delta[1]=%"PRId64", min_delta[1]=%"PRId64", fps=%d/%d\n", delta[1], min_delta[1], frame_rate[1].num, frame_rate[1].den);
    av_log(s, AV_LOG_DEBUG, "delta[2]=%"PRId64", min_delta[2]=%"PRId64", fps=%d/%d\n", delta[2], min_delta[2], frame_rate[2].num, frame_rate[2].den);

    if ((frame_rate[0].num == 0 && frame_rate[0].den == 1) ||
        (frame_rate[1].num == 0 && frame_rate[1].den == 1) ||
        (frame_rate[2].num == 0 && frame_rate[2].den == 1)) {
        av_log(s, AV_LOG_ERROR, "Could not detect framerate\n");
        return AVERROR(EINVAL);
    }


    if (min_delta[0] > delta[0]) {
        is_psf = 1;
        fps = frame_rate[1];
        av_log(s, AV_LOG_DEBUG, " Detected PsF with field rate %d/%d\n", fps.num, fps.den);
    } else if (min_delta[1] > delta[1]) {
        is_psf = 1;
        fps = frame_rate[0];
        av_log(s, AV_LOG_DEBUG, " Detected PsF with field rate %d/%d\n", fps.num, fps.den);
    } else  {
        fps = frame_rate[2];
        av_log(s, AV_LOG_DEBUG, " Detected frame/field rate %d/%d\n", fps.num, fps.den);
    }

    if (properties.IsInterlaced && !is_psf) {
        fps = av_div_q(fps, av_make_q(2, 1));
    }

    av_log(s, AV_LOG_DEBUG, "FrameProperties\n");
    av_log(s, AV_LOG_DEBUG, "  Width=%d\n", properties.Width);
    av_log(s, AV_LOG_DEBUG, "  Height=%d\n", properties.Height);
    av_log(s, AV_LOG_DEBUG, "  IsInterlaced=%s\n", properties.IsInterlaced ? "true" : "false");
    switch (properties.Subsampling) {
        case ChromaSubsampling_Key: subsampling_str = "KEY"; break;
        case ChromaSubsampling_420: subsampling_str = "420"; break;
        case ChromaSubsampling_422: subsampling_str = "422"; break;
        case ChromaSubsampling_444: subsampling_str = "444"; break;
        default:
            subsampling_str = "Invalid";
    }
    av_log(s, AV_LOG_DEBUG, "  Subsampling=%s\n", subsampling_str);
    av_log(s, AV_LOG_DEBUG, "  BitDepth=%d\n", properties.BitDepth);
    av_log(s, AV_LOG_DEBUG, "  is_psf=%s\n", is_psf ? "true" : "false");
    av_log(s, AV_LOG_DEBUG, "  frame_rate=%d/%d\n", fps.num, fps.den);

    if (properties.Subsampling != ChromaSubsampling_422) {
        av_log(s, AV_LOG_ERROR, "Subsampling %s not supported\n", subsampling_str);
        return -1;
    }

    st = avformat_new_stream(s, NULL);
    st->id = stream_id;
    st->avg_frame_rate = fps;
    st->r_frame_rate = st->avg_frame_rate;
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = AV_CODEC_ID_RAWVIDEO;
    if (properties.BitDepth == 8) {
        st->codecpar->format = AV_PIX_FMT_UYVY422;
        context->copy = copy_8b;
        context->copy_interlaced = copy_interlaced_8b;
        video_config.Format = St2110_RxFrameFormat_Uyvy422_8b;
    } else if (properties.BitDepth == 10) {
        st->codecpar->format = AV_PIX_FMT_YUV422P10LE;
        context->copy = copy_10b;
        context->copy_interlaced = copy_interlaced_10b;
        video_config.Format = St2110_RxFrameFormat_Uyvy422_10b;
    } else {
        av_log(s, AV_LOG_ERROR, "Bitdepth of %d is not supported\n",
               properties.BitDepth);
        return -1;
    }
    // avpriv_set_pts_info(st, 64, st->avg_frame_rate.den, st->avg_frame_rate.num);
    avpriv_set_pts_info(st, 64, context->time_scale.num, context->time_scale.den);

    st->codecpar->width  = properties.Width;
    st->codecpar->height = properties.Height;
    st->codecpar->bits_per_coded_sample = properties.BitDepth;
    st->codecpar->bits_per_raw_sample = properties.BitDepth;
    st->codecpar->bit_rate =
        ((int64_t)properties.BytesPerFrame * 8 * fps.num +
         fps.den - 1) /
        fps.den;
    if (properties.IsInterlaced && !is_psf) {
        st->codecpar->field_order = AV_FIELD_TT;
    } else {
        st->codecpar->field_order = AV_FIELD_PROGRESSIVE;
    }
    context->is_interlaced = properties.IsInterlaced;

    av_log(s, AV_LOG_DEBUG, "Auto-detect done\n");

    result = AvFifo_RxFifo_ConfigureVideo(fifo, &video_config);
    if (result != DTAPI_OK) {
        const char *message = ff_dektec_avfifo_result_to_string(result);
        av_log(s, AV_LOG_ERROR, "Could not configure video for RxFifo: %s\n", message);
        return -1;
    }

    AvFifo_RxFifo_SetMaxSize(fifo, 50);

    return 1;
}

static int avfifo_autodetect_audio(AVFormatContext *s, AvFifo_RxFifo *fifo,
                                   int stream_id)
{
    DekTecDemuxContext *context = (DekTecDemuxContext *)s->priv_data;
    St2110_RxConfigAudio audio_config;
    unsigned int result = 0;
    int size = 0;
    int packet_size = 0;
    int sample_rates[] = {48000, 32000, 44100, 96000};
    int bitdepths[] = {24, 16, 20, 32};
    AudioFormat formats[(sizeof(sample_rates) / sizeof(int)) *
                        (sizeof(bitdepths) / sizeof(int))] = {0};
    int n = 0;
    int64_t pts[AD_FRAMES_AUDIO][sizeof(sample_rates) / sizeof(int)] = {0};
    int64_t raw_pts[AD_FRAMES_AUDIO] = {0};
    AVRational raw_dur = {0};
    AVRational raw_bitrate = {0};

    audio_config.Format = St2110_AudioFormat_Raw;
    result = AvFifo_RxFifo_ConfigureAudio(fifo, &audio_config);
    if (result != DTAPI_OK) {
        const char *message = ff_dektec_avfifo_result_to_string(result);
        av_log(s, AV_LOG_ERROR, "Could not configure audio for RxFifo: %s\n", message);
        return -1;
    }

    AvFifo_RxFifo_SetMaxSize(fifo, 1600);

    result = AvFifo_RxFifo_Start(fifo);
    if (result != DTAPI_OK) {
        const char *message = ff_dektec_avfifo_result_to_string(result);
        av_log(s, AV_LOG_ERROR, "Could not start RxFifo: %s\n", message);
        return -1;
    }

    for (int i = 0; i < AD_FRAMES_AUDIO; i++) {
        AvFifo_Frame *frame = NULL;

        while (AvFifo_RxFifo_GetFifoLoad(fifo) <= 0) {
            av_usleep(1000);
            
            if (interrupted(s))
                return AVERROR_EXIT;
        }

        frame = AvFifo_RxFifo_Read(fifo);
        if (!frame) {
            const char *message = ff_dektec_avfifo_result_to_string(DTAPI_E_EXCEPTION);
            av_log(s, AV_LOG_ERROR, "Could not read frame: %s\n", message);
            return AVERROR(EINVAL);
        }

        for (int j = 0; j < sizeof(sample_rates) / sizeof(int); j++)
            pts[i][j] = avfifo_read_pts_audio(frame, sample_rates[j]);
        raw_pts[i] = (((int64_t)frame->ToD.Seconds) * 1000000000) +
                     frame->ToD.Nanoseconds;

        if (i < (AD_FRAMES_AUDIO - 1))
            size += frame->NumValidBytes;
        packet_size = frame->NumValidBytes;

        result = AvFifo_RxFifo_ReturnToMemPool(fifo, frame);
        if (result != DTAPI_OK) {
            const char *message = ff_dektec_avfifo_result_to_string(result);
            av_log(s, AV_LOG_ERROR, "Could not return to memory pool: %s\n", message);
            return -1;
        }
    }

    result = AvFifo_RxFifo_Stop(fifo);
    if (result != DTAPI_OK) {
        const char *message = ff_dektec_avfifo_result_to_string(result);
        av_log(s, AV_LOG_ERROR, "Could not stop RxFifo: %s\n", message);
        return -1;
    }

    av_reduce(&raw_dur.num, &raw_dur.den,
              FFABS(raw_pts[AD_FRAMES_AUDIO - 1] - raw_pts[0]),
              context->time_scale.den, INT_MAX);
    raw_bitrate = av_div_q(av_make_q(size * 8, 1), raw_dur);

    for (int i = 0; i < sizeof(sample_rates) / sizeof(int); i++) {
        int sample_rate = sample_rates[i];
        AVRational dur = {0};
        AVRational bitrate = {0};
        AVRational bitrate_delta = {0};

        av_log(s, AV_LOG_DEBUG, "Possible configurations for %dHz audio\n", sample_rate);

        av_reduce(&dur.num, &dur.den,
                  FFABS(pts[AD_FRAMES_AUDIO - 1][i] - pts[0][i]),
                  context->time_scale.den, INT_MAX);
        bitrate = av_div_q(av_make_q(size * 8, 1), dur);
        bitrate_delta = av_sub_q(bitrate, raw_bitrate);

        av_log(s, AV_LOG_DEBUG, "  Detected bitrate is %.2fbps [%d/%d]\n", av_q2d(bitrate), bitrate.num, bitrate.den);
        av_log(s, AV_LOG_DEBUG, "     delta bitrate is %.2f    [%d/%d]\n", av_q2d(bitrate_delta), bitrate_delta.num, bitrate_delta.den);

        for (int j = 0; j < sizeof(bitdepths) / sizeof(int); j++) {
            int bitdepth = bitdepths[j];
            int bitrate_per_channel = sample_rate * bitdepth;

            AVRational n_channels =
                av_div_q(bitrate, av_make_q(bitrate_per_channel, 1));
            double n_ch = av_q2d(n_channels);
            double delta = FFABS(n_ch - (int)round(n_ch));
            av_log(s, AV_LOG_DEBUG,
                   "   %5.2f channels, %d-bit (%f channels, delta is %f) [%d/%d]\n",
                   n_ch, bitdepth, n_ch, delta, n_channels.num, n_channels.den);
            formats[n].sample_rate = sample_rate;
            formats[n].bps = bitdepth;

            if (FFABS(av_q2d(bitrate_delta)) < 100 &&
                (delta < 0.001 || n_channels.den == 1)) {
                av_log(s, AV_LOG_DEBUG, "    ^candidate\n");
                if (n_channels.den == 1) {
                    formats[n].n_channels = n_channels.num;
                } else {
                    formats[n].n_channels = round(n_ch);
                }
                formats[n].aspp =
                    (packet_size * 8) / formats[n].n_channels / bitdepth;
            } else {
                formats[n].n_channels = -1;
            }
            n++;
        }
    }
    for (int i = 0; i < sizeof(formats) / sizeof(AudioFormat); i++) {
        if ((context->audio_format.sample_rate != -1 && formats[i].sample_rate != context->audio_format.sample_rate) ||
            (context->audio_format.bps != -1 && formats[i].bps != context->audio_format.bps) ||
            (context->audio_format.n_channels != -1 && formats[i].n_channels != context->audio_format.n_channels))
            continue;

        if (formats[i].n_channels > 0) {
            context->audio_format = formats[i];
            break;
        }
    }
    if (context->audio_format.sample_rate == -1 || context->audio_format.bps == -1 || context->audio_format.n_channels == -1) {
        av_log(s, AV_LOG_ERROR, "Could not detect audio format\n");
        return AVERROR(EIO);
    }
    av_log(s, AV_LOG_DEBUG, "Detected audio format:\n");
    av_log(s, AV_LOG_DEBUG, "  %d Hz\n", context->audio_format.sample_rate);
    av_log(s, AV_LOG_DEBUG, "  %d-bit samples\n", context->audio_format.bps);
    av_log(s, AV_LOG_DEBUG, "  %d channels\n", context->audio_format.n_channels);
    av_log(s, AV_LOG_DEBUG, "  %d samples per packet\n", context->audio_format.aspp);

    return 0;
}

static int avfifo_configure_audio(AVFormatContext *s, AvFifo_RxFifo *fifo,
                                  int stream_id)
{
    DekTecDemuxContext *context = (DekTecDemuxContext *)s->priv_data;
    unsigned int result = 0;
    St2110_RxConfigAudio audio_config;
    const AVCodec *codec = NULL;
    AVStream *st = NULL;
    int ret = 0;
    AudioFormat *fmt = &context->audio_format;

    ret = avfifo_autodetect_audio(s, fifo, stream_id);
    if (ret != 0)
        return -1;

    if (fmt->bps == 16) {
        codec = avcodec_find_decoder(AV_CODEC_ID_PCM_S16BE);
    } else if (fmt->bps == 24) {
        codec = avcodec_find_decoder(AV_CODEC_ID_PCM_S24BE);
    }
    st = avformat_new_stream(s, codec);
    st->id = stream_id;
    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    if (fmt->bps == 16) {
        st->codecpar->codec_id = AV_CODEC_ID_PCM_S16BE;
        st->codecpar->format = AV_SAMPLE_FMT_S16;
        audio_config.Format = St2110_AudioFormat_L16BE;
    } else if (fmt->bps == 24) {
        st->codecpar->codec_id = AV_CODEC_ID_PCM_S24BE;
        st->codecpar->format = AV_SAMPLE_FMT_S32;
        audio_config.Format = St2110_AudioFormat_L24BE;
    } else {
        av_log(s, AV_LOG_ERROR, "Unsupported audio format: %d-bit\n",
               fmt->bps);
        return AVERROR(EIO);
    }
    st->codecpar->ch_layout.nb_channels = fmt->n_channels;
    st->codecpar->sample_rate = fmt->sample_rate;
    st->codecpar->bits_per_coded_sample = st->codecpar->bits_per_raw_sample =
        fmt->bps;
    st->codecpar->block_align =
        st->codecpar->bits_per_coded_sample * st->codecpar->ch_layout.nb_channels / 8;
    st->codecpar->bit_rate = (int64_t)st->codecpar->sample_rate *
                             st->codecpar->bits_per_coded_sample *
                             st->codecpar->ch_layout.nb_channels;
    av_channel_layout_default(&st->codecpar->ch_layout, st->codecpar->ch_layout.nb_channels);
    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    audio_config.SampleRate = st->codecpar->sample_rate;
    result = AvFifo_RxFifo_ConfigureAudio(fifo, &audio_config);
    if (result != DTAPI_OK) {
        const char *message = ff_dektec_avfifo_result_to_string(result);
        av_log(s, AV_LOG_ERROR, "Could not configure audio for RxFifo: %s\n", message);
        return -1;
    }

    context->audio_buffer_size = av_samples_get_buffer_size(
        NULL, st->codecpar->ch_layout.nb_channels, (st->codecpar->sample_rate / 1000) * 32,
        st->codecpar->format, 0);
    context->audio_buffer_size = st->codecpar->ch_layout.nb_channels *
                                 ((st->codecpar->sample_rate / 1000) * 32) *
                                 (st->codecpar->bits_per_coded_sample / 8);
    context->audio_buffer = av_fifo_alloc2(context->audio_buffer_size, 1, 0);
    context->audio_buffer_pts = -1;

    return 1;
}

static int avfifo_read_video(AVFormatContext *s, AvFifo_RxFifo *fifo,
                             AVPacket *pkt)
{
    DekTecDemuxContext *context = (DekTecDemuxContext *)s->priv_data;
    unsigned int result = 0;
    AVStream *st = s->streams[pkt->stream_index];
    AVCodecParameters *par = st->codecpar;

    if (AvFifo_RxFifo_GetFifoLoad(fifo) >= 1) {
        AvFifo_Frame *frame = AvFifo_RxFifo_Read(fifo);
        int64_t pts = 0;
        int frame_duration = 0;
        int64_t pts_gap = 0;
        int frame_size = 0;

        if (!frame) {
            const char *message = ff_dektec_avfifo_result_to_string(DTAPI_E_EXCEPTION);
            av_log(s, AV_LOG_ERROR, "Could not read frame: %s\n", message);
            return -1;
        }

        if (context->is_interlaced && context->last_field >= 0 && context->last_field == frame->Field) {
            av_log(s, AV_LOG_WARNING,
                   "Missed field %d, last_field=%d, current_field=%d\n",
                   !context->last_field, context->last_field, frame->Field);
        }

        if (context->is_interlaced && frame->Field == 0) {
            if (context->field0) {
                result = AvFifo_RxFifo_ReturnToMemPool(fifo, context->field0);
                if (result != DTAPI_OK) {
                    const char *message = ff_dektec_avfifo_result_to_string(result);
                    av_log(s, AV_LOG_ERROR, "Could not return to memory pool: %s\n", message);
                    return -1;
                }
                context->field0 = NULL;
            }
            context->field0 = frame; // Keep field
            context->last_field = frame->Field;
            return FFERROR_REDO;
        }
        if (frame->Field == 1 && context->field0 == NULL) {
            result = AvFifo_RxFifo_ReturnToMemPool(fifo, frame);
            if (result != DTAPI_OK) {
                const char *message = ff_dektec_avfifo_result_to_string(result);
                av_log(s, AV_LOG_ERROR, "Could not return to memory pool: %s\n", message);
                return -1;
            }
            context->last_field = frame->Field;
            return FFERROR_REDO;
        }

        pts = avfifo_read_pts_video(frame);
        // If the video is interlaced use the PTS from the first field.
        if (context->is_interlaced && context->field0 != NULL) {
            pts = avfifo_read_pts_video(context->field0);
        }

        frame_duration =
            av_rescale_q(1, av_inv_q(st->avg_frame_rate), context->time_scale);
        pts_gap = FFABS(pts - context->last_pts - frame_duration);
        if (context->last_pts > 0 && pts_gap > 5000000) {
            av_log(
                s, AV_LOG_WARNING,
                "Unexpected gap in PTS last_pts=%"PRId64", pts=%"PRId64", delta=%"PRId64", frame_duration=%d\n",
                context->last_pts, pts, pts - context->last_pts, frame_duration);
        }
        context->last_pts = pts;

        frame_size =
            av_image_get_buffer_size(par->format, par->width, par->height, 1);
        av_new_packet(pkt, frame_size);
        pkt->pos = -1;
        pkt->size = frame_size;
        pkt->stream_index = st->id;
        pkt->pts = pkt->dts = av_rescale_q(pts, context->time_scale, st->time_base);
        pkt->pts = pkt->dts = pkt->pts - av_rescale_q(context->start_time, context->time_scale, st->time_base);
        pkt->duration = av_rescale_q(1, av_inv_q(st->r_frame_rate), st->time_base);
        av_log(s, AV_LOG_TRACE,
               "pkt->pts=%" PRId64 ", pts=%" PRId64 ", context->timescale=%d/%d, "
               "st->time_base=%d/%d\n",
               pkt->pts, pts, context->time_scale.num, context->time_scale.den,
               st->time_base.num, st->time_base.den);


        if (context->is_interlaced) {
            context->copy_interlaced(context->field0, frame, pkt, par);

            result = AvFifo_RxFifo_ReturnToMemPool(fifo, context->field0);
            if (result != DTAPI_OK) {
                const char *message = ff_dektec_avfifo_result_to_string(result);
                av_log(s, AV_LOG_ERROR, "Could not return to memory pool: %s\n", message);
                return -1;
            }
            context->field0 = NULL;
        } else {
            context->copy(frame, pkt, par);
        }

        context->last_field = frame->Field;

        result = AvFifo_RxFifo_ReturnToMemPool(fifo, frame);
        if (result != DTAPI_OK) {
            const char *message = ff_dektec_avfifo_result_to_string(result);
            av_log(s, AV_LOG_ERROR, "Could not return to memory pool: %s\n", message);
            return -1;
        }
        return 0;
    }

    return AVERROR(EAGAIN);
}

static int avfifo_read_audio(AVFormatContext *s, AvFifo_RxFifo *fifo,
                             AVPacket *pkt)
{
    DekTecDemuxContext *context = (DekTecDemuxContext *)s->priv_data;
    unsigned int result = 0;
    AVStream *st = s->streams[pkt->stream_index];
    AVCodecParameters *par = st->codecpar;
    AvFifo_Frame *frame = NULL;
    int64_t pts = 0;
    int64_t next_pts = 0;
    int free_space = 0;

    while (1) {
        while (AvFifo_RxFifo_GetFifoLoad(fifo) <= 0) {
            av_usleep(1000);

            if (interrupted(s))
                return AVERROR_EXIT;
        }

        frame = AvFifo_RxFifo_Read(fifo);
        if (!frame) {
            const char *message = ff_dektec_avfifo_result_to_string(DTAPI_E_EXCEPTION);
            av_log(s, AV_LOG_ERROR, "Could not read frame: %s\n", message);
            return -1;
        }

        pts = avfifo_read_pts_audio(frame, par->sample_rate) - context->start_time;
        pts = av_rescale_q(pts, context->time_scale, st->time_base);
        if (av_fifo_can_read(context->audio_buffer) == 0) {
            context->audio_buffer_pts = pts;
        }
        next_pts =
            context->audio_buffer_pts +
            av_get_audio_frame_duration2(par, (int)av_fifo_can_read(context->audio_buffer));

        if (pts != next_pts) {
            av_log(s, AV_LOG_DEBUG,
                   "Unexpected gap in PTS (%"PRId64" samples missed)\n",
                   pts - next_pts);

            av_new_packet(pkt, (int)av_fifo_can_read(context->audio_buffer));
            pkt->stream_index = st->id;
            pkt->pts = pkt->dts = context->audio_buffer_pts;
            pkt->duration = av_get_audio_frame_duration2(par, pkt->size);
            av_fifo_read(context->audio_buffer, pkt->data, pkt->size);

            av_fifo_write(context->audio_buffer, frame->Data,
                          frame->NumValidBytes);
            context->audio_buffer_pts = pts;

            result = AvFifo_RxFifo_ReturnToMemPool(fifo, frame);
            if (result != DTAPI_OK) {
                const char *message = ff_dektec_avfifo_result_to_string(result);
                av_log(s, AV_LOG_ERROR, "Could not return to memory pool: %s\n", message);
                return -1;
            }
            return 0;
        }

        free_space = (int)av_fifo_can_write(context->audio_buffer);
        if (free_space < frame->NumValidBytes) {
            av_fifo_write(context->audio_buffer, frame->Data,
                                  free_space);

            av_new_packet(pkt, (int)av_fifo_can_read(context->audio_buffer));
            pkt->stream_index = st->id;
            pkt->pts = pkt->dts = context->audio_buffer_pts;
            pkt->duration = av_get_audio_frame_duration2(par, pkt->size);
            av_fifo_read(context->audio_buffer, pkt->data, pkt->size);

            av_fifo_write(context->audio_buffer,
                                  frame->Data + free_space,
                                  frame->NumValidBytes - free_space);

            context->audio_buffer_pts =
                pts + av_get_audio_frame_duration2(par, free_space);

            result = AvFifo_RxFifo_ReturnToMemPool(fifo, frame);
            if (result != DTAPI_OK) {
                const char *message = ff_dektec_avfifo_result_to_string(result);
                av_log(s, AV_LOG_ERROR, "Could not return to memory pool: %s\n",
                       message);
                return -1;
            }
            return 0;
        } else {
            av_fifo_write(context->audio_buffer, frame->Data,
                          frame->NumValidBytes);
        }

        result = AvFifo_RxFifo_ReturnToMemPool(fifo, frame);
        if (result != DTAPI_OK) {
            const char *message = ff_dektec_avfifo_result_to_string(result);
            av_log(s, AV_LOG_ERROR, "Could not return to memory pool: %s\n", message);
            return -1;
        }
        
        if (interrupted(s))
            return AVERROR_EXIT;
    }

    return AVERROR(EAGAIN);
}

#if CONFIG_LIBCDTAPI_NMOS
/**
 * Return nonzero when the user stops the program, for ff_dektec_nmos_wait().
 */
static int nmos_interrupted(void *opaque)
{
    return interrupted(opaque);
}

/**
 * Open the NMOS node when nmos_registry is given, register each of the nb FIFOs as a
 * receiver, "video" and "audio 0", "audio 1" and so on, and wait until a controller has
 * connected those whose URL is nmos.
 *
 * @param urls  the URL of each FIFO
 * @return 0, or a negative AVERROR after logging why
 */
static int nmos_open(AVFormatContext *s, char *const *urls, int nb)
{
    DekTecDemuxContext *context = (DekTecDemuxContext *)s->priv_data;
    FFDektecNmosOptions options = { context->nmos_registry, context->nmos_label,
                                    context->nmos_host, context->nmos_port };
    int audio = 0;
    int waits = 0;
    int ret;

    for (int i = 0; i < nb; i++)
        waits |= !strcmp(urls[i], "nmos");
    if (!context->nmos_registry || !*context->nmos_registry) {
        if (!waits)
            return 0;
        av_log(s, AV_LOG_ERROR, "The URL nmos needs nmos_registry\n");
        return AVERROR(EINVAL);
    }
    ret = ff_dektec_nmos_open(s, &options, context->device, context->serial_number,
                              context->port, &context->nmos);
    if (ret < 0)
        return ret;
    for (int i = 0; i < nb && ret >= 0; i++) {
        // The video stream comes first, as its URL is the first one.
        int video = urls[i] == context->url[0];
        char name[32];
        if (video)
            snprintf(name, sizeof(name), "video");
        else
            snprintf(name, sizeof(name), "audio %d", audio++);
        ret = ff_dektec_nmos_add_receiver(
            context->nmos, context->fifos[i], video ? AVMEDIA_TYPE_VIDEO : AVMEDIA_TYPE_AUDIO,
            strcmp(urls[i], "nmos") ? &context->ippars[i] : NULL, name);
    }
    if (ret >= 0 && waits)
        ret = ff_dektec_nmos_wait(context->nmos, context->nmos_wait, nmos_interrupted, s);
    if (ret < 0)
        ff_dektec_nmos_close(&context->nmos);
    return ret;
}

/**
 * Fix the format of each receiver now that its stream is open.
 */
static void nmos_set_streams(AVFormatContext *s)
{
    DekTecDemuxContext *context = (DekTecDemuxContext *)s->priv_data;

    for (int i = 0; i < s->nb_streams; i++) {
        const AVStream *st = s->streams[i];
        ff_dektec_nmos_set_stream(context->nmos, i,
                                  st->codecpar->format == AV_PIX_FMT_UYVY422
                                      ? St2110_RxFrameFormat_Uyvy422_8b
                                      : St2110_RxFrameFormat_Uyvy422_10b,
                                  st);
    }
}

/**
 * Apply a controller's change, if one waits. A video receiver that changed starts its
 * fields anew, and the statistics the FIFO started again are not reported as a change.
 */
static void nmos_poll(AVFormatContext *s)
{
    DekTecDemuxContext *context = (DekTecDemuxContext *)s->priv_data;
    int idx = ff_dektec_nmos_poll(context->nmos);

    if (idx < 0)
        return;
    if (s->streams[idx]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (context->field0) {
            AvFifo_RxFifo_ReturnToMemPool(context->fifos[idx], context->field0);
            context->field0 = NULL;
        }
        context->last_field = -1;
    }
    context->statistics[idx] = AvFifo_RxFifo_GetStatistics(context->fifos[idx]);
}
#endif

static int avfifo_read_header(AVFormatContext *s)
{
    DekTecDemuxContext* context = (DekTecDemuxContext*)s->priv_data;
    int ret = 0;
    int nb_streams = 0;
    int stream_index = 0;
    char *urls[MAX_STREAMS];        // The URL of each FIFO, in the order of the streams

    context->last_pts = -1;
    context->last_field = -1;
    context->time_scale = av_make_q(1, 1000000000);
    context->start_time = -1;
    context->start_tod.Seconds = context->start_tod.Nanoseconds = 0;

    for (int i = 0; i < MAX_STREAMS; i++) {
        if (context->url[i]) {
            nb_streams++;
        }
    }
    if (!nb_streams) {
        av_log(s, AV_LOG_ERROR, "Please provide url:v or url:a argument\n");
        return AVERROR(EINVAL);
    }
    
    context->fifos = av_calloc(MAX_STREAMS, sizeof(AvFifo_RxFifo*));

    // Each stream's FIFO is attached and given its URL first, so that NMOS can connect
    // the streams whose URL is nmos before their formats are detected.
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (!context->url[i]) {
            continue;
        }
        ret = avfifo_init_rxfifo(s, &context->fifos[stream_index], context->url[i]);
        if (ret < 0) {
            return ret;
        }
        urls[stream_index++] = context->url[i];
    }

#if CONFIG_LIBCDTAPI_NMOS
    ret = nmos_open(s, urls, stream_index);
    if (ret < 0)
        return ret;
#endif

    for (int idx = 0; idx < stream_index; idx++) {
        if (urls[idx] == context->url[0]) {
            ret = avfifo_configure_video(s, context->fifos[idx], idx);
        } else {
            ret = avfifo_configure_audio(s, context->fifos[idx], idx);
        }
        if (ret < 0) {
            return ret;
        }
    }

#if CONFIG_LIBCDTAPI_NMOS
    nmos_set_streams(s);
#endif
    return 1;
}

static int avfifo_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    DekTecDemuxContext *context = (DekTecDemuxContext *)s->priv_data;
    AvFifo_RxFifo *fifo = NULL;
    AVStream *st = NULL;
    AVCodecParameters *par = NULL;
    int ret = 0;
    RxStatistics statistics;
    int max_fifo_load = 0;

    if (context->start_tod.Seconds == 0 &&
        context->start_tod.Nanoseconds == 0) {
        unsigned int result = 0;

        for (int idx = 0; idx < MAX_STREAMS; idx++) {
            if (context->fifos[idx]) {
                result = AvFifo_RxFifo_Start(context->fifos[idx]);
                if (result != DTAPI_OK) {
                    const char *message = ff_dektec_avfifo_result_to_string(result);
                    av_log(s, AV_LOG_ERROR, "Could not start RxFifo: %s\n",
                            message);
                    return -1;
                }
            }
        }

        result = DtDevice_GetTimeOfDay(context->device, &context->start_tod);
        if (result != DTAPI_OK) {
            const char *message = ff_dektec_avfifo_result_to_string(result);
            av_log(s, AV_LOG_ERROR, "Error getting time of day: %s\n", message);
            return AVERROR(EIO);
        }

        context->start_time =
            (((int64_t)context->start_tod.Seconds) * 1000000000) +
            context->start_tod.Nanoseconds;
    }

#if CONFIG_LIBCDTAPI_NMOS
    nmos_poll(s);
#endif
    while (!fifo || !st || !par) {
        for (int i = 0; i < MAX_STREAMS; i++) {
            if (context->fifos[i]) {
                int fifo_load =
                    (AvFifo_RxFifo_GetFifoLoad(context->fifos[i]) * 100) /
                    AvFifo_RxFifo_GetMaxSize(context->fifos[i]);
                if (fifo_load > max_fifo_load) {
                    fifo = context->fifos[i];
                    st = s->streams[i];
                    par = st->codecpar;
                    pkt->stream_index = i;
                    max_fifo_load = fifo_load;
                }
            }
        }

        if (interrupted(s))
            return AVERROR_EXIT;
        
        if (!fifo || !st || !par) {
            av_usleep(5000);
#if CONFIG_LIBCDTAPI_NMOS
            // A disabled receiver delivers nothing; only a change can enable it again.
            nmos_poll(s);
#endif
        }
    }

    if (!fifo || !st || !par) {
        return AVERROR(EAGAIN);
    }

    if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
        ret = avfifo_read_video(s, fifo, pkt);
    } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
        ret = avfifo_read_audio(s, fifo, pkt);
    }

    statistics = AvFifo_RxFifo_GetStatistics(fifo);
    if (context->statistics[pkt->stream_index].FramesIncomplete != statistics.FramesIncomplete ||
        context->statistics[pkt->stream_index].FramesSizeError != statistics.FramesSizeError ||
        context->statistics[pkt->stream_index].Gaps != statistics.Gaps ||
        context->statistics[pkt->stream_index].IpPacketErrors != statistics.IpPacketErrors ||
        context->statistics[pkt->stream_index].DroppedFrames != statistics.DroppedFrames ||
        context->statistics[pkt->stream_index].SyncErrors != statistics.SyncErrors) {
        av_log(s, AV_LOG_WARNING, "RxStatistics changed\n");
        av_log(s, AV_LOG_WARNING, "  FramesOk=%d\n", statistics.FramesOk);
        av_log(s, AV_LOG_WARNING, "  FramesIncomplete=%d\n", statistics.FramesIncomplete);
        av_log(s, AV_LOG_WARNING, "  FramesSizeError=%d\n", statistics.FramesSizeError);
        av_log(s, AV_LOG_WARNING, "  Gaps=%d\n", statistics.Gaps);
        av_log(s, AV_LOG_WARNING, "  IpPacketErrors=%d\n", statistics.IpPacketErrors);
        av_log(s, AV_LOG_WARNING, "  DroppedFrames=%d\n", statistics.DroppedFrames);
        av_log(s, AV_LOG_WARNING, "  SyncErrors=%d\n", statistics.SyncErrors);
        context->statistics[pkt->stream_index] = statistics;
    }
    return ret;
}

static int avfifo_read_close(AVFormatContext *s)
{
    DekTecDemuxContext *context = (DekTecDemuxContext *)s->priv_data;
    unsigned int result = 0;

#if CONFIG_LIBCDTAPI_NMOS
    // Closed before the FIFOs stop, so that no controller's request waits for them.
    ff_dektec_nmos_close(&context->nmos);
#endif
    for (int i = 0; i < s->nb_streams; i++) {
        result = AvFifo_RxFifo_Stop(context->fifos[i]);
        if (result != DTAPI_OK) {
            const char *message = ff_dektec_avfifo_result_to_string(result);
            av_log(s, AV_LOG_ERROR, "Error stopping RxFifo: %s\n", message);
            return -1;
        }
        AvFifo_RxFifo_Freep(&context->fifos[i]);
    }
    av_freep(&context->fifos);

    return 1;
}

static int ff_dektec_read_header(AVFormatContext *s)
{
    DekTecDemuxContext* context = (DekTecDemuxContext*)s->priv_data;
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
                context->read_header = avfifo_read_header;
                context->read_packet = avfifo_read_packet;
                context->read_close = avfifo_read_close;
            } else {
                context->read_header = inpchannel_read_header;
                context->read_packet = inpchannel_read_packet;
                context->read_close = inpchannel_read_close;
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
        av_log(s, AV_LOG_ERROR, "Could not attach to %"PRId64"\n\n", context->serial_number);
        return -1;
    }

    return context->read_header(s);
}
static int ff_dektec_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    DekTecDemuxContext *context = (DekTecDemuxContext *)s->priv_data;
    return context->read_packet(s, pkt);
}
static int ff_dektec_read_close(AVFormatContext *s)
{
    DekTecDemuxContext *context = (DekTecDemuxContext *)s->priv_data;
    return context->read_close(s);
}

static int ff_dektec_list_input_devices(AVFormatContext *s, struct AVDeviceInfoList *device_list)
{
    return ff_dektec_list_devices(device_list);
}

#define OFFSET(x) offsetof(DekTecDemuxContext, x)
static const AVOption options[] = {
    { "sdi_standard", "", OFFSET(option_standard), AV_OPT_TYPE_STRING, {.str = ""}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, NULL},
    { "timestamp_align", "capture start time alignment (in seconds)", OFFSET(timestamp_align), AV_OPT_TYPE_DURATION, { .i64 = 0 }, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM, NULL},
    { "signal_timeout", "how long to wait for an SDI signal, negative without limit", OFFSET(signal_timeout), AV_OPT_TYPE_DURATION, { .i64 = 5000000 }, -INT64_MAX, INT64_MAX, AV_OPT_FLAG_DECODING_PARAM, NULL},
    { "threads", "threads an SDI frame is converted over: auto, 1 for one, or more", OFFSET(threads), AV_OPT_TYPE_INT, { .i64 = FF_DEKTEC_THREADS_AUTO }, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM, "threads"},
    { "auto", "4 threads, and as many pieces as the standard calls for", 0, AV_OPT_TYPE_CONST, { .i64 = FF_DEKTEC_THREADS_AUTO }, 0, 0, AV_OPT_FLAG_DECODING_PARAM, "threads"},

    { "pt",   "RTP payload type", OFFSET(pt),    AV_OPT_TYPE_INT, {.i64 = 96}, 96, 127, AV_OPT_FLAG_DECODING_PARAM, NULL},

    { "url:v",   "Destination IP or hostname and port", OFFSET(url[0]), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, NULL},
    { "url:a",   "Destination IP or hostname and port", OFFSET(url[1]), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, NULL},
    { "url:a:0", "Destination IP or hostname and port", OFFSET(url[1]), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, NULL},
    { "url:a:1", "Destination IP or hostname and port", OFFSET(url[2]), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, NULL},
    { "url:a:2", "Destination IP or hostname and port", OFFSET(url[3]), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, NULL},
    { "url:a:3", "Destination IP or hostname and port", OFFSET(url[4]), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, NULL},
    { "url:a:4", "Destination IP or hostname and port", OFFSET(url[5]), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, NULL},
    { "url:a:5", "Destination IP or hostname and port", OFFSET(url[6]), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, NULL},
    { "url:a:6", "Destination IP or hostname and port", OFFSET(url[7]), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, NULL},
    { "url:a:7", "Destination IP or hostname and port", OFFSET(url[8]), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, NULL},

    { "sample_rate", "Audio sample rate",        OFFSET(audio_format.sample_rate), AV_OPT_TYPE_INT, {.i64 = -1}, -1, 96000, AV_OPT_FLAG_DECODING_PARAM, NULL},
    { "bps",         "Audio bits per sample",    OFFSET(audio_format.bps),         AV_OPT_TYPE_INT, {.i64 = -1}, -1,    24, AV_OPT_FLAG_DECODING_PARAM, NULL},
    { "n_channels",  "Number of audio channels", OFFSET(audio_format.n_channels),  AV_OPT_TYPE_INT, {.i64 = -1}, -1,    64, AV_OPT_FLAG_DECODING_PARAM, NULL},
#if CONFIG_LIBCDTAPI_NMOS
    { "nmos_registry", "register the SMPTE 2110 streams with this NMOS registry, or auto to find one", OFFSET(nmos_registry), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, NULL},
    { "nmos_label", "the label of the NMOS node; ffmpeg-<serial>:<port> when not given", OFFSET(nmos_label), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, NULL},
    { "nmos_host", "the address at which controllers reach the NMOS node", OFFSET(nmos_host), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, NULL},
    { "nmos_port", "the port of the NMOS node's APIs, 0 for any free one", OFFSET(nmos_port), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 65535, AV_OPT_FLAG_DECODING_PARAM, NULL},
    { "nmos_wait", "how long to wait for a controller to connect the streams whose URL is nmos, negative without limit", OFFSET(nmos_wait), AV_OPT_TYPE_DURATION, {.i64 = 60000000}, -INT64_MAX, INT64_MAX, AV_OPT_FLAG_DECODING_PARAM, NULL},
#endif

    { NULL },
};

static const AVClass dektec_demuxer_class = {
    .class_name = "DekTec indev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT,
};

const FFInputFormat ff_dektec_demuxer = {
    .p.name          = "dektec",
    .p.long_name     = NULL_IF_CONFIG_SMALL("DekTec input"),
    .p.flags         = AVFMT_NOFILE,
    .p.priv_class    = &dektec_demuxer_class,
    .priv_data_size  = sizeof(DekTecDemuxContext),
    .read_header     = ff_dektec_read_header,
    .read_packet     = ff_dektec_read_packet,
    .read_close      = ff_dektec_read_close,
    .get_device_list = ff_dektec_list_input_devices,
};
