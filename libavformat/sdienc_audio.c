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

#include "sdicommon.h"
#include "sdienc_audio.h"
#include "internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/avassert.h"

// nr of bytes for an ST 299 audio packet
#define AUDIO_CONTROL_PKT_SIZE  (18)
#define AUDIO_DATA_PKT_SIZE     (31)

#define AUDIO_GRP_1 1
#define AUDIO_GRP_2 2
#define AUDIO_GRP_3 3
#define AUDIO_GRP_4 4

// AES/EBU status word with flags default (48 kHz, stereo)
static const uint8_t AES3_DEFAULT_STATUS[] = {
        0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9b};

static void euclidean_division(AVRational rational, int *quotient,
                               int *remainder)
{
    int q, r;

    if (quotient == NULL && remainder == NULL)
        return;

    q = rational.num / rational.den;
    r = rational.num % rational.den;
    if (r < 0) {
        r += rational.den;
        if (q >= 0)
            (q)++;
        else
            (q)--;
    }
    av_assert1(r >= 0);
    if (quotient)
        *quotient = q;
    if (remainder)
        *remainder = r;
}

static int av_ceil_q(AVRational rational)
{
    int quotient = 0;
    int remainder = 0;
    euclidean_division(rational, &quotient, &remainder);
    if (remainder == 0)
        return quotient;
    return quotient + 1;
}

static int64_t av_lcm(int64_t a, int64_t b)
{
    return (a / av_gcd(a, b)) * b;
}

static int even(int n)
{
    if ((n % 2) == 0)
        return n;
    return n + 1;
}

static int max_audio_samples_per_line(AVRational audio_sample_rate,
                                      const struct SdiInfo *sdi)
{
    AVRational sdi_rate, samples_per_frame, line_rate;
    int usable_lines, max_samples_per_line, max_samples_per_frame;

    if (!sdi)
        return -1;

    sdi_rate = av_sdi_rate(sdi->picture_rate);
    samples_per_frame = av_div_q(audio_sample_rate, sdi_rate);
    line_rate = av_mul_q(sdi_rate, (AVRational){sdi->nr_sdi_lines, 1});
    usable_lines = sdi->nr_sdi_lines;
    if (sdi->scanning_method == SDI_I_PICT_I_TR ||
        sdi->scanning_method == SDI_P_PICT_I_TR)
        usable_lines -= 2; // 2 switching lines for interlaced
    else
        usable_lines -= 1; // 1 switching line for progressive
    if (ff_is_sd(sdi->payload_format))
        usable_lines -= 2; // 2 error lines

    max_samples_per_line = av_ceil_q(av_div_q(audio_sample_rate, line_rate));
    max_samples_per_frame = max_samples_per_line * usable_lines;
    if (max_samples_per_frame < av_ceil_q(samples_per_frame))
        max_samples_per_line += 1;

    if (audio_sample_rate.num == 96000 && audio_sample_rate.den == 1001)
        max_samples_per_line = even(max_samples_per_line);

    return max_samples_per_line;
}

static void log_setup(AVFormatContext *s, SdiAudio *audio)
{
    av_log(s, AV_LOG_DEBUG, "Setup audio\n");
    av_log(s, AV_LOG_DEBUG, "frame duration:  %d samples\n", audio->frame_duration);
    av_log(s, AV_LOG_DEBUG, "nr of channels:  %d\n", audio->nr_ch);
    av_log(s, AV_LOG_DEBUG, "fifo size:       %d\n", audio->fifo_size);
    av_log(s, AV_LOG_DEBUG, "max samples/line:%d\n", audio->max_samples_per_line);

    av_log(s, AV_LOG_DEBUG, "af:                    %d\n", audio->af);
    av_log(s, AV_LOG_DEBUG, "max_af:                %d\n", audio->max_af);
    av_log(s, AV_LOG_DEBUG, "max_samples_per_line:  %d\n", audio->max_samples_per_line);
    av_log(s, AV_LOG_DEBUG, "clock_phase_increment: %"PRId64"\n", audio->clock_phase_increment);
    av_log(s, AV_LOG_DEBUG, "sample_credit:         %"PRId64"\n", audio->sample_credit);
    av_log(s, AV_LOG_DEBUG, "line_credit:           %"PRId64"\n", audio->line_credit);
    av_log(s, AV_LOG_DEBUG, "clock_phase:           %"PRId64"\n", audio->clock_phase);
    av_log(s, AV_LOG_DEBUG, "credit:                %"PRId64"\n", audio->credit);
    av_log(s, AV_LOG_DEBUG, "delayed_samples:       %d\n", audio->delayed_samples);
    av_log(s, AV_LOG_DEBUG, "magic_factor:          %"PRId64"\n", audio->magic_factor);
}

SdiAudio *ff_sdi_audio_alloc(void)
{
    return av_mallocz(sizeof(SdiAudio));
}

int ff_sdi_audio_init(SdiAudio *audio, AVFormatContext *s, int option_nr_ch,
                             SdiBuffer *buffer, const struct SdiInfo *sdi_info,
                             AVStream *audio_stream)
{
    AVCodecParameters *par;
    AVRational frame_rate;
    AVRational sdi_samples_per_line;
    AVRational sdi_samples_per_frame;
    AVRational sdi_rate;
    AVRational audio_sample_rate;
    AVRational audio_samples_per_frame;
    AVRational clock_phase_increment;
    AVRational sample_credit;
    AVRational line_credit;
    AVRational credit;

    audio->buffer = buffer;
    audio->sdi_info = sdi_info;

    if (audio_stream == NULL || option_nr_ch == 0)
        return AVERROR(EINVAL);
    
    par = audio_stream->codecpar;

    if (option_nr_ch == -1)
        audio->nr_ch = par->ch_layout.nb_channels;
    else
        audio->nr_ch = option_nr_ch;
    if (par->codec_id == AV_CODEC_ID_PCM_S24LE ||
        par->codec_id == AV_CODEC_ID_PCM_S32LE) {
        audio->bits_per_sample = av_get_bits_per_sample(par->codec_id);
    } else {
        av_log(s, AV_LOG_ERROR, "Unsupported audio codec %s\n",
               avcodec_get_name(par->codec_id));
        return AVERROR(EINVAL);
    }

    if (par->sample_rate == 48000) {
        audio->rate = par->sample_rate;
    } else {
        av_log(s, AV_LOG_ERROR, "Unsupported sample rate %d\n",
               par->sample_rate);
        return AVERROR(EINVAL);
    }
    if (audio->nr_ch >
        ff_sdi_max_audio_channels(sdi_info->payload_format, audio->rate)) {
        av_log(s, AV_LOG_ERROR, "Max number of audio channels exceeded\n");
        return AVERROR(EINVAL);
    }

    // Calculate nr. of samples in an SDI frame (rounded up), allocate audio
    // fifo with space for at least 2 frames.
    frame_rate = av_sdi_rate(sdi_info->picture_rate);
    audio->frame_duration =
        (audio->rate * frame_rate.den + frame_rate.num - 1) / frame_rate.num;
    //        avpriv_set_pts_info(s->streams[1], 64, 1, 1000000);
    audio->fifo_size = 2 * audio->frame_duration * audio->nr_ch * (audio->bits_per_sample / 8);
    audio->fifo = av_fifo_alloc2(audio->fifo_size, 1, 0);
    if (!audio->fifo)
        return AVERROR(ENOMEM);

    // Initialize audio insertion algorithm
    sdi_samples_per_line = (AVRational){sdi_info->nr_hanc_symbols + sdi_info->nr_vanc_symbols, audio->nr_ch > 1 ? 2 : 1};
    sdi_samples_per_frame = av_mul_q(sdi_samples_per_line, (AVRational){sdi_info->nr_sdi_lines, 1});
    sdi_rate = av_sdi_rate(sdi_info->picture_rate);

    audio_sample_rate = av_make_q(audio->rate, 1);
    audio_samples_per_frame = av_div_q(audio_sample_rate, sdi_rate);
    audio->af = 1;
    audio->max_af = audio_samples_per_frame.den;
    audio->max_samples_per_line = max_audio_samples_per_line(audio_sample_rate, sdi_info);

    clock_phase_increment = av_div_q(sdi_samples_per_frame, audio_samples_per_frame);
    sample_credit = clock_phase_increment;
    line_credit = sdi_samples_per_line;
    credit = av_sub_q(sample_credit, (AVRational){1,1});

    audio->magic_factor = av_lcm(clock_phase_increment.den, sample_credit.den);
    audio->magic_factor = av_lcm(audio->magic_factor, line_credit.den);
    audio->magic_factor = av_lcm(audio->magic_factor, credit.den);

    clock_phase_increment = av_mul_q(clock_phase_increment, (AVRational){audio->magic_factor, 1});
    sample_credit = av_mul_q(sample_credit, (AVRational){audio->magic_factor, 1});
    line_credit = av_mul_q(line_credit, (AVRational){audio->magic_factor, 1});
    credit = av_mul_q(credit, (AVRational){audio->magic_factor, 1});

    av_assert1(clock_phase_increment.den == 1);
    av_assert1(sample_credit.den == 1);
    av_assert1(line_credit.den == 1);
    av_assert1(credit.den == 1);

    audio->clock_phase = 0;
    audio->clock_phase_increment = clock_phase_increment.num;
    audio->sample_credit = sample_credit.num;
    audio->line_credit = line_credit.num;
    audio->credit = credit.num;
    audio->delayed_samples = 0;

    log_setup(s, audio);

    return 0;
}

/**
 * Clean up
 */
void ff_sdi_audio_free(SdiAudio *audio)
{
    if (audio) {
        av_fifo_freep2(&audio->fifo);
    }
}

void ff_sdi_audio_freep(SdiAudio **audio)
{
    if (audio) {
        ff_sdi_audio_free(*audio);
        *audio = NULL;
    }
}

int ff_sdi_audio_buffered_samples(SdiAudio *audio)
{
    int size = (int)av_fifo_can_read(audio->fifo);
    if (size)
        size /= (audio->bits_per_sample / 8) * audio->nr_ch;
    return size;
}

/**
 * Return 1 if enough audio for the next frame is available in the FIFO
 */
int ff_sdi_audio_enough_for_frame(SdiAudio *audio)
{
    int samples_in_next_frame = 0;
    int credit = audio->credit;
    for (int line = 0; line < audio->sdi_info->nr_sdi_lines; line++) {
        credit += audio->line_credit;
        if (!ff_is_switching_line(audio->sdi_info, line - 1)) {
            int n_samples = 0;
            while (credit > audio->sample_credit &&
                   n_samples < audio->max_samples_per_line) {
                n_samples++;
                credit -= audio->sample_credit;
            }
            samples_in_next_frame += n_samples;
        }
    }
    return ff_sdi_audio_buffered_samples(audio) >= samples_in_next_frame;
}

static int parity_32(uint32_t data)
{
    data ^= data >> 16;
    data ^= data >> 8;
    return PARITY_TABLE256[data & 0xff];
}

static int parity_16(uint32_t data)
{
    data ^= data >> 16;
    data ^= data >> 8;
    return PARITY_TABLE256[data & 0xff];
}

/*
 * Create audio data packet for channel 0-3 according to SMPTE 272M
 */
static size_t write_audio_data_pkt_272m(SdiAudio *audio, uint16_t *out, int nr_samples)
{
    uint16_t *start = out;
    uint32_t sample[4] = { 0, 0, 0, 0 };
    int ch = 0;
    const int did = SDI_DID_AUDIO_DATA_GRP1_ST0272M;
    int z = 0;
    int c = 0;
    int bytes_per_sample = audio->bits_per_sample / 8;
    int minsize = audio->nr_ch * bytes_per_sample;
    int shift = audio->bits_per_sample - 20;

    // Construct ADP header
    *out++ = 0x000;
    *out++ = 0x3ff;
    *out++ = 0x3ff;
    *out++ = did;
    *out++ = PARITY_TABLE256_DATA[audio->dbn[ch]++];
    if (audio->dbn[ch] == 0)
        audio->dbn[ch] = 1;            // counts from 1-255!
    *out++ = PARITY_TABLE256_DATA[nr_samples * 4 * bytes_per_sample];

    while (nr_samples--) {
        // AES status word (and so the bit counter) is common for all channels
        c = AES3_DEFAULT_STATUS[audio->aes_cnt[0] >> 3] & (1 << (audio->aes_cnt[0] & 7)) ? 1 : 0;
        z = audio->aes_cnt[0] == 0 ? 1 : 0;
        if (++audio->aes_cnt[0] >= 192)
            audio->aes_cnt[0] = 0;

        // Get samples from fifo
        if (audio->fifo && av_fifo_can_read(audio->fifo) >= minsize) {
            for (int j = 0; j < audio->nr_ch; j++)
                av_fifo_read(audio->fifo, &sample[j], bytes_per_sample);
            audio->queued_samples -= audio->nr_ch;    // TODO: why do we have this variable anyway?
        }

        for (ch = 0; ch < 4; ch++) {
            int sample_20bit = (sample[ch] >> shift) & 0xfffff;
            uint16_t w1 = z | (ch << 1) | ((sample_20bit & 0x3f) << 3);
            uint16_t w2 = (sample_20bit >> 6) & 0x1ff;
            uint16_t w3 = ((sample_20bit >> 15) & 0x1f) | (c << 7);
            w3 |= parity_16(w1 ^ w2 ^ w3) ? 0x100 : 0;
            *out++ = ff_b9_not_b8(w1);
            *out++ = ff_b9_not_b8(w2);
            *out++ = ff_b9_not_b8(w3);
        }
    }
    *out = ff_calculate_adp_cs(start + 3, out - start - 3);
    out++;

    return out - start;
}

static int get_audio_control_did(int audio_group)
{
    if (audio_group == AUDIO_GRP_1)
        return SDI_DID_AUDIO_CONTROL_GRP1;
    if (audio_group == AUDIO_GRP_2)
        return SDI_DID_AUDIO_CONTROL_GRP2;
    if (audio_group == AUDIO_GRP_3)
        return SDI_DID_AUDIO_CONTROL_GRP3;
    if (audio_group == AUDIO_GRP_4)
        return SDI_DID_AUDIO_CONTROL_GRP4;
    return 0;
}

static int get_audio_data_did(int audio_group)
{
    if (audio_group == AUDIO_GRP_1)
        return SDI_DID_AUDIO_DATA_GRP1;
    if (audio_group == AUDIO_GRP_2)
        return SDI_DID_AUDIO_DATA_GRP2;
    if (audio_group == AUDIO_GRP_3)
        return SDI_DID_AUDIO_DATA_GRP3;
    if (audio_group == AUDIO_GRP_4)
        return SDI_DID_AUDIO_DATA_GRP4;
    return 0;
}

static int get_start_channel(int audio_group)
{
    if (audio_group == AUDIO_GRP_1)
        return 0;
    if (audio_group == AUDIO_GRP_2)
        return 4;
    if (audio_group == AUDIO_GRP_3)
        return 8;
    if (audio_group == AUDIO_GRP_4)
        return 12;
    return 0;
}

static int get_n_channels(const SdiAudio *audio, int audio_group)
{
    if (audio_group == AUDIO_GRP_1)
        return FFMAX(0, FFMIN(audio->nr_ch, 4));
    if (audio_group == AUDIO_GRP_2)
        return FFMAX(4, FFMIN(audio->nr_ch, 8)) - 4;
    if (audio_group == AUDIO_GRP_3)
        return FFMAX(8, FFMIN(audio->nr_ch, 12)) - 8;
    if (audio_group == AUDIO_GRP_4)
        return FFMAX(12, FFMIN(audio->nr_ch, 16)) - 12;
    return 0;
}

/*
 * Create audio control packet for channel 0-3
 */
static size_t write_audio_control_pkt(SdiAudio *audio, uint16_t *out, int group)
{
    uint16_t *start = out;
    const int did = get_audio_control_did(group);
    const int rate = 0; // 48 kHz
    const int act = (1 << get_n_channels(audio, group)) - 1;    // active channel flags

    // Construct packet
    *out++ = 0x000;
    *out++ = 0x3ff;
    *out++ = 0x3ff;
    *out++ = did;
    *out++ = 0x200;                     // DBN, always 0x200
    *out++ = 0x10b;                     // data count, always 0x10B

    *out++ = ff_b9_not_b8(audio->af);
    *out++ = ff_b9_not_b8(rate);
    *out++ = PARITY_TABLE256_DATA[act];

    *out++ = ff_b9_not_b8(0);   // DEL1-2
    *out++ = ff_b9_not_b8(0);   // DEL1-2
    *out++ = ff_b9_not_b8(0);   // DEL1-2
    *out++ = ff_b9_not_b8(0);   // DEL3-4
    *out++ = ff_b9_not_b8(0);   // DEL3-4
    *out++ = ff_b9_not_b8(0);   // DEL3-4

    *out++ = ff_b9_not_b8(0);   // RSRV
    *out++ = ff_b9_not_b8(0);   // RSRV

    *out = ff_calculate_adp_cs(start + 3, out - start - 3);
    out++;

    return out - start;
}

/*
 * Create audio data packet for channel 0-3
 */
static size_t write_audio_data_pkt(SdiAudio *audio, uint16_t *out, int line, int mpf, int phase, int group)
{
    uint16_t *start = out;
    uint32_t sample[4] = { 0, 0, 0, 0 };
    int ch = get_start_channel(group);
    int i = 0;
    const int did = get_audio_data_did(group);
    uint64_t bch = 0;
    int z = 0;
    int c = 0;
    int n_channels = get_n_channels(audio, group);
    int bytes_per_sample = audio->bits_per_sample / 8;
    int minsize =  n_channels * bytes_per_sample;
    int shift = audio->bits_per_sample - 24;

    // Get samples from fifo
    av_assert1(audio->queued_samples >= n_channels);
    if (audio->fifo && av_fifo_can_read(audio->fifo) >= minsize) {
        for (int j = 0; j < n_channels; j++)
            av_fifo_read(audio->fifo, &sample[j], bytes_per_sample);
        audio->queued_samples -= n_channels;    // TODO: why do we have this variable anyway?
    }

    // Construct packet
    *out++ = 0x000;
    *out++ = 0x3ff;
    *out++ = 0x3ff;
    *out++ = did;
    *out++ = PARITY_TABLE256_DATA[audio->dbn[ch]++];
    if (audio->dbn[ch] == 0)
        audio->dbn[ch] = 1;            // counts from 1-255!
    *out++ = PARITY_TABLE256_DATA[24];    // data count, always 24
    *out++ = PARITY_TABLE256_DATA[phase & 0xff];
    *out++ = PARITY_TABLE256_DATA[((phase >> 8) & 0xf) |
            ((phase >> 7) & 0x20) | (mpf ? 0x10 : 0)];

    // AES status word (and so the bit counter) is common per group
    // TODO: check if AES status word should be unique per channel pair.
    c = AES3_DEFAULT_STATUS[audio->aes_cnt[group] >> 3] & (1 << (audio->aes_cnt[group] & 7));
    z = audio->aes_cnt[group] == 0;
    if (++audio->aes_cnt[group] >= 192)
        audio->aes_cnt[group] = 0;

    for (ch = 0; ch < 4; ch++) {
        uint32_t sample_24b = (sample[ch] >> shift) & 0xffffff;
        int p = parity_32(sample_24b ^ c);
        *out++ = PARITY_TABLE256_DATA[((sample_24b << 4) & 0xf0)
                                      | (z && ((ch & 1) == 0) ? 0x8 : 0)];
        *out++ = PARITY_TABLE256_DATA[(sample_24b >> 4) & 0xff];
        *out++ = PARITY_TABLE256_DATA[(sample_24b >> 12) & 0xff];
        *out++ = PARITY_TABLE256_DATA[((sample_24b >> 20) & 0x0f)
                                      | (p ? 0x80 : 0) | (c ? 0x40 : 0)];
    }

    bch = ff_calculate_adp_bch(start, out - start);

    for (i = 0; i < 6; i++)
        *out++ = PARITY_TABLE256_DATA[(bch >> (i * 8)) & 0xff];
    *out = ff_calculate_adp_cs(start + 3, out - start - 3);
    out++;
    return out - start;
}

static void sdi_insert_audio_st299(SdiAudio *audio, uint16_t *ptr, int size,
        int line)
{
    const struct SdiInfo *sdi_info = audio->sdi_info;
    uint16_t *cptr = ptr;       // c-channel
    uint16_t *yptr = ptr + 1;   // y-channel
    int nr_virtch = ff_sdi_get_nr_channels(sdi_info->payload_format);
    int csize = size / nr_virtch;
    int ysize = csize;

    int insert_control = (line == sdi_info->switching_line_field1 + 2);
    if (sdi_info->switching_line_field2 > 0)
        insert_control |= (line == sdi_info->switching_line_field2 + 2);

    if (insert_control && ysize > AUDIO_CONTROL_PKT_SIZE) {
        int group = AUDIO_GRP_1;
        for (int i = audio->nr_ch; i > 0; i-=4) {
            uint16_t buf[18];
            write_audio_control_pkt(audio, buf, group++);
            for (int i = 0; i < AUDIO_CONTROL_PKT_SIZE; i++) {
                *yptr = buf[i];
                yptr += nr_virtch;
            }
            ysize -= AUDIO_CONTROL_PKT_SIZE;
        }
    }

    audio->credit += audio->line_credit;
    if (!ff_is_switching_line(sdi_info, line - 1)) {
        int n_samples = 0;
        while (audio->credit > audio->sample_credit &&
               n_samples < audio->max_samples_per_line) {
            int line_clock_phase = audio->clock_phase % audio->line_credit;
            int mpf = 0;
            int group = AUDIO_GRP_1;
            if (audio->delayed_samples > 0) {
                mpf = 1;
                audio->delayed_samples--;
            }

            for (int i = audio->nr_ch; i > 0; i-=4) {
                uint16_t buffer[AUDIO_DATA_PKT_SIZE];
                write_audio_data_pkt(audio, buffer, line, mpf, line_clock_phase, group++);
                for (int i = 0; i < AUDIO_DATA_PKT_SIZE; i++) {
                    *cptr = buffer[i];
                    cptr += nr_virtch;
                }
            }

            n_samples++;
            audio->credit -= audio->sample_credit;
            audio->clock_phase += audio->clock_phase_increment;
        }
        audio->pkt_cnt += n_samples;
    }
}

static void sdi_insert_audio_272m(SdiAudio *audio, uint16_t *ptr, int size,
        int line)
{
    int nr_samples_to_insert = 0;

    int do_skip = (line == audio->sdi_info->switching_line_field1 + 1) ||
            (line == audio->sdi_info->switching_line_field2 + 1) ||
            (line == audio->sdi_info->error_line_field1) ||
                    (line == audio->sdi_info->error_line_field2) ? 1 : 0;

    audio->credit += audio->line_credit;
    if (audio->nr_ch > 0 && !do_skip) {
        // insert audio data packets as long as the belong to this line, as long
        // as we have space for it and as long as the maximum nr. of packets
        // per line is not reached...
        while (audio->credit > audio->sample_credit &&
                nr_samples_to_insert < audio->max_samples_per_line) {
            audio->credit -= audio->sample_credit;
            nr_samples_to_insert++;
            audio->pkt_cnt++;   // statistics
        }
        ptr += write_audio_data_pkt_272m(audio, ptr, nr_samples_to_insert);
    }
}

void ff_sdi_audio_frame_start(SdiAudio *audio, int64_t pts)
{
    int samples_in_next_frame = 0;
    int credit = audio->credit;
    unsigned char buf[100000];
    for (int line = 0; line < audio->sdi_info->nr_sdi_lines; line++) {
        credit += audio->line_credit;
        if (!ff_is_switching_line(audio->sdi_info, line - 1)) {
            int n_samples = 0;
            while (credit > audio->sample_credit &&
                   n_samples < audio->max_samples_per_line) {
                n_samples++;
                credit -= audio->sample_credit;
            }
            samples_in_next_frame += n_samples;
        }
    }

    ff_sdi_buffer_get_audio(audio->buffer, buf, 32768, pts, samples_in_next_frame);
    av_fifo_write(audio->fifo, buf, samples_in_next_frame * (audio->bits_per_sample / 8) * audio->nr_ch);
    audio->queued_samples += samples_in_next_frame * audio->nr_ch;
}

/**
 * Insert audio data packets. Call every line.
 * We calculate the 'audio phase', the distance to the EAV,
 * for each audio sample. If the phase falls within a line period, and
 * we've less than max_samples_per_line, insert the sample. If a phase is >
 * line time, subtract line time and set the skipped_line flag.
 */
void ff_sdi_audio_insert(SdiAudio *audio, uint16_t *ptr, int size,
        int line)
{
    if (ff_is_sd(audio->sdi_info->payload_format))
        sdi_insert_audio_272m(audio, ptr, size, line);
    else
        sdi_insert_audio_st299(audio, ptr, size, line);
}

/**
 * Call on end of each sdi frame
 */
void ff_sdi_audio_frame_end(SdiAudio *audio)
{
    if (++audio->af > audio->max_af)
        audio->af = 1;
}
