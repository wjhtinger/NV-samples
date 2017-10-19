/*
 * nvgldemo_fence.c
 *
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// This file contains required logic for renderahead
//

#include "nvgldemo.h"

static GLsync * syncobjarr;

int NvGlDemoThrottleInit()
{

    if (!demoOptions.nFifo)
    {
        if (demoOptions.renderahead == -1) {
            return 1;
        } else {
            NvGlDemoCqInitIndex(demoOptions.renderahead);
            syncobjarr = malloc(demoOptions.renderahead * sizeof(GLsync));

            if (syncobjarr == NULL) {
                return 0;
            }
            return 1;
        }

        NvGlDemoLog(" Renderahead allowed is %d \n", demoOptions.renderahead);
    }
    return 1;
}

int NvGlDemoThrottleRendering()
{
    int index;

    if (!demoOptions.nFifo) {

        switch(demoOptions.renderahead) {
        case -1:
            break;
        case 0:
            glFinish();
            break;
        default:
            {
                if (NvGlDemoCqFull()) {
                    index = NvGlDemoCqDeleteIndex();

                    if (index == -1) {
                        break;
                    }

                    GLsync  tempobj = syncobjarr[index];
                    long long wait_time = 10000000; //10 ms
                    long long total_time = 0;
                    long long time_out = 10000000000; //10 sec
                    GLenum ret;

                    do {
                        ret = glClientWaitSync(tempobj,
                                               GL_SYNC_FLUSH_COMMANDS_BIT,
                                               wait_time);
                        total_time += wait_time;
                    } while(ret != GL_ALREADY_SIGNALED &&
                            ret != GL_CONDITION_SATISFIED &&
                            total_time < time_out);

                    // Timeout of 10 sec
                    if (total_time >= time_out) {
                        return 0;
                    }
                    glDeleteSync(tempobj);
                }
                GLsync syncobj = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                index = NvGlDemoCqInsertIndex();
 
                if (index == -1) {
                    break;
                }

                syncobjarr[index] = syncobj;
            }
        }
    }
    return 1;
}

void NvGlDemoThrottleShutdown()
{
    if (demoOptions.renderahead > 0) {
        free(syncobjarr);
    }
}
