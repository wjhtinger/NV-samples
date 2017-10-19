/*
 * Copyright (c) 2016 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "nvrm_init.h"
#include "nvrm_gpu.h"

#define pabort(fmt...)    \
do {             \
    printf(fmt);    \
    closeThermalSession();    \
    exit(2);    \
} while(0)

typedef struct {

    NvRmDeviceHandle nvRmDeviceHandle;
    NvRmGpuDevice *nvRmGpuDevice;
    NvRmGpuLib *nvRmGpuLib;
    int32_t temp_mC;
    NvRmGpuDeviceEventSession *hSession;
} NvTestThermalSession;

NvTestThermalSession ctx;

static void closeThermalSession(void)
{
    if (ctx.nvRmGpuDevice)
        NvRmGpuDeviceClose(ctx.nvRmGpuDevice);

    if (ctx.nvRmGpuLib)
        NvRmGpuLibClose(ctx.nvRmGpuLib);

    if (ctx.nvRmDeviceHandle)
        NvRmClose(ctx.nvRmDeviceHandle);

    memset(&ctx, 0, sizeof(ctx));
}

static void sigIntThermalSession(int signal)
{
    NvError status;

    printf("^C caught\n");

    if (ctx.hSession) {
        NvRmGpuDeviceEventSessionClose(ctx.hSession);
        ctx.hSession = NULL;
    }

    status = NvRmGpuDeviceThermalAlertSetLimit(ctx.nvRmGpuDevice,
                                               0x00);
    if (status != NvSuccess)
        printf("NvRmGpuDeviceThermalAlertSetLimit-failed/not_supported[%d]\n",status);

    closeThermalSession();
}

static int openThermalSession(void)
{
    NvError status;
    NVRM_GPU_DEFINE_DEVICE_OPEN_ATTR(nvRmGpuDeviceOpenAttr);
    NVRM_GPU_DEFINE_LIB_OPEN_ATTR(nvRmGpuLibOpenAttr);

    status = NvRmOpenNew(&ctx.nvRmDeviceHandle);
    if (status != NvSuccess)
        goto fail;

    ctx.nvRmGpuLib = NvRmGpuLibOpen(&nvRmGpuLibOpenAttr);
    if (!ctx.nvRmGpuLib)
        goto fail;

    status = NvRmGpuDeviceOpen(ctx.nvRmGpuLib, NVRM_GPU_DEVICE_INDEX_DEFAULT,
                        &nvRmGpuDeviceOpenAttr, &ctx.nvRmGpuDevice);
    if (status != NvSuccess)
        goto fail;

    return 0;

fail:
    closeThermalSession();

    return -1;
}

int main(int argc, char *argv[])
{
    NvError status;
    int32_t curr_temp_mC;
    const NvRmGpuDeviceEventId filterList[] = { NvRmGpuDeviceEventId_AlarmThermalAboveThreshold };
    NvRmGpuDeviceEventInfo info;
    NVRM_GPU_DEFINE_DEVICE_EVENT_SESSION_ATTR(sessionAttr);
    sessionAttr.filterList = filterList;
    sessionAttr.filterListSize = NV_ARRAY_SIZE(filterList);

    if (argc < 3)
        pabort("Please use --temp_mC option for thermal alert input configuration\n");

    argc--;
    argv++;

    if (!strcmp(argv[0], "--temp_mC")) {
        argc--;
        argv++;
        ctx.temp_mC = atoi(argv[0]);
    } else
        pabort("Please use --temp_mC option for thermal alert input configuration\n");

    if (openThermalSession())
        pabort("could not open thermal session\n");

    if (NvRmGpuDeviceEventSessionOpen(ctx.nvRmGpuDevice, &sessionAttr, &ctx.hSession))
        pabort("could not open thermal event session\n");

    status = NvRmGpuDeviceThermalAlertSetLimit(ctx.nvRmGpuDevice,
                                               ctx.temp_mC);
    if (status != NvSuccess)
        pabort("NvRmGpuDeviceThermalAlertSetLimit-failed/not_supported[%d]\n",status);

    signal(SIGINT, sigIntThermalSession);

    while(1) {
        status = NvRmGpuDeviceEventSessionRead(ctx.hSession, &info, NV_WAIT_INFINITE);

        if (status != NvSuccess)
            pabort("NvRmGpuDeviceEventSessionRead failed[%d]",status);

        if (info.eventId == NvRmGpuDeviceEventId_AlarmThermalAboveThreshold) {
            curr_temp_mC = 0;
            if (NvRmGpuDeviceGetTemperature(ctx.nvRmGpuDevice,
                    NvRmGpuDeviceTemperature_InternalSensor, &curr_temp_mC))
                pabort("NvRmGpuDeviceGetTemperature -failed\n");

            printf("Thermal alert notification received!!!!!!!-configured alert limit[%d] current temp[%d]\n",
                ctx.temp_mC, curr_temp_mC);
        } else
            printf("Unwanted/unregistered event received!!!!!!-[%d]\n", info.eventId);
    }

    if (ctx.hSession) {
        NvRmGpuDeviceEventSessionClose(ctx.hSession);
        ctx.hSession = NULL;
    }

    status = NvRmGpuDeviceThermalAlertSetLimit(ctx.nvRmGpuDevice,
                                               0x00);
    if (status != NvSuccess)
        printf("NvRmGpuDeviceThermalAlertSetLimit-failed/not_supported[%d]\n",status);

    closeThermalSession();

    exit(0);
}

