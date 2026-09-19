/*
 * SDI payload ID packet generation
 * Copyright (c) 2019 DekTec, Werner Damman
 * Copyright (c) 2021 DekTec, Jeroen Steendam
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

#ifndef AVFORMAT_SDIENC_PAYLOADID_H
#define AVFORMAT_SDIENC_PAYLOADID_H

/**
 * Call on every line, inserts payload ID packets according to SMPTE352
 * where applicable.
 * Returns symbols written.
 */
int ff_sdi_insert_payloadid(uint16_t *ptr, int line, const struct SdiInfo *sdi);

#endif /* LIBAVFORMAT_SDIENC_PAYLOADID_H_ */
