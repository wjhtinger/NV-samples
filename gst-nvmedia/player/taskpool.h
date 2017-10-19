/* Copyright (c) 2014, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __GST_NVM_TASK_POOL_H__
#define __GST_NVM_TASK_POOL_H__

#include <gst/gst.h>
#include <pthread.h>

G_BEGIN_DECLS

typedef struct _GstNvmTaskPool GstNvmTaskPool;
typedef struct _GstNvmTaskPoolClass GstNvmTaskPoolClass;

#define GST_TYPE_NVM_TASK_POOL \
    (gst_nvm_task_pool_get_type())
#define GST_NVM_TASK_POOL(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_NVM_TASK_POOL, GstNvmTaskPool))
#define GST_NVM_TASK_POOL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_NVM_TASK_POOL, GstNvmTaskPoolClass))
#define GST_NVM_TASK_POOL_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_NVM_TASK_POOL, GstNvmTaskPoolClass))
#define GST_IS_NVM_TASK_POOL(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_NVM_TASK_POOL))
#define GST_IS_NVM_TASK_POOL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_NVM_TASK_POOL))

#define GST_NVM_AUDIO_QUEUE_PRIORITY 30
#define GST_NVM_AUDIO_QUEUE_SCHED_POLICY SCHED_FIFO
#define GST_NVM_VIDEO_QUEUE_PRIORITY 30
#define GST_NVM_VIDEO_QUEUE_SCHED_POLICY SCHED_FIFO

struct _GstNvmTaskPool {
    GstTaskPool object;
};

struct _GstNvmTaskPoolClass {
    GstTaskPoolClass parent_class;
};

GType gst_nvm_task_pool_get_type (void);
GstTaskPool* gst_nvm_task_pool_new (int priority, int policy);

G_END_DECLS

#endif /* __GST_NVM_TASK_POOL_H__ */
