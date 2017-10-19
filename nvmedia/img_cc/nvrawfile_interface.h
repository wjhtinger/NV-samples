/* Copyright (c) 2015-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _NVRAWFILE_INTERFACE_H_
#define _NVRAWFILE_INTERFACE_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_EXPOSURE_MODES 3

// Latest version
#define NVRAW_INTERFACE_VERSION 2
//
// Version history:
//
// Version 2.0
// Change 936449 (nvrawfile_interface: add new HDRInfo version)
//
// Version 1.0
// Add basic interface definitions and APIs

/**
 * \brief The set of all NvRaw Output Compression Formats.
 */

typedef enum
{
     NvRawCompressionFormat_10BitLinear = 0,
     NvRawCompressionFormat_2x11_1,
     NvRawCompressionFormat_3x12,
     NvRawCompressionFormat_12BitLinear,
     NvRawCompressionFormat_12BitCombinedCompressed,
     NvRawCompressionFormat_12BitCombinedCompressedExtended,
     NvRawCompressionFormat_16bitLinear,
     NvRawCompressionFormat_16BitLogDomain,
     NvRawCompressionFormat_16BitLogDomainExtended,
     NvRawCompressionFormat_20BitLinear,
     NvRawCompressionFormat_20BitLinearExtended
} NvRawOutputCompressionFormat;

/**
 * \brief The set of all possible error codes.
 */
typedef enum {
    /** \The operation completed successfully; no error. */
    NvRawFileError_Success = 0,
    /** Bad parameter was passed. */
    NvRawFileError_BadParameter,
    /** File Read operation Failed. */
    NvRawFileError_FileReadFailed,
    /** File Write operation Failed. */
    NvRawFileError_FileWriteFailed,
    /** Operation timed out. */
    NvRawFileError_Timeout,
    /** Out of memory. */
    NvRawFileError_InsufficientMemory,
    /** A catch-all error, used when no other error code applies. */
    NvRawFileError_Failure
} NvRawFileErrorStatus;

/**
 * NvRawSensorHDRInfo version1
 * This is old structure and to be
 * replaced by NvRawSensorHDRInfo_v2
 */
typedef struct {
    struct {
       NvF32 exposureTime;
       NvF32 analogGain;
       NvF32 digitalGain;
       NvU32 conversionGain;
    } exposure[MAX_EXPOSURE_MODES];

    struct {
        float value[4];
    } wbGain[MAX_EXPOSURE_MODES];
} NvRawSensorHDRInfo;

/**
 * NvRawSensorHDRInfo version2
 * Latest structure definition version
 * This version should be used in place
 * of outdated NvRawSensorHDRInfo
 */
typedef struct {
    struct {
       NvF32 exposureTime;
       NvF32 analogGain;
       NvF32 digitalGain;
       NvU32 conversionGain;
    } exposure;

    struct {
        float value[4];
    } wbGain;
} NvRawSensorHDRInfo_v2;


/**
 * NvRawFileHeaderChunk - first chunk present in NvRaw file.
 */
typedef struct {} NvRawFileHeaderChunkHandle;
NvRawFileHeaderChunkHandle* NvRawFileHeaderChunkCreate(void);
NvRawFileErrorStatus NvRawFileHeaderChunkSize(NvRawFileHeaderChunkHandle* hdr, NvU32 *size);
NvRawFileErrorStatus NvRawFileHeaderChunkFileWrite(NvRawFileHeaderChunkHandle* hdr, FILE* f);
NvRawFileErrorStatus NvRawFileHeaderChunkMemWrite(NvRawFileHeaderChunkHandle* hdr, NvU8* dest, NvU8** memPtr);
NvRawFileErrorStatus NvRawFileHeaderChunkGetBitsPerSample(NvRawFileHeaderChunkHandle *hdr,
                                     NvU32 *bitsPerSample);
NvRawFileErrorStatus NvRawFileHeaderChunkSetBitsPerSample(NvRawFileHeaderChunkHandle *hdr,
                                     NvU32 bitsPerSample);
NvRawFileErrorStatus NvRawFileHeaderChunkGetSamplesPerPixel(NvRawFileHeaderChunkHandle *hdr,
                                       NvU32 *samplesPerPixel);
NvRawFileErrorStatus NvRawFileHeaderChunkSetSamplesPerPixel(NvRawFileHeaderChunkHandle *hdr,
                                       NvU32 samplesPerPixel);
NvRawFileErrorStatus NvRawFileHeaderChunkGetNumImages(NvRawFileHeaderChunkHandle *hdr,
                                 NvU32 *numImages);
NvRawFileErrorStatus NvRawFileHeaderChunkSetNumImages(NvRawFileHeaderChunkHandle *hdr,
                                 NvU32 numImages);
NvRawFileErrorStatus NvRawFileHeaderChunkGetProcessingFlags(NvRawFileHeaderChunkHandle *hdr,
                                       NvU32 *processingFlags);
NvRawFileErrorStatus NvRawFileHeaderChunkSetProcessingFlags(NvRawFileHeaderChunkHandle *hdr,
                                       NvU32 processingFlags);
NvRawFileErrorStatus NvRawFileHeaderChunkGetDataFormat(NvRawFileHeaderChunkHandle *hdr,
                                  NvU32 *dataFormat);
NvRawFileErrorStatus NvRawFileHeaderChunkSetDataFormat(NvRawFileHeaderChunkHandle *hdr,
                                  NvU32 dataFormat);

NvRawFileErrorStatus NvRawFileHeaderChunkGetImageWidth(NvRawFileHeaderChunkHandle *hdr,
                                  NvU32 *width);
NvRawFileErrorStatus NvRawFileHeaderChunkSetImageWidth(NvRawFileHeaderChunkHandle *hdr,
                                  NvU32 width);
NvRawFileErrorStatus NvRawFileHeaderChunkGetImageHeight(NvRawFileHeaderChunkHandle *hdr,
                                   NvU32 *height);
NvRawFileErrorStatus NvRawFileHeaderChunkSetImageHeight(NvRawFileHeaderChunkHandle *hdr,
                                   NvU32 height);
void NvRawFileHeaderChunkDelete(NvRawFileHeaderChunkHandle* hdr);


/**
 * NvRawFileDataChunk- second chunk present in NvRaw file.
 *
 */
typedef struct {} NvRawFileDataChunkHandle;
NvRawFileDataChunkHandle* NvRawFileDataChunkCreate(NvU32 dataLength, NvBool shouldAlloc);
NvRawFileErrorStatus NvRawFileDataChunkGetSize(NvRawFileDataChunkHandle* dc, NvU32 *size);
NvRawFileErrorStatus NvRawFileDataChunkFileWrite(NvRawFileDataChunkHandle* dc, FILE* f, NvBool writePixels);
NvRawFileErrorStatus NvRawFileDataChunkMemWrite(NvRawFileDataChunkHandle* dc, NvU8* dest, NvBool writePixels, NvU8** memPtr);
NvRawFileErrorStatus NvRawFileDataChunkGet(NvRawFileDataChunkHandle* nrfd, NvU16** dataChunk);
void NvRawFileDataChunkDelete(NvRawFileDataChunkHandle* dc);


/*
 * NvRawFileCaptureChunk - contains the image's exposure
 * information.
 */
typedef enum
{
    NvRawFilePixelFormat_Int16 = 1,
    NvRawFilePixelFormat_S114
} NvRawFilePixelFormat;

typedef struct {} NvRawFileCaptureChunkHandle;
NvRawFileCaptureChunkHandle* NvRawFileCaptureChunkCreate(void);
NvRawFileErrorStatus NvRawFileCaptureChunkVersion(NvRawFileCaptureChunkHandle* cc, NvU32 *version);
NvRawFileErrorStatus NvRawFileCaptureChunkGetSize(NvRawFileCaptureChunkHandle* cc, NvU32 *size);
NvRawFileErrorStatus NvRawFileCaptureChunkFileWrite(NvRawFileCaptureChunkHandle* cc, FILE* f);
NvRawFileErrorStatus NvRawFileCaptureChunkMemWrite(NvRawFileCaptureChunkHandle* cc, NvU8* dest, NvU8** memPtr);
void NvRawFileCaptureChunkDelete(NvRawFileCaptureChunkHandle* cc);
NvRawFileErrorStatus NvRawFileCaptureChunkInvalidateExposureForHDR(NvRawFileCaptureChunkHandle *cc);
NvRawFileErrorStatus NvRawFileCaptureChunkSetIspDigitalGain(NvRawFileCaptureChunkHandle *cc, NvF32 ispDigitalGain);
NvRawFileErrorStatus NvRawFileCaptureChunkGetIspDigitalGain(NvRawFileCaptureChunkHandle *cc,  NvF32 *ispDigitalGain);
NvRawFileErrorStatus NvRawFileCaptureChunkSetPixelFormat(NvRawFileCaptureChunkHandle *cc,
                                                         NvRawFilePixelFormat pf);
NvRawFileErrorStatus NvRawFileCaptureChunkGetPixelFormat(NvRawFileCaptureChunkHandle *cc,
                                                         NvRawFilePixelFormat *pf);
NvRawFileErrorStatus NvRawFileCaptureChunkSetOutputDataFormat(NvRawFileCaptureChunkHandle *cc,
                                         NvRawOutputCompressionFormat outputCompressionFormat);
NvRawFileErrorStatus NvRawFileCaptureChunkGetOutputDataFormat(NvRawFileCaptureChunkHandle *cc,
                                         NvRawOutputCompressionFormat *outputCompressionFormat);
NvRawFileErrorStatus NvRawFileCaptureChunkSetPixelEndianness(NvRawFileCaptureChunkHandle *cc,
                                          NvBool bPixelLittleEndian);
NvRawFileErrorStatus NvRawFileCaptureChunkGetPixelEndianness(NvRawFileCaptureChunkHandle *cc,
                                          NvBool *bPixelLittleEndian);
NvRawFileErrorStatus NvRawFileCaptureChunkSetEmbeddedLineCountTop(NvRawFileCaptureChunkHandle *cc,
                                             NvU32 embeddedLineCount);
NvRawFileErrorStatus NvRawFileCaptureChunkGetEmbeddedLineCountTop(NvRawFileCaptureChunkHandle *cc,
                                             NvU32 *embeddedLineCount);
NvRawFileErrorStatus NvRawFileCaptureChunkSetEmbeddedLineCountBottom(NvRawFileCaptureChunkHandle *cc,
                                                NvU32 embeddedLineCount);
NvRawFileErrorStatus NvRawFileCaptureChunkGetEmbeddedLineCountBottom(NvRawFileCaptureChunkHandle *cc,
                                                NvU32 *embeddedLineCount);
NvRawFileErrorStatus NvRawFileCaptureChunkSetLut(NvRawFileCaptureChunkHandle *cc, NvU8 *pBuffer, NvU32 size);
NvRawFileErrorStatus NvRawFileCaptureChunkGetLut(NvRawFileCaptureChunkHandle *cc, NvU8 **ppBuffer, NvU32 *pSize);
NvRawFileErrorStatus NvRawFileCaptureChunkSetExposureTime(NvRawFileCaptureChunkHandle *cc,
                                     NvF32 exposureTime);
NvRawFileErrorStatus NvRawFileCaptureChunkGetExposureTime(NvRawFileCaptureChunkHandle *cc,
                                     NvF32 *exposureTime);
NvRawFileErrorStatus NvRawFileCaptureChunkSetISO(NvRawFileCaptureChunkHandle *cc, NvU32 iso);
NvRawFileErrorStatus NvRawFileCaptureChunkGetISO(NvRawFileCaptureChunkHandle *cc, NvU32 *iso);
NvRawFileErrorStatus NvRawFileCaptureChunkSetSensorGain(NvRawFileCaptureChunkHandle *cc, NvF32 *sensorGains);
NvRawFileErrorStatus NvRawFileCaptureChunkGetSensorGain(NvRawFileCaptureChunkHandle *cc, NvF32 *sensorGains);
NvRawFileErrorStatus NvRawFileCaptureChunkSetFlashPower(NvRawFileCaptureChunkHandle *cc,
                                   NvF32 flashPower);
NvRawFileErrorStatus NvRawFileCaptureChunkGetFlashPower(NvRawFileCaptureChunkHandle *cc,
                                   NvF32 *flashPower);
NvRawFileErrorStatus NvRawFileCaptureChunkSetFocusPosition(NvRawFileCaptureChunkHandle *cc,
                                      NvS32 focusPosition);
NvRawFileErrorStatus NvRawFileCaptureChunkGetFocusPosition(NvRawFileCaptureChunkHandle *cc,
                                      NvS32 *focusPosition);
NvRawFileErrorStatus NvRawFileCaptureChunkSetLux(NvRawFileCaptureChunkHandle *cc,
                                      NvF32 lux);
NvRawFileErrorStatus NvRawFileCaptureChunkGetLux(NvRawFileCaptureChunkHandle *cc,
                                      NvF32 *lux);


/**
 * NvRawFileHDRChunk- information about
 * the exposure, pixel readout, etc.
 */

typedef struct {} NvRawFileHDRChunkHandle;
NvRawFileHDRChunkHandle* NvRawFileHDRChunkCreate(void);
NvRawFileErrorStatus NvRawFileHDRChunkGetVersion(NvRawFileHDRChunkHandle* hc, NvU32* version);
NvRawFileErrorStatus NvRawFileHDRChunkGetSize(NvRawFileHDRChunkHandle* hc, NvU32* size);
NvRawFileErrorStatus NvRawFileHDRChunkFileWrite(NvRawFileHDRChunkHandle* hc, FILE* f);
NvRawFileErrorStatus NvRawFileHDRChunkMemWrite(NvRawFileHDRChunkHandle* hc, NvU8* dest, NvU8** memPtr);
void NvRawFileHDRChunkDelete(NvRawFileHDRChunkHandle* hc);
NvRawFileErrorStatus NvRawFileHDRChunkSetNumberOfExposures(NvRawFileHDRChunkHandle* hc, NvU32 count);
NvRawFileErrorStatus NvRawFileHDRChunkSetExposureInfo(NvRawFileHDRChunkHandle* hc, NvRawSensorHDRInfo *sensorInfo);
NvRawFileErrorStatus NvRawFileHDRChunkSetExposureInfo_v2(NvRawFileHDRChunkHandle* hc, NvRawSensorHDRInfo_v2 *sensorInfo);
NvRawFileErrorStatus NvRawFileHDRChunkSetReadoutScheme(NvRawFileHDRChunkHandle* hc, const char* scheme);
NvRawFileErrorStatus NvRawFileHDRChunkGetReadoutScheme(NvRawFileHDRChunkHandle* hc, const char** scheme);
NvRawFileErrorStatus NvRawFileHDRChunkGetExposureInfoSize(NvRawFileHDRChunkHandle* hc, NvU32* exposureInfoSize);

/**
 * NvRawFileSensorInfoChunk - contains several ASCII strings.
 * This chunk and other chunks that contain strings should
 * not be used with sizeof() since their size is not dictated
 * by structure layout, but by dynamic contents. Use
 * NvRawFileSensorInfoChunk_size() and NvRawFileSensorInfoChunk_set()
 * to ensure correct memory management and size determination.
 */
typedef struct {} NvRawFileSensorInfoChunkHandle;
NvRawFileSensorInfoChunkHandle* NvRawFileSensorInfoChunkCreate(void);
NvRawFileErrorStatus NvRawFileSensorInfoChunkGetSize(NvRawFileSensorInfoChunkHandle* sc, NvU32* size);
NvRawFileErrorStatus NvRawFileSensorInfoChunkSetSensor(NvRawFileSensorInfoChunkHandle* sc, const char* sensorString);
NvRawFileErrorStatus NvRawFileSensorInfoChunkGetSensor(NvRawFileSensorInfoChunkHandle* sc, const char** sensorString);
NvRawFileErrorStatus NvRawFileSensorInfoChunkSetFuse(NvRawFileSensorInfoChunkHandle* sc, const char* fuseString);
NvRawFileErrorStatus NvRawFileSensorInfoChunkGetFuse(NvRawFileSensorInfoChunkHandle* sc, const char** fuseString);
NvRawFileErrorStatus NvRawFileSensorInfoChunkSetModule(NvRawFileSensorInfoChunkHandle* sc, const char* moduleString);
NvRawFileErrorStatus NvRawFileSensorInfoChunkGetModule(NvRawFileSensorInfoChunkHandle* sc, const char** moduleString);
NvRawFileErrorStatus NvRawFileSensorInfoChunkFileWrite(NvRawFileSensorInfoChunkHandle* sc, FILE* f);
NvRawFileErrorStatus NvRawFileSensorInfoChunkMemWrite(NvRawFileSensorInfoChunkHandle* sc, NvU8* dest, NvU8** memPtr);
void NvRawFileSensorInfoChunkDelete(NvRawFileSensorInfoChunkHandle* sc);


/**
 * NvRawFileCameraStateChunk- contains information about
 * the auto-algorithms' convergence states at the time
 * of exposure.
 */
typedef struct {} NvRawFileCameraStateChunkHandle;
NvRawFileCameraStateChunkHandle* NvRawFileCameraStateChunkCreate(void);
NvRawFileErrorStatus NvRawFileCameraStateChunkGetSize(NvRawFileCameraStateChunkHandle* ec, NvU32 *size);
NvRawFileErrorStatus NvRawFileCameraStateChunkFileWrite(NvRawFileCameraStateChunkHandle* ec, FILE* f);
NvRawFileErrorStatus NvRawFileCameraStateChunkMemWrite(NvRawFileCameraStateChunkHandle* ec, NvU8* dest, NvU8** memPtr);
void NvRawFileCameraStateChunkDelete(NvRawFileCameraStateChunkHandle* ec);

#ifdef __cplusplus
};     /* extern "C" */
#endif

#endif /* _NVMEDIA_RAWFILE_H */

