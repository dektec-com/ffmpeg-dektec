/*
 * DekTec hardware: NMOS
 * Copyright (c) 2026 DekTec
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
 * @file dektec_nmos.h
 * Makes the SMPTE ST 2110 streams of a DekTec port visible to an NMOS controller.
 *
 * An input of the dektec device that is given nmos_registry opens an NMOS node, with the
 * port as its device and each stream as a receiver. A controller connects and disconnects
 * the receivers over IS-05. The node calls back on a thread of its own, but only the
 * thread that reads the FIFOs may change them, so the change waits in a mailbox until
 * that thread calls ff_dektec_nmos_poll(). The steps:
 *
 *   1. ff_dektec_nmos_open() in read_header, once the FIFOs are attached;
 *   2. ff_dektec_nmos_add_receiver() for each stream, with its address, or without one
 *      for a stream whose URL is nmos;
 *   3. ff_dektec_nmos_wait() until a controller has connected every receiver that has
 *      no address;
 *   4. ff_dektec_nmos_set_stream() for each stream, once it is open;
 *   5. ff_dektec_nmos_poll() often from read_packet, also while it waits for data;
 *   6. ff_dektec_nmos_close() in read_close, before the FIFOs stop.
 *
 * A stream's format is fixed once it is open: a controller may move a receiver to
 * another stream of the same format, or disable and enable it, but an activation of
 * another format is refused, with a message that names the format the receiver takes.
 */

#ifndef AVDEVICE_DEKTEC_NMOS_H
#define AVDEVICE_DEKTEC_NMOS_H

#include <stdint.h>

#include "libavformat/avformat.h"

#include "cdtapi.h"
#include "cdtapi_avfifo.h"

typedef struct FFDektecNmos FFDektecNmos;

/**
 * The options that make a node, as the user gives them.
 */
typedef struct FFDektecNmosOptions {
    const char *registry; ///< The base URL of a Registration API, or "auto" to search
    const char *label;    ///< The node's label; NULL or empty for ffmpeg-<serial>:<port>
    const char *host;     ///< The address the node's APIs are reached at; NULL to find it
    int api_port;         ///< The port of the node's APIs; 0 for any free port
} FFDektecNmosOptions;

/**
 * Open an NMOS node for a port, register the port as its NMOS device, and start serving
 * the node's APIs.
 *
 * @param log_ctx  where to log, e.g. the AVFormatContext
 * @param options  the node's options; options->registry must not be empty
 * @param device   the attached device
 * @param serial   the device's serial number, for the default label
 * @param port     the port, counted from 1
 * @param nmos     set to the new node; NULL after a failure
 * @return 0, or a negative AVERROR after logging why
 */
int ff_dektec_nmos_open(void *log_ctx, const FFDektecNmosOptions *options,
                        DtDevice *device, int64_t serial, int port, FFDektecNmos **nmos);

/**
 * Register a receive FIFO as an NMOS receiver of the port. Call it once for each stream,
 * in the order of the streams. Its label is the node's label followed by name, e.g.
 * "video" or "audio 0", so that its ID stays the same each time the same command runs.
 *
 * @param nmos    the node
 * @param fifo    the FIFO, attached; it is changed only from ff_dektec_nmos_poll()
 * @param media   video or audio
 * @param ippars  the stream the FIFO receives, from its URL, which the node gives as
 *                what the receiver receives; NULL for a stream whose URL is nmos, which
 *                waits for a controller to connect it
 * @param name    the receiver's name within the node
 * @return 0, or a negative AVERROR after logging why
 */
int ff_dektec_nmos_add_receiver(FFDektecNmos *nmos, AvFifo_RxFifo *fifo,
                                enum AVMediaType media, const AvFifo_IpPars *ippars,
                                const char *name);

/**
 * Wait until a controller has connected every receiver registered without an address,
 * applying each connection as it comes, so that its FIFO knows what to receive.
 *
 * @param nmos         the node
 * @param timeout_us   how long to wait, in microseconds; negative without limit
 * @param interrupted  returns nonzero when the user stops the program
 * @param opaque       passed to interrupted
 * @return 0; AVERROR(ETIMEDOUT) after logging the receivers that were not connected;
 *         or AVERROR_EXIT when interrupted
 */
int ff_dektec_nmos_wait(FFDektecNmos *nmos, int64_t timeout_us,
                        int (*interrupted)(void *opaque), void *opaque);

/**
 * Fix the format of a receiver once its stream is open: from then on, an activation of
 * another format is refused.
 *
 * @param nmos    the node; NULL does nothing
 * @param index   the stream's index, which is the receiver's place in the order of
 *                ff_dektec_nmos_add_receiver()
 * @param format  the frame format the FIFO delivers video in; not used for audio
 * @param st      the stream; its codec parameters are the format to keep
 */
void ff_dektec_nmos_set_stream(FFDektecNmos *nmos, int index, St2110_RxFrameFormat format,
                               const AVStream *st);

/**
 * Apply the change a controller asked for, if one waits. Call it from the thread that
 * reads the FIFOs, between two packets, and also while waiting for data, so that a
 * disabled receiver can be enabled again.
 *
 * @param nmos  the node; NULL does nothing
 * @return the stream index of the FIFO that was changed, or -1 when none was or the
 *         stream is not open yet
 */
int ff_dektec_nmos_poll(FFDektecNmos *nmos);

/**
 * Remove the node's registrations, stop serving its APIs and free it. A controller's
 * request that waits for ff_dektec_nmos_poll() is answered with an error.
 *
 * @param nmos  the node, set to NULL; NULL does nothing
 */
void ff_dektec_nmos_close(FFDektecNmos **nmos);

#endif /* AVDEVICE_DEKTEC_NMOS_H */
