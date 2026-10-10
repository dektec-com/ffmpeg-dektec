/*
 * SDI frames taken apart and put together with CDTAPI
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

#include <string.h>

#include "libavcodec/codec_id.h"
#include "libavutil/buffer.h"
#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libswscale/swscale.h"
#include "internal.h"
#include "sdiframe.h"
#include "sdipair.h"

/* The sample rate of SDI's embedded audio. */
#define SAMPLE_RATE 48000

/* The bytes of a sample the unpacker's audio stream carries. */
#define UNPACKED_SAMPLE_SIZE 3

/* The most places of an audio cadence: ten pictures of 3G level B at 59.94 Hz. */
#define MAX_CADENCE 10

/*
 * Start the worker pool that the threads option asks for. Sets *pool to NULL when the
 * option asks for one thread, and *num_threads to the threads a frame is divided over,
 * 0 for as many as its standard calls for.
 */
static int worker_pool(void *log_ctx, int threads, DtWorkerPool **pool, int *num_threads)
{
    int pool_threads = threads == FF_SDI_THREADS_AUTO ? FF_SDI_AUTO_THREADS : threads;
    DtapiResult result;

    *pool = NULL;
    *num_threads = 0;
    if (pool_threads < 2)
        return 0;

    *pool = DtWorkerPool_Alloc();
    if (!*pool)
        return AVERROR(ENOMEM);
    result = DtWorkerPool_StartThreads(*pool, pool_threads);
    if (result != DTAPI_OK) {
        av_log(log_ctx, AV_LOG_ERROR, "Could not start %d threads: %s\n", pool_threads,
               DtapiResult2Str(result));
        DtWorkerPool_Freep(pool);
        return AVERROR(ENOMEM);
    }
    *num_threads = threads == FF_SDI_THREADS_AUTO ? 0 : threads;
    return 0;
}

/* The places of the audio cadence of the standard vidstd, as builder puts the samples
 * in, and in offset, when not NULL, the samples of a cycle before each place. A rate
 * without a cadence, or with one of a single place, gives 0. */
static int audio_cadence(DtSdiBuilder *builder, int vidstd, int offset[MAX_CADENCE + 1])
{
    int length = 0;

    for (int place = 1, sum = 0; place <= MAX_CADENCE; place++) {
        int nb_samples = 0;

        if (DtSdiBuilder_GetNumAudioSamples(builder, vidstd, place, &nb_samples) !=
            DTAPI_OK)
            break;
        if (offset)
            offset[place] = sum;
        length = place;
        sum += nb_samples;
    }
    return length >= 2 ? length : 0;
}

/* The number of the first sample of frame number frame at frame_rate. At a 1001 rate it
 * is the sum of the samples of the cadence before. */
static int64_t frame_first_sample(AVRational frame_rate, int64_t frame)
{
    return av_rescale_rnd(frame, (int64_t)SAMPLE_RATE * frame_rate.den, frame_rate.num,
                          AV_ROUND_NEAR_INF);
}

/* ======================== Taking frames apart ======================== */

struct SdiUnpacker {
    void *log_ctx;
    const struct SdiInfo *info;
    int vidstd;                 ///< the standard's DTAPI_VIDSTD_ code
    int level_b;                ///< whether it is 3G level B, whose frames are pictures
    AVRational frame_rate;
    DtSdiView *view;            ///< the frame to take apart, which the caller points
    DtSdiParser *parser;

    int v210;                   ///< whether the images are v210 packets
    AVBufferPool *image_pool;   ///< the buffers of the images the parser writes
    int linesize[4];            ///< the linesizes of an image's planes; of v210, its lines
    size_t plane_size[3];       ///< the sizes of an image's planes; of v210, the image

    int32_t *audio_buf;         ///< a frame's samples, one channel after the other
    int max_samples;            ///< room for samples per channel in audio_buf
    int nb_channels;            ///< channels the frame parsed last carried
    int nb_samples;             ///< samples per channel of that frame, on every channel
    int present[FF_SDI_AUDIO_MAX_CHANNELS];  ///< whether that frame carried each channel
    int samples[FF_SDI_AUDIO_MAX_CHANNELS];  ///< and the samples it carried on it
    int64_t frame_number;       ///< that frame's number
    int cadence_place;          ///< and its place in the audio cadence, 0 for none

    int cadence_length;         ///< the places of the standard's cadence, 0 for none
    int cadence_offset[MAX_CADENCE + 1]; ///< the samples of a cycle before each place

    AVStream *audio_stream;     ///< the audio stream, or NULL
    int stream_channels;        ///< its channels
    int audio_waiting;          ///< whether the audio of the frame parsed last is to go out
    int64_t audio_pts;          ///< the number of that frame's first sample
    int warned_channels;        ///< whether a change in the number of channels was logged
};

static void free_frame(void *opaque, uint8_t *data)
{
    AVFrame *frame = (AVFrame *)data;

    av_frame_free(&frame);
}

/* The bytes of a v210 line of width pixels: six pixels in 16 bytes, in whole blocks of
 * 48 pixels, 128 bytes, as FFmpeg's v210 codec and CDTAPI have it. */
static int v210_stride(int width)
{
    return (width + 47) / 48 * 128;
}

int ff_sdi_unpacker_alloc(SdiUnpacker **unpacker, void *log_ctx,
                          const struct SdiInfo *info, int threads, int v210)
{
    SdiUnpacker *u;
    DtWorkerPool *pool = NULL;
    DtSdiBuilder *builder;
    int num_threads = 0;
    DtapiResult result;
    int ret;

    *unpacker = u = av_mallocz(sizeof(*u));
    if (!u)
        return AVERROR(ENOMEM);
    u->log_ctx = log_ctx;
    u->info = info;
    u->vidstd = av_sdi_vidstd(info);
    u->level_b = av_sdi_is_level_b(info);
    u->frame_rate = av_sdi_rate(info->picture_rate);

    u->view = DtSdiView_Alloc();
    u->parser = DtSdiParser_Alloc();
    if (!u->view || !u->parser)
        return AVERROR(ENOMEM);

    // The images come from a pool: a buffer the size of an image, allocated anew for
    // every frame, costs the system more than the parser takes to fill it.
    u->v210 = v210;
    if (v210) {
        u->linesize[0] = v210_stride(info->picture_width);
        u->plane_size[0] = (size_t)u->linesize[0] * info->picture_height;
        u->image_pool = av_buffer_pool_init(u->plane_size[0] + AV_INPUT_BUFFER_PADDING_SIZE,
                                            av_buffer_alloc);
    } else {
        ret = av_image_fill_linesizes(u->linesize, AV_PIX_FMT_YUV422P10LE,
                                      FFALIGN(info->picture_width, 64));
        if (ret < 0)
            return ret;
        for (int i = 0; i < 3; i++)
            u->plane_size[i] = (size_t)u->linesize[i] * info->picture_height;
        u->image_pool = av_buffer_pool_init(u->plane_size[0] + u->plane_size[1] +
                                            u->plane_size[2], av_buffer_alloc);
    }
    if (!u->image_pool)
        return AVERROR(ENOMEM);

    ret = worker_pool(log_ctx, threads, &pool, &num_threads);
    if (ret < 0)
        return ret;
    if (pool) {
        result = DtSdiParser_SetWorkerPool(u->parser, pool, num_threads);
        DtWorkerPool_Freep(&pool);
        if (result != DTAPI_OK) {
            av_log(log_ctx, AV_LOG_ERROR, "Could not give the parser its threads: %s\n",
                   DtapiResult2Str(result));
            return AVERROR(ENOMEM);
        }
    }

    // The samples of each place of the standard's audio cadence, as a builder puts
    // them in; a rate without a cadence has none
    builder = DtSdiBuilder_Alloc();
    if (!builder)
        return AVERROR(ENOMEM);
    u->cadence_length = audio_cadence(builder, u->vidstd, u->cadence_offset);
    DtSdiBuilder_Freep(&builder);

    // Room for the most samples a frame of the standard carries, on every channel
    result = DtSdiAudio_MaxSamples(u->vidstd, &u->max_samples);
    if (result != DTAPI_OK)
        return AVERROR_INVALIDDATA;
    u->audio_buf = av_calloc((size_t)FF_SDI_AUDIO_MAX_CHANNELS * u->max_samples,
                             sizeof(*u->audio_buf));
    if (!u->audio_buf)
        return AVERROR(ENOMEM);
    return 0;
}

void ff_sdi_unpacker_free(SdiUnpacker **unpacker)
{
    SdiUnpacker *u = *unpacker;

    if (!u)
        return;
    DtSdiParser_Freep(&u->parser);
    DtSdiView_Freep(&u->view);
    av_buffer_pool_uninit(&u->image_pool);
    av_freep(&u->audio_buf);
    av_freep(unpacker);
}

DtSdiView *ff_sdi_unpacker_view(SdiUnpacker *unpacker)
{
    return unpacker->view;
}

int ff_sdi_unpacker_add_video_stream(SdiUnpacker *unpacker, AVFormatContext *s,
                                     int64_t nb_frames)
{
    const struct SdiInfo *info = unpacker->info;
    AVStream *st = avformat_new_stream(s, NULL);

    if (!st)
        return AVERROR(ENOMEM);
    st->id = 0;
    // The images count in frames, which every rate gives exactly
    avpriv_set_pts_info(st, 64, unpacker->frame_rate.den, unpacker->frame_rate.num);
    st->avg_frame_rate = unpacker->frame_rate;
    st->r_frame_rate = unpacker->frame_rate;
    st->nb_frames = nb_frames;
    st->duration = nb_frames;
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = unpacker->v210 ? AV_CODEC_ID_V210 : AV_CODEC_ID_WRAPPED_AVFRAME;
    st->codecpar->format = AV_PIX_FMT_YUV422P10;
    if (unpacker->v210)
        st->codecpar->bit_rate = av_rescale(unpacker->plane_size[0] * 8,
                                            unpacker->frame_rate.num,
                                            unpacker->frame_rate.den);
    st->codecpar->width = info->picture_width;
    st->codecpar->height = info->picture_height;
    // An interlaced picture, carried in two fields or in one, has its top field first
    st->codecpar->field_order = info->scanning_method == SDI_I_PICT_I_TR ||
                                info->scanning_method == SDI_I_PICT_P_TR
                                    ? AV_FIELD_TT : AV_FIELD_PROGRESSIVE;
    return 0;
}

/*
 * Ready the audio of the frame parsed last for the audio stream: the samples of the
 * stream's channels, those of a channel the frame carried fewer samples on, or none,
 * silent.
 */
static void prepare_audio(SdiUnpacker *u)
{
    u->audio_waiting = 0;
    u->nb_samples = 0;
    if (!u->audio_stream || u->nb_channels == 0)
        return;

    if (u->nb_channels != u->stream_channels && !u->warned_channels) {
        av_log(u->log_ctx, AV_LOG_WARNING, "The frames now carry %d audio channels, the "
               "stream keeps %d\n", u->nb_channels, u->stream_channels);
        u->warned_channels = 1;
    }
    for (int ch = 0; ch < u->stream_channels; ch++) {
        if (u->present[ch])
            u->nb_samples = FFMAX(u->nb_samples, u->samples[ch]);
    }
    for (int ch = 0; ch < u->stream_channels; ch++) {
        int32_t *samples = u->audio_buf + (size_t)ch * u->max_samples;
        int from = u->present[ch] ? FFMIN(u->samples[ch], u->nb_samples) : 0;

        memset(samples + from, 0, (size_t)(u->nb_samples - from) * sizeof(*samples));
    }
    u->audio_waiting = u->nb_samples > 0;
    if (u->cadence_place > 0 && u->cadence_place <= u->cadence_length) {
        // A frame of a 1001 rate carries its place in the cadence, and the samples come
        // after those of the places before it in the cycle. So the timestamps follow
        // what the frames carry, from whichever place the frames start.
        int64_t start = u->frame_number - (u->cadence_place - 1);

        u->audio_pts = frame_first_sample(u->frame_rate, start) +
                       u->cadence_offset[u->cadence_place];
    } else if (u->level_b) {
        // The interface's frames, of half the picture rate, follow the cadence, and its
        // two pictures share a frame's samples unequally: field 1 starts with the frame,
        // and field 2 ends with it.
        AVRational interface_rate = { u->frame_rate.num, u->frame_rate.den * 2 };
        int64_t frame = u->frame_number / 2;

        if (u->frame_number % 2 == 0)
            u->audio_pts = frame_first_sample(interface_rate, frame);
        else
            u->audio_pts = frame_first_sample(interface_rate, frame + 1) - u->nb_samples;
    } else {
        u->audio_pts = frame_first_sample(u->frame_rate, u->frame_number);
    }
}

int ff_sdi_unpacker_add_audio_stream(SdiUnpacker *unpacker, AVFormatContext *s,
                                     int nb_channels)
{
    AVStream *st = avformat_new_stream(s, NULL);

    if (!st)
        return AVERROR(ENOMEM);
    st->id = 1;
    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id = AV_CODEC_ID_PCM_S24LE;
    st->codecpar->ch_layout.nb_channels = nb_channels;
    st->codecpar->sample_rate = SAMPLE_RATE;
    st->codecpar->format = AV_SAMPLE_FMT_S32;
    st->codecpar->bits_per_coded_sample = 24;
    st->codecpar->bits_per_raw_sample = 24;
    st->codecpar->block_align = UNPACKED_SAMPLE_SIZE * nb_channels;
    st->codecpar->bit_rate = (int64_t)SAMPLE_RATE * 24 * nb_channels;
    // The audio counts in samples
    avpriv_set_pts_info(st, 64, 1, SAMPLE_RATE);
    unpacker->audio_stream = st;
    unpacker->stream_channels = nb_channels;
    prepare_audio(unpacker);
    return 0;
}

int ff_sdi_unpacker_parse(SdiUnpacker *unpacker, int64_t frame_number, AVPacket *pkt)
{
    SdiUnpacker *u = unpacker;
    const struct SdiInfo *info = u->info;
    DtSdiImage image = { 0 };
    DtSdiAudio audio = { 0 };
    DtapiResult result;
    AVFrame *frame;

    image.Fields = DT_SDI_FIELDS_WOVEN;
    for (int pair = 0; pair < FF_SDI_AUDIO_MAX_CHANNELS / 2; pair++)
        audio.Formats[pair] = DT_SDI_AUDIO_PCM;
    for (int ch = 0; ch < FF_SDI_AUDIO_MAX_CHANNELS; ch++) {
        audio.Channels[ch].Samples = u->audio_buf + (size_t)ch * u->max_samples;
        audio.Channels[ch].MaxSamples = u->max_samples;
    }

    if (u->v210) {
        // The image is the packet
        pkt->buf = av_buffer_pool_get(u->image_pool);
        if (!pkt->buf)
            return AVERROR(ENOMEM);
        pkt->data = pkt->buf->data;
        pkt->size = (int)u->plane_size[0];
        memset(pkt->data + pkt->size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        image.Format = DT_SDI_PIXFMT_V210;
        image.Planes[0] = pkt->data;
        image.Strides[0] = u->linesize[0];
        result = DtSdiParser_Parse(u->parser, u->view, &image, &audio, NULL);
        if (result != DTAPI_OK) {
            av_log(u->log_ctx, AV_LOG_ERROR, "Could not take the frame apart: %s\n",
                   DtapiResult2Str(result));
            av_packet_unref(pkt);
            return AVERROR_INVALIDDATA;
        }
        goto image_done;
    }

    frame = av_frame_alloc();
    if (!frame)
        return AVERROR(ENOMEM);
    frame->format = AV_PIX_FMT_YUV422P10LE;
    frame->width = info->picture_width;
    frame->height = info->picture_height;
    frame->buf[0] = av_buffer_pool_get(u->image_pool);
    if (!frame->buf[0]) {
        av_frame_free(&frame);
        return AVERROR(ENOMEM);
    }
    frame->data[0] = frame->buf[0]->data;
    frame->data[1] = frame->data[0] + u->plane_size[0];
    frame->data[2] = frame->data[1] + u->plane_size[1];
    for (int i = 0; i < 3; i++)
        frame->linesize[i] = u->linesize[i];
    frame->extended_data = frame->data;

    image.Format = DT_SDI_PIXFMT_YUV422P_10B;
    for (int i = 0; i < 3; i++) {
        image.Planes[i] = frame->data[i];
        image.Strides[i] = frame->linesize[i];
    }

    // PCM on every channel, each into its own part of audio_buf
    result = DtSdiParser_Parse(u->parser, u->view, &image, &audio, NULL);
    if (result != DTAPI_OK) {
        av_log(u->log_ctx, AV_LOG_ERROR, "Could not take the frame apart: %s\n",
               DtapiResult2Str(result));
        av_frame_free(&frame);
        return AVERROR_INVALIDDATA;
    }

    pkt->buf = av_buffer_create((uint8_t *)frame, sizeof(*frame), free_frame, NULL, 0);
    if (!pkt->buf) {
        av_frame_free(&frame);
        return AVERROR(ENOMEM);
    }
    pkt->data = (uint8_t *)frame;
    pkt->size = sizeof(*frame);
    pkt->flags |= AV_PKT_FLAG_TRUSTED;

image_done:
    pkt->flags |= AV_PKT_FLAG_KEY;
    pkt->stream_index = 0;
    pkt->pts = pkt->dts = frame_number;
    pkt->duration = 1;

    // The channels up to the last with valid audio
    u->nb_channels = 0;
    for (int ch = 0; ch < FF_SDI_AUDIO_MAX_CHANNELS; ch++) {
        const DtSdiAudioChannel *channel = &audio.Channels[ch];
        u->present[ch] = channel->Present;
        u->samples[ch] = channel->NumSamples;
        if (channel->Present && !channel->Invalid)
            u->nb_channels = ch + 1;
    }
    u->frame_number = frame_number;
    u->cadence_place = audio.FrameNumber;
    prepare_audio(u);
    return 0;
}

int ff_sdi_unpacker_nb_channels(const SdiUnpacker *unpacker)
{
    return unpacker->nb_channels;
}

int ff_sdi_unpacker_audio(SdiUnpacker *unpacker, AVPacket *pkt)
{
    SdiUnpacker *u = unpacker;
    int nb_channels = u->stream_channels;
    uint8_t *ptr;
    int ret;

    if (!u->audio_waiting)
        return 0;
    u->audio_waiting = 0;
    ret = av_new_packet(pkt, u->nb_samples * nb_channels * UNPACKED_SAMPLE_SIZE);
    if (ret < 0)
        return ret;
    pkt->stream_index = u->audio_stream->index;
    pkt->pts = pkt->dts = u->audio_pts;
    pkt->duration = u->nb_samples;

    // The parser gives a sample its 24 bits at the top of 32
    ptr = pkt->data;
    for (int i = 0; i < u->nb_samples; i++) {
        for (int ch = 0; ch < nb_channels; ch++) {
            AV_WL24(ptr, (uint32_t)u->audio_buf[(size_t)ch * u->max_samples + i] >> 8);
            ptr += UNPACKED_SAMPLE_SIZE;
        }
    }
    return 1;
}

/* ======================== Putting frames together ======================== */

struct SdiPacker {
    void *log_ctx;
    const struct SdiInfo *info;
    int vidstd;                 ///< the standard's DTAPI_VIDSTD_ code
    size_t frame_size;          ///< a frame of the standard
    DtSdiView *view;            ///< the room to build in, which the caller points
    DtSdiBuilder *builder;

    AVStream *video_stream;
    int v210;                   ///< whether the images are v210 packets
    struct SwsContext *scale_context; ///< scales the images to the standard's, or NULL
    AVFrame *scale_frame;             ///< the scaled image

    AVStream *audio_stream;     ///< the audio stream, or NULL
    int stream_channels;        ///< its channels
    int nb_channels;            ///< channels the frames carry
    int bytes_per_sample;       ///< of the audio stream: 3 or 4
    int big_endian;             ///< whether its samples are big-endian
    uint8_t *audio_in;          ///< a frame's samples, as the audio stream has them
    int32_t *audio_buf;         ///< the same, 24 bits at the top of 32, interleaved

    SdiPair *pair;              ///< pairs the images and the audio into frames
    AVPacket *image_pkt;        ///< the image of the frame being built

    int cadence_length;         ///< the places of the standard's audio cadence, or 0
    int place;                  ///< the next frame's place in it, 0 for the builder's own
};

/* The builder's format of an FFmpeg pixel format it takes as it is, or
 * DT_SDI_PIXFMT_NONE. */
static DtSdiPixelFormat builder_format(int format)
{
    switch (format) {
    case AV_PIX_FMT_YUV422P10LE: return DT_SDI_PIXFMT_YUV422P_10B;
    case AV_PIX_FMT_Y210LE:      return DT_SDI_PIXFMT_Y210;
    case AV_PIX_FMT_YUV422P:     return DT_SDI_PIXFMT_YUV422P_8B;
    case AV_PIX_FMT_UYVY422:     return DT_SDI_PIXFMT_UYVY_8B;
    default:                     return DT_SDI_PIXFMT_NONE;
    }
}

/*
 * Set up scaling to the standard's image when the stream's differs in size, or is in a
 * format the builder does not take. v210 is not scaled: it must be of the standard's
 * size.
 */
static int init_scaling(SdiPacker *p)
{
    const AVCodecParameters *par = p->video_stream->codecpar;
    int dst_width = p->info->picture_width;
    int dst_height = p->info->picture_height;
    int dst_format = AV_PIX_FMT_YUV422P10LE;
    int ret;

    if (par->codec_id == AV_CODEC_ID_V210) {
        if (par->width != dst_width || par->height != dst_height) {
            av_log(p->log_ctx, AV_LOG_ERROR, "v210 of %dx%d is not the %dx%d of %s\n",
                   par->width, par->height, dst_width, dst_height, p->info->name);
            return AVERROR(EINVAL);
        }
        p->v210 = 1;
        return 0;
    }
    if (par->codec_id != AV_CODEC_ID_WRAPPED_AVFRAME) {
        av_log(p->log_ctx, AV_LOG_ERROR, "SDI takes video as wrapped_avframe or v210, "
               "not %s\n", avcodec_get_name(par->codec_id));
        return AVERROR(EINVAL);
    }
    if (par->width == dst_width && par->height == dst_height &&
        builder_format(par->format) != DT_SDI_PIXFMT_NONE)
        return 0;

    p->scale_context = sws_getContext(par->width, par->height, par->format, dst_width,
                                      dst_height, dst_format, SWS_BICUBIC, NULL, NULL,
                                      NULL);
    p->scale_frame = av_frame_alloc();
    if (!p->scale_context || !p->scale_frame) {
        av_log(p->log_ctx, AV_LOG_ERROR, "Cannot initialize the swscale context\n");
        return AVERROR(EINVAL);
    }
    p->scale_frame->format = dst_format;
    p->scale_frame->width = dst_width;
    p->scale_frame->height = dst_height;
    ret = av_frame_get_buffer(p->scale_frame, 0);
    if (ret < 0)
        return ret;
    av_log(p->log_ctx, AV_LOG_DEBUG, "Scale from (%dx%d, %s) to (%dx%d, %s)\n",
           par->width, par->height, av_get_pix_fmt_name(par->format), dst_width,
           dst_height, av_get_pix_fmt_name(dst_format));
    return 0;
}

/*
 * Set up the audio the frames carry: the channels of the audio stream, or nb_channels,
 * the ones the stream lacks silent.
 */
static int init_audio(SdiPacker *p, int nb_channels)
{
    const AVCodecParameters *par = p->audio_stream->codecpar;
    int max_samples = 0;
    DtapiResult result;

    if (par->codec_id != AV_CODEC_ID_PCM_S24LE && par->codec_id != AV_CODEC_ID_PCM_S24BE &&
        par->codec_id != AV_CODEC_ID_PCM_S32LE) {
        av_log(p->log_ctx, AV_LOG_ERROR, "Unsupported audio codec %s\n",
               avcodec_get_name(par->codec_id));
        return AVERROR(EINVAL);
    }
    if (par->sample_rate != SAMPLE_RATE) {
        av_log(p->log_ctx, AV_LOG_ERROR, "Unsupported sample rate %d\n", par->sample_rate);
        return AVERROR(EINVAL);
    }

    p->stream_channels = par->ch_layout.nb_channels;
    p->nb_channels = nb_channels == -1 ? p->stream_channels : nb_channels;
    if (p->nb_channels < 1 || p->nb_channels > FF_SDI_AUDIO_MAX_CHANNELS) {
        av_log(p->log_ctx, AV_LOG_ERROR, "SDI carries 1 to %d audio channels, not %d\n",
               FF_SDI_AUDIO_MAX_CHANNELS, p->nb_channels);
        return AVERROR(EINVAL);
    }
    p->bytes_per_sample = av_get_bits_per_sample(par->codec_id) / 8;
    p->big_endian = par->codec_id == AV_CODEC_ID_PCM_S24BE;

    result = DtSdiAudio_MaxSamples(p->vidstd, &max_samples);
    if (result != DTAPI_OK)
        return AVERROR(EINVAL);
    p->audio_in = av_malloc((size_t)max_samples * p->stream_channels * p->bytes_per_sample);
    p->audio_buf = av_malloc_array((size_t)max_samples * p->nb_channels,
                                   sizeof(*p->audio_buf));
    if (!p->audio_in || !p->audio_buf)
        return AVERROR(ENOMEM);
    return 0;
}

int ff_sdi_packer_alloc(SdiPacker **packer, void *log_ctx, const struct SdiInfo *info,
                        int threads, int checksums, AVStream *video_stream,
                        AVStream *audio_stream, int nb_channels)
{
    SdiPacker *p;
    DtWorkerPool *pool = NULL;
    int num_threads = 0;
    DtapiResult result;
    int ret;

    *packer = p = av_mallocz(sizeof(*p));
    if (!p)
        return AVERROR(ENOMEM);
    p->log_ctx = log_ctx;
    p->info = info;
    p->vidstd = av_sdi_vidstd(info);
    p->video_stream = video_stream;
    p->audio_stream = audio_stream;

    result = DtSdiView_RawFrameSize(p->vidstd, 10, &p->frame_size);
    if (result != DTAPI_OK) {
        av_log(log_ctx, AV_LOG_ERROR, "CDTAPI does not build frames of %s: %s\n",
               info->name, DtapiResult2Str(result));
        return AVERROR(EINVAL);
    }
    p->view = DtSdiView_Alloc();
    p->builder = DtSdiBuilder_Alloc();
    p->image_pkt = av_packet_alloc();
    if (!p->view || !p->builder || !p->image_pkt)
        return AVERROR(ENOMEM);
    if (DtSdiBuilder_SetChecksums(p->builder, checksums != 0) != DTAPI_OK)
        return AVERROR(EINVAL);

    ret = worker_pool(log_ctx, threads, &pool, &num_threads);
    if (ret < 0)
        return ret;
    if (pool) {
        result = DtSdiBuilder_SetWorkerPool(p->builder, pool, num_threads);
        DtWorkerPool_Freep(&pool);
        if (result != DTAPI_OK) {
            av_log(log_ctx, AV_LOG_ERROR, "Could not give the builder its threads: %s\n",
                   DtapiResult2Str(result));
            return AVERROR(ENOMEM);
        }
    }

    ret = init_scaling(p);
    if (ret < 0)
        return ret;
    if (audio_stream) {
        ret = init_audio(p, nb_channels);
        if (ret < 0)
            return ret;
    }
    p->pair = ff_sdi_pair_alloc(av_sdi_rate(info->picture_rate), audio_stream ?
                                p->stream_channels * p->bytes_per_sample : 0);
    if (!p->pair)
        return AVERROR(ENOMEM);
    return 0;
}

void ff_sdi_packer_free(SdiPacker **packer)
{
    SdiPacker *p = *packer;

    if (!p)
        return;
    DtSdiBuilder_Freep(&p->builder);
    DtSdiView_Freep(&p->view);
    sws_freeContext(p->scale_context);
    av_frame_free(&p->scale_frame);
    av_freep(&p->audio_in);
    av_freep(&p->audio_buf);
    ff_sdi_pair_free(&p->pair);
    av_packet_free(&p->image_pkt);
    av_freep(packer);
}

DtSdiView *ff_sdi_packer_view(SdiPacker *packer)
{
    return packer->view;
}

size_t ff_sdi_packer_frame_size(const SdiPacker *packer)
{
    return packer->frame_size;
}

void ff_sdi_packer_count_places(SdiPacker *packer)
{
    packer->cadence_length = audio_cadence(packer->builder, packer->vidstd, NULL);
    packer->place = packer->cadence_length > 0;
}

/* The next frame takes the next place of the cadence, when the packer counts them. */
static void next_place(SdiPacker *p)
{
    if (p->place)
        p->place = p->place % p->cadence_length + 1;
}

int ff_sdi_packer_add(SdiPacker *packer, const AVPacket *pkt)
{
    AVStream *video = packer->video_stream;
    AVStream *audio = packer->audio_stream;

    if (pkt->stream_index == video->index)
        return ff_sdi_pair_add_video(packer->pair, pkt, video->time_base);
    if (audio && pkt->stream_index == audio->index)
        return ff_sdi_pair_add_audio(packer->pair, pkt, audio->time_base);
    return 0;
}

/* The samples per channel the builder's next frame takes, which follows the cadence at
 * a 1001 rate; 0 without audio. */
static int next_nb_samples(SdiPacker *p, int *nb_samples)
{
    *nb_samples = 0;
    if (!p->audio_stream)
        return 0;
    if (DtSdiBuilder_GetNumAudioSamples(p->builder, p->vidstd, p->place, nb_samples) !=
        DTAPI_OK)
        return AVERROR(EINVAL);
    return 0;
}

int ff_sdi_packer_ready(SdiPacker *packer, int flush)
{
    int nb_samples;
    int ret = next_nb_samples(packer, &nb_samples);

    if (ret < 0)
        return ret;
    return ff_sdi_pair_ready(packer->pair, nb_samples, flush);
}

/*
 * Convert the nb_samples samples in audio_in, as the audio stream has them, into
 * audio_buf, and point audio at them for the builder. Channels the stream does not have
 * are silent.
 */
static void convert_audio(SdiPacker *p, DtSdiAudio *audio, int nb_samples)
{
    int bps = p->bytes_per_sample;
    int stride = p->stream_channels * bps;

    // The builder takes 24 bits at the top of 32
    for (int i = 0; i < nb_samples; i++) {
        const uint8_t *in = p->audio_in + (size_t)i * stride;
        int32_t *out = p->audio_buf + (size_t)i * p->nb_channels;
        for (int ch = 0; ch < p->nb_channels; ch++) {
            if (ch >= p->stream_channels)
                out[ch] = 0;
            else if (bps == 3)
                out[ch] = (int32_t)((p->big_endian ? AV_RB24(in + ch * bps)
                                                   : AV_RL24(in + ch * bps)) << 8);
            else
                out[ch] = (int32_t)AV_RL32(in + ch * bps);
        }
    }
    for (int ch = 0; ch < p->nb_channels; ch++) {
        audio->Formats[ch / 2] = DT_SDI_AUDIO_PCM;
        audio->Channels[ch].Samples = p->audio_buf + ch;
        audio->Channels[ch].Stride = p->nb_channels;
        audio->Channels[ch].NumSamples = nb_samples;
    }
}

int ff_sdi_packer_build(SdiPacker *packer)
{
    SdiPacker *p = packer;
    DtSdiImage image = { 0 };
    DtSdiAudio audio = { 0 };
    const AVFrame *frame;
    DtapiResult result;
    int nb_samples;
    int ret = next_nb_samples(p, &nb_samples);

    if (ret < 0)
        return ret;
    ret = ff_sdi_pair_take(p->pair, p->image_pkt, p->audio_in, nb_samples);
    if (ret < 0)
        return ret;

    image.Fields = DT_SDI_FIELDS_WOVEN;
    if (p->v210) {
        int stride = v210_stride(p->info->picture_width);

        image.Format = DT_SDI_PIXFMT_V210;
        image.Planes[0] = p->image_pkt->data;
        image.Strides[0] = stride;
        if (p->image_pkt->size < (int64_t)stride * p->info->picture_height) {
            av_log(p->log_ctx, AV_LOG_ERROR, "A v210 image of %d bytes, %d expected\n",
                   p->image_pkt->size, stride * p->info->picture_height);
            ret = AVERROR_INVALIDDATA;
        }
    } else {
        frame = (const AVFrame *)p->image_pkt->data;
        if (p->scale_context) {
            ret = sws_scale_frame(p->scale_context, p->scale_frame, frame);
            frame = p->scale_frame;
        }
        image.Format = builder_format(frame->format);
        if (ret >= 0 && (image.Format == DT_SDI_PIXFMT_NONE ||
                         frame->width != p->info->picture_width ||
                         frame->height != p->info->picture_height)) {
            av_log(p->log_ctx, AV_LOG_ERROR, "An image of %dx%d %s, where the stream "
                   "has %dx%d %s\n", frame->width, frame->height,
                   av_get_pix_fmt_name(frame->format), p->video_stream->codecpar->width,
                   p->video_stream->codecpar->height,
                   av_get_pix_fmt_name(p->video_stream->codecpar->format));
            ret = AVERROR(EINVAL);
        }
        for (int i = 0; i < 3; i++) {
            image.Planes[i] = frame->data[i];
            image.Strides[i] = frame->linesize[i];
        }
    }
    if (ret >= 0) {
        if (p->audio_stream) {
            convert_audio(p, &audio, nb_samples);
            audio.FrameNumber = p->place;
        }
        result = DtSdiBuilder_Build(p->builder, p->view, &image,
                                    p->audio_stream ? &audio : NULL, NULL);
        if (result != DTAPI_OK) {
            av_log(p->log_ctx, AV_LOG_ERROR, "Could not build the frame: %s\n",
                   DtapiResult2Str(result));
            ret = AVERROR(EINVAL);
        }
    }
    av_packet_unref(p->image_pkt);
    next_place(p);
    if (ret >= 0)
        return 0;

    // The room is filled all the same, black and silent
    DtSdiBuilder_Build(p->builder, p->view, NULL, NULL, NULL);
    return ret;
}

int ff_sdi_packer_build_black(SdiPacker *packer)
{
    DtapiResult result = DtSdiBuilder_Build(packer->builder, packer->view, NULL, NULL,
                                            NULL);

    next_place(packer);
    if (result != DTAPI_OK) {
        av_log(packer->log_ctx, AV_LOG_ERROR, "Could not build a black frame: %s\n",
               DtapiResult2Str(result));
        return AVERROR(EINVAL);
    }
    return 0;
}
