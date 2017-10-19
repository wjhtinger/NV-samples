/*
 * Copyright (c) 2016 NVIDIA Corporation.  All rights reserved.
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

#include "nvrm_gpusched.h"

enum {
    SCHED_USAGE = 0,
    SCHED_GET_TSGS,
    SCHED_GET_PARAMS,
    SCHED_SET_TIMESLICE,
    SCHED_SET_INTERLEAVE,
};

#define pabort(fmt...)    \
do {             \
    printf(fmt);    \
    exit(2);    \
} while(0)

static int get_params(NvRmGpuSchedTsg *hTsg)
{
    NvRmGpuSchedTsgParams params[1];
    int err;

    err = NvRmGpuSchedGetTsgParams(hTsg, params);
    if (err)
        return err;

    printf("tsgid=%u\n", params->tsgid);
    printf("\ttimeslice=%u\n", params->timesliceUs);
    printf("\trunlist_interleave=%u\n", params->interleave);
    printf("\tpid=%d\n", (pid_t)params->pid);
    return 0;
}

int main(int argc, char *argv[])
{
    int c;
    int use_pid = 0;
    int use_lock = 0;
    int wait_tsgs = 0;
    pid_t pid = 0;
    int command;
    int done = 0;
    uint32_t interleave = 0;
    uint32_t timeslice = 0;
    uint32_t duration = 0;
    uint32_t msecs = NVRM_GPUSCHED_WAIT_INFINITE;

    NvRmGpuSchedStatus status;
    NvRmGpuSchedSession *hSession = NULL;
    NvRmGpuSchedTsg    *hTsg = NULL;
    NvRmGpuSchedTsgSet *hTsgSet = NULL;
    NvRmGpuSchedTsgId tsgid = 0;

    NVRM_GPUSCHED_DEFINE_SESSION_ATTR(sessionAttr);

    opterr = 0;

    while ((c = getopt(argc, argv, "p:m:hL")) != -1) {
        switch (c) {
        case 'p':
            use_pid = 1;
            pid = strtol(optarg, NULL, 0);
            break;
        case 'l':
            use_lock = 1;
            break;
        case 'm':
            msecs = strtol(optarg, NULL, 0);
            break;
        case 'h':
        default:
            goto usage;
        }
    }

    if (optind == argc)
        goto usage;

    if (!strcmp(argv[optind], "monitor")) {
        command = SCHED_GET_PARAMS;
        use_pid = 0;
        wait_tsgs = true;
        goto parsed;
    }

    if (!strcmp(argv[optind], "get")) {
        if (++optind >= argc)
            goto usage;
        if (!strcmp(argv[optind], "tsgs")) {
            command = SCHED_GET_TSGS;
            goto parsed;
        }
        if (!strcmp(argv[optind], "params")) {
            command = SCHED_GET_PARAMS;
            goto parsed;
        }
        printf("get sub command not found\n");
    }

    if (!strcmp(argv[optind], "set")) {
        if (++optind >= argc)
            goto usage;
        if (!strcmp(argv[optind], "timeslice")) {
            command = SCHED_SET_TIMESLICE;
            if (++optind >= argc)
                goto usage;
            timeslice = strtol(argv[optind], NULL, 0);
            goto parsed;
        }
        if (!strcmp(argv[optind], "interleave")) {
            command = SCHED_SET_INTERLEAVE;
            if (++optind >= argc)
                goto usage;
            interleave = strtol(argv[optind], NULL, 0);
            if (interleave < NVRM_GPUSCHED_RUNLIST_INTERLEAVE_LOW)
                goto usage;
            if (interleave > NVRM_GPUSCHED_RUNLIST_INTERLEAVE_HIGH)
                goto usage;
            goto parsed;
        }
    }

    goto usage;

parsed:
    if (NvRmGpuSchedSessionOpen(&sessionAttr, &hSession))
        pabort("could not open sched session\n");

    if (NvRmGpuSchedAllocateTsgSet(hSession, &hTsgSet))
        pabort("could not allocate TSG set\n");

    if (use_lock && NvRmGpuSchedLockControl(hSession))
        pabort("could not lock control\n");

    if (wait_tsgs)
        goto wait;

    if (use_pid) {
        if (NvRmGpuSchedGetTsgsByPid(hTsgSet, pid))
            pabort("could not read TSGs for this pid\n");
    } else {
        if (NvRmGpuSchedGetAllActiveTsgs(hTsgSet))
            pabort("could not read TSGs\n");
    }

    while (!done) {

        status = NvRmGpuSchedGetNextTsgId(hTsgSet, &tsgid);
        if (status == NVRM_GPUSCHED_STATUS_NONE_PENDING) {
            if (!wait_tsgs)
                break;

            duration = 0;
wait:
            status = NvRmGpuSchedWaitTsgEvents(hSession, NULL, msecs);
            if (status == NVRM_GPUSCHED_STATUS_TIMED_OUT) {
                duration += msecs;
                if (duration >= 10000) {
                    printf("waited %d msecs\n", duration);
                    duration = 0;
                }
                goto wait;
            }

            if (status)
                pabort("wait TSG events failed");

            if (NvRmGpuSchedGetDeltaTsgs(hTsgSet))
                pabort("could not read recent TSGs\n");

            continue;
        }

        if (NvRmGpuSchedGetTsgHandle(hSession, tsgid, &hTsg)) {
            printf("could not acquire handle for tsgid=%u\n", tsgid);
            continue;
        }

        switch (command) {
        case SCHED_GET_TSGS:
            printf("%d ", tsgid);
            break;
        case SCHED_GET_PARAMS:
            if (get_params(hTsg))
                pabort("could not get params for tsgid=%u\n", tsgid);
            break;
        case SCHED_SET_TIMESLICE:
            if (NvRmGpuSchedSetTsgTimeslice(hTsg, timeslice))
                pabort("set timeslice=%u failed for tsgid=%u\n",
                    timeslice, tsgid);
            break;
        case SCHED_SET_INTERLEAVE:
            if (NvRmGpuSchedSetTsgInterleave(hTsg, interleave))
                pabort("set interleave=%u failed for tsgid=%u\n",
                        interleave, tsgid);
            break;
        default:
            pabort("unexpected command %d\n", command);
        }

        NvRmGpuSchedReleaseTsgHandle(hTsg);
    }

    NvRmGpuSchedFreeTsgSet(hTsgSet);

    if (use_lock && NvRmGpuSchedUnlockControl(hSession))
        pabort("could not lock control\n");

    NvRmGpuSchedSessionClose(hSession);

    exit(0);

usage:
    printf("usage:\n");
    printf("\t%s monitor [-l]\n", argv[0]);
    printf("\t%s get tsgs [-p <pid>]\n", argv[0]);
    printf("\t%s get params [-p <pid>]\n", argv[0]);
    printf("\t%s set timeslice [-p <pid>] <timeslice>\n", argv[0]);
    printf("\t%s set interleave [-p <pid>] <interleave>\n", argv[0]);
    printf("\t    with <interleave> in [%d (LOW) .. %d (HIGH)]\n",
            NVRM_GPUSCHED_RUNLIST_INTERLEAVE_LOW,
            NVRM_GPUSCHED_RUNLIST_INTERLEAVE_HIGH);
    printf("options:\n");
    printf("\t-p <pid> select TSGs created by a given process id\n");
    printf("\t-m       msecs to wait for TSGs events\n");
    printf("\t-l       lock control to prevent other process from");
    printf("\t         changing their own params\n");

    exit(1);
}
