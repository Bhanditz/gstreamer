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

#define DEFAULT_MAX_DIFF	(2 * GST_SECOND)

enum {
  ARG_0,
  ARG_STATS,
  ARG_MAX_DIFF,
};

static GstMemChunk *_gst_clock_entries_chunk;

static void		gst_clock_class_init		(GstClockClass *klass);
static void		gst_clock_init			(GstClock *clock);
static void             gst_clock_set_property		(GObject *object, guint prop_id, 
							 const GValue *value, GParamSpec *pspec);
static void             gst_clock_get_property		(GObject *object, guint prop_id, 
							 GValue *value, GParamSpec * pspec);
static void		gst_clock_update_stats		(GstClock *clock);


static GstObjectClass *parent_class = NULL;
/* static guint gst_clock_signals[LAST_SIGNAL] = { 0 }; */

static GMutex *_gst_clock_mutex;
static GCond  *_gst_clock_cond;

static GstClockID
gst_clock_entry_new (GstClock *clock, GstClockTime time, 
		     GstClockTime interval, GstClockEntryType type)
{
  GstClockEntry *entry;

  entry = gst_mem_chunk_alloc (_gst_clock_entries_chunk);

  entry->clock = clock;
  entry->time = time;
  entry->interval = time;
  entry->type = type;
  entry->status = GST_CLOCK_ENTRY_OK;

  return (GstClockID) entry;
}

/**
 * gst_clock_new_single_shot_id
 * @clock: The clockid to get a single shot notification from
 * @time: the requested time
 *
 * Get an ID from the given clock to trigger a single shot 
 * notification at the requested time.
 *
 * Returns: An id that can be used to request the time notification.
 */
GstClockID
gst_clock_new_single_shot_id (GstClock *clock, GstClockTime time)
{
  g_return_val_if_fail (GST_IS_CLOCK (clock), NULL);

  return gst_clock_entry_new (clock, 
		  	      time, 
			      GST_CLOCK_TIME_NONE, 
			      GST_CLOCK_ENTRY_SINGLE);
}

/**
 * gst_clock_new_periodic_id
 * @clock: The clockid to get a periodic notification id from
 * @start_time: the requested start time
 * @interval: the requested interval
 *
 * Get an ID from the given clock to trigger a periodic notification.
 * The periodeic notifications will be start at time start_time and
 * will then be fired with the given interval.
 *
 * Returns: An id that can be used to request the time notification.
 */
GstClockID
gst_clock_new_periodic_id (GstClock *clock, GstClockTime start_time,
                           GstClockTime interval)
{
  g_return_val_if_fail (GST_IS_CLOCK (clock), NULL);
  g_return_val_if_fail (start_time != GST_CLOCK_TIME_NONE, NULL);
  g_return_val_if_fail (interval != 0, NULL);

  return gst_clock_entry_new (clock, 
		  	      start_time, 
			      interval, 
			      GST_CLOCK_ENTRY_PERIODIC);
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
  g_return_val_if_fail (id != NULL, GST_CLOCK_TIME_NONE);

  return GST_CLOCK_ENTRY_TIME ((GstClockEntry *)id);
}


/**
 * gst_clock_id_wait
 * @id: The clockid to wait on
 * @jitter: A pointer that will contain the jitter
 *
 * Perform a blocking wait on the given ID. The jitter arg can be
 * NULL
 *
 * Returns: the result of the blocking wait.
 */
GstClockReturn
gst_clock_id_wait (GstClockID id, GstClockTimeDiff *jitter)
{
  GstClockEntry *entry;
  GstClock *clock;
  GstClockReturn res = GST_CLOCK_UNSUPPORTED;
  GstClockTime requested;
  GstClockClass *cclass;
  
  g_return_val_if_fail (id != NULL, GST_CLOCK_ERROR);

  entry = (GstClockEntry *) id;
  requested = GST_CLOCK_ENTRY_TIME (entry);

  if (requested == GST_CLOCK_TIME_NONE) {
    return GST_CLOCK_TIMEOUT;
  }

  clock = GST_CLOCK_ENTRY_CLOCK (entry);
  cclass = GST_CLOCK_GET_CLASS (clock);
  
  if (cclass->wait) {
    GstClockTime now;
    
    GST_LOCK (clock);
    clock->entries = g_list_prepend (clock->entries, entry);
    GST_UNLOCK (clock);

    do {
      res = cclass->wait (clock, entry);
    }
    while (res == GST_CLOCK_ENTRY_RESTART);

    GST_LOCK (clock);
    clock->entries = g_list_remove (clock->entries, entry);
    GST_UNLOCK (clock);

    if (jitter) {
      now = gst_clock_get_time (clock);
      *jitter = now - requested;
    }

    if (clock->stats) {
      gst_clock_update_stats (clock);
    }
  }

  return res;
}

/**
 * gst_clock_id_wait_async:
 * @id: a #GstClockID to wait on
 * @func: The callback function 
 * @user_data: User data passed in the calback
 *
 * Register a callback on the given clockid with the given
 * function and user_data.
 *
 * Returns: the result of the non blocking wait.
 */
GstClockReturn
gst_clock_id_wait_async (GstClockID id,
		         GstClockCallback func, gpointer user_data)
{
  GstClockEntry *entry;
  GstClock *clock;
  GstClockReturn res = GST_CLOCK_UNSUPPORTED;
  GstClockClass *cclass;
  
  g_return_val_if_fail (id != NULL, GST_CLOCK_ERROR);
  g_return_val_if_fail (func != NULL, GST_CLOCK_ERROR);

  entry = (GstClockEntry *) id;
  clock = entry->clock;

  if (GST_CLOCK_ENTRY_TIME (entry) == GST_CLOCK_TIME_NONE) {
    (func) (clock, GST_CLOCK_TIME_NONE, id, user_data);
    return GST_CLOCK_TIMEOUT;
  }

  cclass = GST_CLOCK_GET_CLASS (clock);

  if (cclass->wait_async) {
    entry->func = func;
    entry->user_data = user_data;

    res = cclass->wait_async (clock, entry);
  }

  return res;
}

static void
gst_clock_reschedule_func (GstClockEntry *entry)
{
  entry->status = GST_CLOCK_ENTRY_OK;
  
  gst_clock_id_unlock ((GstClockID)entry);
}

/**
 * gst_clock_id_unschedule:
 * @id: The id to unschedule
 *
 * Cancel an outstanding async notification request with the given ID.
 */
void
gst_clock_id_unschedule (GstClockID id)
{
  GstClockEntry *entry;
  GstClock *clock;
  GstClockClass *cclass;
  
  g_return_if_fail (id != NULL);

  entry = (GstClockEntry *) id;
  clock = entry->clock;

  cclass = GST_CLOCK_GET_CLASS (clock);

  if (cclass->unschedule)
    cclass->unschedule (clock, entry);
}

/**
 * gst_clock_id_free:
 * @id: The clockid to free
 *
 * Free the resources held by the given id
 */
void
gst_clock_id_free (GstClockID id)
{
  g_return_if_fail (id != NULL);

  gst_mem_chunk_free (_gst_clock_entries_chunk, id);
}

/**
 * gst_clock_id_unlock:
 * @id: The clockid to unlock
 *
 * Unlock the givan ClockID.
 */
void
gst_clock_id_unlock (GstClockID id)
{
  GstClockEntry *entry;
  GstClock *clock;
  GstClockClass *cclass;
  
  g_return_if_fail (id != NULL);

  entry = (GstClockEntry *) id;
  clock = entry->clock;

  cclass = GST_CLOCK_GET_CLASS (clock);

  if (cclass->unlock)
    cclass->unlock (clock, entry);
}


/**
 * GstClock abstract base class implementation
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

  _gst_clock_mutex = g_mutex_new ();
  _gst_clock_cond  = g_cond_new ();

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_clock_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_clock_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_STATS,
    g_param_spec_boolean ("stats", "Stats", "Enable clock stats",
                          FALSE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MAX_DIFF,
    g_param_spec_int64 ("max-diff", "Max diff", "The maximum amount of time to wait in nanoseconds",
                        0, G_MAXINT64, DEFAULT_MAX_DIFF, G_PARAM_READWRITE));
}

static void
gst_clock_init (GstClock *clock)
{
  clock->max_diff = DEFAULT_MAX_DIFF;

  clock->speed = 1.0;
  clock->active = FALSE;
  clock->start_time = 0;
  clock->last_time = 0;
  clock->entries = NULL;
  clock->flags = 0;
  clock->stats = FALSE;

  clock->active_mutex = g_mutex_new ();
  clock->active_cond = g_cond_new ();
}

/**
 * gst_clock_set_speed
 * @clock: a #GstClock to modify
 * @speed: the speed to set on the clock
 *
 * Sets the speed on the given clock. 1.0 is the default 
 * speed.
 *
 * Returns: the new speed of the clock.
 */
gdouble
gst_clock_set_speed (GstClock *clock, gdouble speed)
{
  GstClockClass *cclass;

  g_return_val_if_fail (GST_IS_CLOCK (clock), 0.0);

  cclass = GST_CLOCK_GET_CLASS (clock);

  if (cclass->change_speed)
    clock->speed = cclass->change_speed (clock, clock->speed, speed);

  return clock->speed;
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
 * gst_clock_set_resolution
 * @clock: The clock set the resolution on
 * @resolution: The resolution to set
 *
 * Set the accuracy of the clock.
 *
 * Returns: the new resolution of the clock.
 */
guint64
gst_clock_set_resolution (GstClock *clock, guint64 resolution)
{
  GstClockClass *cclass;

  g_return_val_if_fail (GST_IS_CLOCK (clock), 0LL);
  g_return_val_if_fail (resolution != 0, 0LL);

  cclass = GST_CLOCK_GET_CLASS (clock);

  if (cclass->change_resolution)
    clock->resolution = cclass->change_resolution (clock, clock->resolution, resolution);

  return clock->resolution;
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
  GstClockClass *cclass;

  g_return_val_if_fail (GST_IS_CLOCK (clock), 0LL);

  cclass = GST_CLOCK_GET_CLASS (clock);

  if (cclass->get_resolution)
    return cclass->get_resolution (clock);

  return 1LL;
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
  GstClockClass *cclass;

  g_return_if_fail (GST_IS_CLOCK (clock));

  clock->active = active;

  cclass = GST_CLOCK_GET_CLASS (clock);
	        
  if (cclass->get_internal_time) {
    time = cclass->get_internal_time (clock);
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
  g_list_foreach (clock->entries, (GFunc) gst_clock_reschedule_func, NULL);
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
 * gst_clock_reset
 * @clock: a #GstClock to reset
 *
 * Reset the clock to time 0.
 */
void
gst_clock_reset (GstClock *clock)
{
  GstClockTime time = 0LL;
  GstClockClass *cclass;

  g_return_if_fail (GST_IS_CLOCK (clock));

  cclass = GST_CLOCK_GET_CLASS (clock);
	        
  if (cclass->get_internal_time) {
    time = cclass->get_internal_time (clock);
  }

  GST_LOCK (clock);
  clock->active = FALSE;
  clock->start_time = time;
  clock->last_time = 0LL;
  g_list_foreach (clock->entries, (GFunc) gst_clock_reschedule_func, NULL);
  GST_UNLOCK (clock);
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

  GST_DEBUG (GST_CAT_CLOCK, "clock discont %" G_GUINT64_FORMAT
		            " %" G_GUINT64_FORMAT " %d",
			    time, clock->start_time, clock->accept_discont);

  if (time == GST_CLOCK_TIME_NONE)
    return TRUE;

  GST_LOCK (clock);
  if (clock->accept_discont) {
    GstClockClass *cclass;

    cclass = GST_CLOCK_GET_CLASS (clock);
	  
    if (cclass->get_internal_time) {
      itime = cclass->get_internal_time (clock);
    }
  }
  else {
    GST_UNLOCK (clock);
    GST_DEBUG (GST_CAT_CLOCK, "clock discont refused %" G_GUINT64_FORMAT
		              " %" G_GUINT64_FORMAT,
			      time, clock->start_time);
    return FALSE;
  }

  clock->start_time = itime - time;
  clock->last_time = time;
  clock->accept_discont = FALSE;
  g_list_foreach (clock->entries, (GFunc) gst_clock_reschedule_func, NULL);
  GST_UNLOCK (clock);

  GST_DEBUG (GST_CAT_CLOCK, "new time %" G_GUINT64_FORMAT,
	     gst_clock_get_time (clock));

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
    GstClockClass *cclass;

    cclass = GST_CLOCK_GET_CLASS (clock);

    if (cclass->get_internal_time) {
      ret = cclass->get_internal_time (clock) - clock->start_time;
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

  g_return_val_if_fail (GST_IS_CLOCK (clock), NULL);

  GST_LOCK (clock);
  if (clock->entries)
    entry = GST_CLOCK_ENTRY (clock->entries->data);
  GST_UNLOCK (clock);

  return (GstClockID *) entry;
}

static void
gst_clock_update_stats (GstClock *clock)
{
}

static void
gst_clock_set_property (GObject *object, guint prop_id,
     		        const GValue *value, GParamSpec *pspec)
{
  GstClock *clock;
	     
  clock = GST_CLOCK (object);

  switch (prop_id) {
    case ARG_STATS:
      clock->stats = g_value_get_boolean (value);
      break;
    case ARG_MAX_DIFF:
      clock->max_diff = g_value_get_int64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
   }
}

static void
gst_clock_get_property (GObject *object, guint prop_id, 
			GValue *value, GParamSpec * pspec)
{
  GstClock *clock;
	     
  clock = GST_CLOCK (object);

  switch (prop_id) {
    case ARG_STATS:
      g_value_set_boolean (value, clock->stats);
      break;
    case ARG_MAX_DIFF:
      g_value_set_int64 (value, clock->max_diff);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
   }
}
