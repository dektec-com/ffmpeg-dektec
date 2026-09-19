/*
 * SDI common
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
 * @file sdicommon.c
 * SDI common
 * @author Werner Damman
 * @author Jeroen Steendam
 */

#include "sdicommon.h"
#include "packet_internal.h"
#include "libavutil/fifo.h"
#include "libavutil/mem.h"
#include "libavutil/rational.h"

int is_interlaced_transport(SdiScanningMethod scanning_method)
{
    return (scanning_method == SDI_I_PICT_I_TR ||
            scanning_method == SDI_P_PICT_I_TR) ? 1 : 0;
}

int is_interlaced_picture(SdiScanningMethod scanning_method)
{
    return (scanning_method == SDI_I_PICT_I_TR ||
            scanning_method == SDI_I_PICT_P_TR) ? 1 : 0;
}

/**
 * Converts from SdiPictureRate to AVRational
 */
static const AVRational sdi_rate_table[12] = {
        {     0,    0 },    // dummy
        {     0,    0 },    // dummy
        { 24000, 1001 },
        {    24,    1 },
        { 50000, 1001 },
        {    25,    1 },
        { 30000, 1001 },
        {    30,    1 },
        {    48,    1 },
        {    50,    1 },
        { 60000, 1001 },
        {    60,    1 }
};

/*
 * Converts from SdiAspectRatio to AVrational
 */
static const AVRational ff_sdi_ar_table[2] = {
        { 4, 3 },
        { 16, 9}
};


static const char *ff_scanning_method_names[4] = {
    "i",
    "psf",
    "...",
    "p"
};

static const char *ff_line_rate_names[SDI_LINE_RATE_NB] = {
    [SDI_LINE_RATE_SD]  = "SD",
    [SDI_LINE_RATE_ED]  = "ED",
    [SDI_LINE_RATE_HD]  = "HD",
    [SDI_LINE_RATE_3G]  = "3G",
    [SDI_LINE_RATE_6G]  = "6G",
    [SDI_LINE_RATE_12G] = "12G",
    [SDI_LINE_RATE_24G] = "24G",
};

/*
 * Magic for SDI files
 */
const int SDI_FILE_SIGNATURE = 0x6964732e;

/*
 * SDI standard 'knowledge database'.
 *
 * This table contains all supported SDI configurations and its specific
 * parameters, as gathered from the SMPTE standards.
 *
 * - First sorted on resolutions (ascending)
 * - Then first progressive, then interlaced
 */
static const struct SdiInfo sdi_table[SDI_FMT_NB] = {
    //                        name            fmt   scanning         rate         aspect       w     h     sdil  hanc  vanc      fs1 fs2  fe1   fe2   vs1 vs2  ve1   ve2   sw1 sw2  pi1 pi2  er1 er2
    [SDI_FMT_625I50]       = {"625i50",       0x81, SDI_I_PICT_I_TR, SDI_R_25,    SDI_AR_4_3,  720,  576,  625,  288,  2 * 720,  1,  313, 312,  625,  23, 336, 310,  623,  6,  319, 9,  322, 5,  318},
    [SDI_FMT_525I59_94]    = {"525i59.94",    0x81, SDI_I_PICT_I_TR, SDI_R_29_97, SDI_AR_4_3,  720,  487,  525,  276,  2 * 720,  1,  263, 262,  525,  17, 280, 260,  522,  7,  270, 13, 276, 9,  272},

    [SDI_FMT_720P23_98]    = {"720p23.98",    0x84, SDI_P_PICT_P_TR, SDI_R_23_98, SDI_AR_16_9, 1280, 720,  750,  5690, 2 * 1280, 1,  -1,  750,  -1,   26, -1,  745,  -1,   7,  -1,  10, -1,  -1, -1},
    [SDI_FMT_720P24]       = {"720p24",       0x84, SDI_P_PICT_P_TR, SDI_R_24,    SDI_AR_16_9, 1280, 720,  750,  5690, 2 * 1280, 1,  -1,  750,  -1,   26, -1,  745,  -1,   7,  -1,  10, -1,  -1, -1},
    [SDI_FMT_720P25]       = {"720p25",       0x84, SDI_P_PICT_P_TR, SDI_R_25,    SDI_AR_16_9, 1280, 720,  750,  5360, 2 * 1280, 1,  -1,  750,  -1,   26, -1,  745,  -1,   7,  -1,  10, -1,  -1, -1},
    [SDI_FMT_720P29_97]    = {"720p29.97",    0x84, SDI_P_PICT_P_TR, SDI_R_29_97, SDI_AR_16_9, 1280, 720,  750,  4040, 2 * 1280, 1,  -1,  750,  -1,   26, -1,  745,  -1,   7,  -1,  10, -1,  -1, -1},
    [SDI_FMT_720P30]       = {"720p30",       0x84, SDI_P_PICT_P_TR, SDI_R_30,    SDI_AR_16_9, 1280, 720,  750,  4040, 2 * 1280, 1,  -1,  750,  -1,   26, -1,  745,  -1,   7,  -1,  10, -1,  -1, -1},
    [SDI_FMT_720P50]       = {"720p50",       0x84, SDI_P_PICT_P_TR, SDI_R_50,    SDI_AR_16_9, 1280, 720,  750,  1400, 2 * 1280, 1,  -1,  750,  -1,   26, -1,  745,  -1,   7,  -1,  10, -1,  -1, -1},
    [SDI_FMT_720P59_94]    = {"720p59.94",    0x84, SDI_P_PICT_P_TR, SDI_R_59_94, SDI_AR_16_9, 1280, 720,  750,  740,  2 * 1280, 1,  -1,  750,  -1,   26, -1,  745,  -1,   7,  -1,  10, -1,  -1, -1},
    [SDI_FMT_720P60]       = {"720p60",       0x84, SDI_P_PICT_P_TR, SDI_R_60,    SDI_AR_16_9, 1280, 720,  750,  740,  2 * 1280, 1,  -1,  750,  -1,   26, -1,  745,  -1,   7,  -1,  10, -1,  -1, -1},

    [SDI_FMT_1080P23_98]   = {"1080p23.98",   0x85, SDI_P_PICT_P_TR, SDI_R_23_98, SDI_AR_16_9, 1920, 1080, 1125, 1660, 2 * 1920, 1,  -1,  1125, -1,   42, -1,  1121, -1,   7,  -1,  10, -1,  -1, -1},
    [SDI_FMT_1080P24]      = {"1080p24",      0x85, SDI_P_PICT_P_TR, SDI_R_24,    SDI_AR_16_9, 1920, 1080, 1125, 1660, 2 * 1920, 1,  -1,  1125, -1,   42, -1,  1121, -1,   7,  -1,  10, -1,  -1, -1},
    [SDI_FMT_1080P25]      = {"1080p25",      0x85, SDI_P_PICT_P_TR, SDI_R_25,    SDI_AR_16_9, 1920, 1080, 1125, 1440, 2 * 1920, 1,  -1,  1125, -1,   42, -1,  1121, -1,   7,  -1,  10, -1,  -1, -1},
    [SDI_FMT_1080P29_97]   = {"1080p29.97",   0x85, SDI_P_PICT_P_TR, SDI_R_29_97, SDI_AR_16_9, 1920, 1080, 1125, 560,  2 * 1920, 1,  -1,  1125, -1,   42, -1,  1121, -1,   7,  -1,  10, -1,  -1, -1},
    [SDI_FMT_1080P30]      = {"1080p30",      0x85, SDI_P_PICT_P_TR, SDI_R_30,    SDI_AR_16_9, 1920, 1080, 1125, 560,  2 * 1920, 1,  -1,  1125, -1,   42, -1,  1121, -1,   7,  -1,  10, -1,  -1, -1},
    [SDI_FMT_1080I50]      = {"1080i50",      0x85, SDI_I_PICT_I_TR, SDI_R_25,    SDI_AR_16_9, 1920, 1080, 1125, 1440, 2 * 1920, 1,  564, 563,  1125, 21, 584, 560,  1123, 7,  569, 10, 572, -1, -1},
    [SDI_FMT_1080I59_94]   = {"1080i59.94",   0x85, SDI_I_PICT_I_TR, SDI_R_29_97, SDI_AR_16_9, 1920, 1080, 1125, 560,  2 * 1920, 1,  564, 563,  1125, 21, 584, 560,  1123, 7,  569, 10, 572, -1, -1},
    [SDI_FMT_1080I60]      = {"1080i60",      0x85, SDI_I_PICT_I_TR, SDI_R_30,    SDI_AR_16_9, 1920, 1080, 1125, 560,  2 * 1920, 1,  564, 563,  1125, 21, 584, 560,  1123, 7,  569, 10, 572, -1, -1},
    [SDI_FMT_1080PSF23_98] = {"1080psf23.98", 0x85, SDI_P_PICT_I_TR, SDI_R_23_98, SDI_AR_16_9, 1920, 1080, 1125, 1660, 2 * 1920, 1,  564, 563,  1125, 21, 584, 560,  1123, 7,  569, 10, 572, -1, -1},
    [SDI_FMT_1080PSF24]    = {"1080psf24",    0x85, SDI_P_PICT_I_TR, SDI_R_24,    SDI_AR_16_9, 1920, 1080, 1125, 1660, 2 * 1920, 1,  564, 563,  1125, 21, 584, 560,  1123, 7,  569, 10, 572, -1, -1},
    [SDI_FMT_1080PSF25]    = {"1080psf25",    0x85, SDI_P_PICT_I_TR, SDI_R_25,    SDI_AR_16_9, 1920, 1080, 1125, 1440, 2 * 1920, 1,  564, 563,  1125, 21, 584, 560,  1123, 7,  569, 10, 572, -1, -1},
    [SDI_FMT_1080PSF29_97] = {"1080psf29.97", 0x85, SDI_P_PICT_I_TR, SDI_R_29_97, SDI_AR_16_9, 1920, 1080, 1125, 560,  2 * 1920, 1,  564, 563,  1125, 21, 584, 560,  1123, 7,  569, 10, 572, -1, -1},
    [SDI_FMT_1080PSF30]    = {"1080psf30",    0x85, SDI_P_PICT_I_TR, SDI_R_30,    SDI_AR_16_9, 1920, 1080, 1125, 560,  2 * 1920, 1,  564, 563,  1125, 21, 584, 560,  1123, 7,  569, 10, 572, -1, -1},

    [SDI_FMT_1080P50]      = {"1080p50",      0x89, SDI_P_PICT_P_TR, SDI_R_50,    SDI_AR_16_9, 1920, 1080, 1125, 1440, 2 * 1920, 1,  -1,  1125, -1,   42, -1,  1121, -1,   7,  -1,  10, -1,  -1, -1},
    [SDI_FMT_1080P59_94]   = {"1080p59.94",   0x89, SDI_P_PICT_P_TR, SDI_R_59_94, SDI_AR_16_9, 1920, 1080, 1125, 560,  2 * 1920, 1,  -1,  1125, -1,   42, -1,  1121, -1,   7,  -1,  10, -1,  -1, -1},
    [SDI_FMT_1080P60]      = {"1080p60",      0x89, SDI_P_PICT_P_TR, SDI_R_60,    SDI_AR_16_9, 1920, 1080, 1125, 560,  2 * 1920, 1,  -1,  1125, -1,   42, -1,  1121, -1,   7,  -1,  10, -1,  -1, -1},

    [SDI_FMT_2160P23_98]   = {"2160p23.98",   0xC0, SDI_P_PICT_P_TR, SDI_R_23_98, SDI_AR_16_9, 3840, 2160, 1125, 6640, 2 * 7680, 1,  -1,  1125, -1,   42, -1,  1121, -1,   7,  -1,   10, -1,  -1, -1},
    [SDI_FMT_2160P24]      = {"2160p24",      0xC0, SDI_P_PICT_P_TR, SDI_R_24,    SDI_AR_16_9, 3840, 2160, 1125, 6640, 2 * 7680, 1,  -1,  1125, -1,   42, -1,  1121, -1,   7,  -1,   10, -1,  -1, -1},
    [SDI_FMT_2160P25]      = {"2160p25",      0xC0, SDI_P_PICT_P_TR, SDI_R_25,    SDI_AR_16_9, 3840, 2160, 1125, 5760, 2 * 7680, 1,  -1,  1125, -1,   42, -1,  1121, -1,   7,  -1,   10, -1,  -1, -1},
    [SDI_FMT_2160P29_97]   = {"2160p29.97",   0xC0, SDI_P_PICT_P_TR, SDI_R_29_97, SDI_AR_16_9, 3840, 2160, 1125, 2240, 2 * 7680, 1,  -1,  1125, -1,   42, -1,  1121, -1,   7,  -1,   10, -1,  -1, -1},
    [SDI_FMT_2160P30]      = {"2160p30",      0xC0, SDI_P_PICT_P_TR, SDI_R_30,    SDI_AR_16_9, 3840, 2160, 1125, 2240, 2 * 7680, 1,  -1,  1125, -1,   42, -1,  1121, -1,   7,  -1,   10, -1,  -1, -1},

    [SDI_FMT_2160P50]      = {"2160p50",      0xCE, SDI_P_PICT_P_TR, SDI_R_50,    SDI_AR_16_9, 3840, 2160, 1125, 5760, 2 * 7680, 1,  -1,  1125, -1,   42, -1,  1121, -1,   7,  -1,   10, -1,  -1, -1},
    [SDI_FMT_2160P59_94]   = {"2160p59.94",   0xCE, SDI_P_PICT_P_TR, SDI_R_59_94, SDI_AR_16_9, 3840, 2160, 1125, 2240, 2 * 7680, 1,  -1,  1125, -1,   42, -1,  1121, -1,   7,  -1,   10, -1,  -1, -1},
    [SDI_FMT_2160P60]      = {"2160p60",      0xCE, SDI_P_PICT_P_TR, SDI_R_60,    SDI_AR_16_9, 3840, 2160, 1125, 2240, 2 * 7680, 1,  -1,  1125, -1,   42, -1,  1121, -1,   7,  -1,   10, -1,  -1, -1},
};

const struct SdiInfo *av_sdi_info(SdiFormat sdi_fmt)
{
    if (sdi_fmt < 0 || sdi_fmt >= SDI_FMT_NB)
        return NULL;
    return &sdi_table[sdi_fmt];
}

/*
 * Init SDI buffer for stream.
 */
SdiBuffer *ff_sdi_buffer_alloc(AVStream *stream)
{
    SdiBuffer *buffer = av_mallocz(sizeof(SdiBuffer));
    buffer->queue = av_mallocz(sizeof(PacketList));
    buffer->stream = stream;
    if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        // 100ms fifo for audio
        int buffer_size = av_get_bits_per_sample(stream->codecpar->codec_id) *
                          stream->codecpar->ch_layout.nb_channels *
                          (stream->codecpar->sample_rate / 10);
        buffer->fifo = av_fifo_alloc2(buffer_size, 1, 0);
    }
    return buffer;
}

void ff_sdi_buffer_free(SdiBuffer *buffer)
{
    if (buffer) {
        if (buffer->queue) {
            ff_packet_list_free(buffer->queue);
            av_freep(&buffer->queue);
        }
        
        av_fifo_freep2(&buffer->fifo);
        av_free(buffer);
    }
}

void ff_sdi_buffer_freep(SdiBuffer **buffer)
{
    if (buffer) {
        ff_sdi_buffer_free(*buffer);
        *buffer = NULL;
    }
}

int ff_sdi_buffer_add(SdiBuffer *buffer, AVPacket *pkt)
{
    int result = ff_packet_list_put(buffer->queue, pkt, av_packet_ref, 0);
    if (result == 0) {
        buffer->last_pts = pkt->pts;
        buffer->last_duration = pkt->duration;
    }
    return result;
}

int ff_sdi_buffer_contains(SdiBuffer *buffer, int64_t pts, int64_t duration)
{
    int64_t start = INTMAX_MAX;
    int64_t end = 0;
    AVStream *stream = buffer->stream;
    AVCodecParameters *par = buffer->stream->codecpar;

    if (buffer->fifo) {
        int fifo_samples = av_get_audio_frame_duration2(
            buffer->stream->codecpar, (int)av_fifo_can_read(buffer->fifo));
        start = buffer->fifo_pts;
        end = buffer->fifo_pts + av_rescale_q_rnd(fifo_samples, av_make_q(1, par->sample_rate), stream->time_base, AV_ROUND_UP);
    }
    for (PacketListEntry *pktl = buffer->queue->head; pktl; pktl = pktl->next) {
        start = FFMIN(start, pktl->pkt.pts);
        end = FFMAX(end, pktl->pkt.pts + pktl->pkt.duration);
    }

    return pts <= end && (pts + duration) <= end && (pts + duration) >= start;
}

int ff_sdi_buffer_get_audio(SdiBuffer *buffer, void *dest, int dest_size, int64_t pts, int n_samples)
{
    int bytes_per_sample, n_written_samples, fifo_samples, total_samples;

    if (buffer->stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
        return AVERROR(EINVAL);

    bytes_per_sample =
        (av_get_bits_per_sample(buffer->stream->codecpar->codec_id) / 8) *
        buffer->stream->codecpar->ch_layout.nb_channels;
    n_written_samples = 0;

    // Delete old samples
    if (buffer->fifo_pts < pts) {
        int n_old_samples = pts - buffer->fifo_pts;
        int bytes = n_old_samples * bytes_per_sample;
        av_fifo_drain2(buffer->fifo, bytes);
        buffer->fifo_pts += n_old_samples;
    }

    fifo_samples = av_get_audio_frame_duration2(buffer->stream->codecpar, (int)av_fifo_can_read(buffer->fifo));
    if (fifo_samples > 0) {
        int total_samples = FFMIN(fifo_samples, n_samples);
        int bytes = total_samples * bytes_per_sample;
        av_fifo_read(buffer->fifo, dest, bytes);
        n_written_samples += total_samples;
        fifo_samples -= total_samples;
        buffer->fifo_pts += total_samples;
        dest = (char*)dest + bytes;
        dest_size -= bytes;
    }
    if (fifo_samples < (n_samples - n_written_samples)) {
        PacketListEntry *pktl = buffer->queue->head;
        while (pktl) {
            int fifo_end = buffer->fifo_pts + fifo_samples;
            if (pktl->pkt.pts == fifo_end) {
                AVPacket pkt;
                ff_packet_list_get(buffer->queue, &pkt);
                av_fifo_write(buffer->fifo, pkt.data, pkt.size);
                fifo_samples += pkt.duration;
                av_packet_unref(&pkt);
            }
            else if (pktl->pkt.pts > fifo_end && pktl->pkt.pts <= (pts + n_samples)) {
                int n_missing_samples = FFABS(fifo_end - pktl->pkt.pts);
                int bytes = n_missing_samples * bytes_per_sample;
                AVPacket pkt;

                memset(dest, 0, bytes);
                dest = (char*)dest + bytes;
                dest_size -= bytes;

                ff_packet_list_get(buffer->queue, &pkt);
                av_fifo_write(buffer->fifo, pkt.data, pkt.size);
                fifo_samples += pkt.duration;
                av_packet_unref(&pkt);
            }

            if (fifo_samples >= (n_samples - n_written_samples))
                break;
            pktl = buffer->queue->head;
        }
    }

    total_samples = FFMIN(fifo_samples, (n_samples - n_written_samples));
    if (total_samples > 0) {
        int bytes = total_samples * bytes_per_sample;
        av_fifo_read(buffer->fifo, dest, bytes);
        buffer->fifo_pts += total_samples;
        return total_samples + n_written_samples;
    }

    return n_written_samples;
}

int ff_sdi_buffer_get_video(SdiBuffer *buffer, AVPacket *pkt, int64_t pts, int64_t duration)
{
    PacketListEntry *pktl;

    if (buffer->stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
        return AVERROR(EINVAL);
    
    if (buffer->queue == NULL)
        return AVERROR(EINVAL);

    pktl = buffer->queue->head;
    while (pktl) {
        if (pktl->pkt.pts == pts && pktl->pkt.duration == duration)
            return ff_packet_list_get(buffer->queue, pkt);
        else if (pktl->pkt.pts >= pts && pktl->pkt.pts < (pts + duration))
            return ff_packet_list_get(buffer->queue, pkt);

        ff_packet_list_get(buffer->queue, pkt);
        av_packet_unref(pkt);
        pktl = buffer->queue->head;
    }

    return AVERROR(EAGAIN);
}

AVRational av_sdi_rate(SdiPictureRate rate)
{
    if (rate < SDI_R_23_98 || rate > SDI_R_60)
        return (AVRational){0, 0};
    return sdi_rate_table[rate];
}

AVRational av_sdi_aspect_ratio(SdiAspectRatio ratio)
{
    if (ratio < SDI_AR_4_3 || ratio > SDI_AR_16_9)
        return (AVRational){0, 0};
    return ff_sdi_ar_table[ratio];
}

const char *av_sdi_get_scanning_method_name(SdiScanningMethod method)
{
    if (method < SDI_I_PICT_I_TR || method > SDI_P_PICT_P_TR)
        return NULL;
    return ff_scanning_method_names[method];
}

const char *av_sdi_get_line_rate_name(SdiLineRate rate)
{
    if (rate < 0 || rate >= SDI_LINE_RATE_NB)
        return NULL;
    return ff_line_rate_names[rate];
}

static int parse_links(const char **arg)
{
    const char* dual = strstr(*arg, "Dual");
    const char* quad = strstr(*arg, "Quad");
    const char* octa = strstr(*arg, "Octa");
    if (dual) {
        *arg += 4;
        return 2;
    }
    else if (quad) {
        *arg += 4;
        return 4;
    }
    else if (octa) {
        *arg += 4;
        return 8;
    }
    return 1;
}

static int parse_standard(const char **arg)
{
    const char *sd, *ed, *hd, *_3_ga, *_3_gb, *_3_g, *_6_g, *_12_g, *_24_g;

    sd = strstr(*arg, "SD");
    if (sd) {
        *arg += 2;
        return SDI_LINE_RATE_SD;
    }

    ed = strstr(*arg, "ED");
    if (ed) {
        *arg += 2;
        return SDI_LINE_RATE_ED;
    }

    hd = strstr(*arg, "HD");
    if (hd) {
        *arg += 2;
        return SDI_LINE_RATE_HD;
    }

    _3_ga = strstr(*arg, "3GA");
    if (_3_ga) {
        *arg += 3;
        return SDI_LINE_RATE_3G;
    }

    _3_gb = strstr(*arg, "3GB");
    if (_3_gb) {
        *arg += 3;
        return SDI_LINE_RATE_3G;
    }

    _3_g = strstr(*arg, "3G");
    if (_3_g) {
        *arg += 2;
        return SDI_LINE_RATE_3G;
    }

    _6_g = strstr(*arg, "6G");
    if (_6_g) {
        *arg += 2;
        return SDI_LINE_RATE_6G;
    }

    _12_g = strstr(*arg, "12G");
    if (_12_g) {
        *arg += 3;
        return SDI_LINE_RATE_12G;
    }

    _24_g = strstr(*arg, "24G");
    if (_24_g) {
        *arg += 3;
        return SDI_LINE_RATE_24G;
    }

    return -1;
}

static int parse_lines(const char **arg)
{
    char *number_end;
    int result = strtol(*arg, &number_end, 10);
    *arg = number_end;
    return result;
}

static int parse_scanning_mode(const char **arg)
{
    const char* p = strstr(*arg, "p");
    const char* i = strstr(*arg, "i");
    const char* psf = strstr(*arg, "psf");
    if (psf) {
        *arg += 3;
        return SDI_P_PICT_I_TR;
    }
    else if (i) {
        *arg += 1;
        return SDI_I_PICT_I_TR;
    }
    else if (p) {
        *arg += 1;
        return SDI_P_PICT_P_TR;
    }
    else if (strlen(*arg) == 0) {
        return SDI_P_PICT_P_TR;
    }
    return -1;
}

static SdiPictureRate parse_frame_rate(const char **arg)
{
    if (strstr(*arg, "23")) {
        *arg += 2;
        if (strstr(*arg, "_98")) {
            *arg += 3;
        }
        return SDI_R_23_98;
    }
    else if (strstr(*arg, "24")) {
        *arg += 2;
        return SDI_R_24;
    }
    else if (strstr(*arg, "25")) {
        *arg += 2;
        return SDI_R_25;
    }
    else if (strstr(*arg, "29")) {
        *arg += 2;
        if (strstr(*arg, "_97")) {
            *arg += 3;
        }
        return SDI_R_29_97;
    }
    else if (strstr(*arg, "30")) {
        *arg += 2;
        return SDI_R_30;
    }
    else if (strstr(*arg, "50")) {
        *arg += 2;
        return SDI_R_50;
    }
    else if (strstr(*arg, "59")) {
        *arg += 2;
        if (strstr(*arg, "_94")) {
            *arg += 3;
        }
        return SDI_R_59_94;
    }
    else if (strstr(*arg, "60")) {
        *arg += 2;
        return SDI_R_60;
    }
    return -1;
}

int av_parse_standard_option(AVFormatContext *s, const char *arg, StandardOption *option)
{
    if (strlen(arg) == 0) {
        return -1;
    }

    option->links = parse_links(&arg);
    if (option->links != 1) {
        av_log(s, AV_LOG_ERROR, "Multilink is not supported\n");
        return -1;
    }

    option->standard = parse_standard(&arg);
    if (option->standard < SDI_LINE_RATE_SD ||
        option->standard > SDI_LINE_RATE_12G) {
        av_log(s, AV_LOG_ERROR, "SDI Standard not supported: %s\n", arg);
        return -1;
    }

    option->lines = parse_lines(&arg);
    // 525-line video is 480i by its usual name and 487 lines here
    if (option->lines == 480)
        option->lines = 487;
    if (option->lines < 487 || option->lines > 2160) {
        av_log(s, AV_LOG_ERROR, "Invalid number of lines: %d\n", option->lines);
        return -1;
    }

    option->scanning_mode = parse_scanning_mode(&arg);
    option->frame_rate = parse_frame_rate(&arg);

    return 0;
}

SdiFormat av_sdi_get_fmt(StandardOption *option)
{
    int format;

    if (!option) {
        return SDI_FMT_NONE;
    }

    format = ff_get_sdi_format(option->standard, option->lines);
    if (format == -1) {
        return SDI_FMT_NONE;
    }

    for (SdiFormat fmt = 0; fmt < SDI_FMT_NB; fmt++) {
        const struct SdiInfo *info = &sdi_table[fmt];
        if (format != info->payload_format)
            continue;
        if (option->frame_rate != -1) {
            if (option->frame_rate != info->picture_rate)
                continue;
        }
        if (option->scanning_mode != -1) {
            if (option->scanning_mode != info->scanning_method)
                continue;
        }
        return fmt;
    }
    return SDI_FMT_NONE;
}

SdiFormat av_find_matching_standard(AVFormatContext *s, StandardOption *option, AVStream *stream)
{
    int is_interlaced;
    int width;
    int height;
    AVRational framerate;

    if (stream) {
        enum AVFieldOrder field_order = stream->codecpar->field_order;
        int is_interlaced = !(field_order == AV_FIELD_UNKNOWN || field_order == AV_FIELD_PROGRESSIVE);

        av_log(s, AV_LOG_DEBUG, "Stream:\n");
        av_log(s, AV_LOG_DEBUG, "  Interlaced: %s\n", is_interlaced ? "true" : "false");
        av_log(s, AV_LOG_DEBUG, "  FrameRate: %d/%d\n",
               stream->avg_frame_rate.num, stream->avg_frame_rate.den);
    }

    if (option) {
        AVRational option_rate;
        option_rate = av_sdi_rate(option->frame_rate);

        av_log(s, AV_LOG_DEBUG, "Options:\n");
        av_log(s, AV_LOG_DEBUG, "  Links        : %d\n", option->links);
        av_log(s, AV_LOG_DEBUG, "  Standard     : %s\n", av_sdi_get_line_rate_name(option->standard));
        av_log(s, AV_LOG_DEBUG, "  Lines        : %d\n", option->lines);
        av_log(s, AV_LOG_DEBUG, "  Scanning mode: %s\n", av_sdi_get_scanning_method_name(option->scanning_mode));
        av_log(s, AV_LOG_DEBUG, "  Framerate    : %d/%d\n", option_rate.num, option_rate.den);

        if (option->scanning_mode == SDI_P_PICT_I_TR) {
            if (stream && stream->codecpar->field_order != AV_FIELD_PROGRESSIVE) {
                av_log(s, AV_LOG_ERROR, "Scanning mode mismatch\n");
                return SDI_FMT_NONE;
            }
        }
        else if (option->scanning_mode == SDI_I_PICT_I_TR) {
            if (stream && stream->codecpar->field_order != AV_FIELD_TB) {
                av_log(s, AV_LOG_ERROR, "Scanning mode mismatch\n");
                return SDI_FMT_NONE;
            }
        }
        else if (option->scanning_mode == SDI_P_PICT_P_TR) {
            if (stream && stream->codecpar->field_order != AV_FIELD_PROGRESSIVE) {
                av_log(s, AV_LOG_ERROR, "Scanning mode mismatch\n");
                return SDI_FMT_NONE;
            }
        }
    }

    if (stream) {
        if (stream->codecpar->field_order == AV_FIELD_UNKNOWN ||
            stream->codecpar->field_order == AV_FIELD_PROGRESSIVE)
            is_interlaced = 0;
        else
            is_interlaced = 1;
        width = stream->codecpar->width;
        height = stream->codecpar->height;
        framerate = stream->avg_frame_rate;
    }

    for (SdiFormat sdi_fmt = 0; sdi_fmt < SDI_FMT_NB; sdi_fmt++) {
        const struct SdiInfo *info = &sdi_table[sdi_fmt];

        int sdi_is_interlaced = is_interlaced_picture(info->scanning_method);
        int sdi_width = info->picture_width;
        int sdi_height = info->picture_height;
        AVRational sdi_framerate = av_sdi_rate(info->picture_rate);
        int width_delta, height_delta, valid_resolution, delta, delta_percentage;

        if (!stream) {
            is_interlaced = sdi_is_interlaced;
            width = sdi_width;
            height = sdi_height;
            framerate = sdi_framerate;
        }

        // Allow resolutions withing 10% of source resolution
        width_delta = width - sdi_width;
        height_delta = height - sdi_height;
        valid_resolution = 0;
        delta = FFMIN(width_delta, height_delta);
        delta_percentage = 0;
        if (width_delta < height_delta) {
            delta = width_delta;
            delta_percentage = (int)round(width / 10.0);
        }
        else {
            delta = height_delta;
            delta_percentage = (int)round(height / 10.0);
        }

        if (option) {
            int format = ff_get_sdi_format(option->standard, option->lines);
            if (format != info->payload_format)
                continue;
            if (av_cmp_q(sdi_framerate, framerate) != 0)
                continue;
            if (option->scanning_mode != info->scanning_method)
                continue;
            if (stream && (is_interlaced != sdi_is_interlaced))
                continue;

            return sdi_fmt;
        }
        else {
            // Accept downscale if whithin a 10% margin
            if (delta > 0 && delta_percentage <= 10) {
                valid_resolution = 1;
                av_log(s, AV_LOG_DEBUG, "%d: %d > 0\n", sdi_fmt, delta);
            } else if (delta <= 0) {
                av_log(s, AV_LOG_DEBUG, "%d: %d <= 0\n", sdi_fmt, delta);
                valid_resolution = 1;
            }

            if (valid_resolution && av_cmp_q(sdi_framerate, framerate) == 0) {
                if (is_interlaced != sdi_is_interlaced)
                    continue;
                return sdi_fmt;
            }
        }
    }
    return SDI_FMT_NONE;
}

uint16_t ff_b9_not_b8(uint16_t value)
{
    return (value & 0x1ff) | ((~value & 0x100) << 1);
}

/*
 * Calculate 6-byte BCH over 8-bit words
 */
uint64_t ff_calculate_adp_bch(const uint16_t *in, int length)
{
    uint64_t bch = 0;

    while (length--)
        bch = bch >> 8 ^ (((*in++ ^ bch) & 0xFF) * 0x10101010001ULL);
    return bch;
}

uint16_t ff_calculate_adp_cs(const uint16_t *in, int length)
{
    uint16_t cs = 0;
    while (length--)
        cs += *in++;
    return (cs & 0x1ff) | ((~cs & 0x100) << 1);
}

int ff_is_sd(uint32_t format)
{
    return format == 0x81 ? 1 : 0;
}

int ff_has_sub_images(uint32_t format)
{
    return format == 0xc0 || format == 0xce ? 1 : 0;
}

int ff_sdi_get_nr_channels(uint32_t format)
{
    return ff_is_sd(format) ? 1 : ff_has_sub_images(format) ? 8 : 2;
}

int ff_get_sdi_format(int sdi_line_rate, int lines)
{
    if (sdi_line_rate == SDI_LINE_RATE_SD && (lines == 576 || lines == 487))
        return 0x81;
    if (sdi_line_rate == SDI_LINE_RATE_HD && lines == 720)
        return 0x84;
    if (sdi_line_rate == SDI_LINE_RATE_HD && lines == 1080)
        return 0x85;
    if (sdi_line_rate == SDI_LINE_RATE_3G && lines == 1080)
        return 0x89;
    if (sdi_line_rate == SDI_LINE_RATE_6G && lines == 2160)
        return 0xC0;
    if (sdi_line_rate == SDI_LINE_RATE_12G && lines == 2160)
        return 0xCE;
    return -1;
}

int ff_is_switching_line(const struct SdiInfo *info, int line)
{
    if (line < 0)
        return 0;
    if (line == info->switching_line_field1)
        return 1;
    if (line == info->switching_line_field2)
        return 1;
    if (line == info->error_line_field1)
        return 1;
    if (line == info->error_line_field2)
        return 1;
    return 0;
}

int ff_is_vbi_line(const struct SdiInfo *info, int line)
{
    return !ff_is_video_line(info, line);
}

/*
 * Return 1 if this line should contain video, else 0.
 */
int ff_is_video_line(const struct SdiInfo *info, int line)
{
    if (line >= info->vid_start_line_field1 &&
        line <= info->vid_end_line_field1)
        return 1;

    if (is_interlaced_transport(info->scanning_method)) {
        if (line >= info->vid_start_line_field2 &&
            line <= info->vid_end_line_field2)
            return 1;
    }

    return 0;
}

int ff_get_field_nr(const struct SdiInfo *info, int line)
{
    if (info->start_line_field2 == -1)
        return 1;
    return line >= info->start_line_field2 ? 2 : 1;
}

int ff_sdi_max_audio_channels(int format, int rate)
{
    if (format == 0x81) {
        // For SD SDI only one audio group is implemented. The max number of
        // audio channels is therefore limited to 4 (or 2) instead of 16 (or 8).
        return rate < 96000 ? 4 : 2;
    }
    else if (format == 0x84) {
        return rate < 96000 ? 16 : 8;
    }
    else if (format == 0x85) {
        return rate < 96000 ? 16 : 8;
    }
    else {
        // Limit other standards to 16 channels for now
        return rate < 96000 ? 16 : 8;
    }
    return 0;
}
