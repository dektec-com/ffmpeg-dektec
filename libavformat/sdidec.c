/*
 * SDI demultiplexer
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
 * @file sdidec.c
 * Read SDI stream from .sdi format.
 * @author Werner Damman
 * @author Jeroen Steendam
 */

#include "avformat.h"
#include "avio.h"
#include "demux.h"
#include "internal.h"
#include "libavcodec/get_bits.h"
#include "libavutil/avassert.h"
#include "libavutil/crc.h"
#include "libavutil/frame.h"
#include "libavutil/internal.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/x86/cpu.h"
#include "sdicommon.h"

#include <stdint.h>
#include <string.h>
#ifdef _WIN32
#else
#include <unistd.h>
#endif

#include "config.h"
#if HAVE_INTRINSICS_SSE2
#include <emmintrin.h> // SSE2 intrinsics
#include <immintrin.h> // Other intrinsics
#if defined(__GNUC__)
#pragma GCC target("ssse3")
#endif
#endif

// Diagnostic flags
#define SDI_PARSE_WHOLE_ANC       (0) // Set to 1 to accept gaps between ANC pkts
#define SDI_CHECK_AUDIO_BCH_CS    (0)
#define SDI_PARSE_AES_STATUS_WORD (1)
#define SDI_PARSE_AUDIO_CONTROL   (1)

#define AUDIO_GRP_1 1
#define AUDIO_GRP_2 2
#define AUDIO_GRP_3 3
#define AUDIO_GRP_4 4

#define AUDIO_SAMPLE_SIZE 3

#define VIDEO_STREAM_ID 0
#define AUDIO_STREAM_ID 1

/*
 * Demux context for a single audio channel
 */
typedef struct AudioDemuxContext {
    uint8_t channel_status[24]; ///< AES channel status word
    int channel_status_widx;    ///< AES channel status write index

    int is_present; ///< Indicates if the audio channel is present in the stream

    AVFifo *audio_buffer; ///< Audio sample FIFO
} AudioDemuxContext;

/* SDI private data */
typedef struct SDIDemuxContext {
    const AVClass *av_class;

    char *option_standard; ///< option standard
    int option_no_header;  ///< option to disable file header

    const struct SdiInfo *sdi_info; ///< constants for the standard used
    int header_size;                ///< Size of header in bytes
    int frame_size;                 ///< SDI frame size, without padding
    int frame_padding;              ///< Nr of padding bytes
    int picture_size;               ///< Size of unpacked 422 image
    int64_t frame_duration;         ///< Frame duration in us
    int has_sub_images;             ///< True for 2160 line formats
    int nr_channels;                ///< Nr. of virtual channels (1, 2 or 8)

    int bit_pos;                   ///< Read position
    uint8_t *line_buf_packed;       ///< Line buf, with packed 10 bit symbols
    uint16_t *line_buf;             ///< Line buf, with 16 bit symbols

    int audio_buffer_size;          ///< Size in bytes
    int audio_nr_ch;
    int audio_rate;
    int audio_groups[4];

    int audio_max_channels; ///< Max audio channels for current SDI standard
    AudioDemuxContext *audio_channels; ///< Demux context for audio channels
    int active_channels;               ///< Number of active audio channels

    AVCRC *audio_crc_ctx; ///< CRC context for validation AES channel status
    int64_t audio_pts;    ///< pts of the frame the buffered audio came with
} SDIDemuxContext;

typedef struct AesStatusWord {
    int professional_use;
    int linear_pcm;
    int pre_emphasis;
    int lock_indication;
    int encoded_sampling_frequency;
    int channel_mode;
    int user_bits_management;
    int use_of_aux_sample_bits;
    int audio_sample_word_length;
    int alignment_level;
    int channel_number;
    int multichannel_mode;
    int n;
    int reference_signal;
    int sampling_frequency;
    int frequency_scaling_flag;
    uint8_t crc;
    int is_crc_valid;
} AesStatusWord;

static int parse_aes_status_word(AesStatusWord *status, const uint8_t *data, size_t size, const AVCRC *crc_ctx)
{
    GetBitContext gb;
    int ret = 0;
    uint8_t byte3 = 0;

    if (!status || !data)
        return AVERROR(EINVAL);
    
    if (size < 24)
        return AVERROR_BUFFER_TOO_SMALL;

    ret = init_get_bits(&gb, data, 192);
    if (ret != 0)
        return ret;

    memset(status, 0, sizeof(AesStatusWord));

    status->professional_use = get_bits1(&gb);             // bit 0
    status->linear_pcm = get_bits1(&gb);                   // bit 1
    status->pre_emphasis = get_bits(&gb, 3);               // bit 2..4
    status->lock_indication = get_bits1(&gb);              // bit 5
    status->encoded_sampling_frequency = get_bits(&gb, 2); // bit 6..7
    status->channel_mode = get_bits(&gb, 4);               // bit 8..11
    status->user_bits_management = get_bits(&gb, 4);       // bit 12..15
    status->use_of_aux_sample_bits = get_bits(&gb, 3);     // bit 16..18
    status->audio_sample_word_length = get_bits(&gb, 3);   // bit 19..21
    status->alignment_level = get_bits(&gb, 2);            // bit 22..23
    byte3 = get_bits(&gb, 8);                              // bit 24..31
    status->n = byte3 & 0x1;
    if (status->n) {
        status->channel_number = byte3 >> 1;
    }
    else {
        status->channel_number = byte3 >> 4;
        status->multichannel_mode = (byte3 >> 1) & 0x7;
    }
    status->reference_signal = get_bits(&gb, 2);           // bit 32..33
    skip_bits1(&gb);                                       // bit 34
    status->sampling_frequency = get_bits(&gb, 4);         // bit 35..38
    status->frequency_scaling_flag = get_bits1(&gb);       // bit 39
    skip_bits(&gb, 144);                                   // bit 40..183
    status->crc = get_bits(&gb, 8);                        // bit 184..192
    status->is_crc_valid = av_crc(crc_ctx, 0xFF, data, 23) == status->crc;
    return 0;
}

static void free_frame(void *opaque, uint8_t *data)
{
    AVFrame *frame = (AVFrame *)data;

    av_frame_free(&frame);
}

/* Test if we have a DekTec SDI file */
static int sdi_probe(const AVProbeData *p)
{
    GetBitContext gb;

    if (p->buf_size <= sizeof(struct SdiFileHeader))
        return 0;

    init_get_bits(&gb, p->buf, p->buf_size);
    if (get_bits_long(&gb, 32) == SDI_MAGIC)
    {
        if (get_bits(&gb, 8) != 0)
            return 0;
        return AVPROBE_SCORE_MAX;
    }
    
    return 0;
}

/*
 * Return sdi info for a given header, or NULL if not found
 */
static const struct SdiInfo *find_standard_from_sdi(AVFormatContext *s, struct SdiFileHeader *hdr)
{
    int i = 0;
    const AVRational aspect_ratio = hdr->logical_frame_properties.aspect_ratio;
    const AVRational picture_rate = hdr->logical_frame_properties.picture_rate;
    const int interlaced_picture = hdr->logical_frame_properties.is_interlaced;
    const int interlaced_transport = hdr->physical_frame_properties.num_fields == 2 ? 1 : 0;
    const int sampling_structure = hdr->logical_frame_properties.sampling_structure;
    const int is_stereoscopic = hdr->logical_frame_properties.is_stereoscopic;
    const int bit_depth = hdr->logical_frame_properties.bit_depth;
    const int picture_width = hdr->logical_frame_properties.picture_width;
    const int picture_height = hdr->logical_frame_properties.picture_height;

    SdiScanningMethod scanning_method;
    if (interlaced_picture && interlaced_transport)
        scanning_method = SDI_I_PICT_I_TR;
    else if (!interlaced_picture && interlaced_transport)
        scanning_method = SDI_P_PICT_I_TR;
    else if (interlaced_picture && !interlaced_transport)
        scanning_method = SDI_I_PICT_P_TR;
    else if (!interlaced_picture && !interlaced_transport)
        scanning_method = SDI_P_PICT_P_TR;
    else
        return NULL;

    av_log(s, AV_LOG_DEBUG, "find_sdi_standard:\n");
    av_log(s, AV_LOG_DEBUG, "  aspect_ratio: %d/%d\n", aspect_ratio.num, aspect_ratio.den);
    av_log(s, AV_LOG_DEBUG, "  picture_rate: %d/%d\n", picture_rate.num, picture_rate.den);
    if (scanning_method == SDI_I_PICT_I_TR)
        av_log(s, AV_LOG_DEBUG, "  scanning_mode: Interlaced\n");
    else if (scanning_method == SDI_P_PICT_P_TR)
        av_log(s, AV_LOG_DEBUG, "  scanning_mode: Progressive\n");
    else if (scanning_method == SDI_P_PICT_I_TR)
        av_log(s, AV_LOG_DEBUG, "  scanning_mode: PsF\n");
    av_log(s, AV_LOG_DEBUG, "  sampling_structure: %d\n", sampling_structure);
    av_log(s, AV_LOG_DEBUG, "  is_stereoscopic: %d\n", is_stereoscopic);
    av_log(s, AV_LOG_DEBUG, "  bit_depth: %d\n", bit_depth);
    av_log(s, AV_LOG_DEBUG, "  picture_width: %d\n", picture_width);
    av_log(s, AV_LOG_DEBUG, "  picture_height: %d\n", picture_height);

    for (const struct SdiInfo *info = NULL; info = av_sdi_info(i); i++) {
        if (av_cmp_q(av_sdi_aspect_ratio(info->aspect_ratio), aspect_ratio) != 0)
             continue;
        if (av_cmp_q(av_sdi_rate(info->picture_rate), picture_rate) != 0)
            continue;
        if (scanning_method != info->scanning_method)
            continue;
        if (sampling_structure != SDI_SAMPLING_YCbCr422)
            continue;
        if (is_stereoscopic != 0)
            continue;
        if (bit_depth != 10)
            continue;
        if (info->picture_width < picture_width)
            continue;
        if (info->picture_height < picture_height)
            continue;

        return info;
    }
    return NULL;
}

static void log_setup(AVFormatContext *s, AVRational sar)
{
    SDIDemuxContext *sdi = s->priv_data;
    const struct SdiInfo *sdi_info = sdi->sdi_info;

    av_log(s, AV_LOG_DEBUG, "Header size   = %d\n", sdi->header_size);
    av_log(s, AV_LOG_DEBUG, "Frame size    = %d, padding %d\n", sdi->frame_size, sdi->frame_padding);
    av_log(s, AV_LOG_DEBUG, "PayloadFormat = 0x%02x\n", sdi_info->payload_format);
    av_log(s, AV_LOG_DEBUG, "AspectRatio   = %s\n", sdi_info->aspect_ratio == SDI_AR_16_9 ? "16:9" : "4:3");
    av_log(s, AV_LOG_DEBUG, "Sample ar     = %d/%d\n", sar.num, sar.den);
    av_log(s, AV_LOG_DEBUG, "Picture dim   = %dx%d (%d bytes)\n", sdi_info->picture_width, sdi_info->picture_height, sdi->picture_size);
    av_log(s, AV_LOG_DEBUG, "Has subimages = %d\n", sdi->has_sub_images);
    av_log(s, AV_LOG_DEBUG, "Interlacing   = pic:%d transport:%d\n", is_interlaced_picture(sdi_info->scanning_method), is_interlaced_transport(sdi_info->scanning_method));
    av_log(s, AV_LOG_DEBUG, "Audio nr.ch   = %d\n", sdi->audio_nr_ch);
    av_log(s, AV_LOG_DEBUG, "Audio rate    = %d\n", sdi->audio_rate);
    //av_log(s, AV_LOG_DEBUG, "Audio buffer  = %d bytes (%d samples/ch)\n", sdi->audio_buffer_size, sdi->audio_buffer_size / (4 * sdi->audio_nr_ch));
}

/**
 * Initialize the SDI and AVFormatContext structure.
 * sdi_info, frame_size and frame_padding must be set!
 *
 * Return 0 if OK.
 */
static int sdi_setup(AVFormatContext *s)
{
    SDIDemuxContext *sdi = s->priv_data;
    const struct SdiInfo *sdi_info = sdi->sdi_info;
    AVStream *vst = NULL;
    AVRational rrate;
    AVRational sar;
    int64_t frame_count = 0;
    int height = -1;
    int width = -1;
    int ret = 0;
    int nb_line_syms = 0;

    height = sdi_info->picture_height;
    width = sdi_info->picture_width;

    sdi->picture_size = height * width * 2 * 2;

    frame_count = (avio_size(s->pb) - sdi->header_size) / (sdi->frame_size + sdi->frame_padding);

    rrate = av_sdi_rate(sdi_info->picture_rate);
    sdi->frame_duration = (1000000 * rrate.den + rrate.num - 1)/ rrate.num;  // in us, rounded up
    av_reduce(&sar.num, &sar.den,
            av_sdi_aspect_ratio(sdi_info->aspect_ratio).num * height,
            av_sdi_aspect_ratio(sdi_info->aspect_ratio).den * width,
            1024*1024);
    sdi->has_sub_images = ff_has_sub_images(sdi_info->payload_format);
    sdi->nr_channels = ff_sdi_get_nr_channels(sdi_info->payload_format);

    // Allocate temporary buffer for an SDI active line segment
    nb_line_syms = sdi_info->nr_hanc_symbols + sdi_info->nr_vanc_symbols;
    if (sdi_info->payload_format == 0x84) {
        // Double the line buffer size because for some SDI standards the lines
        // are not byte aligned.
        nb_line_syms *= 2;
    }
    sdi->line_buf = av_malloc(nb_line_syms * sizeof(uint16_t));
    sdi->line_buf_packed = av_malloc((nb_line_syms * 10 + 7) / 8);

    log_setup(s, sar);

    /*
     * Create new video stream.
     */
    vst = avformat_new_stream(s, NULL);
    if (!vst) {
        av_log(s, AV_LOG_ERROR, "could not allocate stream\n");
        return AVERROR(ENOMEM);
    }
    // Setup stream parameters. Use planar YUV format, as this seems to be
    // the only 10-bit YUV422 format.
    vst->id = VIDEO_STREAM_ID;
    avpriv_set_pts_info(vst, 64, 1, 1000000);
    vst->avg_frame_rate = rrate;
    vst->r_frame_rate = rrate;
    vst->nb_frames = frame_count;
    vst->duration = frame_count * sdi->frame_duration;
    vst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    vst->codecpar->codec_id = AV_CODEC_ID_WRAPPED_AVFRAME;
    vst->codecpar->format = AV_PIX_FMT_YUV422P10;
    vst->codecpar->width  = sdi_info->picture_width;
    vst->codecpar->height = sdi_info->picture_height;

    // TODO: is it always TT? NTSC could be (is?) display bottom first?
    if (is_interlaced_picture(sdi_info->scanning_method)) {
        vst->codecpar->field_order = AV_FIELD_TT;
    } else {
        vst->codecpar->field_order = AV_FIELD_PROGRESSIVE;
    }

    av_log(s, AV_LOG_DEBUG, "Duration=%zd nrframes=%zd rate=%d/%d Hz\n",
            vst->duration, vst->nb_frames, rrate.num, rrate.den);

    // Initialize buffer for max supported audio channels
    sdi->audio_rate = 48000;
    sdi->audio_max_channels = ff_sdi_max_audio_channels(sdi_info->payload_format, sdi->audio_rate);
    sdi->audio_buffer_size = (int64_t)AUDIO_SAMPLE_SIZE * sdi->audio_max_channels * (sdi->audio_rate * rrate.den + rrate.num - 1) / rrate.num;
    sdi->audio_channels = av_mallocz(sdi->audio_max_channels * sizeof(AudioDemuxContext));
    for (int i = 0; i < sdi->audio_max_channels; i++) {
        sdi->audio_channels[i].audio_buffer = av_fifo_alloc2(sdi->audio_buffer_size, 1, 0);
    }
    sdi->active_channels = 0;
    sdi->audio_crc_ctx = av_malloc(1024 * sizeof(AVCRC));
    ret = av_crc_init(sdi->audio_crc_ctx, 1, 8, 0xB8, 1024 * sizeof(AVCRC));
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "Can't initialize CRC table\n");
        return -1;
    }

    // Set packet size to read full frames.
    // TODO: multiple of something? Lines? Frames?
    s->packet_size = sdi->frame_size + sdi->frame_padding;

    return 0;
}
static void get_sdi_format(struct GetBitContext *gb, struct Format *format)
{
    int sdi_level, num_streams;
    format->line_rate = get_bits(gb, 5);
    format->interleaving_type = get_bits(gb, 2);
    sdi_level = get_bits(gb, 2);
    num_streams = get_bits(gb, 2);
    if (sdi_level == 0)
        format->sdi_level = SDI_LEVEL_NOT_APPLICABLE;
    else if (sdi_level == 1)
        format->sdi_level = SDI_LEVEL_A;
    else if (sdi_level == 2) {
        if (num_streams == 1)
            format->sdi_level = SDI_LEVEL_B_DL;
        else if (num_streams == 2)
            format->sdi_level = SDI_LEVEL_B_DS;
        else // Assume B-DL
            format->sdi_level = SDI_LEVEL_B_DL;
    }
    else // Assume Level A
        format->sdi_level = SDI_LEVEL_A;
    get_bits(gb, 5); // reserved
}

static void get_logical_frame_properties(struct GetBitContext *gb, struct LogicalFrameProperties *properties)
{
    properties->picture_rate.num = get_bits_long(gb, 32);
    properties->picture_rate.den = get_bits_long(gb, 32);
    properties->aspect_ratio.num = get_bits(gb, 8);
    properties->aspect_ratio.den = get_bits(gb, 8);
    properties->is_interlaced = get_bits(gb, 1);
    properties->sampling_structure = get_bits(gb, 4);
    properties->is_stereoscopic = get_bits(gb, 1);
    get_bits(gb, 2); // reserved
    properties->bit_depth = get_bits(gb, 8);
    properties->picture_width = get_bits(gb, 16);
    properties->picture_height = get_bits(gb, 16);
}

static void get_physical_field_properties(struct GetBitContext *gb, struct PhysicalFieldProperties *properties)
{
    properties->num_lines_field = get_bits(gb, 16);
    properties->first_video_line = get_bits(gb, 16);
    properties->num_lines_video = get_bits(gb, 16);
}

static void get_physical_frame_properties(struct GetBitContext *gb, struct PhysicalFrameProperties *properties)
{
    properties->num_fields = get_bits(gb, 2);
    properties->crc_omitted = get_bits(gb, 1);
    get_bits(gb, 5); // reserved
    properties->num_lines_frame = get_bits(gb, 16);
    properties->num_syms_hanc = get_bits(gb, 16);
    properties->num_syms_vanc_video = get_bits(gb, 16);
    for (int i = 0; i < properties->num_fields; i++)
        get_physical_field_properties(gb, &properties->field_properties[i]);
}

static void get_sdi_file_header(struct GetBitContext *gb, struct SdiFileHeader *header)
{
    header->magic_code = get_bits_long(gb, 32);
    header->version = get_bits(gb, 8);
    header->header_size = get_bits(gb, 16);
    header->num_physical_links = get_bits(gb, 8);
    header->frame_size = get_bits_long(gb, 32);
    header->num_frames = get_bits_long(gb, 32);
    header->compression_mode = get_bits_long(gb, 8);
    get_sdi_format(gb, &header->format);
    get_logical_frame_properties(gb, &header->logical_frame_properties);
    get_physical_frame_properties(gb, &header->physical_frame_properties);
}

static int sdi_read_header(AVFormatContext *s)
{
    struct SdiFileHeader hdr;
    GetBitContext gb;
    
    SDIDemuxContext *sdi = s->priv_data;
    int n = 0;

    uint8_t buffer[1024];

    // The audio stream is added when the first frame with audio arrives
    s->ctx_flags |= AVFMTCTX_NOHEADER;

    if (sdi->option_no_header) {
        StandardOption options = {0};
        int ret;
        int aligned_frame_size;

        if (strlen(sdi->option_standard) == 0) {
            av_log(s, AV_LOG_WARNING, "Please provide SDI standard when there is no header\n");
            return AVERROR(EINVAL);
        }

        ret = av_parse_standard_option(s, sdi->option_standard, &options);
        if (ret < 0)
            return AVERROR(EINVAL);

        sdi->sdi_info = av_sdi_info(av_sdi_get_fmt(&options));
        sdi->frame_size = sdi->sdi_info->nr_sdi_lines *
                (sdi->sdi_info->nr_hanc_symbols + sdi->sdi_info->nr_vanc_symbols) * 10 / 8;
        aligned_frame_size = (sdi->frame_size + 7) & ~7;
        sdi->frame_padding = aligned_frame_size - sdi->frame_size;
        sdi->header_size = 0;
    }
    else {
        avio_read(s->pb, buffer, 1024);

        init_get_bits(&gb, buffer, 1024);
        get_sdi_file_header(&gb, &hdr);

        // Retrieve image properties from header
        sdi->sdi_info = find_standard_from_sdi(s, &hdr);
        if (sdi->sdi_info == NULL) {
            av_log(s, AV_LOG_WARNING, "SDI format is not supported\n");
            return AVERROR(EINVAL);
        }
        sdi->frame_size = sdi->sdi_info->nr_sdi_lines *
                (sdi->sdi_info->nr_hanc_symbols + sdi->sdi_info->nr_vanc_symbols) * 10 / 8;
        sdi->frame_padding = hdr.frame_size - sdi->frame_size;
        sdi->header_size = hdr.header_size;

        // Advance read pointer to start of payload
        n = avio_seek(s->pb, hdr.header_size, SEEK_SET);
        if (n < 0)
            return AVERROR(EIO);
    }

    return sdi_setup(s);
}

/*
 * Convert interleaved UYVY 4:2:2 to planar.
 */
static void to_planar(const uint16_t *in, uint16_t *py, uint16_t *pu,
        uint16_t *pv, int width, int has_sub_images)
{
    if (has_sub_images) {
        // 4k SDI lines contain samples for 2 picture lines.
        uint16_t *py2 = py + width;
        uint16_t *pu2 = pu + (width >> 1);
        uint16_t *pv2 = pv + (width >> 1);
        while (width > 0)
        {
            width -= 4; // we do 4 pixels on 2 lines in each iteration
            pu2[1] = *in++;
            pu[1] = *in++;
            pu2[0] = *in++;
            pu[0] = *in++;
            py2[1] = *in++;
            py[1] = *in++;
            py2[0] = *in++;
            py[0] = *in++;
            pv2[1] = *in++;
            pv[1] = *in++;
            pv2[0] = *in++;
            pv[0] = *in++;
            py2[3] = *in++;
            py[3] = *in++;
            py2[2] = *in++;
            py[2] = *in++;
            pu += 2; pu2 += 2;
            pv += 2; pv2 += 2;
            py += 4; py2 += 4;
        }
    } else {
#if HAVE_INTRINSICS_SSE2
        int cpu_flags = av_get_cpu_flags();
        if (X86_SSSE3(cpu_flags)) {
            while (width >= 8) {
                #define Z (uint8_t)0x80

                __m128i symbols1, symbols2, y_symbols, uv_symbols;

                symbols1 = _mm_loadu_si128((__m128i*)in); // 8 symbols
                in += 8;
                symbols2 = _mm_loadu_si128((__m128i*)in); // 8 symbols
                in += 8;

                y_symbols = _mm_or_si128(_mm_shuffle_epi8(symbols1, _mm_set_epi8(Z, Z, Z, Z, Z, Z, Z, Z, 15, 14, 11, 10, 7, 6, 3, 2)),
                                            _mm_shuffle_epi8(symbols2, _mm_set_epi8(15, 14, 11, 10, 7, 6, 3, 2, Z, Z, Z, Z, Z, Z, Z, Z)));
                uv_symbols = _mm_or_si128(_mm_shuffle_epi8(symbols1, _mm_set_epi8(Z, Z, Z, Z, 13, 12, 5, 4, Z, Z, Z, Z, 9, 8, 1, 0)),
                                                _mm_shuffle_epi8(symbols2, _mm_set_epi8(13, 12, 5, 4, Z, Z, Z, Z, 9, 8, 1, 0, Z, Z, Z, Z)));
                _mm_storeu_si128((__m128i*)py, y_symbols);
                _mm_storel_epi64((__m128i*)pu, uv_symbols);
                _mm_storel_epi64((__m128i*)pv, _mm_srli_si128(uv_symbols, 8));

                width -= 8;
                py += 8;
                pu += 4;
                pv += 4;

                #undef Z
            }
        }
#endif
        while (width > 0)
        {
            width -= 2; // do 2 pixels
            *pu++ = *in++;
            *py++ = *in++;
            *pv++ = *in++;
            *py++ = *in++;
        }
    }
}

/*
 * Read nr_syms symbols from stream, return error code or 0 if ok.
 */
static int read_to16(AVIOContext *pb, SDIDemuxContext *sdi, int nr_syms)
{
    int ret;
    int nr_bytes;
    uint8_t *pin = sdi->line_buf_packed;
    uint16_t *pout = sdi->line_buf;
    const int mask = (1 << 10) - 1;

    // Read packed symbols
    nr_bytes = (nr_syms * 10 + sdi->bit_pos + 7) >> 3;
    ret = avio_read(pb, pin, nr_bytes);
    if (ret < 0)
        return ret;
    if ((nr_syms * 10 + sdi->bit_pos) & 7) {
        int64_t ret64 = avio_seek(pb, -1, SEEK_CUR);
        if (ret64 < 0)
            return ret64;
    }

    // Convert 10-bit packed to 16-bit
#if HAVE_INTRINSICS_SSE2
    if (X86_SSSE3(av_get_cpu_flags())) {
        __m128i mask10_to16 = _mm_set_epi16(~0x3F, 0x3FF0, 0xFFC, 0x3FF, ~0x3F, 0x3FF0, 0xFFC, 0x3FF);
        __m128i mult10_to16 = _mm_set_epi16(1, 4, 16, 64, 1, 4, 16, 64);
        __m128i shuf10_to16 = _mm_set_epi8(9, 8, 8, 7, 7, 6, 6, 5, 4, 3, 3, 2, 2, 1, 1, 0);
        while (nr_syms >= 8) {
            __m128i symbols = _mm_loadu_si128((__m128i*)pin);
            pin += 10;
            symbols = _mm_shuffle_epi8(symbols, shuf10_to16);
            symbols = _mm_and_si128(symbols, mask10_to16);
            symbols = _mm_mullo_epi16(symbols, mult10_to16);
            symbols = _mm_srli_epi16(symbols, 6);
            _mm_storeu_si128((__m128i*)pout, symbols);
            pout += 8;

            nr_syms -= 8;
        }
    }
#endif
    while (nr_syms--) {
        *pout++ = (*(uint32_t*)pin >> sdi->bit_pos) & mask;
        sdi->bit_pos += 10;
        pin += sdi->bit_pos >> 3;
        sdi->bit_pos &= 7;
    }
    return 0;
}

static void parse_audio_data_272m(SDIDemuxContext *sdi, int group, uint16_t *udw, int count, int step)
{
    while (count > 0) {
        uint16_t w0 = *udw++;
        uint16_t w1 = *udw++;
        uint16_t w2 = *udw++;
        int ch_idx = (4 * (group - 1)) + (w0 >> 1) & 3;
        AudioDemuxContext *audio = sdi->audio_channels + ch_idx;
        // int z = w0 & 1;
        // int p = w2 & 0x100 ? 1 : 0;
        // int c = w2 & 0x80 ? 1 : 0;
        // int u = w2 & 0x40 ? 1 : 0;
        int v = w2 & 0x20 ? 1 : 0;
        int sample =
            ((w0 >> 3) & 0x3f) | ((w1 & 0x1ff) << 6) | ((w2 & 0x1f) << 15);
        sample <<= 4;
        av_fifo_write(audio->audio_buffer, &sample, AUDIO_SAMPLE_SIZE);
        audio->is_present = v == 0;
        count -= 3;
    }
    av_assert1(count == 0);
}

/*
 * Each audio group carries four audio channels.
 *
 * Audio packet contents:
 * W0..1   CLK
 * W2..5   Ch1
 * W6..9   Ch2
 * W10..13 Ch3
 * W14..17 Ch4
 * W18..23 ECC
 */
static void parse_audio_data(SDIDemuxContext *sdi, int group, uint16_t *udw, int step)
{
    int32_t ch[4];
    int i;
    int ch_idx = (4 * group) - 4;
    int nr_ch = 4;
    AudioDemuxContext *audio = sdi->audio_channels + ch_idx;

//    int clk = (udw[0 * step] & 0xff) | ((udw[1 * step] & 0xf) << 8) |
//            ((udw[1 * step] & 0x20) << 7);
//    int mpf = udw[1 * step] & 0x10 ? 1 : 0;
    ch[0] = ((udw[2 * step] & 0xf0) >> 4) |
            ((udw[3 * step] & 0xff) << 4) |
            ((udw[4 * step] & 0xff) << 12) |
            ((udw[5 * step] & 0x0f) << 20);
    ch[1] = ((udw[6 * step] & 0xf0) >> 4) |
            ((udw[7 * step] & 0xff) << 4) |
            ((udw[8 * step] & 0xff) << 12) |
            ((udw[9 * step] & 0x0f) << 20);
    ch[2] = ((udw[10 * step] & 0xf0) >> 4) |
            ((udw[11 * step] & 0xff) << 4) |
            ((udw[12 * step] & 0xff) << 12) |
            ((udw[13 * step] & 0x0f) << 20);
    ch[3] = ((udw[14 * step] & 0xf0) >> 4) |
            ((udw[15 * step] & 0xff) << 4) |
            ((udw[16 * step] & 0xff) << 12) |
            ((udw[17 * step] & 0x0f) << 20);

    if (SDI_PARSE_AES_STATUS_WORD) {
        // Audio channel status
        for (i = 0; i < 2; i++) {
            AesStatusWord status;
            int ret = 0;
            int z = udw[(2 + i * 8) * step] & 0x08;
            // int p_bit0 = (udw[(5 + i * 8) * step] & 0x80) ? 1 : 0; // parity
            // int p_bit1 = (udw[(9 + i * 8) * step] & 0x80) ? 1 : 0; // parity
            int c_bit0 = (udw[(5 + i * 8) * step] & 0x40) ? 1 : 0; // channel status
            int c_bit1 = (udw[(9 + i * 8) * step] & 0x40) ? 1 : 0; // channel status
            // int u_bit0 = (udw[(5 + i * 8) * step] & 0x20) ? 1 : 0; // user data
            // int u_bit1 = (udw[(9 + i * 8) * step] & 0x20) ? 1 : 0; // user data
            int v_bit0 = (udw[(5 + i * 8) * step] & 0x10) ? 1 : 0; // validity
            int v_bit1 = (udw[(9 + i * 8) * step] & 0x10) ? 1 : 0; // validity
            uint8_t *status_word0, *status_word1;

            if (z || audio[i * 2 + 0].channel_status_widx >= 192) {
                ret = parse_aes_status_word(&status, audio[i * 2 + 0].channel_status, 24, sdi->audio_crc_ctx);
                if (ret == 0 && audio[i * 2 + 0].channel_status_widx >= 192) {
                   audio[i * 2 + 0].is_present = status.linear_pcm == 0 && !v_bit0;
                }
                else if (!z)
                   av_log(sdi, AV_LOG_DEBUG, "Can't parse aes status word\n");

                audio[i * 2 + 0].channel_status_widx = 0;
                memset(audio[i * 2 + 0].channel_status, 0, 24);
            }
            if (z || audio[i * 2 + 1].channel_status_widx >= 192) {
                ret = parse_aes_status_word(&status, audio[i * 2 + 1].channel_status, 24, sdi->audio_crc_ctx);
                if (ret == 0 && audio[i * 2 + 1].channel_status_widx >= 192)
                   audio[i * 2 + 1].is_present = status.linear_pcm == 0 && !v_bit1;
                else if (!z)
                   av_log(sdi, AV_LOG_DEBUG, "Can't parse aes status word\n");

                audio[i * 2 + 1].channel_status_widx = 0;
                memset(audio[i * 2 + 1].channel_status, 0, 24);
            }

            status_word0 = audio[i * 2 + 0].channel_status;
            status_word1 = audio[i * 2 + 1].channel_status;
            status_word0 += audio[i * 2 + 0].channel_status_widx >> 3;
            status_word1 += audio[i * 2 + 1].channel_status_widx >> 3;
            *status_word0 |= c_bit0 << (audio[i * 2 + 0].channel_status_widx & 7);
            *status_word1 |= c_bit1 << (audio[i * 2 + 1].channel_status_widx & 7);
            audio[i * 2 + 0].channel_status_widx++;
            audio[i * 2 + 1].channel_status_widx++;
        }
    }

    // TODO: 16/20 bit!

    // sign-extend...
    ch[0] = ch[0] << 8 >> 8;
    ch[1] = ch[1] << 8 >> 8;
    ch[2] = ch[2] << 8 >> 8;
    ch[3] = ch[3] << 8 >> 8;

    for (int i = 0; i < nr_ch; i++)
        av_fifo_write(audio[i].audio_buffer, &ch[i], AUDIO_SAMPLE_SIZE);

    // Check BCH and CS
    if (SDI_CHECK_AUDIO_BCH_CS) {
        uint16_t buf[31];
        int i = 0;
        uint64_t mybch;
        uint16_t cs;

        for (i = 0; i < 31; i++)
            buf[i] = udw[(i - 6) * step];
        mybch = ff_calculate_adp_bch(buf, 24);
        cs = ff_calculate_adp_cs(buf + 3, 27);

        for (i = 0; i < 6; i++)
            if (((mybch >> (i * 8)) & 0xff) != (udw[(18 + i) * step] & 0xff))
                printf("BCH word %d differs 0x%02x != 0x%02x\n", i,
                        (int)(mybch >> (i * 8)) & 0xff, udw[(18 + i) * step] & 0xff);
        if (cs != udw[24 * step])
            printf("CS 0x%03x differs from 0x%03x\n", cs, udw[24 * step]);
    }
}

/*
 * Parse audio control packet:
 *
 * 0 AF
 * 1 RATE
 * 2 ACT
 * 3..5 DEL1-2
 * 6..8 DEL3-4
 * 9..10 RSVR
 */
#if SDI_PARSE_AUDIO_CONTROL
static void parse_audio_control(SDIDemuxContext *sdi, int group, uint16_t *udw, int step)
{
    int af = udw[0 * step] & 0xff;
    int is_async = udw[1 * step] & 0x01;
    int rate = (udw[1 * step] >> 1) & 0x07;
    int active_ch = udw[2 * step] & 0x0f;
    int nr_ch = av_popcount64(active_ch);
    av_log(NULL, AV_LOG_DEBUG, "Audio control group%d: af=%d, is_async=%d, rate=%d, active_ch=%d\n",
            group, af, is_async, rate, nr_ch);
    sdi->audio_groups[group - 1] = nr_ch;
}
#endif

/*
 * Search for next ADF.
 *
 * If found, set DID and DC and return pointer to user word 0, else return NULL
 */
static uint16_t *find_packet_header(uint16_t **line, int *size, int *did, int *dc, int step)
{
    int i;
    uint16_t *ret = NULL;
    for (i = 0; i < *size - 7; i++) {
        if ((*line)[i*step] == 0x000 && (*line)[(i+1)*step] == 0x3ff && (*line)[(i+2)*step] == 0x3ff) {
            *dc = (*line)[(i+5) * step] & 0xff;
            *did = (*line)[(i+3) * step];
            ret = (*line) + (i+6) * step;
            *size -= *dc + 7;               // subtract pkt size
            (*line) += (*dc + 7) * step;    // point to word after pkt
            break;
        }
// #if !SDI_PARSE_WHOLE_ANC
//         break;
// #endif
    }
    return ret;
}

/*
 * Parse ancillary section of 'size' size.
 * If packets found, extract info.
 */
static void parse_anc(SDIDemuxContext *sdi, uint16_t *p, int step, int size)
{
    uint16_t *pkt;
    do {
        int did;
        int dc;
        pkt = find_packet_header(&p, &size, &did, &dc, step);
        if (pkt) {
            switch(did) {
            case SDI_DID_AUDIO_DATA_GRP1:
                parse_audio_data(sdi, AUDIO_GRP_1, pkt, step);
                break;
            case SDI_DID_AUDIO_DATA_GRP2:
                parse_audio_data(sdi, AUDIO_GRP_2, pkt, step);
                break;
            case SDI_DID_AUDIO_DATA_GRP3:
                parse_audio_data(sdi, AUDIO_GRP_3, pkt, step);
                break;
            case SDI_DID_AUDIO_DATA_GRP4:
                parse_audio_data(sdi, AUDIO_GRP_4, pkt, step);
                break;
#if SDI_PARSE_AUDIO_CONTROL
            case SDI_DID_AUDIO_CONTROL_GRP1:
                parse_audio_control(sdi, 1, pkt, step);
                break;
            case SDI_DID_AUDIO_CONTROL_GRP2:
                parse_audio_control(sdi, 2, pkt, step);
                break;
            case SDI_DID_AUDIO_CONTROL_GRP3:
                parse_audio_control(sdi, 3, pkt, step);
                break;
            case SDI_DID_AUDIO_CONTROL_GRP4:
                parse_audio_control(sdi, 4, pkt, step);
                break;
#endif
            case SDI_DID_AUDIO_DATA_GRP1_ST0272M:
                parse_audio_data_272m(sdi, 1, pkt, dc, step);
                break;
            case SDI_DID_AUDIO_DATA_GRP2_ST0272M:
                parse_audio_data_272m(sdi, 2, pkt, dc, step);
                break;
            case SDI_DID_AUDIO_DATA_GRP3_ST0272M:
                parse_audio_data_272m(sdi, 3, pkt, dc, step);
                break;
            case SDI_DID_AUDIO_DATA_GRP4_ST0272M:
                parse_audio_data_272m(sdi, 4, pkt, dc, step);
                break;
            case SDI_DID_PAYLOAD_ID: break;
            default:
                break;
            }
        }
    } while(pkt);
}

/*
 * Read one packet and put it in 'pkt'. pts and flags are also
 * set. 'avformat_new_stream' can be called only if the flag
 * AVFMTCTX_NOHEADER is used and only in the calling thread (not in a
 * background thread).
 * @return 0 on success, < 0 on error.
 *         When returning an error, pkt must not have been allocated
 *         or must be freed before returning
 *
 * Read a single SDI frame and extract video.
 *
 */
static int sdi_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int line = 0;
    uint16_t *py, *pu, *pv;
    int write_stride[3] = {0};
    int ret = 0;
    int ch = 0;
    SDIDemuxContext *sdi = s->priv_data;
    const struct SdiInfo *info = sdi->sdi_info;
    int nr_ch = sdi->nr_channels;
    AVFrame *frame = NULL;
    int detected_channels = 0;
    int signaled_channels = 0;
    int active_channels = 0;

    if (sdi->active_channels && av_fifo_can_read(sdi->audio_channels[0].audio_buffer)) {
        int fifo_size = (int)av_fifo_can_read(sdi->audio_channels[0].audio_buffer);
        int n_samples = fifo_size / AUDIO_SAMPLE_SIZE;
        int packet_size = fifo_size * sdi->active_channels;
        uint8_t *ptr;

        for (int ch = 0; ch < sdi->audio_max_channels; ch++) {
            int size = (int)av_fifo_can_read(sdi->audio_channels[ch].audio_buffer);
            av_log(s, AV_LOG_DEBUG, "Channel %d FIFO size=%dB, N_SAMPLES=%"PRId32"\n", ch, size, size / AUDIO_SAMPLE_SIZE);
        }

        ret = av_new_packet(pkt, packet_size);
        pkt->pos = avio_tell(s->pb);
        pkt->size = packet_size;
        pkt->stream_index = AUDIO_STREAM_ID;
        pkt->pts = pkt->dts = sdi->audio_pts;
        pkt->duration = sdi->frame_duration;

        ptr = pkt->buf->data;
        for (int i = 0; i < n_samples; i++) {
            for (int ch = 0; ch < sdi->active_channels; ch++) {
                av_fifo_read(sdi->audio_channels[ch].audio_buffer, ptr, AUDIO_SAMPLE_SIZE);
                ptr += AUDIO_SAMPLE_SIZE;
            }
        }
        for (int ch = 0; ch < sdi->audio_max_channels; ch++) {
            av_fifo_reset2(sdi->audio_channels[ch].audio_buffer);
        }
        return 0;
    }

    frame = av_frame_alloc();
    if (!frame)
        return AVERROR(ENOMEM);

    frame->format = AV_PIX_FMT_YUV422P10LE;
    frame->width = info->picture_width;
    frame->height = info->picture_height;
    ret = av_frame_get_buffer(frame, 0);
    if (ret != 0)
        return ret;

    pkt->buf =
        av_buffer_create((uint8_t *)frame, sizeof(*frame), free_frame, NULL, 0);
    if (!pkt->buf)
        return AVERROR(ENOMEM);

    pkt->data = (uint8_t*)frame;
    pkt->size = sizeof(*frame);
    pkt->flags |= AV_PKT_FLAG_KEY;
    pkt->flags |= AV_PKT_FLAG_TRUSTED;
    pkt->pos = avio_tell(s->pb);

    // The 'write_stride' is the offset in symbols to the next position to write
    // video data to. When using subimages, we write two video lines per SDI
    // line. With interlaced transport, we write one video line per SDI line and
    // skip the next line (which belongs to the other field).
    write_stride[0] = frame->linesize[0] / 2;
    write_stride[1] = frame->linesize[1] / 2;
    write_stride[2] = frame->linesize[2] / 2;
    if (sdi->has_sub_images) {
        write_stride[0] *= 2;
        write_stride[1] *= 2;
        write_stride[2] *= 2;
    }
    if (is_interlaced_transport(info->scanning_method)) {
        write_stride[0] *= 2;
        write_stride[1] *= 2;
        write_stride[2] *= 2;
    }

    sdi->bit_pos = 0;

    py = (uint16_t*)frame->data[0];
    pu = (uint16_t*)frame->data[1];
    pv = (uint16_t*)frame->data[2];


    // Read in frame data, line by line
    for (line = 1; line <= info->nr_sdi_lines; line++) {
        // Determine line type
        int is_vb = ff_is_vbi_line(sdi->sdi_info, line);
        uint16_t *line_buf = sdi->line_buf;

        // Read data and convert to 16-bit symbols.
        int nb_line_syms = info->nr_hanc_symbols + info->nr_vanc_symbols;
        if (info->payload_format == 0x84) {
            // Read two lines at once because a line is not always byte aligned.
            if (line & 1) {
                ret = read_to16(s->pb, sdi, nb_line_syms * 2);
                if (ret < 0)
                    break;
            } else {
                line_buf = sdi->line_buf + nb_line_syms;
            }
        } else {
            ret = read_to16(s->pb, sdi, nb_line_syms);
            if (ret < 0)
                break;
        }

        // Parse packets in HANC for each (virtual) channel.
        for (ch = 0; ch < nr_ch; ch++) {
            int eav_ln_crc_size = nr_ch == 1 ? 4 : 8 * nr_ch;
            int hanc_size = (info->nr_hanc_symbols - eav_ln_crc_size) / nr_ch;
            uint16_t *hanc_start = line_buf + eav_ln_crc_size + ch;
            parse_anc(sdi, hanc_start, nr_ch, hanc_size);
        }

        if (is_vb) {
            // Parse packets in VANC for each virtual channel
            int vanc_size = info->nr_vanc_symbols / nr_ch;
            for (ch = 0; ch < nr_ch; ch++) {
                parse_anc(sdi, line_buf + info->nr_hanc_symbols + ch, nr_ch, vanc_size);
            }
        }
        else {
            // If first video line of 2nd field, reset pointers to start of line #1
            if (line == info->vid_start_line_field2) {
                py = (uint16_t*)(frame->data[0] + frame->linesize[0]);
                pu = (uint16_t*)(frame->data[1] + frame->linesize[1]);
                pv = (uint16_t*)(frame->data[2] + frame->linesize[2]);
            }
            to_planar(line_buf + info->nr_hanc_symbols, py, pu, pv, info->picture_width, sdi->has_sub_images);
            py += write_stride[0];
            pu += write_stride[1];
            pv += write_stride[2];
        }
    }

    for (int i = 0; i < sdi->audio_max_channels; i++) {
        if (sdi->audio_channels[i].is_present)
            detected_channels++;
    }

    for (int i = 0; i < 4; i++) {
        signaled_channels += sdi->audio_groups[i];
    }

    active_channels = detected_channels;
    if (signaled_channels > 0)
        active_channels = FFMIN(detected_channels, signaled_channels);

    if (sdi->active_channels != active_channels) {
        AVStream *ast = avformat_new_stream(s, NULL);
        if (!ast) {
            av_log(s, AV_LOG_ERROR, "could not allocate stream\n");
            return AVERROR(ENOMEM);
        }

        sdi->active_channels = active_channels;
        sdi->audio_nr_ch = sdi->active_channels;
        sdi->audio_rate = 48000;

        ast->id = AUDIO_STREAM_ID;
        ast->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        ast->codecpar->codec_id = AV_CODEC_ID_PCM_S24LE;
        ast->codecpar->ch_layout.nb_channels = sdi->active_channels;
        ast->codecpar->sample_rate = sdi->audio_rate;
        ast->codecpar->format = AV_SAMPLE_FMT_S32;
        ast->codecpar->bits_per_coded_sample = 24;
        ast->codecpar->bits_per_raw_sample = 24;
        ast->codecpar->block_align = ast->codecpar->bits_per_coded_sample *
                                     ast->codecpar->ch_layout.nb_channels / 8;
        ast->codecpar->bit_rate = (int64_t)ast->codecpar->sample_rate *
                                  ast->codecpar->bits_per_coded_sample *
                                  ast->codecpar->ch_layout.nb_channels;

        // TODO: pts in us or in samples??
        avpriv_set_pts_info(ast, 64, 1, 1000000);
        //    avpriv_set_pts_info(ast, 64, 1, ast->codecpar->sample_rate);
    }

    if (ret < 0) {
        av_packet_unref(pkt);
        return ret;
    }
    else {
        if (sdi->frame_padding > 0)
            avio_skip(s->pb, sdi->frame_padding);

        pkt->stream_index = VIDEO_STREAM_ID;
        pkt->pts = pkt->dts = sdi->frame_duration * (pkt->pos - sdi->header_size) / s->packet_size;
        pkt->duration = sdi->frame_duration;
        // The frame's audio goes out with the next call, at the frame's time
        sdi->audio_pts = pkt->pts;
    }
    return 0;
}

/*
 * Clean up
 */
static int sdi_read_close(struct AVFormatContext *s)
{
    SDIDemuxContext *sdi = s->priv_data;

    av_free(sdi->line_buf);
    av_free(sdi->line_buf_packed);

    av_free(sdi->audio_channels);
    av_free(sdi->audio_crc_ctx);

    return 0;
}

/* Demux options */
static const AVOption options[] = {
    { "sdi_standard", "", offsetof(SDIDemuxContext, option_standard), AV_OPT_TYPE_STRING, {.str = ""}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, NULL},
    { "no_header", "", offsetof(SDIDemuxContext, option_no_header), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, AV_OPT_FLAG_DECODING_PARAM, NULL },

    { NULL },
};

static const AVClass sdi_dec_class = {
    .class_name = "sdi",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFInputFormat ff_sdi_demuxer = {
    .p.name         = "sdi",
    .p.long_name    = NULL_IF_CONFIG_SMALL("DekTec SDI (digital video)"),
    .p.flags        = AVFMT_GENERIC_INDEX,
    .p.extensions   = "sdi",
    .p.codec_tag    = (const AVCodecTag * const []) { 0 },
    .p.priv_class   = &sdi_dec_class,
    .priv_data_size = sizeof(SDIDemuxContext),
    .read_probe     = sdi_probe,
    .read_header    = sdi_read_header,
    .read_packet    = sdi_read_packet,
    .read_close     = sdi_read_close,
};
