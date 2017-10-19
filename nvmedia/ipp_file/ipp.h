/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __IPP_H__
#define __IPP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "cmdline.h"
#include "nvmedia_ipp.h"
#include "img_dev.h"

#define IPP_TEST_MAX_AGGREGATE_IMAGES  NVMEDIA_MAX_AGGREGATE_IMAGES
#define FILE_NAME_MAX_LENGTH           256

typedef struct {
    NvMediaBool ispEnabled[IPP_TEST_MAX_AGGREGATE_IMAGES];
    NvMediaBool outputEnabled[IPP_TEST_MAX_AGGREGATE_IMAGES];
    NvMediaBool controlAlgorithmEnabled[IPP_TEST_MAX_AGGREGATE_IMAGES];

    NvMediaDevice              *device;

    // IPPManager
    NvMediaIPPManager          *ippManager;

    // IPP
    NvMediaIPPPipeline         *ipp[NVMEDIA_MAX_PIPELINES_PER_MANAGER];

    // IPP component
    NvMediaIPPComponent *ippComponents[NVMEDIA_MAX_PIPELINES_PER_MANAGER][NVMEDIA_MAX_COMPONENTS_PER_PIPELINE];
    // IPP ISP component
    NvMediaIPPComponent *ippIspComponents[NVMEDIA_MAX_PIPELINES_PER_MANAGER];
    // IPP ISC component
    NvMediaIPPComponent *ippIscComponents[NVMEDIA_MAX_PIPELINES_PER_MANAGER];
    // IPP Control Algorithm component
    NvMediaIPPComponent *ippControlAlgorithmComponents[NVMEDIA_MAX_PIPELINES_PER_MANAGER];
    // IPP component Num
    NvU32 componentNum[NVMEDIA_MAX_PIPELINES_PER_MANAGER];

    // ExtImg Device
    ExtImgDevice               *extImgDevice;
    ExtImgDevParam              extImgDevParam;

    NvMediaBool                 quit;
    NvMediaSurfaceType          inputSurfType;
    NvU32                       inputSurfAttributes;
    NvMediaImageAdvancedConfig  inputSurfAdvConfig;
    NvU32                       inputWidth;
    NvU32                       inputHeight;
    NvMediaRawPixelOrder        inputPixelOrder;

    NvMediaBool                 aggregateFlag;
    NvU32                       imagesNum;
    NvU32                       ippNum;
    NvMediaSurfaceType          ispOutType;
    NvMediaBool                 ispMvFlag;
    NvMediaSurfaceType          ispMvSurfaceType;
    NvMediaSurfaceType          mvSurfaceType;
    NvMediaACPluginType         pluginFlag;

    // save isp machine vision
    NvMediaBool                 saveIspMvFlag;
    char                        ispMvOutputFile[IPP_TEST_MAX_AGGREGATE_IMAGES][FILE_NAME_MAX_LENGTH];
    NvMediaIPPComponent        *ispMvFileWriter[NVMEDIA_MAX_PIPELINES_PER_MANAGER];

    NvMediaBool                 saveMetadataFlag;
    char                        metadataFile[IPP_TEST_MAX_AGGREGATE_IMAGES][FILE_NAME_MAX_LENGTH];

    char                        outputFile[IPP_TEST_MAX_AGGREGATE_IMAGES][FILE_NAME_MAX_LENGTH];
    char                        *inputFileName;
    int                         activeFileWriters;
} IPPTest;

typedef NvMediaStatus (* ProcessorFunc) (IPPTest *ctx);

//  IPP_Init
//
//    IPP_Init()  Create IPP pipelines, allocate needed components
//       and build pipelines between components
//
//  Arguments:
//
//   ctx
//      (out) Pointer to pointer to IPPTest
//
//   testArgs
//      (in) Pointer to test arguments

NvMediaStatus
IPP_Init (IPPTest **ctx, TestArgs *testArgs);

//  IPP_Start
//
//    IPP_Start()  Start IPP pipelines.
//
//  Arguments:
//
//   ctx
//      (int) Pointer to IPPTest
//
//   testArgs
//      (out) Pointer to test arguments

NvMediaStatus
IPP_Start (IPPTest *ctx, TestArgs *testArgs);

//  IPP_Stop
//
//    IPP_Stop()  Stop the IPP pipeline
//
//  Arguments:
//
//   ctx
//      (int) Pointer to IPPTest

NvMediaStatus
IPP_Stop (IPPTest *ctx);

//  IPP_Fini
//
//    IPP_Fini()  Destory the IPP pipeline, and
//       release the resources used
//
//  Arguments:
//
//   ctx
//      (int) Pointer to IPPTest

NvMediaStatus
IPP_Fini (IPPTest *ctx);

#ifdef __cplusplus
}
#endif

#endif // __IPP_H__
