/*
 * SDI muxer
 * Copyright (c) 2019 DekTec, Werner Damman
 * Copyright (c) 2020-2024 DekTec, Jeroen Steendam
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
 * @file
 * Generate SDI stream in .sdi format.
 * @author Werner Damman
 * @author Jeroen Steendam
 */

/*
 * Terminology used in specification:
 */

#include "avformat.h"
#include "libavcodec/codec_id.h"
#include "mux.h"
#include "libavcodec/put_bits.h"
#include "libavutil/fifo.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/x86/cpu.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"
#include "sdicommon.h"
#include "sdienc_audio.h"
#include "sdienc_payloadid.h"

#include <inttypes.h>

#include "config.h"
#if HAVE_INTRINSICS_SSE2
#include <emmintrin.h> // SSE2 intrinsics
#include <immintrin.h> // Other intrinsics
#if defined(__GNUC__)
#pragma GCC target("ssse3")
#endif
#endif

#define MAX_STREAMS 8

typedef struct SdiMuxContext {
    const AVClass *av_class;

    uint32_t option_payload_id;     ///< option payload identifier
    uint32_t option_audio_nr_ch;    ///< option number of audio channels
    char* option_standard;          ///< option sdi standard
    int option_interleave_type;     ///< option interleave type
    int option_calc_crc;            ///< option to enable/disable CRC insertion
    int option_no_header;           ///< option to disable file header

    int frame_padding;              ///< nr. of padding bytes after a frame
    int src_width;                  ///< width of encapsulated picture
    int src_height;                 ///< height of encapsulated picture
    const struct SdiInfo *sdi_info; ///< constants for the standard used
    AVRational rrate;
    int is_sd;                      ///< true for 525/625 line standards
    int has_sub_images;             ///< true for 2160 line standards
    int is_interlaced_transport;    ///< true for interlaced transport
    uint32_t crc[8];                ///< running crc
    uint32_t out_word[2];           ///< running output word
    int bitpos;
    uint16_t *line_buf;
    uint16_t *blank;
    int num_frames;
    int64_t next_pts;
    AVRational time_base;

    SdiBuffer *buffer[MAX_STREAMS];

    struct SwsContext *scale_context; ///< swscale context
    AVFrame *scale_frame;             ///< scale frame

    SdiAudio *audio;          ///< audio related

    void (*from_planar)(const uint16_t *py, const uint16_t *pu,
                        const uint16_t *pv, uint16_t *sdi, int width);
} SdiMuxContext;

#if HAVE_INTRINSICS_SSE2
static int is_sse_aligned(const uint16_t *ptr)
{
    return (((intptr_t)ptr) % 16) == 0;
}
#endif

static void write_blanking(uint16_t* ptr, int length, uint16_t blanking_symbol)
{
    uint16_t *ptr_end = ptr + length;
#if HAVE_INTRINSICS_SSE2
    __m128i blanking;
    while (!is_sse_aligned(ptr) && ptr < ptr_end)
        *ptr++ = blanking_symbol;
    blanking = _mm_set1_epi16(blanking_symbol);
    while (ptr+7 < ptr_end) {
        _mm_stream_si128((__m128i*)ptr, blanking);
        ptr += 8;
    }
#endif
    while (ptr < ptr_end)
        *ptr++ = blanking_symbol;
}

static uint16_t get_sav_eav_word(int IsVanc, int IsField2, int IsEav)
{
    uint16_t  Pattern = 0x200 | (IsField2 ? 0x100 : 0) | (IsVanc ? 0x80 : 0) | (IsEav ? 0x40 : 0);
    Pattern |= (IsVanc ^ IsEav) ? 0x20 : 0; // p3
    Pattern |= (IsField2 ^ IsEav) ? 0x10 : 0; // p2
    Pattern |= (IsField2 ^ IsVanc) ? 0x8 : 0; // p1
    Pattern |= (IsField2 ^ IsVanc ^ IsEav) ? 0x4 : 0; // p0
    return Pattern;
}

static void from_planar_2si(const uint16_t *py, const uint16_t *pu,
                            const uint16_t *pv, uint16_t *sdi, int width)
{
    const uint16_t *py2 = py + width;
    const uint16_t *pu2 = pu + (width >> 1);
    const uint16_t *pv2 = pv + (width >> 1);
    while (width > 0) {
        width -= 4;
        *sdi++ = pu2[1];
        *sdi++ = pu[1];
        *sdi++ = pu2[0];
        *sdi++ = pu[0];
        *sdi++ = py2[1];
        *sdi++ = py[1];
        *sdi++ = py2[0];
        *sdi++ = py[0];
        *sdi++ = pv2[1];
        *sdi++ = pv[1];
        *sdi++ = pv2[0];
        *sdi++ = pv[0];
        *sdi++ = py2[3];
        *sdi++ = py[3];
        *sdi++ = py2[2];
        *sdi++ = py[2];
        pu += 2;
        pu2 += 2;
        pv += 2;
        pv2 += 2;
        py += 4;
        py2 += 4;
    }
}

#if HAVE_INTRINSICS_SSE2
static void from_planar_sse(const uint16_t *py, const uint16_t *pu,
                            const uint16_t *pv, uint16_t *sdi, int width)
{
    while (width >= 8) {
        __m128i y_symbols = _mm_loadu_si128((__m128i*)py); // 8 symbols
        __m128i u_symbols = _mm_loadl_epi64((__m128i*)pu); // 4 symbols
        __m128i v_symbols = _mm_loadl_epi64((__m128i*)pv); // 4 symbols

        __m128i uv_symbols = _mm_unpacklo_epi16(u_symbols, v_symbols); // Interleave U and V

        _mm_storeu_si128((__m128i*)sdi, _mm_unpacklo_epi16(uv_symbols, y_symbols)); // Interleave low part of UV and Y
        sdi += 8;
        _mm_storeu_si128((__m128i*)sdi, _mm_unpackhi_epi16(uv_symbols, y_symbols)); // Interleave low part of UV and Y
        sdi += 8;
        
        py += 8;
        pu += 4;
        pv += 4;
        width -= 8;
    }
}
#endif

/*
 * Copy planar pixels to SDI line. Width is the picture width in pixels.
 */
static void from_planar(const uint16_t *py, const uint16_t *pu,
                        const uint16_t *pv, uint16_t *sdi, int width)
{
    while (width > 0) {
        width -= 2;
        *sdi++ = *pu++;
        *sdi++ = *py++;
        *sdi++ = *pv++;
        *sdi++ = *py++;
    }
}

static int put_sdi_format(struct PutBitContext *pb, struct Format *format)
{
    if (pb != NULL) {
        put_bits(pb, 5, format->line_rate);
        put_bits(pb, 2, format->interleaving_type);
        switch(format->sdi_level) {
        case SDI_LEVEL_A:
            put_bits(pb, 2, 1); // SdiLevel
            put_bits(pb, 2, 0); // NumStreams
            break;
        case SDI_LEVEL_B_DL:
            put_bits(pb, 2, 2); // SdiLevel
            put_bits(pb, 2, 1); // NumStreams
            break;
        case SDI_LEVEL_B_DS:
            put_bits(pb, 2, 2); // SdiLevel
            put_bits(pb, 2, 2); // NumStreams
            break;
        default:
            put_bits(pb, 2, 0);
            put_bits(pb, 2, 0);
            break;
        }
        put_bits(pb, 5, 0); // reserved
    }
    return 16;
}

static int put_logical_frame_properties(struct PutBitContext *pb, struct LogicalFrameProperties *properties)
{
    if (pb != NULL) {
        put_bits32(pb, properties->picture_rate.num);
        put_bits32(pb, properties->picture_rate.den);
        put_bits(pb, 8, properties->aspect_ratio.num);
        put_bits(pb, 8, properties->aspect_ratio.den);
        put_bits(pb, 1, properties->is_interlaced);
        put_bits(pb, 4, properties->sampling_structure);
        put_bits(pb, 1, properties->is_stereoscopic);
        put_bits(pb, 2, 0); // reserved
        put_bits(pb, 8, properties->bit_depth);
        put_bits(pb, 16, properties->picture_width);
        put_bits(pb, 16, properties->picture_height);
    }
    return 128;
}

static int put_physical_field_properties(struct PutBitContext *pb, struct PhysicalFieldProperties *properties)
{
    if (pb != NULL) {
        put_bits(pb, 16, properties->num_lines_field);
        put_bits(pb, 16, properties->first_video_line);
        put_bits(pb, 16, properties->num_lines_video);
    }
    return 48;
}

static int put_physical_frame_properties(struct PutBitContext *pb, struct PhysicalFrameProperties *properties)
{
    int size = 56;

    if (pb != NULL) {
        put_bits(pb, 2, properties->num_fields);
        put_bits(pb, 1, properties->crc_omitted);
        put_bits(pb, 5, 0);
        put_bits(pb, 16, properties->num_lines_frame);
        put_bits(pb, 16, properties->num_syms_hanc);
        put_bits(pb, 16, properties->num_syms_vanc_video);
    }

    for (int i = 0; i < properties->num_fields; i++)
        size += put_physical_field_properties(pb, &properties->field_properties[i]);

    return size;
}

static int put_sdi_file_header(struct PutBitContext *pb, struct SdiFileHeader *header)
{
    int size = 136;

    if (pb != NULL) {
        put_bits32(pb, header->magic_code);
        put_bits(pb, 8, header->version);
        put_bits(pb, 16, header->header_size);
        put_bits(pb, 8, header->num_physical_links);
        put_bits32(pb, header->frame_size);
        put_bits32(pb, header->num_frames);
        put_bits(pb, 8, header->compression_mode);
    }

    size += put_sdi_format(pb, &header->format);
    size += put_logical_frame_properties(pb, &header->logical_frame_properties);
    size += put_physical_frame_properties(pb, &header->physical_frame_properties);

    return size;
}

static struct SdiFileHeader make_sdi_file_header(struct AVFormatContext *s, SdiMuxContext *sdi)
{
    struct SdiFileHeader hdr;
    struct PhysicalFieldProperties *field_props;
    const struct SdiInfo *sdi_info = sdi->sdi_info;
    int sdi_frame_size = sdi_info->nr_sdi_lines *
            (sdi_info->nr_hanc_symbols + sdi_info->nr_vanc_symbols) * 10 / 8;

    hdr.magic_code = SDI_MAGIC;
    hdr.version = 0;
    hdr.header_size = 0; // Is calculated at the end of this function
    hdr.num_physical_links = 1;
    hdr.frame_size = (sdi_frame_size + 7) & ~7;   // align to 8-byte
    hdr.num_frames = 0; // Is written in sdi_write_trailer()
    hdr.compression_mode = SDI_COMPRESSION_MODE_NONE;
    sdi->frame_padding = hdr.frame_size - sdi_frame_size;

    switch (sdi_info->payload_format) {
    case 0x81:
        hdr.format.line_rate = SDI_LINE_RATE_SD;
        hdr.format.interleaving_type = SDI_INTERLEAVING_TYPE_NONE;
        hdr.format.sdi_level = 0;
        break;
    case 0x84:
    case 0x85:
        hdr.format.line_rate = SDI_LINE_RATE_HD;
        hdr.format.interleaving_type = SDI_INTERLEAVING_TYPE_NONE;
        hdr.format.sdi_level = 0;
        break;
    case 0x89:
        hdr.format.line_rate = SDI_LINE_RATE_3G;
        hdr.format.interleaving_type = SDI_INTERLEAVING_TYPE_NONE;
        hdr.format.sdi_level = SDI_LEVEL_A;
        break;
    case 0xC0:
        hdr.format.line_rate = SDI_LINE_RATE_6G;
        hdr.format.interleaving_type = SDI_INTERLEAVING_TYPE_NONE;
        hdr.format.sdi_level = 0;
        break;
    case 0xCE:
        hdr.format.line_rate = SDI_LINE_RATE_12G;
        hdr.format.interleaving_type = SDI_INTERLEAVING_TYPE_2SI;
        hdr.format.sdi_level = 0;
        break;
    };

    hdr.logical_frame_properties.picture_rate = av_sdi_rate(sdi_info->picture_rate);
    hdr.logical_frame_properties.aspect_ratio = av_sdi_aspect_ratio(sdi_info->aspect_ratio);
    hdr.logical_frame_properties.is_interlaced = is_interlaced_picture(sdi_info->scanning_method);
    hdr.logical_frame_properties.sampling_structure = SDI_SAMPLING_YCbCr422;
    hdr.logical_frame_properties.is_stereoscopic = 0;
    hdr.logical_frame_properties.bit_depth = 10;
    hdr.logical_frame_properties.picture_width = sdi_info->picture_width;
    hdr.logical_frame_properties.picture_height = sdi_info->picture_height;

    field_props = hdr.physical_frame_properties.field_properties;
    if (is_interlaced_transport(sdi_info->scanning_method)) {
        field_props[0].num_lines_field = (sdi_info->end_line_field1 - sdi_info->start_line_field1) + 1;
        field_props[0].first_video_line = sdi_info->vid_start_line_field1;
        field_props[0].num_lines_video = (sdi_info->vid_end_line_field1 - sdi_info->vid_start_line_field1) + 1;

        field_props[1].num_lines_field = (sdi_info->end_line_field2 - sdi_info->start_line_field2) + 1;
        field_props[1].first_video_line = sdi_info->vid_start_line_field2 - sdi_info->start_line_field2;
        field_props[1].num_lines_video = (sdi_info->vid_end_line_field2 - sdi_info->vid_start_line_field2) + 1;

        hdr.physical_frame_properties.num_fields = 2;
    } else {
        field_props[0].num_lines_field = sdi_info->nr_sdi_lines;
        field_props[0].first_video_line = sdi_info->vid_start_line_field1;
        field_props[0].num_lines_video = sdi_info->picture_height;

        hdr.physical_frame_properties.num_fields = 1;
    }

    hdr.physical_frame_properties.crc_omitted = sdi->option_calc_crc == 0;
    hdr.physical_frame_properties.num_lines_frame = sdi_info->nr_sdi_lines;
    hdr.physical_frame_properties.num_syms_hanc = sdi_info->nr_hanc_symbols;
    hdr.physical_frame_properties.num_syms_vanc_video = sdi_info->nr_vanc_symbols;

    hdr.header_size = ((put_sdi_file_header(NULL, &hdr) >> 3) + 7) & ~7;

    return hdr;
}

static void write_sdi_header(struct AVFormatContext *s, SdiMuxContext *sdi)
{
    struct SdiFileHeader hdr;
    PutBitContext pb;
    uint8_t buffer[1024] = { 0 };
    
    hdr = make_sdi_file_header(s, sdi);

    init_put_bits(&pb, buffer, 1024);
    put_sdi_file_header(&pb, &hdr);
    flush_put_bits(&pb);

    avio_write(s->pb, buffer, hdr.header_size);
}

static void prepare_buffers(AVFormatContext *s)
{
    SdiMuxContext *sdi = s->priv_data;
    int line_len = sdi->sdi_info->nr_hanc_symbols + sdi->sdi_info->nr_vanc_symbols;
    int nr_ch = ff_sdi_get_nr_channels(sdi->sdi_info->payload_format);
    int i, N;

    sdi->line_buf = av_malloc(line_len * sizeof(uint16_t));
    sdi->blank = av_malloc(line_len * sizeof(uint16_t));

    for (int i = 0; i < FFMIN(s->nb_streams, MAX_STREAMS); ++i) {
        sdi->buffer[i] = ff_sdi_buffer_alloc(s->streams[i]);
        if (s->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            sdi->buffer[i]->max_buffer = 3;
        else if (s->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            sdi->buffer[i]->max_buffer = 3*800;
    }

    // Prepare buffer with blanking for this nr. of channels
    N = FFMAX(2, nr_ch);
    for (i = 0; i < line_len; i++)
        sdi->blank[i] = (i & (N - 1)) < (N / 2) ? 0x200 : 0x40;
}

static int init_scaling(AVFormatContext *s, SdiMuxContext *sdi, AVStream *stream)
{
    int src_width = stream->codecpar->width;
    int src_height = stream->codecpar->height;
    int src_format = stream->codecpar->format;
    int dst_width = sdi->sdi_info->picture_width;
    int dst_height = sdi->sdi_info->picture_height;
    int dst_format = AV_PIX_FMT_YUV422P10LE;

    if (src_width == dst_width && src_height == dst_height &&
        src_format == dst_format)
        return 0;

    sdi->scale_context = sws_getContext(src_width, src_height, src_format,
                                        dst_width, dst_height, dst_format,
                                        SWS_BICUBIC, NULL, NULL, NULL);
    if (sdi->scale_context == NULL) {
        av_log(s, AV_LOG_ERROR, "Cannot initialize the swscale context\n");
        return AVERROR(EINVAL);
    } else {
        sdi->scale_frame = av_frame_alloc();
        sdi->scale_frame->format = dst_format;
        sdi->scale_frame->width = dst_width;
        sdi->scale_frame->height = dst_height;
        av_frame_get_buffer(sdi->scale_frame, 0);

        av_log(s, AV_LOG_DEBUG, "Scale from (%dx%d, %s) to (%dx%d, %s)\n",
                        src_width, src_height, av_get_pix_fmt_name(src_format),
                        dst_width, dst_height, av_get_pix_fmt_name(dst_format));
    }
    return 0;
}

/*
 * First determine video parameters, then either check if it matches
 * the user-specified standard, if any, else use the best match.
 */
static int sdi_init(AVFormatContext *s)
{
    SdiMuxContext *sdi = s->priv_data;
    const struct SdiInfo *sdi_info = NULL;
    int width = s->streams[0]->codecpar->width;
    int height = s->streams[0]->codecpar->height;
    int ret = 0;
    StandardOption option;
    SdiFormat standard = SDI_FMT_NONE;
    AVStream *audio_stream = NULL;
    AVStream *video_stream = NULL;
    int sdi_frame_size;
    int aligned_frame_size;

    for (int i = 0; i < s->nb_streams; i++) {
        if (s->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (video_stream == NULL)
                video_stream = s->streams[i];
        }
        else if (s->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (audio_stream == NULL)
                audio_stream = s->streams[i];
        }

        if (audio_stream != NULL && video_stream != NULL)
            break;
    }

    if (video_stream) {
        av_log(s, AV_LOG_DEBUG, "codec_id0:  %d, format:%d\n",video_stream->codecpar->codec_id, video_stream->codecpar->format);
        av_log(s, AV_LOG_DEBUG, "scanning:   %d\n", video_stream->codecpar->field_order);
        av_log(s, AV_LOG_DEBUG, "frame rate: %d/%d Hz\n", video_stream->avg_frame_rate.num, video_stream->avg_frame_rate.den);
        av_log(s, AV_LOG_DEBUG, "image size: %dx%d\n", width, height);
    } else {
        av_log(s, AV_LOG_ERROR, "SDI requires a video stream\n");
        return AVERROR(EINVAL);
    }

    if (audio_stream) {
        av_log(s, AV_LOG_DEBUG, "codec_id1:  %d, format:%d bitrate:%zd\n",audio_stream->codecpar->codec_id, audio_stream->codecpar->format, audio_stream->codecpar->bit_rate);
    }

    /*
     * Setup video
     */
    sdi->num_frames = 0;
    s->packet_size = 10000;

    if (width > 3840 || height > 2160) {
        av_log(s, AV_LOG_ERROR, "Scale first, max image size is 3840x2160 for SDI\n");
        return AVERROR(EINVAL);
    }

    ret = av_parse_standard_option(s, sdi->option_standard, &option);
    if (ret < 0) {
        standard = av_find_matching_standard(s, NULL, video_stream);
    } else {
        standard = av_find_matching_standard(s, &option, video_stream);
    }

    if (standard == SDI_FMT_NONE) {
        av_log(s, AV_LOG_ERROR, "No suitable SDI standard found for these video parameters\n");
        return AVERROR(EINVAL);
    }
    sdi->sdi_info = sdi_info = av_sdi_info(standard);
    sdi->rrate = av_sdi_rate(sdi_info->picture_rate);
    sdi->next_pts = AV_NOPTS_VALUE;
    sdi->time_base = av_inv_q(sdi->rrate);

    av_log(s, AV_LOG_DEBUG, "SDI standard: %s\n", sdi_info->name);

    ret = init_scaling(s, sdi, video_stream);
    if (ret < 0)
        return ret;

    // Store info in private data. Some data is just for convenience.
    sdi->src_width = width;
    sdi->src_height = height;
    sdi->has_sub_images = ff_has_sub_images(sdi_info->payload_format);
    sdi->is_sd = ff_is_sd(sdi_info->payload_format);
    if (is_interlaced_transport(sdi_info->scanning_method)) {
        sdi->is_interlaced_transport = 1;
    } else {
        sdi->is_interlaced_transport = 0;
    }

    sdi_frame_size = sdi_info->nr_sdi_lines * (sdi_info->nr_hanc_symbols + sdi_info->nr_vanc_symbols) * 10 / 8;
    aligned_frame_size = (sdi_frame_size + 7) & ~7; // align to 8-byte
    sdi->frame_padding = aligned_frame_size - sdi_frame_size;

    prepare_buffers(s);

    av_log(s, AV_LOG_DEBUG, "Frame size %d with padding %d\n",
            sdi_info->nr_sdi_lines * (sdi_info->nr_hanc_symbols + sdi_info->nr_vanc_symbols) * 10 / 8,
            sdi->frame_padding);
    av_log(s, AV_LOG_DEBUG, "  Lines:   %d\n", sdi_info->nr_sdi_lines);
    av_log(s, AV_LOG_DEBUG, "  Hanc:    %d\n", sdi_info->nr_hanc_symbols);
    av_log(s, AV_LOG_DEBUG, "  Vanc:    %d\n", sdi_info->nr_vanc_symbols);
    av_log(s, AV_LOG_DEBUG, "  Padding: %d\n", sdi->frame_padding);

    /*
     * Setup audio
     */
    if (audio_stream) {
        sdi->audio = ff_sdi_audio_alloc();
        if (!sdi->audio)
            return AVERROR(ENOMEM);

        ret = ff_sdi_audio_init(sdi->audio, s, sdi->option_audio_nr_ch, sdi->buffer[audio_stream->index], sdi_info, audio_stream);
        if (ret != 0)
            return ret;
    }

    if (sdi->has_sub_images) {
        sdi->from_planar = from_planar_2si;
    }
#if HAVE_INTRINSICS_SSE2
    else if (X86_SSSE3(av_get_cpu_flags())) {
        sdi->from_planar = from_planar_sse;
    }
#endif
    else {
        sdi->from_planar = from_planar;
    }

    return 0;
}

static void sdi_deinit(AVFormatContext *s)
{
    SdiMuxContext *sdi = s->priv_data;
    av_free (sdi->blank);
    av_free (sdi->line_buf);
    ff_sdi_audio_freep(&sdi->audio);
    for (int i = 0; i < FFMIN(s->nb_streams, MAX_STREAMS); ++i) {
        ff_sdi_buffer_freep(&sdi->buffer[i]);
    }
    sws_freeContext(sdi->scale_context);
    sdi->scale_context = NULL;
    av_frame_free(&sdi->scale_frame);
}

static int sdi_write_sdi_header(AVFormatContext *s)
{
    SdiMuxContext *sdi = s->priv_data;
    if (!sdi->option_no_header) {
        write_sdi_header(s, sdi);
    }
    return 0;
}

/*
 * Write nr_ch eav/sav patterns, return nr. of symbols written
 */
static int store_eav_sav(uint16_t *buf, int nr_ch, uint16_t eav_sav)
{
    int i;
    for (i = 0; i < nr_ch; i++)
        *buf++ = 0x3ff;
    for (i = 0; i < nr_ch * 2; i++)
        *buf++ = 0x000;
    for (i = 0; i < nr_ch; i++)
        *buf++ = eav_sav;
    return nr_ch * 4;
}

/*
 * Write line numbers, return nr. of symbols written
 */
static int store_line_number(uint16_t *buf, int nr_ch, int LineNr)
{
    int i;
    uint16_t LN0 = (LineNr << 2) & (0x7f << 2);
    uint16_t LN1 = (LineNr >> 5) & (0x7f << 2);

    LN0 |= (~LN0 & 0x100) << 1;
    LN1 |= (~LN1 & 0x100) << 1;

    for (i = 0; i < nr_ch; i++)
        *buf++ = LN0;
    for (i = 0; i < nr_ch; i++)
        *buf++ = LN1;
    return nr_ch * 2;
}

/*
 * Write crc codes, return nr. of symbols written
 */
static int store_crc(uint16_t *buf, int nr_ch, uint32_t* crc)
{
    int i;
    for (i = 0; i < nr_ch; i++)
    {
        uint16_t CRC0 = (crc[i] >> 0) & 0x1ff;
        uint16_t CRC1 = (crc[i] >> 9) & 0x1ff;
        buf[i] = CRC0 | ((~CRC0 & 0x100) << 1);
        buf[i + nr_ch] = CRC1 | ((~CRC1 & 0x100) << 1);
    }
    return nr_ch * 2;
}

static void update_crc(uint32_t *crc, int nr_ch, uint16_t* pin, int n)
{
    // InvPolynom is x(18) + x(5) + x(4) + 1
    const uint32_t InvPolynom = (1 << (17 - 5)) | (1 << (17 - 4)) | (1 << 17);
    int i, bit;
    for (i = 0; i < n; i++)
    {
        // Update CRC, for the correct channel
        int  Ch = i & (nr_ch - 1);
        crc[Ch] ^= (uint32_t)(*pin);
        for (bit = 0; bit < 10; bit++)
        {
            if ((crc[Ch] & 1) != 0)
                crc[Ch] = (crc[Ch] >> 1) ^ InvPolynom;
            else
                crc[Ch] >>= 1;
        }
    }
}

/*
 * Write the 10 LSBs in each word of pin packed to the stream.
 */
static void write_10b_packed(AVIOContext *pb, SdiMuxContext *sdi,
        const uint16_t *pin, int len)
{
    #define Z (uint8_t)0x80
    #define multiplier (_mm_set_epi16(64, 16, 4, 1, 64, 16, 4, 1))
    #define shuffle_even (_mm_set_epi8(Z, Z, Z, Z, Z, Z, Z, 13, 12, 9, 8, Z, 5, 4, 1, 0))
    #define shuffle_odd (_mm_set_epi8(Z, Z, Z, Z, Z, Z, 15, 14, 11, 10, Z, 7, 6, 3, 2, Z))

    int symlen = 10;
    while (sdi->bitpos != 0 && len--) {
        // Store word
        sdi->out_word[0] |= *pin << sdi->bitpos;
        sdi->out_word[1] = (*pin >> (31 - sdi->bitpos)) >> 1;
        sdi->bitpos += symlen;
        // if 32 bits or more collected, store them
        if ((sdi->bitpos >> 5) > 0)
        {
            avio_write(pb, (const unsigned char*)sdi->out_word, 4);
            sdi->out_word[0] = sdi->out_word[1];
        }
        sdi->bitpos &= 0x1F;
        pin++;
    }
#if HAVE_INTRINSICS_SSE2
    if (X86_SSSE3(av_get_cpu_flags())) {
        uint8_t buffer[16];
        while (len >= 8) {
            __m128i symbols, symbols_even, symbols_odd;

            symbols = _mm_loadu_si128((__m128i*)pin);
            symbols = _mm_mullo_epi16(symbols, multiplier);
            symbols_even = _mm_shuffle_epi8(symbols, shuffle_even);
            symbols_odd = _mm_shuffle_epi8(symbols, shuffle_odd);
            _mm_storeu_si128((__m128i*)buffer, _mm_or_si128(symbols_even, symbols_odd));

            avio_write(pb, buffer, 10);
            pin += 8;
            len -= 8;
        }
    }
#endif
    while (len--) {
        // Store word
        sdi->out_word[0] |= *pin << sdi->bitpos;
        sdi->out_word[1] = (*pin >> (31 - sdi->bitpos)) >> 1;
        sdi->bitpos += symlen;
        // if 32 bits or more collected, store them
        if ((sdi->bitpos >> 5) > 0)
        {
            avio_write(pb, (const unsigned char*)sdi->out_word, 4);
            sdi->out_word[0] = sdi->out_word[1];
        }
        sdi->bitpos &= 0x1F;
        pin++;
    }

    #undef Z
    #undef multiplier
    #undef shuffle_even
    #undef shuffle_odd
}

/*
 * Write a new SDI frame.
 * Prerequisite: there should be enough audio in the FIFO!
 *
 */
static int write_sdi_frame(struct AVFormatContext *s, AVPacket *arg_pkt)
{
    int line = 0;
    SdiMuxContext *sdi = s->priv_data;
    const struct SdiInfo *info = sdi->sdi_info;
    int nr_ch = ff_sdi_get_nr_channels(info->payload_format);
    int line_len = info->nr_hanc_symbols + info->nr_vanc_symbols;
    int h_blank_cnt = info->nr_hanc_symbols - nr_ch * 8;
    int pad = sdi->frame_padding;
    uint16_t* py, *pu, *pv;
    int width = sdi->src_width;
    int read_stride[3] = {0};
    int h_start = 0;
    int h_end = 0;

    AVFrame *arg_frame;
    AVFrame *frame;

    arg_frame = (AVFrame*)arg_pkt->data;

    // Scale frame if needed
    if (sdi->scale_context) {
        int result = 0;

        frame = sdi->scale_frame;

        width = frame->width;

        result = sws_scale_frame(sdi->scale_context, frame, arg_frame);
        if (result <= 0) {
            return result;
        }
    } else {
        frame = arg_frame;
    }

    if (nr_ch > 1)
        h_blank_cnt -= nr_ch * (2 + 2); // line nr and crc

    // Set pointers to source video
    py = (uint16_t*)frame->data[0];
    pu = (uint16_t*)frame->data[1];
    pv = (uint16_t*)frame->data[2];

    // The read_stride is the stride in symbols that is used when reading the
    // input image. When using subimages, we read two video lines per SDI line.
    // And with interlaced transport, we read one video line per SDI line and
    // skip the next line (which is writter to the other field).
    read_stride[0] = frame->linesize[0] / 2;
    read_stride[1] = frame->linesize[1] / 2;
    read_stride[2] = frame->linesize[2] / 2;
    if (sdi->has_sub_images) {
        read_stride[0] *= 2;
        read_stride[1] *= 2;
        read_stride[2] *= 2;
    }
    if (is_interlaced_transport(info->scanning_method)) {
        read_stride[0] *= 2;
        read_stride[1] *= 2;
        read_stride[2] *= 2;
    }

    // Calculate position of the picture in the SDI frame
    if (!sdi->has_sub_images) {
        // Align to multiple of 4 (UYVY group)
        h_start = (2 * ((info->picture_width - width) >> 1)) & ~3;
        h_end = h_start + 2 * width;
    } else {
        // Align to multiple of 16
        h_start = (4 * ((info->picture_width - width) >> 1)) & ~15;
        h_end = h_start + 4 * width;
    }

    sdi->out_word[0] = sdi->out_word[1] = 0;
    sdi->bitpos = 0;

    if (sdi->audio) {
        sdi->audio->pkt_cnt = 0;
        for (int i = 0; i < s->nb_streams; i++) {
            if (s->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                int64_t next_pts = av_rescale_q_rnd(sdi->next_pts, sdi->time_base, s->streams[i]->time_base, AV_ROUND_UP);
                ff_sdi_audio_frame_start(sdi->audio, next_pts);
                break;
            }
        }
    }

    // Construct SDI frame
    for (line = 1; line <= info->nr_sdi_lines; line++) {
        uint16_t *line_ptr = sdi->line_buf;
        int used = 0;
        int hanc_avail = h_blank_cnt;

        // First determine line type
        int is_vb = ff_is_vbi_line(sdi->sdi_info, line);
        int is_f2 = ff_get_field_nr(sdi->sdi_info, line) == 2;
        uint16_t eav = get_sav_eav_word(is_vb, is_f2, 1);
        uint16_t sav = get_sav_eav_word(is_vb, is_f2, 0);

        // Construct hanc
        line_ptr += store_eav_sav(line_ptr, nr_ch, eav);
        if (nr_ch > 1) {
            line_ptr += store_line_number(line_ptr, nr_ch, line);
            if (sdi->option_calc_crc) {
                update_crc(sdi->crc, nr_ch, sdi->line_buf, line_ptr - sdi->line_buf);
            }
            line_ptr += store_crc(line_ptr, nr_ch, sdi->crc);
        }

        // Fill with blanking first, possible packet will overwrite the blanking
        write_blanking(line_ptr, hanc_avail, 0x200);

        // Insert payload ID packets
        used = ff_sdi_insert_payloadid(line_ptr, line, sdi->sdi_info);
        line_ptr += used;
        hanc_avail -= used;

        // Insert audio packets
        if (sdi->audio)
            ff_sdi_audio_insert(sdi->audio, line_ptr, hanc_avail, line);

        line_ptr += hanc_avail;
        line_ptr += store_eav_sav(line_ptr, nr_ch, sav);

        // Write vanc or video
        if (is_vb) {
            write_blanking(line_ptr, info->nr_vanc_symbols, 0x200);
        } else {
            // On the first line of field 2, reset pointers to line 1 of the
            // picture.
            if (line == info->vid_start_line_field2) {
                py = (uint16_t*)(frame->data[0] + frame->linesize[0]);
                pu = (uint16_t*)(frame->data[1] + frame->linesize[1]);
                pv = (uint16_t*)(frame->data[2] + frame->linesize[2]);
            }

            if (ff_is_video_line(info, line)) {
                if (h_start > 0)
                    write_blanking(line_ptr, h_start, 0x200);
                sdi->from_planar(py, pu, pv, line_ptr + h_start, width);
                py += read_stride[0];
                pu += read_stride[1];
                pv += read_stride[2];
                if (h_end < info->nr_vanc_symbols)
                    write_blanking(line_ptr + h_end, (info->nr_vanc_symbols - h_end), 0x200);
            } else {
                write_blanking(line_ptr, info->nr_vanc_symbols, 0x200);
            }
        }

        // calculate CRC over active line or vanc
        if (nr_ch > 1) {
            memset(sdi->crc, 0, nr_ch * sizeof(sdi->crc[0]));
            if (sdi->option_calc_crc) {
                update_crc(sdi->crc, nr_ch, line_ptr, line_len - (line_ptr - sdi->line_buf));
            }
        }

        // convert line to 10-bit, write to file
        write_10b_packed(s->pb, sdi, sdi->line_buf, line_len);
    }

    sdi->next_pts += 1;

    if (sdi->audio) {
        int queued_samples = sdi->audio->queued_samples / sdi->audio->nr_ch;
        AVRational sample_duration = {1000, sdi->audio->rate};
        AVRational queued_duration = av_mul_q(sample_duration, (AVRational){queued_samples,1});
        
        av_log(s, AV_LOG_DEBUG, "SDI MUX FRAME:%d AF:%d Pkts:%d bytes:%"PRId64" queued:%d queued_duration:%.2fms\n",
                sdi->num_frames + 1,
                sdi->audio->af,
                sdi->audio->pkt_cnt,
                av_fifo_can_read(sdi->audio->fifo),
                queued_samples,
                queued_duration.num/(double)queued_duration.den);
    }

    // Update audio counters
    if (sdi->audio)
        ff_sdi_audio_frame_end(sdi->audio);

    // Write remaining data and padding, if any.
    if (sdi->bitpos > 0) {
        avio_write(s->pb, (const unsigned char*)sdi->out_word, sdi->bitpos / 8);
    }
    while (pad--)
        avio_w8(s->pb, 0x00);

    return 0;
}

/**
 * 
 */
static int sdi_write_packet_internal(struct AVFormatContext *s)
{
    SdiMuxContext *sdi = s->priv_data;
    AVPacket pkt;
    int ret;
    for (int i = 0; i < s->nb_streams; i++) {
        int64_t next_pts =
            av_rescale_q_rnd(sdi->next_pts, sdi->time_base,
                             s->streams[i]->time_base, AV_ROUND_UP);
        int64_t duration = av_rescale_q_rnd(
            1, sdi->time_base, s->streams[i]->time_base, AV_ROUND_UP);
        if (s->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (!ff_sdi_buffer_contains(sdi->buffer[i], next_pts, duration))
                av_log(s, AV_LOG_DEBUG, "No video for (%"PRId64", %"PRId64")\n", next_pts,
                       duration);
            ret = ff_sdi_buffer_get_video(sdi->buffer[i], &pkt, next_pts, duration);
            if (ret != 0) {
                av_log(s, AV_LOG_DEBUG,
                       "No video for (%"PRId64", %"PRId64"), ret=%d\n", next_pts,
                       duration, ret);
                return ret;
            }
        } else if (s->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (!ff_sdi_buffer_contains(sdi->buffer[i], next_pts, duration))
                av_log(s, AV_LOG_DEBUG, "No audio for (%"PRId64", %"PRId64")\n", next_pts,
                       duration);
        }
    }
    ret = write_sdi_frame(s, &pkt);
    if (!ret)
        sdi->num_frames++;
    av_packet_unref(&pkt);
    return ret;
}

static int64_t to_stream_time(SdiMuxContext *sdi, int64_t sdi_pts,
                              AVStream *stream)
{
    return av_rescale_q_rnd(sdi_pts, sdi->time_base, stream->time_base,
                            AV_ROUND_UP);
}
static int64_t to_sdi_time(SdiMuxContext *sdi, int64_t stream_pts,
                           AVStream *stream)
{
    return av_rescale_q_rnd(stream_pts, stream->time_base, sdi->time_base,
                            AV_ROUND_UP);
}

/**
 * Buffer audio/video, write a single SDI frame if enough data collected.
 */
static int sdi_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    SdiMuxContext *sdi = s->priv_data;
    AVCodecParameters *par = s->streams[pkt->stream_index]->codecpar;
    AVStream *stream = s->streams[pkt->stream_index];
    int ret = 0;
    int64_t pts = 0;
    int has_enough = 1;

    // Buffer audio/video
    if (pkt) {
        SdiBuffer *buffer = sdi->buffer[pkt->stream_index];
        int64_t expected_pts = buffer->last_pts + buffer->last_duration;
        pts = pkt->pts;

        if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            // If the difference between the PTS and the expected PTS is exactly
            // one sample there could be a rounding issue upstream.
            int delta = pkt->pts - expected_pts;
            if (delta == 1 || delta == -1) {
                pkt->pts = expected_pts;
            }
        }

        if (pkt->pts >= expected_pts || buffer->last_duration <= 0) {
            ff_sdi_buffer_add(buffer, pkt);
            if (sdi->next_pts == AV_NOPTS_VALUE && pts != AV_NOPTS_VALUE)
                sdi->next_pts = to_sdi_time(sdi, pts, stream);
        }
        else {
            av_log(s, AV_LOG_WARNING,
                   "Packet dropped for stream %d (pts=%"PRId64", expected_pts=%"PRId64"\n",
                   pkt->stream_index, pkt->pts, expected_pts);
        }
    }
    else {
        if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            av_log(s, AV_LOG_DEBUG, "Null audio packet!");
        } else if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
            av_log(s, AV_LOG_DEBUG, "Null video packet!");
        }
    }

    for (int i = 0; i < s->nb_streams; ++i) {
        int64_t sdi_pts = to_sdi_time(sdi, pts, stream);
        int64_t packet_pts = to_stream_time(sdi, sdi_pts, s->streams[i]);
        int64_t next_pts = to_stream_time(sdi, sdi->next_pts, s->streams[i]);
        int64_t duration = to_stream_time(sdi, 1, s->streams[i]);
        if (sdi->buffer[i]->last_pts < (packet_pts - sdi->buffer[i]->max_buffer)) {
            has_enough = 0;
            break;
        }
        if (!ff_sdi_buffer_contains(sdi->buffer[i], next_pts, duration)) {
            has_enough = 0;
            break;
        }
    }

    // Construct new SDI frame if we have enough audio and video.
    if (has_enough)
    {
        ret = sdi_write_packet_internal(s);
        if (ret < 0)
            return 0;
    }

    return ret;
}

/**
 * Buffer audio/video and write SDI frame if enough data is collected.
 * 
 * If pkt is NULL flush the internal buffer.
 */
static int sdi_write_flush_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    if (!pkt) {
        av_log(s, AV_LOG_ERROR, "Flushing not supported\n");
        return 1;
    }
    return sdi_write_packet(s, pkt);
}

/*
 * Write num_frames and file_size in the file header.
 */
static int sdi_write_trailer(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    SdiMuxContext *sdi = s->priv_data;
    int64_t file_size;

    if (s->pb->seekable & AVIO_SEEKABLE_NORMAL && !sdi->option_no_header) {
        file_size = avio_tell(pb);
        avio_seek(pb, 12, SEEK_SET);
        avio_wb32(pb, sdi->num_frames);
        avio_seek(pb, file_size, SEEK_SET);
    }

    return 0;
}

/* Mux options */
static const AVOption options[] = {
    { "sdi_nr_audio", "Set nr. of audio channels", offsetof(SdiMuxContext, option_audio_nr_ch), AV_OPT_TYPE_INT, {.i64 = -1}, -1, 16, AV_OPT_FLAG_ENCODING_PARAM, NULL },
    { "sdi_payload_id", "Set SDI payload identification byte", offsetof(SdiMuxContext, option_payload_id), AV_OPT_TYPE_INT, {.i64 = -1}, -1, 0xff, AV_OPT_FLAG_ENCODING_PARAM, "format" },
    { "auto", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = -1 }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM, "format" },
    { "sd-sdi", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0x81 }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM, "format" },
    { "hd-sdi", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0x85 }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM, "format" },
    { "3g-sdi", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0x89 }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM, "format" },
    { "6g-sdi", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0xc0 }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM, "format" },
    { "12g-sdi", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0xce }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM, "format" },

    { "sdi_standard", "", offsetof(SdiMuxContext, option_standard), AV_OPT_TYPE_STRING, {.str = ""}, 0, 0, AV_OPT_FLAG_ENCODING_PARAM, NULL},
    { "sdi_interleaving_type", "", offsetof(SdiMuxContext, option_interleave_type), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, AV_OPT_FLAG_ENCODING_PARAM, "interleaving_type" },
    { "2si", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = SDI_INTERLEAVING_TYPE_2SI }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM, "interleaving_type" },
    { "quadrant", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = SDI_INTERLEAVING_TYPE_QUADRANT }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM, "interleaving_type" },
    { "calc_crc", "", offsetof(SdiMuxContext, option_calc_crc), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, AV_OPT_FLAG_ENCODING_PARAM, NULL },
    { "no_header", "", offsetof(SdiMuxContext, option_no_header), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, AV_OPT_FLAG_DECODING_PARAM, NULL },

    { NULL },
};

static const AVClass sdi_enc_class = {
    .class_name = "sdi",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFOutputFormat ff_sdi_muxer = {
    .p.name           = "sdi",
    .p.long_name      = NULL_IF_CONFIG_SMALL("DekTec SDI (digital video)"),
    .p.extensions     = "sdi",
    .p.audio_codec    = AV_CODEC_ID_PCM_S24LE,
    .p.video_codec    = AV_CODEC_ID_WRAPPED_AVFRAME,
    .p.subtitle_codec = AV_CODEC_ID_NONE,
    .flags_internal   = FF_OFMT_FLAG_ALLOW_FLUSH,
    .p.priv_class     = &sdi_enc_class,
    .priv_data_size   = sizeof(SdiMuxContext),
    .write_header     = sdi_write_sdi_header,
    .write_packet     = sdi_write_flush_packet,
    .write_trailer    = sdi_write_trailer,
    .init             = sdi_init,
    .deinit           = sdi_deinit,
};
