/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstclock.c: Clock subsystem for maintaining time sync
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

#include <sys/time.h>

/* #define GST_DEBUG_ENABLED */
#include "gst_private.h"

#include "gstclock.h"
#include "gstlog.h"
#include "gstmemchunk.h"

#define CLASS(clock)  GST_CLOCK_CLASS (G_OBJECT_GET_CLASS (clock))

static GstMemChunk *_gst_clock_entries_chunk;

static void		gst_clock_class_init		(GstClockClass *klass);
static void		gst_clock_init			(GstClock *clock);


static GstObjectClass *parent_class = NULL;
/* static guint gst_clock_signals[LAST_SIGNAL] = { 0 }; */

typedef struct _GstClockEntry GstClockEntry;

static void 		gst_clock_free_entry 		(GstClock *clock, GstClockEntry *entry);

typedef enum {
  GST_ENTRY_OK,
  GST_ENTRY_RESTART,
} GstEntryStatus;

struct _GstClockEntry {
  GstClockTime 		 time;
  GstEntryStatus 	 status;
  GstClockCallback 	 func;
  gpointer		 user_data;
};

#define GST_CLOCK_ENTRY(entry)          ((GstClockEntry *)(entry))
#define GST_CLOCK_ENTRY_TIME(entry)     (((GstClockEntry *)(entry))->time)

static GstClockEntry*
gst_clock_entry_new (GstClockTime time,
		     GstClockCallback func, gpointer user_data)
{
  GstClockEntry *entry;

  entry = gst_mem_chunk_alloc (_gst_clock_entries_chunk);

  entry->time = time;
  entry->func = func;
  entry->user_data = user_data;

  return entry;
}

/*
static gint
clock_compare_func (gconstpointer a,
                    gconstpointer b)
{
  GstClockEntry *entry1 = (GstClockEntry *)a;
  GstClockEntry *entry2 = (GstClockEntry *)b;

  return (entry1->time - entry2->time);
}
*/

GType
gst_clock_get_type (void)
{
  static GType clock_type = 0;

  if (!clock_type) {
    static const GTypeInfo clock_info = {
      sizeof (GstClockClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_clock_class_init,
      NULL,
      NULL,
      sizeof (GstClock),
      4,
      (GInstanceInitFunc) gst_clock_init,
      NULL
    };
    clock_type = g_type_register_static (GST_TYPE_OBJECT, "GstClock", 
		    			 &clock_info,  G_TYPE_FLAG_ABSTRACT);
  }
  return clock_type;
}

static void
gst_clock_class_init (GstClockClass *klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass*) klass;
  gstobject_class = (GstObjectClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_OBJECT);

  if (!g_thread_supported ())
    g_thread_init (NULL);

  _gst_clock_entries_chunk = gst_mem_chunk_new ("GstClockEntries",
                     sizeof (GstClockEntry), sizeof (GstClockEntry) * 32,
                     G_ALLOC_AND_FREE);
}

static void
gst_clock_init (GstClock *clock)
{
  clock->speed = 1.0;
  clock->active = FALSE;
  clock->start_time = 0;
  clock->last_time = 0;
  clock->entries = NULL;
  clock->async_supported = FALSE;

  clock->active_mutex = g_mutex_new ();
  clock->active_cond = g_cond_new ();
}

/**
 * gst_clock_async_supported
 * @clock: a #GstClock to query
 *
 * Checks if this clock can support asynchronous notification.
 *
 * Returns: TRUE if async notification is supported.
 */
gboolean
gst_clock_async_supported (GstClock *clock)
{
  g_return_val_if_fail (GST_IS_CLOCK (clock), FALSE);

  return clock->async_supported;
}

/**
 * gst_clock_set_speed
 * @clock: a #GstClock to modify
 * @speed: the speed to set on the clock
 *
 * Sets the speed on the given clock. 1.0 is the default 
 * speed.
 */
void
gst_clock_set_speed (GstClock *clock, gdouble speed)
{
  g_return_if_fail (GST_IS_CLOCK (clock));

  clock->speed = speed;
}

/**
 * gst_clock_get_speed
 * @clock: a #GstClock to query
 *
 * Gets the speed of the given clock.
 *
 * Returns: the speed of the clock.
 */
gdouble
gst_clock_get_speed (GstClock *clock)
{
  g_return_val_if_fail (GST_IS_CLOCK (clock), 0.0);

  return clock->speed;
}


/**
 * gst_clock_reset
 * @clock: a #GstClock to reset
 *
 * Reset the clock to time 0.
 */
void
gst_clock_reset (GstClock *clock)
{
  GstClockTime time = 0LL;

  g_return_if_fail (GST_IS_CLOCK (clock));

  if (CLASS (clock)->get_internal_time) {
    time = CLASS (clock)->get_internal_time (clock);
  }

  GST_LOCK (clock);
  clock->active = FALSE;
  clock->start_time = time;
  clock->last_time = 0LL;
  GST_UNLOCK (clock);
}

/**
 * gst_clock_set_active
 * @clock: a #GstClock to set state of
 * @active: flag indicating if the clock should be activated (TRUE) or deactivated
 *
 * Activates or deactivates the clock based on the active parameter.
 * As soon as the clock is activated, the time will start ticking.
 */
void
gst_clock_set_active (GstClock *clock, gboolean active)
{
  GstClockTime time = 0LL;

  g_return_if_fail (GST_IS_CLOCK (clock));

  clock->active = active;
	        
  if (CLASS (clock)->get_internal_time) {
    time = CLASS (clock)->get_internal_time (clock);
  }

  GST_LOCK (clock);
  if (active) {
    clock->start_time = time - clock->last_time;
    clock->accept_discont = TRUE;
  }
  else {
    clock->last_time = time - clock->start_time;
    clock->accept_discont = FALSE;
  }
  GST_UNLOCK (clock);

  g_mutex_lock (clock->active_mutex);	
  g_cond_broadcast (clock->active_cond);	
  g_mutex_unlock (clock->active_mutex);	
}

/**
 * gst_clock_is_active
 * @clock: a #GstClock to query
 *
 * Checks if the given clock is active.
 * 
 * Returns: TRUE if the clock is active.
 */
gboolean
gst_clock_is_active (GstClock *clock)
{
  g_return_val_if_fail (GST_IS_CLOCK (clock), FALSE);

  return clock->active;
}

/**
 * gst_clock_handle_discont
 * @clock: a #GstClock to notify of the discontinuity
 * @time: The new time
 *
 * Notifies the clock of a discontinuity in time.
 * 
 * Returns: TRUE if the clock was updated. It is possible that
 * the clock was not updated by this call because only the first
 * discontinuitity in the pipeline is honoured.
 */
gboolean
gst_clock_handle_discont (GstClock *clock, guint64 time)
{
  GstClockTime itime = 0LL;
  
  GST_DEBUG (GST_CAT_CLOCK, "clock discont %llu %llu %d", time, clock->start_time, clock->accept_discont);

  GST_LOCK (clock);
  if (clock->accept_discont) {
    if (CLASS (clock)->get_internal_time) {
      itime = CLASS (clock)->get_internal_time (clock);
    }
  }
  else {
    GST_UNLOCK (clock);
    GST_DEBUG (GST_CAT_CLOCK, "clock discont refused %llu %llu", time, clock->start_time);
    return FALSE;
  }

  clock->start_time = itime - time;
  clock->last_time = time;
  clock->accept_discont = FALSE;
  GST_UNLOCK (clock);

  GST_DEBUG (GST_CAT_CLOCK, "new time %llu", gst_clock_get_time (clock));

  g_mutex_lock (clock->active_mutex);	
  g_cond_broadcast (clock->active_cond);	
  g_mutex_unlock (clock->active_mutex);	

  return TRUE;
}

/**
 * gst_clock_get_time
 * @clock: a #GstClock to query
 *
 * Gets the current time of the given clock. The time is always
 * monotonically increasing.
 * 
 * Returns: the time of the clock.
 */
GstClockTime
gst_clock_get_time (GstClock *clock)
{
  GstClockTime ret = 0LL;

  g_return_val_if_fail (GST_IS_CLOCK (clock), 0LL);

  if (!clock->active) {
    /* clock is not activen return previous time */
    ret = clock->last_time;
  }
  else {
    if (CLASS (clock)->get_internal_time) {
      ret = CLASS (clock)->get_internal_time (clock) - clock->start_time;
    }
    /* make sure the time is increasing, else return last_time */
    if ((gint64) ret < (gint64) clock->last_time) {
      ret = clock->last_time;
    }
    else {
      clock->last_time = ret;
    }
  }

  return ret;
}

static GstClockID
gst_clock_wait_async_func (GstClock *clock, GstClockTime time,
		           GstClockCallback func, gpointer user_data)
{
  GstClockEntry *entry = NULL;
  g_return_val_if_fail (GST_IS_CLOCK (clock), NULL);

  if (!clock->active) {
    GST_DEBUG (GST_CAT_CLOCK, "blocking on clock");
    g_mutex_lock (clock->active_mutex);	
    g_cond_wait (clock->active_cond, clock->active_mutex);	
    g_mutex_unlock (clock->active_mutex);	
  }

  entry = gst_clock_entry_new (time, func, user_data);

  return entry;
}

/**
 * gst_clock_wait
 * @clock: a #GstClock to wait on
 * @time: The #GstClockTime to wait for
 * @jitter: The jitter 
 *
 * Wait and block till the clock reaches the specified time.
 * The jitter value contains the difference between the requested time and
 * the actual time, negative values indicate that the requested time
 * was allready passed when this call was made.
 *
 * Returns: the #GstClockReturn result of the operation.
 */
GstClockReturn
gst_clock_wait (GstClock *clock, GstClockTime time, GstClockTimeDiff *jitter)
{
  GstClockID id;
  GstClockReturn res;
  
  g_return_val_if_fail (GST_IS_CLOCK (clock), GST_CLOCK_STOPPED);

  id = gst_clock_wait_async_func (clock, time, NULL, NULL);
  res = gst_clock_wait_id (clock, id, jitter);

  return res;
}

/**
 * gst_clock_wait_async
 * @clock: a #GstClock to wait on
 * @time: The #GstClockTime to wait for
 * @func: The callback function 
 * @user_data: User data passed in the calback
 *
 * Register a callback on the given clock that will be triggered 
 * when the clock has reached the given time. A ClockID is returned
 * that can be used to cancel the request.
 *
 * Returns: the clock id or NULL when async notification is not supported.
 */
GstClockID
gst_clock_wait_async (GstClock *clock, GstClockTime time,
		      GstClockCallback func, gpointer user_data)
{
  g_return_val_if_fail (GST_IS_CLOCK (clock), NULL);

  if (clock->async_supported) {
    return gst_clock_wait_async_func (clock, time, func, user_data);
  }
  return NULL;
}

/**
 * gst_clock_cancel_wait_async
 * @clock: The clock to cancel the request on
 * @id: The id to cancel
 *
 * Cancel an outstanding async notification request with the given ID.
 */
void
gst_clock_cancel_wait_async (GstClock *clock, GstClockID id)
{
  g_warning ("not supported");
}

/**
 * gst_clock_notify_async
 * @clock: The clock to wait on
 * @interval: The interval between notifications
 * @func: The callback function 
 * @user_data: User data passed in the calback
 *
 * Register a callback on the given clock that will be periodically
 * triggered with the specified interval. A ClockID is returned
 * that can be used to cancel the request.
 *
 * Returns: the clock id or NULL when async notification is not supported.
 */
GstClockID
gst_clock_notify_async (GstClock *clock, GstClockTime interval,
		        GstClockCallback func, gpointer user_data) 
{
  g_warning ("not supported");
  return NULL;
}

/**
 * gst_clock_remove_notify_async
 * @clock: The clock to cancel the request on
 * @id: The id to cancel
 *
 * Cancel an outstanding async notification request with the given ID.
 */
void
gst_clock_remove_notify_async (GstClock *clock, GstClockID id)
{
  g_warning ("not supported");
}

static void
gst_clock_unlock_func (GstClock *clock, GstClockTime time, GstClockID id, gpointer user_data)
{
}

/**
 * gst_clock_wait_id
 * @clock: The clock to wait on
 * @id: The clock id to wait on
 * @jitter: The jitter 
 *
 * Wait and block on the clockid obtained with gst_clock_wait_async.
 * The jitter value is described in gst_clock_wait().
 *
 * Returns: result of the operation.
 */
GstClockReturn
gst_clock_wait_id (GstClock *clock, GstClockID id, GstClockTimeDiff *jitter)
{
  GstClockReturn res = GST_CLOCK_TIMEOUT;
  GstClockEntry *entry = (GstClockEntry *) id;
  GstClockTime current, target;
  GstClockTimeDiff this_jitter;
  
  g_return_val_if_fail (GST_IS_CLOCK (clock), GST_CLOCK_ERROR);
  g_return_val_if_fail (entry, GST_CLOCK_ERROR);

  current = gst_clock_get_time (clock);

  entry->func = gst_clock_unlock_func;
  target = GST_CLOCK_ENTRY_TIME (entry) - current;

  GST_DEBUG (GST_CAT_CLOCK, "real_target %llu,  target %llu, now %llu", 
		  target, GST_CLOCK_ENTRY_TIME (entry), current); 
  
  if (((gint64)target) > 0) {
    struct timeval tv;

    GST_TIME_TO_TIMEVAL (target, tv);
    select (0, NULL, NULL, NULL, &tv);

    current = gst_clock_get_time (clock);
    this_jitter = current - GST_CLOCK_ENTRY_TIME (entry);
  }
  else {
    res = GST_CLOCK_EARLY;
    this_jitter = target;
  }

  if (jitter)
    *jitter = this_jitter;

  gst_clock_free_entry (clock, entry);

  return res;
}

/**
 * gst_clock_get_next_id
 * @clock: The clock to query
 *
 * Get the clockid of the next event.
 *
 * Returns: a clockid or NULL is no event is pending.
 */
GstClockID
gst_clock_get_next_id (GstClock *clock)
{
  GstClockEntry *entry = NULL;

  GST_LOCK (clock);
  if (clock->entries)
    entry = GST_CLOCK_ENTRY (clock->entries->data);
  GST_UNLOCK (clock);

  return (GstClockID *) entry;
}

/**
 * gst_clock_id_get_time
 * @id: The clockid to query
 *
 * Get the time of the clock ID
 *
 * Returns: the time of the given clock id
 */
GstClockTime
gst_clock_id_get_time (GstClockID id)
{
  return GST_CLOCK_ENTRY_TIME (id);
}

static void
gst_clock_free_entry (GstClock *clock, GstClockEntry *entry)
{
  gst_mem_chunk_free (_gst_clock_entries_chunk, entry);
}

/**
 * gst_clock_unlock_id
 * @clock: The clock that own the id
 * @id: The clockid to unlock
 *
 * Unlock the ClockID.
 */
void
gst_clock_unlock_id (GstClock *clock, GstClockID id)
{
  GstClockEntry *entry = (GstClockEntry *) id;

  if (entry->func)
    entry->func (clock, gst_clock_get_time (clock), id, entry->user_data);

  gst_clock_free_entry (clock, entry);
}

/**
 * gst_clock_set_resolution
 * @clock: The clock set the resolution on
 * @resolution: The resolution to set
 *
 * Set the accuracy of the clock.
 */
void
gst_clock_set_resolution (GstClock *clock, guint64 resolution)
{
  g_return_if_fail (GST_IS_CLOCK (clock));

  if (CLASS (clock)->set_resolution)
    CLASS (clock)->set_resolution (clock, resolution);
}

/**
 * gst_clock_get_resolution
 * @clock: The clock get the resolution of
 *
 * Get the accuracy of the clock.
 *
 * Returns: the resolution of the clock in microseconds.
 */
guint64
gst_clock_get_resolution (GstClock *clock)
{
  g_return_val_if_fail (GST_IS_CLOCK (clock), 0LL);

  if (CLASS (clock)->get_resolution)
    return CLASS (clock)->get_resolution (clock);

  return 1LL;
}

