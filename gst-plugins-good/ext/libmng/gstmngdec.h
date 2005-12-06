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


#ifndef __GST_MNGDEC_H__
#define __GST_MNGDEC_H__

#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>
#include <libmng.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_MNGDEC         (gst_mngdec_get_type())
#define GST_MNGDEC(obj)         (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MNGDEC,GstMngDec))
#define GST_MNGDEC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MNGDEC,GstMngDec))
#define GST_IS_MNGDEC(obj)      (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MNGDEC))
#define GST_IS_MNGDEC_CLASS(obj)(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MNGDEC))

typedef struct _GstMngDec GstMngDec;
typedef struct _GstMngDecClass GstMngDecClass;

struct _GstMngDec
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  GstBuffer *buffer_out;
  GstByteStream *bs;

  mng_handle mng;
  gboolean first;

  gint width;
  gint stride;
  gint height;
  gint bpp;
  gint color_type;
  gdouble fps;
};

struct _GstMngDecClass
{
  GstElementClass parent_class;
};

GType gst_mngdec_get_type(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_MNGDEC_H__ */
