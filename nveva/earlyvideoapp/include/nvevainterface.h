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
//! \file nvevainterface.h
//! \brief Interface to EarlyVideoApp
//------------------------------------------------------------------------------

#ifndef _INCLUDED_NVEVAINTERFACE_H_
#define _INCLUDED_NVEVAINTERFACE_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "nvevatypes.h"
#include "nvevautils.h"

#define EA_ASSET_DIR_ENV_VAR "EA_ASSET_DIR"

// Early video needs to control what should be displayed.  Logic is if camera selected then camera should be displayed
// if available.  If no camera selected then what is displayed depends on how EVA is configured.  Either splash screen
// should be displayed or welcome animation.   Camera display always has priority (so if camera is selected it should
// be displayed).

// Order matters
typedef enum {
    ECID_REAR_VIEW = 0,
    ECID_SIDE_VIEW,
    ECID_TOP_VIEW,
    ECID_NONE_SELECTED,
    ECID_MAX_ID
} ECamera_ID;

typedef enum {
    CPC_Brightness,
    CPC_ChromaHue,
    CPC_ChromaSaturation,
    CPC_Contrast
} EPictureControl;

// Ordering matters do not reorder
typedef enum {
    EVA_IDLE = 0,
    EVA_DISPLAY_CAMERA_RVC,
    EVA_DISPLAY_CAMERA_SVC,
    EVA_DISPLAY_CAMERA_TVC,
    EVA_CAPTURE_FAILURE_CAMERA_RVC,
    EVA_CAPTURE_FAILURE_CAMERA_SVC,
    EVA_CAPTURE_FAILURE_CAMERA_TVC,
    EVA_DISPLAY_SPLASH_SCREEN,
    EVA_DISPLAY_WELCOME_ANIMATION,
    EVA_SHUTDOWN_OK,
    EVA_SHUTDOWN_FAILURES,
    EVA_STATE_ERROR
} EEVA_Status;

typedef enum {
    EVA_SHUTDOWN
} EEVA_State;

typedef enum {
    EVA_VCF_CAPTURE_TIMEOUT,
    EVA_VCF_CAPTURE_CORRUPTION_ERROR,
    EVA_VCF_CAPTURE_SEQUENCE_ERROR,
} EEVA_VideoCaptureFailure;

#endif /* _INCLUDED_NVEVAINTERFACE_H_ */
