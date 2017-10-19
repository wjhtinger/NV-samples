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
//! \file nvevaapp.h
//! \brief Example implementation of the Early Video App
//------------------------------------------------------------------------------


#ifndef _INCLUDED_NVEARLYVIDEOAPP_H_
#define _INCLUDED_NVEARLYVIDEOAPP_H_

#include "nvassetloader.h"
#include "nvcameracoding.h"
#include "nvevacontrol.h"
#include "nvevainterface.h"
#include "nvcapturevideosink.h"
#include "nvevacoding.h"
#include "nvvideoconnection.h"
#include "nvwelcomeanimation.h"
#include "nvevautils.h"

#define EVA_MAX_COMMANDS 6
#define EVA_MAX_CAPTURE_ERRORS 32
#define EVA_MAX_WATCHDOG_ERRORS 32

typedef enum {
    EVA_INIT,
    EVA_CAMERA_DISPLAY,
    EVA_SPLASH_SCREEN_DISPLAY,
    EVA_WELCOME_ANIMATION,
    EVA_DONE
} EVA_InternalState;

typedef enum {
    EVA_SR_DISPLAY_SPLASH_SCREEN,
    EVA_SR_DISPLAY_WELCOME_ANIMATION,
    EVA_SR_DISPLAY_WELCOME_ANIMATION_DONE,
    EVA_SR_DISPLAY_CAMERA,
    EVA_SR_LOG_CAPTURE_FAILURE,
    EVA_SR_SHUTDOWN,
} EVA_StateRequest;

typedef struct tagCaptureError {
    ECamera_ID CamID;
    U32 errorInfo;
} CaptureError;

typedef enum {
    EVA_Quit = 0,
    StateSet,
    StatusGet,
    LogGet,
    CameraSelect,
    PictureSettings,
    EVA_NumCmds
} EVA_Cmd;

typedef struct {
    EVA_StateRequest   StateRequest;
    U32                Data;
} SSetState;

typedef struct {
    EEVA_Status *       EVA_Status;
} SStatusGet;

typedef struct {
    char               *Log;
    U32                maxLogLength;
} SLogGet;

typedef struct {
    ECamera_ID         cameraID;
} SCameraSelect;

typedef struct {
    float              Brightness;
    float              Contrast;
    float              Saturation;
    float              Hue;
} SPictureSettings;

typedef union {
    SSetState        SetState;
    SStatusGet       StatusGet;
    SLogGet          LogGet;
    SCameraSelect    CameraSelect;
    SPictureSettings PictureSettings;
} UParam;

// Command structure since early video processes all request on single thread
typedef struct {
    EVA_Cmd         Cmd;
    UParam          Param;
    NvEvent *       Event;
    NvResult *      ne;
} EVA_Task;


typedef void  EarlyVideoApp;
typedef NvResult (*FNCmdProc)(EarlyVideoApp *earlyvideoapp, EVA_Task *Task);
typedef void* SurfaceHandle;

typedef struct {
    EVA_StateRequest stateRequest;
    void *Data;
    NvResult result;
} StateMachineRequest;

// Create Instance of EarlyVideoApp
void EarlyVideoAppCreate (void);
// Destroy Instance of EarlyVideoApp
void EarlyVideoAppFinish (void);
// Get Instance of EarlyVideoApp
EarlyVideoApp* GetEarlyVideoApp(void);
// Initialize EarlyVideoApp structure
EarlyVideoApp* EarlyVideoAppInit (void);
// Release EarlyVideoApp structure
NvResult EarlyVideoAppDestroy (EarlyVideoApp *earlyvideoapp);
// Used to inform Early Video Application to start shutdown.
NvResult EVA_StateSet (EarlyVideoApp *earlyvideoapp, EEVA_State eva_state);
// Used to get EVA status
EEVA_Status EVA_StatusGet (EarlyVideoApp *earlyvideoapp);
// Used to retrieve log information from EVA. Since EVA has not knowledge of external logger this information must be gotten from EVA and passed to external logger.
// Please not the only information passed from to EVA is through EVA_StatusSet.
NvResult EVA_LogGet (EarlyVideoApp *earlyvideoapp, char *Log, U32 maxLogLength);
// This control selects camera to be displayed.  Camera mapping must match the camera coding mapping.
NvResult EVA_CameraSelect (EarlyVideoApp *earlyvideoapp, ECamera_ID camID);
// This controls are for adjustments to the picture.  These are mapped to the currently active video (note not all video sources supports all settings).
NvResult EVA_PictureControl (EarlyVideoApp *earlyvideoapp, float Brightness, float Contrast, float Saturation, float Hue);
// This control plays the Welcome animation
NvResult WelcomeAnimationPlay (EarlyVideoApp *earlyvideoapp);
// This is called when Welcome animation is done playing
NvResult WelcomeAnimationDone (EarlyVideoApp *earlyvideoapp);
// Notify Early Video App of video capture failure
NvResult VideoCaptureFail (EarlyVideoApp *earlyvideoapp);
// Notify Early Video App of end of StartCapture
NvResult StartCaptureEndCallback (void *earlyvideoapp, NvResult nr);
// Notify Early Video App of end of StopCapture
NvResult StopCaptureEndCallback (void *earlyvideoapp, NvResult nr);
int EVA_main (EarlyVideoApp *earlyvideoapp, int argc, char* argv[]);
// EarlyVideoApp Thread Processor
void EarlyVideoAppThreadProc (EarlyVideoApp *earlyvideoapp);

void SetEarlyVideoInterface (CaptureVideoSinkCallback *capturevideosinkcallback, EarlyVideoApp *earlyvideoapp);
// NvResult OnCapture (CaptureVideoSinkCallback *capturevideosinkcallback);

#endif /* _INCLUDED_NVEARLYVIDEOAPP_H_ */
