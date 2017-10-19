/*
 * cuda_consumer.c
 *
 * Copyright (c) 2014, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// DESCRIPTION: CUDA consumer for gst-nvmedia-player
//

#include "cuda_consumer.h"

#if defined(EXTENSION_LIST)
EXTENSION_LIST(EXTLST_DECL)
typedef void (*extlst_fnptr_t)(void);
static struct {
    extlst_fnptr_t *fnptr;
    char const *name;
} extensionList[] = { EXTENSION_LIST(EXTLST_ENTRY) };

int EGLSetupExtensions (void)
{
    unsigned int i;

    for (i = 0; i < (sizeof(extensionList) / sizeof(*extensionList)); i++) {
        *extensionList[i].fnptr = eglGetProcAddress (extensionList[i].name);
        if (*extensionList[i].fnptr == NULL) {
            g_print ("Couldn't get address of %s()\n", extensionList[i].name);
            return 0;
        }
    }

    return 1;
}

static GLint acquireTimeout = 16000;
static GLboolean fifo_mode = GL_FALSE;

int EGLStreamInit (void)
{
    static const EGLint streamAttrMailboxMode[] = { EGL_NONE };
    static const EGLint streamAttrFIFOMode[] = { EGL_STREAM_FIFO_LENGTH_KHR, 4, EGL_NONE };
    EGLint fifo_length = 0, latency = 0, timeout = 0;

    if (!EGLSetupExtensions())
        return 0;

    eglStream = eglCreateStreamKHR (grUtilState.display,
        fifo_mode ? streamAttrFIFOMode : streamAttrMailboxMode);
    if (eglStream == EGL_NO_STREAM_KHR) {
        g_print ("Couldn't create eglStream.\n");
        return 0;
    }

    // Set stream attribute
    if (!eglStreamAttribKHR (grUtilState.display, eglStream, EGL_CONSUMER_LATENCY_USEC_KHR, 16000)) {
        g_print ("Consumer: eglStreamAttribKHR EGL_CONSUMER_LATENCY_USEC_KHR failed\n");
    }
    if (!eglStreamAttribKHR (grUtilState.display, eglStream, EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR, acquireTimeout)) {
        g_print ("Consumer: eglStreamAttribKHR EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR failed\n");
    }

    // Get stream attributes
    if (!eglQueryStreamKHR (grUtilState.display, eglStream, EGL_STREAM_FIFO_LENGTH_KHR, &fifo_length)) {
        g_print ("Consumer: eglQueryStreamKHR EGL_STREAM_FIFO_LENGTH_KHR failed\n");
    }
    if (!eglQueryStreamKHR (grUtilState.display, eglStream, EGL_CONSUMER_LATENCY_USEC_KHR, &latency)) {
        g_print ("Consumer: eglQueryStreamKHR EGL_CONSUMER_LATENCY_USEC_KHR failed\n");
    }
    if (!eglQueryStreamKHR (grUtilState.display, eglStream, EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR, &timeout)) {
        g_print ("Consumer: eglQueryStreamKHR EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR failed\n");
    }

    if (fifo_mode != (fifo_length > 0)) {
        g_print ("EGL Stream consumer - Unable to set FIFO mode\n");
        fifo_mode = GL_FALSE;
    }
    if (fifo_mode)
        g_print ("EGL Stream consumer - Mode: FIFO Length: %d\n",  fifo_length);
    else
        g_print ("EGL Stream consumer - Mode: Mailbox\n");
    g_print ("EGL Stream consumer - Latency: %d usec\n", latency);
    g_print ("EGL Stream consumer - Timeout: %d usec\n", timeout);

    return 1;
}

void EGLStreamFini (void)
{
    eglDestroyStreamKHR (grUtilState.display, eglStream);
}
#endif

//Cuda consumer init
static int *cudaDeviceCreate (test_cuda_consumer_s *cudaConsumer)
{
   CUdevice device;
   CUcontext oldContext;
   static int _initialized = 0;

   if (!_initialized) {
      if (cuInit(0)) {
         g_print ("failed to initialize Cuda\n");
         return NULL;
      }
      _initialized = 1;
   }

   if (cuDeviceGet (&device, 0)) {
      g_print ("failed to get Cuda device\n");
      return NULL;
   }

   if (cuCtxCreate (&cudaConsumer->context, 0, device)) {
      g_print ("failed to create Cuda context\n");
      return NULL;
   }

   cuCtxPopCurrent (&oldContext);
   return NULL;
}

void *cudaConsumerProc (test_cuda_consumer_s *cudaConsumer)
{
    CUcontext oldContext;
    CUresult cuStatus;
    CUgraphicsResource cudaResource;
    CUarray cudaArr = NULL;
    CUeglFrame cudaEgl;

    if (!cudaConsumer) {
        g_print ("%s: Bad parameter\n", __func__);
        cudaConsumer->quit = TRUE;
        goto done;
    }

    EGLint streamState = 0;
    if (!eglQueryStreamKHR (grUtilState.display, eglStream, EGL_STREAM_STATE_KHR,
                            &streamState)) {
        g_print ("Cuda consumer, eglQueryStreamKHR EGL_STREAM_STATE_KHR failed\n");
        cudaConsumer->quit = TRUE;
        goto done;
    }

    if (streamState == EGL_STREAM_STATE_DISCONNECTED_KHR) {
        g_print ("CUDA Consumer: - EGL_STREAM_STATE_DISCONNECTED_KHR received\n");
        cudaConsumer->quit = TRUE;
        goto done;
    }

    if (streamState != EGL_STREAM_STATE_NEW_FRAME_AVAILABLE_KHR) {
        cudaConsumer->quit = TRUE;
        goto done;
    }

    cuCtxPushCurrent (cudaConsumer->context);
    cuStatus = cuEGLStreamConsumerAcquireFrame (&(cudaConsumer->cudaConn),
                                                &cudaResource, NULL, acquireTimeout);
    cuCtxPopCurrent (&oldContext);
    g_print ("Cuda Consumer Acquire Frame\n");

    if (cuStatus == CUDA_SUCCESS) {
        CUdeviceptr pDevPtr = NULL;
        int bufferSize;
        unsigned char *pCudaCopyMem = NULL;
        unsigned int i, copyWidthInBytes = 0, copyHeight = 0;

        if (cudaConsumer->frameCount == 1) {
            //CUDA memcpy
            g_print ("Cuda Consumer Copy Frame\n");
            cuStatus = cuGraphicsResourceGetMappedEglFrame (&cudaEgl, cudaResource, 0, 0);
            for ( i = 0; i < cudaEgl.planeCount; i++)
            {
                cuCtxPushCurrent (cudaConsumer->context);
                if (cudaConsumer->pitchLinearOutput) {
                  pDevPtr = (CUdeviceptr ) cudaEgl.frame.pPitch[i];

                  if (cudaEgl.eglColorFormat == CU_EGL_COLOR_FORMAT_RGBA) {
                    bufferSize = cudaEgl.pitch * cudaEgl.height;
                    copyWidthInBytes = cudaEgl.pitch;
                    copyHeight = cudaEgl.height;
                  } else if (i == 1 && cudaEgl.eglColorFormat == CU_EGL_COLOR_FORMAT_YUV420_SEMIPLANAR) { //YUV 420 semi-planar
                    bufferSize = cudaEgl.pitch * cudaEgl.height / 2;
                    copyWidthInBytes = cudaEgl.pitch;
                    copyHeight = cudaEgl.height / 2;
                  } else {
                    bufferSize = cudaEgl.pitch * cudaEgl.height;
                    copyWidthInBytes = cudaEgl.pitch;
                    copyHeight = cudaEgl.height;
                    if (i > 0) {
                      bufferSize >>= 2;
                      copyWidthInBytes >>= 1;
                      copyHeight >>= 1;
                    }
                  }
                } else {
                  cudaArr = cudaEgl.frame.pArray[i];
                  if (cudaEgl.eglColorFormat == CU_EGL_COLOR_FORMAT_RGBA ) {
                    bufferSize = cudaEgl.width * cudaEgl.height * 4;
                    copyWidthInBytes = cudaEgl.width * 4;
                    copyHeight = cudaEgl.height;
                  } else if (i == 1 && cudaEgl.eglColorFormat == CU_EGL_COLOR_FORMAT_YUV420_SEMIPLANAR) { //YUV 420 semi-planar
                    bufferSize = cudaEgl.width * cudaEgl.height / 2;
                    copyWidthInBytes = cudaEgl.width;
                    copyHeight = cudaEgl.height / 2;
                  } else {
                    bufferSize = cudaEgl.width * cudaEgl.height;
                    copyWidthInBytes = cudaEgl.width;
                    copyHeight = cudaEgl.height;
                    if (i > 0) {
                      bufferSize >>= 2;
                      copyWidthInBytes >>= 1;
                      copyHeight >>= 1;
                    }
                  }
                }
                cuCtxPopCurrent (&oldContext);
                if (cuStatus != CUDA_SUCCESS) {
                  g_print ("Cuda get resource failed with %d\n", cuStatus);
                  cudaConsumer->quit = TRUE;
                  goto done;
                }

                pCudaCopyMem = (unsigned char *) malloc (bufferSize);
                if (pCudaCopyMem == NULL) {
                  g_print ("pCudaCopyMem malloc failed\n");
                  cudaConsumer->quit = TRUE;
                  goto done;
                }
                memset (pCudaCopyMem, 0, bufferSize);

               if (cudaConsumer->pitchLinearOutput) {
                 cuCtxPushCurrent (cudaConsumer->context);
                 cuStatus = cuMemcpyDtoH (pCudaCopyMem, pDevPtr, bufferSize);
                 if (cuStatus != CUDA_SUCCESS) {
                   g_print ("Cuda memcpy failed. \n");
                   cudaConsumer->quit = TRUE;
                   free (pCudaCopyMem);
                   goto done;
                 }
                 cuStatus = cuCtxSynchronize ();
                 if (cuStatus != CUDA_SUCCESS) {
                   g_print ("cuCtxSynchronize failed after memcpy\n");
                   cudaConsumer->quit = TRUE;
                   free (pCudaCopyMem);
                   goto done;
                 }
                 cuCtxPopCurrent(&oldContext);
               } else {
                 CUDA_MEMCPY3D cpdesc;
                 cuCtxPushCurrent (cudaConsumer->context);
                 cuStatus = cuCtxSynchronize ();
                 if (cuStatus != CUDA_SUCCESS) {
                   g_print ("cuCtxSynchronize failed \n");
                   cudaConsumer->quit = TRUE;
                   free (pCudaCopyMem);
                   goto done;
                 }

                 memset (&cpdesc, 0, sizeof(cpdesc));
                 cpdesc.srcXInBytes = cpdesc.srcY = cpdesc.srcZ = cpdesc.srcLOD = 0;
                 cpdesc.srcMemoryType = CU_MEMORYTYPE_ARRAY;
                 cpdesc.srcArray = cudaArr;
                 cpdesc.dstXInBytes = cpdesc.dstY = cpdesc.dstZ = cpdesc.dstLOD = 0;
                 cpdesc.dstMemoryType = CU_MEMORYTYPE_HOST;
                 cpdesc.dstHost = (void *)pCudaCopyMem;

                 cpdesc.WidthInBytes = copyWidthInBytes;
                 cpdesc.Height = copyHeight;
                 cpdesc.Depth = 1;

                 cuStatus = cuMemcpy3D (&cpdesc);
                 if (cuStatus != CUDA_SUCCESS) {
                   g_print ("Cuda consumer: cuMemCpy3D failed, copyWidthInBytes=%d, copyHeight=%d\n", copyWidthInBytes, copyHeight);
                   cudaConsumer->quit = TRUE;
                   free (pCudaCopyMem);
                   goto done;
                 }
                 cuStatus = cuCtxSynchronize ();
                 if (cuStatus != CUDA_SUCCESS) {
                   g_print ("cuCtxSynchronize failed after memcpy \n");
                   cudaConsumer->quit = TRUE;
                   free (pCudaCopyMem);
                   goto done;
                 }
                 cuCtxPopCurrent(&oldContext);
               }

               // write the first frame to output file
               if (fwrite (pCudaCopyMem, bufferSize, 1, cudaConsumer->outFile) != 1) {
                  g_print ("Cuda Consumer: output file write failed\n");
                  cudaConsumer->quit = TRUE;
                  free (pCudaCopyMem);
                  goto done;
               }
               free (pCudaCopyMem);
            }
        }
        cudaConsumer->frameCount++;
        g_print ("Cuda Consumer Release Frame %d\n", cudaConsumer->frameCount);
        cuCtxPushCurrent (cudaConsumer->context);
        cuStatus = cuEGLStreamConsumerReleaseFrame (&cudaConsumer->cudaConn,
                                                    cudaResource, NULL);
        cuCtxPopCurrent (&oldContext);
    } else {
            g_print("Cuda Acquire failed cuStatus=%d\n", cuStatus);
    }

done:
    return NULL;
}

gboolean cuda_consumer_init (test_cuda_consumer_s *cudaConsumer, gboolean cuda_yuv_flag)
{
    CUresult curesult;
    CUcontext oldContext;
    memset (cudaConsumer, 0, sizeof(test_cuda_consumer_s));
    cudaDeviceCreate (cudaConsumer);
    if (!cuda_yuv_flag)
      cudaConsumer->pitchLinearOutput = TRUE;

    cuCtxPushCurrent (cudaConsumer->context);
    if (CUDA_SUCCESS !=
        (curesult = cuEGLStreamConsumerConnect (&cudaConsumer->cudaConn, eglStream))) {
        g_print ("Connect CUDA consumer ERROR %d\n", curesult);
        return FALSE;
    }
    cuCtxPopCurrent (&oldContext);

    if (cudaConsumer->pitchLinearOutput)
        cudaConsumer->outFile = fopen ("cuda.rgb", "wb");
    else
        cudaConsumer->outFile = fopen ("cuda.yuv", "wb");
    if (!cudaConsumer->outFile) {
        g_print ("WriteFrame: file open failed\n");
        perror (NULL);
        return FALSE;
    }
    return TRUE;
}

void cuda_consumer_deinit (test_cuda_consumer_s *cudaConsumer)
{
    g_print ("cuda_consumer_Deinit\n");
    cudaConsumer->quit = TRUE;
    cuCtxDestroy (cudaConsumer->context);

    if (cudaConsumer->outFile) {
        fclose (cudaConsumer->outFile);
    }
    return;
}

