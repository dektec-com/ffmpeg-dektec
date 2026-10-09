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

#include "avformat.h"
#include "internal.h"
#include "libavcodec/codec_id.h"
#include "mux.h"
#include "libavcodec/put_bits.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "sdicommon.h"
#include "sdiframe.h"

#include "cdtapi_sdi.h"

#include <inttypes.h>
#include <string.h>

typedef struct SdiMuxContext {
    const AVClass *av_class;

    uint32_t option_payload_id;     ///< option payload identifier, not used
    int option_audio_nr_ch;         ///< option number of audio channels
    char* option_standard;          ///< option sdi standard
    int option_interleave_type;     ///< option interleave type, not used
    int option_calc_crc;            ///< option to enable/disable CRC insertion
    int option_no_header;           ///< option to disable file header
    int threads;                    ///< option threads, FF_SDI_THREADS_AUTO, 1 or more

    int frame_padding;              ///< nr. of padding bytes after a frame
    const struct SdiInfo *sdi_info; ///< constants for the standard used
    int vidstd;                     ///< the standard's DTAPI_VIDSTD_ code
    uint8_t *frame_buf;             ///< a whole frame, packed to 10-bit symbols
    int frame_size;                 ///< size of a frame, padding included
    SdiPacker *packer;              ///< puts each frame together in frame_buf
    int num_frames;
} SdiMuxContext;

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
    sdi->vidstd = av_sdi_vidstd(sdi_info);

    av_log(s, AV_LOG_DEBUG, "SDI standard: %s\n", sdi_info->name);

    sdi_frame_size = sdi_info->nr_sdi_lines * (sdi_info->nr_hanc_symbols + sdi_info->nr_vanc_symbols) * 10 / 8;
    aligned_frame_size = (sdi_frame_size + 7) & ~7; // align to 8-byte
    sdi->frame_padding = aligned_frame_size - sdi_frame_size;

    // CDTAPI's builder puts the frames together, in a buffer whose padding stays zero
    ret = ff_sdi_packer_alloc(&sdi->packer, s, sdi_info, sdi->threads,
                              sdi->option_calc_crc, video_stream, audio_stream,
                              sdi->option_audio_nr_ch);
    if (ret < 0)
        return ret;
    sdi->frame_size = ff_sdi_packer_frame_size(sdi->packer);
    sdi->frame_buf = av_mallocz(sdi->frame_size);
    if (!sdi->frame_buf)
        return AVERROR(ENOMEM);

    av_log(s, AV_LOG_DEBUG, "Frame size %d with padding %d\n", sdi->frame_size,
           sdi->frame_padding);
    return 0;
}

static void sdi_deinit(AVFormatContext *s)
{
    SdiMuxContext *sdi = s->priv_data;

    ff_sdi_packer_free(&sdi->packer);
    av_freep(&sdi->frame_buf);
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
 * Build every frame the packer has ready, or with flush set every frame it holds, and
 * write it to the output.
 */
static int write_ready_frames(AVFormatContext *s, int flush)
{
    SdiMuxContext *sdi = s->priv_data;
    int ret;

    while ((ret = ff_sdi_packer_ready(sdi->packer, flush)) > 0) {
        if (DtSdiView_SetRawFrame(ff_sdi_packer_view(sdi->packer), sdi->frame_buf,
                                  sdi->frame_size, sdi->vidstd, 10) != DTAPI_OK)
            return AVERROR(EINVAL);
        ret = ff_sdi_packer_build(sdi->packer);
        if (ret < 0)
            return ret;
        avio_write(s->pb, sdi->frame_buf, sdi->frame_size);
        sdi->num_frames++;
    }
    return ret;
}

/*
 * Hand the packet to the packer, and write the frames that are then ready.
 */
static int sdi_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    SdiMuxContext *sdi = s->priv_data;
    int ret = ff_sdi_packer_add(sdi->packer, pkt);

    if (ret < 0)
        return ret;
    return write_ready_frames(s, 0);
}

/*
 * Write a packet. A flush, pkt NULL, builds nothing: a frame waits for its audio until
 * the trailer.
 */
static int sdi_write_flush_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    if (!pkt)
        return 1;
    return sdi_write_packet(s, pkt);
}

/*
 * Build the frames still held, and write num_frames in the file header.
 */
static int sdi_write_trailer(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    SdiMuxContext *sdi = s->priv_data;
    int64_t file_size;
    int ret;

    // The frames still waiting for their audio go out with what audio there is
    ret = write_ready_frames(s, 1);
    if (ret < 0)
        return ret;

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
    { "threads", "threads a frame is put together over: auto, 1 for one, or more", offsetof(SdiMuxContext, threads), AV_OPT_TYPE_INT, { .i64 = FF_SDI_THREADS_AUTO }, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, "threads" },
    { "auto", "4 threads, and as many pieces as the standard calls for", 0, AV_OPT_TYPE_CONST, { .i64 = FF_SDI_THREADS_AUTO }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM, "threads" },

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
