/*
 * Pairing of images and audio into SDI frames
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

#include "libavutil/avutil.h"
#include "libavutil/error.h"
#include "libavutil/fifo.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"
#include "sdipair.h"

/* The sample rate of SDI's embedded audio. */
#define SAMPLE_RATE 48000

/* The images that may wait for their audio; with more, the first goes without it. */
#define MAX_IMAGES_AHEAD 8

/* A gap or an overlap in the audio up to this many samples is jitter in the
 * timestamps: the samples follow on all the same. */
#define MAX_JITTER 96

/* The most audio the pairing holds, 5 s; a gap or an overlap larger than that starts
 * the audio afresh. */
#define MAX_SAMPLES (5 * SAMPLE_RATE)

/* The silence written into a gap at a time. */
#define SILENCE_SAMPLES 1024

struct SdiPair {
    AVRational frame_rate;
    int sample_size;             ///< bytes per sample, every channel; 0 without audio

    AVPacket *images[MAX_IMAGES_AHEAD + 1]; ///< the images waiting, oldest first
    int64_t image_frames[MAX_IMAGES_AHEAD + 1]; ///< the frame number of each
    int nb_images;

    int64_t next_frame;          ///< number of the next frame to take; INT64_MIN before any
    int64_t next_sample;         ///< number of that frame's first sample

    AVFifo *audio;               ///< the samples not taken yet
    int64_t audio_start;         ///< number of the first sample in audio; INT64_MIN before any
    uint8_t *silence;            ///< SILENCE_SAMPLES samples of silence
};

/* The number of the first sample of frame number frame, at the nominal rate. */
static int64_t frame_first_sample(const SdiPair *pair, int64_t frame)
{
    return av_rescale_rnd(frame, (int64_t)SAMPLE_RATE * pair->frame_rate.den,
                          pair->frame_rate.num, AV_ROUND_NEAR_INF);
}

/* The number of the sample after the last one in the FIFO. */
static int64_t audio_end(const SdiPair *pair)
{
    return pair->audio_start + (int64_t)av_fifo_can_read(pair->audio);
}

/* Drop the oldest n samples of the FIFO. */
static void drop_samples(SdiPair *pair, int64_t n)
{
    n = FFMIN(n, (int64_t)av_fifo_can_read(pair->audio));
    if (n <= 0)
        return;
    av_fifo_drain2(pair->audio, n);
    pair->audio_start += n;
}

/* Append n samples of silence to the FIFO. */
static int write_silence(SdiPair *pair, int64_t n)
{
    while (n > 0) {
        int chunk = (int)FFMIN(n, SILENCE_SAMPLES);
        int ret = av_fifo_write(pair->audio, pair->silence, chunk);
        if (ret < 0)
            return ret;
        n -= chunk;
    }
    return 0;
}

SdiPair *ff_sdi_pair_alloc(AVRational frame_rate, int sample_size)
{
    SdiPair *pair = av_mallocz(sizeof(*pair));

    if (!pair)
        return NULL;
    pair->frame_rate = frame_rate;
    pair->sample_size = sample_size;
    pair->next_frame = INT64_MIN;
    pair->audio_start = INT64_MIN;
    if (sample_size > 0) {
        pair->audio = av_fifo_alloc2(SAMPLE_RATE / 5, sample_size, AV_FIFO_FLAG_AUTO_GROW);
        pair->silence = av_mallocz((size_t)SILENCE_SAMPLES * sample_size);
        if (!pair->audio || !pair->silence) {
            ff_sdi_pair_free(&pair);
            return NULL;
        }
        // Room for the most audio held and a packet more
        av_fifo_auto_grow_limit(pair->audio, MAX_SAMPLES + 2 * SAMPLE_RATE);
    }
    return pair;
}

void ff_sdi_pair_free(SdiPair **pair)
{
    if (!*pair)
        return;
    for (int i = 0; i < (*pair)->nb_images; i++)
        av_packet_free(&(*pair)->images[i]);
    av_fifo_freep2(&(*pair)->audio);
    av_freep(&(*pair)->silence);
    av_freep(pair);
}

/*
 * The frame number of an image without a timestamp is the one after the image before.
 */
int ff_sdi_pair_add_video(SdiPair *pair, const AVPacket *pkt, AVRational time_base)
{
    int64_t frame;
    int ret;

    if (pkt->pts != AV_NOPTS_VALUE)
        frame = av_rescale_q_rnd(pkt->pts, time_base, av_inv_q(pair->frame_rate),
                                 AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
    else if (pair->nb_images > 0)
        frame = pair->image_frames[pair->nb_images - 1] + 1;
    else if (pair->next_frame != INT64_MIN)
        frame = pair->next_frame;
    else
        frame = 0;

    if (pair->next_frame != INT64_MIN && frame < pair->next_frame)
        return 0;
    if (pair->nb_images > 0 && frame <= pair->image_frames[pair->nb_images - 1])
        return 0;
    if (pair->nb_images > MAX_IMAGES_AHEAD)
        return AVERROR(EAGAIN);

    pair->images[pair->nb_images] = av_packet_alloc();
    if (!pair->images[pair->nb_images])
        return AVERROR(ENOMEM);
    ret = av_packet_ref(pair->images[pair->nb_images], pkt);
    if (ret < 0) {
        av_packet_free(&pair->images[pair->nb_images]);
        return ret;
    }
    pair->image_frames[pair->nb_images++] = frame;

    if (pair->next_frame == INT64_MIN) {
        pair->next_frame = frame;
        pair->next_sample = frame_first_sample(pair, frame);
    }
    return 0;
}

/*
 * Samples without a timestamp follow on from those before.
 */
int ff_sdi_pair_add_audio(SdiPair *pair, const AVPacket *pkt, AVRational time_base)
{
    const uint8_t *data = pkt->data;
    int64_t n, first, gap;
    int ret;

    if (pair->sample_size == 0)
        return 0;
    n = pkt->size / pair->sample_size;
    if (n == 0)
        return 0;

    if (pkt->pts != AV_NOPTS_VALUE)
        first = av_rescale_q_rnd(pkt->pts, time_base, (AVRational){ 1, SAMPLE_RATE },
                                 AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
    else if (pair->audio_start != INT64_MIN)
        first = audio_end(pair);
    else
        first = pair->next_sample != INT64_MIN && pair->next_frame != INT64_MIN
                    ? pair->next_sample : 0;

    if (pair->audio_start == INT64_MIN) {
        pair->audio_start = first;
    } else {
        gap = first - audio_end(pair);
        if (gap > MAX_SAMPLES || -gap > MAX_SAMPLES) {
            // A jump: the audio starts afresh
            drop_samples(pair, av_fifo_can_read(pair->audio));
            pair->audio_start = first;
        } else if (gap > MAX_JITTER) {
            ret = write_silence(pair, gap);
            if (ret < 0)
                return ret;
        } else if (gap < -MAX_JITTER) {
            int64_t skip = FFMIN(-gap, n);
            data += skip * pair->sample_size;
            n -= skip;
        }
    }

    ret = av_fifo_write(pair->audio, data, n);
    if (ret < 0)
        return ret;

    // Audio far ahead of the images loses its oldest samples
    drop_samples(pair, (int64_t)av_fifo_can_read(pair->audio) - MAX_SAMPLES);
    return 0;
}

/* The number of the first sample of the next frame to take, which is the first image's:
 * an image after a gap starts at its own nominal sample. */
static int64_t first_image_sample(const SdiPair *pair)
{
    if (pair->image_frames[0] == pair->next_frame)
        return pair->next_sample;
    return frame_first_sample(pair, pair->image_frames[0]);
}

int ff_sdi_pair_ready(const SdiPair *pair, int nb_samples, int flush)
{
    if (pair->nb_images == 0)
        return 0;
    if (pair->sample_size == 0 || flush || pair->nb_images >= MAX_IMAGES_AHEAD)
        return 1;
    if (pair->audio_start == INT64_MIN)
        return 0;
    return audio_end(pair) >= first_image_sample(pair) + nb_samples;
}

int ff_sdi_pair_take(SdiPair *pair, AVPacket *video, uint8_t *samples, int nb_samples)
{
    int64_t first;

    if (pair->nb_images == 0)
        return AVERROR(EAGAIN);

    first = first_image_sample(pair);
    pair->next_frame = pair->image_frames[0];
    av_packet_move_ref(video, pair->images[0]);
    av_packet_free(&pair->images[0]);
    pair->nb_images--;
    memmove(pair->images, pair->images + 1, pair->nb_images * sizeof(*pair->images));
    memmove(pair->image_frames, pair->image_frames + 1,
            pair->nb_images * sizeof(*pair->image_frames));

    if (pair->sample_size > 0) {
        int64_t lead = 0, n = 0;

        if (pair->audio_start != INT64_MIN) {
            drop_samples(pair, first - pair->audio_start);
            // Audio that starts later than the frame leaves silence before it
            lead = FFMIN(FFMAX(pair->audio_start - first, 0), nb_samples);
            n = FFMIN((int64_t)av_fifo_can_read(pair->audio), nb_samples - lead);
        } else {
            lead = nb_samples;
        }
        memset(samples, 0, (size_t)lead * pair->sample_size);
        if (n > 0) {
            av_fifo_read(pair->audio, samples + lead * pair->sample_size, n);
            pair->audio_start += n;
        }
        memset(samples + (lead + n) * pair->sample_size, 0,
               (size_t)(nb_samples - lead - n) * pair->sample_size);
    }

    pair->next_frame++;
    pair->next_sample = first + nb_samples;
    return 0;
}
