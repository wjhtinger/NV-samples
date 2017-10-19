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
//! \file ga_rle.h
//! \brief This is a graphics streaming RLE encoder/decoder.
//------------------------------------------------------------------------------

#ifndef GA_RLE_H
#define GA_RLE_H


typedef enum
{
    RLE_GA_OK,
    RLE_GA_STATUS_OUTPUT_FULL,
    RLE_GA_STATUS_DECODE_DONE,
    RLE_GA_STATUS_ENCODE_DONE,
    RLE_GA_ERROR_INVALID_INPUT_SIZE,
    RLE_GA_ERROR_INCORRECT_STATE,
    RLE_GA_ERROR_INVALID_INPUT
}RLE_GA_Status;


// RLE
typedef enum
{
    RLE_GA_ENCODE_INVALID,
    RLE_GA_ENCODE_BUILD_TOKEN,
    RLE_GA_ENCODE_WRITE_OUT_COMPRESSED_CHANGE,
    RLE_GA_ENCODE_WRITE_OUT_COMPRESSED_MAX,
    RLE_GA_ENCODE_WRITE_OUT_UNCOMPRESSED_CHANGE,
    RLE_GA_ENCODE_WRITE_OUT_UNCOMPRESSED_MAX,
    RLE_GA_ENCODE_DONE
}RLE_GA_EncodeState;

typedef enum
{
    RLE_GA_DECODE_INVALID,
    RLE_GA_DECODE_UNDECIDED,
    RLE_GA_DECODE_COMPRESSED,
    RLE_GA_DECODE_UNCOMPRESSED,
    RLE_GA_DECODE_WRITEOUT_COMPRESSED,
    RLE_GA_DECODE_DONE
}RLE_GA_DecodeState;

typedef enum
{
    RLE_GA_STREAM_NOT_END,
    RLE_GA_STREAM_END
}RLE_GA_StreamState;

typedef enum
{
    RLE_GA_ENCODE,
    RLE_GA_DECODE
}RLE_GA_Mode;

typedef enum
{
    RLE_GA_RUN_TYPE_UNDECIDED,
    RLE_GA_RUN_TYPE_COMPRESSED,
    RLE_GA_RUN_TYPE_UNCOMPRESSED
}RLE_GA_RUN_TYPE;

typedef struct rle_stream_s {
    unsigned char *next_in;  /* next input byte */
    unsigned int avail_in;  /* number of bytes available at next_in */
    unsigned long total_in;  /* total nb of input bytes read so far */

    unsigned char *next_out; /* next output byte should be put there */
    unsigned int avail_out; /* remaining free space at next_out */
    unsigned long total_out; /* total nb of bytes output so far */

    RLE_GA_Mode currentMode;
    RLE_GA_EncodeState encodeState;
    RLE_GA_DecodeState decodeState;
    RLE_GA_RUN_TYPE runType;
    unsigned int currentRunType;
    unsigned int last32bits;
    unsigned int previous32bits;
    unsigned int currentRunLength;
    unsigned int currentOffsetIntoRunLength;
    unsigned char currentRunData[127*4];
} RLA_GA_Stream;

RLE_GA_Status RLE_GA_EncodeInit(RLA_GA_Stream *pStream);
RLE_GA_Status RLE_GA_EncodeReset(RLA_GA_Stream *pEncodeStream);
RLE_GA_Status RLE_GA_Encode(RLA_GA_Stream *pEncodeStream, RLE_GA_StreamState eStreamState);
RLE_GA_Status RLE_GA_EncodeEnd(RLA_GA_Stream *pEncodeStream);
RLE_GA_Status RLE_GA_DecodeInit(RLA_GA_Stream *pStream);
RLE_GA_Status RLE_GA_DecodeReset(RLA_GA_Stream *pDecodeStream);
RLE_GA_Status RLE_GA_Decode(RLA_GA_Stream *pDecodeStream, RLE_GA_StreamState eStreamState);
RLE_GA_Status RLE_GA_DecodeEnd(RLA_GA_Stream *pDecodeStream);
#endif
