/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2005> Wim Taymans <wim@fluendo.com>
 *
 * gsttask.h: Streaming tasks
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_TASK_H__
#define __GST_TASK_H__

#include <gst/gstobject.h>

G_BEGIN_DECLS

typedef void         (*GstTaskFunction)          (void *data);

/* --- standard type macros --- */
#define GST_TYPE_TASK                 	(gst_task_get_type ())
#define GST_TASK(task)                	(G_TYPE_CHECK_INSTANCE_CAST ((task), GST_TYPE_TASK, GstTask))
#define GST_IS_TASK(task)             	(G_TYPE_CHECK_INSTANCE_TYPE ((task), GST_TYPE_TASK))
#define GST_TASK_CLASS(tclass)         	(G_TYPE_CHECK_CLASS_CAST ((tclass), GST_TYPE_TASK, GstTaskClass))
#define GST_IS_TASK_CLASS(tclass)      	(G_TYPE_CHECK_CLASS_TYPE ((tclass), GST_TYPE_TASK))
#define GST_TASK_GET_CLASS(task)      	(G_TYPE_INSTANCE_GET_CLASS ((task), GST_TYPE_TASK, GstTaskClass))
#define GST_TASK_CAST(task)            	((GstTask*)(task))

typedef struct _GstTask GstTask;
typedef struct _GstTaskClass GstTaskClass;

typedef enum {
  GST_TASK_STARTED,
  GST_TASK_STOPPED,
  GST_TASK_PAUSED,
} GstTaskState;

#define GST_TASK_STATE(task)		(GST_TASK_CAST(task)->state)

#define GST_TASK_GET_COND(task)		(GST_TASK_CAST(task)->cond)
#define GST_TASK_WAIT(task)		g_cond_wait(GST_TASK_GET_COND (task), GST_GET_LOCK (task))
#define GST_TASK_SIGNAL(task)		g_cond_signal(GST_TASK_GET_COND (task))
#define GST_TASK_BROADCAST(task)	g_cond_breadcast(GST_TASK_GET_COND (task))

#define GST_TASK_GET_LOCK(task)		(GST_TASK_CAST(task)->lock)
#define GST_TASK_LOCK(task)		g_static_rec_mutex_lock(GST_TASK_GET_LOCK(task))
#define GST_TASK_UNLOCK(task)		g_static_rec_mutex_unlock(GST_TASK_GET_LOCK(task))

struct _GstTask {
  GstObject      object;


  /*< public >*/ /* with LOCK */
  GstTaskState     state;
  GCond 	  *cond;

  GStaticRecMutex *lock;

  GstTaskFunction func;
  gpointer 	 data;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstTaskClass {
  GstObjectClass parent_class;

  /*< protected >*/
  gboolean (*start) (GstTask *task);
  gboolean (*stop)  (GstTask *task);
  gboolean (*pause) (GstTask *task);

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType           gst_task_get_type       (void);

GstTask*	gst_task_create		(GstTaskFunction func, gpointer data);
void		gst_task_set_lock	(GstTask *task, GStaticRecMutex *mutex);

GstTaskState	gst_task_get_state	(GstTask *task);

gboolean	gst_task_start		(GstTask *task);
gboolean	gst_task_stop		(GstTask *task);
gboolean	gst_task_pause		(GstTask *task);

G_END_DECLS

#endif /* __GST_TASK_H__ */

