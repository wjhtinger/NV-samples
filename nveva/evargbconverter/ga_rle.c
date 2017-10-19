/*
 * Copyright (c) 2012, NVIDIA Corporation.  All Rights Reserved.
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
//! \file ga_rle.cpp
//! \brief This is a graphics streaming RLE encoder/decoder.
//------------------------------------------------------------------------------

#include <string.h>
#include "ga_rle.h"

static void clearState(RLA_GA_Stream *pStream)
{
    pStream->next_in = NULL;  /* next input byte */
    pStream->avail_in = 0;  /* number of bytes available at next_in */
    pStream->total_in = 0;  /* total nb of input bytes read so far */

    pStream->next_out = NULL; /* next output byte should be put there */
    pStream->avail_out = 0; /* remaining free space at next_out */
    pStream->total_out = 0; /* total nb of bytes output so far */

    pStream->currentMode = RLE_GA_DECODE;
    pStream->encodeState = RLE_GA_ENCODE_INVALID;
    pStream->decodeState = RLE_GA_DECODE_INVALID;
    pStream->runType = RLE_GA_RUN_TYPE_UNDECIDED,

    pStream->last32bits = 0;
    pStream->currentRunLength = 0;
    pStream->currentOffsetIntoRunLength=0;
 }
static void writeOut(RLA_GA_Stream *pStream, unsigned char out)
{
    *pStream->next_out = out;
    pStream->next_out++;
    pStream->avail_out--;
    pStream->total_out++;
}

static unsigned char readIn(RLA_GA_Stream *pStream)
{
    unsigned char returnValue;
    returnValue = *pStream->next_in;
    pStream->next_in++;
    pStream->avail_in--;
    pStream->total_in++;
    return returnValue;
}

RLE_GA_Status RLE_GA_EncodeInit(RLA_GA_Stream *pStream)
{
    if (pStream)
    {
        clearState(pStream);
        pStream->encodeState = RLE_GA_ENCODE_BUILD_TOKEN;
        return RLE_GA_OK;
    }
    return RLE_GA_ERROR_INVALID_INPUT;
}
RLE_GA_Status RLE_GA_EncodeReset(RLA_GA_Stream *pEncodeStream)
{
    if (pEncodeStream)
    {
        clearState(pEncodeStream);
        pEncodeStream->encodeState = RLE_GA_ENCODE_BUILD_TOKEN;
        return RLE_GA_OK;
    }
    return RLE_GA_ERROR_INVALID_INPUT;
}
RLE_GA_Status RLE_GA_Encode(RLA_GA_Stream *pEncodeStream, RLE_GA_StreamState eStreamState)
{
    if (pEncodeStream)
    {
        if (pEncodeStream->encodeState == RLE_GA_ENCODE_INVALID)
        {
            return RLE_GA_ERROR_INCORRECT_STATE;
        }
        if (pEncodeStream->next_in == NULL || pEncodeStream->next_out == NULL)
        {
            return RLE_GA_ERROR_INVALID_INPUT;
        }
        if (pEncodeStream->avail_out == 0)
        {
            return RLE_GA_STATUS_OUTPUT_FULL;
        }
        // Run state machine until done unless state machine returns
        while (pEncodeStream->encodeState != RLE_GA_ENCODE_DONE)
        {
            switch(pEncodeStream->encodeState)
            {
                case RLE_GA_ENCODE_BUILD_TOKEN:
                    while (pEncodeStream->avail_in && pEncodeStream->encodeState == RLE_GA_ENCODE_BUILD_TOKEN)
                    {
                        pEncodeStream->currentRunData[pEncodeStream->currentRunLength] = readIn(pEncodeStream);
                        pEncodeStream->last32bits <<= 8;
                        pEncodeStream->last32bits |= pEncodeStream->currentRunData[pEncodeStream->currentRunLength];
                        pEncodeStream->currentRunLength++;
                        if ((pEncodeStream->currentRunLength & 0x3) == 0)
                        {
                            if (pEncodeStream->currentRunLength > 7)
                            {
                                switch (pEncodeStream->runType)
                                {
                                    case RLE_GA_RUN_TYPE_UNDECIDED:
                                        if (pEncodeStream->previous32bits == pEncodeStream->last32bits)
                                            pEncodeStream->runType = RLE_GA_RUN_TYPE_COMPRESSED;
                                        else
                                            pEncodeStream->runType = RLE_GA_RUN_TYPE_UNCOMPRESSED;
                                        break;
                                    case RLE_GA_RUN_TYPE_COMPRESSED:
                                        if (pEncodeStream->previous32bits != pEncodeStream->last32bits)
                                        {
                                            writeOut(pEncodeStream,(unsigned char) (0x80|((pEncodeStream->currentRunLength-4)/4)));
                                            pEncodeStream->encodeState = RLE_GA_ENCODE_WRITE_OUT_COMPRESSED_CHANGE;
                                        } else if (pEncodeStream->currentRunLength == 127*4) {
                                            writeOut(pEncodeStream,(unsigned char) (0x80|(pEncodeStream->currentRunLength/4)));
                                            pEncodeStream->encodeState = RLE_GA_ENCODE_WRITE_OUT_COMPRESSED_MAX;
                                        }
                                        else if (pEncodeStream->avail_in == 0 && eStreamState == RLE_GA_STREAM_END)
                                        {
                                            writeOut(pEncodeStream,(unsigned char) (0x80|(pEncodeStream->currentRunLength/4)));
                                            pEncodeStream->encodeState = RLE_GA_ENCODE_WRITE_OUT_COMPRESSED_MAX;
                                        }
                                        break;
                                    case RLE_GA_RUN_TYPE_UNCOMPRESSED:
                                        if (pEncodeStream->previous32bits == pEncodeStream->last32bits)
                                        {
                                            // write out header
                                            writeOut(pEncodeStream,(unsigned char) ((pEncodeStream->currentRunLength-8)/4));
                                            pEncodeStream->encodeState = RLE_GA_ENCODE_WRITE_OUT_UNCOMPRESSED_CHANGE;

                                        } else if (pEncodeStream->currentRunLength == 127*4)
                                        {
                                            // write out header
                                            writeOut(pEncodeStream,(unsigned char) (pEncodeStream->currentRunLength/4));
                                            pEncodeStream->encodeState = RLE_GA_ENCODE_WRITE_OUT_UNCOMPRESSED_MAX;
                                        }
                                        else if (pEncodeStream->avail_in == 0 && eStreamState == RLE_GA_STREAM_END)
                                        {
                                            writeOut(pEncodeStream,(unsigned char) (pEncodeStream->currentRunLength/4));
                                            pEncodeStream->encodeState = RLE_GA_ENCODE_WRITE_OUT_UNCOMPRESSED_MAX;
                                        }

                                        break;
                                    default:
                                        return RLE_GA_ERROR_INCORRECT_STATE;
                                        break;
                                }
                            }
                            pEncodeStream->previous32bits = pEncodeStream->last32bits;
                        }
                    }
                    if (pEncodeStream->avail_in == 0 && pEncodeStream->encodeState == RLE_GA_ENCODE_BUILD_TOKEN)
                    {
                        if (eStreamState == RLE_GA_STREAM_END)
                        {
                            if (pEncodeStream->runType == RLE_GA_RUN_TYPE_COMPRESSED)
                            {
                                writeOut(pEncodeStream,(unsigned char) (0x80|(pEncodeStream->currentRunLength/4)));
                                pEncodeStream->encodeState = RLE_GA_ENCODE_WRITE_OUT_COMPRESSED_MAX;
                            }
                            else
                            {
                                pEncodeStream->runType = RLE_GA_RUN_TYPE_UNCOMPRESSED;
                                writeOut(pEncodeStream,(unsigned char) (pEncodeStream->currentRunLength/4));
                                pEncodeStream->encodeState = RLE_GA_ENCODE_WRITE_OUT_UNCOMPRESSED_MAX;
                            }
                        }
                        else
                            return RLE_GA_OK;
                    }
                    break;
                case RLE_GA_ENCODE_WRITE_OUT_COMPRESSED_MAX:
                    // Are we at limit
                    while (pEncodeStream->avail_out)
                    {
                        writeOut(pEncodeStream,pEncodeStream->currentRunData[pEncodeStream->currentOffsetIntoRunLength]);
                        pEncodeStream->currentOffsetIntoRunLength++;
                        if (pEncodeStream->currentOffsetIntoRunLength == 4)
                        {
                            // Reset
                            pEncodeStream->currentRunLength = 0;
                            pEncodeStream->currentOffsetIntoRunLength = 0;
                            pEncodeStream->runType = RLE_GA_RUN_TYPE_UNDECIDED;
                            if (eStreamState == RLE_GA_STREAM_END && pEncodeStream->avail_in == 0)
                                pEncodeStream->encodeState = RLE_GA_ENCODE_DONE;
                            else
                                pEncodeStream->encodeState = RLE_GA_ENCODE_BUILD_TOKEN;
                            break;
                        }
                    }
                    if (pEncodeStream->avail_out == 0)
                        return RLE_GA_STATUS_OUTPUT_FULL;
                    break;
                case RLE_GA_ENCODE_WRITE_OUT_COMPRESSED_CHANGE:
                    // Are we at limit
                    while (pEncodeStream->avail_out)
                    {
                        writeOut(pEncodeStream,pEncodeStream->currentRunData[pEncodeStream->currentOffsetIntoRunLength]);
                        pEncodeStream->currentOffsetIntoRunLength++;
                        if (pEncodeStream->currentOffsetIntoRunLength == 4)
                        {
                            // copy down 4 bytes which were the change
                            memcpy(pEncodeStream->currentRunData,pEncodeStream->currentRunData+(pEncodeStream->currentRunLength-4),4);
                            pEncodeStream->currentRunLength = 4;
                            pEncodeStream->currentOffsetIntoRunLength = 0;
                            pEncodeStream->runType = RLE_GA_RUN_TYPE_UNDECIDED;
                            pEncodeStream->encodeState = RLE_GA_ENCODE_BUILD_TOKEN;
                            break;
                        }
                    }
                    if (pEncodeStream->avail_out == 0)
                        return RLE_GA_STATUS_OUTPUT_FULL;
                    break;
                case RLE_GA_ENCODE_WRITE_OUT_UNCOMPRESSED_MAX:
                    while (pEncodeStream->avail_out)
                    {
                        writeOut(pEncodeStream,pEncodeStream->currentRunData[pEncodeStream->currentOffsetIntoRunLength]);
                        pEncodeStream->currentOffsetIntoRunLength++;
                        if (pEncodeStream->currentOffsetIntoRunLength == pEncodeStream->currentRunLength)
                        {
                            // Reset
                            pEncodeStream->currentRunLength = 0;
                            pEncodeStream->currentOffsetIntoRunLength = 0;
                            pEncodeStream->runType = RLE_GA_RUN_TYPE_UNDECIDED;
                            if (eStreamState == RLE_GA_STREAM_END && pEncodeStream->avail_in == 0)
                                pEncodeStream->encodeState = RLE_GA_ENCODE_DONE;
                            else
                                pEncodeStream->encodeState = RLE_GA_ENCODE_BUILD_TOKEN;
                            break;
                        }
                    }
                    if (pEncodeStream->avail_out == 0)
                        return RLE_GA_STATUS_OUTPUT_FULL;
                    break;
                case RLE_GA_ENCODE_WRITE_OUT_UNCOMPRESSED_CHANGE:
                    while (pEncodeStream->avail_out)
                    {
                        writeOut(pEncodeStream,pEncodeStream->currentRunData[pEncodeStream->currentOffsetIntoRunLength]);
                        pEncodeStream->currentOffsetIntoRunLength++;
                        if (pEncodeStream->currentOffsetIntoRunLength == pEncodeStream->currentRunLength-8)
                        {
                            memcpy(pEncodeStream->currentRunData,pEncodeStream->currentRunData+(pEncodeStream->currentRunLength-8),8);
                            pEncodeStream->currentRunLength = 8;
                            pEncodeStream->currentOffsetIntoRunLength = 0;
                            pEncodeStream->runType = RLE_GA_RUN_TYPE_COMPRESSED;
                            pEncodeStream->encodeState = RLE_GA_ENCODE_BUILD_TOKEN;
                            break;
                        }
                    }
                    if (pEncodeStream->avail_out == 0)
                        return RLE_GA_STATUS_OUTPUT_FULL;
                    break;
                default:
                    return RLE_GA_ERROR_INCORRECT_STATE;
                    break;
            }
        }
        return RLE_GA_STATUS_ENCODE_DONE;
    }
    return RLE_GA_ERROR_INCORRECT_STATE;
}

RLE_GA_Status RLE_GA_EncodeEnd(RLA_GA_Stream *pEncodeStream)
{
    if (pEncodeStream)
    {
        clearState(pEncodeStream);
        return RLE_GA_OK;
    }
    return RLE_GA_ERROR_INVALID_INPUT;
}

RLE_GA_Status RLE_GA_DecodeInit(RLA_GA_Stream *pStream)
{
    if (pStream)
    {
        clearState(pStream);
        pStream->decodeState = RLE_GA_DECODE_UNDECIDED;
        return RLE_GA_OK;
    }
    return RLE_GA_ERROR_INVALID_INPUT;
}

RLE_GA_Status RLE_GA_DecodeReset(RLA_GA_Stream *pDecodeStream)
{
    if (pDecodeStream)
    {
        clearState(pDecodeStream);
        pDecodeStream->decodeState = RLE_GA_DECODE_UNDECIDED;
        return RLE_GA_OK;
    }
    return RLE_GA_ERROR_INVALID_INPUT;
}

RLE_GA_Status RLE_GA_Decode(RLA_GA_Stream *pDecodeStream, RLE_GA_StreamState eStreamState)
{
    unsigned char currentIn;
    if (!pDecodeStream || pDecodeStream->next_in == NULL || pDecodeStream->next_out == NULL)
    {
        return RLE_GA_ERROR_INVALID_INPUT;
    }

    if (pDecodeStream->avail_out == 0)
    {
        return RLE_GA_STATUS_OUTPUT_FULL;
    }

    if (pDecodeStream->decodeState == RLE_GA_DECODE_INVALID)
    {
        return RLE_GA_ERROR_INCORRECT_STATE;
    }

    while (pDecodeStream->decodeState != RLE_GA_DECODE_DONE)
    {
        switch (pDecodeStream->decodeState)
        {
            case RLE_GA_DECODE_UNDECIDED:

                do
                {
                    if (pDecodeStream->avail_in == 0)
                    {
                        if (eStreamState != RLE_GA_STREAM_END)
                            return RLE_GA_OK;
                        else
                        {
                            pDecodeStream->decodeState = RLE_GA_DECODE_DONE;
                            return RLE_GA_STATUS_DECODE_DONE;
                        }
                    }
                } while ((currentIn = readIn(pDecodeStream)) == 0);
                if (currentIn & 0x80)
                {
                    pDecodeStream->decodeState = RLE_GA_DECODE_COMPRESSED;
                    pDecodeStream->runType = RLE_GA_RUN_TYPE_COMPRESSED;
                }
                else
                {
                    pDecodeStream->decodeState = RLE_GA_DECODE_UNCOMPRESSED;
                    pDecodeStream->runType = RLE_GA_RUN_TYPE_UNCOMPRESSED;
                }
                pDecodeStream->currentRunLength = currentIn & 0x7F;
                pDecodeStream->currentRunLength *= 4;
                pDecodeStream->currentOffsetIntoRunLength = 0;
                break;
            case RLE_GA_DECODE_COMPRESSED:
                while (pDecodeStream->avail_in != 0) {
                    pDecodeStream->currentRunData[pDecodeStream->currentOffsetIntoRunLength] = readIn(pDecodeStream);
                    pDecodeStream->currentOffsetIntoRunLength++;
                    if (pDecodeStream->currentOffsetIntoRunLength == 4) {
                        pDecodeStream->decodeState = RLE_GA_DECODE_WRITEOUT_COMPRESSED;
                        pDecodeStream->currentOffsetIntoRunLength = 0;
                        break;
                    }
                }
                break;
            case RLE_GA_DECODE_WRITEOUT_COMPRESSED:
                while (pDecodeStream->avail_out && pDecodeStream->currentOffsetIntoRunLength<pDecodeStream->currentRunLength)
                {
                    writeOut(pDecodeStream,pDecodeStream->currentRunData[pDecodeStream->currentOffsetIntoRunLength&0x3]);
                    pDecodeStream->currentOffsetIntoRunLength++;
                }
                if (pDecodeStream->currentOffsetIntoRunLength==pDecodeStream->currentRunLength)
                {
                    pDecodeStream->decodeState = RLE_GA_DECODE_UNDECIDED;
                    pDecodeStream->currentOffsetIntoRunLength = 0;
                    pDecodeStream->currentRunLength = 0;
                }
                else if (pDecodeStream->avail_out == 0)
                    return RLE_GA_STATUS_OUTPUT_FULL;
                break;
            case RLE_GA_DECODE_UNCOMPRESSED:
                while (pDecodeStream->avail_in && pDecodeStream->avail_out && pDecodeStream->currentOffsetIntoRunLength<pDecodeStream->currentRunLength)
                {
                    writeOut(pDecodeStream,readIn(pDecodeStream));
                    pDecodeStream->currentOffsetIntoRunLength++;
                }
                if (pDecodeStream->currentOffsetIntoRunLength==pDecodeStream->currentRunLength)
                {
                    pDecodeStream->decodeState = RLE_GA_DECODE_UNDECIDED;
                    pDecodeStream->currentOffsetIntoRunLength = 0;
                    pDecodeStream->currentRunLength = 0;
                } else  if (pDecodeStream->avail_out == 0)
                    return RLE_GA_STATUS_OUTPUT_FULL;
                break;
            default:
                return RLE_GA_ERROR_INCORRECT_STATE;
                break;
        }
    }
    if (eStreamState == RLE_GA_STREAM_END && pDecodeStream->decodeState == RLE_GA_DECODE_UNDECIDED)
        pDecodeStream->decodeState = RLE_GA_DECODE_DONE;
    if (pDecodeStream->decodeState == RLE_GA_DECODE_DONE)
        return RLE_GA_STATUS_DECODE_DONE;

    return RLE_GA_OK;
}

RLE_GA_Status RLE_GA_DecodeEnd(RLA_GA_Stream *pDecodeStream)
{
    if (pDecodeStream)
    {
        clearState(pDecodeStream);
        return RLE_GA_OK;
    }
    return RLE_GA_ERROR_INVALID_INPUT;
}
