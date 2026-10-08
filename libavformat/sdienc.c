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
#include "libavutil/pixdesc.h"
#include "libswscale/swscale.h"
#include "sdicommon.h"

#include "cdtapi_sdi.h"

#include <inttypes.h>
#include <string.h>

#define MAX_STREAMS 8

/* The audio channels a frame can carry: four groups of four. */
#define AUDIO_MAX_CHANNELS 16

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
    AVRational rrate;
    uint8_t *frame_buf;             ///< a whole frame, packed to 10-bit symbols
    int frame_size;                 ///< size of a frame, padding included
    DtSdiView *view;                ///< the view of frame_buf the builder writes
    DtSdiBuilder *builder;          ///< puts each frame together
    int num_frames;
    int64_t next_pts;
    AVRational time_base;

    SdiBuffer *buffer[MAX_STREAMS];

    struct SwsContext *scale_context; ///< swscale context
    AVFrame *scale_frame;             ///< scale frame

    AVStream *audio_stream;   ///< the audio stream, or NULL
    int audio_nr_ch;          ///< channels the frames carry
    int stream_nr_ch;         ///< channels of the audio stream
    int bytes_per_sample;     ///< of the audio stream: 3 or 4
    int max_samples;          ///< most samples per channel a frame carries
    uint8_t *audio_in;        ///< a frame's samples, as the audio stream has them
    int32_t *audio_buf;       ///< the same, 24 bits at the top of 32, interleaved
    int64_t audio_pts;        ///< pts of the next frame's first sample

    SdiFrameSink sink;        ///< lends room for the frames, instead of the output
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

static void prepare_buffers(AVFormatContext *s)
{
    SdiMuxContext *sdi = s->priv_data;

    for (int i = 0; i < FFMIN(s->nb_streams, MAX_STREAMS); ++i) {
        sdi->buffer[i] = ff_sdi_buffer_alloc(s->streams[i]);
        if (s->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            sdi->buffer[i]->max_buffer = 3;
        else if (s->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            sdi->buffer[i]->max_buffer = 3*800;
    }
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
 * Set up CDTAPI's builder for the standard, which puts the frames together, and the
 * buffer it builds them in.
 */
static int init_builder(AVFormatContext *s, SdiMuxContext *sdi)
{
    DtWorkerPool *pool = NULL;
    int num_threads = 0;
    size_t raw_size = 0;
    DtapiResult result;
    int ret;

    sdi->vidstd = av_sdi_vidstd(sdi->sdi_info);
    result = DtSdiView_RawFrameSize(sdi->vidstd, 10, &raw_size);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "CDTAPI does not build frames of %s: %s\n",
               sdi->sdi_info->name, DtapiResult2Str(result));
        return AVERROR(EINVAL);
    }
    sdi->frame_size = raw_size;

    // The padding after the frame stays zero: the builder writes the frame only
    sdi->frame_buf = av_mallocz(sdi->frame_size);
    sdi->view = DtSdiView_Alloc();
    sdi->builder = DtSdiBuilder_Alloc();
    if (!sdi->frame_buf || !sdi->view || !sdi->builder)
        return AVERROR(ENOMEM);

    result = DtSdiBuilder_SetChecksums(sdi->builder, sdi->option_calc_crc != 0);
    if (result != DTAPI_OK)
        return AVERROR(EINVAL);

    ret = ff_sdi_worker_pool(s, sdi->threads, &pool, &num_threads);
    if (ret < 0)
        return ret;
    if (pool) {
        result = DtSdiBuilder_SetWorkerPool(sdi->builder, pool, num_threads);
        DtWorkerPool_Freep(&pool);
        if (result != DTAPI_OK) {
            av_log(s, AV_LOG_ERROR, "Could not give the builder its threads: %s\n",
                   DtapiResult2Str(result));
            return AVERROR(ENOMEM);
        }
    }
    return 0;
}

/*
 * Set up the audio the frames carry: the channels of the audio stream, or as many as
 * the sdi_nr_audio option asks for, the ones the stream lacks silent.
 */
static int init_audio(AVFormatContext *s, SdiMuxContext *sdi, AVStream *audio_stream)
{
    AVCodecParameters *par = audio_stream->codecpar;
    DtapiResult result;

    if (par->codec_id != AV_CODEC_ID_PCM_S24LE && par->codec_id != AV_CODEC_ID_PCM_S32LE) {
        av_log(s, AV_LOG_ERROR, "Unsupported audio codec %s\n",
               avcodec_get_name(par->codec_id));
        return AVERROR(EINVAL);
    }
    if (par->sample_rate != 48000) {
        av_log(s, AV_LOG_ERROR, "Unsupported sample rate %d\n", par->sample_rate);
        return AVERROR(EINVAL);
    }

    // The audio buffer counts in samples, whatever time base the stream came with
    avpriv_set_pts_info(audio_stream, 64, 1, par->sample_rate);

    sdi->stream_nr_ch = par->ch_layout.nb_channels;
    sdi->audio_nr_ch = sdi->option_audio_nr_ch == -1 ? sdi->stream_nr_ch
                                                     : sdi->option_audio_nr_ch;
    if (sdi->audio_nr_ch < 1 || sdi->audio_nr_ch > AUDIO_MAX_CHANNELS) {
        av_log(s, AV_LOG_ERROR, "SDI carries 1 to %d audio channels, not %d\n",
               AUDIO_MAX_CHANNELS, sdi->audio_nr_ch);
        return AVERROR(EINVAL);
    }
    sdi->bytes_per_sample = av_get_bits_per_sample(par->codec_id) / 8;

    result = DtSdiAudio_MaxSamples(sdi->vidstd, &sdi->max_samples);
    if (result != DTAPI_OK)
        return AVERROR(EINVAL);
    sdi->audio_in = av_malloc((size_t)sdi->max_samples * sdi->stream_nr_ch *
                              sdi->bytes_per_sample);
    sdi->audio_buf = av_malloc_array((size_t)sdi->max_samples * sdi->audio_nr_ch,
                                     sizeof(*sdi->audio_buf));
    if (!sdi->audio_in || !sdi->audio_buf)
        return AVERROR(ENOMEM);
    sdi->audio_stream = audio_stream;
    sdi->audio_pts = AV_NOPTS_VALUE;
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

    sdi_frame_size = sdi_info->nr_sdi_lines * (sdi_info->nr_hanc_symbols + sdi_info->nr_vanc_symbols) * 10 / 8;
    aligned_frame_size = (sdi_frame_size + 7) & ~7; // align to 8-byte
    sdi->frame_padding = aligned_frame_size - sdi_frame_size;

    ret = init_builder(s, sdi);
    if (ret < 0)
        return ret;

    prepare_buffers(s);

    av_log(s, AV_LOG_DEBUG, "Frame size %d with padding %d\n", sdi->frame_size,
           sdi->frame_padding);

    /*
     * Setup audio
     */
    if (audio_stream) {
        ret = init_audio(s, sdi, audio_stream);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static void sdi_deinit(AVFormatContext *s)
{
    SdiMuxContext *sdi = s->priv_data;
    DtSdiBuilder_Freep(&sdi->builder);
    DtSdiView_Freep(&sdi->view);
    av_freep(&sdi->frame_buf);
    av_freep(&sdi->audio_in);
    av_freep(&sdi->audio_buf);
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
 * Take the samples of the next frame from the audio stream, as many as the builder
 * embeds in it, into audio_buf; samples the stream does not have are silent. Sets up
 * audio to point the builder at them.
 *
 * The samples follow on from those of the frame before. At a 1001 rate a frame does not
 * start on a sample, and its number of samples follows the cadence, so the first sample
 * of a frame is counted on from the first frame's rather than worked out from its time.
 */
static int get_frame_audio(AVFormatContext *s, SdiMuxContext *sdi, DtSdiAudio *audio)
{
    int stream_index = sdi->audio_stream->index;
    int bps = sdi->bytes_per_sample;
    int stride = sdi->stream_nr_ch * bps;
    int nr_samples = 0;
    DtapiResult result;

    result = DtSdiBuilder_GetNumAudioSamples(sdi->builder, sdi->vidstd, 0, &nr_samples);
    if (result != DTAPI_OK)
        return AVERROR(EINVAL);

    if (sdi->audio_pts == AV_NOPTS_VALUE)
        sdi->audio_pts = av_rescale_q_rnd(sdi->next_pts, sdi->time_base,
                                          sdi->audio_stream->time_base, AV_ROUND_UP);
    memset(sdi->audio_in, 0, (size_t)nr_samples * stride);
    ff_sdi_buffer_get_audio(sdi->buffer[stream_index], sdi->audio_in,
                            nr_samples * stride, sdi->audio_pts, nr_samples);
    sdi->audio_pts += av_rescale_q(nr_samples, (AVRational){ 1, 48000 },
                                   sdi->audio_stream->time_base);

    // The builder takes 24 bits at the top of 32
    for (int i = 0; i < nr_samples; i++) {
        const uint8_t *in = sdi->audio_in + (size_t)i * stride;
        int32_t *out = sdi->audio_buf + (size_t)i * sdi->audio_nr_ch;
        for (int ch = 0; ch < sdi->audio_nr_ch; ch++) {
            if (ch >= sdi->stream_nr_ch)
                out[ch] = 0;
            else if (bps == 3)
                out[ch] = (int32_t)(AV_RL24(in + ch * bps) << 8);
            else
                out[ch] = (int32_t)AV_RL32(in + ch * bps);
        }
    }

    for (int ch = 0; ch < sdi->audio_nr_ch; ch++) {
        audio->Formats[ch / 2] = DT_SDI_AUDIO_PCM;
        audio->Channels[ch].Samples = sdi->audio_buf + ch;
        audio->Channels[ch].Stride = sdi->audio_nr_ch;
        audio->Channels[ch].NumSamples = nr_samples;
    }
    return 0;
}

/*
 * Point the view at room for the next frame: room the sink lends, or the muxer's own
 * buffer.
 */
static int get_room(AVFormatContext *s, SdiMuxContext *sdi)
{
    DtapiResult result;

    if (sdi->sink.acquire)
        return sdi->sink.acquire(sdi->sink.opaque, sdi->view);

    result = DtSdiView_SetRawFrame(sdi->view, sdi->frame_buf, sdi->frame_size,
                                   sdi->vidstd, 10);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not point the builder at the frame: %s\n",
               DtapiResult2Str(result));
        return AVERROR(EINVAL);
    }
    return 0;
}

/*
 * Write a new SDI frame, which CDTAPI's builder puts together from the image and the
 * audio for its time, in the muxer's buffer or in room the sink lends.
 */
static int write_sdi_frame(struct AVFormatContext *s, AVPacket *arg_pkt)
{
    SdiMuxContext *sdi = s->priv_data;
    AVFrame *arg_frame = (AVFrame*)arg_pkt->data;
    AVFrame *frame;
    DtSdiImage image = { 0 };
    DtSdiAudio audio = { 0 };
    DtapiResult result;
    int ret;

    // Scale frame if needed
    if (sdi->scale_context) {
        frame = sdi->scale_frame;
        ret = sws_scale_frame(sdi->scale_context, frame, arg_frame);
        if (ret <= 0)
            return ret;
    } else {
        frame = arg_frame;
    }

    image.Format = DT_SDI_PIXFMT_YUV422P_10B;
    image.Fields = DT_SDI_FIELDS_WOVEN;
    for (int i = 0; i < 3; i++) {
        image.Planes[i] = frame->data[i];
        image.Strides[i] = frame->linesize[i];
    }

    if (sdi->audio_stream) {
        ret = get_frame_audio(s, sdi, &audio);
        if (ret < 0)
            return ret;
    }

    ret = get_room(s, sdi);
    if (ret < 0)
        return ret;
    result = DtSdiBuilder_Build(sdi->builder, sdi->view, &image,
                                sdi->audio_stream ? &audio : NULL, NULL);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not build the frame: %s\n",
               DtapiResult2Str(result));
        // Lent room is handed on all the same, black and silent: until it is, the sink
        // lends no more
        if (!sdi->sink.commit ||
            DtSdiBuilder_Build(sdi->builder, sdi->view, NULL, NULL, NULL) != DTAPI_OK ||
            sdi->sink.commit(sdi->sink.opaque, sdi->view) < 0)
            return AVERROR(EINVAL);
        sdi->next_pts += 1;
        return 0;
    }

    sdi->next_pts += 1;

    if (sdi->sink.commit)
        return sdi->sink.commit(sdi->sink.opaque, sdi->view);
    avio_write(s->pb, sdi->frame_buf, sdi->frame_size);
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

int av_sdi_mux_set_sink(AVFormatContext *s, const SdiFrameSink *sink)
{
    SdiMuxContext *sdi;

    if (!s->oformat || strcmp(s->oformat->name, "sdi") || !s->priv_data)
        return AVERROR(EINVAL);
    sdi = s->priv_data;
    sdi->sink = *sink;
    return 0;
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
