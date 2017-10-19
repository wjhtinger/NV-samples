/* Copyright (c) 2015-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "nvavtp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <netpacket/packet.h>
#include <netinet/in.h>
#include <net/ethernet.h>
#include <fcntl.h>
#include <unistd.h>
#include <gst/gst.h>
#include "player-core-priv.h"

#define QUEUE_LENGTH 10000
#define QUEUE_ELEMENT_SIZE NVAVTP_TSP_SIZE
#define TSP_SIZE NVAVTP_TSP_SIZE
#define AVTP_TSP_SIZE "188"
#define PACKET_IPG      (125000)        /* (1) packet every 125 usec */
#define NV_MSRP_SR_CLASS_A_PRIO    3
#define NV_MSRP_SR_CLASS_B_PRIO    2
#define AUDIO_DEV_NULL "NULL"
#define ALSA_SINK_DEV_NULL "null"

struct ifr_datastruct_1722
{
    U32 txred_len;
    U32 pkt_len;
    U8  data[1500];
};

typedef struct
{
    U32 lsock_video;
    NvAvtpContextHandle pHandle;
    U8 *tmp_packet;
    U32 pkt_size;
    U32 cnt;
    U32 NvAvtpTimeStamp;
    U8 NvInitialised;
    struct timespec prev_rec_time;
    gchar eth_iface[GST_NVM_MAX_DEVICE_NAME];
    gint vlan_prio;
    gint stream_id;
} GstNvmAvbSrcData;

U32 NvGetMacAddress(gchar *interface);
void NvSendPkt1722Video(gchar *eth_iface, U32 lsock_video, U8 *pAvtpPkt, U32 pktSize);
U32 NvSendAvtpVideoPacket(GstNvmAvbSrcData *priv_data, U8 *NvBuffer, U32 NvBufferSize);



U8 STATION_ADDR[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
U8 STREAM_ID[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
U8 DEST_ADDR[] = { 0x91, 0xE0, 0xF0, 0x00, 0x0e, 0x80 };


U32
NvGetMacAddress(gchar *interface)
{
    struct ifreq if_request;
    U32 lsock;
    U32 rc;
    lsock = socket(PF_PACKET, SOCK_RAW, htons(0x22EA));
    if (lsock < 0)
    {
        GST_ERROR("raw socket failed\n");
        return -1;
    }
    memset(&if_request, 0, sizeof(if_request));
    strncpy(if_request.ifr_name, (const char *)interface,sizeof(if_request.ifr_name));
    rc = ioctl(lsock, SIOCGIFHWADDR, &if_request);
    if (rc < 0)
    {
        close(lsock);
        return -1;
    }
    memcpy(STATION_ADDR, if_request.ifr_hwaddr.sa_data, sizeof(STATION_ADDR));
    close(lsock);
    return 0;
}

void
NvSendPkt1722Video(gchar * eth_iface, U32 lsock_video, U8 *pAvtpPkt, U32 pktSize)
{
    U32 rc;
    struct ifreq ifr;
    struct sockaddr_ll socket_address;
    struct ifr_datastruct_1722 pkt_1722;
    if( lsock_video < 0 )
    {
        GST_ERROR("Socket error");
    }
    /* Init tx parameters */
    memset(&ifr, 0, sizeof(ifr));
    strcpy(ifr.ifr_name, eth_iface);
    ifr.ifr_data = (void *) (&pkt_1722 + 8);
    if ((rc = ioctl(lsock_video, SIOCGIFINDEX, &ifr)) < 0)
    {
        GST_ERROR("SIOCGIFINDEX");
    }
    /* Index of the network device */
    socket_address.sll_ifindex = ifr.ifr_ifindex;
    /* Address length*/
    socket_address.sll_halen = ETH_ALEN;
    memcpy(socket_address.sll_addr,  DEST_ADDR, sizeof(DEST_ADDR));
    memcpy(pkt_1722.data, pAvtpPkt, pktSize);
    /* Init tx parameters */
    pkt_1722.txred_len = 0;
    pkt_1722.pkt_len = pktSize;
    /* Transmit available packets */
    /* Workaround for packet drops */
    sendto(lsock_video, pkt_1722.data,  pkt_1722.pkt_len, 0,
          (struct sockaddr*)&socket_address, sizeof(struct sockaddr_ll));
}

U32
NvSendAvtpVideoPacket(GstNvmAvbSrcData *priv_data, U8 *NvBuffer, U32 NvBufferSize)
{
    U8 *tmp_packet;
    NvAvtpInputParams *pAvtpInpPrms;
    U32 rc = 0;
    U32 a_priority = 0;
    U32 a_vid = 0;
    struct timespec rec_time;
    NvAvtpContextHandle pHandle;
    U32 packets;
    U32 pkt_grps;
    U32 pkt_rem;
    U32 i;

    if(priv_data->NvInitialised == 0)
    {
        pAvtpInpPrms = g_malloc(sizeof(NvAvtpInputParams));
        if(pAvtpInpPrms == NULL)
            return 1;
        memset(pAvtpInpPrms, 0x0, sizeof(NvAvtpInputParams));
        pAvtpInpPrms->bAvtpDepacketization = eNvAvtpFalse;
        pAvtpInpPrms->eDataType = eNvMpegts;
        strcpy(pAvtpInpPrms->interface, priv_data->eth_iface);
        NvAvtpInit (pAvtpInpPrms,&priv_data->pHandle);
        pHandle = priv_data->pHandle;
        rc = NvGetMacAddress(priv_data->eth_iface);
        if (rc)
        {
            GST_ERROR("failed to open interface\n");
            return 1;
        }

        a_priority = priv_data->vlan_prio;
        a_vid = 200;
        GST_DEBUG("detected domain Class A PRIO=%d VID=%04x...\n", a_priority, a_vid);
        memset(STREAM_ID, 0, sizeof(STREAM_ID));
        memcpy(STREAM_ID, STATION_ADDR, sizeof(STATION_ADDR));
        STREAM_ID[7] = (priv_data->stream_id & 0xff);
        STREAM_ID[6] = ((priv_data->stream_id >> 8) & 0xff);
        NvAvtpSetPacketSize(pHandle, NV_AVTP_MPEGTS_MAX_TS_PER_PKT);
        priv_data->tmp_packet = (U8 *) g_malloc(sizeof(U8)* NvAvtpGetPacketSize(pHandle));
        assert(priv_data->tmp_packet != NULL);
        priv_data->pkt_size = NvAvtpGetPacketSize(pHandle);
        GST_DEBUG("Packet Size %d\n", priv_data->pkt_size);
        tmp_packet = priv_data->tmp_packet;

        NvAvtpSetStaticAvtpHeader(pHandle, tmp_packet);
        NvAvtpSetDestAdd(pHandle, tmp_packet, DEST_ADDR);
        NvAvtpSetSrcAdd(pHandle, tmp_packet, STATION_ADDR);
        NvAvtpSetQTagFields(pHandle, tmp_packet, a_priority, a_vid);
        NvAvtpSetSIDValid(pHandle, tmp_packet, 1);
        NvAvtpSetStreamID(pHandle, tmp_packet,STREAM_ID);
        priv_data->NvInitialised++;
    }
    else
    {
        pHandle = priv_data->pHandle;
        tmp_packet = priv_data->tmp_packet;
    }


    packets = NvBufferSize/(NVAVTP_TSP_SIZE);
    pkt_grps = packets/NV_AVTP_MPEGTS_MAX_TS_PER_PKT;
    pkt_rem = packets % NV_AVTP_MPEGTS_MAX_TS_PER_PKT;
    for(i = 0; i < pkt_grps; i++)
    {
        if (NULL == tmp_packet)
            goto cleanup;
        NvAvtpSetDynamicAvtpHeader(pHandle,tmp_packet);
        NvAvtpFillDataPayload(pHandle, tmp_packet, (NvBuffer + (i*(NVAVTP_TSP_SIZE*NV_AVTP_MPEGTS_MAX_TS_PER_PKT))), NV_AVTP_MPEGTS_MAX_TS_PER_PKT);
        NvSendPkt1722Video(priv_data->eth_iface, priv_data->lsock_video, tmp_packet, priv_data->pkt_size);
        usleep(250);
        clock_gettime(CLOCK_MONOTONIC, &rec_time);
        if(priv_data->cnt != 0)
        {
            if(priv_data->prev_rec_time.tv_sec != rec_time.tv_sec)
            {
                GST_DEBUG("Sent %d packets in last second\n", priv_data->cnt);
                priv_data->cnt = 1;
                priv_data->prev_rec_time.tv_sec = rec_time.tv_sec;
            }
            else
                priv_data->cnt++;
        }
        else
        {
            priv_data->prev_rec_time.tv_sec = rec_time.tv_sec;
            priv_data->prev_rec_time.tv_nsec = rec_time.tv_nsec;
            priv_data->cnt++;
        }
    }
    if(pkt_rem > 0)
    {
        if (NULL == tmp_packet)
            goto cleanup;
        NvAvtpSetDynamicAvtpHeader(pHandle,tmp_packet);
        NvAvtpFillDataPayload(pHandle, tmp_packet, (NvBuffer + (pkt_grps * NVAVTP_TSP_SIZE * NV_AVTP_MPEGTS_MAX_TS_PER_PKT)), pkt_rem);
        NvSendPkt1722Video(priv_data->eth_iface, priv_data->lsock_video, tmp_packet, priv_data->pkt_size);
        usleep(250);
        clock_gettime(CLOCK_MONOTONIC, &rec_time);
        if(priv_data->cnt != 0)
        {
            if(priv_data->prev_rec_time.tv_sec != rec_time.tv_sec)
            {
                GST_DEBUG("Sent %d packets in last second\n", priv_data->cnt);
                priv_data->cnt = 1;
                priv_data->prev_rec_time.tv_sec = rec_time.tv_sec;
            }
            else
                priv_data->cnt++;
        }
        else
        {
            priv_data->prev_rec_time.tv_sec = rec_time.tv_sec;
            priv_data->prev_rec_time.tv_nsec = rec_time.tv_nsec;
            priv_data->cnt++;
        }
    }

cleanup:
    return 0;
}
static GstFlowReturn
on_new_sample_from_sink (GstElement * sink, gpointer user_data)
{
    GstSample *sample;
    GstBuffer *buffer;
    guint8 *ptr;
    GstMapInfo info;
    GstNvmAvbSrcData *priv_data;
    gsize offset = 0;
    gsize size = TSP_SIZE;

    priv_data =  (GstNvmAvbSrcData *)user_data;
    g_signal_emit_by_name (sink, "pull-sample", &sample);
    buffer = gst_sample_get_buffer (sample);
    gst_buffer_map(buffer,&info,GST_MAP_READ);
    ptr = g_malloc(info.size);
    g_assert(ptr);
    size = info.size;
    gst_buffer_unmap (buffer, &info);
    gst_buffer_extract(buffer,offset,(gpointer)ptr,size);
    NvSendAvtpVideoPacket(priv_data, ptr, size);
    g_free(ptr);
    /* we don't need the appsink sample anymore */
    gst_sample_unref (sample);
    /* get source an push new buffer */
    return GST_FLOW_OK;
}
/* called when we get a GstMessage from the sink pipeline when we get EOS, we
* exit the mainloop and this testapp. */
static gboolean
on_sink_message (GstBus * bus, GstMessage * message, gpointer    data)
{
    GstNvmContext *ctx = (GstNvmContext *) data;
    GError* err;
    gchar* debug;
    GstState old, new, pending;

    switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_ERROR:
            gst_message_parse_error (message, &err, &debug);
            GST_ERROR ("ERROR from element %s: %s\nDebugging info: %s",
            GST_OBJECT_NAME (message->src), err->message, (debug) ? debug : "none");
            g_error_free (err);
            g_free (debug);
            break;
        case GST_MESSAGE_EOS:
            GST_DEBUG ("Received Bus Callback EOS");
            break;
        case GST_MESSAGE_APPLICATION:
            g_timeout_add_seconds (1, (GSourceFunc) gst_nvm_common_timeout_callback, ctx);
            break;
        case GST_MESSAGE_STATE_CHANGED:
            if (GST_MESSAGE_SRC (message) == GST_OBJECT (ctx->pipeline)) {
                gst_message_parse_state_changed (message, &old, &new, &pending);
                if (new == GST_STATE_PLAYING)
                    gst_nvm_semaphore_signal (ctx->state_playing_sem);
            }
            break;
        default:
            /* unhandled message */
            break;
    }
    return TRUE;
}


static GstNvmResult
_avb_src_load (GstNvmContextHandle handle)
{
    if (!handle)
    {
        GST_ERROR ("Handle not initialised");
        return GST_NVM_RESULT_FAIL;
    }
    GstStateChangeReturn ret;
    GstNvmContext *ctx = (GstNvmContext *) handle;
    gchar *avb_src = ctx->last_command_param.string_value;

    gchar *string = NULL;
    gchar audio_dev[10];
    GstBus *bus = NULL;
    GstNvmAvbSrcData *priv_data = (GstNvmAvbSrcData *) ctx->private_data;

    GST_DEBUG ("Loading avb src");
    strcpy (priv_data->eth_iface, ctx->asrc_eth_iface);
    priv_data->vlan_prio = ctx->asrc_vlan_prio;
    priv_data->stream_id = ctx->asrc_stream_id;
    priv_data->lsock_video = socket( AF_PACKET, SOCK_RAW, IPPROTO_RAW );
    if( priv_data->lsock_video < 0 )
    {
       GST_ERROR("Socket error");
    }
     /* setting up source pipeline, we read from a file and convert to our desired
    * caps. */

    if(!strcmp(ctx->audio_dev, AUDIO_DEV_NULL))
        strcpy(audio_dev, ALSA_SINK_DEV_NULL);
    else
        strcpy(audio_dev, ctx->audio_dev);
    if(strcmp(avb_src, "eglstream") == 0) {
        string = g_strdup_printf
      ("nvmediaeglstreamsrc width=%d height=%d socket-path=%s surface-type=%d fifo-mode=%d ! nvmediasurfmixer ! nvmediah264videnc low-latency=%d ! h264parse ! mpegtsmux ! queue ! appsink name=avbappsink", \
        ctx->last_command_param.avb_params.stream_width, \
        ctx->last_command_param.avb_params.stream_height, \
        ctx->last_command_param.avb_params.socket_path, \
        ctx->last_command_param.avb_params.surface_type, \
        ctx->last_command_param.avb_params.fifo_mode, \
        ctx->last_command_param.avb_params.low_latency);
    }
    else {
        string = g_strdup_printf
      ("filesrc location=%s ! decodebin caps=\"video/x-nvmedia; audio/x-raw\" name=decode decode. ! audioconvert ! queue ! alsasink device=%s decode. ! nvmediah264videnc ! h264parse ! mpegtsmux ! queue ! appsink name=avbappsink", avb_src, audio_dev);
    }

    ctx->pipeline=gst_parse_launch (string, NULL);
    bus = gst_element_get_bus (ctx->pipeline);
    ctx->bus_watch_id = gst_bus_add_watch (bus, (GstBusFunc) on_sink_message, ctx);
    gst_object_unref (bus);

     /* to be notified of messages from this pipeline, mostly EOS */
    /* we use appsink in push mode, it sends us a signal when data is available
    * and we pull out the data in the signal callback. We want the appsink to
    * push as fast as it can, hence the sync=false */
    ctx->avbappsink = gst_bin_get_by_name (GST_BIN(ctx->pipeline), "avbappsink");
    g_object_set (G_OBJECT (ctx->avbappsink), "emit-signals", TRUE, "sync", 0, NULL);
    g_signal_connect (ctx->avbappsink, "new-sample",G_CALLBACK (on_new_sample_from_sink), priv_data);

     /* launching things */
    ret = gst_element_set_state (ctx->pipeline, GST_STATE_PLAYING);

    if (ret == GST_STATE_CHANGE_FAILURE) {
       GST_ERROR ("Couldn't change state ");
       return GST_NVM_RESULT_FAIL;
    }
    return GST_NVM_RESULT_OK;
}

static GstNvmResult
_avb_src_set_audio_device (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    gchar *audio_device = ctx->last_command_param.string_value;

    // If audio device changed
    if(strcasecmp(ctx->audio_dev, audio_device)) {
        GST_DEBUG ("Setting audio device for context %d to %s", ctx->id, audio_device);
        strcpy (ctx->audio_dev, audio_device);
        return GST_NVM_RESULT_OK;
    }

    GST_DEBUG ("Context %d already using audio channel %s", ctx->id, audio_device);
    return GST_NVM_RESULT_NOOP;
}

static GstNvmResult
_avb_src_set_eth_interface(GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    gchar *eth_iface = ctx->last_command_param.string_value;

    // If eth interface changed
    if(strcasecmp(ctx->asrc_eth_iface, eth_iface)) {
        GST_DEBUG ("Setting ethernet interface for context %d to %s", ctx->id, eth_iface);
        strcpy (ctx->asrc_eth_iface, eth_iface);
        return GST_NVM_RESULT_OK;
    }

    GST_DEBUG ("Context %d already using ethernet interface %s", ctx->id, eth_iface);
    return GST_NVM_RESULT_NOOP;
}

static GstNvmResult
_avb_src_set_vlan_priority(GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    gint vlan_prio = ctx->last_command_param.int_value;

    // If vlan priority changed
    if(ctx->asrc_vlan_prio != vlan_prio) {
        GST_DEBUG ("Setting vlan_prio for context %d to %d", ctx->id,
vlan_prio);
        ctx->asrc_vlan_prio = vlan_prio;
        return GST_NVM_RESULT_OK;
    }

    GST_DEBUG ("Context %d already using vlan_prio %d", ctx->id,
vlan_prio);
    return GST_NVM_RESULT_NOOP;
}

static GstNvmResult
_avb_src_set_stream_id(GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    gint stream_id = ctx->last_command_param.int_value;

    // If vlan priority changed
    if(ctx->asrc_stream_id != stream_id) {
        GST_DEBUG ("Setting stream_id for context %d to %d", ctx->id,
stream_id);
        ctx->asrc_stream_id = stream_id;
        return GST_NVM_RESULT_OK;
    }

    GST_DEBUG ("Context %d already using vlan_prio %d", ctx->id, stream_id);
    return GST_NVM_RESULT_NOOP;
}

static GstNvmResult
_avb_src_unload (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmAvbSrcData *priv_data = (GstNvmAvbSrcData *) ctx->private_data;

    GST_DEBUG ("Unloading avb src");
    gst_element_set_state (ctx->pipeline, GST_STATE_NULL);
    gst_object_unref (G_OBJECT (ctx->avbappsink));
    gst_object_unref (ctx->pipeline);
    g_source_remove(ctx->bus_watch_id);
    if(priv_data->tmp_packet != NULL)
    {
        g_free(priv_data->tmp_packet);
        priv_data->tmp_packet = NULL;
    }
    priv_data->NvInitialised = 0;
    close(priv_data->lsock_video);
    NvAvtpDeinit(priv_data->pHandle);
    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_avb_src_play (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmResult result;

    ctx->is_active = TRUE;
    GST_DEBUG ("Start Playing...");
    result = _avb_src_load (ctx);

    /* WAR: wait for state change bus callback to signal completion of
       state transistion to playing state. */
    if (IsSucceed (result)) {
        gst_nvm_semaphore_wait (ctx->state_playing_sem);
    }

    if (IsFailed (result)) {
        GST_DEBUG ("Play avbsrc failed. Unloading Avb Src...");
        _avb_src_unload(ctx);
    }

    return result;
}

GstNvmResult
gst_nvm_avb_src_stop (GstNvmContextHandle handle)
{
    if (!handle) {
        GST_ERROR ("Handle not initialised");
        return GST_NVM_RESULT_FAIL;
    }

    GstNvmContext *ctx = (GstNvmContext *) handle;

    g_mutex_lock (&ctx->gst_lock);
    if (ctx->is_active) {
        GST_DEBUG ("Stopping usb in context %d...", ctx->id);
        ctx->is_active = FALSE;
        _avb_src_unload (handle);

    }
    g_mutex_unlock (&ctx->gst_lock);
    return GST_NVM_RESULT_OK;
}


GstNvmResult
gst_nvm_avb_src_init (GstNvmContextHandle handle)
{
    if (!handle) {
        GST_ERROR ("Handle not initialised");
        return GST_NVM_RESULT_FAIL;
    }

    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmAvbSrcData *priv_data = (GstNvmAvbSrcData *) g_malloc (sizeof (GstNvmAvbSrcData));
    memset(priv_data, 0x0, sizeof(GstNvmAvbSrcData));

    ctx->private_data = priv_data;
    memset(ctx->func_table, 0x0, sizeof(GST_NVM_COMMANDS_NUM * sizeof (GstNvmFunc)));

    ctx->func_table[GST_NVM_CMD_PLAY]                        = gst_nvm_player_start;
    ctx->func_table[GST_NVM_CMD_SET_AUDIO_DEVICE]            = _avb_src_set_audio_device;
    ctx->func_table[GST_NVM_CMD_SET_ETHERNET_INTERFACE]      = _avb_src_set_eth_interface;
    ctx->func_table[GST_NVM_CMD_SET_STREAM_ID]               = _avb_src_set_stream_id;
    ctx->func_table[GST_NVM_CMD_SET_VLAN_PRIORITY]           = _avb_src_set_vlan_priority;
    ctx->func_table[GST_NVM_CMD_STOP]                        = gst_nvm_player_stop;
    ctx->func_table[GST_NVM_CMD_QUIT]                        = gst_nvm_player_quit;


    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_avb_src_fini (GstNvmContextHandle handle)
{
    if (!handle) {
        GST_ERROR ("Handle not initialised");
        return GST_NVM_RESULT_FAIL;
    }
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmAvbSrcData *priv_data = (GstNvmAvbSrcData *) ctx->private_data;

    if (priv_data) {
        g_free (priv_data);
        priv_data = NULL;
        ctx->private_data = NULL;
        return GST_NVM_RESULT_OK;
    }
    else
        return GST_NVM_RESULT_FAIL;
}

