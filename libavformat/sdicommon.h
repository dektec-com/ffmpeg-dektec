/*
 * SDI common definitions
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
 * @file sdicommon.h
 * SDI common definitions
 * @author Werner Damman
 * @author Jeroen Steendam
 */

#ifndef AVFORMAT_SDICOMMON_H
#define AVFORMAT_SDICOMMON_H

#include "avformat.h"
#include "packet_internal.h"
#include "libavutil/fifo.h"
#include "libavutil/rational.h"

#include "cdtapi.h"

/*
 * Types, used in .sdi file header
 */
typedef enum SdiAspectRatio
{
    SDI_AR_4_3 = 0,
    SDI_AR_16_9 = 1
} SdiAspectRatio;

typedef enum SdiPictureRate
{
    SDI_R_23_98 = 2,
    SDI_R_24 = 3,
    SDI_R_47_95 = 4,
    SDI_R_25 = 5,
    SDI_R_29_97 = 6,
    SDI_R_30 = 7,
    SDI_R_48 = 8,
    SDI_R_50 = 9,
    SDI_R_59_94 = 10,
    SDI_R_60 = 11
} SdiPictureRate;

typedef enum SdiScanningMethod
{
    SDI_I_PICT_I_TR = 0,
    SDI_P_PICT_I_TR = 1,
    SDI_I_PICT_P_TR = 2,
    SDI_P_PICT_P_TR = 3
} SdiScanningMethod;

/**
 * Checks if the scanning method has interlaced transport
 *
 * @param scanning_method The scanning method to check
 * @return Returns 1 if the scanning method has interlaced transport otherwise
 * returns 0
 */
int is_interlaced_transport(SdiScanningMethod scanning_method);

/**
 * Checks if the scanning method has interlaced pictures
 *
 * @param scanning_method The scanning method to check
 * @return Returns 1 if the scanning method has interlaced pictures otherwise
 * returns 0
 */
int is_interlaced_picture(SdiScanningMethod scanning_method);

typedef enum SdiSamplingStructure
{
    SDI_SAMPLING_YCbCr422 = 0
} SdiSamplingStructure;

typedef enum SdiBitDepth
{
    SDI_BIT_DEPTH_8 = 0,
    SDI_BIT_DEPTH_10 = 1,
    SDI_BIT_DEPTH_12 = 2
} SdiBitDepth;

typedef enum SdiLineRate
{
    SDI_LINE_RATE_SD = 0,
    SDI_LINE_RATE_ED = 1,
    SDI_LINE_RATE_HD = 2,
    SDI_LINE_RATE_3G = 3,
    SDI_LINE_RATE_6G = 4,
    SDI_LINE_RATE_12G = 5,
    SDI_LINE_RATE_24G = 6,

    SDI_LINE_RATE_NB
} SdiLineRate;

typedef enum SdiInterleavingType
{
    SDI_INTERLEAVING_TYPE_NONE = 0,
    SDI_INTERLEAVING_TYPE_2SI = 1,
    SDI_INTERLEAVING_TYPE_QUADRANT = 2
} SdiInterleavingType;

typedef enum SdiLevel
{
    SDI_LEVEL_NOT_APPLICABLE = 0,
    SDI_LEVEL_A = 1,
    SDI_LEVEL_B_DL = 2,
    SDI_LEVEL_B_DS = 3
} SdiLevel;

typedef enum SdiCompressionMode
{
    SDI_COMPRESSION_MODE_NONE = 0
} SdiCompressionMode;

struct Format
{
    SdiLineRate line_rate;
    SdiInterleavingType interleaving_type;
    SdiLevel sdi_level;
};

struct LogicalFrameProperties
{
    AVRational picture_rate;
    AVRational aspect_ratio;
    int is_interlaced;
    int sampling_structure;
    int is_stereoscopic;
    int bit_depth;
    int picture_width;
    int picture_height;
};

struct PhysicalFieldProperties
{
    int num_lines_field;
    int first_video_line;
    int num_lines_video;
};

struct PhysicalFrameProperties
{
    int num_fields;
    int crc_omitted;
    int num_lines_frame;
    int num_syms_hanc;
    int num_syms_vanc_video;
    struct PhysicalFieldProperties field_properties[2];
};

/**
 * .sdi file header definition
 */
struct SdiFileHeader
{
    uint32_t magic_code;  // shall have the value 0x2e736469 (=".sdi")
    uint8_t version;      // shall have the value 0
    uint16_t header_size; // total size of SdiFileHeader
    uint8_t num_physical_links;
    uint32_t frame_size;
    uint32_t num_frames;
    SdiCompressionMode compression_mode;

    struct Format format;
    struct LogicalFrameProperties logical_frame_properties;
    struct PhysicalFrameProperties physical_frame_properties;
};

#define SDI_MAGIC 0x2e736469

typedef enum SdiFormat {
    SDI_FMT_NONE = -1,

    SDI_FMT_625I50,
    SDI_FMT_525I59_94,

    SDI_FMT_720P23_98,
    SDI_FMT_720P24,
    SDI_FMT_720P25,
    SDI_FMT_720P29_97,
    SDI_FMT_720P30,
    SDI_FMT_720P50,
    SDI_FMT_720P59_94,
    SDI_FMT_720P60,

    SDI_FMT_1080P23_98,
    SDI_FMT_1080P24,
    SDI_FMT_1080P25,
    SDI_FMT_1080P29_97,
    SDI_FMT_1080P30,
    SDI_FMT_1080I50,
    SDI_FMT_1080I59_94,
    SDI_FMT_1080I60,
    SDI_FMT_1080PSF23_98,
    SDI_FMT_1080PSF24,
    SDI_FMT_1080PSF25,
    SDI_FMT_1080PSF29_97,
    SDI_FMT_1080PSF30,

    SDI_FMT_1080P50,
    SDI_FMT_1080P59_94,
    SDI_FMT_1080P60,

    SDI_FMT_2160P23_98,
    SDI_FMT_2160P24,
    SDI_FMT_2160P25,
    SDI_FMT_2160P29_97,
    SDI_FMT_2160P30,

    SDI_FMT_2160P50,
    SDI_FMT_2160P59_94,
    SDI_FMT_2160P60,

    SDI_FMT_NB
} SdiFormat;

/*
 * Struct containing all 'magics' for a specific SDI standard.
 */
struct SdiInfo
{
    const char *name;
    int payload_format;
    SdiScanningMethod scanning_method;
    SdiPictureRate picture_rate;
    SdiAspectRatio aspect_ratio;
    int picture_width;
    int picture_height;
    int nr_sdi_lines;
    int nr_hanc_symbols;
    int nr_vanc_symbols;
    
    int start_line_field1;
    int start_line_field2;
    int end_line_field1;
    int end_line_field2;
    int vid_start_line_field1;
    int vid_start_line_field2;
    int vid_end_line_field1;
    int vid_end_line_field2;

    int switching_line_field1;
    int switching_line_field2;
    int payload_id_line_field1;
    int payload_id_line_field2;
    int error_line_field1;
    int error_line_field2;
};
const struct SdiInfo *av_sdi_info(SdiFormat sdi_fmt);

// Parity lookup table
static const int PARITY_TABLE256[256] =
{
    #define P2(n) n, n^1, n^1, n
    #define P4(n) P2(n), P2(n^1), P2(n^1), P2(n)
    #define P6(n) P4(n), P4(n^1), P4(n^1), P4(n)
            P6(0), P6(1), P6(1), P6(0)
};
#undef P2
#undef P4
#undef P6

static const uint16_t PARITY_TABLE256_DATA[256] =
{
    #define P0(n, v) ((((!(n))+1)<<8) | (v))
    #define P2(n, v) P0(n,v), P0(n^1,v+1), P0(n^1,v+2), P0(n,v+3)
    #define P4(n, v) P2(n,v), P2(n^1,v+4), P2(n^1,v+8), P2(n,v+12)
    #define P6(n, v) P4(n,v), P4(n^1,v+16), P4(n^1,v+32), P4(n,v+48)
            P6(0,0), P6(1,64), P6(1,128), P6(0,192)
};
#undef P0
#undef P2
#undef P4
#undef P6

typedef struct SdiBuffer {
    int max_buffer;
    AVStream *stream;
    int64_t last_pts;
    int64_t last_duration;
    PacketList *queue;

    int64_t fifo_pts;
    struct AVFifo *fifo;
} SdiBuffer;

SdiBuffer *ff_sdi_buffer_alloc(AVStream *stream);
void ff_sdi_buffer_free(SdiBuffer *buffer);
void ff_sdi_buffer_freep(SdiBuffer **buffer);
int ff_sdi_buffer_add(SdiBuffer *buffer, AVPacket *pkt);
int ff_sdi_buffer_contains(SdiBuffer *buffer, int64_t pts, int64_t duration);
int ff_sdi_buffer_get_audio(SdiBuffer *buffer, void *dest, int dest_size, int64_t pts, int n_samples);
int ff_sdi_buffer_get_video(SdiBuffer *buffer, AVPacket *pkt, int64_t pts, int64_t duration);

AVRational av_sdi_rate(SdiPictureRate rate);          // From SdiPictureRate to AVRational
AVRational av_sdi_aspect_ratio(SdiAspectRatio ratio); // From SdiAspectRatio to AVrational
const char *av_sdi_get_scanning_method_name(SdiScanningMethod method);
const char *av_sdi_get_line_rate_name(SdiLineRate rate);

extern const int SDI_FILE_SIGNATURE;            // Magic for SDI files

typedef struct StandardOption {
    int links;
    int standard;
    int lines;
    int scanning_mode;
    SdiPictureRate frame_rate;
} StandardOption;

int av_parse_standard_option(AVFormatContext *s, const char *arg, StandardOption *option);
SdiFormat av_sdi_get_fmt(StandardOption *option);
SdiFormat av_find_matching_standard(AVFormatContext *s, StandardOption *option, AVStream *stream);

/**
 * Return the DTAPI_VIDSTD_ code of a standard of the table, by which CDTAPI's parser
 * and builder know it.
 */
int av_sdi_vidstd(const struct SdiInfo *info);

/*
 * The threads option of the sdi muxer and demuxer. Auto, the default, starts
 * FF_SDI_AUTO_THREADS threads and lets CDTAPI divide a frame as its standard calls for;
 * 1 converts a frame in the calling thread; more start that many threads.
 */
#define FF_SDI_THREADS_AUTO 0
#define FF_SDI_AUTO_THREADS 4

/**
 * Start the worker pool that the threads option asks for. Sets *pool to NULL when the
 * option asks for one thread, and *num_threads to the threads a frame is divided over,
 * 0 for as many as its standard calls for.
 */
int ff_sdi_worker_pool(void *log_ctx, int threads, DtWorkerPool **pool, int *num_threads);

/**
 * Return lowest 9 bits of value, set bit 9 to not bit 8.
 */
uint16_t ff_b9_not_b8(uint16_t value);
/**
 * Calculate 6-byte BCH over 8-bit words
 */
uint64_t ff_calculate_adp_bch(const uint16_t *in, int length);
/**
 * Calculate 9-bit checksum over 10-bit words
 */
uint16_t ff_calculate_adp_cs(const uint16_t *in, int length);
/**
 * Return 1 if format is SD (525/625 lines)
 */
int ff_is_sd(uint32_t format);
/**
 * Return 1 if format is 4k (6G/12G)
 */
int ff_has_sub_images(uint32_t format);
/**
 * Return nr of virtual channels for this format
 */
int ff_sdi_get_nr_channels(uint32_t format);
/**
 * 
 */
int ff_get_sdi_format(int sdi_line_rate, int lines);
/**
 * 
 */
int ff_is_switching_line(const struct SdiInfo *info, int line);
/**
 * 
 */
int ff_is_vbi_line(const struct SdiInfo *info, int line);
/**
 * 
 */
int ff_is_video_line(const struct SdiInfo *info, int line);
/**
 * 
 */
int ff_get_field_nr(const struct SdiInfo *info, int line);

/**
 * Returns the number of audio channels supported for a specific SDI format and
 * audio samplerate combination.
 *
 * @param format SDI format
 * @param rate audio samplerate
 * @return the maximum number of supported audio channels
 */
int ff_sdi_max_audio_channels(int format, int rate);

/*
 * Ancillary data identification words
 */
#define SDI_DID_PAYLOAD_ID           (0x241)
#define SDI_SDID_PAYLOAD_ID          (0x101)

#define SDI_DID_AUDIO_DATA_GRP1_ST0272M (0x2ff)
#define SDI_DID_AUDIO_DATA_GRP2_ST0272M (0x1fd)
#define SDI_DID_AUDIO_DATA_GRP3_ST0272M (0x1fb)
#define SDI_DID_AUDIO_DATA_GRP4_ST0272M (0x2f9)

#define SDI_DID_AUDIO_DATA_GRP1      (0x2e7)
#define SDI_DID_AUDIO_DATA_GRP2      (0x1e6)
#define SDI_DID_AUDIO_DATA_GRP3      (0x1e5)
#define SDI_DID_AUDIO_DATA_GRP4      (0x2e4)
#define SDI_DID_AUDIO_CONTROL_GRP1   (0x1e3)
#define SDI_DID_AUDIO_CONTROL_GRP2   (0x2e2)
#define SDI_DID_AUDIO_CONTROL_GRP3   (0x2e1)
#define SDI_DID_AUDIO_CONTROL_GRP4   (0x1e0)

#endif // AVFORMAT_SDICOMMON_H
