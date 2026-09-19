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
enum SdiFormat;

int ff_get_hw_funcs(struct DtHwFuncDesc **hw_funcs, int *count);
int ff_dektec_list_devices(struct AVDeviceInfoList *device_list);

int ff_dektec_get_vidstd(enum SdiFormat sdi_fmt);
const struct SdiInfo *ff_dektec_get_sdi_info(int vid_std); 

int ff_dektec_parse_url(void *ctx, char *url_arg, int pt_arg,
                        struct AvFifo_IpPars *ippars);

const char *ff_dektec_avfifo_result_to_string(unsigned int result);

#endif /* AVDEVICE_DEKTEC_COMMON_H */
