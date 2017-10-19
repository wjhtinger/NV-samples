/* Copyright (c) 2014-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <string.h>
#include "player-core.h"
#include "player-core-priv.h"
#include "eglconsumer.h"
#include "cuda_consumer.h"

/* Static Functions */
static void _egl_thread_func (gpointer data, gpointer user_data);
static void _egl_cuda_thread_func (gpointer data, gpointer user_data);

static void
_egl_cuda_thread_func (
    gpointer data,
    gpointer user_data)
{
    GstNvmContext *ctx = (GstNvmContext *) data;
    test_cuda_consumer_s cudaConsumer;
    gboolean cuda_yuv_flag = FALSE;
    int c = 0;

    if (ctx->egl_consumer_type == GST_NVM_EGL_CONSUMER_CUDA_YUV)
        cuda_yuv_flag = TRUE;
    else if (ctx->egl_consumer_type == GST_NVM_EGL_CONSUMER_CUDA_RGB)
        cuda_yuv_flag = FALSE;

    if (!GrUtilInitialize (&c, NULL, "gstnvmplayer", 2, 8, 0))
        return;

    while (!ctx->quit_flag && ctx->eglsink_enabled_flag)
    {
        if (!ctx->egl_initialized_flag)
        {
            if (!EGLStreamInit ())
                break;
            if (!cuda_consumer_init (&cudaConsumer, cuda_yuv_flag))
                break;
            ctx->egl_initialized_flag = TRUE;
        }
        gst_nvm_semaphore_wait (ctx->egl_playback_start_sem); // Wait for playback to start.
        if (ctx->quit_flag || !ctx->eglsink_enabled_flag)
        {
            cuda_consumer_deinit (&cudaConsumer);
            EGLStreamFini ();
            ctx->egl_initialized_flag = FALSE;
            break;
        }
        cudaConsumerProc (&cudaConsumer);
        while (!ctx->egl_playback_stop_flag) {
               cudaConsumerProc (&cudaConsumer);
        }
        cuda_consumer_deinit (&cudaConsumer);
        EGLStreamFini ();
        gst_nvm_semaphore_signal (ctx->egl_playback_stop_sem);
        ctx->egl_initialized_flag = FALSE;
    }
    GrUtilShutdown();
    gst_nvm_semaphore_signal (ctx->egl_playback_stop_sem);
}

static void
_egl_thread_func (
    gpointer data,
    gpointer user_data)
{
    GstNvmContext *ctx = (GstNvmContext *) data;
    int c = 0;

    if (!GrUtilInitialize (&c, NULL, "gstnvmplayer", 2, 8, 0))
        return;

    while (!ctx->quit_flag && ctx->eglsink_enabled_flag)
    {
        if (!ctx->egl_initialized_flag)
        {
            if (!init ())
                break;
            GrUtilSetCloseCB (closeCB);
            GrUtilSetKeyCB (keyCB);
            ctx->egl_initialized_flag = TRUE;
        }
        gst_nvm_semaphore_wait (ctx->egl_playback_start_sem); // Wait for playback to start.
        if (ctx->quit_flag || !ctx->eglsink_enabled_flag)
        {
            fini ();
            cleanup ();
            ctx->egl_initialized_flag = FALSE;
            break;
        }
        drawQuad ();
        eglSwapBuffers (grUtilState.display, grUtilState.surface);
        g_usleep (GST_NVM_DISPLAY_REFRESH_FREQ);
        while (!ctx->egl_playback_stop_flag)
        {
            drawQuad ();
            eglSwapBuffers (grUtilState.display, grUtilState.surface);
            g_usleep (GST_NVM_DISPLAY_REFRESH_FREQ);
        }
        glClearColor (0.0f, 0.0f, 0.0f, 0.0f);
        glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable (GL_BLEND);
        eglSwapBuffers (grUtilState.display, grUtilState.surface);
        fini ();
        cleanup ();
        gst_nvm_semaphore_signal (ctx->egl_playback_stop_sem);
        ctx->egl_initialized_flag = FALSE;
    }
    GrUtilShutdown();
    gst_nvm_semaphore_signal (ctx->egl_playback_stop_sem);
}

GstNvmResult
gst_nvm_player_egl_init (void)
{
    s_player_data.egl_threads_pool = g_thread_pool_new (_egl_thread_func,
                                                        &s_player_data,
                                                        s_player_data.max_instances,
                                                        TRUE,
                                                        NULL);
    s_player_data.egl_cuda_threads_pool = g_thread_pool_new (_egl_cuda_thread_func,
                                                             &s_player_data,
                                                             s_player_data.max_instances,
                                                             TRUE,
                                                             NULL);
    return GST_NVM_RESULT_OK;
}


GstNvmResult
gst_nvm_player_egl_fini (void)
{
    g_thread_pool_free (s_player_data.egl_threads_pool, TRUE, FALSE);
    g_thread_pool_free (s_player_data.egl_cuda_threads_pool, TRUE, FALSE);
    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_player_start_egl_playback (
    GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;

    g_usleep (5000);
    ctx->egl_playback_stop_flag = FALSE;
    gst_nvm_semaphore_signal (ctx->egl_playback_start_sem);

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_player_stop_egl_playback (
    GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstState state;

    /* Bring the pipeline to paused state, so that no more buffers
       are sent by producer to consumer. */
    if (IsFailed (gst_nvm_common_query_state (ctx, &state, GST_NVM_WAIT_TIMEOUT)))
        gst_nvm_common_change_state (ctx, GST_STATE_NULL, GST_NVM_WAIT_TIMEOUT);
    else {
        if (state == GST_STATE_PLAYING)
            gst_nvm_common_change_state (ctx, GST_STATE_PAUSED, GST_NVM_WAIT_TIMEOUT);

        if (IsFailed (gst_nvm_common_query_state (ctx, &state, GST_NVM_WAIT_TIMEOUT)))
            gst_nvm_common_change_state (ctx, GST_STATE_NULL, GST_NVM_WAIT_TIMEOUT);
        if (state == GST_STATE_PAUSED)
            gst_nvm_common_change_state (ctx, GST_STATE_READY, GST_NVM_WAIT_TIMEOUT);

        if (IsFailed (gst_nvm_common_query_state (ctx, &state, GST_NVM_WAIT_TIMEOUT)))
            gst_nvm_common_change_state (ctx, GST_STATE_NULL, GST_NVM_WAIT_TIMEOUT);
        if (state == GST_STATE_READY)
            gst_nvm_common_change_state (ctx, GST_STATE_NULL, GST_NVM_WAIT_TIMEOUT);

        if (IsFailed (gst_nvm_common_query_state (ctx, &state, GST_NVM_WAIT_TIMEOUT)))
            gst_nvm_common_change_state (ctx, GST_STATE_NULL, GST_NVM_WAIT_TIMEOUT);

        if (state != GST_STATE_NULL)
            GST_ERROR ("Failed setting pipeline to NULL state while unloading media");
        else
            GST_DEBUG ("Pipeline was set to NULL state");
    }
    ctx->egl_playback_stop_flag = TRUE;
    gst_nvm_semaphore_wait (ctx->egl_playback_stop_sem); // Wait for consumer to be destroyed.

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_player_init_egl_cross_process (
    GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    int c = 0;

    if (!GrUtilInitialize (&c, NULL, "gstnvmplayer", 2, 8, 0))
        GST_ERROR ("Failed display initialization");
    if (!ctx->quit_flag)
    {
        if (!ctx->egl_initialized_flag)
        {
            while(!init_CP()){
                g_usleep(1000);
            }
            ctx->egl_initialized_flag = TRUE;
        }
     }
    ctx->eglsink_enabled_flag = TRUE;
    ctx->vsink_type = GST_NVM_EGLSTREAM_SINK;
    ctx->egl_cross_process_enabled_flag = TRUE;

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_player_start_egl_thread (
    GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;

    if (!ctx->eglsink_enabled_flag) {
        if (ctx->egl_consumer_type == GST_NVM_EGL_CONSUMER_GL_YUV ||
            ctx->egl_consumer_type == GST_NVM_EGL_CONSUMER_GL_RGB)
          g_thread_pool_push (s_player_data.egl_threads_pool, ctx, NULL);
        else if (ctx->egl_consumer_type == GST_NVM_EGL_CONSUMER_CUDA_YUV ||
                 ctx->egl_consumer_type == GST_NVM_EGL_CONSUMER_CUDA_RGB)
          g_thread_pool_push (s_player_data.egl_cuda_threads_pool, ctx, NULL);
        ctx->eglsink_enabled_flag = TRUE;
        ctx->vsink_type = GST_NVM_EGLSTREAM_SINK;
    }

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_player_stop_egl_thread (
    GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    if (ctx->eglsink_enabled_flag) {
        gst_nvm_semaphore_signal (ctx->egl_playback_start_sem);
        ctx->egl_playback_stop_flag = TRUE;
        ctx->eglsink_enabled_flag = FALSE;
        ctx->vsink_type = GST_NVM_OVERLAY_SINK;
    }

    return GST_NVM_RESULT_OK;
}
