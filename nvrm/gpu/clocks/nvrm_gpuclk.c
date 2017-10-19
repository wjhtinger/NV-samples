/*
 * Copyright (c) 2017 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <linux/types.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include <inttypes.h>
#include <libgen.h>


#include "nvrm_init.h"
#include "nvrm_gpu.h"

#define MAX_ENTRIES 16

#define pabort(fmt...)    \
do {             \
    printf(fmt);    \
    exit(2);    \
} while(0)

enum NvTestClkCmd {
    NvTestClkCmd_Usage = 0,
    NvTestClkCmd_GetDomains,
    NvTestClkCmd_GetRange,
    NvTestClkCmd_GetPoints,
    NvTestClkCmd_GetTarget,
    NvTestClkCmd_GetActual,
    NvTestClkCmd_GetEffective,
    NvTestClkCmd_SetTarget,
    NvTestClkCmd_SetPoints,
};

enum NvTestClkUnit {
    NvTestClkUnit_Hz = 1,
    NvTestClkUnit_KHz = 1000,
    NvTestClkUnit_MHz = 1000000,
};

typedef struct {
    size_t numPoints;
    NvRmGpuClockPoint *clkPoints;
} NvTestClkDomainVfPoints;

typedef struct {

    NvRmDeviceHandle nvRmDeviceHandle;
    NvRmGpuDevice *nvRmGpuDevice;
    NvRmGpuLib *nvRmGpuLib;

    size_t numDomains;
    const NvRmGpuClockDomainInfo *infos;
    NvTestClkDomainVfPoints *domainVfPoints;

    size_t numEntries;
    NvRmGpuClockGetEntry clkEntry[MAX_ENTRIES];

    int clkUnit;
    uint32_t intervalMs;
    uint32_t waitOnExitMs;

} NvTestClkSession;

static const NvRmGpuClockDomainInfo *getClkDomainInfo(NvTestClkSession *ctx, NvRmGpuClockDomain domain)
{
    uint32_t i;

    for (i = 0; i < ctx->numDomains; i++) {
        if (ctx->infos[i].domain == domain)
            return &ctx->infos[i];
    }
    return NULL;
}

static NvTestClkDomainVfPoints *getClkDomainVfPoints(NvTestClkSession *ctx, NvRmGpuClockDomain domain)
{
    uint32_t i;

    for (i = 0; i < ctx->numDomains; i++) {
        if (ctx->infos[i].domain == domain)
            return &ctx->domainVfPoints[i];
    }
    return NULL;
}

static void closeClkSession(NvTestClkSession *ctx);

static int openClkSession(NvTestClkSession *ctx)
{
    NvError status;
    NVRM_GPU_DEFINE_DEVICE_OPEN_ATTR(nvRmGpuDeviceOpenAttr);
    NVRM_GPU_DEFINE_LIB_OPEN_ATTR(nvRmGpuLibOpenAttr);

    status = NvRmOpenNew(&ctx->nvRmDeviceHandle);
    if (status != NvSuccess)
        goto fail;

    ctx->nvRmGpuLib = NvRmGpuLibOpen(&nvRmGpuLibOpenAttr);
    if (!ctx->nvRmGpuLib)
        goto fail;

    status = NvRmGpuDeviceOpen(ctx->nvRmGpuLib, NVRM_GPU_DEVICE_INDEX_DEFAULT,
                        &nvRmGpuDeviceOpenAttr, &ctx->nvRmGpuDevice);
    if (status != NvSuccess)
        goto fail;

    return 0;

fail:
    closeClkSession(ctx);

    return -1;
}

static void closeClkSession(NvTestClkSession *ctx)
{
    if (ctx->nvRmGpuDevice)
        NvRmGpuDeviceClose(ctx->nvRmGpuDevice);

    if (ctx->nvRmGpuLib)
        NvRmGpuLibClose(ctx->nvRmGpuLib);

    if (ctx->nvRmDeviceHandle)
        NvRmClose(ctx->nvRmDeviceHandle);

    memset(ctx, 0, sizeof(*ctx));
}

static const char *clkDomainName(NvRmGpuClockDomain api_domain)
{
    switch (api_domain)
    {
        case NvRmGpuClockDomain_MCLK:
            return "MCLK";

        case NvRmGpuClockDomain_GPCCLK:
            return "GPCCLK";

        default:
            return "?";
    }
}

static const char *clkUnitName(int unit)
{
    switch (unit) {
    case NvTestClkUnit_Hz:
        return "Hz";
    case NvTestClkUnit_KHz:
        return "KHz";
    case NvTestClkUnit_MHz:
        return "MHz";
    default:
        return "?";
    }
}


static const char *clkTypeName(NvRmGpuClockType type)
{
    switch (type) {
    case NvRmGpuClockType_Target:
        return "Target";
    case NvRmGpuClockType_Actual:
        return "Actual";
    case NvRmGpuClockType_Effective:
        return "Effective";
    default:
        return "?";
    }
}

static const char *eventName(NvRmGpuDeviceEventId eventId)
{
    switch (eventId)
    {
#define C(x) case NvRmGpuDeviceEventId_##x: return #x

        C(VfUpdate);
        C(AlarmTargetVfNotPossible);
        C(AlarmLocalTargetVfNotPossible);
        C(AlarmClockArbiterFailed);
        C(AlarmVfTableUpdateFailed);
        C(AlarmThermalAboveThreshold);
        C(AlarmPowerAboveThreshold);
        C(AlarmGpuLost);

#undef C
    default:
        return "???";
    }
}

static void showDomains(NvTestClkSession *ctx)
{
    uint32_t i;

    for (i = 0; i < ctx->numDomains; i++) {
        printf("%s\n", clkDomainName(ctx->infos[i].domain));
    }
}

static void showClkInfo(NvTestClkSession *ctx)
{
    uint32_t i;

    for (i = 0; i < ctx->numEntries; i++) {

        printf("%s %s %"PRIu64" %s\n",
                clkTypeName(ctx->clkEntry[i].type),
                clkDomainName(ctx->clkEntry[i].domain),
                ctx->clkEntry[i].freqHz / (uint64_t)ctx->clkUnit,
                clkUnitName(ctx->clkUnit));
    }
}

static void showDomainClkPoints(NvTestClkSession *ctx, NvTestClkDomainVfPoints *vfPoints)
{
    uint32_t i;

    for (i = 0; i < vfPoints->numPoints; i++) {
        printf("\t%"PRIu64" %s\n",
                vfPoints->clkPoints[i].freqHz / (uint64_t)ctx->clkUnit,
                clkUnitName(ctx->clkUnit));
    }
}

static void showClkPoints(NvTestClkSession *ctx)
{
    uint32_t i;
    NvTestClkDomainVfPoints *vfPoints;
    NvRmGpuClockDomain domain;

    for (i = 0; i < ctx->numEntries; i++) {

    	domain = ctx->clkEntry[i].domain;

        printf("%s\n", clkDomainName(domain));

        vfPoints = getClkDomainVfPoints(ctx, domain);
        showDomainClkPoints(ctx, vfPoints);
    }
}

static int getClkDomains(NvTestClkSession *ctx)
{
    NvError status;

    status = NvRmGpuClockGetDomains(ctx->nvRmGpuDevice, &ctx->infos, &ctx->numDomains);
    if (status != NvSuccess)
        return -1;

    ctx->domainVfPoints = (NvTestClkDomainVfPoints *)
            calloc(ctx->numDomains, sizeof(NvTestClkDomainVfPoints));
    if (!ctx->domainVfPoints) {
        ctx->numDomains = 0;
        return -1;
    }

    return 0;
}

static int getClkRange(NvTestClkSession *ctx)
{
    uint32_t i;
    const NvRmGpuClockDomainInfo *info;

    for (i = 0; i < ctx->numEntries; i++) {

        info = getClkDomainInfo(ctx, ctx->clkEntry[i].domain);
        if (!info)
            return -1;

        printf("%s %"PRIu64" %"PRIu64" %s\n",
                clkDomainName(info->domain),
                info->range.minHz / (uint64_t)ctx->clkUnit,
                info->range.maxHz / (uint64_t)ctx->clkUnit,
                clkUnitName(ctx->clkUnit));
    }
    return 0;
}

static int getClkPoints(NvTestClkSession *ctx)
{
    NvError status;
    uint32_t i;
    NvTestClkDomainVfPoints *vfPoints;
    const NvRmGpuClockDomainInfo *info;

    for (i = 0; i < ctx->numEntries; i++) {

        vfPoints = getClkDomainVfPoints(ctx, ctx->clkEntry[i].domain);
        info = getClkDomainInfo(ctx, ctx->clkEntry[i].domain);
        if (!info || !vfPoints)
            return -1;

        if (!vfPoints->clkPoints) {
            vfPoints->clkPoints = (NvRmGpuClockPoint *)
                        calloc(info->maxVfPoints, sizeof(NvRmGpuClockPoint));
            if (!vfPoints->clkPoints)
                return -1;
        }

        status = NvRmGpuClockGetPoints(ctx->nvRmGpuDevice,
                info->domain, vfPoints->clkPoints, &vfPoints->numPoints);
        if (status != NvSuccess)
        {
            free(vfPoints->clkPoints);
            vfPoints->clkPoints = NULL;
            return -1;
        }
    }

    return 0;
}

static int getClk(NvTestClkSession *ctx, NvRmGpuClockType type)
{
    uint32_t i;
    int err;

    for (i = 0; i < ctx->numEntries; i++) {
        ctx->clkEntry[i].type = type;
        ctx->clkEntry[i].freqHz = 0;
    }

    err = NvRmGpuClockGet(ctx->nvRmGpuDevice, ctx->clkEntry, ctx->numEntries);
    if (err != NvSuccess)
        return -1;

    showClkInfo(ctx);
    return 0;
}

static int setClk(NvTestClkSession *ctx)
{
    NvRmGpuClockSetEntry *clkSetEntries;
    uint32_t i;
    int err;

    clkSetEntries = (NvRmGpuClockSetEntry *)
            calloc(ctx->numEntries, sizeof(NvRmGpuClockSetEntry));
    if (!clkSetEntries)
        return -1;

    for (i = 0; i < ctx->numEntries; i++) {
        clkSetEntries[i].domain = ctx->clkEntry[i].domain;
        clkSetEntries[i].freqHz = ctx->clkEntry[i].freqHz;
    }

    err = NvRmGpuClockSet(ctx->nvRmGpuDevice, clkSetEntries, ctx->numEntries, NULL);

    free(clkSetEntries);
    return err;
}

static int setClkPoint(NvTestClkSession *ctx, const NvRmGpuClockDomainInfo *info,
        NvTestClkDomainVfPoints *vfPoints, uint32_t pointIdx)
{
    NvRmGpuClockSetEntry clkSetEntry;
    NvRmGpuClockGetEntry clkGetEntry;
    struct timespec req;

    NvError status;

    printf("\tsetting %s %s = %"PRIu64" %s\n",
            clkTypeName(NvRmGpuClockType_Target),
            clkDomainName(info->domain),
            vfPoints->clkPoints[pointIdx].freqHz / ctx->clkUnit,
            clkUnitName(ctx->clkUnit));

    memset(&clkSetEntry, 0, sizeof(clkSetEntry));
    clkSetEntry.domain = info->domain;
    clkSetEntry.freqHz = vfPoints->clkPoints[pointIdx].freqHz;
    status = NvRmGpuClockSet(ctx->nvRmGpuDevice, &clkSetEntry, 1, NULL);
    if (status != NvSuccess)
    return -1;

    memset(&clkGetEntry, 0, sizeof(clkGetEntry));
    clkGetEntry.domain = info->domain;
    clkGetEntry.type = NvRmGpuClockType_Actual;
    status = NvRmGpuClockGet(ctx->nvRmGpuDevice, &clkGetEntry, 1);
    if (status != NvSuccess)
        return -1;

    if (ctx->intervalMs) {
        req.tv_nsec = (ctx->intervalMs % 1000) * 1000000LL;
        req.tv_sec = (ctx->intervalMs / 1000);
        nanosleep(&req, NULL);
    }

    return 0;
}

static int setClkDomainPoints(NvTestClkSession *ctx,
        int entryIdx, int step_entry, int step_points)
{
    const NvRmGpuClockDomainInfo *info;
    NvTestClkDomainVfPoints *vfPoints;
    uint32_t pointIdx;
    size_t numPoints;

    if ((entryIdx < 0) || (entryIdx >= (int)ctx->numEntries))
        return 0;

    info = getClkDomainInfo(ctx, ctx->clkEntry[entryIdx].domain);
    vfPoints = getClkDomainVfPoints(ctx, ctx->clkEntry[entryIdx].domain);
    if (!info || !vfPoints)
        return -1;

    numPoints = vfPoints->numPoints;
    pointIdx = (step_points > 0) ? 0 : (vfPoints->numPoints - 1);
    while (numPoints--) {
        if (setClkPoint(ctx, info, vfPoints, pointIdx) < 0)
            return -1;

        if (setClkDomainPoints(ctx, entryIdx + step_entry,
                step_entry, step_points) < 0)
            return -1;

        pointIdx += step_points;
    }

    return 0;
}

static int setClkPoints(NvTestClkSession *ctx)
{
    int step_point, step_entry;

    if (getClkPoints(ctx) < 0)
        return -1;

    for (step_entry = 1; step_entry >= -1; step_entry -= 2) {

        for (step_point = 1; step_point >= -1; step_point -= 2) {

            if (setClkDomainPoints(ctx,
                    step_entry < 0 ? ctx->numEntries - 1 : 0,
                    step_entry, step_point) < 0)
                return -1;
        }

        if (ctx->numEntries < 2)
            break;
    }

    return 0;
}

static int parse_domain(const char *name)
{
    if (!strcasecmp(name, "mclk"))
        return NvRmGpuClockDomain_MCLK;

    if (!strcasecmp(name, "gpcclk"))
        return NvRmGpuClockDomain_GPCCLK;

    return -1;
}

int main(int argc, char *argv[])
{
    int c;
    int command = NvTestClkCmd_Usage;
    uint32_t msecs = NV_WAIT_INFINITE;
    uint32_t duration;
    bool monitor = false;
    NvTestClkSession ctx[1];
    int err;
    uint32_t i;
    char *cmd;
    struct timespec req;
    long int t;
    NvRmGpuDeviceEventSession *hSession = NULL;
    NvRmGpuDeviceEventInfo info;
    NVRM_GPU_DEFINE_DEVICE_EVENT_SESSION_ATTR(sessionAttr);

    NvRmGpuDeviceEventSessionOpenAttrSetAllEvents(&sessionAttr);

    opterr = 0;

    memset(ctx, 0, sizeof(ctx));
    ctx->clkUnit = NvTestClkUnit_Hz;
    ctx->numEntries = 0;

    while ((c = getopt(argc, argv, "t:hkmi:w:")) != -1) {
        switch (c) {
        case 't':
            msecs = strtol(optarg, NULL, 0);
            break;
        case 'k':
            ctx->clkUnit = NvTestClkUnit_KHz;
            break;
        case 'm':
            ctx->clkUnit = NvTestClkUnit_MHz;
            break;
        case 'i':
            ctx->intervalMs = strtol(optarg, NULL, 0);
            break;
        case 'w':
            t = strtol(optarg, NULL, 0);
            if (t < 0)
                ctx->waitOnExitMs = NV_WAIT_INFINITE;
            else
                ctx->waitOnExitMs = t;
            break;
        case 'h':
        default:
            goto usage;
        }
    }

    if (optind == argc)
        goto usage;

    if (!strcasecmp(argv[optind], "monitor")) {
        command = NvTestClkCmd_GetActual;
        optind++;
        monitor = true;
        goto parsed;
    }

    if (!strcasecmp(argv[optind], "get")) {
        if (++optind >= argc)
            goto usage;
        if (!strcasecmp(argv[optind], "domains")) {
            command = NvTestClkCmd_GetDomains;
            optind++;
            goto parsed;
        }
        if (!strcasecmp(argv[optind], "range")) {
            command = NvTestClkCmd_GetRange;
            optind++;
            goto parse_domains;
        }
        if (!strcasecmp(argv[optind], "points")) {
            command = NvTestClkCmd_GetPoints;
            optind++;
            goto parse_domains;
        }
        if (!strcasecmp(argv[optind], "target")) {
            command = NvTestClkCmd_GetTarget;
            optind++;
            goto parse_domains;
        }
        if (!strcasecmp(argv[optind], "actual")) {
            command = NvTestClkCmd_GetActual;
            optind++;
            goto parse_domains;
        }
        if (!strcasecmp(argv[optind], "effective")) {
            command = NvTestClkCmd_GetEffective;
            optind++;
            goto parse_domains;
        }
        goto usage;

parse_domains:
        while ((optind < argc) && (ctx->numEntries < MAX_ENTRIES)) {
            err = parse_domain(argv[optind]);
            if (err < 0)
                goto usage;
            ctx->clkEntry[ctx->numEntries].domain = err;
            ctx->numEntries++;
            optind++;
        }
        goto parsed;
    }

    if (!strcasecmp(argv[optind], "set")) {
        if (++optind >= argc)
            goto usage;

        if (!strcasecmp(argv[optind], "target")) {
            command = NvTestClkCmd_SetTarget;
            optind++;

            while (((optind + 1) < argc) && (ctx->numEntries < MAX_ENTRIES)) {
                err = parse_domain(argv[optind]);
                if (err < 0)
                    goto usage;
                ctx->clkEntry[ctx->numEntries].domain = err;
                optind++;

                ctx->clkEntry[ctx->numEntries].freqHz =
                        strtoull(argv[optind], NULL, 0) * (uint64_t)ctx->clkUnit;

                optind++;
                ctx->numEntries++;
            }
            goto parsed;
        }

        if (!strcasecmp(argv[optind], "points")) {
            command = NvTestClkCmd_SetPoints;
            optind++;
            goto parse_domains;
        }

        goto usage;
    }

parsed:
    if (command == NvTestClkCmd_Usage)
        goto usage;

    if (optind < argc)
        goto usage;

    if (openClkSession(ctx))
        pabort("could not open clk session\n");

    if (getClkDomains(ctx))
        pabort("could not get clock domains\n");

    /* if not specific domain was provided, apply command to all domains */
    if (!ctx->numEntries) {
        ctx->numEntries = ctx->numDomains;
        for (i = 0; i < ctx->numEntries; i++) {
            ctx->clkEntry[i].domain = ctx->infos[i].domain;
        }
    }

    if (monitor) {
        err = NvRmGpuDeviceEventSessionOpen(ctx->nvRmGpuDevice, &sessionAttr, &hSession);
        if (err)
            pabort("could not open event session");
    }

    err = 0;
    duration = 0;
    do {

        if (monitor) {
 wait:
            err = NvRmGpuDeviceEventSessionRead(hSession, &info, msecs);
            if (err == NvError_Timeout) {
                duration += msecs;
                if (duration >= 10000) {
                    printf("waited %u msecs\n", duration);
                        duration = 0;
                }
                goto wait;
            }

            if (err != NvSuccess)
                pabort("wait clock events failed");

            printf("CPU timestamp=%" PRIu64 " (ns) event=%s (%u)\n",
                    info.timeNs, eventName(info.eventId), info.eventId);

        }

        switch (command) {
        case NvTestClkCmd_GetDomains:
            showDomains(ctx);
            break;
        case NvTestClkCmd_GetRange:
            err = getClkRange(ctx);
            break;
        case NvTestClkCmd_GetPoints:
            err = getClkPoints(ctx);
            if (!err)
                showClkPoints(ctx);
            break;
        case NvTestClkCmd_GetTarget:
            err = getClk(ctx, NvRmGpuClockType_Target);
            break;
        case NvTestClkCmd_GetActual:
            err = getClk(ctx, NvRmGpuClockType_Actual);
            break;
        case NvTestClkCmd_GetEffective:
            err = getClk(ctx, NvRmGpuClockType_Effective);
            break;
        case NvTestClkCmd_SetTarget:
            err = setClk(ctx);
            break;
        case NvTestClkCmd_SetPoints:
            err = setClkPoints(ctx);
            break;

        default:
            pabort("unexpected command %d\n", command);
        }
    }
    while (monitor);

    if (err)
        pabort("command failed");

    if (ctx->waitOnExitMs == NV_WAIT_INFINITE) {
        while (1)
            pause();
    }

    if (ctx->waitOnExitMs) {
        req.tv_nsec = (ctx->waitOnExitMs % 1000) * 1000000LL;
        req.tv_sec = (ctx->waitOnExitMs / 1000);
        nanosleep(&req, NULL);
    }

    closeClkSession(ctx);

    exit(0);

usage:
    cmd = basename(argv[0]);
    printf("usage:\n");
    printf("\t%s monitor\n", cmd);
    printf("\t%s get domains\n", cmd);
    printf("\t%s get range [<domains>]\n", cmd);
    printf("\t%s get points [<domains>]\n", cmd);
    printf("\t%s get target [<domains>]\n", cmd);
    printf("\t%s get actual [<domains>]\n", cmd);
    printf("\t%s get effective [<domains>]\n", cmd);
    printf("\t%s set target {<domain> <frequency>}*\n", cmd);
    printf("\t%s set points [<domains>]\n", cmd);
    printf("options\n");
    printf("    -t <msecs>  timeout for wait event\n");
    printf("    -k          set default clock unit to KHz\n");
    printf("    -m          set default clock unit to MHz\n");
    printf("    -i <msecs>  wait time after setting a clock\n");
    printf("    -w <msecs>  wait time before exiting, -1 for infinite\n");
    printf("examples\n");
    printf("\t%s get range mclk\n", cmd);
    printf("\t%s set target gpcclk 1250 -m -w 500\n", cmd);
    printf("\t%s set target mclk 808 gpcclk 1000 -m -w -1\n", cmd);
    printf("\t%s set points mclk -i 100\n", cmd);
    printf("\t%s set points gpcclk -i 100\n", cmd);
    printf("\t%s set points mclk gpcclk -i 100\n", cmd);

    exit(1);
}

