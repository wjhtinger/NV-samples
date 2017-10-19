/*
 * Copyright (c) 2011-2017, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "cmdline.h"
#include "deinterlace_utils.h"
#include "log_utils.h"
#include "misc_utils.h"
#include "nvmedia.h"
#include "surf_utils.h"
#include "video_parser.h"

#ifdef NVMEDIA_QNX
#include "screen_init.h"
#endif

/* Max number of decoder reference buffers */
#define MAX_DEC_REF_BUFFERS         (16)
/* Max number of buffers for display */
#define MAX_DISPLAY_BUFFERS         (4)
/* Max number of buffers between decoder and deinterlace */
#define MAX_DEC_DEINTER_BUFFERS     (MAX_DISPLAY_BUFFERS)
/* Total number of buffers for decoder to operate.*/
#define MAX_DEC_BUFFERS             (MAX_DEC_REF_BUFFERS + MAX_DEC_DEINTER_BUFFERS + 1)
#define READ_SIZE                   (32 * 1024)
// For VP8 ivf file parsing
#define IVF_FILE_HDR_SIZE           32
#define IVF_FRAME_HDR_SIZE          12

typedef struct _VideoDemoTestCtx {
    video_parser_context_s *parser;
    NVDSequenceInfo         nvsi;
    NVDParserParams         nvdp;
    NvVideoCompressionStd   eCodec;

    //  Stream params
    FILE                    *file;
    char                    *filename;
    NvBool                  bVC1SimpleMainProfile;
    char                    *OutputYUVFilename;
    NvS64                   fileSize;
    NvBool                  bRCVfile;

    // Decoder params
    int                      decodeWidth;
    int                      decodeHeight;
    int                      displayWidth;
    int                      displayHeight;
    NvMediaVideoDecoder     *decoder;
    int                      decodeCount;
    float                    totalDecodeTime;
    NvBool                   stopDecoding;
    NvBool                   showDecodeTimimg;
    int                      numFramesToDecode;
    int                      deinterlace;
    int                      deinterlaceAlgo;
    NvBool                   inverceTelecine;
    DeinterlaceContext      *deinterlaceCtx;
    int                      loop;

    // Picture buffer params
    int                      nBuffers;
    int                      nPicNum;
    int                      sumCompressedLen;
    FrameBuffer              RefFrame[MAX_DEC_BUFFERS];

    // Display params
    NvMediaDevice           *device;
    NvMediaVideoMixer       *mixer;
    NvMediaVideoOutput      *output;
    NvMediaVideoSurface     *renderSurfaces[MAX_DISPLAY_BUFFERS];
    NvMediaVideoSurface     *freeRenderSurfaces[MAX_DISPLAY_BUFFERS];
    int                      lDispCounter;
    NvMediaTime              baseTime;
    double                   frameTimeUSec;
    float                    aspectRatio;
    NvBool                   videoFullRangeFlag;
    int                      colorPrimaries;
    NvBool                   displayEnabled;
    unsigned int             displayId;
    NvMediaBool              displayDeviceEnabled;
    unsigned int             windowId;
    NvBool                   positionSpecifiedFlag;
    NvMediaRect              position;
    unsigned int             depth;
    int                      monitorWidth;
    int                      monitorHeight;
    unsigned int             filterQuality;

#ifdef NVMEDIA_QNX
    // Screen params
    NvBool                   bScreen;
    ScreenArgs               screenArgs;
    NvBool                   bYuv2Rgb;
#endif
} VideoDemoTestCtx;

int     signal_stop = 0;

#ifdef NVMEDIA_QNX
/* Used for calculating explicit delays to achieve correct FPS
 * g_timeBase : timestamp when first frame is posted
 * g_timeNow  : timestamp before posting subsequent frame
 */
uint64_t g_timeBase = UINT64_MAX, g_timeNow = UINT64_MAX;

/* Count of frames after which measured FPS value is printed */
#define FPS_DISPLAY_PERIOD 500

/* For measuring the frames per sec after every FPS_DISPLAY_PERIOD frames */
uint64_t g_tFPS;
#endif

NvS32   cbBeginSequence(void *ptr, const NVDSequenceInfo *pnvsi);
NvBool  cbDecodePicture(void *ptr, NVDPictureData *pd);
NvBool  cbDisplayPicture(void *ptr, NVDPicBuff *p, NvS64 llPTS);
void    cbUnhandledNALU(void *ptr, const NvU8 *buf, NvS32 size);
NvBool  cbAllocPictureBuffer(void *ptr, NVDPicBuff **p);
void    cbRelease(void *ptr, NVDPicBuff *p);
void    cbAddRef(void *ptr, NVDPicBuff *p);
NvBool  cbGetBackwardUpdates(void *ptr, NVDPictureData *pd);

int     Init(VideoDemoTestCtx *ctx, TestArgs *testArgs);
void    Deinit(VideoDemoTestCtx *parser);
int     Decode(VideoDemoTestCtx *parser);
void    Stats(VideoDemoTestCtx *parser);

static int MixerInit(VideoDemoTestCtx *ctx, int width, int height, int videoWidth, int videoHeight);
int     DisplayInit(VideoDemoTestCtx *parser, int width, int height, int videoWidth, int videoHeight);
void    DisplayDestroy(VideoDemoTestCtx *parser);
void    DisplayFrame(VideoDemoTestCtx *parser, FrameBuffer *frame);
void    DisplayFlush(VideoDemoTestCtx *parser);
int     StreamVC1SimpleProfile(VideoDemoTestCtx *ctx);

#ifdef NVMEDIA_QNX
/*
 * Displays Frames by posting screen buffers.
 * index refers to the decoded frame from RefFrame pool.
 */
void    DisplayFrameScreen(VideoDemoTestCtx *ctx, FrameBuffer *frame); //int index);
#endif

static char *Strcasestr(char *haystack, char *needle)
{
    char *haystack_temp, *needle_temp, *res;
    int pos;

    if(!haystack || !strlen(haystack) || !needle || !strlen(needle))
        return NULL;

    haystack_temp = malloc(strlen(haystack) + 1);
    if(!haystack_temp)
        return NULL;

    needle_temp = malloc(strlen(needle) + 1);
    if(!needle_temp) {
        free(haystack_temp);
        return NULL;
    }

    pos = 0;
    while(haystack[pos]) {
        haystack_temp[pos] = toupper(haystack[pos]);
        pos++;
    }
    haystack_temp[pos] = 0;

    pos = 0;
    while(needle[pos]) {
        needle_temp[pos] = toupper(needle[pos]);
        pos++;
    }
    needle_temp[pos] = 0;

    res = strstr(haystack_temp, needle_temp);
    res = res ? (res - haystack_temp) + haystack : NULL;

    free(haystack_temp);
    free(needle_temp);

    return res;
}

static void SetParamsVC1(VideoDemoTestCtx *ctx, NVDPictureData *pd, NvMediaPictureInfo *pictureInfo)
{
    NVVC1PictureData *vc1Data = &pd->CodecSpecific.vc1;
    NvMediaPictureInfoVC1 *vc1PictureInfo = (NvMediaPictureInfoVC1 *)pictureInfo;

    if (vc1Data->pForwardRef)
        vc1PictureInfo->forward_reference = ((FrameBuffer *)vc1Data->pForwardRef)->videoSurface;
    else
        vc1PictureInfo->forward_reference = NULL;

    if (vc1Data->pBackwardRef)
        vc1PictureInfo->backward_reference = ((FrameBuffer *)vc1Data->pBackwardRef)->videoSurface;
    else
        vc1PictureInfo->backward_reference = NULL;

    if(vc1Data->pRangeMapped)
        vc1PictureInfo->range_mapped = ((FrameBuffer *)vc1Data->pRangeMapped)->videoSurface;
    else
        vc1PictureInfo->range_mapped = NULL;

    vc1PictureInfo->picture_type = vc1Data->ptype;
    vc1PictureInfo->frame_coding_mode = pd->progressive_frame ? 0 :
                                        pd->field_pic_flag    ? 3 :
                                                                2 ;
    vc1PictureInfo->bottom_field_flag = pd->bottom_field_flag;
    COPYFIELD(vc1PictureInfo, vc1Data, postprocflag);
    COPYFIELD(vc1PictureInfo, vc1Data, pulldown);
    COPYFIELD(vc1PictureInfo, vc1Data, interlace);
    COPYFIELD(vc1PictureInfo, vc1Data, tfcntrflag);
    COPYFIELD(vc1PictureInfo, vc1Data, finterpflag);
    COPYFIELD(vc1PictureInfo, vc1Data, psf);
    COPYFIELD(vc1PictureInfo, vc1Data, dquant);
    COPYFIELD(vc1PictureInfo, vc1Data, panscan_flag);
    COPYFIELD(vc1PictureInfo, vc1Data, refdist_flag);
    COPYFIELD(vc1PictureInfo, vc1Data, quantizer);
    COPYFIELD(vc1PictureInfo, vc1Data, extended_mv);
    COPYFIELD(vc1PictureInfo, vc1Data, extended_dmv);
    COPYFIELD(vc1PictureInfo, vc1Data, overlap);
    COPYFIELD(vc1PictureInfo, vc1Data, vstransform);
    COPYFIELD(vc1PictureInfo, vc1Data, loopfilter);
    COPYFIELD(vc1PictureInfo, vc1Data, fastuvmc);
    COPYFIELD(vc1PictureInfo, vc1Data, range_mapy_flag);
    COPYFIELD(vc1PictureInfo, vc1Data, range_mapy);
    COPYFIELD(vc1PictureInfo, vc1Data, range_mapuv_flag);
    COPYFIELD(vc1PictureInfo, vc1Data, range_mapuv);
    COPYFIELD(vc1PictureInfo, vc1Data, multires);
    COPYFIELD(vc1PictureInfo, vc1Data, syncmarker);
    COPYFIELD(vc1PictureInfo, vc1Data, rangered);
    COPYFIELD(vc1PictureInfo, vc1Data, rangeredfrm);
    COPYFIELD(vc1PictureInfo, vc1Data, maxbframes);
    //nvdec specific, not required for avp+vde
    COPYFIELD(vc1PictureInfo, pd, nNumSlices);
    COPYFIELD(vc1PictureInfo, pd, pSliceDataOffsets);
}

static void SetParamsVP8(VideoDemoTestCtx *ctx, NVDPictureData *pd, NvMediaPictureInfo *pictureInfo)
{
    NvU32 i;
    NVVP8PictureData *vp8Data = &pd->CodecSpecific.vp8;
    NvMediaPictureInfoVP8 *vp8PictureInfo = (NvMediaPictureInfoVP8 *)pictureInfo;

    if(vp8Data->pLastRef)
        vp8PictureInfo->LastReference = ((FrameBuffer *)vp8Data->pLastRef)->videoSurface;
    else
        vp8PictureInfo->LastReference = ((FrameBuffer *)pd->pCurrPic)->videoSurface;

    if(vp8Data->pGoldenRef)
        vp8PictureInfo->GoldenReference = ((FrameBuffer *)vp8Data->pGoldenRef)->videoSurface;
    else
        vp8PictureInfo->GoldenReference = ((FrameBuffer *)pd->pCurrPic)->videoSurface;

    if(vp8Data->pAltRef)
        vp8PictureInfo->AltReference = ((FrameBuffer *)vp8Data->pAltRef)->videoSurface;
    else
        vp8PictureInfo->AltReference = ((FrameBuffer *)pd->pCurrPic)->videoSurface;

    COPYFIELD(vp8PictureInfo, vp8Data, key_frame);
    COPYFIELD(vp8PictureInfo, vp8Data, version);
    COPYFIELD(vp8PictureInfo, vp8Data, show_frame);
    COPYFIELD(vp8PictureInfo, vp8Data, clamp_type);
    COPYFIELD(vp8PictureInfo, vp8Data, segmentation_enabled);
    COPYFIELD(vp8PictureInfo, vp8Data, update_mb_seg_map);
    COPYFIELD(vp8PictureInfo, vp8Data, update_mb_seg_data);
    COPYFIELD(vp8PictureInfo, vp8Data, update_mb_seg_abs);
    COPYFIELD(vp8PictureInfo, vp8Data, filter_type);
    COPYFIELD(vp8PictureInfo, vp8Data, loop_filter_level);
    COPYFIELD(vp8PictureInfo, vp8Data, sharpness_level);
    COPYFIELD(vp8PictureInfo, vp8Data, mode_ref_lf_delta_enabled);
    COPYFIELD(vp8PictureInfo, vp8Data, mode_ref_lf_delta_update);
    COPYFIELD(vp8PictureInfo, vp8Data, num_of_partitions);
    COPYFIELD(vp8PictureInfo, vp8Data, dequant_index);
    memcpy(vp8PictureInfo->deltaq, vp8Data->deltaq,sizeof(vp8Data->deltaq));
    COPYFIELD(vp8PictureInfo, vp8Data, golden_ref_frame_sign_bias);
    COPYFIELD(vp8PictureInfo, vp8Data, alt_ref_frame_sign_bias);
    COPYFIELD(vp8PictureInfo, vp8Data, refresh_entropy_probs);
    COPYFIELD(vp8PictureInfo, vp8Data, CbrHdrBedValue);
    COPYFIELD(vp8PictureInfo, vp8Data, CbrHdrBedRange);
    memcpy(vp8PictureInfo->mb_seg_tree_probs, vp8Data->mb_seg_tree_probs, sizeof(vp8Data->mb_seg_tree_probs));
    memcpy(vp8PictureInfo->seg_feature, vp8Data->seg_feature, sizeof(vp8Data->seg_feature));
    memcpy(vp8PictureInfo->ref_lf_deltas, vp8Data->ref_lf_deltas, sizeof(vp8Data->ref_lf_deltas));
    memcpy(vp8PictureInfo->mode_lf_deltas, vp8Data->mode_lf_deltas, sizeof(vp8Data->mode_lf_deltas));
    COPYFIELD(vp8PictureInfo, vp8Data, BitsConsumed);
    memcpy(vp8PictureInfo->AlignByte, vp8Data->AlignByte, sizeof(vp8Data->AlignByte));
    COPYFIELD(vp8PictureInfo, vp8Data, hdr_partition_size);
    COPYFIELD(vp8PictureInfo, vp8Data, hdr_start_offset);
    COPYFIELD(vp8PictureInfo, vp8Data, hdr_processed_offset);

    for (i = 0; i < 8; i++) {
        vp8PictureInfo->coeff_partition_size[i] = vp8Data->coeff_partition_size[i];
        vp8PictureInfo->coeff_partition_start_offset[i] = vp8Data->coeff_partition_start_offset[i];
    }
    //nvdec specific, not required for avp+vde
    COPYFIELD(vp8PictureInfo, pd, nNumSlices);
    COPYFIELD(vp8PictureInfo, pd, pSliceDataOffsets);
}

static void SetParamsVP9(VideoDemoTestCtx *ctx, NVDPictureData *pd, NvMediaPictureInfo *pictureInfo)
{
    NvU32 i, j;
    NVVP9PictureData *vp9Data = &pd->CodecSpecific.vp9;
    NvMediaPictureInfoVP9 *vp9PictureInfo = (NvMediaPictureInfoVP9 *)pictureInfo;

    if(vp9Data->pLastRef)
        vp9PictureInfo->LastReference = ((FrameBuffer *)vp9Data->pLastRef)->videoSurface;
    else
        vp9PictureInfo->LastReference = ((FrameBuffer *)pd->pCurrPic)->videoSurface;

    if(vp9Data->pGoldenRef)
        vp9PictureInfo->GoldenReference = ((FrameBuffer *)vp9Data->pGoldenRef)->videoSurface;
    else
        vp9PictureInfo->GoldenReference = ((FrameBuffer *)pd->pCurrPic)->videoSurface;

    if(vp9Data->pAltRef)
        vp9PictureInfo->AltReference = ((FrameBuffer *)vp9Data->pAltRef)->videoSurface;
    else
        vp9PictureInfo->AltReference = ((FrameBuffer *)pd->pCurrPic)->videoSurface;

    ((FrameBuffer *)pd->pCurrPic)->width = vp9Data->width;
    ((FrameBuffer *)pd->pCurrPic)->height = vp9Data->height;

    COPYFIELD(vp9PictureInfo, vp9Data, width);
    COPYFIELD(vp9PictureInfo, vp9Data, height);
    vp9PictureInfo->ref0_width = vp9Data->pLastRef ? ((FrameBuffer *)vp9Data->pLastRef)->width : 0;
    vp9PictureInfo->ref0_height = vp9Data->pLastRef ? ((FrameBuffer *)vp9Data->pLastRef)->height : 0;
    vp9PictureInfo->ref1_width = vp9Data->pGoldenRef ? ((FrameBuffer *)vp9Data->pGoldenRef)->width : 0;
    vp9PictureInfo->ref1_height = vp9Data->pGoldenRef ? ((FrameBuffer *)vp9Data->pGoldenRef)->height : 0;
    vp9PictureInfo->ref2_width = vp9Data->pAltRef ? ((FrameBuffer *)vp9Data->pAltRef)->width : 0;
    vp9PictureInfo->ref2_height = vp9Data->pAltRef ? ((FrameBuffer *)vp9Data->pAltRef)->height : 0;

    COPYFIELD(vp9PictureInfo, vp9Data, keyFrame);
    COPYFIELD(vp9PictureInfo, vp9Data, bit_depth);
    COPYFIELD(vp9PictureInfo, vp9Data, prevIsKeyFrame);
    COPYFIELD(vp9PictureInfo, vp9Data, PrevShowFrame);
    COPYFIELD(vp9PictureInfo, vp9Data, resolutionChange);
    COPYFIELD(vp9PictureInfo, vp9Data, errorResilient);
    COPYFIELD(vp9PictureInfo, vp9Data, intraOnly);
    COPYFIELD(vp9PictureInfo, vp9Data, frameContextIdx);
    memcpy(vp9PictureInfo->refFrameSignBias, vp9Data->refFrameSignBias, sizeof(vp9Data->refFrameSignBias));
    COPYFIELD(vp9PictureInfo, vp9Data, loopFilterLevel);
    COPYFIELD(vp9PictureInfo, vp9Data, loopFilterSharpness);
    COPYFIELD(vp9PictureInfo, vp9Data, qpYAc);
    COPYFIELD(vp9PictureInfo, vp9Data, qpYDc);
    COPYFIELD(vp9PictureInfo, vp9Data, qpChAc);
    COPYFIELD(vp9PictureInfo, vp9Data, qpChDc);
    COPYFIELD(vp9PictureInfo, vp9Data, lossless);
    COPYFIELD(vp9PictureInfo, vp9Data, transform_mode);
    COPYFIELD(vp9PictureInfo, vp9Data, allow_high_precision_mv);
    COPYFIELD(vp9PictureInfo, vp9Data, allow_comp_inter_inter);
    COPYFIELD(vp9PictureInfo, vp9Data, mcomp_filter_type);
    COPYFIELD(vp9PictureInfo, vp9Data, comp_pred_mode);
    COPYFIELD(vp9PictureInfo, vp9Data, comp_fixed_ref);
    COPYFIELD(vp9PictureInfo, vp9Data, comp_var_ref[0]);
    COPYFIELD(vp9PictureInfo, vp9Data, comp_var_ref[1]);
    COPYFIELD(vp9PictureInfo, vp9Data, log2_tile_columns);
    COPYFIELD(vp9PictureInfo, vp9Data, log2_tile_rows);
    COPYFIELD(vp9PictureInfo, vp9Data, segmentEnabled);
    COPYFIELD(vp9PictureInfo, vp9Data, segmentMapUpdate);
    COPYFIELD(vp9PictureInfo, vp9Data, segmentMapTemporalUpdate);
    COPYFIELD(vp9PictureInfo, vp9Data, segmentFeatureMode);
    for (i = 0; i < 8; i++) {
        for (j = 0; j < 4; j++) {
            COPYFIELD(vp9PictureInfo, vp9Data, segmentFeatureEnable[i][j]);
            COPYFIELD(vp9PictureInfo, vp9Data, segmentFeatureData[i][j]);
        }
    }
    COPYFIELD(vp9PictureInfo, vp9Data, modeRefLfEnabled);
    for (j = 0; j < 4; j++) {
        COPYFIELD(vp9PictureInfo, vp9Data, mbRefLfDelta[j]);
    }
    COPYFIELD(vp9PictureInfo, vp9Data, mbModeLfDelta[0]);
    COPYFIELD(vp9PictureInfo, vp9Data, mbModeLfDelta[1]);
    COPYFIELD(vp9PictureInfo, vp9Data, offsetToDctParts);
    COPYFIELD(vp9PictureInfo, vp9Data, frameTagSize);

    if (sizeof(vp9PictureInfo->entropy) == sizeof(vp9Data->entropy)) {
        memcpy(&vp9PictureInfo->entropy, &vp9Data->entropy, sizeof(vp9Data->entropy));
    }
}

static void SetParamsMPEG2(VideoDemoTestCtx *ctx, NVDPictureData *pd, NvMediaPictureInfo *pictureInfo)
{
    NVMPEG2PictureData *mpeg2Data = &pd->CodecSpecific.mpeg2;
    NvMediaPictureInfoMPEG1Or2 *mpeg2PictureInfo = (NvMediaPictureInfoMPEG1Or2 *)pictureInfo;

    if (mpeg2Data->pForwardRef)
        mpeg2PictureInfo->forward_reference = ((FrameBuffer *)mpeg2Data->pForwardRef)->videoSurface;
    else
        mpeg2PictureInfo->forward_reference = ((FrameBuffer *)pd->pCurrPic)->videoSurface;

    if (mpeg2Data->pBackwardRef)
        mpeg2PictureInfo->backward_reference = ((FrameBuffer *)mpeg2Data->pBackwardRef)->videoSurface;
    else
        mpeg2PictureInfo->backward_reference = ((FrameBuffer *)pd->pCurrPic)->videoSurface;

    COPYFIELD(mpeg2PictureInfo, mpeg2Data, picture_structure);
    COPYFIELD(mpeg2PictureInfo, mpeg2Data, picture_coding_type);
    COPYFIELD(mpeg2PictureInfo, mpeg2Data, intra_dc_precision);
    COPYFIELD(mpeg2PictureInfo, mpeg2Data, frame_pred_frame_dct);
    COPYFIELD(mpeg2PictureInfo, mpeg2Data, concealment_motion_vectors);
    COPYFIELD(mpeg2PictureInfo, mpeg2Data, intra_vlc_format);
    COPYFIELD(mpeg2PictureInfo, mpeg2Data, alternate_scan);
    COPYFIELD(mpeg2PictureInfo, mpeg2Data, q_scale_type);
    COPYFIELD(mpeg2PictureInfo, pd, top_field_first);
    COPYFIELD(mpeg2PictureInfo, mpeg2Data, full_pel_forward_vector);
    COPYFIELD(mpeg2PictureInfo, mpeg2Data, full_pel_backward_vector);
    COPYFIELD(mpeg2PictureInfo, mpeg2Data, f_code[0][0]);
    COPYFIELD(mpeg2PictureInfo, mpeg2Data, f_code[0][1]);
    COPYFIELD(mpeg2PictureInfo, mpeg2Data, f_code[1][0]);
    COPYFIELD(mpeg2PictureInfo, mpeg2Data, f_code[1][1]);

    memcpy(mpeg2PictureInfo->intra_quantizer_matrix, mpeg2Data->QuantMatrixIntra, 64);
    memcpy(mpeg2PictureInfo->non_intra_quantizer_matrix, mpeg2Data->QuantMatrixInter, 64);
    //nvdec specific, not required for avp+vde
    COPYFIELD(mpeg2PictureInfo, pd, nNumSlices);
    COPYFIELD(mpeg2PictureInfo, pd, pSliceDataOffsets);
    COPYFIELD(mpeg2PictureInfo, mpeg2Data, flag_slices_across_multiple_rows);
}

static void SetParamsH264(VideoDemoTestCtx *ctx, NVDPictureData *pd, NvMediaPictureInfo *pictureInfo)
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
        FrameBuffer* picbuf = (FrameBuffer*)dpb_in->pPicBuf;

        COPYFIELD(dpb_out, dpb_in, FrameIdx);
        COPYFIELD(dpb_out, dpb_in, is_long_term);
        dpb_out->field_order_cnt[0] = dpb_in->FieldOrderCnt[0];
        dpb_out->field_order_cnt[1] = dpb_in->FieldOrderCnt[1];
        dpb_out->top_is_reference = !!(dpb_in->used_for_reference & 1);
        dpb_out->bottom_is_reference = !!(dpb_in->used_for_reference & 2);
        dpb_out->surface = picbuf ? picbuf->videoSurface : NULL;
    }

    // frame type
    h264PictureInfo->frameType = h264Data->slice_type;
}

static void SetParamsH265(VideoDemoTestCtx *ctx, NVDPictureData *pd, NvMediaPictureInfo *pictureInfo)
{
    NVHEVCPictureData *h265Data = &pd->CodecSpecific.hevc;
    NvMediaPictureInfoH265 *h265PictureInfo = (NvMediaPictureInfoH265 *)pictureInfo;
    NvU32 i;
    FrameBuffer* picbuf;

    // sps
    COPYFIELD(h265PictureInfo, h265Data, pic_width_in_luma_samples);
    COPYFIELD(h265PictureInfo, h265Data, pic_height_in_luma_samples);
    COPYFIELD(h265PictureInfo, h265Data, log2_min_luma_coding_block_size_minus3);
    COPYFIELD(h265PictureInfo, h265Data, log2_diff_max_min_luma_coding_block_size);
    COPYFIELD(h265PictureInfo, h265Data, log2_min_transform_block_size_minus2);
    COPYFIELD(h265PictureInfo, h265Data, log2_diff_max_min_transform_block_size);
    COPYFIELD(h265PictureInfo, h265Data, pcm_enabled_flag);
    COPYFIELD(h265PictureInfo, h265Data, log2_min_pcm_luma_coding_block_size_minus3);
    COPYFIELD(h265PictureInfo, h265Data, log2_diff_max_min_pcm_luma_coding_block_size);
    COPYFIELD(h265PictureInfo, h265Data, bit_depth_luma);
    COPYFIELD(h265PictureInfo, h265Data, bit_depth_chroma);
    COPYFIELD(h265PictureInfo, h265Data, pcm_sample_bit_depth_luma_minus1);
    COPYFIELD(h265PictureInfo, h265Data, pcm_sample_bit_depth_chroma_minus1);
    COPYFIELD(h265PictureInfo, h265Data, pcm_loop_filter_disabled_flag);
    COPYFIELD(h265PictureInfo, h265Data, strong_intra_smoothing_enabled_flag);
    COPYFIELD(h265PictureInfo, h265Data, max_transform_hierarchy_depth_intra);
    COPYFIELD(h265PictureInfo, h265Data, max_transform_hierarchy_depth_inter);
    COPYFIELD(h265PictureInfo, h265Data, amp_enabled_flag);
    COPYFIELD(h265PictureInfo, h265Data, separate_colour_plane_flag);
    COPYFIELD(h265PictureInfo, h265Data, log2_max_pic_order_cnt_lsb_minus4);
    COPYFIELD(h265PictureInfo, h265Data, num_short_term_ref_pic_sets);
    COPYFIELD(h265PictureInfo, h265Data, long_term_ref_pics_present_flag);
    COPYFIELD(h265PictureInfo, h265Data, num_long_term_ref_pics_sps);
    COPYFIELD(h265PictureInfo, h265Data, sps_temporal_mvp_enabled_flag);
    COPYFIELD(h265PictureInfo, h265Data, sample_adaptive_offset_enabled_flag);
    COPYFIELD(h265PictureInfo, h265Data, scaling_list_enable_flag);
    COPYFIELD(h265PictureInfo, h265Data, chroma_format_idc);
    // pps
    COPYFIELD(h265PictureInfo, h265Data, dependent_slice_segments_enabled_flag);
    COPYFIELD(h265PictureInfo, h265Data, slice_segment_header_extension_present_flag);
    COPYFIELD(h265PictureInfo, h265Data, sign_data_hiding_enabled_flag);
    COPYFIELD(h265PictureInfo, h265Data, cu_qp_delta_enabled_flag);
    COPYFIELD(h265PictureInfo, h265Data, diff_cu_qp_delta_depth);
    COPYFIELD(h265PictureInfo, h265Data, init_qp_minus26);
    COPYFIELD(h265PictureInfo, h265Data, pps_cb_qp_offset);
    COPYFIELD(h265PictureInfo, h265Data, pps_cr_qp_offset);
    COPYFIELD(h265PictureInfo, h265Data, constrained_intra_pred_flag);
    COPYFIELD(h265PictureInfo, h265Data, weighted_pred_flag);
    COPYFIELD(h265PictureInfo, h265Data, weighted_bipred_flag);
    COPYFIELD(h265PictureInfo, h265Data, transform_skip_enabled_flag);
    COPYFIELD(h265PictureInfo, h265Data, transquant_bypass_enabled_flag);
    COPYFIELD(h265PictureInfo, h265Data, entropy_coding_sync_enabled_flag);
    COPYFIELD(h265PictureInfo, h265Data, log2_parallel_merge_level_minus2);
    COPYFIELD(h265PictureInfo, h265Data, num_extra_slice_header_bits);
    COPYFIELD(h265PictureInfo, h265Data, loop_filter_across_tiles_enabled_flag);
    COPYFIELD(h265PictureInfo, h265Data, loop_filter_across_slices_enabled_flag);
    COPYFIELD(h265PictureInfo, h265Data, output_flag_present_flag);
    COPYFIELD(h265PictureInfo, h265Data, num_ref_idx_l0_default_active_minus1);
    COPYFIELD(h265PictureInfo, h265Data, num_ref_idx_l1_default_active_minus1);
    COPYFIELD(h265PictureInfo, h265Data, lists_modification_present_flag);
    COPYFIELD(h265PictureInfo, h265Data, cabac_init_present_flag);
    COPYFIELD(h265PictureInfo, h265Data, pps_slice_chroma_qp_offsets_present_flag);
    COPYFIELD(h265PictureInfo, h265Data, deblocking_filter_control_present_flag);
    COPYFIELD(h265PictureInfo, h265Data, deblocking_filter_override_enabled_flag);
    COPYFIELD(h265PictureInfo, h265Data, pps_deblocking_filter_disabled_flag);
    COPYFIELD(h265PictureInfo, h265Data, pps_beta_offset_div2);
    COPYFIELD(h265PictureInfo, h265Data, pps_tc_offset_div2);
    COPYFIELD(h265PictureInfo, h265Data, tiles_enabled_flag);
    COPYFIELD(h265PictureInfo, h265Data, uniform_spacing_flag);
    COPYFIELD(h265PictureInfo, h265Data, num_tile_columns_minus1);
    COPYFIELD(h265PictureInfo, h265Data, num_tile_rows_minus1);
    memcpy(h265PictureInfo->column_width_minus1, h265Data->column_width_minus1, sizeof(NvU16)*22);
    memcpy(h265PictureInfo->row_height_minus1, h265Data->row_height_minus1, sizeof(NvU16)*20);

    // RefPicSets
    COPYFIELD(h265PictureInfo, h265Data, iCur);
    COPYFIELD(h265PictureInfo, h265Data, IDRPicFlag);
    COPYFIELD(h265PictureInfo, h265Data, RAPPicFlag);
    COPYFIELD(h265PictureInfo, h265Data, NumDeltaPocsOfRefRpsIdx);
    COPYFIELD(h265PictureInfo, h265Data, NumPocTotalCurr);
    COPYFIELD(h265PictureInfo, h265Data, NumPocStCurrBefore);
    COPYFIELD(h265PictureInfo, h265Data, NumPocStCurrAfter);
    COPYFIELD(h265PictureInfo, h265Data, NumPocLtCurr);
    COPYFIELD(h265PictureInfo, h265Data, NumBitsToSkip);
    COPYFIELD(h265PictureInfo, h265Data, CurrPicOrderCntVal);

    for (i = 0; i < 16; i++)
    {
        picbuf = (FrameBuffer*)h265Data->RefPics[i];
        h265PictureInfo->RefPics[i] = picbuf ? picbuf->videoSurface : NULL;
    }

    memcpy(h265PictureInfo->PicOrderCntVal, h265Data->PicOrderCntVal, (sizeof(NvS32)*16));
    memcpy(h265PictureInfo->IsLongTerm, h265Data->IsLongTerm, 16);
    memcpy(h265PictureInfo->RefPicSetStCurrBefore, h265Data->RefPicSetStCurrBefore, 8);
    memcpy(h265PictureInfo->RefPicSetStCurrAfter, h265Data->RefPicSetStCurrAfter, 8);
    memcpy(h265PictureInfo->RefPicSetLtCurr, h265Data->RefPicSetLtCurr, 8);

    // scaling lists (diag order)
    memcpy(h265PictureInfo->ScalingList4x4, h265Data->ScalingList4x4, (6*16));
    memcpy(h265PictureInfo->ScalingList8x8, h265Data->ScalingList8x8, (6*64));
    memcpy(h265PictureInfo->ScalingList16x16, h265Data->ScalingList16x16, (6*64));
    memcpy(h265PictureInfo->ScalingList32x32, h265Data->ScalingList32x32, (2*64));
    memcpy(h265PictureInfo->ScalingListDCCoeff16x16, h265Data->ScalingListDCCoeff16x16, 6);
    memcpy(h265PictureInfo->ScalingListDCCoeff32x32, h265Data->ScalingListDCCoeff32x32, 2);

    memcpy(h265PictureInfo->NumDeltaPocs, h265Data->NumDeltaPocs, 64*sizeof(NvU32));

    COPYFIELD(h265PictureInfo, h265Data, sps_range_extension_present_flag);
    COPYFIELD(h265PictureInfo, h265Data, pps_range_extension_present_flag);

    if(h265Data->sps_range_extension_present_flag)
    {
        COPYFIELD(h265PictureInfo, h265Data, transformSkipRotationEnableFlag);
        COPYFIELD(h265PictureInfo, h265Data, transformSkipContextEnableFlag);
        COPYFIELD(h265PictureInfo, h265Data, implicitRdpcmEnableFlag);
        COPYFIELD(h265PictureInfo, h265Data, explicitRdpcmEnableFlag);
        COPYFIELD(h265PictureInfo, h265Data, extendedPrecisionProcessingFlag);
        COPYFIELD(h265PictureInfo, h265Data, intraSmoothingDisabledFlag);
        COPYFIELD(h265PictureInfo, h265Data, highPrecisionOffsetsEnableFlag);
        COPYFIELD(h265PictureInfo, h265Data, fastRiceAdaptationEnableFlag);
        COPYFIELD(h265PictureInfo, h265Data, cabacBypassAlignmentEnableFlag);
        COPYFIELD(h265PictureInfo, h265Data, intraBlockCopyEnableFlag);
    }

    if(h265Data->pps_range_extension_present_flag)
    {
        COPYFIELD(h265PictureInfo, h265Data, log2MaxTransformSkipSize);
        COPYFIELD(h265PictureInfo, h265Data, crossComponentPredictionEnableFlag);
        COPYFIELD(h265PictureInfo, h265Data, chromaQpAdjustmentEnableFlag);
        COPYFIELD(h265PictureInfo, h265Data, diffCuChromaQpAdjustmentDepth);
        COPYFIELD(h265PictureInfo, h265Data, chromaQpAdjustmentTableSize);
        COPYFIELD(h265PictureInfo, h265Data, log2SaoOffsetScaleLuma);
        COPYFIELD(h265PictureInfo, h265Data, log2SaoOffsetScaleChroma);

        for (i = 0; i < 6; i++)
        {
           h265PictureInfo->cb_qp_adjustment[i] = h265Data->cb_qp_adjustment[i];
           h265PictureInfo->cr_qp_adjustment[i] = h265Data->cr_qp_adjustment[i];
        }
    }

    // frame type
    h265PictureInfo->frameType = h265Data->SliceType;
}

static void SetParamsMPEG4(VideoDemoTestCtx *ctx, NVDPictureData *pd, NvMediaPictureInfo *pictureInfo)
{
    NVMPEG4PictureData *mpeg4Data = &pd->CodecSpecific.mpeg4;
    NvMediaPictureInfoMPEG4Part2 *mpeg4PictureInfo = (NvMediaPictureInfoMPEG4Part2 *)pictureInfo;

    if (mpeg4Data->pForwardRef)
        mpeg4PictureInfo->forward_reference = ((FrameBuffer *)mpeg4Data->pForwardRef)->videoSurface;
    else
        mpeg4PictureInfo->forward_reference = ((FrameBuffer *)pd->pCurrPic)->videoSurface;

    if (mpeg4Data->pBackwardRef)
        mpeg4PictureInfo->backward_reference = ((FrameBuffer *)mpeg4Data->pBackwardRef)->videoSurface;
    else
        mpeg4PictureInfo->backward_reference = ((FrameBuffer *)pd->pCurrPic)->videoSurface;

    mpeg4PictureInfo->trd[0] = mpeg4Data->trd[0];
    mpeg4PictureInfo->trd[1] = mpeg4Data->trd[1];
    mpeg4PictureInfo->trb[0] = mpeg4Data->trb[0];
    mpeg4PictureInfo->trb[1] = mpeg4Data->trb[1];

    COPYFIELD(mpeg4PictureInfo, mpeg4Data, vop_time_increment_resolution);
    COPYFIELD(mpeg4PictureInfo, mpeg4Data, vop_time_increment_bitcount);
    COPYFIELD(mpeg4PictureInfo, mpeg4Data, vop_coding_type);
    COPYFIELD(mpeg4PictureInfo, mpeg4Data, vop_fcode_forward);
    COPYFIELD(mpeg4PictureInfo, mpeg4Data, vop_fcode_backward);
    COPYFIELD(mpeg4PictureInfo, mpeg4Data, resync_marker_disable);
    COPYFIELD(mpeg4PictureInfo, mpeg4Data, interlaced);
    COPYFIELD(mpeg4PictureInfo, mpeg4Data, quant_type);
    COPYFIELD(mpeg4PictureInfo, mpeg4Data, quarter_sample);
    COPYFIELD(mpeg4PictureInfo, mpeg4Data, short_video_header);
    mpeg4PictureInfo->rounding_control = mpeg4Data->vop_rounding_type;
    COPYFIELD(mpeg4PictureInfo, mpeg4Data, alternate_vertical_scan_flag);
    COPYFIELD(mpeg4PictureInfo, pd, top_field_first);
    memcpy(mpeg4PictureInfo->intra_quantizer_matrix, mpeg4Data->QuantMatrixIntra, 64);
    memcpy(mpeg4PictureInfo->non_intra_quantizer_matrix, mpeg4Data->QuantMatrixInter, 64);
    COPYFIELD(mpeg4PictureInfo, mpeg4Data, data_partitioned);
    COPYFIELD(mpeg4PictureInfo, mpeg4Data, reversible_vlc);
    //nvdec specific, not required for avp+vde
    COPYFIELD(mpeg4PictureInfo, pd, nNumSlices);
    COPYFIELD(mpeg4PictureInfo, pd, pSliceDataOffsets);
}

#ifdef NVMEDIA_QNX
/*
 * Deinitializes the surfaces for the three scenarios i.e. (1) overlay,
 * (2) screen with posting YUV frames directly, and
 * (3) screen with pre-processing of YUV frames to RGB before posting.
 * Chooses NvMediaVideoSurfaceDestroy method or
 * NvxScreenDestroyNvMediaVideoSurfaceSibling method accordingly.
 */
static void DeinitSurfaces(VideoDemoTestCtx *ctx) {
    int j;
    for (j = 0; j < MAX_DEC_BUFFERS; j++) {
        if (ctx->RefFrame[j].videoSurface) {
            NvMediaVideoSurfaceDestroy(ctx->RefFrame[j].videoSurface);
            ctx->RefFrame[j].videoSurface = NULL;
        }
    }

    for (j = 0; j < MAX_DISPLAY_BUFFERS; j++) {
        if (ctx->bScreen) {
            if (ctx->renderSurfaces[j]) {
                    NvxScreenDestroyNvMediaVideoSurfaceSibling(
                                         ctx->renderSurfaces[j]);
            }
        } else {
            if (ctx->renderSurfaces[j]) {
                NvMediaVideoSurfaceDestroy(ctx->renderSurfaces[j]);
            }
        }

        ctx->renderSurfaces[j] = NULL;
    }
}
#endif

NvS32 cbBeginSequence(void *ptr, const NVDSequenceInfo *pnvsi)
{
    VideoDemoTestCtx *ctx = (VideoDemoTestCtx*)ptr;
    unsigned int flags = 0;
    NvBool surface12bit = NV_FALSE;
    char *codecList[] = {
        "MPEG1",
        "MPEG2",
        "MPEG4",
        "VC1",
        "H264",
        "H264_MVC",
        "H264_SVC",
        "",
        "",
        "VP8",
        "H265",
        "VP9"
    };
    char *chroma[] = {
        "Monochrome",
        "4:2:0",
        "4:2:2",
        "4:4:4"
    };
    NvU32 decodeBuffers = pnvsi->nDecodeBuffers;
    NvMediaVideoDecoderAttributes attributes;
    NvMediaVideoOutputDeviceParams videoOutputs[MAX_OUTPUT_DEVICES];
    int outputDevicesNum;

    if (pnvsi->eCodec == NVCS_Unknown) {
        LOG_ERR("BeginSequence: Invalid codec type: %d\n", pnvsi->eCodec);
        return 0;
    }

    if((pnvsi->eCodec == NVCS_H265) || (pnvsi->eCodec == NVCS_VP9)) {
        if((pnvsi->nCodedWidth < 180) || (pnvsi->nCodedHeight < 180)) {
            LOG_ERR("BeginSequence: (Width=%d, Height=%d) < (180, 180) NOT SUPPORTED for %s\n",
                    pnvsi->nCodedWidth, pnvsi->nCodedHeight,
                    ((pnvsi->eCodec == NVCS_H265)?"H265":"VP9"));
                return -1;
            }
    }

    LOG_INFO("BeginSequence: %dx%d (disp: %dx%d) codec: %s decode buffers: %d aspect: %d:%d fps: %f chroma: %s\n",
        pnvsi->nCodedWidth, pnvsi->nCodedHeight, pnvsi->nDisplayWidth, pnvsi->nDisplayHeight,
        codecList[pnvsi->eCodec], pnvsi->nDecodeBuffers, pnvsi->lDARWidth, pnvsi->lDARHeight,
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
        NvMediaStatus st;

        LOG_INFO("BeginSequence: Resolution changed: Old:%dx%d New:%dx%d\n",
            ctx->decodeWidth, ctx->decodeHeight, pnvsi->nCodedWidth, pnvsi->nCodedHeight);

        ctx->decodeWidth = pnvsi->nCodedWidth;
        ctx->decodeHeight = pnvsi->nCodedHeight;

        ctx->displayWidth = pnvsi->nDisplayWidth;
        ctx->displayHeight = pnvsi->nDisplayHeight;

        ctx->videoFullRangeFlag = pnvsi->lVideoFullRangeFlag;
        ctx->colorPrimaries = pnvsi->lColorPrimaries;

        if (ctx->decoder) {
            NvMediaVideoDecoderDestroy(ctx->decoder);
        }

        LOG_INFO("Create decoder: ");
        switch (pnvsi->eCodec) {
            case NVCS_MPEG1:
                codec = NVMEDIA_VIDEO_CODEC_MPEG1;
                LOG_INFO("NVMEDIA_VIDEO_CODEC_MPEG1");
                break;
            case NVCS_MPEG2:
                codec = NVMEDIA_VIDEO_CODEC_MPEG2;
                LOG_INFO("NVMEDIA_VIDEO_CODEC_MPEG2");
                break;
            case NVCS_MPEG4:
                codec = NVMEDIA_VIDEO_CODEC_MPEG4;
                LOG_INFO("NVMEDIA_VIDEO_CODEC_MPEG4");
                break;
            case NVCS_VC1:
                if (ctx->bVC1SimpleMainProfile) {
                    codec = NVMEDIA_VIDEO_CODEC_VC1;
                    LOG_INFO("NVMEDIA_VIDEO_CODEC_VC1");
                } else {
                    codec = NVMEDIA_VIDEO_CODEC_VC1_ADVANCED;
                    LOG_INFO("NVMEDIA_VIDEO_CODEC_VC1_ADVANCED");
                }
                break;
            case NVCS_H264:
                codec = NVMEDIA_VIDEO_CODEC_H264;
                LOG_INFO("NVMEDIA_VIDEO_CODEC_H264");
                break;
            case NVCS_VP8:
                codec = NVMEDIA_VIDEO_CODEC_VP8;
                LOG_INFO("NVMEDIA_VIDEO_CODEC_VP8");
                break;
            case NVCS_H265:
                codec = NVMEDIA_VIDEO_CODEC_HEVC;
                LOG_INFO("NVMEDIA_VIDEO_CODEC_HEVC");
                if (pnvsi->uBitDepthLumaMinus8 || pnvsi->uBitDepthChromaMinus8) {
                    flags |= NVMEDIA_VIDEO_DECODER_10BIT_DECODE;
                    if (pnvsi->uBitDepthLumaMinus8 > 2 || pnvsi->uBitDepthChromaMinus8 > 2) {
                        surface12bit = NV_TRUE;
                    }
                }

                if(pnvsi->lColorPrimaries == NVEColorPrimaries_BT2020)
                    flags |= NVMEDIA_VIDEO_DECODER_PIXEL_REC_2020;
                break;
            case NVCS_VP9:
                codec = NVMEDIA_VIDEO_CODEC_VP9;
                LOG_INFO("NVMEDIA_VIDEO_CODEC_VP9");
                if (pnvsi->uBitDepthLumaMinus8 || pnvsi->uBitDepthChromaMinus8) {
                    flags |= NVMEDIA_VIDEO_DECODER_10BIT_DECODE;
                    if (pnvsi->uBitDepthLumaMinus8 > 2 || pnvsi->uBitDepthChromaMinus8 > 2) {
                        surface12bit = NV_TRUE;
                    }
                }
                break;
            default:
                LOG_ERR("Invalid decoder type\n");
                return 0;
        }

        maxReferences = (decodeBuffers > 0) ? decodeBuffers - 1 : 0;
        maxReferences = (maxReferences > MAX_DEC_REF_BUFFERS) ? MAX_DEC_REF_BUFFERS : maxReferences;

        LOG_DBG(" Size: %dx%d maxReferences: %d\n", ctx->decodeWidth, ctx->decodeHeight,
            maxReferences);
        ctx->decoder = NvMediaVideoDecoderCreateEx(codec, // codec
                                                   ctx->decodeWidth, // width
                                                   ctx->decodeHeight, // height
                                                   maxReferences, // maxReferences
                                                   pnvsi->MaxBitstreamSize, //maxBitstreamSize
                                                   5, // inputBuffering
                                                   flags); // decoder flags
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

#ifdef NVMEDIA_QNX
        DeinitSurfaces(ctx);
#else
        for(i = 0; i < MAX_DEC_BUFFERS; i++) {
            if (ctx->RefFrame[i].videoSurface) {
                NvMediaVideoSurfaceDestroy(ctx->RefFrame[i].videoSurface);
                ctx->RefFrame[i].videoSurface = NULL;
            }
        }
        for(i = 0; i < MAX_DISPLAY_BUFFERS; i++) {
            if (ctx->renderSurfaces[i]) {
                NvMediaVideoSurfaceDestroy(ctx->renderSurfaces[i]);
                ctx->renderSurfaces[i] = NULL;
            }
        }
#endif

        memset(&ctx->RefFrame[0], 0, sizeof(FrameBuffer) * MAX_DEC_BUFFERS);

        switch (pnvsi->nChromaFormat) {
            case 0: // Monochrome
            case 1: // 4:2:0
                if(flags & NVMEDIA_VIDEO_DECODER_10BIT_DECODE) {
                    if(surface12bit) {
                        LOG_INFO("BeginSequence: Chroma format: NvMediaSurfaceType_Video_420_12bit\n");
                        surfType = NvMediaSurfaceType_Video_420_12bit;
                    } else {
                        LOG_INFO("BeginSequence: Chroma format: NvMediaSurfaceType_Video_420_10bit\n");
                        surfType = NvMediaSurfaceType_Video_420_10bit;
                    }
                } else {
                    LOG_INFO("Chroma format: NvMediaSurfaceType_YV12\n");
                    surfType = NvMediaSurfaceType_YV12;
                }

                if(NvMediaVideoOutputCheckFormatSupport(surfType) != NVMEDIA_STATUS_OK) {
                    LOG_INFO("Chroma format: NvMediaSurfaceType_YV12\n");
                    surfType = NvMediaSurfaceType_YV12;
                }
                break;
            case 2: // 4:2:2
                if(flags & NVMEDIA_VIDEO_DECODER_10BIT_DECODE) {
                    if(surface12bit) {
                        LOG_INFO("BeginSequence: Chroma format: NvMediaSurfaceType_Video_422_12bit\n");
                        surfType = NvMediaSurfaceType_Video_422_12bit;
                    } else {
                        LOG_INFO("BeginSequence: Chroma format: NvMediaSurfaceType_Video_422_10bit\n");
                        surfType = NvMediaSurfaceType_Video_422_10bit;
                    }
                } else {
                    LOG_INFO("Chroma format: NvMediaSurfaceType_YV16\n");
                    surfType = NvMediaSurfaceType_YV16;
                }

                if(NvMediaVideoOutputCheckFormatSupport(surfType) != NVMEDIA_STATUS_OK) {
                    LOG_INFO("Chroma format: NvMediaSurfaceType_YV12\n");
                    surfType = NvMediaSurfaceType_YV16;
                }
                break;
            case 3: // 4:4:4
                if(flags & NVMEDIA_VIDEO_DECODER_10BIT_DECODE) {
                    if(surface12bit) {
                        LOG_INFO("BeginSequence: Chroma format: NvMediaSurfaceType_Video_444_12bit\n");
                        surfType = NvMediaSurfaceType_Video_444_12bit;
                    } else {
                        LOG_INFO("BeginSequence: Chroma format: NvMediaSurfaceType_Video_444_10bit\n");
                        surfType = NvMediaSurfaceType_Video_444_10bit;
                    }
                } else {
                    LOG_INFO("Chroma format: NvMediaSurfaceType_YV24\n");
                    surfType = NvMediaSurfaceType_YV24;
                }

                if(NvMediaVideoOutputCheckFormatSupport(surfType) != NVMEDIA_STATUS_OK) {
                    LOG_INFO("Chroma format: NvMediaSurfaceType_YV12\n");
                    surfType = NvMediaSurfaceType_YV24;
                }
                break;
            default:
                LOG_INFO("Invalid chroma format: %d\n", pnvsi->nChromaFormat);
                return 0;
        }

        ctx->nBuffers = decodeBuffers + MAX_DEC_DEINTER_BUFFERS;

        /* Creates surfaces for decode
         */
        for (i = 0; i < ctx->nBuffers; i++) {
            ctx->RefFrame[i].videoSurface =
                   NvMediaVideoSurfaceCreate(ctx->device, // device
                                             surfType,    // type
                                             (pnvsi->nCodedWidth + 15) & ~15,
                                             (pnvsi->nCodedHeight + 15) & ~15);
            if (!ctx->RefFrame[i].videoSurface) {
                LOG_ERR("Unable to create video surface\n");
                return 0;
            }
            LOG_DBG("Create video surface[%d]: %dx%d\n Ptr:%p Surface:%p"
                    " Device:%p\n", i, (pnvsi->nCodedWidth + 15) & ~15,
                    (pnvsi->nCodedHeight + 15) & ~15, &ctx->RefFrame[i],
                    ctx->RefFrame[i].videoSurface, ctx->device);

            ctx->RefFrame[i].videoSurface->tag = &ctx->RefFrame[i];
        }

#ifndef NVMEDIA_QNX
        if (ctx->displayEnabled) {
            DisplayDestroy(ctx);
            if (DisplayInit(ctx, ctx->displayWidth, ctx->displayHeight,
                            pnvsi->nCodedWidth, pnvsi->nCodedHeight)) {
                LOG_ERR("cbBeginSequence : Unable to create DisplayInit\n");
                return 0;
            }

            LOG_DBG("cbBeginSequence: DisplayInit done");
        }
#endif
        if (ctx->displayEnabled) {
            /* Query the monitor resolution and set it to renderSurfaces */
            st = GetAvailableDisplayDevices(&outputDevicesNum, &videoOutputs[0]);
            if (st != NVMEDIA_STATUS_OK) {
                LOG_DBG("cbBeginSequence: Failed retrieving available video output devices\n");
                return NV_FALSE;
            }

            for (i = 0; i < outputDevicesNum; i++) {
                if (videoOutputs[i].displayId == ctx->displayId) {
                    LOG_DBG("cbBeginSequence: monitor resolution is %dx%d \n",
                                             videoOutputs[i].width,
                                             videoOutputs[i].height);
                    ctx->monitorWidth = videoOutputs[i].width;
                    ctx->monitorHeight = videoOutputs[i].height;
                    break;
                } else {
                    continue;
                }
            }
        } else {
                ctx->monitorWidth = ctx->displayWidth;
                ctx->monitorHeight = ctx->displayHeight;
        }

        if (!ctx->monitorWidth || !ctx->monitorHeight) {
            LOG_ERR("cbBeginSequence: bad monitor resolution \n");
            return NV_FALSE;
        }

#ifndef NVMEDIA_QNX
        /* Handling overlay cases in case of builds other than NVMEDIA_QNX */
        for (i = 0; i < MAX_DISPLAY_BUFFERS; i++) {
            ctx->renderSurfaces[i] = NvMediaVideoSurfaceCreateEx(ctx->device,
                                                                 NvMediaSurfaceType_YV12,
                                                                 ctx->monitorWidth,
                                                                 ctx->monitorHeight,
                                                                 NVMEDIA_SURFACE_CREATE_ATTRIBUTE_DISPLAY);
            if (!ctx->renderSurfaces[i]) {
                LOG_DBG("Unable to create render surface\n");
                return NV_FALSE;
            }
            ctx->freeRenderSurfaces[i] = ctx->renderSurfaces[i];
        }
#endif

#ifdef NVMEDIA_QNX
        if (ctx->bScreen) {
            /* Initializing screen specific parameters */
            if (InitScreen(ctx->displayId, ctx->monitorWidth,
                           ctx->monitorHeight, MAX_DISPLAY_BUFFERS, &ctx->screenArgs,
                           ctx->bYuv2Rgb)) {
                LOG_ERR("InitScreen failed\n");
                return 0;
            }

            /* Create sibling surfaces for render */
            for(i = 0; i < MAX_DISPLAY_BUFFERS; i++) {

                if (!ctx->screenArgs.pScreenBuffers[i]) {
                    LOG_ERR("%s: %s", __func__, "No screen buffer to attach");
                    return 0;
                }

                st = NvxScreenCreateNvMediaVideoSurfaceSibling(
                                            ctx->device,
                                            ctx->screenArgs.pScreenBuffers[i],
                                            &ctx->renderSurfaces[i]);
                if ((!ctx->renderSurfaces[i]) ||
                    (st != NVMEDIA_STATUS_OK)) {
                    LOG_ERR("%s: %s", __func__, "Unable to create sibling for"
                            " screen buffer");
                    return 0;
                }

                ctx->freeRenderSurfaces[i] = ctx->renderSurfaces[i];

                LOG_DBG("Create sibling video surface[%d]: Surface:%p"
                        " Device:%p\n", i,
                        ctx->renderSurfaces[i], ctx->device);
            }

            /* Mixer needed for render surface, including convert YUV frames
             * to RGB and deinterlacing */
            if (MixerInit(ctx, ctx->displayWidth, ctx->displayHeight,
                          pnvsi->nCodedWidth, pnvsi->nCodedHeight)) {
                LOG_ERR("cbBeginSequence: Mixer Init error \n");
                return 0;
            }

            LOG_DBG("cbBeginSequence: Mixer init done: Mixer:%p\n", ctx->mixer);

        } else {
            /* Overlay mode specific initializations */
            for (i = 0; i < MAX_DISPLAY_BUFFERS; i++) {
                ctx->renderSurfaces[i] = NvMediaVideoSurfaceCreateEx(ctx->device,
                                                                     NvMediaSurfaceType_YV12,
                                                                     ctx->displayWidth,
                                                                     ctx->displayHeight,
                                                                     NVMEDIA_SURFACE_CREATE_ATTRIBUTE_DISPLAY);
                if (!ctx->renderSurfaces[i]) {
                    LOG_DBG("Unable to create render surface\n");
                    return NV_FALSE;
                }
                ctx->freeRenderSurfaces[i] = ctx->renderSurfaces[i];
            }

            if (ctx->displayEnabled) {
                DisplayDestroy(ctx);
                if (DisplayInit(ctx, ctx->displayWidth, ctx->displayHeight,
                                pnvsi->nCodedWidth, pnvsi->nCodedHeight)) {
                    LOG_ERR("cbBeginSequence : Unable to create DisplayInit\n");
                    return 0;
                }

                LOG_DBG("cbBeginSequence: DisplayInit done");
            }
        }

#endif
    } else {
        LOG_INFO("cbBeginSequence: No resolution change\n");
    }

    return decodeBuffers;
}

NvBool cbDecodePicture(void *ptr, NVDPictureData *pd)
{
    VideoDemoTestCtx *ctx = (VideoDemoTestCtx*)ptr;
    NvMediaStatus status;
    FrameBuffer *targetBuffer = NULL;
    NvU64 timeEnd, timeStart = 0;
    NvMediaBitstreamBuffer bitStreamBuffer[1];

    union {
        NvMediaPictureInfoH264 picInfoH264;
        NvMediaPictureInfoVC1 picInfoVC1;
        NvMediaPictureInfoMPEG1Or2 picInfoMPEG2;
        NvMediaPictureInfoMPEG4Part2 picInfoMPEG4;
        NvMediaPictureInfoVP8 picInfoVP8;
        NvMediaPictureInfoH265 picInfoH265;
        NvMediaPictureInfoVP9 picInfoVP9;
    } nvMediaPictureInfo;

    if (pd->pCurrPic) {
        GetTimeMicroSec(&timeStart);
        switch (ctx->eCodec) {
            case NVCS_MPEG1:
            case NVCS_MPEG2:
                SetParamsMPEG2(ctx, pd, &nvMediaPictureInfo.picInfoMPEG2);
                break;
            case NVCS_MPEG4:
                SetParamsMPEG4(ctx, pd, &nvMediaPictureInfo.picInfoMPEG4);
                break;
            case NVCS_VC1:
                SetParamsVC1(ctx, pd, &nvMediaPictureInfo.picInfoVC1);
                break;
            case NVCS_H264:
                SetParamsH264(ctx, pd, &nvMediaPictureInfo.picInfoH264);
                break;

            case NVCS_VP8:
                SetParamsVP8(ctx, pd, &nvMediaPictureInfo.picInfoVP8);
                break;
            case NVCS_H265:
                SetParamsH265(ctx, pd, &nvMediaPictureInfo.picInfoH265);
                break;
            case NVCS_VP9:
                SetParamsVP9(ctx, pd, &nvMediaPictureInfo.picInfoVP9);
                break;
            default:
                LOG_ERR("cbDecodePicture: Invalid decoder type\n");
                return NV_FALSE;
        }

        targetBuffer = (FrameBuffer *)pd->pCurrPic;
        targetBuffer->frameNum = ctx->nPicNum;
        targetBuffer->topFieldFirstFlag = !!pd->top_field_first;        // Frame pictures only
        targetBuffer->progressiveFrameFlag = !!pd->progressive_frame;   // Frame is progressive
        bitStreamBuffer[0].bitstream = (NvU8 *)pd->pBitstreamData;
        bitStreamBuffer[0].bitstreamBytes = pd->nBitstreamDataLen;

        LOG_DBG("cbDecodePicture: %d Ptr: %p Surface: %p (stream ptr: %p size: %d)\n",
            ctx->nPicNum, targetBuffer, targetBuffer->videoSurface, pd->pBitstreamData, pd->nBitstreamDataLen);
        ctx->nPicNum++;

        if (targetBuffer->videoSurface) {
            status = NvMediaVideoDecoderRender(ctx->decoder,                              // decoder
                                               targetBuffer->videoSurface,                // target
                                               (NvMediaPictureInfo *)&nvMediaPictureInfo, // pictureInfo
                                               1,                                         // numBitstreamBuffers
                                               &bitStreamBuffer[0]);
            if (status != NVMEDIA_STATUS_OK) {
                LOG_ERR("cbDecodePicture: Decode failed: %d\n", status);
                return NV_FALSE;
            }
            LOG_DBG("cbDecodePicture: Frame decode done\n");

            if (ctx->showDecodeTimimg) {
                // Wait for decode completion
                NvMediaVideoSurfaceMap surfaceMap;

                status = NvMediaVideoSurfaceLock(targetBuffer->videoSurface, &surfaceMap);
                if (status != NVMEDIA_STATUS_OK) {
                    LOG_ERR("cbDecodePicture: Video surface lock failed : %d\n", status);
                    return NV_FALSE;
                }

                NvMediaVideoSurfaceUnlock(targetBuffer->videoSurface);
            }
        } else {
            LOG_ERR("cbDecodePicture: Invalid target surface\n");
        }

        GetTimeMicroSec(&timeEnd);
        ctx->totalDecodeTime += (timeEnd - timeStart) / 1000.0;
        if (ctx->showDecodeTimimg) {
            LOG_DBG("cbDecodePicture: %03d %lld us\n", ctx->decodeCount, timeEnd - timeStart);
        }

        if (ctx->OutputYUVFilename) {
            if((!pd->field_pic_flag) || (pd->field_pic_flag && pd->second_field)) {
                LOG_DBG("cbDecodePicture: Saving YUV file %d ...\n", ctx->decodeCount);
                status = WriteFrame(ctx->OutputYUVFilename, targetBuffer->videoSurface, NVMEDIA_TRUE, NVMEDIA_TRUE);
                if (status != NVMEDIA_STATUS_OK) {
                    LOG_ERR("cbDecodePicture: Write frame to file failed: %d\n", status);
                }
                LOG_DBG("cbDecodePicture: Saving YUV file Done\n");
            }
        }

        ctx->decodeCount++;
        if (ctx->numFramesToDecode && ctx->numFramesToDecode == ctx->decodeCount) {
            LOG_DBG("cbDecodePicture: Requested number of frames read (%d). Setting stop decoding flag to TRUE\n", ctx->numFramesToDecode);
            ctx->stopDecoding = NV_TRUE;
        }

        ctx->sumCompressedLen += pd->nBitstreamDataLen;
    } else {
        LOG_ERR("cbDecodePicture: No valid frame\n");
        return NV_FALSE;
    }

    return NV_TRUE;
}

NvBool cbDisplayPicture(void *ptr, NVDPicBuff *p, NvS64 llPTS)
{
    VideoDemoTestCtx *ctx = (VideoDemoTestCtx*)ptr;
    FrameBuffer* buffer = (FrameBuffer*)p;

    if(buffer) {
        LOG_DBG("Display: %d ctx:%p frame_buffer:%p Surface:%p\n", buffer->frameNum, ctx, buffer, buffer->videoSurface);
#ifdef NVMEDIA_QNX
        if (ctx->bScreen) {
            DisplayFrameScreen(ctx, buffer);
        } else {
#endif
            /* Display using overlay */
            if (ctx->displayEnabled) {
                DisplayFrame(ctx, buffer);
            }
#ifdef NVMEDIA_QNX
        }
#endif
    } else {
        LOG_ERR("Display: Invalid buffer\n");
        return NV_FALSE;
    }

    return NV_TRUE;
}

void cbUnhandledNALU(void *ptr, const NvU8 *buf, NvS32 size)
{
    // Empty
}

#ifdef NVMEDIA_QNX
/*
 * Searches for a particular screen buffer needle, in the
 * available screen buffer pool curFreeScreenBufs.
 * Returns false if needle not found else returns true.
 */
static
NvBool isScreenBufFree(screen_buffer_t * curFreeScreenBufs,
                       screen_buffer_t needle, int nBuffers) {
    int i;

    for (i=0; i<nBuffers; i++) {
        if (curFreeScreenBufs[i] == needle) {
            return NV_TRUE;
        }
    }
    return NV_FALSE;
}
#endif


NvBool cbAllocPictureBuffer(void *ptr, NVDPicBuff **p)
{
    int i;
    VideoDemoTestCtx *ctx = (VideoDemoTestCtx*)ptr;

    *p = (NVDPicBuff *) NULL;

    for (i = 0; i < ctx->nBuffers; i++) {
        if (!ctx->RefFrame[i].refCount) {
            *p = (NVDPicBuff *) &ctx->RefFrame[i];
            ctx->RefFrame[i].refCount++;
            ctx->RefFrame[i].index = i;
            LOG_DBG("Allocated buffer for picture index: %d Ptr:%p Surface:%p\n", i, *p, ctx->RefFrame[i].videoSurface);
            return NV_TRUE;
        }
    }

    LOG_ERR("Alloc picture failed\n");
    return NV_FALSE;
}

void cbRelease(void *ptr, NVDPicBuff *p)
{
    FrameBuffer* buffer = (FrameBuffer*)p;

    LOG_DBG("Releasing picture: %d index: %d\n", buffer->frameNum, buffer->index);
    if (buffer->refCount > 0)
        buffer->refCount--;
}

void cbAddRef(void *ptr, NVDPicBuff *p)
{
    FrameBuffer* buffer = (FrameBuffer*)p;

    LOG_DBG("Adding reference to picture: %d\n", buffer->frameNum);
    buffer->refCount++;
}

NvBool cbGetBackwardUpdates(void *ptr, NVDPictureData *pd)
{
    NvMediaStatus status;
    VideoDemoTestCtx *ctx = (VideoDemoTestCtx*)ptr;
    NvMediaVP9BackwardUpdates backupdates;
    memset(&backupdates, 0, sizeof(backupdates));

    status = NvMediaVideoBackwardUpdates(ctx->decoder, &backupdates);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("cbGetBackwardUpdates : Failed to get Video backward updates : %d\n", status);
        return NV_FALSE;
    }

    memcpy(&pd->CodecSpecific.vp9.buCounts, &backupdates, sizeof(NvMediaVP9BackwardUpdates));
    return NV_TRUE;
}

NVDClientCb TestClientCb =
{
    &cbBeginSequence,
    &cbDecodePicture,
    &cbDisplayPicture,
    &cbUnhandledNALU,
    &cbAllocPictureBuffer,
    &cbRelease,
    &cbAddRef,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    &cbGetBackwardUpdates
};

int Init(VideoDemoTestCtx *ctx, TestArgs *testArgs)
{
    struct stat st;
    NvBool enableVC1APInterlaced = NV_TRUE;
    float defaultFrameRate = 30.0;
    EDeinterlaceMode eDeinterlacingMode;

    ctx->aspectRatio            = testArgs->aspectRatio;
    ctx->deinterlace            = testArgs->deinterlace;
    ctx->deinterlaceAlgo        = testArgs->deinterlaceAlgo;
    ctx->inverceTelecine        = testArgs->inverceTelecine;
    ctx->frameTimeUSec          = testArgs->frameTimeUSec;
    ctx->loop                   = testArgs->loop;
    ctx->numFramesToDecode      = testArgs->numFramesToDecode;
    ctx->eCodec                 = testArgs->eCodec;
    ctx->OutputYUVFilename      = testArgs->OutputYUVFilename;
    ctx->filename               = testArgs->filename;
    ctx->windowId               = testArgs->windowId;
    ctx->displayEnabled         = testArgs->displayEnabled;
    ctx->displayDeviceEnabled   = testArgs->displayDeviceEnabled;
    ctx->displayId              = testArgs->displayId;
    ctx->depth                  = testArgs->depth;
    ctx->position               = testArgs->position;
    ctx->positionSpecifiedFlag  = testArgs->positionSpecifiedFlag;
    ctx->filterQuality          = testArgs->filterQuality;
#ifdef NVMEDIA_QNX
    ctx->bScreen                = testArgs->bScreen;
    ctx->bYuv2Rgb               = testArgs->bYuv2Rgb;
#endif
    ctx->showDecodeTimimg       = testArgs->showDecodeTimimg;

    switch(ctx->deinterlace) {
        case 0: /* Deinterlace Off/Weave */
            eDeinterlacingMode = DEINTERLACE_MODE_WEAVE;
            break;
        case 1: /* Deinterlace BOB */
            eDeinterlacingMode = DEINTERLACE_MODE_BOB;
            break;
        case 2: /* Deinterlace Advanced, Frame Rate */
            eDeinterlacingMode = DEINTERLACE_MODE_ADVANCED_FRAMERATE;
            break;
        case 3: /* Deinterlace Advanced, Field Rate */
            eDeinterlacingMode = DEINTERLACE_MODE_ADVANCED_FIELDRATE;
            break;
        default:
            LOG_ERR("Init: Invalid deinterlace mode\n");
            return -1;
    }

    DeinterlaceInit(&ctx->deinterlaceCtx, eDeinterlacingMode);

    LOG_DBG("Init: Opening file %s\n", testArgs->filename);
    ctx->file = fopen(ctx->filename, "rb");
    if (!ctx->file) {
        LOG_ERR("Init: Failed to open stream %s\n", testArgs->filename);
        return -1;
    }

    memset(&ctx->nvsi, 0, sizeof(ctx->nvsi));
    ctx->lDispCounter = 0;

    if(stat(ctx->filename, &st) == -1) {
        fclose(ctx->file);
        LOG_ERR("Init: cannot determine size of stream %s\n", ctx->filename);
        return -1;
    }
    ctx->fileSize = st.st_size;

    ctx->bRCVfile = Strcasestr(ctx->filename, ".rcv")  != NULL;

    // create video parser
    memset(&ctx->nvdp, 0, sizeof(NVDParserParams));
    ctx->nvdp.pClient = &TestClientCb;
    ctx->nvdp.pClientCtx = ctx;
    ctx->nvdp.lErrorThreshold = 50;
    ctx->nvdp.lReferenceClockRate = 0;
    ctx->nvdp.eCodec = ctx->eCodec;

    LOG_DBG("Init: Creating parser\n");
    ctx->parser = video_parser_create(&ctx->nvdp);
    if (!ctx->parser) {
        LOG_ERR("Init: video_parser_create failed\n");
        return -1;
    }
    video_parser_set_attribute(ctx->parser, NVDVideoParserAttribute_EnableVC1APInterlaced, sizeof(NvBool), &enableVC1APInterlaced);
    video_parser_set_attribute(ctx->parser, NVDVideoParserAttribute_SetDefaultFramerate, sizeof(float), &defaultFrameRate);

    LOG_DBG("Init: Creating NvMediaDevice\n");
    ctx->device = NvMediaDeviceCreate();
    if (!ctx->device) {
        LOG_ERR("Init: NvMediaDeviceCreate Failed\n");
        return -1;
    }

    if(ctx->OutputYUVFilename) {
        FILE *file = fopen(ctx->OutputYUVFilename, "w");
        if(!file) {
            LOG_ERR("Init: unable to open output file %s\n", ctx->OutputYUVFilename);
            return -1;
        }
        fclose(file);
    }

    return 0;
}

void Deinit(VideoDemoTestCtx *ctx)
{
#ifndef NVMEDIA_QNX
    NvU32 i;
#endif

    video_parser_destroy(ctx->parser);
    DisplayFlush(ctx);

    if (ctx->file)
        fclose(ctx->file);

    if (ctx->deinterlaceCtx)
        DeinterlaceFini(ctx->deinterlaceCtx);

    if (ctx->decoder)
        NvMediaVideoDecoderDestroy(ctx->decoder);

    DisplayDestroy(ctx);

#ifndef NVMEDIA_QNX
    /* Destroying surfaces here only for the non-QNX builds */
    for(i = 0; i < MAX_DEC_BUFFERS; i++) {
        if (ctx->RefFrame[i].videoSurface) {
            NvMediaVideoSurfaceDestroy(ctx->RefFrame[i].videoSurface);
            ctx->RefFrame[i].videoSurface = NULL;
        }
    }
    for(i = 0; i < MAX_DISPLAY_BUFFERS; i++) {
        if (ctx->renderSurfaces[i]) {
            NvMediaVideoSurfaceDestroy(ctx->renderSurfaces[i]);
            ctx->renderSurfaces[i] = NULL;
        }
    }
#endif

    if (ctx->device) {
        NvMediaDeviceDestroy(ctx->device);
        ctx->device = NULL;
    }
}

int StreamVC1SimpleProfile(VideoDemoTestCtx *ctx)
{
    NvU8 *bits;
    NvU8 header[256 + 32];
    NvU32 readSize = 0;
    NvS32 frameCount = 0;
    RCVFileHeader RCVHeader;
    NVDSequenceInfo nvsi, *pnvsi;
    NVDParserParams *pnvdp = &ctx->nvdp;
    float defaultFrameRate = 30.0;
    NvU32 len;

    rewind(ctx->file);
    fread(header, 5, 1, ctx->file);
    if (header[3] == 0xC5)
        readSize = 32;
    readSize += header[4];
    fread(header + 5, readSize - 5, 1, ctx->file);

    ctx->bVC1SimpleMainProfile = NV_TRUE;     //setting it for Simple/Main profile
    LOG_DBG("VC1 Simple/Main profile clip\n");
    len = ParseRCVHeader(&RCVHeader, header, readSize);
    LOG_DBG("ParseRCVHeader : len = %d \n", len);
    // Close previous instance
    video_parser_destroy(ctx->parser);
    pnvsi = &nvsi;
    pnvsi->eCodec = NVCS_VC1;
    pnvsi->bProgSeq = NV_TRUE;
    pnvsi->nDisplayWidth = RCVHeader.lMaxCodedWidth;
    pnvsi->nDisplayHeight = RCVHeader.lMaxCodedHeight;
    pnvsi->nCodedWidth = (pnvsi->nDisplayWidth + 15) & ~15;
    pnvsi->nCodedHeight = (pnvsi->nDisplayHeight + 15) & ~15;
    pnvsi->nChromaFormat = 1;
    pnvsi->lBitrate = RCVHeader.lBitRate;
    pnvsi->fFrameRate = (RCVHeader.lFrameRate && RCVHeader.lFrameRate != -1) ? (float)RCVHeader.lFrameRate : 0;
    pnvsi->lDARWidth = pnvsi->nDisplayWidth;
    pnvsi->lDARHeight = pnvsi->nDisplayHeight;
    pnvsi->lVideoFormat = NVEVideoFormat_Unspecified;
    pnvsi->lColorPrimaries = NVEColorPrimaries_Unspecified;
    pnvsi->lTransferCharacteristics = NVETransferCharacteristics_Unspecified;
    pnvsi->lMatrixCoefficients = NVEMatrixCoefficients_Unspecified;
    pnvsi->cbSequenceHeader = RCVHeader.cbSeqHdr;
    if (pnvsi->cbSequenceHeader > 0)
        memcpy(pnvsi->SequenceHeaderData, RCVHeader.SeqHdrData, pnvsi->cbSequenceHeader);
    pnvdp->pExternalSeqInfo = pnvsi;
    ctx->parser = video_parser_create(pnvdp);
    if (!ctx->parser) {
        LOG_ERR("video_parser_create failed\n");
    }
    video_parser_set_attribute(ctx->parser, NVDVideoParserAttribute_SetDefaultFramerate, sizeof(float), &defaultFrameRate);

    bits = malloc(RCV_MAX_FRAME_SIZE);
    if (!bits)
        return -1;

    while (!feof(ctx->file) && !ctx->stopDecoding && !signal_stop) {
        size_t len;
        bitstream_packet_s packet;
        NvU32 timeStamp;

        memset(&packet, 0, sizeof(bitstream_packet_s));

        // Read frame length
        len = fread(&readSize, 4, 1, ctx->file);
        // Check end of file
        if (!len)
            break;
        readSize &= 0x00FFFFFF;
        // Read time stamp
        len = fread(&timeStamp, 4, 1, ctx->file);
        if (!len)
            break;
        if (readSize) {
            //  Read frame data
            len = fread(bits, 1, readSize, ctx->file);
        } else {
            // Skipped P-Frame
            bits[0] = 0;
            len = 1;
        }

        LOG_DBG("Frame: %d readSize: %d actual read length: %d timeStamp: %d\n", frameCount, readSize, len, timeStamp);

        packet.nDataLength = (NvS32) len;
        packet.pByteStream = bits;

        packet.bEOS = feof(ctx->file);
        if (!video_parser_parse(ctx->parser, &packet))
            return -1;
        frameCount++;
    }

    if (frameCount != RCVHeader.lNumFrames) {
        LOG_ERR("Actual (%d) and RCV header (%d) frame count does not match\n",
            frameCount, RCVHeader.lNumFrames);
    }

    video_parser_flush(ctx->parser);
    DisplayFlush(ctx);

    free(bits);

    return 0;
}

static int StreamVP8(VideoDemoTestCtx *ctx)
{
    int i;
    NvU8 *bits;
    NvU32 FrameSize;
    NvU32 numFrames;
    NvU32 frameRateNum;
    NvU32 frameRateDen;
    float frameRate;
    NvU32 frameCount;
    NvBool Vp8IvfFileHdrRead;
    NvU32 readSize = READ_SIZE;

    bits = malloc(readSize);
    if (!bits) {
        LOG_ERR("StreamVP8: Failed allocating memory for file buffer\n");
        return -1;
    }

    for(i = 0; (i < ctx->loop) || (ctx->loop == -1); i++) {
        Vp8IvfFileHdrRead = NV_FALSE;
        frameCount = 0;
        numFrames = 0;
        ctx->lDispCounter = 0;
        while(!feof(ctx->file) && !ctx->stopDecoding && !signal_stop) {
            size_t len;
            bitstream_packet_s packet;
            memset(&packet, 0, sizeof(bitstream_packet_s));

            if(Vp8IvfFileHdrRead == NV_FALSE) {
                if(fread(bits, 1, IVF_FILE_HDR_SIZE, ctx-> file) != IVF_FILE_HDR_SIZE) {
                    LOG_ERR("StreamVP8: Failed to read IVF file header\n");
                    free(bits);
                    return -1;
                }
                if(!((bits[0] == 'D') && (bits[1] == 'K') && (bits[2] == 'I') && (bits[3] == 'F'))) {
                    LOG_ERR("StreamVP8: It is not a valid IVF file \n");
                    free(bits);
                    return -1;
                }
                Vp8IvfFileHdrRead = NV_TRUE;
                LOG_DBG("StreamVP8: It is a valid IVF file \n");

                frameRateNum = u32(bits + 16);
                frameRateDen = u32(bits + 20);
                if(frameRateDen)
                    frameRate = (frameRateNum * 1.0)/ frameRateDen;
                else {
                    LOG_INFO("StreamVP8: Value of time scale in IVF heder is zero. Using default frame rate\n");
                    frameRate = 0;
                }
                if(frameRate)
                    video_parser_set_attribute(ctx->parser, NVDVideoParserAttribute_SetFramerate, sizeof(float), &frameRate);

                numFrames = u32(bits + 24);
                if(!numFrames) {
                    LOG_ERR("StreamVP8: IVF file has no frames\n");
                    free(bits);
                    return -1;
                }

                LOG_DBG("StreamVP8:Frame Rate: %f \t Frame Count: %d \n",frameRate,numFrames);
            }

            if(fread(bits, 1, IVF_FRAME_HDR_SIZE, ctx->file) == IVF_FRAME_HDR_SIZE) {
                FrameSize = (bits[3]<<24) + (bits[2]<<16) + (bits[1]<<8) + bits[0];
                if(FrameSize > readSize) {
                    bits = realloc(bits, FrameSize);
                    readSize = FrameSize;
                }
                len = fread(bits, 1, FrameSize, ctx->file);
                packet.nDataLength = (NvS32) len;
                packet.pByteStream = bits;
                frameCount++;
            } else {
                FrameSize = 0;
                packet.nDataLength = 0;
                packet.pByteStream = NULL;
            }

            packet.bEOS = feof(ctx->file);
            packet.bPTSValid = 0; // (pts != (NvU32)-1);
            packet.llPTS = 0; // packet.bPTSValid ? (1000 * pts / 9)  : 0;    // 100 ns scale
            if (!video_parser_parse(ctx->parser, &packet))
                return -1;
        }

        if(feof(ctx->file)) {
            if(frameCount != numFrames) {
                LOG_ERR("StreamVP8: Actual (%d) and IVF header (%d) frame count does not match\n",
                    frameCount, numFrames);
                free(bits);
                return -1;
            }
        }
        if(ctx->stopDecoding) {
            if((int)frameCount != ctx->numFramesToDecode) {
                LOG_ERR("StreamVP8: Actual frame count (%d) and frames to be decoded count(%d) does not match\n",
                    frameCount, ctx->numFramesToDecode);
                free(bits);
                return -1;
            }
        }
        video_parser_flush(ctx->parser);
        DisplayFlush(ctx);
        rewind(ctx->file);

        if(ctx->loop != 1 && !signal_stop) {
            if(ctx->stopDecoding) {
                ctx->stopDecoding = NV_FALSE;
                ctx->decodeCount = 0;
                ctx->totalDecodeTime = 0;
            }
            LOG_MSG("loop count: %d/%d \n", i+1, ctx->loop);
        } else
            break;
    }
    free(bits);

    return 0;
}

static int Decode_orig(VideoDemoTestCtx *ctx)
{
    NvU8 *bits;
    int i;
    NvU32 readSize = READ_SIZE;

    bits = malloc(readSize);
    if (!bits) {
        LOG_ERR("Decode_orig: Failed allocating memory for file buffer\n");
        return -1;
    }

    LOG_DBG("Decode_orig: Starting %d loops of decode\n", ctx->loop);

    for(i = 0; (i < ctx->loop) || (ctx->loop == -1); i++) {
        LOG_DBG("Decode_orig: loop %d out of %d\n", i, ctx->loop);
        if(ctx->bRCVfile) {
            NvU8 header[32] = { 0, };
            if(fread(header, 32, 1, ctx->file)) {
                int i;
                NvMediaBool startCode = NVMEDIA_FALSE;
                // Check start code
                for(i = 0; i <= (32 - 4); i++) {
                    if(!header[i + 0] && !header[i + 1] && header[i + 2] == 0x01 &&
                        (header[i + 3] == 0x0D || header[i + 3] == 0x0F)) {
                        startCode = NVMEDIA_TRUE;
                        break;
                    }
                }
                if(!startCode) {
                    StreamVC1SimpleProfile(ctx);
                }
            }
        } else {
            while (!feof(ctx->file) && !ctx->stopDecoding && !signal_stop) {
                size_t len;
                bitstream_packet_s packet;
                memset(&packet, 0, sizeof(bitstream_packet_s));
                len = fread(bits, 1, readSize, ctx->file);
                packet.nDataLength = (NvS32) len;
                packet.pByteStream = bits;
                packet.bEOS = feof(ctx->file);
                packet.bPTSValid = 0; // (pts != (NvU32)-1);
                packet.llPTS = 0; // packet.bPTSValid ? (1000 * pts / 9)  : 0;    // 100 ns scale
                if (!video_parser_parse(ctx->parser, &packet)) {
                    LOG_ERR("Decode_orig: video_parser_parse returned with failure\n");
                    return -1;
                }
            }
            video_parser_flush(ctx->parser);
            DisplayFlush(ctx);
        }

        LOG_DBG("Decode_orig: Finished decoding. Flushing parser and display\n");
        rewind(ctx->file);

        if(ctx->loop != 1 && !signal_stop) {
            if(ctx->stopDecoding) {
                ctx->stopDecoding = NV_FALSE;
                ctx->decodeCount = 0;
                ctx->totalDecodeTime = 0;
            }
            LOG_MSG("loop count: %d/%d \n", i+1, ctx->loop);
        } else
            break;

    }

    free(bits);

    return 0;
}

int Decode(VideoDemoTestCtx *ctx)
{
    if(ctx->eCodec == NVCS_VP8 || ctx->eCodec == NVCS_VP9)
        return StreamVP8(ctx);
    else
        return Decode_orig(ctx);
}

static int MixerInit(VideoDemoTestCtx *ctx, int width, int height, int videoWidth, int videoHeight)
{
    unsigned int features =  0;
    float aspectRatio = (float)width / (float)height;
    NvMediaVideoMixerAttributes mixerAttributes;
    unsigned int attributeMask = 0;

    if (ctx->aspectRatio != 0.0) {
        aspectRatio = ctx->aspectRatio;
    }

    LOG_DBG("MixerInit: %dx%d Aspect: %f\n", width, height, aspectRatio);
    memset(&mixerAttributes, 0, sizeof(mixerAttributes));
    switch(ctx->deinterlace) {
        case 0: /* Deinterlace Off/Weave */
            break;
        case 1: /* Deinterlace BOB */
            features |= NVMEDIA_VIDEO_MIXER_FEATURE_PRIMARY_VIDEO_DEINTERLACING;
            break;
        case 2: /* Deinterlace Advanced, Frame Rate */
        case 3: /* Deinterlace Advanced, Field Rate */
            switch(ctx->deinterlaceAlgo) {
                case 1:
                default:
                    features |= NVMEDIA_VIDEO_MIXER_FEATURE_ADVANCED1_PRIMARY_DEINTERLACING;
                    mixerAttributes.primaryDeinterlaceType = NVMEDIA_DEINTERLACE_TYPE_ADVANCED1;
                    break;
                case 2:
                    features |= NVMEDIA_VIDEO_MIXER_FEATURE_ADVANCED2_PRIMARY_DEINTERLACING;
                    mixerAttributes.primaryDeinterlaceType = NVMEDIA_DEINTERLACE_TYPE_ADVANCED2;
                    break;
            }
            break;
        default:
            LOG_ERR("MixerInit: Invalid deinterlace mode\n");
            return NVMEDIA_STATUS_NOT_SUPPORTED;
    }

    if(ctx->inverceTelecine) {
        features |= NVMEDIA_VIDEO_MIXER_FEATURE_INVERSE_TELECINE;
        mixerAttributes.inverseTelecine = NVMEDIA_TRUE;
    }

    LOG_DBG("MixerInit: Creating Mixer\n");

#ifdef NVMEDIA_QNX
    if (ctx->bYuv2Rgb) {
        features |= NVMEDIA_VIDEO_MIXER_FEATURE_SURFACE_RENDERING;
    } else {
        features |= NVMEDIA_VIDEO_MIXER_FEATURE_SURFACE_RENDERING_YUV;
    }
#else
    features |= NVMEDIA_VIDEO_MIXER_FEATURE_SURFACE_RENDERING_YUV;
#endif

    ctx->mixer = NvMediaVideoMixerCreate(ctx->device, // device,
                                         width, // mixerWidth
                                         height, // mixerHeight
                                         aspectRatio, // sourceAspectRatio
                                         videoWidth, // primaryVideoWidth
                                         videoHeight, // primaryVideoHeight
                                         0, // secondaryVideoWidth
                                         0, // secondaryVideoHeight
                                         0, // graphics0Width
                                         0, // graphics0Height
                                         0, // graphics1Width
                                         0, // graphics1Height
                                         features,
                                         NULL);

    if (!ctx->mixer) {
        LOG_ERR("MixerInit: Unable to create mixer\n");
        return NVMEDIA_STATUS_ERROR;
    }

    if(ctx->filterQuality) {
        switch(ctx->filterQuality) {
            case 1: // Low
                mixerAttributes.filterQuality = NVMEDIA_FILTER_QUALITY_LOW;
                break;
            case 2: // Medium
            default:
                mixerAttributes.filterQuality = NVMEDIA_FILTER_QUALITY_MEDIUM;
                break;
            case 3: // High
                mixerAttributes.filterQuality = NVMEDIA_FILTER_QUALITY_HIGH;
                break;
        }
    }

    attributeMask = NVMEDIA_VIDEO_MIXER_ATTRIBUTE_DEINTERLACE_TYPE_PRIMARY |
                    NVMEDIA_VIDEO_MIXER_ATTRIBUTE_INVERSE_TELECINE |
                    NVMEDIA_VIDEO_MIXER_ATTRIBUTE_COLOR_STANDARD_PRIMARY |
                    (ctx->filterQuality ?
                     NVMEDIA_VIDEO_MIXER_ATTRIBUTE_FILTER_QUALITY : 0);

    switch (ctx->colorPrimaries) {
        case NVEColorPrimaries_BT709:
            mixerAttributes.colorStandardPrimary = ctx->videoFullRangeFlag ?
                    NVMEDIA_COLOR_STANDARD_ITUR_BT_709_ER : NVMEDIA_COLOR_STANDARD_ITUR_BT_709;
            break;
        case NVEColorPrimaries_SMPTE240M:
            mixerAttributes.colorStandardPrimary = NVMEDIA_COLOR_STANDARD_SMPTE_240M;
            break;
        case NVEColorPrimaries_Unspecified:
        default:
            mixerAttributes.colorStandardPrimary = ctx->videoFullRangeFlag ?
                    NVMEDIA_COLOR_STANDARD_ITUR_BT_601_ER : NVMEDIA_COLOR_STANDARD_ITUR_BT_601;
            break;
    }

    NvMediaVideoMixerSetAttributes(ctx->mixer,
                                   NVMEDIA_OUTPUT_DEVICE_0,    // outputMask
                                   attributeMask,
                                   &mixerAttributes);

    LOG_DBG("MixerInit: Created mixer: %p\n", ctx->mixer);
    return NVMEDIA_STATUS_OK;
}

int DisplayInit(VideoDemoTestCtx *ctx, int width, int height, int videoWidth, int videoHeight)
{
    NvMediaStatus status;

    LOG_DBG("DisplayInit: Output create 0. displayId %d, enabled? %d\n",
            ctx->displayId, ctx->displayEnabled);

    ctx->output = NvMediaVideoOutputCreate((NvMediaVideoOutputType)0,   // outputType
                                           (NvMediaVideoOutputDevice)0, // outputDevice
                                           NULL,                        // outputPreference
                                           ctx->displayDeviceEnabled,   // alreadyCreated
                                           ctx->displayId,
                                           ctx->windowId,
                                           NULL);                       // displayHandle
    if (!ctx->output) {
        LOG_ERR("DisplayInit: Unable to create output\n");
        return -1;
    }

    if (ctx->positionSpecifiedFlag) {
        status = NvMediaVideoOutputSetPosition(ctx->output, &ctx->position);
        if (status != NVMEDIA_STATUS_OK)
            LOG_ERR("DisplayInit: Unable to set position\n");
    }

    status = NvMediaVideoOutputSetDepth (ctx->output, ctx->depth);
    if (status != NVMEDIA_STATUS_OK)
        LOG_ERR("DisplayInit: Unable to set depth\n");

    LOG_DBG("DisplayInit: Output create done: %p\n", ctx->output);

    if (MixerInit(ctx, width, height, videoWidth, videoHeight)) {
        LOG_ERR("DisplayInit: Mixer Init error \n");
        return NVMEDIA_STATUS_ERROR;
    }

    LOG_DBG("DisplayInit: Created mixer: %p\n", ctx->mixer);
    return NVMEDIA_STATUS_OK;
}

void DisplayDestroy(VideoDemoTestCtx *ctx)
{
    if (ctx->mixer) {
        NvMediaVideoMixerDestroy(ctx->mixer);
        ctx->mixer = NULL;
    }

    if (ctx->output) {
        NvMediaVideoOutputDestroy(ctx->output);
        ctx->output = NULL;
    }
}

static void ReleaseFrame(VideoDemoTestCtx *ctx, NvMediaVideoSurface *videoSurface)
{
    int i;
    FrameBuffer *p;

    for (i = 0; i < MAX_DEC_BUFFERS; i++) {
        p = &ctx->RefFrame[i];
        if (videoSurface == p->videoSurface) {
            cbRelease((void *)ctx, (NVDPicBuff *)p);
            break;
        }
    }
}

static void ReleaseRenderFrame(VideoDemoTestCtx *ctx, NvMediaVideoSurface *renderSurface)
{
    int i;

    for (i = 0; i < MAX_DISPLAY_BUFFERS; i++) {
        if (!ctx->freeRenderSurfaces[i]) {
            ctx->freeRenderSurfaces[i] = renderSurface;
            break;
        }
    }
}

void DisplayFlush(VideoDemoTestCtx *ctx)
{
    NvMediaVideoSurface *releaseFrames[9], **releaseList = &releaseFrames[0];
    FrameBuffer *pReleaseSurface;
    NvMediaStatus status;

    do {
        status = DeinterlaceFlush(ctx->deinterlaceCtx,
                                  &pReleaseSurface);
        if (status != NVMEDIA_STATUS_OK) {
                LOG_ERR("DisplayFlush: Deinterlace flush failed\n");
                return;
        }

        if (pReleaseSurface)
            ReleaseFrame(ctx, pReleaseSurface->videoSurface);
    } while(pReleaseSurface);

    if (ctx->output) {
        status = NvMediaVideoOutputFlip(ctx->output,
                                        NULL,  // videoSurface
                                        NULL,  // srcRect
                                        NULL,  // dstRect
                                        releaseList,
                                        NULL); // timeStamp

        while (*releaseList) {
            ReleaseRenderFrame(ctx, *releaseList++);
        }
    }
}

static NvMediaVideoSurface *GetRenderSurface(VideoDemoTestCtx *ctx, int *idx)
{
    NvMediaVideoSurface *renderSurface = NULL;
    int i;

    for (i = 0; i < MAX_DISPLAY_BUFFERS; i++) {
        if (ctx->freeRenderSurfaces[i]) {
            renderSurface = ctx->freeRenderSurfaces[i];
            if (idx) {
                *idx = i;
            }
            ctx->freeRenderSurfaces[i] = NULL;
            return renderSurface;
        }
    }

    return NULL;
}

void DisplayFrame(VideoDemoTestCtx *ctx, FrameBuffer *frame)
{
    NvMediaPrimaryVideo primaryVideo;
    NvMediaTime timeStamp;
    NvMediaStatus status;
    NvMediaVideoSurface *renderSurface = NULL;
    NvMediaVideoSurface *releaseFrames[9], **releaseList = &releaseFrames[0];
    NvMediaRect primarySourceRect = { 0, 0, ctx->displayWidth, ctx->displayHeight };
    NvMediaVideoSurface *pOutputSurface[DEINT_LOOPS][DEINT_OUTPUT_SURFACES];
    NvU32 outputLoopCount;
    FrameBuffer *pReleaseSurface;
    NvMediaPictureStructure picStructure[DEINT_LOOPS];
    FrameBuffer* frameBuff;
    NvU32 frameTimeUSec = ctx->frameTimeUSec;
    NvU32 i;

    if (!frame || !frame->videoSurface) {
        LOG_ERR("DisplayFrame: Invalid surface\n");
        return;
    }

    // Deintarlacing purpose ref
    cbAddRef((void *)ctx, (NVDPicBuff *)frame);
    memset(pOutputSurface, 0, sizeof(NvMediaVideoSurface*) * DEINT_LOOPS * DEINT_OUTPUT_SURFACES);
    status = Deinterlace(ctx->deinterlaceCtx,
                         frame,
                         &outputLoopCount,
                         picStructure,
                         pOutputSurface,
                         &pReleaseSurface);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("DisplayFrame: Deinterlacing failed\n");
        return;
    }
    if(outputLoopCount > 0)
        frameTimeUSec = ctx->frameTimeUSec / outputLoopCount;

    if (pReleaseSurface)
        ReleaseFrame(ctx, pReleaseSurface->videoSurface);

    memset(&primaryVideo, 0, sizeof(primaryVideo));
    primaryVideo.srcRect = &primarySourceRect;
    primaryVideo.dstRect = NULL;

    // Start timing at the first frame
    if (!ctx->lDispCounter)
        GetTimeUtil(&ctx->baseTime);

    NvAddTime(&ctx->baseTime, (NvU64)((double)(ctx->lDispCounter + 1) * ctx->frameTimeUSec), &timeStamp);

    for (i = 0; i < outputLoopCount; i++) {
        releaseList = &releaseFrames[0];
        primaryVideo.pictureStructure = picStructure[i];
        primaryVideo.previous2 = pOutputSurface[i][DEINT_OUTPUT_PREV2];
        primaryVideo.previous = pOutputSurface[i][DEINT_OUTPUT_PREV];
        primaryVideo.current = pOutputSurface[i][DEINT_OUTPUT_CURR];
        primaryVideo.next = pOutputSurface[i][DEINT_OUTPUT_NEXT];

        if (primaryVideo.current == NULL) {
            LOG_WARN ("DisplayFrame: Skipping the first frame for Advanced-2 deinterlacing");
            return;
        }

        frameBuff = (FrameBuffer*)((primaryVideo.current)->tag);
        cbAddRef((void *)ctx, (NVDPicBuff *)frameBuff);

        renderSurface = GetRenderSurface(ctx, NULL);
        if (!renderSurface) {
            LOG_ERR("DisplayFrame: renderSurface empty\n");
            return;
        }
        status = NvMediaVideoMixerRenderSurface(ctx->mixer,    // mixer
                                                renderSurface, // outputSurface
                                                NULL,          // background
                                                &primaryVideo, // primaryVideo
                                                NULL,          // secondaryVideo
                                                NULL,          // graphics0
                                                NULL);         // graphics1
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("DisplayFrame: NvMediaVideoMixerRenderSurface failed\n");
            return;
        }

        status = NvMediaVideoOutputFlip(ctx->output,
                                        renderSurface, // videoSurface
                                        NULL,          // srcRect
                                        NULL,          // dstRect
                                        releaseList,
                                        &timeStamp);   // timeStamp

        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("DisplayFrame: NvMediaVideoOutputFlip failed\n");
            return;
        }

        while (*releaseList) {
            ReleaseRenderFrame(ctx, *releaseList++);
        }

        ReleaseFrame(ctx, primaryVideo.current);

        // Calculate time for the second field
        NvAddTime(&timeStamp, (NvU64)((double)frameTimeUSec), &timeStamp);

    }

    ctx->lDispCounter++;
}

static void sig_handler(int sig)
{
    LOG_INFO("sig_handler: Received Signal: %d\n", sig);
    signal_stop = 1;
}

#ifdef NVMEDIA_QNX
static NvBool ReleaseRenderFrameUnderScreen(VideoDemoTestCtx *ctx)
{
    int i;
    int ret, bufCount=0;

    if (ctx->bScreen) {
        ret = screen_get_window_property_iv(ctx->screenArgs.screenWindow,
                                            SCREEN_PROPERTY_RENDER_BUFFER_COUNT,
                                            &bufCount);
        if (ret) {
            LOG_ERR("%s: %s", __func__, "screen_get_window_property_iv()"
                    " failed: SCREEN_PROPERTY_RENDER_BUFFER_COUNT");
            return NV_FALSE;
        }

        if (!ctx->screenArgs.pCurFreeScreenBufs) {
            LOG_ERR("%s: %s", __func__, "screen buffer memory unallocated");
            return NV_FALSE;
        }

        /* Query the available screen buffers */
        ret = screen_get_window_property_pv(ctx->screenArgs.screenWindow,
                                            SCREEN_PROPERTY_RENDER_BUFFERS,
                                            (void**)
                                            ctx->screenArgs.pCurFreeScreenBufs);
        if (ret) {
            LOG_ERR("%s: %s", __func__, "screen_get_window_property_pv()"
                    " failed: SCREEN_PROPERTY_RENDER_BUFFERS");
            return NV_FALSE;
        }

        for (i = 0; i < ctx->nBuffers; i++) {
            /* Don't free the buffer if it still in use by screen
             * for posting
             */
            if (!isScreenBufFree(ctx->screenArgs.pCurFreeScreenBufs,
                                ctx->screenArgs.pScreenBuffers[i],
                                bufCount)) {
                continue;
            }

            if (!ctx->freeRenderSurfaces[i]) {
                ctx->freeRenderSurfaces[i] = ctx->renderSurfaces[i];
            }
        }
    }
    return NV_TRUE;
}

void DisplayFrameScreen(VideoDemoTestCtx *ctx, FrameBuffer *frame) //, int buf_idx)
{
    unsigned int              loop;
    NvMediaPrimaryVideo       primaryVideo;
    unsigned int              outputLoopCount;
    FrameBuffer               *pReleaseSurface;
    NvMediaVideoSurface       *pOutputSurface[DEINT_LOOPS]
                               [DEINT_OUTPUT_SURFACES];
    NvMediaPictureStructure   picStructure[DEINT_LOOPS];
    NvMediaVideoSurface       *renderSurface = NULL;
    NvMediaStatus             status = NVMEDIA_STATUS_OK;
    NvMediaRect               primarySrcRect = { 0, 0, ctx->decodeWidth,
                                                 ctx->decodeHeight };
    NvMediaRect               primaryDstRect = { 0, 0, ctx->displayWidth,
                                                 ctx->displayHeight };
    FrameBuffer               *frameBuff;
    NvU32                     frameTimeUSec = ctx->frameTimeUSec;
    int                       rc;
    int                       buf_idx = 0;
    int                       dirty_rect[4];
    struct timespec           ts;
    int                       sleepTime;

    /* Indicate the window area being updated (full window in our case) */
    dirty_rect[0] = 0;
    dirty_rect[1] = 0;
    dirty_rect[2] = ctx->displayWidth;
    dirty_rect[3] = ctx->displayHeight;

    if (!frame || !frame->videoSurface) {
        LOG_ERR("%s: Invalid surface\n", __func__);
        return;
    }

    cbAddRef((void *)ctx, (NVDPicBuff *)frame);

    memset(pOutputSurface, 0,
           sizeof(NvMediaVideoSurface*) * DEINT_LOOPS * DEINT_OUTPUT_SURFACES);
    status = Deinterlace(ctx->deinterlaceCtx,
                         frame,
                         &outputLoopCount,
                         picStructure,
                         pOutputSurface,
                         &pReleaseSurface);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: %s", __func__, "Deinterlacing failed");
        return;
    }

    if(outputLoopCount > 0)
        frameTimeUSec = ctx->frameTimeUSec / outputLoopCount;

    if (pReleaseSurface)
        ReleaseFrame(ctx, pReleaseSurface->videoSurface);

    /*Initialize primary video*/
    memset(&primaryVideo, 0, sizeof(primaryVideo));
    primaryVideo.srcRect = &primarySrcRect;
    primaryVideo.dstRect = &primaryDstRect;

    for (loop = 0; loop < outputLoopCount; loop++) {
        primaryVideo.pictureStructure = picStructure[loop];
        primaryVideo.previous2 = pOutputSurface[loop][DEINT_OUTPUT_PREV2];
        primaryVideo.previous = pOutputSurface[loop][DEINT_OUTPUT_PREV];
        primaryVideo.current = pOutputSurface[loop][DEINT_OUTPUT_CURR];
        primaryVideo.next = pOutputSurface[loop][DEINT_OUTPUT_NEXT];

        if (primaryVideo.current == NULL) {
            LOG_INFO("%s: Skipping first frame for Advanced-2 deinterlacing",
                     __func__);
            continue;
        }

        frameBuff = (FrameBuffer*)((primaryVideo.current)->tag);
        cbAddRef((void *)ctx, (NVDPicBuff *)frameBuff);

        renderSurface = GetRenderSurface(ctx, &buf_idx);
        if (!renderSurface) {
            LOG_ERR("%s: renderSurface empty\n", __func__);
            return;
        }

        status = NvMediaVideoMixerRenderSurface(ctx->mixer,     //Mixer
                                                renderSurface,  //outputSurface
                                                NULL,           //background
                                                &primaryVideo,  //primary video
                                                NULL,           //background
                                                NULL,           //graphics 1
                                                NULL);          //graphics 2

        if (status != NVMEDIA_STATUS_OK){
            LOG_ERR("%s: %s", __func__,
                    "NvMediaVideoMixerRenderSurface() failed.");
            return;
        }

        ReleaseFrame(ctx, primaryVideo.current); //ctx->RefFrame[buf_idx].videoSurface);

        clock_gettime(CLOCK_REALTIME, &ts);
        g_timeNow = (timespec2nsec(&ts) / 1000);

        if (ctx->lDispCounter > 0) {
            /* Calculate delay to be introduced, such that the duration between
             * posting of two consecutive frames remains as 1/framesPerSec
             */
            sleepTime = (frameTimeUSec * ctx->lDispCounter) - (g_timeNow - g_timeBase);
            if (sleepTime > 0)
                usleep(sleepTime);
        }

        /* Note: screen_post_window() returns immediately if render buffers are
         * available and if SCREEN_WAIT_IDLE flag is not set (not set below)
         */
        rc = screen_post_window(ctx->screenArgs.screenWindow,
                                ctx->screenArgs.pScreenBuffers[buf_idx],
                                1, &dirty_rect[0], 0); // count=1, flags=0
        if (rc && !ctx->stopDecoding && !signal_stop) {
            LOG_ERR("%s: %s", __func__, "Failed to Post Surface to Screen!\n");
        }

        ReleaseRenderFrameUnderScreen(ctx);

        /* Store the timestamp when First Frame gets posted.
         * This timestamp is taken to be the base time for calculating subsequent
         * frame delays
         */
        if (!ctx->lDispCounter) {
            clock_gettime(CLOCK_REALTIME, &ts);
            g_timeBase = (timespec2nsec(&ts) / 1000);
        }

        /* Measure the fps value after displaying FPS_DISPLAY_PERIOD frames. */
        if (ctx->lDispCounter % FPS_DISPLAY_PERIOD == 0) {
            if (ctx->lDispCounter > 0) {
                clock_gettime(CLOCK_REALTIME, &ts );
                g_tFPS = timespec2nsec(&ts) - g_tFPS;
                LOG_INFO("Frame per Second [After %d frames] (fps) = %.2f\n",
                         FPS_DISPLAY_PERIOD,
                         (float)(FPS_DISPLAY_PERIOD * 1000.0 * 1e6 / g_tFPS * 1.0));
            }
            clock_gettime(CLOCK_REALTIME, &ts);
            g_tFPS = timespec2nsec(&ts);
        }
        ctx->lDispCounter++;
    }
}
#endif

int main(int argc, char *argv[])
{
    VideoDemoTestCtx ctx;
    TestArgs testArgs;
    int status;

    memset(&ctx, 0, sizeof(ctx));
    memset(&testArgs, 0, sizeof(testArgs));

    signal(SIGINT, sig_handler);

    LOG_DBG("main: Parsing command line arguments\n");
    status = ParseArgs(argc, argv, &testArgs);
    if(status) {
        if (status != 1)
            PrintUsage();
        return -1;
    }

    LOG_DBG("main: Initializing test context\n");
    if(Init(&ctx, &testArgs)) {
        LOG_ERR("Init failed\n");
        return -1;
    }

    LOG_DBG("main: Starting decode process\n");
    if(Decode(&ctx)) {
        LOG_ERR("Decode failed\n");
        Deinit(&ctx);
#ifdef NVMEDIA_QNX
        DeinitSurfaces(&ctx);
        if (ctx.bScreen) {
            DeinitScreen(&ctx.screenArgs);
        }
#endif
        return -1;
    }

    LOG_DBG("main: Deinitializing\n");
    Deinit(&ctx);
#ifdef NVMEDIA_QNX
    DeinitSurfaces(&ctx);
    if (ctx.bScreen) {
        DeinitScreen(&ctx.screenArgs);
    }
#endif

    if(ctx.showDecodeTimimg) {
        //get decoding time info
        LOG_MSG("\nTotal Decoding time for %d frames: %.3f ms\n", ctx.decodeCount, ctx.totalDecodeTime);
        LOG_MSG("Decoding time per frame %.4f ms \n", ctx.totalDecodeTime / ctx.decodeCount);
    }

    return 0;
}
