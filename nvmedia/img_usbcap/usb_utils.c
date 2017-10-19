/* Copyright (c) 2015-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "linux/videodev2.h"
#include "sys/ioctl.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "sys/stat.h"
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <limits.h>
#include "stdio.h"
#include "stdlib.h"
#include "time.h"
#include "usb_utils.h"
#include "log_utils.h"

static void UtilUsbSensorScan(const char * dirName, const char * fileName,
                              NvMediaBool *foundSensor)
{
    DIR *sys1 = NULL, *sys2 = NULL, *sys3 = NULL;
    *foundSensor = NVMEDIA_FALSE;

    sys1 = opendir(dirName);
    if (sys1 != NULL) {
        struct dirent *ep;
        // Level 0 - scan directory sysDevicePath
        ep = readdir(sys1);
        while (ep != NULL) {
            char dpath[PATH_MAX];
            snprintf(dpath, PATH_MAX, "%s/%s", dirName, ep->d_name);
            sys2 = opendir(dpath);
            if (sys2 != NULL) {
                struct dirent *ep2;
                // Level 1 - inside a sub folder of sysDevicePath
                ep2 = readdir(sys2);
                while (ep2 != NULL) {
                    char dpath2[PATH_MAX];
                    if (strstr(ep2->d_name, "video4linux")) {
                        snprintf(dpath2, PATH_MAX, "%s/%s", dpath, ep2->d_name);
                        sys3 = opendir(dpath2);
                        if (sys3 != NULL) {
                            struct dirent *ep3;
                            // Level 1 - inside a sub folder of sub folder of sysDevicePath
                            ep3 = readdir(sys3);
                            while (ep3 != NULL) {
                                char *dname3 = ep3->d_name;
                                if (strcmp(dname3, fileName) == 0) {
                                    *foundSensor = NVMEDIA_TRUE;
                                    break;
                                }
                                ep3 = readdir(sys3);
                            }
                            if (closedir(sys3))  {
                                printf("cannot close directory %s with error %s\n",
                                    dpath2, strerror(errno));
                                goto fail;
                            }
                        } else if(errno != ENOTDIR) {
                            printf("cannot open directory %s with error %s\n",
                                dpath2, strerror(errno));
                            goto fail;
                        }
                    }
                    ep2 = readdir(sys2);
                }
                if (closedir(sys2)) {
                    printf("cannot close directory %s with error %s\n", dpath, strerror(errno));
                    goto fail;
                }
            }  else if(errno != ENOTDIR) {
                printf("cannot open directory %s with error %s\n", dpath, strerror(errno));
                goto fail;
            }
            ep = readdir(sys1);
        }
        if (closedir(sys1)) {
            printf("cannot close directory %s with error %s\n", dirName, strerror(errno));
            goto fail;
        }
    }  else {
        printf("cannot open directory %s with error %s\n", dirName, strerror(errno));
        goto fail;
    }
    return;
fail:
    if (sys3)
        closedir(sys3);
    if (sys2)
        closedir(sys2);
    if (sys1)
        closedir(sys1);
}

void UtilUsbSensorFindCameras(void)
{
    unsigned int i =0;
    const char * sysDevicePath = "/sys/bus/usb/devices";
    char fileName[32];
    NvMediaBool foundSensor;

    /* scan for video0 to video9 camera nodes */
    for (i = 0; i < USB_OPEN_MAX_DEVICES; i++) {
        foundSensor = NVMEDIA_FALSE;
        sprintf(fileName, "video%d", i);
        UtilUsbSensorScan(sysDevicePath, fileName, &foundSensor);
        if(foundSensor)
            printf("                     \t/dev/%s \n", fileName);

    }
}

int UtilUsbSensorGetFirstAvailableCamera(char *camName)
{
    unsigned int i = 0;
    const char * sysDevicePath = "/sys/bus/usb/devices";
    char fileName[32];
    NvMediaBool foundSensor;

    if(!camName) {
        printf("Invalid parameter provided for camName\n");
        return -1;
    }

     /* scan for video0 to video9 camera nodes */
    for (i = 0; i < USB_OPEN_MAX_DEVICES; i++) {
        foundSensor = NVMEDIA_FALSE;
        sprintf(fileName, "video%d", i);
        UtilUsbSensorScan(sysDevicePath, fileName, &foundSensor);
        if(foundSensor) {
            sprintf(camName, "/dev/%s", fileName);
            return 0;
        }
    }

    return -1;
}

static int UtilUsbSensorDeviceIsOpen(UtilUsbSensor *sensor)
{
    return (sensor && (sensor->fdUsbCameraDevice != USB_INVALID_FD));
}

static int UtilUsbSensorOpenDevice(UtilUsbSensor *sensor)
{
    unsigned int i = 0;

    if (UtilUsbSensorDeviceIsOpen(sensor))
        return 0;

    for (i = 0; i < USB_OPEN_MAX_RETRIES; i++) {
        usleep(USB_OPEN_MAX_TIMEOUT);
        sensor->fdUsbCameraDevice = open(sensor->config->devPath, O_RDWR | O_NONBLOCK, 0);
        if (sensor->fdUsbCameraDevice != USB_INVALID_FD) {
            printf("UtilUsbSensor Device %s open is successful\n", sensor->config->devPath);
            return 0;
        }
    }
    LOG_ERR("UtilUsbSensor Device open failed \n");
    return -1;
}

static int UtilUsbSensorCloseDevice(UtilUsbSensor *sensor)
{
    if (!UtilUsbSensorDeviceIsOpen(sensor))
        return 0;

    if (close(sensor->fdUsbCameraDevice) == USB_INVALID_FD) {
        sensor->fdUsbCameraDevice = USB_INVALID_FD;
        return -1;
    }

    sensor->fdUsbCameraDevice = USB_INVALID_FD;
    return 0;
}


static int UtilUsbSensorDeinitBuffer(UtilUsbSensor *sensor)
{
    unsigned int i;
    if (sensor->isMmapBufsInit) {
        for (i = 0; i < sensor->numBufs; i++) {
            if (munmap(sensor->mmapBufs[i].start,
                       sensor->mmapBufs[i].length) == USB_INVALID_FD) {
                LOG_ERR ("Error in MunMap: %s\n",strerror(errno));
                return -1;
            }
        }
        sensor->isMmapBufsInit = false;
    }

    LOG_DBG("MUNMAP success\n");
    return 0;
}

static int UtilUsbSensorInitBuffer(UtilUsbSensor *sensor)
{
    struct v4l2_requestbuffers paramReqBuf;
    unsigned int  nbuf;

    //Don't report this error since stream might be already off
    if (UtilUsbSensorStopCapture(sensor)) {
        LOG_DBG("Ioctl Error in stream off\n");
    }

    if (UtilUsbSensorDeinitBuffer(sensor)) {
        LOG_DBG("Buffer uninitialization failed\n");
    }

    memset(&paramReqBuf, 0, sizeof(paramReqBuf));
    paramReqBuf.count      = USB_MMAP_MAX_BUFFERS;
    paramReqBuf.type       = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    paramReqBuf.memory     = V4L2_MEMORY_MMAP;

    if (ioctl(sensor->fdUsbCameraDevice,VIDIOC_REQBUFS, &paramReqBuf) < 0) {
        LOG_DBG("Error in VIDIOC_REQBUFS: %s \n",strerror(errno));
    }

    if (paramReqBuf.count < 2 && paramReqBuf.count > 0) {
        LOG_DBG("Request buffer count is very less %d\n", paramReqBuf.count);
    } else if (paramReqBuf.count <= 0) {
        LOG_DBG( "Buffer count is not valid");
    }

    sensor->numBufs = paramReqBuf.count;

    for (nbuf = 0; nbuf < paramReqBuf.count; ++nbuf) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));

        buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory      = V4L2_MEMORY_MMAP;
        buf.index       = nbuf;

        if( ioctl(sensor->fdUsbCameraDevice, VIDIOC_QUERYBUF, &buf) < 0 ) {
            LOG_ERR("Error in VIDIOC_QUERYBUF: %s \n",strerror(errno));
            return -1;
        }

        sensor->mmapBufs[nbuf].length = buf.length;
        sensor->mmapBufs[nbuf].start = mmap (NULL,
                                             buf.length,
                                             PROT_READ | PROT_WRITE,
                                             MAP_SHARED,
                                             sensor->fdUsbCameraDevice,
                                             buf.m.offset);

        if (sensor->mmapBufs[nbuf].start == NULL) {
            LOG_ERR("MMAP failure buf num %d\n",nbuf);
            return -1;
        }
    }
    sensor->isMmapBufsInit = true;
    return 0;
}


static int UtilUsbSensorInitDevice(UtilUsbSensor *sensor)
{
    struct v4l2_format paramFmt;
    unsigned char *pPixfmt;

    /* Querying the default format */
    memset(&paramFmt, 0, sizeof(paramFmt));
    paramFmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(sensor->fdUsbCameraDevice, VIDIOC_G_FMT, (void *)(&paramFmt)) < 0) {
        LOG_ERR("Error in VIDIOC_G_FMT: %s \n",strerror(errno));
        return -1;
    }

    pPixfmt = (unsigned char*)&paramFmt.fmt.pix.pixelformat;
    LOG_DBG("\nDefault Parameters: Height: %d Width: %d Pixelformat: %c%c%c%c\n",
            paramFmt.fmt.pix.height, paramFmt.fmt.pix.width,
            *pPixfmt, *(pPixfmt+1), *(pPixfmt+2), *(pPixfmt+3));

    paramFmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    paramFmt.fmt.pix.width = sensor->config->width;
    paramFmt.fmt.pix.height = sensor->config->height;
    paramFmt.fmt.pix.pixelformat = sensor->config->fmt;

    if (ioctl(sensor->fdUsbCameraDevice, VIDIOC_S_FMT, (void *)(&paramFmt)) < 0) {
        LOG_ERR("Error in VIDIOC_S_FMT: %s \n",strerror(errno));
        return -1;
    }

    printf ("Parameters set: Height: %d Width: %d Pixelformat: %c%c%c%c\n",
            paramFmt.fmt.pix.height, paramFmt.fmt.pix.width,
            *pPixfmt, *(pPixfmt+1), *(pPixfmt+2), *(pPixfmt+3));

    /* VIDIOC_S_FMT updates width and height. */
    sensor->config->width  = paramFmt.fmt.pix.width;
    sensor->config->height = paramFmt.fmt.pix.height;
    sensor->config->fmt    = paramFmt.fmt.pix.pixelformat;

    return 0;
}

static int UtilUsbWriteImage(unsigned char *pBuff, unsigned int width,
                             unsigned int height, NvMediaImage *image)
{
    unsigned int uHeightSurface, uWidthSurface, srcYUVPitch[3] , dstYUVPitch[3];
    unsigned char *pSrcY = NULL;
    unsigned char *pDstY = NULL;
    NvMediaImageSurfaceMap surfaceMap;
    unsigned int i = 0;
    int ret = -1;

    if (!image || !width  || !height || !pBuff ) {
        LOG_ERR("UtilUsbWriteImage: Bad parameters\n");
        return ret;
    }

    if (NvMediaImageLock(image, NVMEDIA_IMAGE_ACCESS_WRITE,
                        &surfaceMap) != NVMEDIA_STATUS_OK) {
        LOG_ERR("UtilUsbWriteImage: NvMediaImageLock failed\n");
        return ret;
    }

    uHeightSurface = surfaceMap.height;
    uWidthSurface  = surfaceMap.width;

    if (!image || width > uWidthSurface || height > uHeightSurface) {
        LOG_ERR("UtilUsbWriteImage: Bad parameters\n");
        goto done;
    }

    /* copy buf to NvMediaImage; currently supports only YUYV type */
    pDstY = surfaceMap.surface[0].mapping;
    pSrcY = pBuff;
    dstYUVPitch[0] = surfaceMap.surface[0].pitch;
    srcYUVPitch[0] = width*2;

    for (i = 0 ; i < height ; i++) {
        memcpy(pDstY, pSrcY, srcYUVPitch[0]);
        pDstY += dstYUVPitch[0];
        pSrcY += srcYUVPitch[0];
    }
    ret = 0;

done:
    NvMediaImageUnlock(image);
    return ret;
}

UtilUsbSensor* UtilUsbSensorInit(UtilUsbSensorConfig *config)
{
    UtilUsbSensor *sensor = NULL;

    sensor = malloc(sizeof(UtilUsbSensor));
    if (!sensor) {
        LOG_ERR("\n UtilUsbSensor allocation failed \n");
        return NULL;
    }
    memset(sensor, 0, sizeof(UtilUsbSensor));

    /*Initialize the input settings*/
    sensor->config = config;
    sensor->fdUsbCameraDevice = USB_INVALID_FD;

    if (UtilUsbSensorOpenDevice(sensor) < 0) {
        LOG_ERR("UtilUsbSensor Open failed \n");
        free (sensor);
        return NULL;
    }

    /* Initialize v4l2 driver with some ioctl calls */
    if (UtilUsbSensorInitDevice(sensor) < 0) {
        LOG_ERR("UtilUsbSensor Init failed \n");
        UtilUsbSensorCloseDevice(sensor);
        free(sensor);
        return NULL;
    }

    sensor->isMmapBufsInit = false;

    /* Using IO_METHOD_MMAP to acquire the video frames */
    if (UtilUsbSensorInitBuffer(sensor) < 0) {
        LOG_ERR("UtilUsbSensor Init buffer failed \n");
        UtilUsbSensorDeinit(sensor);
        return NULL;
    }
    return sensor;
}

int UtilUsbSensorDeinit(UtilUsbSensor *sensor)
{
    if (sensor) {
        if (UtilUsbSensorDeinitBuffer(sensor)) {
            LOG_ERR("Buffer uninitialization failed\n");
            return -1;
        }
        if (UtilUsbSensorCloseDevice(sensor)) {
            LOG_ERR("Device close failed\n");
            return -1;
        }
        free(sensor);
    }
    return 0;
}

int UtilUsbSensorGetFrame(UtilUsbSensor *sensor,
                NvMediaImage *image,unsigned int millisecondTimeout)
{
    struct pollfd fds;
    int ret = 0;

    if (!image) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return -1;
    }

    while (1) {
        memset(&fds, 0, sizeof(fds));
        fds.events = POLLIN;
        fds.fd = sensor->fdUsbCameraDevice;

        const int pollReturn = poll(&fds, 1, millisecondTimeout);

        if ((pollReturn > 0) && (fds.revents == POLLIN)) {

            /* Initialize the V4L2 buffer struct for the dequeue */
            struct v4l2_buffer buf;
            memset(&buf,0,sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;

            /* Do the V4L2 capture */
            if (ioctl(sensor->fdUsbCameraDevice, VIDIOC_DQBUF, &buf) < 0) {
                LOG_ERR( "Error in VIDIOC_DQBUF: %s \n",strerror(errno));
                return -1;
            }

            /* Copy the frame to output buffer */
            if(UtilUsbWriteImage(sensor->mmapBufs[buf.index].start,
                                 sensor->config->width, sensor->config->height,
                                 image) < 0) {
                LOG_ERR("Error in copying the capture image \n");
                ret = -1;
            }

            /* Return the V4L2 buffer */
            if (ioctl(sensor->fdUsbCameraDevice, VIDIOC_QBUF, &buf) < 0) {
                LOG_ERR("Error in VIDIOC_QBUF: %s \n",strerror(errno));
                return -1;
            }
            break;

        } else {
            if (pollReturn == 0) {
                LOG_DBG("Device busy: Retrying\n");
                continue;
            } else {
                LOG_ERR("\nError in polling device: %s \n",strerror(errno));
                return -1;
            }
        }
    }
    return ret;
}

int UtilUsbSensorStartCapture(UtilUsbSensor *sensor)
{
    unsigned int i;
    enum v4l2_buf_type paramType;

    for (i = 0; i < sensor->numBufs; ++i) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory      = V4L2_MEMORY_MMAP;
        buf.index       = i;

        if (ioctl(sensor->fdUsbCameraDevice, VIDIOC_QBUF, &buf) < 0) {
            LOG_ERR("Error in VIDIOC_QBUF: %s \n",strerror(errno));
            return -1;
        }
        LOG_DBG("VIDIOC_QBUF for buf %d success\n",i);
    }

    paramType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(sensor->fdUsbCameraDevice, VIDIOC_STREAMON,
        &paramType) < 0) {
        LOG_ERR("Error in VIDIOC_STREAMON: %s \n",strerror(errno));
        return -1;
    }
    LOG_DBG("VIDIOC_STREAMON success\n");
    return 0;
}

int UtilUsbSensorStopCapture(UtilUsbSensor *sensor)
{
    enum v4l2_buf_type paramType;
    paramType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(sensor->fdUsbCameraDevice,VIDIOC_STREAMOFF,
        &paramType) < 0) {
        LOG_ERR("Error in VIDIOC_STREAMOFF: %s \n",strerror(errno));
        return -1;
    }
    LOG_DBG( "VIDIOC_STREAMOFF success\n");
    return 0;
}
