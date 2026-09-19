/*
 * SDI payload ID packet generation
 * Copyright (c) 2019 DekTec, Werner Damman
 * Copyright (c) 2021-2023 DekTec, Jeroen Steendam
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
 * SDI payload ID packet generation
 * @author Werner Damman
 * @author Jeroen Steendam
 */

#include "sdicommon.h"
#include "sdienc_payloadid.h"
#include "internal.h"
#include "libavutil/opt.h"

#define PAYLOAD_ID_PACKET_SIZE  11

/*
 * Fill payload identifier fields for SMPTE-352
 */
static void fill_payload_id_pkt_st352(const struct SdiInfo *sdi, uint8_t *fields, int channel_id)
{
    const int sampling_structure = 0; // YCbCr 4:2:2
    const int bit_depth = 1;          // 10 bit quantization

    fields[0] = sdi->payload_format;
    fields[1] = sdi->picture_rate | (sdi->scanning_method << 6);
    fields[2] = sampling_structure | (sdi->aspect_ratio << 7);
    fields[3] = bit_depth | (channel_id << 5);
}

/*
 * Fill payload identifier fields for SMPTE-292-1
 */
static void fill_payload_id_pkt_st292(const struct SdiInfo *sdi, uint8_t *fields)
{
    const int transfer_characteristics = 0; // SDR-TV
    const int horizontal_size = 0;          // 1920 pixels
    const int colorimetry = 0;              // BT.709
    const int sampling_structure = 0;       // YCbCr 4:2:2
    const int color_difference = 0;         // YCbCr
    const int bit_depth = 1;                // 10 bit quantization

    if (sdi->picture_height == 720) {
        fields[0] = sdi->payload_format;
        fields[1] = sdi->picture_rate | ((sdi->scanning_method & 1) << 6);
        fields[2] = sampling_structure;
        fields[3] = bit_depth;
    }
    else if (sdi->picture_height == 1080) {
        fields[0] = sdi->payload_format;
        fields[1] = sdi->picture_rate | (transfer_characteristics << 4) | (sdi->scanning_method << 6);
        fields[2] = sampling_structure | ((colorimetry & 1) << 4) | (sdi->aspect_ratio << 5) | (horizontal_size << 6) | ((colorimetry >> 1) << 7);
        fields[3] = bit_depth | (color_difference << 4);
    }
    else {
        memset(fields, 0, 4);
    }
}

/*
 * Fill payload identifier fields for SMPTE-425-1
 */
static void fill_payload_id_pkt_st425(const struct SdiInfo *sdi, uint8_t *fields)
{
    const int transfer_characteristics = 0; // SDR-TV
    const int horizontal_size = 0;          // 1920 pixels
    const int colorimetry = 0;              // BT.709
    const int sampling_structure = 0;       // YCbCr 4:2:2
    const int color_difference = 0;         // YCbCr
    const int bit_depth = 1;                // 10 bit quantization

    fields[0] = sdi->payload_format;
    fields[1] = sdi->picture_rate | (transfer_characteristics << 4) | (sdi->scanning_method << 6);
    fields[2] = sampling_structure | (colorimetry << 4) | (horizontal_size << 6) | (sdi->aspect_ratio << 7);
    fields[3] = bit_depth | (color_difference << 4);
}

/*
 * Fill payload identifier fields for SMPTE-2081-10
 */
static void fill_payload_id_pkt_st2081(const struct SdiInfo *sdi, uint8_t *fields)
{
    const int transfer_characteristics = 0; // SDR-TV
    const int horizontal_size = 0;          // 1920 pixels
    const int colorimetry = 0;              // BT.709
    const int sampling_structure = 0;       // YCbCr 4:2:2
    const int link_assignment = 0;          // Single link
    const int color_difference = 0;         // YCbCr
    const int audio_copy_status = 0;        // Audio is not copied
    const int bit_depth = 1;                // 10 bit quantization

    fields[0] = sdi->payload_format;
    fields[1] = sdi->picture_rate | (transfer_characteristics << 4) | (sdi->scanning_method << 6);
    fields[2] = sampling_structure | (colorimetry << 4) | (horizontal_size << 6) | (sdi->aspect_ratio << 7);
    fields[3] = bit_depth | (audio_copy_status << 2) | (color_difference << 4) | (link_assignment << 5);
}

/*
 * Fill payload identifier fields for SMPTE-2082-10
 */
static void fill_payload_id_pkt_st2082(const struct SdiInfo *sdi, uint8_t *fields)
{
    const int transfer_characteristics = 0; // SDR-TV
    const int horizontal_size = 0;          // 1920 pixels
    const int colorimetry = 0;              // BT.709
    const int sampling_structure = 0;       // YCbCr 4:2:2
    const int link_assignment = 0;          // Single link
    const int color_difference = 0;         // YCbCr
    const int audio_copy_status = 0;        // Audio is not copied
    const int bit_depth = 1;                // 10 bit quantization

    fields[0] = sdi->payload_format;
    fields[1] = sdi->picture_rate | (transfer_characteristics << 4) | (sdi->scanning_method << 6);
    fields[2] = sampling_structure | (colorimetry << 4) | (horizontal_size << 6) | (sdi->aspect_ratio << 7);
    fields[3] = bit_depth | (audio_copy_status << 2) | (color_difference << 4) | (link_assignment << 5);
}
/*
 * Fill payload identifier fields according to standard
 */
static void fill_payload_id_pkt(const struct SdiInfo *sdi, uint8_t *fields, int channel_id)
{
    if (sdi->payload_format == 0x84 || sdi->payload_format == 0x85) {
        fill_payload_id_pkt_st292(sdi, fields);
    }
    else if (sdi->payload_format == 0x89) {
        fill_payload_id_pkt_st425(sdi, fields);
    }
    else if (sdi->payload_format == 0xC0) {
        fill_payload_id_pkt_st2081(sdi, fields);
    }
    else if (sdi->payload_format == 0xCE) {
        fill_payload_id_pkt_st2082(sdi, fields);
    }
    else {
        fill_payload_id_pkt_st352(sdi, fields, channel_id);
    }
}

/*
 * Construct Payload ID packet according to SMPTE-352
 */
static size_t write_payload_id_pkt(const struct SdiInfo *sdi, uint16_t *out,
        int channel_id)
{
    const uint16_t *start = out;
    uint8_t fields[4] = {0};

    // Construct packet
    *out++ = 0x000;
    *out++ = 0x3ff;
    *out++ = 0x3ff;
    *out++ = SDI_DID_PAYLOAD_ID;
    *out++ = SDI_SDID_PAYLOAD_ID;
    *out++ = PARITY_TABLE256_DATA[4]; // data count

    fill_payload_id_pkt(sdi, fields, channel_id);

    *out++ = PARITY_TABLE256_DATA[fields[0]];
    *out++ = PARITY_TABLE256_DATA[fields[1]];
    *out++ = PARITY_TABLE256_DATA[fields[2]];
    *out++ = PARITY_TABLE256_DATA[fields[3]];

    *out = ff_calculate_adp_cs(start + 3, out - start - 3);
    out++;
    return out - start;
}

int ff_sdi_insert_payloadid(uint16_t *ptr, int line, const struct SdiInfo *sdi)
{
    const uint16_t *start = ptr;
    const int nr_virtch = ff_sdi_get_nr_channels(sdi->payload_format);

    if (line == sdi->payload_id_line_field1 ||
        line == sdi->payload_id_line_field2) {
        uint16_t buf[PAYLOAD_ID_PACKET_SIZE];

        // Generate packet. All virtual channels contain the same payload ID
        // packet for now. However, for multi-link formats we need a packet
        // per link.
        write_payload_id_pkt(sdi, buf, 0);
        for (int i = 0; i < PAYLOAD_ID_PACKET_SIZE; i++) {
            for (int ch = 0; ch < nr_virtch; ch++) {
                *ptr++ = buf[i];
            }
        }
    }
    return ptr - start;
}
