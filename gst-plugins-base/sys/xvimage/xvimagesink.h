/* GStreamer
 * Copyright (C) <2003> Julien Moutte <julien@moutte.net>
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

#ifndef __GST_XVIMAGESINK_H__
#define __GST_XVIMAGESINK_H__

#include <gst/video/videosink.h>

#ifdef HAVE_XSHM
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#endif /* HAVE_XSHM */

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#ifdef HAVE_XSHM
#include <X11/extensions/XShm.h>
#endif /* HAVE_XSHM */

#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>

#include <string.h>
#include <math.h>

G_BEGIN_DECLS

#define GST_TYPE_XVIMAGESINK \
  (gst_xvimagesink_get_type())
#define GST_XVIMAGESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_XVIMAGESINK, GstXvImageSink))
#define GST_XVIMAGESINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_XVIMAGESINK, GstXvImageSink))
#define GST_IS_XVIMAGESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_XVIMAGESINK))
#define GST_IS_XVIMAGESINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_XVIMAGESINK))

typedef struct _GstXContext GstXContext;
typedef struct _GstXWindow GstXWindow;
typedef struct _GstXvImage GstXvImage;
typedef struct _GstXvImageFormat GstXvImageFormat;

typedef struct _GstXvImageSink GstXvImageSink;
typedef struct _GstXvImageSinkClass GstXvImageSinkClass;

/* Global X Context stuff */
struct _GstXContext {
  Display *disp;

  Screen *screen;
  gint screen_num;

  Visual *visual;

  Window root;

  gulong white, black;

  gint depth;
  gint bpp;
  gint endianness;

  gint width, height;
  gint widthmm, heightmm;
  GValue *par;                  /* calculated pixel aspect ratio */

  gboolean use_xshm;

  XvPortID xv_port_id;
  gint im_format;

  GList *formats_list;
  GList *channels_list;

  GstCaps *caps;
};

/* XWindow stuff */
struct _GstXWindow {
  Window win;
  gint width, height;
  gboolean internal;
  GC gc;
};

/* XvImage format stuff */
struct _GstXvImageFormat {
  gint format;
  GstCaps *caps;
};

/* XvImage stuff */
struct _GstXvImage {
  /* Reference to the xvimagesink we belong to */
  GstXvImageSink *xvimagesink;

  XvImage *xvimage;

#ifdef HAVE_XSHM
  XShmSegmentInfo SHMInfo;
#endif /* HAVE_XSHM */

  gint width, height, size, im_format;
};

struct _GstXvImageSink {
  /* Our element stuff */
  GstVideoSink videosink;

  char *display_name;

  GstXContext *xcontext;
  GstXWindow *xwindow;
  GstXvImage *xvimage;
  GstXvImage *cur_image;

  gdouble framerate;

  gint brightness;
  gint contrast;
  gint hue;
  gint saturation;
  gboolean cb_changed;

  GMutex *x_lock;
  GMutex *stream_lock;

  guint video_width, video_height;     /* size of incoming video;
                                        * used as the size for XvImage */
  GValue *par;                         /* object-set pixel aspect ratio */

  GstClockTime time;

  GMutex *pool_lock;
  GSList *image_pool;

  gboolean synchronous;
};

struct _GstXvImageSinkClass {
  GstVideoSinkClass parent_class;
};

GType gst_xvimagesink_get_type(void);

G_END_DECLS

#endif /* __GST_XVIMAGESINK_H__ */
