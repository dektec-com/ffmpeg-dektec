/*
 * DekTec hardware common
 * Copyright (c) 2022-2024 DekTec, Jeroen Steendam
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
 * @file dektec_common.c
 * DekTec hardware common
 * @author Jeroen Steendam
 */

#include "dektec_common.h"
#include "libavformat/network.h"
#include "libavformat/sdicommon.h"
#include "libavformat/internal.h"
#include "libavutil/avstring.h"
#include "libavutil/bswap.h"
#include "libavutil/mem.h"
#include "libavformat/ip.h"
#include "cdtapi.h"
#include "cdtapi_avfifo.h"

static const int sdi_fmt_to_vidstd[SDI_FMT_NB] = {
    [SDI_FMT_625I50] = DTAPI_VIDSTD_625I50,
    [SDI_FMT_525I59_94] = DTAPI_VIDSTD_525I59_94,
    [SDI_FMT_720P23_98] = DTAPI_VIDSTD_720P23_98,
    [SDI_FMT_720P24] = DTAPI_VIDSTD_720P24,
    [SDI_FMT_720P25] = DTAPI_VIDSTD_720P25,
    [SDI_FMT_720P29_97] = DTAPI_VIDSTD_720P29_97,
    [SDI_FMT_720P30] = DTAPI_VIDSTD_720P30,
    [SDI_FMT_720P50] = DTAPI_VIDSTD_720P50,
    [SDI_FMT_720P59_94] = DTAPI_VIDSTD_720P59_94,
    [SDI_FMT_720P60] = DTAPI_VIDSTD_720P60,
    [SDI_FMT_1080P23_98] = DTAPI_VIDSTD_1080P23_98,
    [SDI_FMT_1080P24] = DTAPI_VIDSTD_1080P24,
    [SDI_FMT_1080P25] = DTAPI_VIDSTD_1080P25,
    [SDI_FMT_1080P29_97] = DTAPI_VIDSTD_1080P29_97,
    [SDI_FMT_1080P30] = DTAPI_VIDSTD_1080P30,
    [SDI_FMT_1080I50] = DTAPI_VIDSTD_1080I50,
    [SDI_FMT_1080I59_94] = DTAPI_VIDSTD_1080I59_94,
    [SDI_FMT_1080I60] = DTAPI_VIDSTD_1080I60,
    [SDI_FMT_1080PSF23_98] = DTAPI_VIDSTD_1080PSF23_98,
    [SDI_FMT_1080PSF24] = DTAPI_VIDSTD_1080PSF24,
    [SDI_FMT_1080PSF25] = DTAPI_VIDSTD_1080PSF25,
    [SDI_FMT_1080PSF29_97] = DTAPI_VIDSTD_1080PSF29_97,
    [SDI_FMT_1080PSF30] = DTAPI_VIDSTD_1080PSF30,
    [SDI_FMT_1080P50] = DTAPI_VIDSTD_1080P50,
    [SDI_FMT_1080P59_94] = DTAPI_VIDSTD_1080P59_94,
    [SDI_FMT_1080P60] = DTAPI_VIDSTD_1080P60,
    [SDI_FMT_2160P23_98] = DTAPI_VIDSTD_2160P23_98,
    [SDI_FMT_2160P24] = DTAPI_VIDSTD_2160P24,
    [SDI_FMT_2160P25] = DTAPI_VIDSTD_2160P25,
    [SDI_FMT_2160P29_97] = DTAPI_VIDSTD_2160P29_97,
    [SDI_FMT_2160P30] = DTAPI_VIDSTD_2160P30,
    [SDI_FMT_2160P50] = DTAPI_VIDSTD_2160P50,
    [SDI_FMT_2160P59_94] = DTAPI_VIDSTD_2160P59_94,
    [SDI_FMT_2160P60] = DTAPI_VIDSTD_2160P60,
};

int ff_get_hw_funcs(DtHwFuncDesc **hw_funcs, int *count)
{
    int n_elements_out = 0;
    unsigned int result = 0;
    result = DtapiHwFuncScan(*count, &n_elements_out, *hw_funcs); 
    if (result != DTAPI_E_BUF_TOO_SMALL) 
        return -1; 
    *hw_funcs = av_mallocz(sizeof(DtHwFuncDesc) * n_elements_out); 
    *count = n_elements_out; 
    result = DtapiHwFuncScan(*count, &n_elements_out, *hw_funcs); 
    if (result != DTAPI_OK) 
        return -1;
    return 1;
}

int ff_dektec_list_devices(struct AVDeviceInfoList *device_list)
{
    int ret = 0;
    int n_hw_funcs = 0;
    DtHwFuncDesc *hw_funcs = NULL;
    AVDeviceInfo *new_device = NULL;

    ret = ff_get_hw_funcs(&hw_funcs, &n_hw_funcs);
    if (ret < 0)
        return -1;

    for (int i = 0; i < n_hw_funcs; i++) {
        if (!hw_funcs[i].IsSdi && !hw_funcs[i].IsAvFifo)
            continue;

        if (!hw_funcs[i].IsInput && !hw_funcs[i].IsOutput && !hw_funcs[i].IsAvFifo)
            continue;
        

        new_device = (AVDeviceInfo *) av_mallocz(sizeof(AVDeviceInfo));
        new_device->device_name = av_strdup(hw_funcs[i].DeviceName);
        new_device->device_description = av_strdup(hw_funcs[i].Description);
        ret = av_dynarray_add_nofree(&device_list->devices, &device_list->nb_devices, new_device);
        if (ret < 0)
            return ret;
    }

    av_freep(&hw_funcs);

    return 1;
}

int ff_dektec_get_vidstd(SdiFormat sdi_fmt)
{
    if (sdi_fmt < 0 || sdi_fmt >= SDI_FMT_NB)
        return DTAPI_VIDSTD_UNKNOWN;
    return sdi_fmt_to_vidstd[sdi_fmt];
}

/*
 * How 4K is carried, as DtapiVidStd2IoStd takes it: 2 is one 6G link and 3 is one 12G
 * link, which the payload format tells apart, 0xc0 being 6G and 0xce 12G. A standard
 * that is not 4K has no link standard and gives -1.
 */
int ff_dektec_get_linkstd(SdiFormat sdi_fmt)
{
    const struct SdiInfo *info;

    if (sdi_fmt < 0 || sdi_fmt >= SDI_FMT_NB)
        return -1;
    info = av_sdi_info(sdi_fmt);
    if (!ff_has_sub_images(info->payload_format))
        return -1;
    return info->payload_format == 0xce ? 3 : 2;
}

const struct SdiInfo *ff_dektec_get_sdi_info(int vid_std)
{
    for (SdiFormat sdi_fmt = 0; sdi_fmt < SDI_FMT_NB; sdi_fmt++) {
        if (sdi_fmt_to_vidstd[sdi_fmt] == vid_std)
            return av_sdi_info(sdi_fmt);
    }
    return NULL;
}

int ff_dektec_parse_url(void *ctx, char *url_arg, int pt_arg,
                        struct AvFifo_IpPars *ippars)
{
    char proto[10], authorization[1024];
    struct addrinfo *ai = NULL, *src_ai = NULL;
    char hostname[1024], src_hostname[1024];
    int port = -1, src_port = -1;
    char *url = NULL;

    if (!strstr(url_arg, "://"))
        url = av_asprintf("rtp://%s", url_arg);
    else
        url = av_strdup(url_arg);
    av_url_split(proto, sizeof(proto), authorization, sizeof(authorization),
                 hostname, sizeof(hostname), &port, NULL, 0, url);
    ai = ff_ip_resolve_host(ctx, hostname, port, SOCK_DGRAM, AF_UNSPEC, 0);
    av_freep(&url);
    if (!ai)
        return AVERROR(EIO);

    if (strlen(authorization)) {
        if (!strstr(authorization, "://"))
            url = av_asprintf("rtp://%s", authorization);
        else
            url = av_strdup(authorization);
        av_url_split(NULL, 0, NULL, 0, src_hostname, sizeof(src_hostname),
                     &src_port, NULL, 0, url);
        src_ai = ff_ip_resolve_host(ctx, src_hostname, src_port, SOCK_DGRAM,
                                    AF_UNSPEC, 0);
        av_freep(&url);
        if (!src_ai)
            return AVERROR(EIO);
    }

    if (!strcmp(proto, "rtp")) {
        if (pt_arg < 96 || pt_arg > 127) {
            av_log(ctx, AV_LOG_ERROR,
                   "Invalid RTP payload type %d. RTP payload type should "
                   "be in the range of 96-127\n",
                   pt_arg);
            return AVERROR(EINVAL);
        }
        ippars->TransportProtocol = IpTransportProtocol_Rtp;
        ippars->RtpPayloadType = pt_arg;
    } else {
        av_log(ctx, AV_LOG_ERROR, "Invalid protocol: %s\n", proto);
        return AVERROR(EINVAL);
    }

    if (port <= 0 || port >= 65536) {
        av_log(ctx, AV_LOG_ERROR, "Port is missing or invalid\n");
        return AVERROR(EINVAL);
    }
    ippars->Port = port;

    // log_addrinfo(ctx, ai, "URL resolved to");

    if (src_ai) {
        ippars->SrcFlt = av_mallocz(sizeof(IpSrcFlt));
        ippars->NSrcFlt = 1;
        ippars->SrcFlt->Port = src_port;
        if (src_ai->ai_family == PF_INET) {
            struct sockaddr_in *addr = (struct sockaddr_in *)src_ai->ai_addr;
            memcpy(ippars->SrcFlt->IpAddr, &addr->sin_addr,
                   sizeof(struct in_addr));
        } else if (src_ai) {
            av_log(ctx, AV_LOG_ERROR, "Invalid source address\n");
            return AVERROR(EIO);
        }
    }

    if (ai->ai_family == PF_INET) {
        uint32_t first_nmos_mc = 0, last_nmos_mc = 0, address = 0;
        struct sockaddr_in *addr = (struct sockaddr_in *)ai->ai_addr;

        // The multicast range 224.0.0.0-224.0.1.255 is reserved for NMOS
        first_nmos_mc = 224 << 24;               // 224.0.0.0
        last_nmos_mc = 224 << 24 | 1 << 8 | 255; // 224.0.1.255
        address = av_bswap32(addr->sin_addr.s_addr);
        if (address >= first_nmos_mc && address <= last_nmos_mc) {
            av_log(ctx, AV_LOG_ERROR,
                   "Address %d.%d.%d.%d is reserved for NMOS\n",
                   (address >> 24) & 0xFF,
                   (address >> 16) & 0xFF,
                   (address >>  8) & 0xFF,
                   (address >>  0) & 0xFF);
            return AVERROR(EINVAL);
        }
        memcpy(ippars->IpAddr, &addr->sin_addr, sizeof(struct in_addr));
        ippars->IpVersion = IpProtocolVersion_IPv4;
    } else {
        av_log(ctx, AV_LOG_ERROR, "Invalid address\n");
        return AVERROR(EIO);
    }
    
    av_log(ctx, AV_LOG_VERBOSE, "URL=%s:%d\n", hostname, port);
    if (src_ai)
        av_log(ctx, AV_LOG_VERBOSE, "  SourceURL=%s:%d\n", src_hostname,
               src_port);
    av_log(ctx, AV_LOG_VERBOSE, "  RTP_payload_type=%d\n", pt_arg);

    return 1;
}

const char *ff_dektec_avfifo_result_to_string(unsigned int result)
{
    if (result == DTAPI_E_EXCEPTION) {
        return GetLastException();
    }
    else {
        return DtapiResult2Str(result);
    }
}
