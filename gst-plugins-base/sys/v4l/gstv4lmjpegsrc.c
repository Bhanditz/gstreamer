/* G-Streamer hardware MJPEG video source plugin
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include "v4lmjpegsrc_calls.h"

/* elementfactory information */
static GstElementDetails gst_v4lmjpegsrc_details = {
  "Video (video4linux/MJPEG) Source",
  "Source/Video",
  "LGPL",
  "Reads MJPEG-encoded frames from a zoran MJPEG/video4linux device",
  VERSION,
  "Ronald Bultje <rbultje@ronald.bitfreak.net>",
  "(C) 2001",
};

/* V4lMjpegSrc signals and args */
enum {
  SIGNAL_FRAME_CAPTURE,
  SIGNAL_FRAME_DROP,
  SIGNAL_FRAME_INSERT,
  SIGNAL_FRAME_LOST,
  LAST_SIGNAL
};

/* arguments */
enum {
  ARG_0,
#if 0
  ARG_X_OFFSET,
  ARG_Y_OFFSET,
  ARG_F_WIDTH,
  ARG_F_HEIGHT,
  /* normally, we would want to use subframe capture, however,
   * for the time being it's easier if we disable it first */
#endif
  ARG_QUALITY,
  ARG_NUMBUFS,
  ARG_BUFSIZE,
  ARG_USE_FIXED_FPS
};


/* init functions */
static void                  gst_v4lmjpegsrc_class_init   (GstV4lMjpegSrcClass *klass);
static void                  gst_v4lmjpegsrc_init         (GstV4lMjpegSrc *v4lmjpegsrc);

/* pad/buffer functions */
static gboolean              gst_v4lmjpegsrc_srcconvert   (GstPad         *pad,
                                                           GstFormat      src_format,
                                                           gint64         src_value,
                                                           GstFormat      *dest_format,
                                                           gint64         *dest_value);
static GstPadLinkReturn      gst_v4lmjpegsrc_srcconnect   (GstPad         *pad,
                                                           GstCaps        *caps);
static GstBuffer*            gst_v4lmjpegsrc_get          (GstPad         *pad);
static GstCaps*              gst_v4lmjpegsrc_getcaps      (GstPad         *pad,
                                                           GstCaps        *caps);

/* get/set params */
static void                  gst_v4lmjpegsrc_set_property (GObject        *object,
                                                           guint          prop_id,
                                                           const GValue   *value,
                                                           GParamSpec     *pspec);
static void                  gst_v4lmjpegsrc_get_property (GObject        *object,
                                                           guint          prop_id,
                                                           GValue         *value,
                                                           GParamSpec     *pspec);

/* set_clock function for A/V sync */
static void                  gst_v4lmjpegsrc_set_clock    (GstElement     *element,
                                                           GstClock       *clock);

/* state handling */
static GstElementStateReturn gst_v4lmjpegsrc_change_state (GstElement     *element);

/* bufferpool functions */
static GstBuffer*            gst_v4lmjpegsrc_buffer_new   (GstBufferPool  *pool,
                                                           guint64        location,
                                                           guint          size,
                                                           gpointer       user_data);
static void                  gst_v4lmjpegsrc_buffer_free  (GstBufferPool  *pool,
							   GstBuffer      *buf,
                                                           gpointer       user_data);


static GstCaps *capslist = NULL;
static GstPadTemplate *src_template;

static GstElementClass *parent_class = NULL;
static guint gst_v4lmjpegsrc_signals[LAST_SIGNAL] = { 0 };


GType
gst_v4lmjpegsrc_get_type (void)
{
  static GType v4lmjpegsrc_type = 0;

  if (!v4lmjpegsrc_type) {
    static const GTypeInfo v4lmjpegsrc_info = {
      sizeof(GstV4lMjpegSrcClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_v4lmjpegsrc_class_init,
      NULL,
      NULL,
      sizeof(GstV4lMjpegSrc),
      0,
      (GInstanceInitFunc)gst_v4lmjpegsrc_init,
      NULL
    };
    v4lmjpegsrc_type = g_type_register_static(GST_TYPE_V4LELEMENT, "GstV4lMjpegSrc", &v4lmjpegsrc_info, 0);
  }
  return v4lmjpegsrc_type;
}


static void
gst_v4lmjpegsrc_class_init (GstV4lMjpegSrcClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_V4LELEMENT);

#if 0
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_X_OFFSET,
    g_param_spec_int("x_offset","x_offset","x_offset",
                     G_MININT,G_MAXINT,0,G_PARAM_WRITABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_Y_OFFSET,
    g_param_spec_int("y_offset","y_offset","y_offset",
                     G_MININT,G_MAXINT,0,G_PARAM_WRITABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_F_WIDTH,
    g_param_spec_int("frame_width","frame_width","frame_width",
                     G_MININT,G_MAXINT,0,G_PARAM_WRITABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_F_HEIGHT,
    g_param_spec_int("frame_height","frame_height","frame_height",
                     G_MININT,G_MAXINT,0,G_PARAM_WRITABLE));
#endif

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_QUALITY,
    g_param_spec_int("quality","quality","quality",
                     G_MININT,G_MAXINT,0,G_PARAM_WRITABLE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_NUMBUFS,
    g_param_spec_int("num_buffers","Num Buffers","Number of Buffers",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BUFSIZE,
    g_param_spec_int("buffer_size", "Buffer Size", "Size of buffers",
                     0, G_MAXINT, 0, G_PARAM_READABLE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_USE_FIXED_FPS,
    g_param_spec_boolean("use_fixed_fps", "Use Fixed FPS",
                         "Drop/Insert frames to reach a certain FPS (TRUE) "
                         "or adapt FPS to suit the number of frabbed frames",
                         TRUE, G_PARAM_READWRITE));

  /* signals */
  gst_v4lmjpegsrc_signals[SIGNAL_FRAME_CAPTURE] =
    g_signal_new("frame_capture", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                 G_STRUCT_OFFSET(GstV4lMjpegSrcClass, frame_capture),
                 NULL, NULL, g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);
  gst_v4lmjpegsrc_signals[SIGNAL_FRAME_DROP] =
    g_signal_new("frame_drop", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                 G_STRUCT_OFFSET(GstV4lMjpegSrcClass, frame_drop),
                 NULL, NULL, g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);
  gst_v4lmjpegsrc_signals[SIGNAL_FRAME_INSERT] =
    g_signal_new("frame_insert", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                 G_STRUCT_OFFSET(GstV4lMjpegSrcClass, frame_insert),
                 NULL, NULL, g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);
  gst_v4lmjpegsrc_signals[SIGNAL_FRAME_LOST] =
    g_signal_new("frame_lost", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                 G_STRUCT_OFFSET(GstV4lMjpegSrcClass, frame_lost),
                 NULL, NULL, g_cclosure_marshal_VOID__INT,
                 G_TYPE_NONE, 1, G_TYPE_INT);

  gobject_class->set_property = gst_v4lmjpegsrc_set_property;
  gobject_class->get_property = gst_v4lmjpegsrc_get_property;

  gstelement_class->change_state = gst_v4lmjpegsrc_change_state;

  gstelement_class->set_clock = gst_v4lmjpegsrc_set_clock;
}


static void
gst_v4lmjpegsrc_init (GstV4lMjpegSrc *v4lmjpegsrc)
{
  GST_FLAG_SET(GST_ELEMENT(v4lmjpegsrc), GST_ELEMENT_THREAD_SUGGESTED);

  v4lmjpegsrc->srcpad = gst_pad_new_from_template (src_template, "src");
  gst_element_add_pad(GST_ELEMENT(v4lmjpegsrc), v4lmjpegsrc->srcpad);

  gst_pad_set_get_function (v4lmjpegsrc->srcpad, gst_v4lmjpegsrc_get);
  gst_pad_set_getcaps_function (v4lmjpegsrc->srcpad, gst_v4lmjpegsrc_getcaps);
  gst_pad_set_link_function (v4lmjpegsrc->srcpad, gst_v4lmjpegsrc_srcconnect);
  gst_pad_set_convert_function (v4lmjpegsrc->srcpad, gst_v4lmjpegsrc_srcconvert);

  v4lmjpegsrc->bufferpool = gst_buffer_pool_new(
		  			NULL,
					NULL,
					(GstBufferPoolBufferNewFunction)gst_v4lmjpegsrc_buffer_new,
					NULL,
					(GstBufferPoolBufferFreeFunction)gst_v4lmjpegsrc_buffer_free,
					v4lmjpegsrc);

#if 0
  v4lmjpegsrc->frame_width = 0;
  v4lmjpegsrc->frame_height = 0;
  v4lmjpegsrc->x_offset = -1;
  v4lmjpegsrc->y_offset = -1;
#endif

  v4lmjpegsrc->quality = 50;

  v4lmjpegsrc->numbufs = 64;

  /* no clock */
  v4lmjpegsrc->clock = NULL;

  /* fps */
  v4lmjpegsrc->use_fixed_fps = TRUE;
}


static gdouble
gst_v4lmjpegsrc_get_fps (GstV4lMjpegSrc *v4lmjpegsrc)
{
  gint norm;
  gdouble fps;
 
  if (!v4lmjpegsrc->use_fixed_fps &&
      v4lmjpegsrc->clock != NULL &&
      v4lmjpegsrc->handled > 0) {
    /* try to get time from clock master and calculate fps */
    GstClockTime time = gst_clock_get_time(v4lmjpegsrc->clock) - v4lmjpegsrc->substract_time;
    return v4lmjpegsrc->handled * GST_SECOND / time;
  }

  /* if that failed ... */

  if (!GST_V4L_IS_OPEN(GST_V4LELEMENT(v4lmjpegsrc)))
    return 0.;

  if (!gst_v4l_get_chan_norm(GST_V4LELEMENT(v4lmjpegsrc), NULL, &norm))
    return 0.;

  if (norm == VIDEO_MODE_NTSC)
    fps = 30000/1001;
  else
    fps = 25.;

  return fps;
}


static gboolean
gst_v4lmjpegsrc_srcconvert (GstPad    *pad,
                            GstFormat  src_format,
                            gint64     src_value,
                            GstFormat *dest_format,
                            gint64    *dest_value)
{
  GstV4lMjpegSrc *v4lmjpegsrc;
  gdouble fps;

  v4lmjpegsrc = GST_V4LMJPEGSRC (gst_pad_get_parent (pad));

  if ((fps = gst_v4lmjpegsrc_get_fps(v4lmjpegsrc)) == 0)
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_UNITS;
          /* fall-through */
        case GST_FORMAT_UNITS:
          *dest_value = src_value * fps / GST_SECOND;
          break;
        default:
          return FALSE;
      }
      break;

    case GST_FORMAT_UNITS:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_TIME;
          /* fall-through */
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND / fps;
          break;
        default:
          return FALSE;
      }
      break;

    default:
      return FALSE;
  }

  return TRUE;
}


static inline gulong
calc_bufsize (int hor_dec,
              int ver_dec)
{
        guint8 div = hor_dec * ver_dec;
        guint32 num = (1024 * 512) / (div);
        guint32 result = 2;
                                                                                
        num--;
        while (num) {
                num >>= 1;
                result <<= 1;
        }
                                                                                
        if (result > (512 * 1024))
                return (512 * 1024);
        if (result < 8192)
                return 8192;
        return result;
}

#define gst_caps_get_int_range(caps, name, min, max) \
  gst_props_entry_get_int_range(gst_props_get_entry((caps)->properties, \
                                                    name), \
                                min, max)


static GstPadLinkReturn
gst_v4lmjpegsrc_srcconnect (GstPad  *pad,
                            GstCaps *caps)
{
  GstPadLinkReturn ret_val;
  GstV4lMjpegSrc *v4lmjpegsrc = GST_V4LMJPEGSRC(gst_pad_get_parent(pad));
  gint hor_dec, ver_dec;
  gint w, h;
  gint max_w = GST_V4LELEMENT(v4lmjpegsrc)->vcap.maxwidth,
       max_h = GST_V4LELEMENT(v4lmjpegsrc)->vcap.maxheight;
  gulong bufsize;

  /* in case the buffers are active (which means that we already
   * did capsnego before and didn't clean up), clean up anyways */
  if (GST_V4L_IS_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc)))
  {
    if (!gst_v4lmjpegsrc_capture_deinit(v4lmjpegsrc))
      return GST_PAD_LINK_REFUSED;
  }
  else if (!GST_V4L_IS_OPEN(GST_V4LELEMENT(v4lmjpegsrc)))
  {
    return GST_PAD_LINK_DELAYED;
  }

  /* Note: basically, we don't give a damn about the opposite caps here.
   * that might seem odd, but it isn't. we know that the opposite caps is
   * either NULL or has mime type video/jpeg, and in both cases, we'll set
   * our own mime type back and it'll work. Other properties are to be set
   * by the src, not by the opposite caps */

  if (gst_caps_has_property(caps, "width")) {
    if (gst_caps_has_fixed_property(caps, "width")) {
      gst_caps_get_int(caps, "width", &w);
    } else {
      int max;
      gst_caps_get_int_range(caps, "width",  &max, &w);
    }
  }
  if (gst_caps_has_property(caps, "height")) {
    if (gst_caps_has_fixed_property(caps, "height")) {
      gst_caps_get_int(caps, "height", &h);
    } else {
      int max;
      gst_caps_get_int_range(caps, "height", &max, &h);
    }
  }

  /* figure out decimation */
  if (w >= max_w) {
    hor_dec = 1;
  } else if (w*2 >= max_w) {
    hor_dec = 2;
  } else {
    hor_dec = 4;
  }
  if (h >= max_h) {
    ver_dec = 1;
  } else if (h*2 >= max_h) {
    ver_dec = 2;
  } else {
    ver_dec = 4;
  }

  /* calculate bufsize */
  bufsize = calc_bufsize(hor_dec, ver_dec);

  /* set buffer info */
  if (!gst_v4lmjpegsrc_set_buffer(v4lmjpegsrc,
                                  v4lmjpegsrc->numbufs, bufsize)) {
    return GST_PAD_LINK_REFUSED;
  }

  /* set capture parameters and mmap the buffers */
  if (hor_dec == ver_dec) {
    if (!gst_v4lmjpegsrc_set_capture(v4lmjpegsrc,
                                     hor_dec,
                                     v4lmjpegsrc->quality)) {
      return GST_PAD_LINK_REFUSED;
    }
  } else {
    if (!gst_v4lmjpegsrc_set_capture_m(v4lmjpegsrc,
                                       0, 0, max_w, max_h,
                                       hor_dec, ver_dec,
                                       v4lmjpegsrc->quality)) {
      return GST_PAD_LINK_REFUSED;
    }
  }
#if 0
  if (!v4lmjpegsrc->frame_width && !v4lmjpegsrc->frame_height &&
       v4lmjpegsrc->x_offset < 0 && v4lmjpegsrc->y_offset < 0 &&
       v4lmjpegsrc->horizontal_decimation == v4lmjpegsrc->vertical_decimation)
  {
    if (!gst_v4lmjpegsrc_set_capture(v4lmjpegsrc,
        v4lmjpegsrc->horizontal_decimation, v4lmjpegsrc->quality))
      return GST_PAD_LINK_REFUSED;
  }
  else
  {
    if (!gst_v4lmjpegsrc_set_capture_m(v4lmjpegsrc,
         v4lmjpegsrc->x_offset, v4lmjpegsrc->y_offset,
         v4lmjpegsrc->frame_width, v4lmjpegsrc->frame_height,
         v4lmjpegsrc->horizontal_decimation, v4lmjpegsrc->vertical_decimation,
         v4lmjpegsrc->quality))
      return GST_PAD_LINK_REFUSED;
  }
#endif

  /* we now have an actual width/height - *set it* */
  caps = gst_caps_new("v4lmjpegsrc_caps",
                      "video/jpeg",
                      gst_props_new(
                        "width",  GST_PROPS_INT(v4lmjpegsrc->end_width),
                        "height", GST_PROPS_INT(v4lmjpegsrc->end_height),
                        NULL       ) );
  if ((ret_val = gst_pad_try_set_caps(v4lmjpegsrc->srcpad, caps)) == GST_PAD_LINK_REFUSED)
    return GST_PAD_LINK_REFUSED;
  else if (ret_val == GST_PAD_LINK_DELAYED)
    return GST_PAD_LINK_DELAYED;

  if (!gst_v4lmjpegsrc_capture_init(v4lmjpegsrc))
    return GST_PAD_LINK_REFUSED;

  return GST_PAD_LINK_DONE;
}


static GstBuffer*
gst_v4lmjpegsrc_get (GstPad *pad)
{
  GstV4lMjpegSrc *v4lmjpegsrc;
  GstBuffer *buf;
  gint num;
  gdouble fps = 0;

  g_return_val_if_fail (pad != NULL, NULL);

  v4lmjpegsrc = GST_V4LMJPEGSRC (gst_pad_get_parent (pad));

  if (v4lmjpegsrc->use_fixed_fps &&
      (fps = gst_v4lmjpegsrc_get_fps(v4lmjpegsrc)) == 0)
    return NULL;

  buf = gst_buffer_new_from_pool(v4lmjpegsrc->bufferpool, 0, 0);
  if (!buf)
  {
    gst_element_error(GST_ELEMENT(v4lmjpegsrc),
      "Failed to create a new GstBuffer");
    return NULL;
  }

  if (v4lmjpegsrc->need_writes > 0) {
    /* use last frame */
    num = v4lmjpegsrc->last_frame;
    v4lmjpegsrc->need_writes--;
  } else if (v4lmjpegsrc->clock && v4lmjpegsrc->use_fixed_fps) {
    GstClockTime time;
    gboolean have_frame = FALSE;

    do {
      /* by default, we use the frame once */
      v4lmjpegsrc->need_writes = 1;

      /* grab a frame from the device */
      if (!gst_v4lmjpegsrc_grab_frame(v4lmjpegsrc, &num, &v4lmjpegsrc->last_size))
        return NULL;

      v4lmjpegsrc->last_frame = num;
      time = GST_TIMEVAL_TO_TIME(v4lmjpegsrc->bsync.timestamp) -
               v4lmjpegsrc->substract_time;

      /* first check whether we lost any frames according to the device */
      if (v4lmjpegsrc->last_seq != 0) {
        if (v4lmjpegsrc->bsync.seq - v4lmjpegsrc->last_seq > 1) {
          v4lmjpegsrc->need_writes = v4lmjpegsrc->bsync.seq - v4lmjpegsrc->last_seq;
          g_signal_emit(G_OBJECT(v4lmjpegsrc),
                        gst_v4lmjpegsrc_signals[SIGNAL_FRAME_LOST], 0,
                        v4lmjpegsrc->bsync.seq - v4lmjpegsrc->last_seq - 1);
        }
      }
      v4lmjpegsrc->last_seq = v4lmjpegsrc->bsync.seq;

      /* decide how often we're going to write the frame - set
       * v4lmjpegsrc->need_writes to (that-1) and have_frame to TRUE
       * if we're going to write it - else, just continue.
       * 
       * time is generally the system or audio clock. Let's
       * say that we've written one second of audio, then we want
       * to have written one second of video too, within the same
       * timeframe. This means that if time - begin_time = X sec,
       * we want to have written X*fps frames. If we've written
       * more - drop, if we've written less - dup... */
      if (v4lmjpegsrc->handled * (GST_SECOND/fps) - time > 1.5 * (GST_SECOND/fps)) {
        /* yo dude, we've got too many frames here! Drop! DROP! */
        v4lmjpegsrc->need_writes--; /* -= (v4lmjpegsrc->handled - (time / fps)); */
        g_signal_emit(G_OBJECT(v4lmjpegsrc),
                      gst_v4lmjpegsrc_signals[SIGNAL_FRAME_DROP], 0);
      } else if (v4lmjpegsrc->handled * (GST_SECOND/fps) - time < - 1.5 * (GST_SECOND/fps)) {
        /* this means we're lagging far behind */
        v4lmjpegsrc->need_writes++; /* += ((time / fps) - v4lmjpegsrc->handled); */
        g_signal_emit(G_OBJECT(v4lmjpegsrc),
                      gst_v4lmjpegsrc_signals[SIGNAL_FRAME_INSERT], 0);
      }

      if (v4lmjpegsrc->need_writes > 0) {
        have_frame = TRUE;
        v4lmjpegsrc->use_num_times[num] = v4lmjpegsrc->need_writes;
        v4lmjpegsrc->need_writes--;
      } else {
        gst_v4lmjpegsrc_requeue_frame(v4lmjpegsrc, num);
      }
    } while (!have_frame);
  } else {
    /* grab a frame from the device */
    if (!gst_v4lmjpegsrc_grab_frame(v4lmjpegsrc, &num, &v4lmjpegsrc->last_size))
      return NULL;

    v4lmjpegsrc->use_num_times[num] = 1;
  }

  GST_BUFFER_DATA(buf) = gst_v4lmjpegsrc_get_buffer(v4lmjpegsrc, num);
  GST_BUFFER_SIZE(buf) = v4lmjpegsrc->last_size;
  if (v4lmjpegsrc->use_fixed_fps)
    GST_BUFFER_TIMESTAMP(buf) = v4lmjpegsrc->handled * GST_SECOND / fps;
  else /* calculate time based on our own clock */
    GST_BUFFER_TIMESTAMP(buf) = GST_TIMEVAL_TO_TIME(v4lmjpegsrc->bsync.timestamp) -
                                  v4lmjpegsrc->substract_time;

  v4lmjpegsrc->handled++;
  g_signal_emit(G_OBJECT(v4lmjpegsrc),
                gst_v4lmjpegsrc_signals[SIGNAL_FRAME_CAPTURE], 0);

  return buf;
}


static GstCaps*
gst_v4lmjpegsrc_getcaps (GstPad  *pad,
                         GstCaps *_caps)
{
  GstV4lMjpegSrc *v4lmjpegsrc = GST_V4LMJPEGSRC(gst_pad_get_parent(pad));
  struct video_capability *vcap = &GST_V4LELEMENT(v4lmjpegsrc)->vcap;
  GstCaps *caps;

  if (!GST_V4L_IS_OPEN(GST_V4LELEMENT(v4lmjpegsrc))) {
    return NULL;
  }

  caps = GST_CAPS_NEW("v4lmjpegsrc_jpeg_caps",
                      "video/jpeg",
                        "width",  GST_PROPS_INT_RANGE(vcap->maxwidth/4,
                                                      vcap->maxwidth),
                        "height", GST_PROPS_INT_RANGE(vcap->maxheight/4,
                                                      vcap->maxheight),
                        NULL);

  return caps;
}


static void
gst_v4lmjpegsrc_set_property (GObject      *object,
                              guint        prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  GstV4lMjpegSrc *v4lmjpegsrc;

  g_return_if_fail(GST_IS_V4LMJPEGSRC(object));
  v4lmjpegsrc = GST_V4LMJPEGSRC(object);

  switch (prop_id) {
#if 0
    case ARG_X_OFFSET:
      v4lmjpegsrc->x_offset = g_value_get_int(value);
      break;
    case ARG_Y_OFFSET:
      v4lmjpegsrc->y_offset = g_value_get_int(value);
      break;
    case ARG_F_WIDTH:
      v4lmjpegsrc->frame_width = g_value_get_int(value);
      break;
    case ARG_F_HEIGHT:
      v4lmjpegsrc->frame_height = g_value_get_int(value);
      break;
#endif
    case ARG_QUALITY:
      v4lmjpegsrc->quality = g_value_get_int(value);
      break;
    case ARG_NUMBUFS:
      v4lmjpegsrc->numbufs = g_value_get_int(value);
      break;
    case ARG_USE_FIXED_FPS:
      if (!GST_V4L_IS_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc))) {
        v4lmjpegsrc->use_fixed_fps = g_value_get_boolean(value);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_v4lmjpegsrc_get_property (GObject    *object,
                              guint      prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  GstV4lMjpegSrc *v4lmjpegsrc;

  g_return_if_fail(GST_IS_V4LMJPEGSRC(object));
  v4lmjpegsrc = GST_V4LMJPEGSRC(object);

  switch (prop_id) {
#if 0
    case ARG_X_OFFSET:
      g_value_set_int(value, v4lmjpegsrc->x_offset);
      break;
    case ARG_Y_OFFSET:
      g_value_set_int(value, v4lmjpegsrc->y_offset);
      break;
    case ARG_F_WIDTH:
      g_value_set_int(value, v4lmjpegsrc->frame_width);
      break;
    case ARG_F_HEIGHT:
      g_value_set_int(value, v4lmjpegsrc->frame_height);
      break;
#endif
    case ARG_QUALITY:
      g_value_set_int(value, v4lmjpegsrc->quality);
      break;
    case ARG_NUMBUFS:
      g_value_set_int(value, v4lmjpegsrc->breq.count);
      break;
    case ARG_BUFSIZE:
      g_value_set_int(value, v4lmjpegsrc->breq.size);
      break;
    case ARG_USE_FIXED_FPS:
      g_value_set_boolean(value, v4lmjpegsrc->use_fixed_fps);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static GstElementStateReturn
gst_v4lmjpegsrc_change_state (GstElement *element)
{
  GstV4lMjpegSrc *v4lmjpegsrc;
  GstElementStateReturn parent_value;
  GTimeVal time;

  g_return_val_if_fail(GST_IS_V4LMJPEGSRC(element), GST_STATE_FAILURE);
  
  v4lmjpegsrc = GST_V4LMJPEGSRC(element);

  switch (GST_STATE_TRANSITION(element)) {
    case GST_STATE_READY_TO_PAUSED:
      /* actual buffer set-up used to be done here - but I moved
       * it to capsnego itself */
      v4lmjpegsrc->handled = 0;
      v4lmjpegsrc->need_writes = 0;
      v4lmjpegsrc->last_frame = 0;
      v4lmjpegsrc->substract_time = 0;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      /* queue all buffer, start streaming capture */
      if (!gst_v4lmjpegsrc_capture_start(v4lmjpegsrc))
        return GST_STATE_FAILURE;
      g_get_current_time(&time);
      v4lmjpegsrc->substract_time = GST_TIMEVAL_TO_TIME(time) -
                                      v4lmjpegsrc->substract_time;
      v4lmjpegsrc->last_seq = 0;
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      g_get_current_time(&time);
      v4lmjpegsrc->substract_time = GST_TIMEVAL_TO_TIME(time) -
                                      v4lmjpegsrc->substract_time;
      /* de-queue all queued buffers */
      if (!gst_v4lmjpegsrc_capture_stop(v4lmjpegsrc))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_PAUSED_TO_READY:
      /* stop capturing, unmap all buffers */
      if (!gst_v4lmjpegsrc_capture_deinit(v4lmjpegsrc))
        return GST_STATE_FAILURE;
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state) {
    parent_value = GST_ELEMENT_CLASS (parent_class)->change_state (element);
  } else {
    parent_value = GST_STATE_FAILURE;
  }

  if (GST_STATE_TRANSITION(element) == GST_STATE_NULL_TO_READY)
  {
    /* do autodetection if no input/norm is selected yet */
    if ((GST_V4LELEMENT(v4lmjpegsrc)->norm < VIDEO_MODE_PAL ||
         GST_V4LELEMENT(v4lmjpegsrc)->norm == VIDEO_MODE_AUTO) ||
        (GST_V4LELEMENT(v4lmjpegsrc)->channel < 0 ||
         GST_V4LELEMENT(v4lmjpegsrc)->channel == V4L_MJPEG_INPUT_AUTO))
    {
      gint norm, input;

      if (GST_V4LELEMENT(v4lmjpegsrc)->norm < 0)
        norm = VIDEO_MODE_AUTO;
      else
        norm = GST_V4LELEMENT(v4lmjpegsrc)->norm;

      if (GST_V4LELEMENT(v4lmjpegsrc)->channel < 0)
        input = V4L_MJPEG_INPUT_AUTO;
      else
        input = GST_V4LELEMENT(v4lmjpegsrc)->channel;

      if (!gst_v4lmjpegsrc_set_input_norm(v4lmjpegsrc, input, norm))
        return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return parent_value;

  return GST_STATE_SUCCESS;
}


static void
gst_v4lmjpegsrc_set_clock (GstElement *element,
                           GstClock   *clock)
{
  GST_V4LMJPEGSRC(element)->clock = clock;
}


static GstBuffer*
gst_v4lmjpegsrc_buffer_new (GstBufferPool *pool,
                            guint64       location,
                            guint         size,
                            gpointer      user_data)
{
  GstBuffer *buffer;
  GstV4lMjpegSrc *v4lmjpegsrc = GST_V4LMJPEGSRC(user_data);

  if (!GST_V4L_IS_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc)))
    return NULL;

  buffer = gst_buffer_new();
  if (!buffer)
    return NULL;

  /* TODO: add interlacing info to buffer as metadata */
  GST_BUFFER_MAXSIZE(buffer) = v4lmjpegsrc->breq.size;
  GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_DONTFREE);

  return buffer;
}


static void
gst_v4lmjpegsrc_buffer_free (GstBufferPool *pool, GstBuffer *buf, gpointer user_data)
{
  GstV4lMjpegSrc *v4lmjpegsrc = GST_V4LMJPEGSRC (user_data);
  int n;

  if (gst_element_get_state(GST_ELEMENT(v4lmjpegsrc)) != GST_STATE_PLAYING)
    return; /* we've already cleaned up ourselves */

  for (n=0;n<v4lmjpegsrc->breq.count;n++)
    if (GST_BUFFER_DATA(buf) == gst_v4lmjpegsrc_get_buffer(v4lmjpegsrc, n))
    {
      v4lmjpegsrc->use_num_times[n]--;
      if (v4lmjpegsrc->use_num_times[n] <= 0) {
        gst_v4lmjpegsrc_requeue_frame(v4lmjpegsrc, n);
      }
      break;
    }

  if (n == v4lmjpegsrc->breq.count)
    gst_element_error(GST_ELEMENT(v4lmjpegsrc),
      "Couldn't find the buffer");

  /* free the buffer struct et all */
  gst_buffer_default_free(buf);
}


static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
  GstCaps *caps;

  /* create an elementfactory for the v4lmjpegsrcparse element */
  factory = gst_element_factory_new("v4lmjpegsrc",GST_TYPE_V4LMJPEGSRC,
                                   &gst_v4lmjpegsrc_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  caps = gst_caps_new ("v4lmjpegsrc_caps",
                       "video/jpeg",
                       gst_props_new (
                          "width",  GST_PROPS_INT_RANGE (0, G_MAXINT),
                          "height", GST_PROPS_INT_RANGE (0, G_MAXINT),
                          NULL       )
                      );
  capslist = gst_caps_append(capslist, caps);

  src_template = gst_pad_template_new (
		  "src",
                  GST_PAD_SRC,
  		  GST_PAD_ALWAYS,
		  capslist, NULL);

  gst_element_factory_add_pad_template (factory, src_template);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}


GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "v4lmjpegsrc",
  plugin_init
};
