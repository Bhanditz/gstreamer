/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstevent.h: Header for GstEvent subsystem
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

#include <string.h>		/* memcpy */

#include "gstinfo.h"
#include "gstdata_private.h"
#include "gstevent.h"
#include "gstlog.h"

/* #define MEMPROF */

GType _gst_event_type;

static gint _gst_event_live;

void
_gst_event_initialize (void)
{
  /* register the type */
  _gst_event_type = g_boxed_type_register_static ("GstEvent",
                                               (GBoxedCopyFunc) gst_data_ref,
                                               (GBoxedFreeFunc) gst_data_unref);
  _gst_event_live = 0;
}

/**
 * gst_event_print_stats:
 *
 * Logs statistics about live events (using g_log).
 */
void
gst_event_print_stats (void)
{
  g_log (g_log_domain_gstreamer, G_LOG_LEVEL_INFO,
                    "%d live event(s)", _gst_event_live);
}


static GstEvent*
_gst_event_copy (GstEvent *event)
{
  GstEvent *copy;

  copy = g_new0(GstEvent, 1);
  _gst_event_live++;

  memcpy (copy, event, sizeof (GstEvent));
  
  /* FIXME copy/ref additional fields */

  return copy;
}

static void
_gst_event_free (GstEvent* event)
{
  GST_INFO (GST_CAT_EVENT, "freeing event %p", event);

  if (GST_EVENT_SRC (event)) {
    gst_object_unref (GST_EVENT_SRC (event));
  }
  switch (GST_EVENT_TYPE (event)) {
    default:
      break;
  }
  _GST_DATA_DISPOSE (GST_DATA (event));
  _gst_event_live--;
  g_free (event);
}

/**
 * gst_event_masks_contains:
 * @masks: The eventmask array to search
 * @mask: the event mask to find
 *
 * See if the given eventmask is inside the eventmask array.
 *
 * Returns: TRUE if the eventmask is found inside the array
 */
gboolean
gst_event_masks_contains (const GstEventMask *masks, GstEventMask *mask)
{
  g_return_val_if_fail (mask != NULL, FALSE);

  if (!masks)
    return FALSE;
  
  while (masks->type) {
    if (masks->type == mask->type &&
        (masks->flags & mask->flags) == mask->flags)
      return TRUE;

    masks++;
  }

  return FALSE;
}

/**
 * gst_event_new:
 * @type: The type of the new event
 *
 * Allocate a new event of the given type.
 *
 * Returns: A new event.
 */
GstEvent*
gst_event_new (GstEventType type)
{
  GstEvent *event;

  event = g_new0(GstEvent, 1);
  _gst_event_live++;
  GST_INFO (GST_CAT_EVENT, "creating new event %p %d", event, type);

  _GST_DATA_INIT (GST_DATA (event),
		  _gst_event_type,
		  0,
		  (GstDataFreeFunction) _gst_event_free,
		  (GstDataCopyFunction) _gst_event_copy);

  GST_EVENT_TYPE (event) = type;
  GST_EVENT_TIMESTAMP (event) = 0LL;
  GST_EVENT_SRC (event) = NULL;

  return event;
}

/**
 * gst_event_new_seek:
 * @type: The type of the seek event
 * @offset: The offset of the seek
 *
 * Allocate a new seek event with the given parameters.
 *
 * Returns: A new seek event.
 */
GstEvent*       
gst_event_new_seek (GstSeekType type, gint64 offset)
{
  GstEvent *event;

  event = gst_event_new (GST_EVENT_SEEK);

  GST_EVENT_SEEK_TYPE (event) = type;
  GST_EVENT_SEEK_OFFSET (event) = offset;
  GST_EVENT_SEEK_ENDOFFSET (event) = -1;

  return event;
}

/**
 * gst_event_new_discontinuous:
 * @new_media: A flag indicating a new media type starts
 * @format1: The format of the discont value
 * @...: more discont values and formats
 *
 * Allocate a new discontinuous event with the geven format/value pairs.
 *
 * Returns: A new discontinuous event.
 */
GstEvent*
gst_event_new_discontinuous (gboolean new_media, GstFormat format1, ...)
{
  va_list var_args;
  GstEvent *event;
  gint count = 0;

  event = gst_event_new (GST_EVENT_DISCONTINUOUS);
  GST_EVENT_DISCONT_NEW_MEDIA (event) = new_media;

  va_start (var_args, format1);
	        
  while (format1) {

    GST_EVENT_DISCONT_OFFSET (event, count).format = format1 & GST_SEEK_FORMAT_MASK;
    GST_EVENT_DISCONT_OFFSET (event, count).value = va_arg (var_args, gint64);

    format1 = va_arg (var_args, GstSeekType);

    count++;
  }
  va_end (var_args);

  GST_EVENT_DISCONT_OFFSET_LEN (event) = count;
		    
  return event;
}

/**
 * gst_event_discont_get_value:
 * @event: The event to query
 * @format: The format of the discont value
 * @value: A pointer to the value
 *
 * Get the value for the given format in the dicont event.
 *
 * Returns: TRUE if the discont event caries the specified format/value pair.
 */
gboolean
gst_event_discont_get_value (GstEvent *event, GstFormat format, gint64 *value)
{
  gint i, n;

  g_return_val_if_fail (event != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  n = GST_EVENT_DISCONT_OFFSET_LEN (event);

  for (i = 0; i < n; i++) {
    if (GST_EVENT_DISCONT_OFFSET(event,i).format == format) {
      *value = GST_EVENT_DISCONT_OFFSET(event,i).value;
      return TRUE;
    }
  }
  
  return FALSE;
}


/**
 * gst_event_new_size:
 * @format: The format of the size value
 * @value: The value of the size event
 *
 * Create a new size event with the given values.
 *
 * Returns: The new size event.
 */
GstEvent*
gst_event_new_size (GstFormat format, gint64 value)
{
  GstEvent *event;

  event = gst_event_new (GST_EVENT_SIZE);

  GST_EVENT_SIZE_FORMAT (event) = format;
  GST_EVENT_SIZE_VALUE (event) = value;
  
  return event;
}


/**
 * gst_event_new_segment_seek:
 * @type: The type of the seek event
 * @start: The start offset of the seek
 * @stop: The stop offset of the seek
 *
 * Allocate a new segment seek event with the given parameters. 
 *
 * Returns: A new segment seek event.
 */
GstEvent*       
gst_event_new_segment_seek (GstSeekType type, gint64 start, gint64 stop)
{
  GstEvent *event;

  g_return_val_if_fail (start < stop, NULL);

  event = gst_event_new (GST_EVENT_SEEK_SEGMENT);

  GST_EVENT_SEEK_TYPE (event) = type;
  GST_EVENT_SEEK_OFFSET (event) = start;
  GST_EVENT_SEEK_ENDOFFSET (event) = stop;

  return event;
}

