/*
 * SDI audio encoding
 * Copyright (c) 2019 DekTec, Werner Damman
 * Copyright (c) 2020-2023 DekTec, Jeroen Steendam
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
 * SDI audio encoding
 * @author Werner Damman
 * @author Jeroen Steendam
 */

#ifndef AVFORMAT_SDIENC_AUDIO_H
#define AVFORMAT_SDIENC_AUDIO_H

#include "avformat.h"
#include "libavutil/fifo.h"
#include "libavformat/sdicommon.h"
#include <inttypes.h>

/**
 * Private data for sdi audio
 */
typedef struct SdiAudio {
    SdiBuffer *buffer;
    const struct SdiInfo *sdi_info;     ///< all magics for the current SDI standard
    int rate;                        ///< sample rate in Hz
    int fifo_size;                   ///< audio fifo size in bytes
    AVFifo *fifo;              ///< audio fifo
    int nr_ch;                       ///< nr of audio channels
    int bits_per_sample;
    int aes_cnt[4];                     ///< bit position in aes status word (0-191)
    int pos;                         ///< in samples*rate.num
    int pkt_cnt;                     ///< for diagnostics
    int max_samples_per_line;
    uint8_t dbn[128];   //< data block number TODO: ptr
    int af;                          ///< sequence number, see smpte 299
    int max_af;                      ///< highest sequence nr
    int phase;                       ///< phase related to EAV
    int queued_samples;              ///< total samples queued
    int frame_duration;              ///< in sample pairs

    // Audio state
    int64_t clock_phase;
    int64_t clock_phase_increment;
    int64_t sample_credit;
    int64_t line_credit;
    int64_t credit;
    int delayed_samples;
    int64_t magic_factor;
} SdiAudio;

SdiAudio *ff_sdi_audio_alloc(void);
void ff_sdi_audio_free(SdiAudio *audio);
void ff_sdi_audio_freep(SdiAudio **audio);

int ff_sdi_audio_init(SdiAudio *audio, AVFormatContext *s, int option_nr_ch,
                      SdiBuffer *buffer, const struct SdiInfo *sdi_info,
                      AVStream *audio_stream);
int ff_sdi_audio_buffered_samples(SdiAudio *audio);
int ff_sdi_audio_enough_for_frame(SdiAudio *audio);

void ff_sdi_audio_frame_start(SdiAudio *audio, int64_t pts);
void ff_sdi_audio_insert(SdiAudio *audio, uint16_t *ptr, int size,
                         int line);
void ff_sdi_audio_frame_end(SdiAudio *audio);

#endif // AVFORMAT_SDIENC_AUDIO_H
