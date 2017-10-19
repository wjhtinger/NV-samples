/*
 * Copyright (c) 2013-2016 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software and related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "log_utils.h"
#include "misc_utils.h"

#define  CRC32_POLYNOMIAL   0xEDB88320L

NvU32
u32(const NvU8* ptr)
{
    return ptr[0] | (ptr[1]<<8) | (ptr[2]<<16) | (ptr[3]<<24);
}

NvMediaStatus
GetAvailableDisplayDevices(
    int *outputDevicesNum,
    NvMediaVideoOutputDeviceParams *outputDevicesList)
{
    NvMediaStatus status;

    // Get device information for all devices
    status = NvMediaVideoOutputDevicesQuery(outputDevicesNum, outputDevicesList);
    if( status != NVMEDIA_STATUS_OK) {
        LOG_ERR("CheckDisplayDevice: Failed querying devices. Error: %d\n", status);
        return status;
    }

    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
CheckDisplayDeviceID(
    unsigned int displayId,
    NvMediaBool *enabled)
{
    int outputDevices;
    NvMediaVideoOutputDeviceParams *outputParams;
    NvMediaStatus status;
    unsigned int found = 0;
    int i;

    // By default set it as not enabled (initialized)
    *enabled = NVMEDIA_FALSE;

    // Get the number of devices
    status = NvMediaVideoOutputDevicesQuery(&outputDevices, NULL);
    if( status != NVMEDIA_STATUS_OK) {
        LOG_ERR("CheckDisplayDeviceType: Failed querying the number of devices. Error: %d\n", status);
        return status;
    }

    // Allocate memory for information for all devices
    outputParams = malloc(outputDevices * sizeof(NvMediaVideoOutputDeviceParams));
    if(!outputParams)
        return NVMEDIA_STATUS_OUT_OF_MEMORY;

    // Get device information for all devices
    status = NvMediaVideoOutputDevicesQuery(&outputDevices, outputParams);
    if( status != NVMEDIA_STATUS_OK) {
        LOG_ERR("CheckDisplayDeviceType: Failed querying devices. Error: %d\n", status);
        free(outputParams);
        return status;
    }

    // Find desired device
    for(i = 0; i < outputDevices; i++) {
        if((outputParams + i)->displayId == displayId) {
            // Return information
            *enabled = (outputParams + i)->enabled;
            found = 1;
            break;
        }
    }

    free(outputParams);

    if(!found) {
        LOG_ERR("CheckDisplayDeviceID: Requested display id is invalid (%d)\n", displayId);
        return NVMEDIA_STATUS_ERROR;
    }

    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
GetTimeMicroSec(
    NvU64 *uTime)
{
    struct timespec t;
#if !(defined(CLOCK_MONOTONIC) && defined(_POSIX_MONOTONIC_CLOCK) && _POSIX_MONOTONIC_CLOCK >= 0 && _POSIX_TIMERS > 0)
    struct timeval tv;
#endif

    if(!uTime)
        return NVMEDIA_STATUS_BAD_PARAMETER;

#if !(defined(CLOCK_MONOTONIC) && defined(_POSIX_MONOTONIC_CLOCK) && _POSIX_MONOTONIC_CLOCK >= 0 && _POSIX_TIMERS > 0)
    gettimeofday(&tv, NULL);
    t.tv_sec = tv.tv_sec;
    t.tv_nsec = tv.tv_usec*1000L;
#else
    clock_gettime(CLOCK_MONOTONIC, &t);
#endif

    *uTime = (NvU64)t.tv_sec * 1000000LL + (NvU64)t.tv_nsec / 1000LL;
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
GetTimeUtil(
    NvMediaTime *time)
{
    struct timespec t;
#if !(defined(CLOCK_MONOTONIC) && defined(_POSIX_MONOTONIC_CLOCK) && _POSIX_MONOTONIC_CLOCK >= 0 && _POSIX_TIMERS > 0)
    struct timeval tv;
#endif

    if(!time)
        return NVMEDIA_STATUS_BAD_PARAMETER;

#if !(defined(CLOCK_MONOTONIC) && defined(_POSIX_MONOTONIC_CLOCK) && _POSIX_MONOTONIC_CLOCK >= 0 && _POSIX_TIMERS > 0)
    gettimeofday(&tv, NULL);
    t.tv_sec = tv.tv_sec;
    t.tv_nsec = tv.tv_usec*1000L;
#else
    clock_gettime(CLOCK_MONOTONIC, &t);
#endif

    time->tv_sec = t.tv_sec;
    time->tv_nsec = t.tv_nsec;
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
NvAddTime(
    NvMediaTime *time,
    NvU64 usec,
    NvMediaTime *res)
{
    NvU64 t, new_time;

    if(!time || !res)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    t = (NvU64)time->tv_sec * 1000000000LL + (NvU64)time->tv_nsec;
    new_time = t + usec * 1000LL;
    res->tv_sec = new_time / 1000000000LL;
    res->tv_nsec = new_time % 1000000000LL;
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
NvSubTime(
    NvMediaTime *time1,
    NvMediaTime *time2,
    NvS64 *res)
{
    NvS64 t1, t2;

    if(!time1 || !time2)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    t1 = (NvS64)time1->tv_sec * 1000000000LL + (NvS64)time1->tv_nsec;
    t2 = (NvS64)time2->tv_sec * 1000000000LL + (NvS64)time2->tv_nsec;
    *res = (t1 - t2) / 1000000LL;
    return NVMEDIA_STATUS_OK;
}

static void
BuildCRCTable(
    NvU32 *crcTable)
{
    NvU16 i;
    NvU16 j;
    NvU32 crc;

    if (!crcTable) {
        LOG_ERR("BuildCRCTable: Failed creating CRC table - bad pointer for crcTable %p\n", crcTable);
        return;
    }

    for (i = 0; i <= 255; i++) {
        crc = i;
        for (j = 8; j > 0; j--) {
            if (crc & 1) {
                crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
            } else {
                crc >>= 1;
            }
        }
        crcTable[i] = crc;
    }
    return;
}

NvU32
CalculateBufferCRC(
    NvU32 count,
    NvU32 crc,
    NvU8 *buffer)
{
    NvU8 *p;
    NvU32 temp1;
    NvU32 temp2;
    static NvU32 crcTable[256];
    static int initialized = 0;

    if(!initialized) {
        BuildCRCTable(crcTable);
        initialized = 1;
    }
    p = (NvU8*) buffer;
    while (count-- != 0) {
        temp1 = (crc >> 8) & 0x00FFFFFFL;
        temp2 = crcTable[((NvU32) crc ^ *p++) & 0xFF];
        crc = temp1 ^ temp2;
    }
    return crc;
}

NvS32
ParseRCVHeader(
    RCVFileHeader *pHdr,
    const NvU8 *pBuffer,
    NvS32 lBufferSize)
{
    NvS32 lNumFrames, profile, level;
    NvU32 uType, tmp, res1, uHdrSize;
    NvU32 cur = 0;

    // The first 3 bytes are the number of frames
    lNumFrames = pBuffer[cur++];
    lNumFrames |= pBuffer[cur++] << 8;
    lNumFrames |= pBuffer[cur++] << 16;
    if (lNumFrames <= 0)
        return 0;
    pHdr->lNumFrames = lNumFrames;
    LOG_DBG("pHdr->lNumFrames = %d \n", pHdr->lNumFrames);
    // The next byte is the type and extension flag
    uType = pBuffer[cur++];
    LOG_DBG("uType = %d \n", uType);
    if ((uType & ~RCV_V2_MASK) != RCV_VC1_TYPE)
        return 0;
    pHdr->bRCVIsV2Format = ((uType & RCV_V2_MASK) != 0);
    LOG_DBG("pHdr->bRCVIsV2Format = %d \n", pHdr->bRCVIsV2Format);
    // Next 4 bytes are the size of the extension data
    pHdr->cbSeqHdr = u32(pBuffer+cur);
    LOG_DBG("pHdr->cbSeqHdr = %d \n", pHdr->cbSeqHdr);
    cur += 4;
    memcpy(pHdr->SeqHdrData, pBuffer+cur, pHdr->cbSeqHdr);
    // STRUCT_C
    profile = pBuffer[cur] >> 6;
    cur += pHdr->cbSeqHdr;
    LOG_DBG("VC1 profile = %d \n", profile);
    if (profile >= 2) {
        LOG_ERR("High profile RCV is not supported\n");
        return 0;   // Must be Simple or Main (AP handled as VC1 elementary stream)
    }
    // STRUCT_A
    pHdr->lMaxCodedHeight = u32(pBuffer+cur);
    LOG_DBG("pHdr->lMaxCodedHeight = %d \n", pHdr->lMaxCodedHeight);
    cur += 4;
    if ((pHdr->lMaxCodedHeight <= 31) || (pHdr->lMaxCodedHeight > 2048-32))
        return 0;
    pHdr->lMaxCodedWidth = u32(pBuffer+cur);
    LOG_DBG("pHdr->lMaxCodedWidth = %d \n", pHdr->lMaxCodedWidth);
    cur += 4;
    if ((pHdr->lMaxCodedWidth <= 15) || (pHdr->lMaxCodedWidth > 4096-16))
        return 0;
    tmp = u32(pBuffer+cur); // 0x0000000c
    cur += 4;
    if (tmp != 0x0000000c)
        return 0;
    // STRUCT_B
    tmp = u32(pBuffer+cur);  // level = tmp >> 29 & 0x7; cbr = tmp >> 28 & 0x1;
    cur += 4;
    level = tmp >> 29;
    res1 = (tmp >> 24) & 0xf;
    if ((res1 != 0x0) || (level > 0x4))
        return 0;
    pHdr->lHrdBuffer = (tmp >> 0) & 0xffffff;
    tmp = u32(pBuffer+cur);
    cur += 4;
    pHdr->lBitRate = (tmp >> 0) & 0xffffff;
    pHdr->lFrameRate = u32(pBuffer+cur);
    cur += 4;
    uHdrSize = cur;
    LOG_DBG("uHdrSize = %d \n", uHdrSize);
    return uHdrSize;
}

NvMediaStatus
GetDisplayName (
    NvMediaVideoOutputDevice deviceType,
    char *displayName)
{
    if(deviceType == NvMediaVideoOutputDevice_LVDS)
        strncpy(displayName, "lvds", MAX_STRING_SIZE);
    else if(deviceType == NvMediaVideoOutputDevice_HDMI)
        strncpy(displayName, "hdmi", MAX_STRING_SIZE);
    else if(deviceType == NvMediaVideoOutputDevice_VGA)
        strncpy(displayName, "vga", MAX_STRING_SIZE);
    else if(deviceType == NvMediaVideoOutputDevice_DP)
        strncpy(displayName, "dp", MAX_STRING_SIZE);
    else if(deviceType == NvMediaVideoOutputDevice_Display0)
        strncpy(displayName, "display 0", MAX_STRING_SIZE);
    else if(deviceType == NvMediaVideoOutputDevice_Display1)
        strncpy(displayName, "display 1", MAX_STRING_SIZE);
    else
        return NVMEDIA_STATUS_ERROR;

    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
GetImageDisplayName (
    NvMediaIDPDeviceType deviceType,
    char *displayName)
{
    if(deviceType == NvMediaIDPDeviceType_LVDS)
        strncpy(displayName, "lvds", MAX_STRING_SIZE);
    else if(deviceType == NvMediaIDPDeviceType_HDMI)
        strncpy(displayName, "hdmi", MAX_STRING_SIZE);
    else if(deviceType == NvMediaIDPDeviceType_DP)
        strncpy(displayName, "dp", MAX_STRING_SIZE);
    else if(deviceType == NvMediaIDPDeviceType_VGA)
        strncpy(displayName, "vga", MAX_STRING_SIZE);
    else if(deviceType == NvMediaIDPDeviceType_DSI)
        strncpy(displayName, "dsi", MAX_STRING_SIZE);
    else if(deviceType == NvMediaIDPDeviceType_TFTLCD)
        strncpy(displayName, "TFTLCD", MAX_STRING_SIZE);
    else
        return NVMEDIA_STATUS_ERROR;

    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
SetRect (
    NvMediaRect *rect,
    unsigned short x0,
    unsigned short y0,
    unsigned short x1,
    unsigned short y1)
{
    if(!rect)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    rect->x0 = x0;
    rect->x1 = x1;
    rect->y0 = y0;
    rect->y1 = y1;

    return NVMEDIA_STATUS_OK;
}
