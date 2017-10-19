/* Copyright (c) 2014-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __EGL_CONSUMER_H__
#define __EGL_CONSUMER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "result.h"
#include "player-core-priv.h"

#define GST_NVM_DISPLAY_REFRESH_FREQ    30000

//  gst_nvm_player_egl_init
//
//    gst_nvm_player_egl_init()  Allocates resources for creating egl thread pool
//
//  Arguments:
//
//   player_data
//      (in) Data for egl thread pool

GstNvmResult
gst_nvm_player_egl_init (void);

//  gst_nvm_player_egl_init
//
//    gst_nvm_player_egl_fini ()  Frees resources for of egl thread pool

GstNvmResult
gst_nvm_player_egl_fini (void);

//  gst_nvm_player_start_egl_playback
//
//    gst_nvm_player_start_egl_playback() Signals playback to start with EGL.

GstNvmResult
gst_nvm_player_start_egl_playback (
    GstNvmContextHandle handle);

//  gst_nvm_player_stop_egl_playback
//
//    gst_nvm_player_stop_egl_playback() Brings pipeline to paused state and waits for
//                                the consumer to be destroyed.
//
//  Arguments:
//
//   handle
//      (in) Handle to player instance context

GstNvmResult
gst_nvm_player_stop_egl_playback (
    GstNvmContextHandle handle);

//  gst_nvm_player_init_egl_cross_process
//
//    gst_nvm_player_init_egl_cross_process()  Initializing EGL stream
//              in cross process case
//
//  Arguments:
//
//   handle
//      (in) Handle to player instance context

GstNvmResult
gst_nvm_player_init_egl_cross_process (
    GstNvmContextHandle handle);

//  gst_nvm_player_start_egl_thread
//
//    gst_nvm_player_start_egl_thread()  Initializing EGL stream and consumer
//                               and starts egl display thread
//
//  Arguments:
//
//   handle
//      (in) Handle to player instance context

GstNvmResult
gst_nvm_player_start_egl_thread (
    GstNvmContextHandle handle);

//  gst_nvm_player_stop_egl_thread
//
//    gst_nvm_player_stop_egl_thread()  Terminating EGL and stops egl display thread
//
//  Arguments:
//
//   handle
//      (in) Handle to player instance context

GstNvmResult
gst_nvm_player_stop_egl_thread (
    GstNvmContextHandle handle);

#ifdef __cplusplus
}
#endif

#endif
