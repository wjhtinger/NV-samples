/*
 * Copyright (c) 2013-2016 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software and related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#ifndef _NVMEDIA_TEST_MISC_UTILS_H_
#define _NVMEDIA_TEST_MISC_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "nvcommon.h"
#include "nvmedia.h"
#include "nvmedia_image.h"
#include "nvmedia_idp.h"

#define RCV_MAX_FRAME_SIZE   2048 * 1024
#define RCV_VC1_TYPE         0x85
#define RCV_V2_MASK          (1<<6)
#ifndef __INTEGRITY
#define MIN(a,b)             (((a) < (b)) ? (a) : (b))
#define MAX(a,b)             (((a) > (b)) ? (a) : (b))
#endif
#define COPYFIELD(a,b,field) (a)->field = (b)->field
#define MAX_STRING_SIZE      256
#define MAX_OUTPUT_DEVICES   3

#define IsFailed(result)    result != NVMEDIA_STATUS_OK
#define IsSucceed(result)   result == NVMEDIA_STATUS_OK

typedef struct _RCVFileHeader {
    NvS32   lNumFrames;
    NvS32   bRCVIsV2Format;
    NvU32   uProfile;
    NvS32   lMaxCodedWidth;
    NvS32   lMaxCodedHeight;
    NvS32   lHrdBuffer;
    NvS32   lBitRate;
    NvS32   lFrameRate;
    NvS32   cbSeqHdr;       // Should always be 4 for simple/main
    NvU8    SeqHdrData[32];
} RCVFileHeader;

//  u32
//
//    u32()  Reads 4 bytes from buffer and returns the read value
//
//  Arguments:
//
//   ptr
//      (in) Input buffer

NvU32   u32(const NvU8* ptr);

//  GetAvailableDisplayDevices
//
//    GetAvailableDisplayDevices()  Returns a list of available display devices.
//                                  It's the responsibility of the calling function
//                                  to release this memory after use.
//
//  Arguments:
//
//   outputDevicesNum
//      (out) Pointer to number of devices
//
//   outputDevicesList
//      (out) Pointer to a list of available output devices

NvMediaStatus
GetAvailableDisplayDevices(
    int *outputDevicesNum,
    NvMediaVideoOutputDeviceParams *outputDevicesList);

//  CheckDisplayDeviceType
//
//    CheckDisplayDeviceType()  Get display type by display id
//
//  Arguments:
//
//   displayId
//      (in) Requested display id
//
//   enabled
//      (out) Pointer to display enabled flag

NvMediaStatus
CheckDisplayDeviceID(
    unsigned int displayId,
    NvMediaBool *enabled);

//  GetTimeMicroSec
//
//    GetTimeMicroSec()  Returns current time in microseconds
//
//  Arguments:
//
//   uTime
//      (out) Pointer to current time in microseconds

NvMediaStatus
GetTimeMicroSec(
    NvU64 *uTime);

//  GetTime
//
//    GetTime()  Gets current time
//
//  Arguments:
//
//   time
//      (out) Pointer to current time structure

NvMediaStatus
GetTimeUtil(
    NvMediaTime *time);

//  AddTime
//
//    AddTime()  Adds microseconds to given time and returns pointer to the result
//
//  Arguments:
//
//   time
//      (in) Pointer to base time structure
//
//   usec
//      (in) Number of microseconds to add to base time
//
//   res
//      (out) Pointer to NvMediaTime result structure

NvMediaStatus
NvAddTime(
    NvMediaTime *time,
    NvU64 usec,
    NvMediaTime *res);

//  SubTime
//
//    SubTime()  Substructs times and returns the difference in milliseconds
//
//  Arguments:
//
//   time1
//      (in) Pointer to base time structure
//
//   time2
//      (in) Number of milliseconds to substruct from base time
//
//   res
//      (out) Pointer to time difference (time1 - time2) in milliseconds

NvMediaStatus
NvSubTime(
    NvMediaTime *time1,
    NvMediaTime *time2,
    NvS64 *res);

//  CalculateBufferCRC
//
//    CalculateBufferCRC()  Calculated CRC for a given buffer and base CRC value
//
//  Arguments:
//
//   count
//      (in) buffer length in bytes
//
//   crc
//      (in) Base CRC value
//
//   buffer
//      (in) Pointer to buffer

NvU32
CalculateBufferCRC(
    NvU32 count,
    NvU32 crc,
    NvU8 *buffer);

NvS32
ParseRCVHeader(
    RCVFileHeader *pHdr,
    const NvU8 *pBuffer,
    NvS32 lBufferSize);

//  GetDisplayName
//
//    GetDisplayName()  Get display name for given video output
//       device type
//
//  Arguments:
//
//   deviceType
//      (in) device type
//
//   displayName
//      (out) Pre-allocated memory for display name string

NvMediaStatus
GetDisplayName(
    NvMediaVideoOutputDevice deviceType,
    char *displayName);

//  GetImageDisplayName
//
//    GetImageDisplayName()  Get display name for given image
//       display device type
//
//  Arguments:
//
//   deviceType
//      (in) device type
//
//   displayName
//      (out) Pre-allocated memory for display name string

NvMediaStatus
GetImageDisplayName(
    NvMediaIDPDeviceType deviceType,
    char *displayName);

//  SetRect
//
//    SetRect()  Sets NvMediaRect structure with given values
//
//  Arguments:
//
//   rect
//      (in) Pointer to NvMediaRect
//
//   x0
//      (in) x0 point of the rectangle
//
//   y0
//      (in) y0 point of the rectangle
//
//   x1
//      (in) x1 point of the rectangle
//
//   y1
//      (in) y1 point of the rectangle

NvMediaStatus
SetRect (
    NvMediaRect *rect,
    unsigned short x0,
    unsigned short y0,
    unsigned short x1,
    unsigned short y1);

#ifdef __cplusplus
}
#endif

#endif /* _NVMEDIA_TEST_MISC_UTILS_H_ */
