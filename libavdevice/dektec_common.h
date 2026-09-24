/*
 * DekTec hardware common
 * Copyright (c) 2022-2023 DekTec, Jeroen Steendam
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
 * @file dektec_common.h
 * DekTec hardware common
 * @author Jeroen Steendam
 */

#ifndef AVDEVICE_DEKTEC_COMMON_H
#define AVDEVICE_DEKTEC_COMMON_H

#include "avdevice.h"

struct DtHwFuncDesc;
struct AvFifo_IpPars;
struct DtInpChannel;
struct DtOutpChannel;
enum SdiFormat;

/* The threads option, which means what a codec's does: auto, 0, starts
 * FF_DEKTEC_AUTO_THREADS threads and lets CDTAPI divide each frame as its standard calls
 * for, 4 pieces for 2160p50 and 2160p60, 2 for 2160p24 to 2160p30, and 1 up to 3G, where
 * the threads stay idle; 1 is the thread that reads or writes alone. */
#define FF_DEKTEC_THREADS_AUTO 0
#define FF_DEKTEC_AUTO_THREADS 4

int ff_get_hw_funcs(struct DtHwFuncDesc **hw_funcs, int *count);
int ff_dektec_list_devices(struct AVDeviceInfoList *device_list);

int ff_dektec_get_vidstd(enum SdiFormat sdi_fmt);
int ff_dektec_get_linkstd(enum SdiFormat sdi_fmt);
const struct SdiInfo *ff_dektec_get_sdi_info(int vid_std); 

int ff_dektec_parse_url(void *ctx, char *url_arg, int pt_arg,
                        struct AvFifo_IpPars *ippars);

const char *ff_dektec_avfifo_result_to_string(unsigned int result);

/**
 * Give an SDI channel the threads the threads option asks for: FF_DEKTEC_THREADS_AUTO,
 * 1 for none, which converts in the thread that reads or writes, or 2 or more for that
 * many threads and as many pieces a frame.
 *
 * @return 0 on success, a negative AVERROR code, having logged why, on failure
 */
int ff_dektec_give_input_threads(void *ctx, struct DtInpChannel *channel, int threads);
int ff_dektec_give_output_threads(void *ctx, struct DtOutpChannel *channel, int threads);

#endif /* AVDEVICE_DEKTEC_COMMON_H */
