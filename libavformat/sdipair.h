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

#ifndef AVFORMAT_SDIPAIR_H
#define AVFORMAT_SDIPAIR_H

#include <stdint.h>

#include "libavcodec/packet.h"
#include "libavutil/rational.h"

/**
 * Pairs the packets of a video stream and of an audio stream into SDI frames: each
 * frame's image, and the audio samples the frame carries.
 *
 * It counts in frames and in samples at 48 kHz, from the packets' timestamps rounded to
 * the nearest frame or sample, so that timestamps a little off put each packet where it
 * belongs: microseconds at a rate whose frame does not last a whole number of them, for
 * example. A frame's samples follow on from those of the frame before. Audio that leaves
 * a gap of more than a few samples gets silence in the gap; audio that overlaps loses the
 * samples already passed. A frame whose audio does not come in time is paired with
 * silence where the audio is missing.
 *
 * Shared by the sdi muxer and the dektec output device, which each compile it.
 */
typedef struct SdiPair SdiPair;

/**
 * Allocate a pairing for frames at frame_rate, with audio samples of sample_size bytes,
 * every channel included, or 0 for frames without audio.
 *
 * @return the pairing, or NULL when out of memory
 */
SdiPair *ff_sdi_pair_alloc(AVRational frame_rate, int sample_size);

/**
 * Free a pairing and the packets it holds, and set *pair to NULL.
 */
void ff_sdi_pair_free(SdiPair **pair);

/**
 * Add the image in pkt, whose timestamps count in time_base. The pairing takes a
 * reference. An image for a frame already taken, or for a frame that has one already,
 * is left out.
 *
 * @return 0, or a negative error code
 */
int ff_sdi_pair_add_video(SdiPair *pair, const AVPacket *pkt, AVRational time_base);

/**
 * Add the audio samples in pkt, whose timestamps count in time_base. They are copied.
 *
 * @return 0, or a negative error code
 */
int ff_sdi_pair_add_audio(SdiPair *pair, const AVPacket *pkt, AVRational time_base);

/**
 * Tell whether the next frame can be taken with nb_samples samples: its image is there,
 * and so is its audio, or the audio is too late to wait for. With flush set, any image
 * there is ready, its audio or not.
 */
int ff_sdi_pair_ready(const SdiPair *pair, int nb_samples, int flush);

/**
 * Take the next frame: move its image into video, and copy its nb_samples samples into
 * samples, silence where the audio has none. Call it only when ff_sdi_pair_ready()
 * says the frame is ready.
 *
 * @return 0, or a negative error code
 */
int ff_sdi_pair_take(SdiPair *pair, AVPacket *video, uint8_t *samples, int nb_samples);

#endif /* AVFORMAT_SDIPAIR_H */
