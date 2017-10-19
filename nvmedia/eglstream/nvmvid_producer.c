/*
 * nvmvideo_producer.c
 *
 * Copyright (c) 2013-2017, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// DESCRIPTION:   Simple video decoder EGL stream producer app
//
#include "nvmvid_producer.h"
#include "log_utils.h"

extern NvBool signal_stop;
test_video_parser_s g_parser;
#if defined(EXTENSION_LIST)
EXTENSION_LIST(EXTLST_EXTERN)
#endif
static NvS32 cbBeginSequence(void *ptr, const NVDSequenceInfo *pnvsi);
static NvBool cbDecodePicture(void *ptr, NVDPictureData *pd);
static NvBool cbDisplayPicture(void *ptr, NVDPicBuff *p, NvS64 llPTS);
static void cbUnhandledNALU(void *ptr, const NvU8 *buf, NvS32 size);
static NvBool cbAllocPictureBuffer(void *ptr, NVDPicBuff **p);
static void cbRelease(void *ptr, NVDPicBuff *p);
static void cbAddRef(void *ptr, NVDPicBuff *p);
static int Init(test_video_parser_s *parser);
static void Deinit(test_video_parser_s *parser);
static int Decode(test_video_parser_s *parser);
static void SetParamsH264(test_video_parser_s *ctx, NVDPictureData *pd, NvMediaPictureInfo *pictureInfo);
static NvBool DisplayInit(test_video_parser_s *parser, int width, int height, int videoWidth, int videoHeight);
static void DisplayDestroy(test_video_parser_s *parser);
static void DisplayFrame(test_video_parser_s *parser, frame_buffer_s *frame);
static void DisplayFlush(test_video_parser_s *parser);

static int SendRenderSurface(test_video_parser_s *parser, NvMediaVideoSurface *renderSurface, NvMediaTime *timestamp);
static NvMediaVideoSurface *GetRenderSurface(test_video_parser_s *parser);

static NvS32 cbBeginSequence(void *ptr, const NVDSequenceInfo *pnvsi)
{
    test_video_parser_s *ctx = (test_video_parser_s*)ptr;

    char *chroma[] = {
        "Monochrome",
        "4:2:0",
        "4:2:2",
        "4:4:4"
    };
    NvU32 decodeBuffers = pnvsi->nDecodeBuffers;
    NvMediaVideoDecoderAttributes attributes;

    if (pnvsi->eCodec != NVCS_H264) {
        LOG_ERR("BeginSequence: Invalid codec type: %d\n", pnvsi->eCodec);
        return 0;
    }

    LOG_DBG("BeginSequence: %dx%d (disp: %dx%d) codec: H264 decode buffers: %d aspect: %d:%d fps: %f chroma: %s\n",
        pnvsi->nCodedWidth, pnvsi->nCodedHeight, pnvsi->nDisplayWidth, pnvsi->nDisplayHeight,
        pnvsi->nDecodeBuffers, pnvsi->lDARWidth, pnvsi->lDARHeight,
        pnvsi->fFrameRate, pnvsi->nChromaFormat > 3 ? "Invalid" : chroma[pnvsi->nChromaFormat]);

    if (!ctx->frameTimeUSec && pnvsi->fFrameRate >= 5.0 && pnvsi->fFrameRate <= 120.0) {
        ctx->frameTimeUSec = 1000000.0 / pnvsi->fFrameRate;
    }

    if (!ctx->aspectRatio && pnvsi->lDARWidth && pnvsi->lDARHeight) {
        double aspect = (float)pnvsi->lDARWidth / (float)pnvsi->lDARHeight;
        if (aspect > 0.3 && aspect < 3.0)
            ctx->aspectRatio = aspect;
    }

    // Check resolution change
    if (pnvsi->nCodedWidth != ctx->decodeWidth || pnvsi->nCodedHeight != ctx->decodeHeight) {
        NvMediaVideoCodec codec;
        NvMediaSurfaceType surfType;
        NvU32 maxReferences;
        int i;

        LOG_DBG("BeginSequence: Resolution changed: Old:%dx%d New:%dx%d\n",
            ctx->decodeWidth, ctx->decodeHeight, pnvsi->nCodedWidth, pnvsi->nCodedHeight);

        ctx->decodeWidth = pnvsi->nCodedWidth;
        ctx->decodeHeight = pnvsi->nCodedHeight;

        ctx->displayWidth = pnvsi->nDisplayWidth;
        ctx->displayHeight = pnvsi->nDisplayHeight;

        ctx->renderWidth  = ctx->displayWidth;
        ctx->renderHeight = ctx->displayHeight;

        if (ctx->decoder) {
            NvMediaVideoDecoderDestroy(ctx->decoder);
        }

        LOG_DBG("Create decoder: ");
        codec = NVMEDIA_VIDEO_CODEC_H264;
        LOG_DBG("NVMEDIA_VIDEO_CODEC_H264");

        maxReferences = (decodeBuffers > 0) ? decodeBuffers - 1 : 0;
        maxReferences = (maxReferences > 16) ? 16 : maxReferences;

        LOG_DBG(" Size: %dx%d maxReferences: %d\n", ctx->decodeWidth, ctx->decodeHeight,
            maxReferences);
        ctx->decoder = NvMediaVideoDecoderCreate(
            codec,                   // codec
            ctx->decodeWidth,        // width
            ctx->decodeHeight,       // height
            maxReferences,           // maxReferences
            pnvsi->MaxBitstreamSize, //maxBitstreamSize
            5);                      // inputBuffering
        if (!ctx->decoder) {
            LOG_ERR("Unable to create decoder\n");
            return 0;
        }

        //set progressive sequence
        attributes.progressiveSequence = pnvsi->bProgSeq;
        NvMediaVideoDecoderSetAttributes(
            ctx->decoder,
            NVMEDIA_VIDEO_DECODER_ATTRIBUTE_PROGRESSIVE_SEQUENCE,
            &attributes);

        for(i = 0; i < MAX_FRAMES; i++) {
            if (ctx->RefFrame[i].videoSurface) {
                NvMediaVideoSurfaceDestroy(ctx->RefFrame[i].videoSurface);
            }
        }

        memset(&ctx->RefFrame[0], 0, sizeof(frame_buffer_s) * MAX_FRAMES);

        switch (pnvsi->nChromaFormat) {
            case 0: // Monochrome
            case 1: // 4:2:0
                LOG_DBG("Chroma format: NvMediaSurfaceType_YV12\n");
                surfType = NvMediaSurfaceType_YV12;
                break;
            case 2: // 4:2:2
                LOG_DBG("Chroma format: NvMediaSurfaceType_YV16\n");
                surfType = NvMediaSurfaceType_YV16;
                break;
            case 3: // 4:4:4
                LOG_DBG("Chroma format: NvMediaSurfaceType_YV24\n");
                surfType = NvMediaSurfaceType_YV24;
                break;
            default:
                LOG_DBG("Invalid chroma format: %d\n", pnvsi->nChromaFormat);
                return 0;
        }

        ctx->nBuffers = decodeBuffers + MAX_DISPLAY_BUFFERS;

        for(i = 0; i < ctx->nBuffers; i++) {
            if(ctx->eglOutput) {
                ctx->RefFrame[i].videoSurface =
                    NvMediaVideoSurfaceCreateEx(
                        ctx->device,
                        surfType,
                        (pnvsi->nCodedWidth + 15) & ~15,
                        (pnvsi->nCodedHeight + 15) & ~15,
                        NVMEDIA_SURFACE_CREATE_ATTRIBUTE_DISPLAY);
            } else {
                ctx->RefFrame[i].videoSurface =
                    NvMediaVideoSurfaceCreate(
                        ctx->device,
                        surfType,
                        (pnvsi->nCodedWidth + 15) & ~15,
                        (pnvsi->nCodedHeight + 15) & ~15);
            }
            if (!ctx->RefFrame[i].videoSurface) {
                LOG_ERR("Unable to create video surface\n");
                return 0;
            }
            LOG_DBG("Create video surface[%d]: %dx%d\n Ptr:%p Surface:%p Device:%p\n", i,
                (pnvsi->nCodedWidth + 15) & ~15, (pnvsi->nCodedHeight + 15) & ~15,
                &ctx->RefFrame[i], ctx->RefFrame[i].videoSurface, ctx->device);
        }

        if(ctx->surfaceType == NvMediaSurfaceType_R8G8B8A8_BottomOrigin) {
            for (i = 0; i < MAX_RENDER_SURFACE; i++) {
                if(ctx->eglOutput) {
                    ctx->renderSurfaces[i] = NvMediaVideoSurfaceCreateEx(
                        ctx->device,
                        ctx->surfaceType,
                        ctx->renderWidth,
                        ctx->renderHeight,
                        NVMEDIA_SURFACE_CREATE_ATTRIBUTE_DISPLAY);
                } else {
                    ctx->renderSurfaces[i] = NvMediaVideoSurfaceCreate(
                        ctx->device,
                        ctx->surfaceType,
                        ctx->renderWidth,
                        ctx->renderHeight);
                }
                if(!ctx->renderSurfaces[i]) {
                    LOG_DBG("Unable to create render surface\n");
                    return NV_FALSE;
                }
                ctx->freeRenderSurfaces[i] = ctx->renderSurfaces[i];
            }
        }
        DisplayDestroy(ctx);
        DisplayInit(ctx, ctx->displayWidth, ctx->displayHeight, pnvsi->nCodedWidth, pnvsi->nCodedHeight);
        LOG_DBG("cbBeginSequence: DisplayInit done: Mixer:%p\n", ctx->mixer);
    } else {
        printf("cbBeginSequence: No resolution change\n");
    }

    return decodeBuffers;
}

static void SetParamsH264(test_video_parser_s *ctx, NVDPictureData *pd, NvMediaPictureInfo *pictureInfo)
{
    NVH264PictureData *h264Data = &pd->CodecSpecific.h264;
    NvMediaPictureInfoH264 *h264PictureInfo = (NvMediaPictureInfoH264 *)pictureInfo;
    NvU32 i;

    h264PictureInfo->field_order_cnt[0] = h264Data->CurrFieldOrderCnt[0];
    h264PictureInfo->field_order_cnt[1] = h264Data->CurrFieldOrderCnt[1];
    h264PictureInfo->is_reference = pd->ref_pic_flag;
    h264PictureInfo->chroma_format_idc = pd->chroma_format;
    COPYFIELD(h264PictureInfo, h264Data, frame_num);
    COPYFIELD(h264PictureInfo, pd, field_pic_flag);
    COPYFIELD(h264PictureInfo, pd, bottom_field_flag);
    COPYFIELD(h264PictureInfo, h264Data, num_ref_frames);
    h264PictureInfo->mb_adaptive_frame_field_flag = h264Data->MbaffFrameFlag;
    COPYFIELD(h264PictureInfo, h264Data, constrained_intra_pred_flag);
    COPYFIELD(h264PictureInfo, h264Data, weighted_pred_flag);
    COPYFIELD(h264PictureInfo, h264Data, weighted_bipred_idc);
    COPYFIELD(h264PictureInfo, h264Data, frame_mbs_only_flag);
    COPYFIELD(h264PictureInfo, h264Data, transform_8x8_mode_flag);
    COPYFIELD(h264PictureInfo, h264Data, chroma_qp_index_offset);
    COPYFIELD(h264PictureInfo, h264Data, second_chroma_qp_index_offset);
    COPYFIELD(h264PictureInfo, h264Data, pic_init_qp_minus26);
    COPYFIELD(h264PictureInfo, h264Data, num_ref_idx_l0_active_minus1);
    COPYFIELD(h264PictureInfo, h264Data, num_ref_idx_l1_active_minus1);
    COPYFIELD(h264PictureInfo, h264Data, log2_max_frame_num_minus4);
    COPYFIELD(h264PictureInfo, h264Data, pic_order_cnt_type);
    COPYFIELD(h264PictureInfo, h264Data, log2_max_pic_order_cnt_lsb_minus4);
    COPYFIELD(h264PictureInfo, h264Data, delta_pic_order_always_zero_flag);
    COPYFIELD(h264PictureInfo, h264Data, direct_8x8_inference_flag);
    COPYFIELD(h264PictureInfo, h264Data, entropy_coding_mode_flag);
    COPYFIELD(h264PictureInfo, h264Data, pic_order_present_flag);
    COPYFIELD(h264PictureInfo, h264Data, deblocking_filter_control_present_flag);
    COPYFIELD(h264PictureInfo, h264Data, redundant_pic_cnt_present_flag);
    COPYFIELD(h264PictureInfo, h264Data, num_slice_groups_minus1);
    COPYFIELD(h264PictureInfo, h264Data, slice_group_map_type);
    COPYFIELD(h264PictureInfo, h264Data, slice_group_change_rate_minus1);
    h264PictureInfo->slice_group_map = h264Data->pMb2SliceGroupMap;
    COPYFIELD(h264PictureInfo, h264Data, fmo_aso_enable);
    COPYFIELD(h264PictureInfo, h264Data, scaling_matrix_present);
    COPYFIELD(h264PictureInfo, h264Data, qpprime_y_zero_transform_bypass_flag);
    memcpy(h264PictureInfo->scaling_lists_4x4, h264Data->WeightScale4x4, sizeof(h264Data->WeightScale4x4));
    memcpy(h264PictureInfo->scaling_lists_8x8, h264Data->WeightScale8x8, sizeof(h264Data->WeightScale8x8));

    //nvdec specific, not required for avp+vde
    COPYFIELD(h264PictureInfo, pd, nNumSlices);
    COPYFIELD(h264PictureInfo, pd, pSliceDataOffsets);
    for (i = 0; i < 16; i++)
    {
        NVH264DPBEntry *dpb_in = &h264Data->dpb[i];
        NvMediaReferenceFrameH264 *dpb_out = &h264PictureInfo->referenceFrames[i];
        frame_buffer_s* picbuf = (frame_buffer_s*)dpb_in->pPicBuf;

        COPYFIELD(dpb_out, dpb_in, FrameIdx);
        COPYFIELD(dpb_out, dpb_in, is_long_term);
        dpb_out->field_order_cnt[0] = dpb_in->FieldOrderCnt[0];
        dpb_out->field_order_cnt[1] = dpb_in->FieldOrderCnt[1];
        dpb_out->top_is_reference = !!(dpb_in->used_for_reference & 1);
        dpb_out->bottom_is_reference = !!(dpb_in->used_for_reference & 2);
        dpb_out->surface = picbuf ? picbuf->videoSurface : NULL;
    }
}

static NvBool cbDecodePicture(void *ptr, NVDPictureData *pd)
{
    test_video_parser_s *ctx = (test_video_parser_s*)ptr;
    NvMediaStatus status;
    frame_buffer_s *targetBuffer = NULL;

    NvMediaPictureInfoH264 picInfoH264;

    LOG_DBG("cbDecodePicture: Mixer:%p\n", ctx->mixer);

    if (pd->pCurrPic) {
        NvMediaBitstreamBuffer bitStreamBuffer[1];

        NvU64 timeEnd, timeStart;
        GetTimeMicroSec(&timeStart);

        SetParamsH264(ctx, pd, &picInfoH264);

        targetBuffer = (frame_buffer_s *)pd->pCurrPic;
        targetBuffer->frameNum = ctx->nPicNum;

        bitStreamBuffer[0].bitstream = (NvU8 *)pd->pBitstreamData;
        bitStreamBuffer[0].bitstreamBytes = pd->nBitstreamDataLen;

        LOG_DBG("DecodePicture %d Ptr:%p Surface:%p (stream ptr:%p size: %d)...\n",
            ctx->nPicNum, targetBuffer, targetBuffer->videoSurface, pd->pBitstreamData, pd->nBitstreamDataLen);
        ctx->nPicNum++;

        if (targetBuffer->videoSurface) {
            status = NvMediaVideoDecoderRender(
                ctx->decoder,                       // decoder
                targetBuffer->videoSurface,         // target
                (NvMediaPictureInfo *)&picInfoH264, // pictureInfo
                1,                                  // numBitstreamBuffers
                &bitStreamBuffer[0]);
            if (status != NVMEDIA_STATUS_OK) {
                LOG_ERR("Decode Picture: Decode failed: %d\n", status);
                return NV_FALSE;
            }
            LOG_DBG("Decode Picture: Decode done\n");
        } else {
            LOG_ERR("Decode Picture: Invalid target surface\n");
        }

        GetTimeMicroSec(&timeEnd);

        ctx->decodeCount++;

    } else {
        LOG_ERR("Decode Picture: No valid frame\n");
        return NV_FALSE;
    }

    return NV_TRUE;
}

static NvBool cbDisplayPicture(void *ptr, NVDPicBuff *p, NvS64 llPTS)
{
    test_video_parser_s *ctx = (test_video_parser_s*)ptr;
    frame_buffer_s* buffer = (frame_buffer_s*)p;

    if (p) {
        LOG_DBG("cbDisplayPicture: %d ctx:%p frame_buffer:%p Surface:%p\n", buffer->frameNum, ctx, buffer, buffer->videoSurface);
        DisplayFrame(ctx, buffer);
    } else if(!ctx->stopDecoding) {
        LOG_ERR("Display: Invalid buffer\n");
        return NV_FALSE;
    }
    return NV_TRUE;
}

static void cbUnhandledNALU(void *ptr, const NvU8 *buf, NvS32 size)
{

}

static void ReleaseFrame(test_video_parser_s *parser, NvMediaVideoSurface *videoSurface)
{
    int i;
    frame_buffer_s *p;

    for (i = 0; i < MAX_FRAMES; i++) {
        p = &parser->RefFrame[i];
        if (videoSurface == p->videoSurface) {
            cbRelease((void *)parser, (NVDPicBuff *)p);
            break;
        }
    }
}

static NvBool cbAllocPictureBuffer(void *ptr, NVDPicBuff **p)
{
    int i;
    test_video_parser_s *ctx = (test_video_parser_s*)ptr;

    LOG_DBG("cbAllocPictureBuffer: Mixer:%p\n", ctx->mixer);
    *p = (NVDPicBuff *) NULL;

    while(!ctx->stopDecoding && !signal_stop) {
        for (i = 0; i < ctx->nBuffers; i++) {
            if (!ctx->RefFrame[i].nRefs) {
                *p = (NVDPicBuff *) &ctx->RefFrame[i];
                ctx->RefFrame[i].nRefs++;
                ctx->RefFrame[i].index = i;
                LOG_DBG("Alloc picture index: %d Ptr:%p Surface:%p\n", i, *p, ctx->RefFrame[i].videoSurface);
                return NV_TRUE;
            }
        }
        if(ctx->surfaceType != NvMediaSurfaceType_R8G8B8A8_BottomOrigin) {
            //! [docs_eglstream:producer_gets_surface]
            NvMediaVideoSurface *videoSurface = NULL;
            NvMediaStatus status;
            status = NvMediaEglStreamProducerGetSurface(ctx->producer, &videoSurface, 100);
            if(status == NVMEDIA_STATUS_TIMED_OUT) {
                LOG_DBG("cbAllocPictureBuffer: NvMediaGetSurface waiting\n");
            } else if(status == NVMEDIA_STATUS_OK) {
                ReleaseFrame(ctx, videoSurface);
            } else
                goto failed;
            //! [docs_eglstream:producer_gets_surface]
        }
    }
failed:
    if(!ctx->stopDecoding && !signal_stop)
        LOG_ERR("Alloc picture failed\n");
    return NV_FALSE;
}

static void cbRelease(void *ptr, NVDPicBuff *p)
{
    frame_buffer_s* buffer = (frame_buffer_s*)p;

    LOG_DBG("Release picture: %d index: %d\n", buffer->frameNum, buffer->index);

    if (buffer->nRefs > 0)
        buffer->nRefs--;
}

static void cbAddRef(void *ptr, NVDPicBuff *p)
{
    frame_buffer_s* buffer = (frame_buffer_s*)p;

    LOG_DBG("AddRef picture: %d\n", buffer->frameNum);

    buffer->nRefs++;
}

NVDClientCb TestClientCb =
{
    &cbBeginSequence,
    &cbDecodePicture,
    &cbDisplayPicture,
    &cbUnhandledNALU,
    &cbAllocPictureBuffer,
    &cbRelease,
    &cbAddRef
};

static int Init(test_video_parser_s *parser)
{
    float defaultFrameRate = 30.0;

    printf("Init: Opening file: %s\n", parser->filename);

    parser->fp = fopen(parser->filename, "rb");
    if (!parser->fp) {
        printf("failed to open stream %s\n", parser->filename);
        return 0;
    }

    memset(&parser->nvsi, 0, sizeof(parser->nvsi));
    parser->lDispCounter = 0;
    parser->eCodec = NVCS_H264;

    // create video parser
    memset(&parser->nvdp, 0, sizeof(NVDParserParams));
    parser->nvdp.pClient = &TestClientCb;
    parser->nvdp.pClientCtx = parser;
    parser->nvdp.lErrorThreshold = 50;
    parser->nvdp.lReferenceClockRate = 0;
    parser->nvdp.eCodec = parser->eCodec;

    LOG_DBG("Init: video_parser_create\n");
    parser->ctx = video_parser_create(&parser->nvdp);
    if (!parser->ctx) {
        LOG_ERR("video_parser_create failed\n");
        return 0;
    }

    video_parser_set_attribute(parser->ctx,
                    NVDVideoParserAttribute_SetDefaultFramerate,
                    sizeof(float), &defaultFrameRate);

    LOG_DBG("Init: NvMediaDeviceCreate\n");
    parser->device = NvMediaDeviceCreate();
    if (!parser->device) {
        LOG_DBG("Unable to create device\n");
        return NV_FALSE;
    }

    parser->producer = NvMediaEglStreamProducerCreate(
        parser->device,
        parser->eglDisplay,
        parser->eglStream,
        parser->surfaceType,
        parser->renderWidth,
        parser->renderHeight);
    if(!parser->producer) {
        LOG_DBG("Unable to create producer\n");
        return NV_FALSE;
    }

    return 1;
}

static void Deinit(test_video_parser_s *parser)
{
    NvU32 i;

    video_parser_destroy(parser->ctx);

    if (parser->fp) {
        fclose(parser->fp);
        parser->fp = NULL;
    }

    for(i = 0; i < MAX_FRAMES; i++) {
        if (parser->RefFrame[i].videoSurface) {
            NvMediaVideoSurfaceDestroy(parser->RefFrame[i].videoSurface);
            parser->RefFrame[i].videoSurface = NULL;
        }
    }

    if (parser->decoder) {
        NvMediaVideoDecoderDestroy(parser->decoder);
        parser->decoder = NULL;
    }

    DisplayDestroy(parser);

    for (i = 0; i < MAX_RENDER_SURFACE; i++) {
        if(parser->renderSurfaces[i]) {
            NvMediaVideoSurfaceDestroy(parser->renderSurfaces[i]);
            parser->renderSurfaces[i] = NULL;
        }
    }

    if(parser->producer) {
        NvMediaEglStreamProducerDestroy(parser->producer);
        parser->producer = NULL;
    }

    if (parser->device) {
        NvMediaDeviceDestroy(parser->device);
        parser->device = NULL;
    }
}

static int Decode(test_video_parser_s *parser)
{
    NvU8 *bits;
    int i;
    NvU32 readSize = READ_SIZE;

    printf("Decode start\n");

    rewind(parser->fp);

    bits = malloc(readSize);
    if (!bits) {
        return 0;
    }

    for(i = 0; (i < parser->loop) || (parser->loop == -1); i++) {
        while (!feof(parser->fp) && !parser->stopDecoding && !signal_stop) {
            size_t len;
            bitstream_packet_s packet;

            memset(&packet, 0, sizeof(bitstream_packet_s));

            len = fread(bits, 1, readSize, parser->fp);

            packet.nDataLength = (NvS32) len;
            packet.pByteStream = bits;
            packet.bEOS = feof(parser->fp);
            packet.bPTSValid = 0;
            packet.llPTS = 0;
            if (!video_parser_parse(parser->ctx, &packet))
                return 0;
        }
        video_parser_flush(parser->ctx);
        DisplayFlush(parser);
        rewind(parser->fp);

        if(parser->loop != 1 && !signal_stop) {
            if(parser->stopDecoding) {
                parser->stopDecoding = NV_FALSE;
                parser->decodeCount = 0;
            }
            printf("loop count: %d/%d \n", i+1, parser->loop);
        } else
            break;
    }

    if (bits) {
        free(bits);
        bits = NULL;
    }

    return 1;
}

static NvBool DisplayInit(test_video_parser_s *parser, int width, int height, int videoWidth, int videoHeight)
{
    unsigned int features =  0;
    float aspectRatio = (float)width / (float)height;

    if (parser->aspectRatio != 0.0) {
        aspectRatio = parser->aspectRatio;
    }

    LOG_DBG("DisplayInit: %dx%d Aspect: %f\n", width, height, aspectRatio);

    /* default Deinterlace: Off/Weave */

    LOG_DBG("DisplayInit: Surface Renderer Mixer create\n");
    features |=
        (NVMEDIA_VIDEO_MIXER_FEATURE_DVD_MIXING_MODE |
         NVMEDIA_VIDEO_MIXER_FEATURE_SURFACE_RENDERING);
    parser->mixer = NvMediaVideoMixerCreate(
        parser->device,       // device,
        parser->renderWidth,  // mixerWidth
        parser->renderHeight, // mixerHeight
        aspectRatio,          // sourceAspectRatio
        videoWidth,           // primaryVideoWidth
        videoHeight,          // primaryVideoHeight
        0,                    // secondaryVideoWidth
        0,                    // secondaryVideoHeight
        0,                    // graphics0Width
        0,                    // graphics0Height
        0,                    // graphics1Width
        0,                    // graphics1Height
        features,
        NULL);
    if (!parser->mixer) {
        LOG_ERR("Unable to create mixer\n");
        return NV_FALSE;
    }

    LOG_DBG("DisplayInit: Mixer:%p\n", parser->mixer);

    return NV_TRUE;
}

static void DisplayDestroy(test_video_parser_s *parser)
{
    if (parser->mixer) {
        NvMediaVideoMixerDestroy(parser->mixer);
        parser->mixer = NULL;
    }
}

static void DisplayFlush(test_video_parser_s *parser)
{
    NvMediaVideoSurface *renderSurface = NULL;
    int i;

    if(parser->producer) {
        while(NvMediaEglStreamProducerGetSurface(parser->producer, &renderSurface, 0) == NVMEDIA_STATUS_OK) {
            if(parser->surfaceType == NvMediaSurfaceType_R8G8B8A8_BottomOrigin) {
                for(i = 0; i < MAX_RENDER_SURFACE; i++) {
                    if(!parser->freeRenderSurfaces[i]) {
                        parser->freeRenderSurfaces[i] = renderSurface;
                        break;
                    }
                }
            } else {
                ReleaseFrame(parser, renderSurface);
            }
        }
    }
}

static int SendRenderSurface(test_video_parser_s *parser, NvMediaVideoSurface *renderSurface, NvMediaTime *timestamp)
{
    NvMediaStatus status;

    LOG_DBG("SendRenderSurface: Start\n");

    if(parser->stopDecoding)
        return 1;

    //! [docs_eglstream:producer_posts_frame]

    status = NvMediaEglStreamProducerPostSurface(parser->producer, renderSurface, timestamp);

    if(status != NVMEDIA_STATUS_OK) {
        EGLint streamState = 0;
        if(!eglQueryStreamKHR(
                parser->eglDisplay,
                parser->eglStream,
                EGL_STREAM_STATE_KHR,
                &streamState)) {
            LOG_ERR("main: eglQueryStreamKHR EGL_STREAM_STATE_KHR failed\n");
        }
        if(streamState != EGL_STREAM_STATE_DISCONNECTED_KHR && !parser->stopDecoding)
            LOG_ERR("SendRenderSurface: NvMediaPostSurface failed\n");
        return 0;

    //! [docs_eglstream:producer_posts_frame]
    }

    LOG_DBG("SendRenderSurface: End\n");
    return 1;
}

static NvMediaVideoSurface *GetRenderSurface(test_video_parser_s *parser)
{
    NvMediaVideoSurface *renderSurface = NULL;
    NvMediaStatus status;
    int i;

    if(parser->surfaceType == NvMediaSurfaceType_R8G8B8A8_BottomOrigin) {
        for(i = 0; i < MAX_RENDER_SURFACE; i++) {
            if(parser->freeRenderSurfaces[i]) {
                renderSurface = parser->freeRenderSurfaces[i];
                parser->freeRenderSurfaces[i] = NULL;
                return renderSurface;
            }
        }
    }

    status = NvMediaEglStreamProducerGetSurface(parser->producer, &renderSurface, 100);
    if(status == NVMEDIA_STATUS_ERROR && parser->surfaceType == NvMediaSurfaceType_R8G8B8A8_BottomOrigin) {
        LOG_DBG("GetRenderSurface: NvMediaGetSurface waiting\n");
    }

    return renderSurface;
}

static void DisplayFrame(test_video_parser_s *parser, frame_buffer_s *frame)
{
    NvMediaPrimaryVideo primaryVideo;
    NvMediaTime timeStamp;
    NvMediaStatus status;
    NvMediaRect primarySourceRect = { 0, 0, parser->displayWidth, parser->displayHeight };
    NvBool releaseflag = 1;

    if (!frame || !frame->videoSurface) {
        if(!parser->stopDecoding)
            LOG_ERR("DisplayFrame: Invalid surface\n");
        return;
    }

    LOG_DBG("DisplayFrame: parser:%p surface:%p Mixer:%p\n", parser, frame->videoSurface, parser->mixer);

    NvMediaVideoSurface *renderSurface = GetRenderSurface(parser);

    LOG_DBG("DisplayFrame: Render surface: %p\n", renderSurface);

    if(!renderSurface) {
        if(parser->surfaceType == NvMediaSurfaceType_R8G8B8A8_BottomOrigin) {
             LOG_DBG("DisplayFrame: GetRenderSurface empty\n");
            return;
        } else {
            releaseflag = 0;
        }
    }

    /* Deinterlace Off/Weave */

    primaryVideo.pictureStructure = NVMEDIA_PICTURE_STRUCTURE_FRAME;
    primaryVideo.next = NULL;
    primaryVideo.current = frame->videoSurface;
    primaryVideo.previous = NULL;
    primaryVideo.previous2 = NULL;
    primaryVideo.srcRect = &primarySourceRect;
    primaryVideo.dstRect = NULL;

    LOG_DBG("DisplayFrame: parser->lDispCounter: %d\n", parser->lDispCounter);
    if (!parser->lDispCounter) {
        // Start timing at the first frame
        GetTimeUtil(&parser->baseTime);
    }

    NvAddTime(&parser->baseTime, (NvU64)((double)(parser->lDispCounter + 5) * parser->frameTimeUSec), &timeStamp);

    cbAddRef((void *)parser, (NVDPicBuff *)frame);

    if(parser->surfaceType == NvMediaSurfaceType_R8G8B8A8_BottomOrigin) {
        // Render to surface
        LOG_DBG("DisplayFrame: Render to surface\n");
        status = NvMediaVideoMixerRenderSurface(
            parser->mixer, // mixer
            renderSurface, // renderSurface
            NULL,          // background
            &primaryVideo, // primaryVideo
            NULL,          // secondaryVideo
            NULL,          // graphics0
            NULL);         // graphics1
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("DisplayFrame: NvMediaVideoMixerRender failed\n");
        }
        LOG_DBG("DisplayFrame: Render to surface - Done\n");
    }

    if(!SendRenderSurface(parser,
                          parser->surfaceType == NvMediaSurfaceType_R8G8B8A8_BottomOrigin?
                          renderSurface: frame->videoSurface,
                          &timeStamp)) {
        parser->stopDecoding = 1;
    }
    LOG_DBG("DisplayFrame: SendRenderSurface - Done\n");

    if(releaseflag) {
        ReleaseFrame(parser, parser->surfaceType == NvMediaSurfaceType_R8G8B8A8_BottomOrigin?
                        primaryVideo.current:renderSurface);
    }

    parser->lDispCounter++;

    LOG_DBG("DisplayFrame: End\n");
}

static NvU32 DecodeThread(void *parserArg)
{
    test_video_parser_s *parser = (test_video_parser_s *)parserArg;

    LOG_DBG("DecodeThread: Init\n");
    if (!Init(parser)) {
        printf("DecodeThread - Init failed\n");
        // Signal end of decode
        *parser->decodeFinished = NV_TRUE;
        return 0;
    }

    LOG_DBG("DecodeThread: Decode\n");

    if (!Decode(parser)) {
        printf("DecodeThread - Decode failed\n");
    }

    // Signal end of decode
    *parser->decodeFinished = NV_TRUE;

    return 0;
}

int VideoDecoderInit(volatile NvBool *decodeFinished, EGLDisplay eglDisplay, EGLStreamKHR eglStream, TestArgs *args)
{
    test_video_parser_s *parser = &g_parser;

    // Set parser default parameters
    parser->filename = args->infile;
    parser->loop = args->prodLoop? args->prodLoop : 1;
    parser->eglDisplay = eglDisplay;
    parser->eglStream = eglStream;
    parser->decodeFinished = decodeFinished;
    parser->eglOutput = args->egloutputConsumer ? 1 : 0;

    parser->surfaceType = args->prodSurfaceType;

    // Create video decoder thread
    if(IsFailed(NvThreadCreate(&parser->thread, &DecodeThread, (void *)parser, NV_THREAD_PRIORITY_NORMAL))) {
        LOG_ERR("VideoDecoderInit: Unable to create video decoder thread\n");
        return 0;
    }

    return 1;
}

void VideoDecoderDeinit() {
    test_video_parser_s *parser = &g_parser;

    LOG_DBG("main: Deinit\n");

    Deinit(parser);
}

void VideoDecoderStop() {
    test_video_parser_s *parser = &g_parser;

    if(parser->thread) {
        parser->stopDecoding = NV_TRUE;
        LOG_DBG("wait for video decoder thread exit\n");
        NvThreadDestroy(parser->thread);
    }
}

void VideoDecoderFlush() {
    test_video_parser_s *parser = &g_parser;

    DisplayFlush(parser);
}
