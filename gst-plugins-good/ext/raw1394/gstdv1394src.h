/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_GST1394_H__
#define __GST_GST1394_H__


#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>

#include <libraw1394/raw1394.h>

G_BEGIN_DECLS

#define GST_TYPE_DV1394SRC \
  (gst_dv1394src_get_type())
#define GST_DV1394SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DV1394SRC,GstDV1394Src))
#define GST_DV1394SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DV1394SRC,GstDV1394Src))
#define GST_IS_DV1394SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DV1394SRC))
#define GST_IS_DV1394SRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DV1394SRC))

typedef struct _GstDV1394Src GstDV1394Src;
typedef struct _GstDV1394SrcClass GstDV1394SrcClass;

#define GST_DV_GET_LOCK(dv)	(GST_DV1394SRC (dv)->dv_lock)
#define GST_DV_LOCK(dv)		g_mutex_lock(GST_DV_GET_LOCK (dv))
#define GST_DV_UNLOCK(dv)	g_mutex_unlock(GST_DV_GET_LOCK (dv))

struct _GstDV1394Src {
  GstPushSrc element;

  // consecutive=2, skip=4 will skip 4 frames, then let 2 consecutive ones thru
  gint consecutive;
  gint skip;
  gboolean drop_incomplete;

  GMutex *dv_lock;

  gint num_ports;
  gint port;
  gint channel;
  octlet_t guid;
  gint avc_node;
  gboolean use_avc;

  struct raw1394_portinfo pinfo[16];
  raw1394handle_t handle;

  GstBuffer *buf;
  
  GstBuffer *frame;
  guint frame_size;
  guint frame_rate;
  guint bytes_in_frame;
  guint frame_sequence;

  gboolean negotiated;

  gchar *uri;
};

struct _GstDV1394SrcClass {
  GstPushSrcClass parent_class;

  /* signal */
  void (*frame_dropped)  (GstElement *elem);
};

GType gst_dv1394src_get_type(void);

G_END_DECLS

#endif /* __GST_GST1394_H__ */
