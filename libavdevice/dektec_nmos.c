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
 * @file dektec_nmos.c
 * Makes the SMPTE ST 2110 streams of a DekTec port visible to an NMOS controller.
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/avstring.h"
#include "libavutil/error.h"
#include "libavutil/log.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"
#include "libavutil/thread.h"
#include "libavutil/time.h"
#include "libavformat/network.h"

#include "cdtapi_nmos.h"
#include "dtnmos_http.h"

#include "dektec_nmos.h"

/* The most receivers a node holds: a video stream and eight audio streams. */
#define MAX_RECEIVERS 9

/* How long a controller's request waits for read_packet to apply it, in microseconds.
 * A controller times out after a few seconds itself. */
#define ANSWER_TIMEOUT_US 3000000

/* The namespace the node's ID is made in, a version 4 UUID of this device's own, so
 * that the same host and label give the same node each time. */
static const DtNmosId nmos_namespace = { "7b1e4c92-3d58-4f0a-a6c7-91e2f5d8b304" };

/* A receiver: its FIFO, and the format a controller's activation must keep once the
 * stream is open. Before that, an activation only gives the FIFO its address. */
typedef struct Receiver {
    FFDektecNmos *nmos;            ///< The node it belongs to
    AvFifo_RxFifo *fifo;           ///< The FIFO a change is applied to
    int stream_index;              ///< The index of the stream the FIFO feeds
    int has_address;               ///< 1 once the FIFO knows what to receive
    int configured;                ///< 1 once the stream is open and its format fixed
    St2110_RxFrameFormat format;   ///< The frame format the FIFO delivers video in
    enum AVMediaType media;        ///< Video or audio
    int width, height;             ///< Video: the frame size
    AVRational frame_rate;         ///< Video: frames per second
    int interlaced;                ///< Video: 1 for interlaced
    int depth;                     ///< Video: bits per sample
    int sample_rate;               ///< Audio: samples per second
    int channels;                  ///< Audio: the number of channels
    int bits;                      ///< Audio: 16 or 24
    char name[64];                 ///< The receiver's name, for messages
} Receiver;

/* A sender: its FIFO, and whether a controller has it send. */
typedef struct Sender {
    FFDektecNmos *nmos;            ///< The node it belongs to
    AvFifo_TxFifo *fifo;           ///< The FIFO a change is applied to
    int stream_index;              ///< The index of the stream that feeds the FIFO
    int sending;                   ///< 1 while it sends; 0 while a controller disables it
    char name[64];                 ///< The sender's name, for messages
} Sender;

/* Where a request of the node's thread to read_packet's thread stands. */
enum MailState {
    MAIL_EMPTY,    ///< No request
    MAIL_POSTED,   ///< A request waits for ff_dektec_nmos_poll()
    MAIL_TAKEN,    ///< ff_dektec_nmos_poll() is applying it
    MAIL_ANSWERED, ///< Applied; the answer waits for the node's thread
};

struct FFDektecNmos {
    void *log_ctx;                 ///< Where to log
    DtNmosNode *node;              ///< The node
    DtNmosRegistrySearch *search;  ///< The search for registries, with "auto"; or NULL
    DtNmosId device_id;            ///< The port's NMOS device
    DtDevice *device;              ///< The card, for the clock of its port
    int port;                      ///< The port, counting from 1
    int64_t clock_due;             ///< When to ask for the port's clock again
    char label[128];               ///< The node's label; receivers' labels start with it
    Receiver receivers[MAX_RECEIVERS];
    int nb_receivers;
    Sender senders[MAX_RECEIVERS]; ///< Of an output, as many as an input has receivers
    int nb_senders;

    /* The mailbox, guarded by lock: one request at a time, from the node's thread. */
    AVMutex lock;
    AVCond changed;                ///< Signalled when state changes
    enum MailState state;
    int closing;                   ///< 1 once ff_dektec_nmos_close() has begun
    int abandoned;                 ///< 1 when the asker stopped waiting for a taken request
    Receiver *receiver;            ///< The receiver the request is for, or NULL
    DtNmosAvFifoRxChange change;   ///< The change to apply to it
    Sender *sender;                ///< The sender the request is for, or NULL
    DtNmosAvFifoTxChange tx_change; ///< The change to apply to it
    int made_open;                 ///< 1 when the change was made for an open stream
    unsigned int result;           ///< The result of applying it, DTAPI_OK or an error
    char message[256];             ///< Why it failed
};

/**
 * Pass a message of the node to FFmpeg's log, at the matching level.
 */
static void log_message(void *user, DtNmosLogLevel level, const char *message)
{
    static const int levels[] = { AV_LOG_DEBUG, AV_LOG_VERBOSE, AV_LOG_WARNING, AV_LOG_ERROR };
    FFDektecNmos *nmos = user;
    int av_level = (unsigned)level < FF_ARRAY_ELEMS(levels) ? levels[level] : AV_LOG_INFO;
    av_log(nmos->log_ctx, av_level, "NMOS: %s\n", message);
}

/**
 * Compute the absolute time, as pthread_cond_timedwait() takes it, timeout_us from now.
 */
static struct timespec deadline_after(int64_t timeout_us)
{
    int64_t at = av_gettime() + timeout_us;
    struct timespec ts = { at / 1000000, (at % 1000000) * 1000 };
    return ts;
}

/**
 * Ask the thread that owns the FIFOs, read_packet's or write_packet's, to apply a
 * change, and wait for its answer. Runs on the node's thread. The change is change for
 * receiver r, made for the stream's fixed format when made_open is 1 and before the
 * stream was open otherwise; or tx for sender snd.
 *
 * @return DTNMOS_OK, or an error that the node passes to the controller
 */
static DtNmosResult ask_owner(FFDektecNmos *nmos, Receiver *r,
                              const DtNmosAvFifoRxChange *change, int made_open,
                              Sender *snd, const DtNmosAvFifoTxChange *tx)
{
    struct timespec deadline = deadline_after(ANSWER_TIMEOUT_US);
    DtNmosResult ret = DTNMOS_OK;
    char message[sizeof(nmos->message)];

    ff_mutex_lock(&nmos->lock);
    while (!nmos->closing && nmos->state != MAIL_EMPTY) {
        if (ff_cond_timedwait(&nmos->changed, &nmos->lock, &deadline)) {
            ff_mutex_unlock(&nmos->lock);
            return DtNmos_SetLastError(DTNMOS_E_TIMEOUT, "Another change is still waiting");
        }
    }
    if (nmos->closing) {
        ff_mutex_unlock(&nmos->lock);
        return DtNmos_SetLastError(DTNMOS_E_STATE, "FFmpeg is closing the device");
    }
    nmos->receiver = r;
    nmos->sender = snd;
    if (r)
        nmos->change = *change;
    else
        nmos->tx_change = *tx;
    nmos->made_open = made_open;
    nmos->state = MAIL_POSTED;
    ff_cond_broadcast(&nmos->changed);

    while (!nmos->closing && nmos->state != MAIL_ANSWERED) {
        if (ff_cond_timedwait(&nmos->changed, &nmos->lock, &deadline))
            break;
    }
    if (nmos->state == MAIL_ANSWERED) {
        if (nmos->result != DTAPI_OK) {
            av_strlcpy(message, nmos->message, sizeof(message));
            ret = DTNMOS_E_STATE;
        }
        nmos->state = MAIL_EMPTY;
    } else if (nmos->state == MAIL_POSTED) {
        /* Not taken in time: withdraw it, so that it is never applied. */
        nmos->state = MAIL_EMPTY;
        av_strlcpy(message, nmos->closing ? "FFmpeg is closing the device"
                                          : "FFmpeg did not take the change in time",
                   sizeof(message));
        ret = DTNMOS_E_TIMEOUT;
    } else {
        /* Taken but not answered: it is being applied, and poll empties the mailbox. */
        nmos->abandoned = 1;
        av_strlcpy(message, "FFmpeg did not apply the change in time", sizeof(message));
        ret = DTNMOS_E_TIMEOUT;
    }
    ff_cond_broadcast(&nmos->changed);
    ff_mutex_unlock(&nmos->lock);
    return ret == DTNMOS_OK ? DTNMOS_OK : DtNmos_SetLastError(ret, message);
}

/**
 * Check that an activation's flow has the format receiver r is fixed to.
 *
 * @return 1 when it has, 0 when not, with why in message
 */
static int same_format(const Receiver *r, const DtNmosFlow *flow, char *message, size_t size)
{
    if (r->media == AVMEDIA_TYPE_VIDEO) {
        const DtNmosVideoFormat *v = &flow->Format.Video;
        AVRational rate = { (int)v->RateNumerator,
                            v->RateDenominator ? (int)v->RateDenominator : 1 };
        /* A PsF flow is progressive to the demuxer, which counts its fields as frames. */
        int interlaced = v->Interlaced && !v->Segmented;
        if (v->Segmented)
            rate = av_mul_q(rate, av_make_q(2, 1));
        if (flow->Media != DTNMOS_MEDIA_VIDEO) {
            snprintf(message, size, "Receiver %s takes video", r->name);
            return 0;
        }
        /* An SDP without exactframerate gives no rate; the frames tell it then. */
        if ((int)v->Width == r->width && (int)v->Height == r->height &&
            (!v->RateNumerator || av_cmp_q(rate, r->frame_rate) == 0) &&
            interlaced == !!r->interlaced && (int)v->Depth == r->depth)
            return 1;
        snprintf(message, size,
                 "Receiver %s takes %dx%d%s at %d/%d fps with %d bits, as it was opened; "
                 "the flow is %ux%u%s with %u bits",
                 r->name, r->width, r->height, r->interlaced ? "i" : "p",
                 r->frame_rate.num, r->frame_rate.den, r->depth, v->Width, v->Height,
                 v->Interlaced ? "i" : "p", v->Depth);
        if (v->RateNumerator)
            av_strlcatf(message, size, " at %u/%u fps", v->RateNumerator,
                        v->RateDenominator);
        return 0;
    } else {
        const DtNmosAudioFormat *a = &flow->Format.Audio;
        int bits = a->Encoding == DTNMOS_AUDIO_ENCODING_L16 ? 16
                 : a->Encoding == DTNMOS_AUDIO_ENCODING_L24 ? 24 : 0;
        if (flow->Media != DTNMOS_MEDIA_AUDIO) {
            snprintf(message, size, "Receiver %s takes audio", r->name);
            return 0;
        }
        if ((int)a->SampleRate == r->sample_rate && (int)a->Channels == r->channels &&
            bits == r->bits)
            return 1;
        snprintf(message, size,
                 "Receiver %s takes %d channels of L%d at %d Hz, as it was opened; "
                 "the flow is %u channels of %s at %u Hz",
                 r->name, r->channels, r->bits, r->sample_rate, a->Channels,
                 DtNmosAudioEncoding_Text(a->Encoding), a->SampleRate);
        return 0;
    }
}

/**
 * The receivers' callback, which the node calls on a thread of its own when a
 * controller connects or disconnects a receiver. Refuses a flow of another format, and
 * otherwise asks read_packet's thread to apply the change.
 */
static DtNmosResult activate_receiver(void *user, const DtNmosId *id,
                                      const DtNmosReceiverActivation *activation)
{
    Receiver *r = user;
    Receiver now;
    DtNmosAvFifoRxChange change;
    char message[256];

    (void)id;
    /* read_header may be fixing the format while the node calls this. */
    ff_mutex_lock(&r->nmos->lock);
    now = *r;
    ff_mutex_unlock(&r->nmos->lock);
    if (now.configured && activation->MasterEnable && activation->HasFlow &&
        !same_format(&now, &activation->Flow, message, sizeof(message))) {
        av_log(r->nmos->log_ctx, AV_LOG_WARNING, "NMOS: %s\n", message);
        return DtNmos_SetLastError(DTNMOS_E_INVALID_ARGUMENT, message);
    }
    memset(&change, 0, sizeof(change));
    if (DtNmosAvFifo_RxChangeFromActivation(
            activation, now.configured ? now.format : St2110_RxFrameFormat_Raw, &change) !=
        DTAPI_OK)
        return DtNmos_SetLastError(DTNMOS_E_INVALID_ARGUMENT, GetLastException());
    return ask_owner(r->nmos, r, &change, now.configured, NULL, NULL);
}

/**
 * The senders' callback, which the node calls on a thread of its own when a controller
 * enables, disables or moves a sender. Asks write_packet's thread to apply the change.
 */
static DtNmosResult activate_sender(void *user, const DtNmosId *id,
                                    const DtNmosSenderActivation *activation)
{
    Sender *snd = user;
    DtNmosAvFifoTxChange change;

    (void)id;
    memset(&change, 0, sizeof(change));
    if (DtNmosAvFifo_TxChangeFromActivation(activation, &change) != DTAPI_OK)
        return DtNmos_SetLastError(DTNMOS_E_INVALID_ARGUMENT, GetLastException());
    return ask_owner(snd->nmos, NULL, NULL, 1, snd, &change);
}

/**
 * Write the address of size bytes, 4 for IPv4 or 16 for IPv6, as text into text.
 */
static void address_text(const uint8_t *address, int size, char *text, size_t text_size)
{
    if (!inet_ntop(size == 4 ? AF_INET : AF_INET6, address, text, text_size))
        text[0] = '\0';
}

/**
 * Log a failure of dtnmos or of the bridge, and turn it into an AVERROR.
 */
static int failed(void *log_ctx, const char *what, const char *why)
{
    av_log(log_ctx, AV_LOG_ERROR, "NMOS: %s failed: %s\n", what, why);
    return AVERROR_EXTERNAL;
}

/* The node's clock follows the PTP clock slave of the port, which DtapiService runs; it
 * is asked once a second, and the node registers again only when the clock changed. */
static void update_clock(FFDektecNmos *nmos)
{
    DtNmosClock clock;
    int64_t now = av_gettime_relative();

    if (now < nmos->clock_due)
        return;
    nmos->clock_due = now + 1000000;
    DtNmosAvFifo_ClockFromPort(nmos->device, nmos->port, &clock);
    DtNmosNode_SetClock(nmos->node, &clock);
}

int ff_dektec_nmos_open(void *log_ctx, const FFDektecNmosOptions *options,
                        DtDevice *device, int64_t serial, int port, FFDektecNmos **out)
{
    FFDektecNmos *nmos;
    DtNmosNodeConfig config;
    DtNmosClock clock;
    char name[160];
    int automatic = !strcmp(options->registry, "auto");
    int ret;

    *out = NULL;
    if (!DtNmos_HasServer())
        return failed(log_ctx, "Opening the node", "dtnmos has no HTTP server");
    nmos = av_mallocz(sizeof(*nmos));
    if (!nmos)
        return AVERROR(ENOMEM);
    nmos->log_ctx = log_ctx;
    ff_mutex_init(&nmos->lock, NULL);
    ff_cond_init(&nmos->changed, NULL);
    if (options->label && *options->label)
        av_strlcpy(nmos->label, options->label, sizeof(nmos->label));
    else
        snprintf(nmos->label, sizeof(nmos->label), "ffmpeg-%" PRId64 ":%d", serial, port);

    if (automatic) {
        DtNmosRegistrySearchConfig search;
        memset(&search, 0, sizeof(search));
        search.Size = sizeof(search);
        search.Finds = DTNMOS_FINDS_REGISTRATION;
        nmos->search = DtNmosRegistrySearch_Alloc();
        if (!nmos->search || DtNmosRegistrySearch_Open(nmos->search, &search) != DTNMOS_OK) {
            ret = failed(log_ctx, "Searching for registries", DtNmos_GetLastError());
            goto fail;
        }
    }

    /* The node's ID follows from the host and the label, so that a command that runs
     * again is the same node to the registry and the controller. */
    memset(&config, 0, sizeof(config));
    config.Size = sizeof(config);
    if (options->host && *options->host)
        snprintf(name, sizeof(name), "%s %s", options->host, nmos->label);
    else
        av_strlcpy(name, nmos->label, sizeof(name));
    DtNmosId_FromName(&nmos_namespace, name, &config.Id);
    config.Label = nmos->label;
    config.ApiHost = options->host && *options->host ? options->host : NULL;
    config.ApiPort = (uint16_t)options->api_port;
    config.RegistrationUrl = automatic ? NULL : options->registry;
    config.Search = nmos->search;
    config.Http = DtNmos_CurlHttp;
    config.Log = log_message;
    config.LogUser = nmos;
    /* The port's PTP grandmaster, or an internal clock without one or without
     * DtapiService. */
    DtNmosAvFifo_ClockFromPort(device, port, &clock);
    config.Clock = &clock;
    nmos->device = device;
    nmos->port = port;
    nmos->clock_due = av_gettime_relative() + 1000000;
    nmos->node = DtNmosNode_Alloc();
    if (!nmos->node || DtNmosNode_Open(nmos->node, &config) != DTNMOS_OK ||
        DtNmosNode_Serve(nmos->node) != DTNMOS_OK) {
        ret = failed(log_ctx, "Opening the node", DtNmos_GetLastError());
        goto fail;
    }
    if (DtNmosAvFifo_AddDevice(nmos->node, device, port, NULL, &nmos->device_id) != DTAPI_OK) {
        ret = failed(log_ctx, "Adding the port to the node", GetLastException());
        goto fail;
    }

    {
        char url[256];
        size_t size = sizeof(url);
        if (DtNmosNode_ApiUrl(nmos->node, url, &size) != DTNMOS_OK)
            av_strlcpy(url, "?", sizeof(url));
        av_log(log_ctx, AV_LOG_INFO, "NMOS: node %s, \"%s\", at %s, registry %s\n",
               config.Id.Text, nmos->label, url, options->registry);
        if (clock.Kind == DTNMOS_CLOCK_PTP)
            av_log(log_ctx, AV_LOG_INFO, "NMOS: clock PTP %s%s\n", clock.Grandmaster,
                   clock.Locked ? ", locked" : "");
        else
            av_log(log_ctx, AV_LOG_INFO, "NMOS: clock internal\n");
    }
    *out = nmos;
    return 0;

fail:
    ff_dektec_nmos_close(&nmos);
    return ret;
}

int ff_dektec_nmos_add_receiver(FFDektecNmos *nmos, AvFifo_RxFifo *fifo,
                                enum AVMediaType media, const AvFifo_IpPars *ippars,
                                const char *name)
{
    char group[64] = "", source[64] = "";
    Receiver *r;
    DtNmosReceiverConfig config;
    DtNmosId id;
    char label[200];

    if (nmos->nb_receivers >= MAX_RECEIVERS)
        return AVERROR(EINVAL);
    r = &nmos->receivers[nmos->nb_receivers];
    memset(r, 0, sizeof(*r));
    r->nmos = nmos;
    r->fifo = fifo;
    r->stream_index = nmos->nb_receivers;
    r->media = media;
    r->has_address = ippars != NULL;
    av_strlcpy(r->name, name, sizeof(r->name));

    memset(&config, 0, sizeof(config));
    config.Size = sizeof(config);
    config.DeviceId = nmos->device_id;
    snprintf(label, sizeof(label), "%s %s", nmos->label, name);
    config.Label = label;
    config.Media = r->media == AVMEDIA_TYPE_VIDEO ? DTNMOS_MEDIA_VIDEO : DTNMOS_MEDIA_AUDIO;
    /* The receiver's first stream: a group it joins, or unicast to the port's own
     * address when the destination is not a group; and the one source it takes. */
    if (ippars) {
        int size = ippars->IpVersion == IpProtocolVersion_IPv6 ? 16 : 4;
        if (size == 4 ? (ippars->IpAddr[0] & 0xF0) == 0xE0 : ippars->IpAddr[0] == 0xFF)
            address_text(ippars->IpAddr, size, group, sizeof(group));
        if (ippars->NSrcFlt > 0)
            address_text(ippars->SrcFlt[0].IpAddr, size, source, sizeof(source));
        config.MulticastIp = group;
        config.SourceIp = source;
        config.DestinationPort = (uint16_t)ippars->Port;
    }
    if (DtNmosAvFifo_AddReceiver(nmos->node, fifo, &config, activate_receiver, r, &id) !=
        DTAPI_OK)
        return failed(nmos->log_ctx, "Adding a receiver", GetLastException());
    nmos->nb_receivers++;
    av_log(nmos->log_ctx, AV_LOG_INFO, "NMOS: receiver %s, \"%s\"\n", id.Text, label);
    return 0;
}

int ff_dektec_nmos_add_sender(FFDektecNmos *nmos, AvFifo_TxFifo *fifo, int stream_index,
                              const char *name)
{
    Sender *snd;
    DtNmosSenderConfig config;
    DtNmosId id;
    char label[200];

    if (nmos->nb_senders >= MAX_RECEIVERS)
        return AVERROR(EINVAL);
    snd = &nmos->senders[nmos->nb_senders];
    memset(snd, 0, sizeof(*snd));
    snd->nmos = nmos;
    snd->fifo = fifo;
    snd->stream_index = stream_index;
    snd->sending = 1;
    av_strlcpy(snd->name, name, sizeof(snd->name));

    /* The bridge describes the flow from the FIFO, as it is configured to send. */
    memset(&config, 0, sizeof(config));
    config.Size = sizeof(config);
    config.DeviceId = nmos->device_id;
    snprintf(label, sizeof(label), "%s %s", nmos->label, name);
    config.Label = label;
    if (DtNmosAvFifo_AddSender(nmos->node, fifo, &config, activate_sender, snd, &id) !=
        DTAPI_OK)
        return failed(nmos->log_ctx, "Adding a sender", GetLastException());
    nmos->nb_senders++;
    av_log(nmos->log_ctx, AV_LOG_INFO, "NMOS: sender %s, \"%s\"\n", id.Text, label);
    return 0;
}

int ff_dektec_nmos_sending(FFDektecNmos *nmos, int stream_index)
{
    int sending = 1;

    if (!nmos)
        return 1;
    ff_mutex_lock(&nmos->lock);
    for (int i = 0; i < nmos->nb_senders; i++) {
        if (nmos->senders[i].stream_index == stream_index)
            sending = nmos->senders[i].sending;
    }
    ff_mutex_unlock(&nmos->lock);
    return sending;
}

void ff_dektec_nmos_set_stream(FFDektecNmos *nmos, int index, St2110_RxFrameFormat format,
                               const AVStream *st)
{
    const AVCodecParameters *par = st->codecpar;
    Receiver *r;

    if (!nmos || index < 0 || index >= nmos->nb_receivers)
        return;
    r = &nmos->receivers[index];
    ff_mutex_lock(&nmos->lock);
    r->format = format;
    if (r->media == AVMEDIA_TYPE_VIDEO) {
        r->width = par->width;
        r->height = par->height;
        r->frame_rate = st->r_frame_rate;
        r->interlaced = par->field_order != AV_FIELD_PROGRESSIVE &&
                        par->field_order != AV_FIELD_UNKNOWN;
        r->depth = par->bits_per_raw_sample;
    } else {
        r->sample_rate = par->sample_rate;
        r->channels = par->ch_layout.nb_channels;
        r->bits = par->bits_per_raw_sample;
    }
    r->configured = 1;
    ff_mutex_unlock(&nmos->lock);
}

int ff_dektec_nmos_wait(FFDektecNmos *nmos, int64_t timeout_us,
                        int (*interrupted)(void *opaque), void *opaque)
{
    int64_t end = timeout_us < 0 ? INT64_MAX : av_gettime_relative() + timeout_us;
    char names[256];
    int waiting;

    for (int logged = 0;; logged = 1) {
        ff_dektec_nmos_poll(nmos);
        names[0] = '\0';
        waiting = 0;
        ff_mutex_lock(&nmos->lock);
        for (int i = 0; i < nmos->nb_receivers; i++) {
            if (!nmos->receivers[i].has_address) {
                av_strlcatf(names, sizeof(names), "%s%s", waiting ? ", " : "",
                            nmos->receivers[i].name);
                waiting++;
            }
        }
        ff_mutex_unlock(&nmos->lock);
        if (!waiting)
            return 0;
        if (!logged)
            av_log(nmos->log_ctx, AV_LOG_INFO,
                   "NMOS: waiting for a controller to connect %s\n", names);
        if (interrupted(opaque))
            return AVERROR_EXIT;
        if (av_gettime_relative() >= end) {
            av_log(nmos->log_ctx, AV_LOG_ERROR,
                   "NMOS: no controller connected %s within nmos_wait\n", names);
            return AVERROR(ETIMEDOUT);
        }
        av_usleep(10000);
    }
}

/**
 * Apply the change for a sender that waits in the mailbox, which ff_dektec_nmos_poll()
 * has taken, and answer it. A sender that a controller disabled, or whose FIFO did not
 * start again, sends nothing until a later change.
 *
 * @return the stream index of the sender's FIFO
 */
static int apply_sender_change(FFDektecNmos *nmos, Sender *snd,
                               const DtNmosAvFifoTxChange *change)
{
    unsigned int result = DtNmosAvFifo_ApplyTxChange(snd->fifo, change);
    const char *why = result == DTAPI_OK ? "" : GetLastException();

    ff_mutex_lock(&nmos->lock);
    snd->sending = result == DTAPI_OK && change->MasterEnable;
    nmos->result = result;
    av_strlcpy(nmos->message, why, sizeof(nmos->message));
    nmos->state = nmos->abandoned ? MAIL_EMPTY : MAIL_ANSWERED;
    nmos->abandoned = 0;
    ff_cond_broadcast(&nmos->changed);
    ff_mutex_unlock(&nmos->lock);

    if (result != DTAPI_OK)
        av_log(nmos->log_ctx, AV_LOG_ERROR, "NMOS: sender %s: the change failed: %s\n",
               snd->name, why);
    else if (change->MasterEnable)
        av_log(nmos->log_ctx, AV_LOG_INFO, "NMOS: sender %s sends to port %d\n",
               snd->name, change->DestinationPort);
    else
        av_log(nmos->log_ctx, AV_LOG_INFO, "NMOS: sender %s disabled\n", snd->name);
    return snd->stream_index;
}

int ff_dektec_nmos_poll(FFDektecNmos *nmos)
{
    Receiver *r;
    DtNmosAvFifoRxChange change;
    unsigned int result;
    int configured, made_open;
    const char *why;

    if (!nmos)
        return -1;
    update_clock(nmos);
    ff_mutex_lock(&nmos->lock);
    if (nmos->state != MAIL_POSTED) {
        ff_mutex_unlock(&nmos->lock);
        return -1;
    }
    if (nmos->sender) {
        Sender *snd = nmos->sender;
        DtNmosAvFifoTxChange tx = nmos->tx_change;
        nmos->state = MAIL_TAKEN;
        ff_mutex_unlock(&nmos->lock);
        return apply_sender_change(nmos, snd, &tx);
    }
    r = nmos->receiver;
    change = nmos->change;
    configured = r->configured;
    made_open = nmos->made_open;
    nmos->state = MAIL_TAKEN;
    ff_mutex_unlock(&nmos->lock);

    /* Before the stream is open, the FIFO is stopped and only learns its address; its
     * format comes from the frames that then arrive. A change made before the stream
     * opened, but taken after, was made for no format and is not applied. */
    if (configured != made_open)
        result = DTAPI_E_STATE;
    else if (configured)
        result = DtNmosAvFifo_ApplyRxChange(r->fifo, &change);
    else if (change.MasterEnable)
        result = AvFifo_RxFifo_SetIpPars(r->fifo, &change.IpPars);
    else
        result = DTAPI_OK;
    why = result == DTAPI_OK         ? ""
          : configured != made_open ? "The stream opened meanwhile; connect it again"
                                    : GetLastException();

    ff_mutex_lock(&nmos->lock);
    if (!configured && change.MasterEnable && result == DTAPI_OK)
        r->has_address = 1;
    nmos->result = result;
    av_strlcpy(nmos->message, why, sizeof(nmos->message));
    nmos->state = nmos->abandoned ? MAIL_EMPTY : MAIL_ANSWERED;
    nmos->abandoned = 0;
    ff_cond_broadcast(&nmos->changed);
    ff_mutex_unlock(&nmos->lock);

    if (result != DTAPI_OK)
        av_log(nmos->log_ctx, AV_LOG_ERROR, "NMOS: receiver %s: the change failed: %s\n",
               r->name, why);
    else
        av_log(nmos->log_ctx, AV_LOG_INFO, "NMOS: receiver %s %s\n", r->name,
               change.MasterEnable ? "connected" : "disabled");
    return configured ? r->stream_index : -1;
}

void ff_dektec_nmos_close(FFDektecNmos **pnmos)
{
    FFDektecNmos *nmos = *pnmos;

    if (!nmos)
        return;
    /* A callback that waits for the FIFOs' thread gives up at once, so that closing the node,
     * which waits for its callbacks, does not wait for the time-out. */
    ff_mutex_lock(&nmos->lock);
    nmos->closing = 1;
    ff_cond_broadcast(&nmos->changed);
    ff_mutex_unlock(&nmos->lock);

    DtNmosNode_Freep(&nmos->node);
    DtNmosRegistrySearch_Freep(&nmos->search);
    ff_cond_destroy(&nmos->changed);
    ff_mutex_destroy(&nmos->lock);
    av_freep(pnmos);
}
