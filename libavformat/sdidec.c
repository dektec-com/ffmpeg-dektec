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
#include "libavutil/buffer.h"
#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "sdicommon.h"

#include "cdtapi_sdi.h"

#include <stdint.h>
#include <string.h>

#define VIDEO_STREAM_ID 0
#define AUDIO_STREAM_ID 1

#define AUDIO_SAMPLE_SIZE 3

/* The audio channels a frame can carry: four groups of four. */
#define AUDIO_MAX_CHANNELS 16

/* SDI private data */
typedef struct SDIDemuxContext {
    const AVClass *av_class;

    char *option_standard; ///< option standard
    int option_no_header;  ///< option to disable file header
    int threads;           ///< option threads, FF_SDI_THREADS_AUTO, 1 or more

    const struct SdiInfo *sdi_info; ///< constants for the standard used
    int vidstd;                     ///< the standard's DTAPI_VIDSTD_ code
    int header_size;                ///< Size of header in bytes
    int frame_size;                 ///< SDI frame size, padding included
    int64_t frame_duration;         ///< Frame duration in us

    uint8_t *frame_buf;    ///< the frame being read
    AVBufferPool *image_pool; ///< the buffers of the images the parser writes
    int linesize[4];       ///< the linesizes of an image's planes
    size_t plane_size[3];  ///< the sizes of an image's planes
    DtSdiView *view;       ///< the view of frame_buf the parser reads
    DtSdiParser *parser;   ///< takes each frame apart into its image and audio

    int32_t *audio_buf;    ///< a frame's samples, one channel after the other
    int max_samples;       ///< room for samples per channel in audio_buf
    int audio_index;       ///< index of the audio stream, -1 until a frame carries audio
    int audio_nr_ch;       ///< channels of the audio stream
    int audio_samples;     ///< samples per channel of the last frame, yet to go out
    int warned_nr_ch;      ///< whether a change in the number of channels was logged
    int64_t audio_pts;     ///< pts of the frame the audio came with
} SDIDemuxContext;

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
    av_log(s, AV_LOG_DEBUG, "Frame size    = %d\n", sdi->frame_size);
    av_log(s, AV_LOG_DEBUG, "PayloadFormat = 0x%02x\n", sdi_info->payload_format);
    av_log(s, AV_LOG_DEBUG, "AspectRatio   = %s\n", sdi_info->aspect_ratio == SDI_AR_16_9 ? "16:9" : "4:3");
    av_log(s, AV_LOG_DEBUG, "Sample ar     = %d/%d\n", sar.num, sar.den);
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
    AVStream *vst = NULL;
    AVRational rrate;
    AVRational sar;
    int64_t frame_count = 0;
    int height = sdi_info->picture_height;
    int width = sdi_info->picture_width;
    DtWorkerPool *pool = NULL;
    int num_threads = 0;
    size_t raw_size = 0;
    DtapiResult result;
    int ret = 0;

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
    sdi->view = DtSdiView_Alloc();
    sdi->parser = DtSdiParser_Alloc();
    if (!sdi->frame_buf || !sdi->view || !sdi->parser)
        return AVERROR(ENOMEM);

    // The images come from a pool: a buffer the size of an image, allocated anew for
    // every frame, costs the system more than the parser takes to fill it.
    ret = av_image_fill_linesizes(sdi->linesize, AV_PIX_FMT_YUV422P10LE, FFALIGN(width, 64));
    if (ret < 0)
        return ret;
    for (int i = 0; i < 3; i++)
        sdi->plane_size[i] = (size_t)sdi->linesize[i] * height;
    sdi->image_pool = av_buffer_pool_init(sdi->plane_size[0] + sdi->plane_size[1] +
                                          sdi->plane_size[2], av_buffer_alloc);
    if (!sdi->image_pool)
        return AVERROR(ENOMEM);

    ret = ff_sdi_worker_pool(s, sdi->threads, &pool, &num_threads);
    if (ret < 0)
        return ret;
    if (pool) {
        result = DtSdiParser_SetWorkerPool(sdi->parser, pool, num_threads);
        DtWorkerPool_Freep(&pool);
        if (result != DTAPI_OK) {
            av_log(s, AV_LOG_ERROR, "Could not give the parser its threads: %s\n",
                   DtapiResult2Str(result));
            return AVERROR(ENOMEM);
        }
    }

    // Room for the most samples a frame of the standard carries, on every channel
    result = DtSdiAudio_MaxSamples(sdi->vidstd, &sdi->max_samples);
    if (result != DTAPI_OK)
        return AVERROR_INVALIDDATA;
    sdi->audio_buf = av_calloc((size_t)AUDIO_MAX_CHANNELS * sdi->max_samples,
                               sizeof(*sdi->audio_buf));
    if (!sdi->audio_buf)
        return AVERROR(ENOMEM);
    sdi->audio_index = -1;

    frame_count = (avio_size(s->pb) - sdi->header_size) / sdi->frame_size;

    rrate = av_sdi_rate(sdi_info->picture_rate);
    sdi->frame_duration = (1000000 * rrate.den + rrate.num - 1)/ rrate.num;  // in us, rounded up
    av_reduce(&sar.num, &sar.den,
            av_sdi_aspect_ratio(sdi_info->aspect_ratio).num * height,
            av_sdi_aspect_ratio(sdi_info->aspect_ratio).den * width,
            1024*1024);

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
        sdi->frame_size = (sdi->sdi_info->nr_sdi_lines *
                (sdi->sdi_info->nr_hanc_symbols + sdi->sdi_info->nr_vanc_symbols) * 10 / 8 + 7) & ~7;
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
 * Add the audio stream, with the number of channels of the first frame that carries
 * audio. The number of channels stays: a later frame with fewer gives silence on the
 * others, one with more loses the channels beyond.
 */
static int add_audio_stream(AVFormatContext *s, int nr_ch)
{
    SDIDemuxContext *sdi = s->priv_data;
    AVStream *ast = avformat_new_stream(s, NULL);

    if (!ast) {
        av_log(s, AV_LOG_ERROR, "could not allocate stream\n");
        return AVERROR(ENOMEM);
    }
    sdi->audio_index = ast->index;
    sdi->audio_nr_ch = nr_ch;

    ast->id = AUDIO_STREAM_ID;
    ast->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    ast->codecpar->codec_id = AV_CODEC_ID_PCM_S24LE;
    ast->codecpar->ch_layout.nb_channels = nr_ch;
    ast->codecpar->sample_rate = 48000;
    ast->codecpar->format = AV_SAMPLE_FMT_S32;
    ast->codecpar->bits_per_coded_sample = 24;
    ast->codecpar->bits_per_raw_sample = 24;
    ast->codecpar->block_align = ast->codecpar->bits_per_coded_sample *
                                 ast->codecpar->ch_layout.nb_channels / 8;
    ast->codecpar->bit_rate = (int64_t)ast->codecpar->sample_rate *
                              ast->codecpar->bits_per_coded_sample *
                              ast->codecpar->ch_layout.nb_channels;
    avpriv_set_pts_info(ast, 64, 1, 1000000);
    return 0;
}

/*
 * Take the audio of the frame just parsed: the channels from the first up to the last
 * one the frame carried as valid audio. A channel the frame carried with samples marked
 * not valid is one a transmitter fills for a group's channels that have no audio, so
 * it does not count unless a valid channel follows it.
 */
static int take_audio(AVFormatContext *s, const DtSdiAudio *audio)
{
    SDIDemuxContext *sdi = s->priv_data;
    int nr_ch = 0;
    int nr_samples = 0;
    int ret;

    for (int ch = 0; ch < AUDIO_MAX_CHANNELS; ch++) {
        if (audio->Channels[ch].Present && !audio->Channels[ch].Invalid)
            nr_ch = ch + 1;
    }
    if (nr_ch == 0)
        return 0;

    if (sdi->audio_index < 0) {
        ret = add_audio_stream(s, nr_ch);
        if (ret < 0)
            return ret;
    } else if (nr_ch != sdi->audio_nr_ch && !sdi->warned_nr_ch) {
        av_log(s, AV_LOG_WARNING, "The frames now carry %d audio channels, the stream "
               "keeps %d\n", nr_ch, sdi->audio_nr_ch);
        sdi->warned_nr_ch = 1;
    }

    for (int ch = 0; ch < sdi->audio_nr_ch; ch++) {
        if (audio->Channels[ch].Present)
            nr_samples = FFMAX(nr_samples, audio->Channels[ch].NumSamples);
    }
    for (int ch = 0; ch < sdi->audio_nr_ch; ch++) {
        const DtSdiAudioChannel *channel = &audio->Channels[ch];
        int32_t *samples = sdi->audio_buf + (size_t)ch * sdi->max_samples;
        int from = channel->Present ? FFMIN(channel->NumSamples, nr_samples) : 0;

        memset(samples + from, 0, (nr_samples - from) * sizeof(*samples));
    }
    sdi->audio_samples = nr_samples;
    return 0;
}

/*
 * Put the audio of the frame read last into pkt, as 24-bit samples, the channels
 * interleaved.
 */
static int read_audio_packet(AVFormatContext *s, AVPacket *pkt)
{
    SDIDemuxContext *sdi = s->priv_data;
    int nr_ch = sdi->audio_nr_ch;
    int nr_samples = sdi->audio_samples;
    uint8_t *ptr;
    int ret;

    sdi->audio_samples = 0;
    ret = av_new_packet(pkt, nr_samples * nr_ch * AUDIO_SAMPLE_SIZE);
    if (ret < 0)
        return ret;
    pkt->pos = avio_tell(s->pb);
    pkt->stream_index = sdi->audio_index;
    pkt->pts = pkt->dts = sdi->audio_pts;
    pkt->duration = sdi->frame_duration;

    // The parser gives a sample its 24 bits at the top of 32
    ptr = pkt->data;
    for (int i = 0; i < nr_samples; i++) {
        for (int ch = 0; ch < nr_ch; ch++) {
            AV_WL24(ptr, (uint32_t)sdi->audio_buf[(size_t)ch * sdi->max_samples + i] >> 8);
            ptr += AUDIO_SAMPLE_SIZE;
        }
    }
    return 0;
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
 * Read a single SDI frame and take it apart with CDTAPI's parser into its image, which
 * goes out now, and its audio, which goes out with the next call.
 */
static int sdi_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    SDIDemuxContext *sdi = s->priv_data;
    const struct SdiInfo *info = sdi->sdi_info;
    AVFrame *frame = NULL;
    DtSdiImage image = { 0 };
    DtSdiAudio audio = { 0 };
    DtapiResult result;
    int64_t pos;
    int ret = 0;

    if (sdi->audio_samples > 0)
        return read_audio_packet(s, pkt);

    pos = avio_tell(s->pb);
    ret = avio_read(s->pb, sdi->frame_buf, sdi->frame_size);
    if (ret < 0)
        return ret;
    if (ret < sdi->frame_size)
        return AVERROR_EOF;

    result = DtSdiView_SetRawFrame(sdi->view, sdi->frame_buf, sdi->frame_size,
                                   sdi->vidstd, 10);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not point the parser at the frame: %s\n",
               DtapiResult2Str(result));
        return AVERROR_INVALIDDATA;
    }

    frame = av_frame_alloc();
    if (!frame)
        return AVERROR(ENOMEM);

    frame->format = AV_PIX_FMT_YUV422P10LE;
    frame->width = info->picture_width;
    frame->height = info->picture_height;
    frame->buf[0] = av_buffer_pool_get(sdi->image_pool);
    if (!frame->buf[0]) {
        av_frame_free(&frame);
        return AVERROR(ENOMEM);
    }
    frame->data[0] = frame->buf[0]->data;
    frame->data[1] = frame->data[0] + sdi->plane_size[0];
    frame->data[2] = frame->data[1] + sdi->plane_size[1];
    for (int i = 0; i < 3; i++)
        frame->linesize[i] = sdi->linesize[i];
    frame->extended_data = frame->data;

    image.Format = DT_SDI_PIXFMT_YUV422P_10B;
    image.Fields = DT_SDI_FIELDS_WOVEN;
    for (int i = 0; i < 3; i++) {
        image.Planes[i] = frame->data[i];
        image.Strides[i] = frame->linesize[i];
    }

    // PCM on every channel, each into its own part of audio_buf
    for (int pair = 0; pair < AUDIO_MAX_CHANNELS / 2; pair++)
        audio.Formats[pair] = DT_SDI_AUDIO_PCM;
    for (int ch = 0; ch < AUDIO_MAX_CHANNELS; ch++) {
        audio.Channels[ch].Samples = sdi->audio_buf + (size_t)ch * sdi->max_samples;
        audio.Channels[ch].MaxSamples = sdi->max_samples;
    }

    result = DtSdiParser_Parse(sdi->parser, sdi->view, &image, &audio, NULL);
    if (result != DTAPI_OK) {
        av_log(s, AV_LOG_ERROR, "Could not take the frame apart: %s\n",
               DtapiResult2Str(result));
        av_frame_free(&frame);
        return AVERROR_INVALIDDATA;
    }

    pkt->buf =
        av_buffer_create((uint8_t *)frame, sizeof(*frame), free_frame, NULL, 0);
    if (!pkt->buf) {
        av_frame_free(&frame);
        return AVERROR(ENOMEM);
    }

    pkt->data = (uint8_t*)frame;
    pkt->size = sizeof(*frame);
    pkt->flags |= AV_PKT_FLAG_KEY;
    pkt->flags |= AV_PKT_FLAG_TRUSTED;
    pkt->pos = pos;
    pkt->stream_index = VIDEO_STREAM_ID;
    pkt->pts = pkt->dts = sdi->frame_duration * (pos - sdi->header_size) / sdi->frame_size;
    pkt->duration = sdi->frame_duration;
    // The frame's audio goes out with the next call, at the frame's time
    sdi->audio_pts = pkt->pts;

    ret = take_audio(s, &audio);
    if (ret < 0) {
        av_packet_unref(pkt);
        return ret;
    }
    return 0;
}

/*
 * Clean up
 */
static int sdi_read_close(struct AVFormatContext *s)
{
    SDIDemuxContext *sdi = s->priv_data;

    DtSdiParser_Freep(&sdi->parser);
    DtSdiView_Freep(&sdi->view);
    av_buffer_pool_uninit(&sdi->image_pool);
    av_freep(&sdi->frame_buf);
    av_freep(&sdi->audio_buf);

    return 0;
}

/* Demux options */
static const AVOption options[] = {
    { "sdi_standard", "", offsetof(SDIDemuxContext, option_standard), AV_OPT_TYPE_STRING, {.str = ""}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, NULL},
    { "no_header", "", offsetof(SDIDemuxContext, option_no_header), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, AV_OPT_FLAG_DECODING_PARAM, NULL },
    { "threads", "threads a frame is taken apart over: auto, 1 for one, or more", offsetof(SDIDemuxContext, threads), AV_OPT_TYPE_INT, { .i64 = FF_SDI_THREADS_AUTO }, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM, "threads" },
    { "auto", "4 threads, and as many pieces as the standard calls for", 0, AV_OPT_TYPE_CONST, { .i64 = FF_SDI_THREADS_AUTO }, 0, 0, AV_OPT_FLAG_DECODING_PARAM, "threads" },

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
