/*
 * Copyright (c) 2015-2016, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#ifndef _ERROR_MAX9286_H_
#define _ERROR_MAX9286_H_

#include "nvmedia_isc.h"
#include "img_dev.h"
#include "dev_error.h"
#include "isc_max9286.h"
#include "log_utils.h"

static inline NvMediaStatus
_GetError_max9286(
    NvMediaISCDevice           *iscDeserializer,
    NvU32                      *link,
    ExtImgDevFailureType *errorType
)
{
    NvMediaStatus status;
    ErrorStatusMAX9286 error;
    NvU32 i;

    if(!iscDeserializer)
        return NVMEDIA_STATUS_BAD_PARAMETER;
    if(!link)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    // Check error type here
    status = NvMediaISCGetErrorStatus(iscDeserializer,
                                      sizeof(error),
                                      &error);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: NvMediaISCGetErrorStatus failed\n", __func__);
        return status;
    }

    *link = (NvU32)error.link;

    switch(error.failureType) {
        case ISC_MAX9286_NO_DATA_ACTIVITY:
            for(i = 0; i < MAX_AGGREGATE_IMAGES; i++) {
                if(error.link & MAX9286_GMSL_LINK(i))
                    LOG_ERR("%s: No data activity on link %d\n", __func__,
                            i);
            }
            if(errorType)
                *errorType = EXT_IMG_DEV_NO_DATA_ACTIVITY;
            break;
        case ISC_MAX9286_VIDEO_LINK_ERROR:
            for(i = 0; i < MAX_AGGREGATE_IMAGES; i++) {
                if(error.link & MAX9286_GMSL_LINK(i))
                    LOG_ERR("%s: No video link detected on link %d\n",
                            __func__, i);
            }
            if(errorType)
                *errorType = EXT_IMG_DEV_VIDEO_LINK_ERROR;
            break;
        case ISC_MAX9286_VSYNC_DETECT_FAILURE:
            for(i = 0; i < MAX_AGGREGATE_IMAGES; i++) {
                if(error.link & MAX9286_GMSL_LINK(i))
                    LOG_ERR("%s: No VSYNC detected on link %d\n", __func__,
                            i);
            }
            if(errorType)
                *errorType = EXT_IMG_DEV_VSYNC_DETECT_FAILURE;
            break;
        case ISC_MAX9286_FSYNC_LOSS_OF_LOCK:
            for(i = 0; i < MAX_AGGREGATE_IMAGES; i++) {
                if(error.link & MAX9286_GMSL_LINK(i))
                    LOG_ERR("%s: FSYNC loss of lock detected on link %d\n", __func__,
                            i);
            }
            if(errorType)
                *errorType = EXT_IMG_DEV_FSYNC_LOSS_OF_LOCK;
            break;
        case ISC_MAX9286_LINE_LENGTH_ERROR:
            for(i = 0; i < MAX_AGGREGATE_IMAGES; i++) {
                if(error.link & MAX9286_GMSL_LINK(i))
                    LOG_ERR("%s: Line length error detected on link %d\n", __func__,
                            i);
            }
            if(errorType)
                *errorType = EXT_IMG_DEV_LINE_LENGTH_ERROR;
            break;
        case ISC_MAX9286_LINE_BUFFER_OVERFLOW:
            for(i = 0; i < MAX_AGGREGATE_IMAGES; i++) {
                if(error.link & MAX9286_GMSL_LINK(i))
                    LOG_ERR("%s: Line buffer overflow detected on link %d\n", __func__,
                            i);
            }
            if(errorType)
                *errorType = EXT_IMG_DEV_LINE_BUFFER_OVERFLOW;
            break;
        case ISC_MAX9286_NO_ERROR:
            LOG_ERR("%s:No Link error detected\n", __func__);
            if(errorType)
                *errorType = EXT_IMG_DEV_NO_ERROR;
            break;
        case ISC_MAX9286_NUM_FAILURE_TYPES:
        default:
            LOG_ERR("%s:Invalid link error type\n", __func__);
            return NVMEDIA_STATUS_ERROR;
    }

    return NVMEDIA_STATUS_OK;
}

#endif /* _ERROR_MAX9286_H_ */
