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

#ifndef AVFORMAT_SDIFRAME_H
#define AVFORMAT_SDIFRAME_H

#include <stdint.h>

#include "libavcodec/packet.h"
#include "avformat.h"
#include "sdicommon.h"

#include "cdtapi_sdi.h"

/*
 * The SDI frames of the sdi muxer and demuxer and of the dektec device: an unpacker
 * takes a frame apart with CDTAPI's parser into an image and audio, a packer puts one
 * together with CDTAPI's builder from the packets of a video and an audio stream. Each
 * works on a frame a DtSdiView describes: one in a buffer of its user's, or one a
 * channel lends where the card wrote it or will send it.
 *
 * Shared by libavformat and libavdevice, which each compile it.
 */

/**
 * The threads option of the sdi format and the dektec device: FF_SDI_THREADS_AUTO for
 * FF_SDI_AUTO_THREADS threads, each frame divided as its standard calls for; 1 for none
 * but the caller's; or more.
 */
#define FF_SDI_THREADS_AUTO 0
#define FF_SDI_AUTO_THREADS 4

/**
 * The channels of audio a frame carries: four groups of four.
 */
#define FF_SDI_AUDIO_MAX_CHANNELS 16

/* ---- Taking frames apart ---- */

typedef struct SdiUnpacker SdiUnpacker;

/**
 * Allocate an unpacker for frames of the standard info, with the threads of the threads
 * option.
 *
 * @return 0, or a negative error code
 */
int ff_sdi_unpacker_alloc(SdiUnpacker **unpacker, void *log_ctx,
                          const struct SdiInfo *info, int threads);

/**
 * Free an unpacker, and set *unpacker to NULL.
 */
void ff_sdi_unpacker_free(SdiUnpacker **unpacker);

/**
 * The view the caller points at each frame before ff_sdi_unpacker_parse().
 */
DtSdiView *ff_sdi_unpacker_view(SdiUnpacker *unpacker);

/**
 * Add the video stream to s, as stream 0: wrapped frames of the standard's image, which
 * count in frames, nb_frames of them, 0 when not known.
 *
 * @return 0, or a negative error code
 */
int ff_sdi_unpacker_add_video_stream(SdiUnpacker *unpacker, AVFormatContext *s,
                                     int64_t nb_frames);

/**
 * Add the audio stream to s: nb_channels of 24-bit PCM at 48 kHz, which count in
 * samples. A frame with fewer channels gives silence on the others, one with more loses
 * those beyond.
 *
 * @return 0, or a negative error code
 */
int ff_sdi_unpacker_add_audio_stream(SdiUnpacker *unpacker, AVFormatContext *s,
                                     int nb_channels);

/**
 * Take apart the frame the view describes: its image goes into pkt, with frame_number
 * as its timestamp, and its audio waits for ff_sdi_unpacker_audio(). The view may point
 * elsewhere once this returns.
 *
 * @return 0, or a negative error code
 */
int ff_sdi_unpacker_parse(SdiUnpacker *unpacker, int64_t frame_number, AVPacket *pkt);

/**
 * The channels the frame parsed last carried: up to the last one with valid audio. A
 * channel whose samples are all marked not valid is one a transmitter fills for the
 * channels of a group that have no audio, so it does not count unless a valid one
 * follows it.
 */
int ff_sdi_unpacker_nb_channels(const SdiUnpacker *unpacker);

/**
 * Put the audio of the frame parsed last into pkt, once, for the audio stream.
 *
 * @return 1 with a packet, 0 without: there is no audio stream, or the frame's audio
 *         went out already; or a negative error code
 */
int ff_sdi_unpacker_audio(SdiUnpacker *unpacker, AVPacket *pkt);

/* ---- Putting frames together ---- */

typedef struct SdiPacker SdiPacker;

/**
 * Allocate a packer for frames of the standard info, from the images of video_stream,
 * scaled to the standard's when they differ, and the audio of audio_stream, or none
 * without one, in nb_channels channels, or the stream's with -1. Builds the line CRCs
 * and packet checksums when checksums is set.
 *
 * @return 0, or a negative error code
 */
int ff_sdi_packer_alloc(SdiPacker **packer, void *log_ctx, const struct SdiInfo *info,
                        int threads, int checksums, AVStream *video_stream,
                        AVStream *audio_stream, int nb_channels);

/**
 * Free a packer, and set *packer to NULL.
 */
void ff_sdi_packer_free(SdiPacker **packer);

/**
 * The view the caller points at room for each frame before ff_sdi_packer_build().
 */
DtSdiView *ff_sdi_packer_view(SdiPacker *packer);

/**
 * The size of a frame of the standard, without padding.
 */
size_t ff_sdi_packer_frame_size(const SdiPacker *packer);

/**
 * Hand the packer a packet of the video or the audio stream; one of another stream is
 * left out.
 *
 * @return 0, or a negative error code
 */
int ff_sdi_packer_add(SdiPacker *packer, const AVPacket *pkt);

/**
 * Tell whether the next frame can be built; see ff_sdi_pair_ready() for flush.
 *
 * @return 1 or 0, or a negative error code
 */
int ff_sdi_packer_ready(SdiPacker *packer, int flush);

/**
 * Build the next frame, which ff_sdi_packer_ready() said is ready, into the room the
 * view describes. A frame the builder refuses is built black and silent, so that room a
 * channel lent still holds a frame.
 *
 * @return 0, or a negative error code
 */
int ff_sdi_packer_build(SdiPacker *packer);

#endif /* AVFORMAT_SDIFRAME_H */
