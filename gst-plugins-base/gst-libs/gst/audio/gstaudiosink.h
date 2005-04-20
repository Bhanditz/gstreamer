/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gstaudiosink.h: 
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

/* a base class for simple audio sinks.
 *
 * This base class only requires subclasses to implement a set
 * of simple functions.
 *
 * - open: open the device with the specified caps
 * - write: write the samples to the audio device
 * - close: close the device
 * - delay: the number of samples queued in the device
 * - reset: unblock a write to the device and reset.
 *
 * All scheduling of samples and timestamps is done in this
 * base class together with the GstBaseAudioSink using a 
 * default implementation of a ringbuffer that uses threads.
 */

#ifndef __GST_AUDIOSINK_H__
#define __GST_AUDIOSINK_H__

#include <gst/gst.h>
#include "gstbaseaudiosink.h"

G_BEGIN_DECLS

#define GST_TYPE_AUDIOSINK  	 	(gst_audiosink_get_type())
#define GST_AUDIOSINK(obj) 	 	(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIOSINK,GstAudioSink))
#define GST_AUDIOSINK_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIOSINK,GstAudioSinkClass))
#define GST_AUDIOSINK_GET_CLASS(obj) 	(G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_AUDIOSINK,GstAudioSinkClass))
#define GST_IS_AUDIOSINK(obj)  	 	(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIOSINK))
#define GST_IS_AUDIOSINK_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIOSINK))

typedef struct _GstAudioSink GstAudioSink;
typedef struct _GstAudioSinkClass GstAudioSinkClass;

struct _GstAudioSink {
  GstBaseAudioSink 	 element;

  /*< private >*/ /* with LOCK */
  GThread   *thread;
};

struct _GstAudioSinkClass {
  GstBaseAudioSinkClass parent_class;

  /* vtable */

  /* open the device with given specs */
  gboolean (*open)   (GstAudioSink *sink, GstRingBufferSpec *spec);
  /* close the device */
  gboolean (*close)  (GstAudioSink *sink);
  /* write samples to the device */
  guint    (*write)  (GstAudioSink *sink, gpointer data, guint length);
  /* get number of samples queued in the device */
  guint    (*delay)  (GstAudioSink *sink);
  /* reset the audio device, unblock from a write */
  void     (*reset)  (GstAudioSink *sink);
};

GType gst_audiosink_get_type(void);

G_END_DECLS

#endif /* __GST_AUDIOSINK_H__ */
