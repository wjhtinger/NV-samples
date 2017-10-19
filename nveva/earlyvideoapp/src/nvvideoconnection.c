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
//! \file nvvideoconnection.c
//! \brief Example Video Connection implentation (NULL interface);
//------------------------------------------------------------------------------

#include "nvvideoconnection.h"

//! Video Connection Interface
typedef struct{
    VideoConnection *video_connection;
    VC_SourceID SrcID;
    VC_SinkID SinkID;
} VideoConnectionPriv;

VideoConnection* VideoConnectionInit ()
{
    VideoConnectionPriv *video_connection_priv = (VideoConnectionPriv *) malloc (sizeof(VideoConnectionPriv));
    if (!video_connection_priv) {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "Video Connection Initialization failed\n");
        return NULL;
    }
    video_connection_priv->SrcID = VCSID_NONE;
    video_connection_priv->SinkID = VCSINKID_NONE;

    return (VideoConnection *) video_connection_priv;
}

NvResult VideoConnectionRelease (VideoConnection *video_connection)
{
    VideoConnectionPriv *video_connection_priv = (VideoConnectionPriv *) video_connection;
    if (video_connection_priv) {
        free (video_connection_priv);
        return RESULT_OK;
    }

    return RESULT_INVALID_POINTER;
}

NvResult VideoConnectionBuild (VideoConnection *video_connection, VC_SourceID SourceID, VC_SinkID SinkID)
{
    VideoConnectionPriv *video_connection_priv = (VideoConnectionPriv *) video_connection;
    if (video_connection_priv && video_connection_priv->SrcID == VCSID_NONE && video_connection_priv->SinkID == VCSINKID_NONE) {
        video_connection_priv->SrcID = SourceID;
        video_connection_priv->SinkID = SinkID;
        return RESULT_OK;
    }
    NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: VideoConnection::Failed to build video connection\n");

    return RESULT_INVALID_STATE;
}

NvResult VideoConnectionRemove(VideoConnection *video_connection, VC_SourceID SourceID, VC_SinkID SinkID)
{
    VideoConnectionPriv *video_connection_priv = (VideoConnectionPriv *) video_connection;
    if (video_connection_priv && video_connection_priv->SrcID == SourceID&& video_connection_priv->SinkID == SinkID) {
        video_connection_priv->SrcID = VCSID_NONE ;
        video_connection_priv->SinkID = VCSINKID_NONE;
        return RESULT_OK;
    }
    NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: VideoConnection::Failed to remove video connection\n");

    return RESULT_INVALID_ARGUMENT;
}
