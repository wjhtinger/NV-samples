/*
 * Copyright (c) 2014, NVIDIA Corporation.  All Rights Reserved.
 *
 * BY INSTALLING THE SOFTWARE THE USER AGREES TO THE TERMS BELOW.
 *
 * User agrees to use the software under carefully controlled conditions
 * and to inform all employees and contractors who have access to the software
 * that the source code of the software is confidential and proprietary
 * information of NVIDIA and is licensed to user as such.  User acknowledges
 * and agrees that protection of the source code is essential and user shall
 * retain the source code in strict confidence.  User shall restrict access to
 * the source code of the software to those employees and contractors of user
 * who have agreed to be bound by a confidentiality obligation which
 * incorporates the protections and restrictions substantially set forth
 * herein, and who have a need to access the source code in order to carry out
 * the business purpose between NVIDIA and user.  The software provided
 * herewith to user may only be used so long as the software is used solely
 * with NVIDIA products and no other third party products (hardware or
 * software).   The software must carry the NVIDIA copyright notice shown
 * above.  User must not disclose, copy, duplicate, reproduce, modify,
 * publicly display, create derivative works of the software other than as
 * expressly authorized herein.  User must not under any circumstances,
 * distribute or in any way disseminate the information contained in the
 * source code and/or the source code itself to third parties except as
 * expressly agreed to by NVIDIA.  In the event that user discovers any bugs
 * in the software, such bugs must be reported to NVIDIA and any fixes may be
 * inserted into the source code of the software by NVIDIA only.  User shall
 * not modify the source code of the software in any way.  User shall be fully
 * responsible for the conduct of all of its employees, contractors and
 * representatives who may in any way violate these restrictions.
 *
 * NO WARRANTY
 * THE ACCOMPANYING SOFTWARE (INCLUDING OBJECT AND SOURCE CODE) PROVIDED BY
 * NVIDIA TO USER IS PROVIDED "AS IS."  NVIDIA DISCLAIMS ALL WARRANTIES,
 * EXPRESS, IMPLIED OR STATUTORY, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF TITLE, MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.

 * LIMITATION OF LIABILITY
 * NVIDIA SHALL NOT BE LIABLE TO USER, USERS CUSTOMERS, OR ANY OTHER PERSON
 * OR ENTITY CLAIMING THROUGH OR UNDER USER FOR ANY LOSS OF PROFITS, INCOME,
 * SAVINGS, OR ANY OTHER CONSEQUENTIAL, INCIDENTAL, SPECIAL, PUNITIVE, DIRECT
 * OR INDIRECT DAMAGES (WHETHER IN AN ACTION IN CONTRACT, TORT OR BASED ON A
 * WARRANTY), EVEN IF NVIDIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGES.  THESE LIMITATIONS SHALL APPLY NOTWITHSTANDING ANY FAILURE OF THE
 * ESSENTIAL PURPOSE OF ANY LIMITED REMEDY.  IN NO EVENT SHALL NVIDIAS
 * AGGREGATE LIABILITY TO USER OR ANY OTHER PERSON OR ENTITY CLAIMING THROUGH
 * OR UNDER USER EXCEED THE AMOUNT OF MONEY ACTUALLY PAID BY USER TO NVIDIA
 * FOR THE SOFTWARE PROVIDED HEREWITH.
 */

//------------------------------------------------------------------------------
//! \file nvevaargbconverter.cpp
//! \brief This is the main part of utility to convert RAW RGB graphic file
//! \brief to RGBA format used by Early apps.  Please note flipping
//! \brief issues.
//------------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "nvearlyappdecompression.h"
#include "nvevatypes.h"
#include "ga_rle.h"
#include "lzf.h"

int DecompressToBuffer(char *pFileName, void *pOutputBuffer, U32 *pOutputSize);
void GRLE_Deflate (unsigned char *pInput, int inputSize, unsigned char *pOutput, int *pOutputSize);
void GRLE_Inflate(unsigned char *pInput, int inputSize, unsigned char *pOutput, int *pOutputSize);
void DisplayUsage(void);

// Deflate
// File Name
void GRLE_Deflate (unsigned char *pInput, int inputSize, unsigned char *pOutput, int *pOutputSize)
{
    RLA_GA_Stream encodeStream;
    if (RLE_GA_EncodeInit(&encodeStream) == RLE_GA_OK) {
        if (RLE_GA_EncodeReset(&encodeStream) == RLE_GA_OK) {
            encodeStream.avail_out = *pOutputSize;
            encodeStream.next_out = pOutput;
            encodeStream.next_in = pInput;
            encodeStream.avail_in = inputSize;
            if (RLE_GA_Encode(&encodeStream,RLE_GA_STREAM_NOT_END) != RLE_GA_OK) {
                printf("Failed to encode stream\n");
                *pOutputSize = 0;
                return;
            }
            if (RLE_GA_Encode(&encodeStream,RLE_GA_STREAM_END) == RLE_GA_STATUS_ENCODE_DONE) {
                *pOutputSize = encodeStream.total_out;
            } else {
                printf("Failed to encode stream\n");
                *pOutputSize = 0;
            }
            if (RLE_GA_EncodeEnd(&encodeStream) != RLE_GA_OK) {
                printf("Failed to end stream\n");
            }
        } else {
            printf("Failed to Reset encoder\n");
            *pOutputSize = 0;
        }
    } else {
        printf("Failed to init encoder\n");
        *pOutputSize = 0;
    }
}
// Inflate
void GRLE_Inflate(unsigned char *pInput, int inputSize, unsigned char *pOutput, int *pOutputSize)
{
    RLA_GA_Stream decodeStream;
    if (RLE_GA_DecodeInit(&decodeStream) == RLE_GA_OK) {
        if (RLE_GA_DecodeReset(&decodeStream) == RLE_GA_OK) {
            decodeStream.avail_in = inputSize;
            decodeStream.next_in = pInput;
            decodeStream.avail_out = *pOutputSize;
            decodeStream.next_out = pOutput;
            if (RLE_GA_Decode(&decodeStream,RLE_GA_STREAM_END) == RLE_GA_STATUS_DECODE_DONE) {
                *pOutputSize = decodeStream.total_out;
            } else {
                printf("Failed to decode stream\n");
                *pOutputSize = 0;
            }
            if (RLE_GA_DecodeEnd(&decodeStream) != RLE_GA_OK) {
                printf("Failed to end stream\n");
            }
        } else {
            printf("Failed to Reset decoder\n");
            *pOutputSize = 0;
        }
    } else {
        printf("Failed to init decoder\n");
        *pOutputSize = 0;
    }

}


void DisplayUsage(void) {
     printf("EvaRgbConverter Width Height BlackAlpha nonBlackAlpha InputFile OutputFile [EncodeType]\n");
     printf("where\n");
     printf("\tWidth is width of input image\n");
     printf("\tHeight is height of input image\n");
     printf("\tBlackAlpha is alpha value to use for black (0-255)\n");
     printf("\tnonBlackAlpha is alpha value to use for non black (0-255)\n");
     printf("\tInputFile is a binary file with RGB data\n");
     printf("\tOutputFile is name for output file in Nvidia Early App format\n");
     printf("\tEncodetype is RLE, LZF, or RAW (default is LZF)\n");
 }

int main(int argc, char* argv[])
{
    short width = 0;
    short height = 0;
    U16 encodeType = NEA_TYPE_LZF;
    int imageSizePixels = 0;
    char *fileNameInput = NULL;
    FILE *fpInput = NULL;
    char *fileNameOutput = NULL;
    FILE *fpOutput = NULL;
    unsigned char *pRgbBuffer = NULL;
    unsigned char *pRgbaBuffer = NULL;
    unsigned char *pOutputBuffer = NULL;
    int outputSize;
    int outputSizeMax;
    int temp;
    char blackAlpha;
    char nonBlackAlpha;
    NeaFileHeader  fileHeader;
    int readSize, newSize, j;
    if (argc != 7 && argc != 8) {
        DisplayUsage();
        return -1;;
    }
    temp = atoi(argv[1]);
    if (temp < 1 || temp > 2560 ) {
        printf("Illegal value for width %d\n",temp);
        return -1;
    }
    width = (short) (temp&0xFFFF);

    temp = atoi(argv[2]);
    if (temp < 1 || temp > 2560 ) {
        printf("Illegal value for height %d\n",temp);
        return -1;
    }
    height = (short) (temp&0xFFFF);

    temp = atoi(argv[3]);
    if (temp < 0 || temp > 255 ) {
        printf("Illegal value for black alpha %d\n",temp);
        return -1;
    }
    blackAlpha  = (char) (temp&0xFF);

    temp = atoi(argv[4]);
    if (temp < 0 || temp > 255 ) {
        printf("Illegal value for non black alpha %d\n",temp);
        return -1;
    }
    nonBlackAlpha  = (char) (temp&0xFF);


    fileNameInput = argv[5];
    fileNameOutput = argv[6];
    if (argc == 8) {
        if (strcmp("RLE",argv[7]) == 0)
            encodeType = NEA_TYPE_GRLE;
        else if (strcmp("RAW",argv[7]) == 0)
            encodeType = NEA_TYPE_RAW;
        else if (strcmp("LZF",argv[7]) == 0)
            encodeType = NEA_TYPE_LZF;
        else {
            printf("Illegal encode type choice\n");
            return -1;
        }

    }

    imageSizePixels = width*height;
    if (imageSizePixels == 0) {
        printf("Illegal size (width or height is zero)\n");
    }
    pRgbBuffer = (unsigned char *) malloc(width*height*3);
    pRgbaBuffer = (unsigned char *) malloc(width*height*4);
    outputSizeMax = outputSize = 5*width*height; // More than output could ever be;
    pOutputBuffer = (unsigned char *) malloc(outputSize);
    if (!pRgbBuffer || !pRgbaBuffer || !pOutputBuffer) {
        printf("ran out of memory\n");
        return -1;
    }

    fpInput = fopen(fileNameInput,"rb");
    if (fpInput && imageSizePixels){
        readSize = fread(pRgbBuffer,1,imageSizePixels*3,fpInput);
        if (readSize == imageSizePixels*3) {
            // Expand the data to add alpha
            for (j=0, newSize= 0;j < imageSizePixels*3; j+=3, newSize+=4)
            {
                pRgbaBuffer[newSize] = pRgbBuffer[j];
                pRgbaBuffer[newSize+1] = pRgbBuffer[j+1];
                pRgbaBuffer[newSize+2] = pRgbBuffer[j+2];
                if (pRgbaBuffer[newSize] || pRgbaBuffer[newSize+1] || pRgbaBuffer[newSize+2])
                    pRgbaBuffer[newSize+3] = nonBlackAlpha;
                else
                    pRgbaBuffer[newSize+3] = blackAlpha;
            }

            fpOutput = fopen(fileNameOutput,"wb");
            if (fpOutput)
            {
                switch(encodeType) {
                    case NEA_TYPE_LZF:
                        outputSize = (int) lzf_compress ((const void*)pRgbaBuffer,(unsigned int)newSize,(void*)pOutputBuffer,(unsigned int)outputSize);
                        break;
                    case NEA_TYPE_GRLE:
                        GRLE_Deflate(pRgbaBuffer,newSize,pOutputBuffer,&outputSize);
                        break;
                    case NEA_TYPE_RAW:
                        memcpy(pOutputBuffer,pRgbaBuffer,newSize);
                        outputSize = newSize;
                        break;
                    default:
                        printf("unrecognized encode type\n");
                        return -1;
                        break;
                }
                fileHeader.header[0] = 'N';
                fileHeader.header[1] = 'E';
                fileHeader.header[2] = 'A';
                fileHeader.header[3] = 'C';
                fileHeader.type = encodeType;
                fileHeader.width = width;
                fileHeader.height = height;
                fileHeader.reserved1 = 0;
                fileHeader.dataLengthCompressed = outputSize;
                fileHeader.dataLengthOriginal = newSize;
                fwrite(&fileHeader,1,sizeof(NeaFileHeader),fpOutput);
                fwrite(pOutputBuffer,1,outputSize,fpOutput);
                printf("Compression ratio %f\n",(float) outputSize / (float) newSize );
                if (fpOutput)
                    fclose(fpOutput);

                outputSize = outputSizeMax;
                if (DecompressToBuffer((char*)argv[6], (void*)pOutputBuffer, (U32*)&outputSize) != RESULT_OK) {
                    printf("DecompressToBuffer failed\n");
                }
                if (outputSize != newSize) {
                    printf("Error size mismatch on decompression %d output but %d original\n",outputSize,newSize);
                } else if (memcmp(pRgbaBuffer,pOutputBuffer,newSize) != 0) {
                    printf("memcmp error on decompression\n");
                }
            } else {
                printf("Failed to open output file\n");
            }
        }
        else {
            printf("Input file too small\n");
        }
    }
    if (fpInput)
        fclose(fpInput);
    if (pRgbaBuffer)
        free(pRgbaBuffer);
    if (pRgbBuffer)
        free(pRgbBuffer);
    if (pOutputBuffer)
        free(pOutputBuffer);
    return 0;
}

int DecompressToBuffer(char *pFileName, void *pOutputBuffer, U32 *pOutputSize) {
    FILE *fpInput;
    NeaFileHeader fileHeader;
    fpInput = fopen(pFileName,"rb");
    int nResult = -1;
    if (fpInput) {
        if (fread(&fileHeader,1,sizeof(NeaFileHeader),fpInput) == sizeof(NeaFileHeader)) {
            if (strncmp("NEAC",(const char *)&fileHeader.header[0],4) == 0) {
                if (*pOutputSize >= fileHeader.dataLengthOriginal) {
                    U8 *pTemp = NULL;
                    switch(fileHeader.type) {
                        case NEA_TYPE_LZF:
                        case NEA_TYPE_GRLE:
                            pTemp = (U8 *) malloc(fileHeader.dataLengthCompressed);
                            if (pTemp) {
                                if (fread(pTemp,1,fileHeader.dataLengthCompressed,fpInput) == fileHeader.dataLengthCompressed) {
                                    if (fileHeader.type == NEA_TYPE_LZF) {
                                        if (lzf_decompress (pTemp,fileHeader.dataLengthCompressed, pOutputBuffer, fileHeader.dataLengthOriginal) == fileHeader.dataLengthOriginal) {
                                            *pOutputSize = fileHeader.dataLengthOriginal;
                                            nResult = RESULT_OK;
                                        }
                                    } else {
                                        GRLE_Inflate(pTemp,fileHeader.dataLengthCompressed,(unsigned char *) pOutputBuffer,(int *)pOutputSize);
                                        if (*pOutputSize == fileHeader.dataLengthOriginal)
                                            nResult = RESULT_OK;
                                    }
                                }
                                free(pTemp);
                            }
                            break;
                        case NEA_TYPE_RAW:
                        {
                            int temp;
                            temp = fread(pOutputBuffer,1,fileHeader.dataLengthOriginal,fpInput);
                            if ((unsigned int)temp == fileHeader.dataLengthOriginal) {
                                *pOutputSize = fileHeader.dataLengthOriginal;
                                nResult = RESULT_OK;
                            }
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
        }
        fclose(fpInput);
    }
    return nResult;
}
