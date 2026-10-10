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
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "sdicommon.h"
#include "sdiframe.h"

#include "cdtapi_sdi.h"

#include <stdint.h>
#include <string.h>

/* SDI private data */
typedef struct SDIDemuxContext {
    const AVClass *av_class;

    char *option_standard; ///< option standard
    int option_no_header;  ///< option to disable file header
    int threads;           ///< option threads, FF_SDI_THREADS_AUTO, 1 or more
    int v210;              ///< option v210: the video as v210 packets

    const struct SdiInfo *sdi_info; ///< constants for the standard used
    int vidstd;                     ///< the standard's DTAPI_VIDSTD_ code
    int header_size;                ///< Size of header in bytes
    int frame_size;                 ///< SDI frame size, padding included

    uint8_t *frame_buf;    ///< the frame being read
    SdiUnpacker *unpacker; ///< takes each frame apart into its image and audio
    int has_audio;         ///< whether the audio stream was added

    // 3G level B: a frame of the file is a frame of the interface with two pictures,
    // field 1 and field 2, which go out as two images
    int level_b;           ///< whether the standard is 3G level B
    int field2_waiting;    ///< whether field 2 of the frame read last is to go out
    int64_t frame_pos;     ///< where that frame starts in the file
} SDIDemuxContext;

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
 * Return sdi info for a given header, or NULL if not found. A header of 3G level B
 * describes a frame of the interface, of two fields, which carries progressive
 * pictures, so it finds a standard of level B by its pictures alone; any other finds one
 * that is not level B.
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
    const int level_b = hdr->format.line_rate == SDI_LINE_RATE_3G &&
                        hdr->format.sdi_level == SDI_LEVEL_B_DL;

    SdiScanningMethod scanning_method;
    if (level_b)
        scanning_method = interlaced_picture ? SDI_I_PICT_I_TR : SDI_P_PICT_P_TR;
    else if (interlaced_picture && interlaced_transport)
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
        if (av_sdi_is_level_b(info) != level_b)
            continue;
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

static void log_setup(AVFormatContext *s)
{
    SDIDemuxContext *sdi = s->priv_data;
    const struct SdiInfo *sdi_info = sdi->sdi_info;

    av_log(s, AV_LOG_DEBUG, "Header size   = %d\n", sdi->header_size);
    av_log(s, AV_LOG_DEBUG, "Frame size    = %d\n", sdi->frame_size);
    av_log(s, AV_LOG_DEBUG, "PayloadFormat = 0x%02x\n", sdi_info->payload_format);
    av_log(s, AV_LOG_DEBUG, "AspectRatio   = %s\n", sdi_info->aspect_ratio == SDI_AR_16_9 ? "16:9" : "4:3");
    av_log(s, AV_LOG_DEBUG, "Picture dim   = %dx%d\n", sdi_info->picture_width, sdi_info->picture_height);
    av_log(s, AV_LOG_DEBUG, "Interlacing   = pic:%d transport:%d\n", is_interlaced_picture(sdi_info->scanning_method), is_interlaced_transport(sdi_info->scanning_method));
}

/*
 * Set up CDTAPI's parser for the standard, which takes the frames apart, and the video
 * stream. sdi_info, frame_size and header_size must be set. The audio stream is added
 * when the first frame with audio arrives.
 *
 * Return 0 if OK.
 */
static int sdi_setup(AVFormatContext *s)
{
    SDIDemuxContext *sdi = s->priv_data;
    const struct SdiInfo *sdi_info = sdi->sdi_info;
    size_t raw_size = 0;
    DtapiResult result;
    int ret;

    // The parser takes a frame of the size of the standard's raw frame, which is that
    // of a frame in the file.
    sdi->vidstd = av_sdi_vidstd(sdi_info);
    result = DtSdiView_RawFrameSize(sdi->vidstd, 10, &raw_size);
    if (result != DTAPI_OK || raw_size != sdi->frame_size) {
        av_log(s, AV_LOG_ERROR, "A frame of %s takes %zu bytes, the file's %d\n",
               sdi_info->name, raw_size, sdi->frame_size);
        return AVERROR_INVALIDDATA;
    }
    sdi->frame_buf = av_malloc(sdi->frame_size);
    if (!sdi->frame_buf)
        return AVERROR(ENOMEM);
    sdi->level_b = av_sdi_is_level_b(sdi_info);

    ret = ff_sdi_unpacker_alloc(&sdi->unpacker, s, sdi_info, sdi->threads, sdi->v210);
    if (ret < 0)
        return ret;
    log_setup(s);
    // In 3G level B each frame gives two images
    ret = ff_sdi_unpacker_add_video_stream(sdi->unpacker, s,
            FFMAX(avio_size(s->pb) - sdi->header_size, 0) / sdi->frame_size *
            (sdi->level_b ? 2 : 1));
    if (ret < 0)
        return ret;

    // Set packet size to read full frames.
    s->packet_size = sdi->frame_size;
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
    size_t raw_size = 0;

    uint8_t buffer[1024];

    // The audio stream is added when the first frame with audio arrives
    s->ctx_flags |= AVFMTCTX_NOHEADER;

    if (sdi->option_no_header) {
        StandardOption options = {0};
        int ret;

        if (strlen(sdi->option_standard) == 0) {
            av_log(s, AV_LOG_WARNING, "Please provide SDI standard when there is no header\n");
            return AVERROR(EINVAL);
        }

        ret = av_parse_standard_option(s, sdi->option_standard, &options);
        if (ret < 0)
            return AVERROR(EINVAL);

        sdi->sdi_info = av_sdi_info(av_sdi_get_fmt(&options));
        if (sdi->sdi_info == NULL) {
            av_log(s, AV_LOG_WARNING, "SDI standard %s is not supported\n", sdi->option_standard);
            return AVERROR(EINVAL);
        }
        // A frame of the file is the standard's raw frame, in 3G level B a frame of
        // the interface
        if (DtSdiView_RawFrameSize(av_sdi_vidstd(sdi->sdi_info), 10, &raw_size) != DTAPI_OK)
            return AVERROR(EINVAL);
        sdi->frame_size = (int)raw_size;
        sdi->header_size = 0;
    }
    else {
        avio_read(s->pb, buffer, 1024);

        init_get_bits(&gb, buffer, 1024);
        get_sdi_file_header(&gb, &hdr);

        if (hdr.format.line_rate == SDI_LINE_RATE_3G &&
            hdr.format.sdi_level == SDI_LEVEL_B_DS) {
            av_log(s, AV_LOG_ERROR, "3G level B dual stream, two streams on one link, is "
                   "not supported\n");
            return AVERROR_PATCHWELCOME;
        }

        // Retrieve image properties from header
        sdi->sdi_info = find_standard_from_sdi(s, &hdr);
        if (sdi->sdi_info == NULL) {
            av_log(s, AV_LOG_WARNING, "SDI format is not supported\n");
            return AVERROR(EINVAL);
        }
        sdi->frame_size = hdr.frame_size;
        sdi->header_size = hdr.header_size;

        // Advance read pointer to start of payload
        n = avio_seek(s->pb, hdr.header_size, SEEK_SET);
        if (n < 0)
            return AVERROR(EIO);
    }

    return sdi_setup(s);
}

/*
 * Read one packet: the image of the next frame in the file, which CDTAPI's parser takes
 * apart, or the frame's audio, which goes out with the next call. The audio stream is
 * added with the first frame that carries audio, in the frame's channels.
 *
 * In 3G level B a frame of the file holds two pictures: field 1's image and audio go
 * out, then field 2's, each numbered as a picture, field 1 even and field 2 odd.
 */
static int sdi_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    SDIDemuxContext *sdi = s->priv_data;
    DtapiResult result;
    int64_t pos;
    int64_t frame_number;
    int ret;

    ret = ff_sdi_unpacker_audio(sdi->unpacker, pkt);
    if (ret != 0)
        return ret < 0 ? ret : 0;

    // Field 2 of the frame read last, unless a seek went elsewhere since
    pos = avio_tell(s->pb);
    if (sdi->field2_waiting && pos == sdi->frame_pos + sdi->frame_size) {
        sdi->field2_waiting = 0;
        result = DtSdiView_SetLevelBField(ff_sdi_unpacker_view(sdi->unpacker), 2);
        if (result != DTAPI_OK) {
            av_log(s, AV_LOG_ERROR, "Could not point the parser at field 2: %s\n",
                   DtapiResult2Str(result));
            return AVERROR_INVALIDDATA;
        }
        frame_number = (sdi->frame_pos - sdi->header_size) / sdi->frame_size * 2 + 1;
        ret = ff_sdi_unpacker_parse(sdi->unpacker, frame_number, pkt);
        if (ret < 0)
            return ret;
        pkt->pos = sdi->frame_pos;
        goto parsed;
    }
    sdi->field2_waiting = 0;

    ret = avio_read(s->pb, sdi->frame_buf, sdi->frame_size);
    if (ret < 0)
        return ret;
    if (ret < sdi->frame_size)
        return AVERROR_EOF;
    result = DtSdiView_SetRawFrame(ff_sdi_unpacker_view(sdi->unpacker), sdi->frame_buf,
                                   sdi->frame_size, sdi->vidstd, 10);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not point the parser at the frame: %s\n",
               DtapiResult2Str(result));
        return AVERROR_INVALIDDATA;
    }

    // The view starts with field 1 of a frame of level B
    frame_number = (pos - sdi->header_size) / sdi->frame_size;
    if (sdi->level_b)
        frame_number *= 2;
    ret = ff_sdi_unpacker_parse(sdi->unpacker, frame_number, pkt);
    if (ret < 0)
        return ret;
    pkt->pos = pos;
    if (sdi->level_b) {
        sdi->field2_waiting = 1;
        sdi->frame_pos = pos;
    }

parsed:
    if (!sdi->has_audio && ff_sdi_unpacker_nb_channels(sdi->unpacker) > 0) {
        ret = ff_sdi_unpacker_add_audio_stream(sdi->unpacker, s,
                                               ff_sdi_unpacker_nb_channels(sdi->unpacker));
        if (ret < 0) {
            av_packet_unref(pkt);
            return ret;
        }
        sdi->has_audio = 1;
    }
    return 0;
}

/*
 * Clean up
 */
static int sdi_read_close(struct AVFormatContext *s)
{
    SDIDemuxContext *sdi = s->priv_data;

    ff_sdi_unpacker_free(&sdi->unpacker);
    av_freep(&sdi->frame_buf);
    return 0;
}

/* Demux options */
static const AVOption options[] = {
    { "sdi_standard", "", offsetof(SDIDemuxContext, option_standard), AV_OPT_TYPE_STRING, {.str = ""}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, NULL},
    { "no_header", "", offsetof(SDIDemuxContext, option_no_header), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, AV_OPT_FLAG_DECODING_PARAM, NULL },
    { "threads", "threads a frame is taken apart over: auto, 1 for one, or more", offsetof(SDIDemuxContext, threads), AV_OPT_TYPE_INT, { .i64 = FF_SDI_THREADS_AUTO }, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM, "threads" },
    { "auto", "4 threads, and as many pieces as the standard calls for", 0, AV_OPT_TYPE_CONST, { .i64 = FF_SDI_THREADS_AUTO }, 0, 0, AV_OPT_FLAG_DECODING_PARAM, "threads" },
    { "v210", "give the video as v210 packets rather than wrapped yuv422p10le frames", offsetof(SDIDemuxContext, v210), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, AV_OPT_FLAG_DECODING_PARAM },

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
