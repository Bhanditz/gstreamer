/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstqueue.c:
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

//#define DEBUG_ENABLED
//#define STATUS_ENABLED
#ifdef STATUS_ENABLED
#define STATUS(A) GST_DEBUG(0,A, gst_element_get_name(GST_ELEMENT(queue)))
#else
#define STATUS(A)
#endif

#include <pthread.h>

#include "config.h"
#include "gst_private.h"

#include "gstqueue.h"

GstElementDetails gst_queue_details = {
  "Queue",
  "Connection",
  "Simple data queue",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


/* Queue signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_LEVEL,
  ARG_MAX_LEVEL,
  ARG_BLOCK,
};


static void			gst_queue_class_init	(GstQueueClass *klass);
static void			gst_queue_init		(GstQueue *queue);

static void			gst_queue_set_arg	(GtkObject *object, GtkArg *arg, guint id);
static void			gst_queue_get_arg	(GtkObject *object, GtkArg *arg, guint id);

static gboolean			gst_queue_handle_eos	(GstPad *pad);
static GstPadNegotiateReturn 	gst_queue_handle_negotiate_src (GstPad *pad, GstCaps **caps, gint count);
static GstPadNegotiateReturn	gst_queue_handle_negotiate_sink (GstPad *pad, GstCaps **caps, gint count);
static void			gst_queue_chain		(GstPad *pad, GstBuffer *buf);
static GstBuffer *		gst_queue_get		(GstPad *pad);

static void			gst_queue_flush		(GstQueue *queue);

static GstElementStateReturn	gst_queue_change_state	(GstElement *element);


static GstElementClass *parent_class = NULL;
//static guint gst_queue_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_queue_get_type(void) {
  static GtkType queue_type = 0;

  if (!queue_type) {
    static const GtkTypeInfo queue_info = {
      "GstQueue",
      sizeof(GstQueue),
      sizeof(GstQueueClass),
      (GtkClassInitFunc)gst_queue_class_init,
      (GtkObjectInitFunc)gst_queue_init,
      (GtkArgSetFunc)gst_queue_set_arg,
      (GtkArgGetFunc)gst_queue_get_arg,
      (GtkClassInitFunc)NULL,
    };
    queue_type = gtk_type_unique (GST_TYPE_ELEMENT, &queue_info);
  }
  return queue_type;
}

static void
gst_queue_class_init (GstQueueClass *klass)
{
  GtkObjectClass *gtkobject_class;
  GstElementClass *gstelement_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = gtk_type_class (GST_TYPE_ELEMENT);

  gtk_object_add_arg_type ("GstQueue::level", GTK_TYPE_INT,
                           GTK_ARG_READABLE, ARG_LEVEL);
  gtk_object_add_arg_type ("GstQueue::max_level", GTK_TYPE_INT,
                           GTK_ARG_READWRITE, ARG_MAX_LEVEL);
  gtk_object_add_arg_type ("GstQueue::block", GTK_TYPE_BOOL,
                           GTK_ARG_READWRITE, ARG_BLOCK);

  gtkobject_class->set_arg = gst_queue_set_arg;
  gtkobject_class->get_arg = gst_queue_get_arg;

  gstelement_class->change_state = gst_queue_change_state;
}

static void
gst_queue_init (GstQueue *queue)
{
  // scheduling on this kind of element is, well, interesting
  GST_FLAG_SET (queue, GST_ELEMENT_DECOUPLED);

  queue->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_pad_set_chain_function (queue->sinkpad, GST_DEBUG_FUNCPTR(gst_queue_chain));
  gst_element_add_pad (GST_ELEMENT (queue), queue->sinkpad);
  gst_pad_set_eos_function (queue->sinkpad, gst_queue_handle_eos);
  gst_pad_set_negotiate_function (queue->sinkpad, gst_queue_handle_negotiate_sink);

  queue->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_get_function (queue->srcpad, GST_DEBUG_FUNCPTR(gst_queue_get));
  gst_element_add_pad (GST_ELEMENT (queue), queue->srcpad);
  gst_pad_set_negotiate_function (queue->srcpad, gst_queue_handle_negotiate_src);

  queue->queue = NULL;
  queue->level_buffers = 0;
  queue->max_buffers = 100;
  queue->block = TRUE;
  queue->level_bytes = 0;
  queue->size_buffers = 0;
  queue->size_bytes = 0;

  queue->emptycond = g_cond_new ();
  queue->fullcond = g_cond_new ();
}

static GstPadNegotiateReturn
gst_queue_handle_negotiate_src (GstPad *pad, GstCaps **caps, gint count)
{
  GstQueue *queue;

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));

  return gst_pad_negotiate_proxy (queue->sinkpad, caps, count);
}

static GstPadNegotiateReturn
gst_queue_handle_negotiate_sink (GstPad *pad, GstCaps **caps, gint count)
{
  GstQueue *queue;

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));

  return gst_pad_negotiate_proxy (queue->srcpad, caps, count);
}

static gboolean
gst_queue_handle_eos (GstPad *pad)
{
  GstQueue *queue;

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));

  GST_DEBUG (0,"queue: %s received eos\n", GST_ELEMENT_NAME (queue));

  GST_LOCK (queue);
  GST_DEBUG (0,"queue: %s has %d buffers left\n", GST_ELEMENT_NAME (queue),
		  queue->level_buffers);

  GST_FLAG_SET (pad, GST_PAD_EOS);

  g_cond_signal (queue->emptycond);

  GST_UNLOCK (queue);

  return TRUE;
}

static void
gst_queue_cleanup_buffers (gpointer data, const gpointer user_data)
{
  GST_DEBUG (0,"queue: %s cleaning buffer %p\n", (gchar *)user_data, data);

  gst_buffer_unref (GST_BUFFER (data));
}

static void
gst_queue_flush (GstQueue *queue)
{
  g_slist_foreach (queue->queue, gst_queue_cleanup_buffers,
		  (char *) GST_ELEMENT_NAME (queue));
  g_slist_free (queue->queue);

  queue->queue = NULL;
  queue->level_buffers = 0;
  queue->timeval = NULL;
}

static void
gst_queue_chain (GstPad *pad, GstBuffer *buf)
{
  GstQueue *queue;
  gboolean tosignal = FALSE;
  const guchar *name;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));
  name = GST_ELEMENT_NAME (queue);

  /* we have to lock the queue since we span threads */

  GST_DEBUG (0,"queue: try have queue lock\n");
  GST_LOCK (queue);
  GST_DEBUG (0,"queue: %s adding buffer %p %ld\n", name, buf, pthread_self ());
  GST_DEBUG (0,"queue: have queue lock\n");

  if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLUSH)) {
    gst_queue_flush (queue);
  }

  GST_DEBUG (0,"queue: %s: chain %d %p\n", name, queue->level_buffers, buf);

  while (queue->level_buffers >= queue->max_buffers) {
    GST_DEBUG (0,"queue: %s waiting %d\n", name, queue->level_buffers);
    STATUS("%s: O\n");
    //g_cond_timed_wait (queue->fullcond, queue->fulllock, queue->timeval);
    //FIXME need to signal other thread in case signals got lost?
    g_cond_signal (queue->emptycond);
    g_cond_wait (queue->fullcond, GST_OBJECT(queue)->lock);
    STATUS("%s: O+\n");
    GST_DEBUG (0,"queue: %s waiting done %d\n", name, queue->level_buffers);
  }

  /* put the buffer on the tail of the list */
  queue->queue = g_slist_append (queue->queue, buf);
//  STATUS("%s: +\n");
  GST_DEBUG (0,"(%s:%s)+ ",GST_DEBUG_PAD_NAME(pad));

  /* if we were empty, but aren't any more, signal a condition */
  tosignal = (queue->level_buffers >= 0);
  queue->level_buffers++;

  /* we can unlock now */
  GST_DEBUG (0,"queue: %s chain %d end signal(%d,%p)\n", name, queue->level_buffers, tosignal, queue->emptycond);

  if (tosignal) {
//    STATUS("%s: >\n");
    g_cond_signal (queue->emptycond);
//    STATUS("%s: >>\n");
  }
  GST_UNLOCK (queue);
}

static GstBuffer *
gst_queue_get (GstPad *pad)
{
  GstQueue *queue;
  GstBuffer *buf = NULL;
  GSList *front;
  const guchar *name;

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));
  name = GST_ELEMENT_NAME (queue);

  /* have to lock for thread-safety */
  GST_DEBUG (0,"queue: %s try have queue lock\n", name);
  GST_LOCK (queue);
  GST_DEBUG (0,"queue: %s push %d %ld %p\n", name, queue->level_buffers, pthread_self (), queue->emptycond);
  GST_DEBUG (0,"queue: %s have queue lock\n", name);

  // we bail if there's nothing there
  if (!queue->level_buffers && !queue->block) {
    GST_UNLOCK(queue);
    return NULL;
  }

  while (!queue->level_buffers) {
    STATUS("queue: %s U released lock\n");
    //g_cond_timed_wait (queue->emptycond, queue->emptylock, queue->timeval);
    if (GST_FLAG_IS_SET (queue->sinkpad, GST_PAD_EOS)) {
      gst_pad_set_eos (queue->srcpad);
      return NULL;
    }
    //FIXME need to signal other thread in case signals got lost?
    g_cond_signal (queue->fullcond);
    g_cond_wait (queue->emptycond, GST_OBJECT(queue)->lock);
//    STATUS("queue: %s U- getting lock\n");
  }

  front = queue->queue;
  buf = (GstBuffer *)(front->data);
  GST_DEBUG (0,"retrieved buffer %p from queue\n",buf);
  queue->queue = g_slist_remove_link (queue->queue, front);
  g_slist_free (front);

  queue->level_buffers--;
//  STATUS("%s: -\n");
  GST_DEBUG (0,"(%s:%s)- ",GST_DEBUG_PAD_NAME(pad));

  if (queue->level_buffers < queue->max_buffers) {
//    STATUS("%s: < \n");
    g_cond_signal (queue->fullcond);
//    STATUS("%s: << \n");
  }
  GST_UNLOCK(queue);

//  GST_DEBUG (0,"queue: %s pushing %d %p \n", name, queue->level_buffers, buf);
//  gst_pad_push (queue->srcpad, buf);
//  GST_DEBUG (0,"queue: %s pushing %d done \n", name, queue->level_buffers);

  return buf;
  /* unlock now */
}

static GstElementStateReturn
gst_queue_change_state (GstElement *element)
{
  GstQueue *queue;
  g_return_val_if_fail (GST_IS_QUEUE (element), GST_STATE_FAILURE);

  queue = GST_QUEUE (element);
  GST_DEBUG (0,"gstqueue: state pending %d\n", GST_STATE_PENDING (element));

  /* if going down into NULL state, clear out buffers*/
  if (GST_STATE_PENDING (element) == GST_STATE_READY) {
    /* otherwise (READY or higher) we need to open the file */
    gst_queue_flush (queue);
  }

  /* if we haven't failed already, give the parent class a chance to ;-) */
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}


static void
gst_queue_set_arg (GtkObject *object, GtkArg *arg, guint id)
{
  GstQueue *queue;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_QUEUE (object));

  queue = GST_QUEUE (object);

  switch(id) {
    case ARG_MAX_LEVEL:
      queue->max_buffers = GTK_VALUE_INT (*arg);
      break;
    case ARG_BLOCK:
      queue->block = GTK_VALUE_BOOL (*arg);
      break;
    default:
      break;
  }
}

static void
gst_queue_get_arg (GtkObject *object, GtkArg *arg, guint id)
{
  GstQueue *queue;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_QUEUE (object));

  queue = GST_QUEUE (object);

  switch (id) {
    case ARG_LEVEL:
      GTK_VALUE_INT (*arg) = queue->level_buffers;
      break;
    case ARG_MAX_LEVEL:
      GTK_VALUE_INT (*arg) = queue->max_buffers;
      break;
    case ARG_BLOCK:
      GTK_VALUE_BOOL (*arg) = queue->block;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}
