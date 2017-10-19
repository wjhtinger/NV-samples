/* Copyright (c) 2015-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */
//#define PCAP_COMPILE

#include <gst/gst.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "nvavtp.h"
#include "player-core-priv.h"
#include <dlfcn.h>
#include <semaphore.h>

#include "pcap.h"
#include "raw_socket.h"

#define AVB "avb"
#define QUEUE_LENGTH 20000
#define QUEUE_ELEMENT_SIZE NVAVTP_TSP_SIZE
#define TSP_SIZE NVAVTP_TSP_SIZE

#ifdef PCAP_COMPILE
/* Function pointers for PCAP functions */
struct pcap_dl {
    void* handle;
    pcap_t* (*pcap_open_live_t) (const char *, int, int, int, char *);
    int (*pcap_compile_t) (pcap_t *, struct bpf_program *, const char *, int, bpf_u_int32);
    char* (*pcap_geterr_t) (pcap_t *);
    int (*pcap_setfilter_t) (pcap_t *, struct bpf_program *);
    int (*pcap_loop_t) (pcap_t *, int, pcap_handler, u_char *);
    void (*pcap_breakloop_t) (pcap_t *);
    void (*pcap_close_t) (pcap_t *);
    int (*pcap_setdirection_t) (pcap_t *p, pcap_direction_t d);
    int (*pcap_stats_t)(pcap_t *p, struct pcap_stat *ps);
    const char *dl_error;
} dlpcap;
#endif

//! AVTP Sub Header Type
typedef enum tagStreamOutputType {
    //! IEC 61883-6 Audio and music data transmission protocol
    //(Uncompressed Audio)
    eAudio,
    //! IEC 61883-4 MPEG2-TS data transmission
    eMpegts,
    //! 1722 AVTP Audio Format
    eAAF,
    //! 1722 AVTP Clock Reference Frame
    eCRF,
    //! 1722 CVF Format
    eCVF
} EStreamOutputType;

typedef struct
{
    NvAvtpContextHandle pHandle;
    pcap_t* handle;
    gboolean AVB_thread_active;
    gboolean firstPacket;
    gboolean filedump;
    GMutex  AVB_thread_mutex;
    GThread *configure_pcap_thread;
    U32 pktcnt;
    gchar eth_iface[GST_NVM_MAX_DEVICE_NAME];
    sem_t sem;
    EStreamOutputType mode;
    gboolean audio8;
    U32 payloadsize;
    U64 u64StreamId;
    U64 u64ReqStreamId;
} GstNvmAvbSinkData;

volatile int loop_status;
void pcap_callback_avb(u_char* args, const struct pcap_pkthdr* packet_header, const u_char* packet);
void pcap_callback_avb_audio(u_char* args, const struct pcap_pkthdr* packet_header, const u_char* packet);
void pcap_callback_avb_aaf(u_char* args, const struct pcap_pkthdr* packet_header, const u_char* packet);
gpointer configure_pcap(gpointer pParam);


void pcap_callback_avb(u_char* args, const struct pcap_pkthdr* packet_header, const u_char* packet)
{
    U64 u64StreamId;
    GstBuffer *buffer;
    guint8 *ptr;
    U32 size=188;
    GstFlowReturn ret;
    GstNvmContext *ctx = (GstNvmContext *) args;
    GstNvmAvbSinkData *priv_data = (GstNvmAvbSinkData *) ctx->private_data;
    NvAvtpContextHandle pHandle = priv_data->pHandle;
    ENvAvtpSubHeaderType eAvtpSubHeaderType;

    GST_DEBUG("Got packet.\n");
    if (NvAvtpIs1722Packet((U8 *)packet))
    {
        NvAvtpParseAvtpPacket(pHandle, (U8 *)packet, &eAvtpSubHeaderType);
        if((priv_data->u64ReqStreamId != 0) && (priv_data->u64ReqStreamId != (NvAvtpGetStreamId(pHandle, (U8 *)packet))))
            return;

        GST_DEBUG("Packet arrived %d\n", eAvtpSubHeaderType);

        if(priv_data->firstPacket == eNvAvtpTrue)
        {
              u64StreamId = NvAvtpGetStreamId(pHandle, (U8 *)packet);
              GST_DEBUG("Received stream id: %llx\n", u64StreamId);
              if(eAvtpSubHeaderType == eNvMpegts)
              {
                  GST_DEBUG("IEC 61883-4 MPEGTS AVTP stream\n");
              }
              priv_data->firstPacket = eNvAvtpFalse;
        }


        if((eAvtpSubHeaderType == eNvMpegts) || (eAvtpSubHeaderType == eNvCvf))
        {
            if (eAvtpSubHeaderType == eNvMpegts)
            {
                NvAvtpGetMpegtsDataPayloadSize(pHandle, (U8 *)packet,&size);
            }
            else if (eAvtpSubHeaderType == eNvCvf)
            {
                NvAvtpGetCvfDataPayloadSize(pHandle, (U8 *)packet,&size);
            }
            ptr = g_malloc(size);
            g_assert(ptr);
            NvAvtpExtractDataPayload(pHandle, (U8 *)packet, (U8 *)ptr);

            buffer = gst_buffer_new_and_alloc(size);
            gst_buffer_fill(buffer, 0, (gconstpointer)ptr, size);
            g_signal_emit_by_name (ctx->avbappsrc, "push-buffer", buffer, &ret);

            gst_buffer_unref(buffer);
            g_free(ptr);

            priv_data->pktcnt++;
        }
        else
            return;
    }
}

void pcap_callback_avb_audio(u_char* args, const struct pcap_pkthdr* packet_header, const u_char* packet)
{
    GstBuffer *buffer;
    guint8 *ptrtemp;
    guint8 *ptr;
    U32 size;
    U32 i,j,k;

    GstFlowReturn ret;
    GstNvmContext *ctx = (GstNvmContext *) args;
    GstNvmAvbSinkData *priv_data = (GstNvmAvbSinkData *) ctx->private_data;

    NvAvtpContextHandle pHandle = priv_data->pHandle;
    ENvAvtpSubHeaderType eAvtpSubHeaderType;
    GST_DEBUG("Got packet.\n");
    if (NvAvtpIs1722Packet((U8 *)packet))
    {
          if(priv_data->firstPacket == eNvAvtpFalse)
          {
              if(NvAvtpGetStreamId(pHandle, (U8 *)packet) != priv_data->u64StreamId)
              {
                  printf("Incorrect Stream Id\n");
                  return;
              }

          }
          NvAvtpParseAvtpPacket(pHandle, (U8 *)packet, &eAvtpSubHeaderType);
          if(priv_data->firstPacket == eNvAvtpTrue)
          {

              priv_data->u64StreamId = NvAvtpGetStreamId(pHandle, (U8 *)packet);
              GST_DEBUG("Received stream id: %llx\n", priv_data->u64StreamId);
              if(eAvtpSubHeaderType == eNvAudio)
              {
                  GST_DEBUG("IEC 61883-6 Audio AVTP stream\n");
              }
              priv_data->firstPacket = eNvAvtpFalse;
          }
          NvAvtpGetAudioDataPayloadSize(pHandle, (U8 *)packet, &size);
          GST_DEBUG("Stream Length %d\n", size);
          ptrtemp = g_malloc(size);
          g_assert(ptrtemp);

          NvAvtpExtractDataPayload(pHandle, (U8 *)packet, (U8 *)ptrtemp);
          if(priv_data->audio8)
          {
              ptr = g_malloc(size/4);
              g_assert(ptr);

              i=0;
              j=0;
              for(k=0;k<6;k++)
              {
                  ptr[j]=ptrtemp[i];
                  ptr[j+1]=ptrtemp[i+1];
                  ptr[j+2]=ptrtemp[i+2];
                  ptr[j+3]=ptrtemp[i+3];
                  j+=4;
                  i+=16;
              }
              size/=4;
          }
          else
          {
              ptr = ptrtemp;
          }

          buffer = gst_buffer_new_and_alloc(size);
          gst_buffer_fill(buffer, 0, (gconstpointer)ptr, size);

          g_signal_emit_by_name (ctx->avbappsrc, "push-buffer", buffer, &ret);

          gst_buffer_unref(buffer);
          g_free(ptrtemp);
          if(priv_data->audio8)
              g_free(ptr);
          priv_data->pktcnt++;
    }
}


void pcap_callback_avb_aaf(u_char* args, const struct pcap_pkthdr* packet_header, const u_char* packet)
{
    U64 u64StreamId;

    GstBuffer *buffer;
    guint8 *ptr;
    U32 size;
    GstFlowReturn ret;
    GstNvmContext *ctx = (GstNvmContext *) args;
    NvAvtp1722AAFParams  *pAvtp1722AAFParameters = NULL;
    GstNvmAvbSinkData *priv_data = (GstNvmAvbSinkData *) ctx->private_data;

    NvAvtpContextHandle pHandle = priv_data->pHandle;
    ENvAvtpSubHeaderType eAvtpSubHeaderType;
    GST_DEBUG("Got packet.\n");
    if (NvAvtpIs1722Packet((U8 *)packet))
    {

          NvAvtpParseAvtpPacket(pHandle, (U8 *)packet, &eAvtpSubHeaderType);
          if((priv_data->u64ReqStreamId != 0) && (priv_data->u64ReqStreamId != (NvAvtpGetStreamId(pHandle, (U8 *)packet))))
            return;
          if(priv_data->firstPacket == eNvAvtpTrue)
          {
              u64StreamId = NvAvtpGetStreamId(pHandle, (U8 *)packet);
              GST_DEBUG("Received stream id: %llx\n", u64StreamId);
              if(eAvtpSubHeaderType == eNvAAF)
              {
                  GST_DEBUG("AAF Stream detected\n");
                  pAvtp1722AAFParameters = (NvAvtp1722AAFParams  *)malloc(sizeof(NvAvtp1722AAFParams));

                  NvAvtpGetAAFParams(pHandle, (U8 *)packet, pAvtp1722AAFParameters);
                  GST_DEBUG("AAF Format %d\n", pAvtp1722AAFParameters->format);
                  GST_DEBUG("Channels per frame %d\n", pAvtp1722AAFParameters->channelsPerFrame);
                  GST_DEBUG("Sampling rate %d\n", pAvtp1722AAFParameters->samplingRate);
                  GST_DEBUG("Bit depth %d\n", pAvtp1722AAFParameters->bitDepth);
                  GST_DEBUG("Sparse timestamp %d\n", pAvtp1722AAFParameters->sparseTimestamp);
              }
              priv_data->firstPacket = eNvAvtpFalse;
          }
          if(eAvtpSubHeaderType == eNvAAF)
          {
              NvAvtpGetStreamLength(pHandle, (U8 *)packet, &size);
              GST_DEBUG("Stream Length %d\n", size);
              ptr = g_malloc(size);
              g_assert(ptr);


              NvAvtpExtractDataPayload(pHandle, (U8 *)packet, (U8 *)ptr);
              buffer = gst_buffer_new_and_alloc(size);
              gst_buffer_fill(buffer, 0, (gconstpointer)ptr, size);
              g_signal_emit_by_name (ctx->avbappsrc, "push-buffer", buffer, &ret);
              gst_buffer_unref(buffer);
              g_free(ptr);
              priv_data->pktcnt++;
          }
          else
              return;
    }
}

gpointer configure_pcap(gpointer pParam)
{
    gchar *dev = NULL;
    gchar *type = "avb";
#ifdef PCAP_COMPILE
    U32 bufsize = 1536;
    U32 pcap_time = 50;
    gchar errbuf[PCAP_ERRBUF_SIZE];
#else
    U8 buffer[2048];
    loop_status = 1;
    int fd_read_socket;
    S32 bytesRead = 0;
#endif
    NvAvtpInputParams *pAvtpInpPrms;
    GstNvmContext *ctx = (GstNvmContext *) pParam;
    GstNvmAvbSinkData *priv_data = (GstNvmAvbSinkData *) ctx->private_data;

    strcpy(priv_data->eth_iface, ctx->asink_eth_iface);
    priv_data->u64ReqStreamId = strtoull(ctx->asink_stream_id, NULL, 16);
    dev = strdup(priv_data->eth_iface);

    g_mutex_lock (&priv_data->AVB_thread_mutex);
    priv_data->AVB_thread_active = TRUE;
    g_mutex_unlock (&priv_data->AVB_thread_mutex);

    if (NULL == dev)
        return GINT_TO_POINTER(1);
#ifdef PCAP_COMPILE
    if(0 == memcmp(type, AVB, sizeof(AVB)))
    {

        /** session, get session handler */
        /* take promiscuous vs. non-promiscuous sniffing? (0 or 1) */
        priv_data->handle = dlpcap.pcap_open_live_t(dev, bufsize, 1, pcap_time, errbuf);
        if (NULL == priv_data->handle)
        {
            GST_ERROR("Could not open device %s: %s\n", dev, errbuf);
            return GINT_TO_POINTER(-1);
        }
        printf("bufsize %d pcap %d\n", bufsize,pcap_time);

        if (-1 == dlpcap.pcap_setdirection_t(priv_data->handle, PCAP_D_IN))
        {
            GST_ERROR("Could not set direction %s\n", dlpcap.pcap_geterr_t(priv_data->handle));
            return GINT_TO_POINTER(-1);
        }

        if(0 == memcmp(type, AVB, sizeof(AVB)))
        {
            pAvtpInpPrms = malloc(sizeof(NvAvtpInputParams));
            if(pAvtpInpPrms == NULL)
                return GINT_TO_POINTER(1);
            memset(pAvtpInpPrms, 0x0, sizeof(NvAvtpInputParams));
            pAvtpInpPrms->bAvtpDepacketization = eNvAvtpTrue;
            if(priv_data->mode == eMpegts)
                pAvtpInpPrms->eDataType = eNvMpegts;
            else if (priv_data->mode == eAudio)
                pAvtpInpPrms->eDataType = eNvAudio;
            else if (priv_data->mode == eAAF)
                pAvtpInpPrms->eDataType = eNvAAF;
            else if (priv_data->mode == eCVF)
                pAvtpInpPrms->eDataType = eNvCvf;
            strcpy(pAvtpInpPrms->interface, priv_data->eth_iface);
            NvAvtpInit (pAvtpInpPrms, &priv_data->pHandle);
            free(pAvtpInpPrms);
            GST_DEBUG("Calling pcap_loop\n");

            if(priv_data->mode == eMpegts || priv_data->mode == eCVF)
                dlpcap.pcap_loop_t(priv_data->handle, -1, pcap_callback_avb, (u_char *)ctx);
            else if (priv_data->mode == eAudio)
                dlpcap.pcap_loop_t(priv_data->handle, -1, pcap_callback_avb_audio, (u_char *)ctx);
            else if (priv_data->mode == eAAF)
                dlpcap.pcap_loop_t(priv_data->handle, -1, pcap_callback_avb_aaf, (u_char *)ctx);

        }
        else
            GST_ERROR("Invalid capture format\n");
    }
#else
    fd_read_socket = set_socket((char*)priv_data->eth_iface);

    if(0 == memcmp(type, AVB, sizeof(AVB)))
    {
        pAvtpInpPrms = malloc(sizeof(NvAvtpInputParams));
        if(pAvtpInpPrms == NULL)
            return GINT_TO_POINTER(1);
        memset(pAvtpInpPrms, 0x0, sizeof(NvAvtpInputParams));
        pAvtpInpPrms->bAvtpDepacketization = eNvAvtpTrue;
        if(priv_data->mode == eMpegts)
            pAvtpInpPrms->eDataType = eNvMpegts;
        else if(priv_data->mode == eCVF)
            pAvtpInpPrms->eDataType = eNvCvf;
        else if (priv_data->mode == eAudio)
            pAvtpInpPrms->eDataType = eNvAudio;
        else if (priv_data->mode == eAAF)
            pAvtpInpPrms->eDataType = eNvAAF;
        strcpy(pAvtpInpPrms->interface, priv_data->eth_iface);
        NvAvtpInit (pAvtpInpPrms, &priv_data->pHandle);
        free(pAvtpInpPrms);
        GST_DEBUG("Calling pcap_loop\n");

        while (loop_status)
        {
            bytesRead = recvfrom(fd_read_socket,buffer,2048,0,NULL,NULL);

            if (bytesRead < 0)
            {
                printf("Closing socket\n");
                break;
            }
            /*packet is too short*/
            if (bytesRead < 14)
            {
                perror("recvfrom():");
                printf("Bytes read: %d\n", bytesRead);
                printf("Incomplete packet (errno is %d)\n", errno);
                close(fd_read_socket);
                exit(1);
            }

            if((priv_data->mode == eMpegts) || (priv_data->mode == eCVF))
                pcap_callback_avb((u_char *)ctx, NULL, (const u_char*) &buffer);
            else if (priv_data->mode == eAudio)
                pcap_callback_avb_audio((u_char *)ctx, NULL, (const u_char*) &buffer);
            else if (priv_data->mode == eAAF)
                pcap_callback_avb_aaf((u_char *)ctx, NULL, (const u_char*) &buffer);
        }
    }
    else
        GST_ERROR("Invalid capture format\n");
#endif
    g_mutex_lock (&priv_data->AVB_thread_mutex);
    priv_data->AVB_thread_active = FALSE;
    g_mutex_unlock (&priv_data->AVB_thread_mutex);

    sem_post(&priv_data->sem);

    GST_DEBUG("Exiting configure_pcap\n");
    return GINT_TO_POINTER(0);
}

#ifdef PCAP_COMPILE
static
GstNvmResult init_dl (void)
{
   if (!dlpcap.handle) {
       dlpcap.handle = dlopen ("libpcap.so", RTLD_LAZY);
       dlpcap.dl_error = dlerror ();
       if (dlpcap.dl_error) {
           GST_ERROR ("Cannot open libpcap. %s",dlpcap.dl_error);
           dlclose (dlpcap.handle);
           dlpcap.handle = NULL;
           return GST_NVM_RESULT_FAIL;
       }
   }

   dlpcap.pcap_open_live_t = dlsym (dlpcap.handle, "pcap_open_live");
   dlpcap.dl_error = dlerror ();
   if (dlpcap.dl_error) {
       GST_ERROR ("Cannot get pcap_open_live. %s",dlpcap.dl_error);
       dlpcap.pcap_open_live_t = NULL;
       return GST_NVM_RESULT_FAIL;
   }

   dlpcap.pcap_compile_t = dlsym (dlpcap.handle, "pcap_compile");
   dlpcap.dl_error = dlerror ();
   if (dlpcap.dl_error) {
       GST_ERROR ("Cannot get pcap_compile. %s",dlpcap.dl_error);
       dlpcap.pcap_compile_t = NULL;
       return GST_NVM_RESULT_FAIL;
   }

   dlpcap.pcap_geterr_t = dlsym (dlpcap.handle, "pcap_geterr");
   dlpcap.dl_error = dlerror ();
   if (dlpcap.dl_error) {
       GST_ERROR ("Cannot get pcap_geterr. %s",dlpcap.dl_error);
       dlpcap.pcap_geterr_t = NULL;
       return GST_NVM_RESULT_FAIL;
   }

   dlpcap.pcap_setfilter_t = dlsym (dlpcap.handle, "pcap_setfilter");
   dlpcap.dl_error = dlerror ();
   if (dlpcap.dl_error) {
       GST_ERROR ("Cannot get pcap_setfilter. %s",dlpcap.dl_error);
       dlpcap.pcap_setfilter_t = NULL;
       return GST_NVM_RESULT_FAIL;
   }

   dlpcap.pcap_loop_t = dlsym (dlpcap.handle, "pcap_loop");
   dlpcap.dl_error = dlerror ();
   if (dlpcap.dl_error) {
       GST_ERROR ("Cannot get pcap_loop. %s",dlpcap.dl_error);
       dlpcap.pcap_loop_t = NULL;
       return GST_NVM_RESULT_FAIL;
   }

   dlpcap.pcap_breakloop_t = dlsym (dlpcap.handle, "pcap_breakloop");
   dlpcap.dl_error = dlerror ();
   if (dlpcap.dl_error) {
       GST_ERROR ("Cannot get pcap_breakloop. %s",dlpcap.dl_error);
       dlpcap.pcap_breakloop_t = NULL;
       return GST_NVM_RESULT_FAIL;
   }

   dlpcap.pcap_close_t = dlsym (dlpcap.handle, "pcap_close");
   dlpcap.dl_error = dlerror ();
   if (dlpcap.dl_error) {
       GST_ERROR ("Cannot get pcap_close. %s",dlpcap.dl_error);
       dlpcap.pcap_close_t = NULL;
       return GST_NVM_RESULT_FAIL;
   }

   dlpcap.pcap_setdirection_t = dlsym (dlpcap.handle, "pcap_setdirection");
   dlpcap.dl_error = dlerror ();
   if (dlpcap.dl_error) {
       GST_ERROR ("Cannot get pcap_close. %s",dlpcap.dl_error);
       dlpcap.pcap_setdirection_t = NULL;
       return GST_NVM_RESULT_FAIL;
   }

   dlpcap.pcap_stats_t = dlsym (dlpcap.handle, "pcap_stats");
   dlpcap.dl_error = dlerror ();
   if (dlpcap.dl_error) {
       printf ("Cannot get pcap_stats. %s",dlpcap.dl_error);
       dlpcap.pcap_stats_t = NULL;
       return 0;
   }

   return GST_NVM_RESULT_OK;
}

static
GstNvmResult deinit_dl (void)
{
    if (dlpcap.handle) {
       dlclose (dlpcap.handle);
       dlpcap.handle = NULL;
    }
    if (dlpcap.pcap_open_live_t) {
       dlpcap.pcap_open_live_t = NULL;
    }
    if (dlpcap.pcap_compile_t) {
       dlpcap.pcap_compile_t = NULL;
    }
    if (dlpcap.pcap_geterr_t) {
       dlpcap.pcap_geterr_t = NULL;
    }
    if (dlpcap.pcap_setfilter_t) {
       dlpcap.pcap_setfilter_t = NULL;
    }
    if (dlpcap.pcap_loop_t) {
       dlpcap.pcap_loop_t = NULL;
    }
    if (dlpcap.pcap_breakloop_t) {
       dlpcap.pcap_breakloop_t = NULL;
    }
    if (dlpcap.pcap_close_t) {
       dlpcap.pcap_close_t = NULL;
    }
    if (dlpcap.pcap_setdirection_t) {
       dlpcap.pcap_setdirection_t = NULL;
    }
    if (dlpcap.pcap_stats_t) {
       dlpcap.pcap_stats_t = NULL;
    }
    return GST_NVM_RESULT_OK;
}
#endif

static gboolean
bus_call (GstBus     *bus,
          GstMessage *message,
          gpointer    data)
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

static void
on_pad_added (GstElement *element,
              GstPad     *pad,
              gpointer    data)
{
  GstPad *sinkpad;
  GstElement *decoder = (GstElement *) data;

  /* We can now link this pad with the vorbis-decoder sink pad */
  GST_DEBUG("Dynamic pad created, linking demuxer/decoder\n");

  sinkpad = gst_element_get_static_pad (decoder, "sink");

  gst_pad_link (pad, sinkpad);

  gst_object_unref (sinkpad);
}

static GstNvmResult
_avb_sink_set_eth_interface(GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    gchar *eth_iface = ctx->last_command_param.string_value;

    // If eth interface changed
    if(strcasecmp(ctx->asink_eth_iface, eth_iface)) {
        GST_DEBUG ("Setting ethernet interface for context %d to %s", ctx->id, eth_iface);
        strcpy (ctx->asink_eth_iface, eth_iface);
        return GST_NVM_RESULT_OK;
    }

    GST_DEBUG ("Context %d already using ethernet interface %s", ctx->id, eth_iface);
    return GST_NVM_RESULT_NOOP;
}

static GstNvmResult
_avb_sink_set_stream_id(GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    gchar *streamid = ctx->last_command_param.string_value;

     // If stream id changed
    if(strcasecmp(ctx->asink_stream_id, streamid)) {
        GST_DEBUG ("Setting stream id for context %d to %s", ctx->id, streamid);
        strcpy (ctx->asink_stream_id, streamid);
        return GST_NVM_RESULT_OK;
    }

    GST_DEBUG ("Context %d already using stream id %s", ctx->id, streamid);
    return GST_NVM_RESULT_NOOP;

}
static GstNvmResult
_avb_sink_load (GstNvmContextHandle handle)
{
     if (!handle) {
        GST_ERROR ("Handle not initialised");
        return GST_NVM_RESULT_FAIL;
    }
    GstStateChangeReturn ret;
#ifdef PCAP_COMPILE
    GstNvmResult result;
#endif
    GstCaps *caps = NULL;
    GstElement *videosink, *h264dec, *h264parser, *demux, *sink, *audiosink;
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmAvbSinkData *priv_data = (GstNvmAvbSinkData *) ctx->private_data;
    gchar *mode = ctx->last_command_param.string_value;
    GstBus *bus = NULL;
#ifdef PCAP_COMPILE
    result = init_dl ();
    if (result == GST_NVM_RESULT_FAIL) {
        GST_ERROR ("Error while opening libpcap ");
        return GST_NVM_RESULT_FAIL;
    }
#endif
     /* setup pipeline */
    ctx->pipeline = gst_pipeline_new ("pipeline");
    ctx->avbappsrc = gst_element_factory_make ("appsrc", "avbappsrc");

    priv_data->filedump = FALSE;
    if(!strcmp(mode,"filempegts"))
    {
        priv_data->filedump = TRUE;
        priv_data->mode = eMpegts;
    }
    else if(!strcmp(mode,"mpegts"))
    {
        priv_data->mode = eMpegts;
    }
    else if(!strcmp(mode,"cvf"))
    {
        priv_data->mode = eCVF;
    }
    else if(!strcmp(mode,"fileaudio"))
    {
        priv_data->filedump = TRUE;
        priv_data->mode = eAudio;
    }
    else if(!strcmp(mode,"audio"))
    {
        priv_data->mode = eAudio;
    }
    else if(!strcmp(mode,"fileaudio8"))
    {
        priv_data->filedump = TRUE;
        priv_data->mode = eAudio;
        priv_data->audio8 = TRUE;
    }
    else if(!strcmp(mode,"audio8"))
    {
        priv_data->mode = eAudio;
        priv_data->audio8 = TRUE;
    }
    else if(!strcmp(mode,"fileaaf"))
    {
        priv_data->filedump = TRUE;
        priv_data->mode = eAAF;
    }
    else if(!strcmp(mode,"aaf"))
    {
        priv_data->mode = eAAF;
    }


    if(priv_data->filedump == TRUE)
    {
        sink = gst_element_factory_make("filesink","sink");
        if(priv_data->mode == eMpegts)
            g_object_set(G_OBJECT (sink),"location","recv.ts", NULL);
        else if((priv_data->mode == eAudio) || (priv_data->mode == eAAF))
            g_object_set(G_OBJECT (sink),"location","out.pcm", NULL);
        gst_bin_add_many (GST_BIN (ctx->pipeline), ctx->avbappsrc, sink,NULL);
        gst_element_link_many (ctx->avbappsrc, sink, NULL);

    }
    else
    {
        if(priv_data->mode == eMpegts)
        {
            demux = gst_element_factory_make ("tsdemux","demux");
            h264parser = gst_element_factory_make ("h264parse","h264parser");
            h264dec = gst_element_factory_make("nvmediah264viddec", "h264dec");
            videosink = gst_element_factory_make ("nvmediaoverlaysink", "videosink");

            if (!h264dec) {
                GST_ERROR ("NvMediaH264Dec not found");
                return GST_NVM_RESULT_FAIL;
            } else {
                    g_object_set (G_OBJECT (h264dec), "low-latency", ctx->last_command_param.avb_params.low_latency, NULL);
            }

            if (!videosink){
                GST_ERROR ("NvMediaVideoSink not found");
                return GST_NVM_RESULT_FAIL;
            } else {
                if (!strcasecmp (ctx->display_type, "display-0"))
                    g_object_set (G_OBJECT (videosink), "display-device", 0, NULL);
                else if (!strcasecmp (ctx->display_type, "display-1"))
                    g_object_set (G_OBJECT (videosink), "display-device", 1, NULL);
                else if (!strcasecmp ("dual", ctx->display_type))
                    g_object_set (G_OBJECT (videosink), "display-device", 2, NULL);
                else
                    g_object_set (G_OBJECT (videosink), "display-device", 3, NULL); // None
                g_object_set (G_OBJECT (videosink), "window-id", ctx->display_properties.window_id, NULL);
            }
            g_object_set(G_OBJECT (videosink),"sync",0,NULL);
            gst_bin_add_many (GST_BIN (ctx->pipeline), ctx->avbappsrc, demux, h264parser, h264dec, videosink, NULL);


            if(!gst_element_link_many(ctx->avbappsrc,  demux, NULL))
            {
                GST_ERROR("Failed to link one or more elements!\n");
                return -1;
            }

            if(!gst_element_link_many (h264parser, h264dec, videosink, NULL))
            {
                GST_ERROR("Failed to link one or more elements parser!\n");
                return -1;
            }
            g_signal_connect (demux, "pad-added", G_CALLBACK (on_pad_added), h264parser);

        }
        else if(priv_data->mode == eCVF)
        {
            h264parser = gst_element_factory_make ("h264parse","h264parser");
            h264dec = gst_element_factory_make("nvmediah264viddec", "h264dec");
            videosink = gst_element_factory_make ("nvmediaoverlaysink", "videosink");

            if (!h264dec) {
                GST_ERROR ("NvMediaH264Dec not found");
                return GST_NVM_RESULT_FAIL;
            } else {
                g_object_set (G_OBJECT (h264dec), "low-latency",
                                   ctx->last_command_param.avb_params.low_latency, NULL);
            }

            if (!videosink){
                GST_ERROR ("NvMediaVideoSink not found");
                return GST_NVM_RESULT_FAIL;
            } else {
                if (!strcasecmp (ctx->display_type, "display-0"))
                    g_object_set (G_OBJECT (videosink), "display-device", 0, NULL);
                else if (!strcasecmp (ctx->display_type, "display-1"))
                    g_object_set (G_OBJECT (videosink), "display-device", 1, NULL);
                else if (!strcasecmp ("dual", ctx->display_type))
                    g_object_set (G_OBJECT (videosink), "display-device", 2, NULL);
                else
                    g_object_set (G_OBJECT (videosink), "display-device", 3, NULL); // None
                g_object_set (G_OBJECT (videosink), "window-id",
                               ctx->display_properties.window_id, NULL);
            }
            g_object_set(G_OBJECT (videosink),"sync",0,NULL);

            gst_bin_add_many (GST_BIN (ctx->pipeline), ctx->avbappsrc,
                               h264parser, h264dec, videosink, NULL);
            if(!gst_element_link_many(ctx->avbappsrc,  h264parser, h264dec, videosink, NULL))
            {
                GST_ERROR("Failed to link one or more elements!\n");
                return -1;
            }
        }
        else if(priv_data->mode == eAudio || priv_data->mode == eAAF)
        {
            audiosink = gst_element_factory_make ("alsasink","audiosink");
            /*g_object_set (G_OBJECT (audiosink), "device", "hw:0,4", NULL);*/
            caps = gst_caps_new_simple("audio/x-raw",
                                       "format", G_TYPE_STRING, "S16LE",
                                       "rate", G_TYPE_INT, 48000,
                                       "channels", G_TYPE_INT, 2, NULL);
            gst_bin_add_many (GST_BIN (ctx->pipeline), ctx->avbappsrc, audiosink,NULL);
            gst_element_link_filtered(ctx->avbappsrc, audiosink,caps);


        }
    }
    priv_data->firstPacket = TRUE;

     /* we add a message handler */
    bus = gst_pipeline_get_bus (GST_PIPELINE (ctx->pipeline));
    gst_bus_add_watch (bus, bus_call, ctx);
    gst_object_unref (bus);

     /* setup appsrc */
    g_object_set (G_OBJECT (ctx->avbappsrc),
            "stream-type", 0,
            "is-live", TRUE,
            "format", GST_FORMAT_TIME,
            NULL);
    sem_init(&priv_data->sem, 0, 0);
    priv_data->configure_pcap_thread = g_thread_new("pcap_thread", configure_pcap, ctx);

    /* play */
    ret = gst_element_set_state (ctx->pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
       GST_ERROR ("Couldn't change state ");
       return GST_NVM_RESULT_FAIL;
    }

    return GST_NVM_RESULT_OK;
}

static GstNvmResult
_avb_sink_unload (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmAvbSinkData *priv_data = (GstNvmAvbSinkData *) ctx->private_data;
#ifdef PCAP_COMPILE
    struct pcap_stat ps;

    GST_DEBUG ("Unloading avb sink");
#endif
    g_mutex_lock (&priv_data->AVB_thread_mutex);
    if(priv_data->AVB_thread_active)
    {
#ifdef PCAP_COMPILE
        dlpcap.pcap_breakloop_t(priv_data->handle);
#endif
        loop_status = 0;
        g_mutex_unlock (&priv_data->AVB_thread_mutex);
        sem_wait(&priv_data->sem);
        g_mutex_lock (&priv_data->AVB_thread_mutex);
#ifdef PCAP_COMPILE
        dlpcap.pcap_stats_t(priv_data->handle, &ps);
        dlpcap.pcap_close_t(priv_data->handle);
        printf("pcap total packets %d\n", ps.ps_recv);
        printf("pcap packets dropped  %d\n", ps.ps_drop);
        printf("pcap packets interface dropped %d\n", ps.ps_ifdrop);
#endif
        g_thread_join(priv_data->configure_pcap_thread);
    }
    g_mutex_unlock (&priv_data->AVB_thread_mutex);
    gst_element_set_state (ctx->pipeline, GST_STATE_NULL);
    gst_object_unref (GST_OBJECT (ctx->pipeline));
#ifdef PCAP_COMPILE
    deinit_dl();
#endif
    NvAvtpDeinit(priv_data->pHandle);
    return GST_NVM_RESULT_OK;
}


GstNvmResult
gst_nvm_avb_sink_play (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmResult result;

    ctx->is_active = TRUE;
    GST_DEBUG ("Start Playing...");
    result = _avb_sink_load (ctx);

    /* WAR: wait for state change bus callback to signal completion of
       state transistion to playing state. */
    if (IsSucceed (result)) {
        gst_nvm_semaphore_wait (ctx->state_playing_sem);
    }

    if(IsFailed(result)){
         GST_DEBUG ("Play avbsink failed. Unloading Avb Sink...");
        _avb_sink_unload(ctx);
    }

    return result;
}

GstNvmResult
gst_nvm_avb_sink_stop (GstNvmContextHandle handle)
{
    if (!handle) {
        GST_ERROR ("Handle not initialised");
        return GST_NVM_RESULT_FAIL;
    }

    GstNvmContext *ctx = (GstNvmContext *) handle;
    g_mutex_lock (&ctx->gst_lock);
    if (ctx->is_active) {
        GST_DEBUG("Stopping avb sink in context %d...", ctx->id);
        ctx->is_active = FALSE;
        _avb_sink_unload (handle);

    }
    g_mutex_unlock (&ctx->gst_lock);

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_avb_sink_init (GstNvmContextHandle handle)
{
    if (!handle) {
        GST_ERROR ("Handle not initialised");
        return GST_NVM_RESULT_FAIL;
    }

    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmAvbSinkData *priv_data = (GstNvmAvbSinkData *) g_malloc (sizeof (GstNvmAvbSinkData));
    memset(priv_data, 0x0, sizeof(GstNvmAvbSinkData));
    g_mutex_init (&priv_data->AVB_thread_mutex);
    ctx->func_table = memset(ctx->func_table, 0x0, sizeof(GST_NVM_COMMANDS_NUM * sizeof (GstNvmFunc)));

    ctx->private_data = priv_data;

    ctx->func_table[GST_NVM_CMD_PLAY]                        = gst_nvm_player_start;
    ctx->func_table[GST_NVM_CMD_SET_VIDEO_DEVICE]            = gst_nvm_common_set_display;
    ctx->func_table[GST_NVM_CMD_SET_DISPLAY_WINDOW_ID]       = gst_nvm_common_set_display_window_id;
    ctx->func_table[GST_NVM_CMD_SET_ETHERNET_INTERFACE]      = _avb_sink_set_eth_interface;
    ctx->func_table[GST_NVM_CMD_SET_STREAM_ID]               = _avb_sink_set_stream_id;
    ctx->func_table[GST_NVM_CMD_STOP]                        = gst_nvm_player_stop;
    ctx->func_table[GST_NVM_CMD_QUIT]                        = gst_nvm_player_quit;

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_avb_sink_fini (GstNvmContextHandle handle)
{
    if (!handle) {
        GST_ERROR ("Handle not initialised");
        return GST_NVM_RESULT_FAIL;
    }
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmAvbSinkData *priv_data = (GstNvmAvbSinkData *) ctx->private_data;

    if (priv_data) {
        g_mutex_clear(&priv_data->AVB_thread_mutex);
        g_free (priv_data);
        priv_data = NULL;
        ctx->private_data = NULL;
        return GST_NVM_RESULT_OK;
    }
    else
        return GST_NVM_RESULT_FAIL;
    return GST_NVM_RESULT_OK;
}
