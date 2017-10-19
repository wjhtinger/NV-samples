/* Copyright (c) 2014, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "taskpool.h"

G_DEFINE_TYPE (GstNvmTaskPool, gst_nvm_task_pool, GST_TYPE_TASK_POOL);

typedef struct {
    pthread_t thread;
} GstNvmThread;

typedef struct {
    int priority;
    int sched_policy;
} GstNvmThreadAttr;

GstNvmThreadAttr thread_attr;

static void
_thread_join (GstTaskPool *pool, gpointer id)
{
   GstNvmThread *tid = (GstNvmThread *) id;

   pthread_join (tid->thread, NULL);
   g_slice_free (GstNvmThread, tid);
}

static gpointer
_thread_push (GstTaskPool *pool, GstTaskPoolFunction func, gpointer data, GError **error)
{
   GstNvmThread *tid;
   pthread_attr_t attr;
   gint ret;

   struct sched_param param;
   tid = g_slice_new0 (GstNvmThread);
   pthread_attr_init (&attr);
   if ((ret = pthread_attr_setschedpolicy (&attr,
                                           thread_attr.sched_policy)) != 0)
      GST_WARNING ("setschedpolicy: failure");

   param.sched_priority = thread_attr.priority;
   if ((ret = pthread_attr_setschedparam (&attr, &param)) != 0)
       GST_WARNING ("setschedparam: failure");

   if ((ret = pthread_attr_setinheritsched (&attr,
                                            PTHREAD_EXPLICIT_SCHED)) != 0)
      GST_WARNING ("setinheritsched: failure");

   ret = pthread_create (&tid->thread, &attr,
                        (void *(*)(void *)) func, data);
   switch (ret) {
      case 0 :
          break;
      case EPERM :
          pthread_attr_init (&attr);
          pthread_create (&tid->thread, &attr,
                        (void *(*)(void *)) func, data);
          break;
      default :
          GST_ERROR ("Error creating thread");
          g_slice_free (GstNvmThread, tid);
          tid = NULL;
   }

   return tid;
}

static void
gst_nvm_task_pool_class_init (GstNvmTaskPoolClass * klass)
{
   GstTaskPoolClass *gsttaskpool_class;
   gsttaskpool_class = (GstTaskPoolClass *) klass;

   gsttaskpool_class->push = _thread_push;
   gsttaskpool_class->join = _thread_join;
}

GstTaskPool *
gst_nvm_task_pool_new (int priority, int policy)
{
  GstTaskPool *pool;

  pool = g_object_new (GST_TYPE_NVM_TASK_POOL, NULL);
  thread_attr.priority = priority;
  thread_attr.sched_policy = policy;

  return pool;
}

static void
gst_nvm_task_pool_init (GstNvmTaskPool * pool)
{
}
