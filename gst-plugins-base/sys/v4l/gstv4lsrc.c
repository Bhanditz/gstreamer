/* G-Streamer BT8x8/V4L frame grabber plugin
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <config.h>
#endif

#include <string.h>
#include <sys/time.h>
#include "v4lsrc_calls.h"
#include <gst/video/video.h>

/* elementfactory information */
static GstElementDetails gst_v4lsrc_details = {
  "Video (video4linux/raw) Source",
  "Source/Video",
  "LGPL",
  "Reads raw frames from a video4linux (BT8x8) device",
  VERSION,
  "Ronald Bultje <rbultje@ronald.bitfreak.net>",
  "(C) 2001",
};

/* V4lSrc signals and args */
enum {
  /* FILL ME */
  SIGNAL_FRAME_CAPTURE,
  SIGNAL_FRAME_DROP,
  SIGNAL_FRAME_INSERT,
  LAST_SIGNAL
};

/* arguments */
enum {
  ARG_0,
  ARG_NUMBUFS,
  ARG_BUFSIZE,
  ARG_USE_FIXED_FPS
};


/* init functions */
static void                  gst_v4lsrc_class_init   (GstV4lSrcClass *klass);
static void                  gst_v4lsrc_init         (GstV4lSrc      *v4lsrc);

/* pad/buffer functions */
static gboolean              gst_v4lsrc_srcconvert   (GstPad         *pad,
                                                      GstFormat      src_format,
                                                      gint64         src_value,
                                                      GstFormat      *dest_format,
                                                      gint64         *dest_value);
static GstPadLinkReturn      gst_v4lsrc_srcconnect   (GstPad         *pad,
                                                      GstCaps        *caps);
static GstCaps*	             gst_v4lsrc_getcaps      (GstPad         *pad,
                                                      GstCaps        *caps);
static GstBuffer*            gst_v4lsrc_get          (GstPad         *pad);

/* get/set params */
static void                  gst_v4lsrc_set_property (GObject        *object,
                                                      guint          prop_id,
                                                      const GValue   *value,
                                                      GParamSpec     *pspec);
static void                  gst_v4lsrc_get_property (GObject        *object,
                                                      guint          prop_id,
                                                      GValue         *value,
                                                      GParamSpec     *pspec);

/* state handling */
static GstElementStateReturn gst_v4lsrc_change_state (GstElement     *element);

/* bufferpool functions */
static GstBuffer*            gst_v4lsrc_buffer_new   (GstBufferPool  *pool,
                                                      guint64        offset,
                                                      guint          size,
                                                      gpointer       user_data);
static void                  gst_v4lsrc_buffer_free  (GstBufferPool  *pool,
						      GstBuffer      *buf,
						      gpointer       user_data);

/* set_clock function for a/V sync */
static void                  gst_v4lsrc_set_clock    (GstElement     *element,
                                                      GstClock       *clock);

static GstPadTemplate *src_template = NULL;

static GstElementClass *parent_class = NULL;
static guint gst_v4lsrc_signals[LAST_SIGNAL] = { 0 };


GType
gst_v4lsrc_get_type (void)
{
  static GType v4lsrc_type = 0;

  if (!v4lsrc_type) {
    static const GTypeInfo v4lsrc_info = {
      sizeof(GstV4lSrcClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_v4lsrc_class_init,
      NULL,
      NULL,
      sizeof(GstV4lSrc),
      0,
      (GInstanceInitFunc)gst_v4lsrc_init,
      NULL
    };
    v4lsrc_type = g_type_register_static(GST_TYPE_V4LELEMENT, "GstV4lSrc", &v4lsrc_info, 0);
  }
  return v4lsrc_type;
}


static void
gst_v4lsrc_class_init (GstV4lSrcClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_V4LELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_NUMBUFS,
    g_param_spec_int("num_buffers","Num Buffers","Number of buffers",
                     G_MININT,G_MAXINT,0,G_PARAM_READABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BUFSIZE,
    g_param_spec_int("buffer_size","Buffer Size","Size of buffers",
                     G_MININT,G_MAXINT,0,G_PARAM_READABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_USE_FIXED_FPS,
    g_param_spec_boolean("use_fixed_fps", "Use Fixed FPS",
                         "Drop/Insert frames to reach a certain FPS (TRUE) "
                         "or adapt FPS to suit the number of frabbed frames",
                         TRUE, G_PARAM_READWRITE));

  /* signals */
  gst_v4lsrc_signals[SIGNAL_FRAME_CAPTURE] =
    g_signal_new("frame_capture", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                 G_STRUCT_OFFSET(GstV4lSrcClass, frame_capture),
                 NULL, NULL, g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);
  gst_v4lsrc_signals[SIGNAL_FRAME_DROP] =
    g_signal_new("frame_drop", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                 G_STRUCT_OFFSET(GstV4lSrcClass, frame_drop),
                 NULL, NULL, g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);
  gst_v4lsrc_signals[SIGNAL_FRAME_INSERT] =
    g_signal_new("frame_insert", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                 G_STRUCT_OFFSET(GstV4lSrcClass, frame_insert),
                 NULL, NULL, g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);

  gobject_class->set_property = gst_v4lsrc_set_property;
  gobject_class->get_property = gst_v4lsrc_get_property;

  gstelement_class->change_state = gst_v4lsrc_change_state;

  gstelement_class->set_clock = gst_v4lsrc_set_clock;
}


static void
gst_v4lsrc_init (GstV4lSrc *v4lsrc)
{
  GST_FLAG_SET(GST_ELEMENT(v4lsrc), GST_ELEMENT_THREAD_SUGGESTED);

  v4lsrc->srcpad = gst_pad_new_from_template (src_template, "src");
  gst_element_add_pad(GST_ELEMENT(v4lsrc), v4lsrc->srcpad);

  gst_pad_set_get_function (v4lsrc->srcpad, gst_v4lsrc_get);
  gst_pad_set_getcaps_function (v4lsrc->srcpad, gst_v4lsrc_getcaps);
  gst_pad_set_link_function (v4lsrc->srcpad, gst_v4lsrc_srcconnect);
  gst_pad_set_convert_function (v4lsrc->srcpad, gst_v4lsrc_srcconvert);

  v4lsrc->bufferpool = gst_buffer_pool_new(
		  NULL, 
		  NULL,
		  (GstBufferPoolBufferNewFunction)gst_v4lsrc_buffer_new,
		  NULL,
		  (GstBufferPoolBufferFreeFunction)gst_v4lsrc_buffer_free,
		  v4lsrc);

  v4lsrc->buffer_size = 0;

  /* no clock */
  v4lsrc->clock = NULL;

  /* fps */
  v4lsrc->use_fixed_fps = TRUE;
}


static gfloat
gst_v4lsrc_get_fps (GstV4lSrc *v4lsrc)
{
  gint norm;
  gfloat fps;

  if (!v4lsrc->use_fixed_fps &&
      v4lsrc->clock != NULL &&
      v4lsrc->handled > 0) {
    /* try to get time from clock master and calculate fps */
    GstClockTime time = gst_clock_get_time(v4lsrc->clock) - v4lsrc->substract_time;
    return v4lsrc->handled * GST_SECOND / time;
  }

  /* if that failed ... */

  if (!GST_V4L_IS_OPEN(GST_V4LELEMENT(v4lsrc)))
    return 0.;

  if (!gst_v4l_get_chan_norm(GST_V4LELEMENT(v4lsrc), NULL, &norm))
    return 0.;

  if (norm == VIDEO_MODE_NTSC)
    fps = 30000/1001;
  else
    fps = 25.;

  return fps;
}


static gboolean
gst_v4lsrc_srcconvert (GstPad    *pad,
                       GstFormat  src_format,
                       gint64     src_value,
                       GstFormat *dest_format,
                       gint64    *dest_value)
{
  GstV4lSrc *v4lsrc;
  gdouble fps;

  v4lsrc = GST_V4LSRC (gst_pad_get_parent (pad));

  if ((fps = gst_v4lsrc_get_fps(v4lsrc)) == 0)
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value * fps / GST_SECOND;
          break;
        default:
          return FALSE;
      }
      break;

    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
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

static GstCaps *
gst_v4lsrc_palette_to_caps (int            palette,
                            GstPropsEntry *width,
                            GstPropsEntry *height,
                            GstPropsEntry *fps)
{
  guint32 fourcc;
  GstCaps *caps;

  switch (palette) {
    case VIDEO_PALETTE_YUV422:
    case VIDEO_PALETTE_YUYV:
      fourcc = GST_MAKE_FOURCC('Y','U','Y','2');
      break;
    case VIDEO_PALETTE_YUV420P:
      fourcc = GST_MAKE_FOURCC('I','4','2','0');
      break;
    case VIDEO_PALETTE_UYVY:
      fourcc = GST_MAKE_FOURCC('U','Y','V','Y');
      break;
    case VIDEO_PALETTE_YUV411:
      fourcc = GST_MAKE_FOURCC('Y','4','1','P');
      break;
    case VIDEO_PALETTE_RGB555:
    case VIDEO_PALETTE_RGB565:
    case VIDEO_PALETTE_RGB24:
    case VIDEO_PALETTE_RGB32:
      fourcc = GST_MAKE_FOURCC('R','G','B',' ');
      break;
    default:
      return NULL;
  }

  if (fourcc == GST_MAKE_FOURCC('R','G','B',' ')) {
    gint depth = 0, endianness = 0;
    guint32 r_mask = 0, g_mask = 0, b_mask = 0;

    switch (palette) {
      case VIDEO_PALETTE_RGB555:
        depth = 15; endianness = G_BYTE_ORDER;
        r_mask = 0x7c00; g_mask = 0x03e0; b_mask = 0x001f;
        break;
      case VIDEO_PALETTE_RGB565:
        depth = 16; endianness = G_BYTE_ORDER;
        r_mask = 0xf800; g_mask = 0x07f0; b_mask = 0x001f;
        break;
      case VIDEO_PALETTE_RGB24:
        depth = 24; endianness = G_BIG_ENDIAN;
        r_mask = R_MASK_24; g_mask = G_MASK_24; b_mask = B_MASK_24;
        break;
      case VIDEO_PALETTE_RGB32:
        depth = 32; endianness = G_BIG_ENDIAN;
        r_mask = R_MASK_32; g_mask = G_MASK_32; b_mask = B_MASK_32;
        break;
      default:
        g_assert_not_reached();
        break;
    }

    caps = GST_CAPS_NEW("v4lsrc_rgb_caps",
                        "video/x-raw-rgb",
                          "bpp",        GST_PROPS_INT((depth+1) & ~1),
                          "depth",      GST_PROPS_INT(depth),
                          "endianness", GST_PROPS_INT(G_BYTE_ORDER),
                          "red_mask",   GST_PROPS_INT(r_mask),
                          "green_mask", GST_PROPS_INT(g_mask),
                          "blue_mask",  GST_PROPS_INT(b_mask),
                          NULL);
  } else {
    caps = GST_CAPS_NEW("v4lsrc_yuv_caps",
                        "video/x-raw-yuv",
                          "format",     GST_PROPS_FOURCC(fourcc),
                          NULL);
  }

  gst_props_add_entry(caps->properties, width);
  gst_props_add_entry(caps->properties, height);
  gst_props_add_entry(caps->properties, fps);

  return caps;
}

#define gst_v4lsrc_palette_to_caps_fixed(palette, width, height, fps) \
  gst_v4lsrc_palette_to_caps(palette, \
                             gst_props_entry_new("width", \
                                                 GST_PROPS_INT(width)), \
                             gst_props_entry_new("height", \
                                                 GST_PROPS_INT(height)), \
                             gst_props_entry_new("framerate", \
                                                 GST_PROPS_FLOAT(fps)) \
                            )
#define gst_v4lsrc_palette_to_caps_range(palette, min_w, max_w, min_h, max_h) \
  gst_v4lsrc_palette_to_caps(palette, \
                             gst_props_entry_new("width", \
                                                 GST_PROPS_INT_RANGE(min_w, \
                                                                     max_w)), \
                             gst_props_entry_new("height", \
                                                 GST_PROPS_INT_RANGE(min_h, \
                                                                     max_h)), \
                             gst_props_entry_new("framerate", \
                                                 GST_PROPS_FLOAT_RANGE(0., \
                                                                 G_MAXFLOAT)) \
                            )

#define gst_caps_get_int_range(caps, name, min, max) \
  gst_props_entry_get_int_range(gst_props_get_entry((caps)->properties, \
                                                    name), \
                                min, max)

static GstPadLinkReturn
gst_v4lsrc_srcconnect (GstPad  *pad,
                       GstCaps *vscapslist)
{
  GstPadLinkReturn ret_val;
  GstV4lSrc *v4lsrc;
  GstCaps *caps, *newcaps;
  gint palette;

  v4lsrc = GST_V4LSRC (gst_pad_get_parent (pad));

  /* in case the buffers are active (which means that we already
   * did capsnego before and didn't clean up), clean up anyways */
  if (GST_V4L_IS_ACTIVE(GST_V4LELEMENT(v4lsrc)))
  {
    if (!gst_v4lsrc_capture_deinit(v4lsrc))
      return GST_PAD_LINK_REFUSED;
  }
  else if (!GST_V4L_IS_OPEN(GST_V4LELEMENT(v4lsrc)))
  {
    return GST_PAD_LINK_DELAYED;
  }

  /* TODO: caps = gst_caps_normalize(capslist); */
  for (caps = vscapslist ; caps != NULL ; caps = vscapslist = vscapslist->next)
  {
    guint32 fourcc;
    gint depth, w, h;

    if (!strcmp(gst_caps_get_mime(caps), "video/x-raw-yuv"))
      gst_caps_get_fourcc_int (caps, "format", &fourcc);
    else
      fourcc = GST_MAKE_FOURCC('R','G','B',' ');

    if (gst_caps_has_property(caps, "width")) {
      if (gst_caps_has_fixed_property(caps, "width")) {
        gst_caps_get_int(caps, "width", &w);
      } else {
        int min;
        gst_caps_get_int_range(caps, "width", &min, &w);
      }
    }
    if (gst_caps_has_property(caps, "height")) {
      if (gst_caps_has_fixed_property(caps, "height")) {
        gst_caps_get_int(caps, "height", &h);
      } else {
        int min;
        gst_caps_get_int_range(caps, "height", &min, &h);
      }
    }

    switch (fourcc)
    {
      case GST_MAKE_FOURCC('I','4','2','0'):
      case GST_MAKE_FOURCC('I','Y','U','V'):
        palette = VIDEO_PALETTE_YUV420P;
        v4lsrc->buffer_size = ((w+1)&~1) * ((h+1)&~1) * 1.5;
        goto try_caps;
      case GST_MAKE_FOURCC('Y','U','Y','2'):
        palette = VIDEO_PALETTE_YUV422;
        v4lsrc->buffer_size = ((w+1)&~1) * h * 2;
        goto try_caps;
      case GST_MAKE_FOURCC('U','Y','V','Y'):
        palette = VIDEO_PALETTE_UYVY;
        v4lsrc->buffer_size = ((w+1)&~1) * h * 2;
        goto try_caps;
      case GST_MAKE_FOURCC('Y','4','1','P'):
        palette = VIDEO_PALETTE_YUV411;
        v4lsrc->buffer_size = ((w+3)&~3) * h * 1.5;
        goto try_caps;
      case GST_MAKE_FOURCC('R','G','B',' '):
        depth = gst_caps_get_int (caps, "depth", &depth);
        switch (depth)
        {
          case 15:
            palette = VIDEO_PALETTE_RGB555;
            v4lsrc->buffer_size = w * h * 2;
            goto try_caps;
          case 16:
            palette = VIDEO_PALETTE_RGB565;
            v4lsrc->buffer_size = w * h * 2;
            goto try_caps;
          case 24:
            palette = VIDEO_PALETTE_RGB24;
            v4lsrc->buffer_size = w * h * 3;
            goto try_caps;
          case 32:
            palette = VIDEO_PALETTE_RGB32;
            v4lsrc->buffer_size = w * h * 4;
            goto try_caps;
          default:
            goto try_next;
        }
      default:
        goto try_next;
    }

  /* if this caps wasn't useful, try the next one */
  try_next:
    continue;

  /* if this caps was useful, try it out */
  try_caps:
    /* try the current 'palette' out on the video device */
    if (!gst_v4lsrc_try_palette(v4lsrc, palette))
      continue;

    /* try to connect the pad/caps with the actual width/height */
    newcaps = gst_v4lsrc_palette_to_caps_fixed(palette, w, h,
				gst_v4lsrc_get_fps(v4lsrc));

    gst_caps_debug (newcaps, "new caps to set on v4lsrc's src pad");

    if ((ret_val = gst_pad_try_set_caps(v4lsrc->srcpad, newcaps)) == GST_PAD_LINK_REFUSED)
      continue;
    else if (ret_val == GST_PAD_LINK_DELAYED)
      return GST_PAD_LINK_DELAYED;

    if (!gst_v4lsrc_set_capture(v4lsrc, w, h, palette))
      return GST_PAD_LINK_REFUSED;

    if (!gst_v4lsrc_capture_init(v4lsrc))
      return GST_PAD_LINK_REFUSED;

    return GST_PAD_LINK_DONE;
  }

  /* still nothing - no good caps */
  return GST_PAD_LINK_REFUSED;
}


static GstCaps *
gst_v4lsrc_getcaps (GstPad  *pad,
                    GstCaps *caps)
{
  GstCaps *list = NULL;
  GstV4lSrc *v4lsrc = GST_V4LSRC(gst_pad_get_parent(pad));
  int palette[] = {
    VIDEO_PALETTE_YUV422,
    VIDEO_PALETTE_YUV420P,
    VIDEO_PALETTE_UYVY,
    VIDEO_PALETTE_YUV411P,
    VIDEO_PALETTE_RGB555,
    VIDEO_PALETTE_RGB565,
    VIDEO_PALETTE_RGB24,
    VIDEO_PALETTE_RGB32,
  }, i;
  struct video_capability *vcap = &GST_V4LELEMENT(v4lsrc)->vcap;

  if (!GST_V4L_IS_OPEN(GST_V4LELEMENT(v4lsrc))) {
    return NULL;
  }

  for (i = 0; i < 8; i++) {
    GstCaps *one;
    one = gst_v4lsrc_palette_to_caps_range(palette[i],
                                           vcap->minwidth,  vcap->maxwidth,
                                           vcap->minheight, vcap->maxheight);
    list = gst_caps_append(list, one);
  }

  return list;
}


static GstBuffer*
gst_v4lsrc_get (GstPad *pad)
{
  GstV4lSrc *v4lsrc;
  GstBuffer *buf;
  gint num;
  gdouble fps = 0.;

  g_return_val_if_fail (pad != NULL, NULL);

  v4lsrc = GST_V4LSRC (gst_pad_get_parent (pad));

  if (v4lsrc->use_fixed_fps &&
      (fps = gst_v4lsrc_get_fps(v4lsrc)) == 0)
    return NULL;

  buf = gst_buffer_new_from_pool(v4lsrc->bufferpool, 0, 0);
  if (!buf)
  {
    gst_element_error(GST_ELEMENT(v4lsrc), GST_ERROR_INTERNAL,
      g_strdup(_("An internal error occured")),
      g_strdup("Failed to create a new GstBuffer"));
    return NULL;
  }

  if (v4lsrc->need_writes > 0) {
    /* use last frame */
    num = v4lsrc->last_frame;
    v4lsrc->need_writes--;
  } else if (v4lsrc->clock && v4lsrc->use_fixed_fps) {
    GstClockTime time;
    gboolean have_frame = FALSE;

    do {
      /* by default, we use the frame once */
      v4lsrc->need_writes = 1;

      /* grab a frame from the device */
      if (!gst_v4lsrc_grab_frame(v4lsrc, &num))
        return NULL;

      v4lsrc->last_frame = num;
      time = v4lsrc->timestamp_sync - v4lsrc->substract_time;

      /* decide how often we're going to write the frame - set
       * v4lsrc->need_writes to (that-1) and have_frame to TRUE
       * if we're going to write it - else, just continue.
       * 
       * time is generally the system or audio clock. Let's
       * say that we've written one second of audio, then we want
       * to have written one second of video too, within the same
       * timeframe. This means that if time - begin_time = X sec,
       * we want to have written X*fps frames. If we've written
       * more - drop, if we've written less - dup... */
      if (v4lsrc->handled * (GST_SECOND/fps) - time > 1.5 * (GST_SECOND/fps)) {
        /* yo dude, we've got too many frames here! Drop! DROP! */
        v4lsrc->need_writes--; /* -= (v4lsrc->handled - (time / fps)); */
        g_signal_emit(G_OBJECT(v4lsrc),
                      gst_v4lsrc_signals[SIGNAL_FRAME_DROP], 0);
      } else if (v4lsrc->handled * (GST_SECOND/fps) - time < - 1.5 * (GST_SECOND/fps)) {
        /* this means we're lagging far behind */
        v4lsrc->need_writes++; /* += ((time / fps) - v4lsrc->handled); */
        g_signal_emit(G_OBJECT(v4lsrc),
                      gst_v4lsrc_signals[SIGNAL_FRAME_INSERT], 0);
      }

      if (v4lsrc->need_writes > 0) {
        have_frame = TRUE;
        v4lsrc->use_num_times[num] = v4lsrc->need_writes;
        v4lsrc->need_writes--;
      } else {
        gst_v4lsrc_requeue_frame(v4lsrc, num);
      }
    } while (!have_frame);
  } else {
    /* grab a frame from the device */
    if (!gst_v4lsrc_grab_frame(v4lsrc, &num))
      return NULL;

    v4lsrc->use_num_times[num] = 1;
  }

  GST_BUFFER_DATA(buf) = gst_v4lsrc_get_buffer(v4lsrc, num);
  GST_BUFFER_SIZE(buf) = v4lsrc->buffer_size;
  if (v4lsrc->use_fixed_fps)
    GST_BUFFER_TIMESTAMP(buf) = v4lsrc->handled * GST_SECOND / fps;
  else /* calculate time based on our own clock */
    GST_BUFFER_TIMESTAMP(buf) = v4lsrc->timestamp_sync - v4lsrc->substract_time;

  v4lsrc->handled++;
  g_signal_emit(G_OBJECT(v4lsrc),
                gst_v4lsrc_signals[SIGNAL_FRAME_CAPTURE], 0);

  return buf;
}


static void
gst_v4lsrc_set_property (GObject      *object,
                         guint        prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  GstV4lSrc *v4lsrc;

  g_return_if_fail(GST_IS_V4LSRC(object));
  v4lsrc = GST_V4LSRC(object);

  switch (prop_id) {
    case ARG_USE_FIXED_FPS:
      if (!GST_V4L_IS_ACTIVE(GST_V4LELEMENT(v4lsrc))) {
        v4lsrc->use_fixed_fps = g_value_get_boolean(value);
      }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_v4lsrc_get_property (GObject    *object,
                         guint      prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  GstV4lSrc *v4lsrc;

  g_return_if_fail(GST_IS_V4LSRC(object));
  v4lsrc = GST_V4LSRC(object);

  switch (prop_id) {
    case ARG_NUMBUFS:
      g_value_set_int(value, v4lsrc->mbuf.frames);
      break;

    case ARG_BUFSIZE:
      if (v4lsrc->mbuf.frames == 0)
        g_value_set_int(value, 0);
      else
        g_value_set_int(value, v4lsrc->mbuf.size/(v4lsrc->mbuf.frames*1024));
      break;

    case ARG_USE_FIXED_FPS:
      g_value_set_boolean(value, v4lsrc->use_fixed_fps);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static GstElementStateReturn
gst_v4lsrc_change_state (GstElement *element)
{
  GstV4lSrc *v4lsrc;
  GTimeVal time;
  gint transition = GST_STATE_TRANSITION (element);

  g_return_val_if_fail(GST_IS_V4LSRC(element), GST_STATE_FAILURE);
  
  v4lsrc = GST_V4LSRC(element);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      v4lsrc->handled = 0;
      v4lsrc->need_writes = 0;
      v4lsrc->last_frame = 0;
      v4lsrc->substract_time = 0;
      /* buffer setup used to be done here, but I moved it to
       * capsnego */
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      /* queue all buffer, start streaming capture */
      if (!gst_v4lsrc_capture_start(v4lsrc))
        return GST_STATE_FAILURE;
      g_get_current_time(&time);
      v4lsrc->substract_time = GST_TIMEVAL_TO_TIME(time) - v4lsrc->substract_time;
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      /* de-queue all queued buffers */
      if (!gst_v4lsrc_capture_stop(v4lsrc))
        return GST_STATE_FAILURE;
      g_get_current_time(&time);
      v4lsrc->substract_time = GST_TIMEVAL_TO_TIME(time) - v4lsrc->substract_time;
      break;
    case GST_STATE_PAUSED_TO_READY:
      /* stop capturing, unmap all buffers */
      if (!gst_v4lsrc_capture_deinit(v4lsrc))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_READY_TO_NULL:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}


static GstBuffer*
gst_v4lsrc_buffer_new (GstBufferPool *pool,
		       guint64 	     offset,
                       guint         size,
                       gpointer      user_data)
{
  GstBuffer *buffer;
  GstV4lSrc *v4lsrc = GST_V4LSRC(user_data);

  if (!GST_V4L_IS_ACTIVE(GST_V4LELEMENT(v4lsrc)))
    return NULL;

  buffer = gst_buffer_new();
  if (!buffer)
    return NULL;

  /* TODO: add interlacing info to buffer as metadata
   * (height>288 or 240 = topfieldfirst, else noninterlaced) */
  GST_BUFFER_MAXSIZE(buffer) = v4lsrc->mbuf.size / v4lsrc->mbuf.frames;
  GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_DONTFREE);

  return buffer;
}


static void
gst_v4lsrc_buffer_free (GstBufferPool *pool, GstBuffer *buf, gpointer user_data)
{
  GstV4lSrc *v4lsrc = GST_V4LSRC (user_data);
  int n;

  if (gst_element_get_state(GST_ELEMENT(v4lsrc)) != GST_STATE_PLAYING)
    return; /* we've already cleaned up ourselves */

  for (n=0;n<v4lsrc->mbuf.frames;n++)
    if (GST_BUFFER_DATA(buf) == gst_v4lsrc_get_buffer(v4lsrc, n))
    {
      v4lsrc->use_num_times[n]--;
      if (v4lsrc->use_num_times[n] <= 0) {
        gst_v4lsrc_requeue_frame(v4lsrc, n);
      }
      break;
    }

  if (n == v4lsrc->mbuf.frames)
    gst_element_error(GST_ELEMENT(v4lsrc), GST_ERROR_INTERNAL,
      g_strdup(_("An internal error occured")),
      g_strdup("Couldn\'t find the buffer"));

  /* free struct */
  gst_buffer_default_free(buf);
}


static void
gst_v4lsrc_set_clock (GstElement *element,
                      GstClock   *clock)
{
  GST_V4LSRC(element)->clock = clock;
}


gboolean
gst_v4lsrc_factory_init (GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the v4lsrc */
  factory = gst_element_factory_new("v4lsrc",GST_TYPE_V4LSRC,
                                   &gst_v4lsrc_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  src_template = gst_pad_template_new (
		  "src",
                  GST_PAD_SRC,
  		  GST_PAD_ALWAYS,
		  NULL);

  gst_element_factory_add_pad_template (factory, src_template);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}
