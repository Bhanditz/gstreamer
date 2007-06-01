/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim@fluendo.com>
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

/**
 * SECTION:element-videomark
 * @short_description: Mark a pattern in a video signal
 *
 * <refsect2>
 * <para>
 * This plugin produces ::pattern-count squares in the bottom left corner of
 * the video frames. The squares have a width and height of respectively
 * ::pattern-width and ::patern-height. Even squares will be black and odd
 * squares will be white.
 * </para>
 * <para>
 * After writing the pattern, ::pattern-data-count squares after the
 * pattern squares are produced as the bitarray given in ::pattern-data. 1 bits
 * will produce white squares and 0 bits will produce black squares.
 * </para>
 * <para>
 * The element can be enabled with the ::enabled property. It is mostly used
 * together with the videodetect plugin.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch videotestsrc ! videomark ! ximagesink
 * </programlisting>
 * Add the default black/white squares at the bottom left of the video frames.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2007-06-01 (0.10.6)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideomark.h"

#include <string.h>
#include <math.h>

#include <gst/video/video.h>

/* GstVideoMark signals and args */

#define DEFAULT_PATTERN_WIDTH        4
#define DEFAULT_PATTERN_HEIGHT       16
#define DEFAULT_PATTERN_COUNT        4
#define DEFAULT_PATTERN_DATA_COUNT   5
#define DEFAULT_PATTERN_DATA         10
#define DEFAULT_ENABLED              TRUE

enum
{
  PROP_0,
  PROP_PATTERN_WIDTH,
  PROP_PATTERN_HEIGHT,
  PROP_PATTERN_COUNT,
  PROP_PATTERN_DATA_COUNT,
  PROP_PATTERN_DATA,
  PROP_ENABLED
};

GST_DEBUG_CATEGORY_STATIC (video_mark_debug);
#define GST_CAT_DEFAULT video_mark_debug

static const GstElementDetails video_mark_details =
GST_ELEMENT_DETAILS ("Video marker",
    "Filter/Effect/Video",
    "Marks a video signal with a pattern",
    "Wim Taymans <wim@fluendo.com>");

static GstStaticPadTemplate gst_video_mark_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ I420, YV12 }"))
    );

static GstStaticPadTemplate gst_video_mark_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ I420, YV12 }"))
    );

static GstVideoFilterClass *parent_class = NULL;

static gboolean
gst_video_mark_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVideoMark *vf;
  GstStructure *in_s;
  gboolean ret;

  vf = GST_VIDEO_MARK (btrans);

  in_s = gst_caps_get_structure (incaps, 0);

  ret = gst_structure_get_int (in_s, "width", &vf->width);
  ret &= gst_structure_get_int (in_s, "height", &vf->height);
  ret &= gst_structure_get_fourcc (in_s, "format", &vf->format);

  return ret;
}

/* Useful macros */
#define GST_VIDEO_I420_Y_ROWSTRIDE(width) (GST_ROUND_UP_4(width))
#define GST_VIDEO_I420_U_ROWSTRIDE(width) (GST_ROUND_UP_8(width)/2)
#define GST_VIDEO_I420_V_ROWSTRIDE(width) ((GST_ROUND_UP_8(GST_VIDEO_I420_Y_ROWSTRIDE(width)))/2)

#define GST_VIDEO_I420_Y_OFFSET(w,h) (0)
#define GST_VIDEO_I420_U_OFFSET(w,h) (GST_VIDEO_I420_Y_OFFSET(w,h)+(GST_VIDEO_I420_Y_ROWSTRIDE(w)*GST_ROUND_UP_2(h)))
#define GST_VIDEO_I420_V_OFFSET(w,h) (GST_VIDEO_I420_U_OFFSET(w,h)+(GST_VIDEO_I420_U_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

#define GST_VIDEO_I420_SIZE(w,h)     (GST_VIDEO_I420_V_OFFSET(w,h)+(GST_VIDEO_I420_V_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

static void
gst_video_mark_draw_box (GstVideoMark * videomark, guint8 * data,
    gint width, gint height, gint stride, guint8 color)
{
  gint i, j;

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      data[j] = color;
    }
    /* move to next line */
    data += stride;
  }
}

static void
gst_video_mark_420 (GstVideoMark * videomark, GstBuffer * buffer)
{
  gint i, pw, ph, stride, width, height;
  guint8 *d, *data;
  guint pattern_shift;
  guint8 color;

  data = GST_BUFFER_DATA (buffer);

  width = videomark->width;
  height = videomark->height;

  pw = videomark->pattern_width;
  ph = videomark->pattern_height;
  stride = GST_VIDEO_I420_Y_ROWSTRIDE (width);

  /* draw the bottom left pixels */
  for (i = 0; i < videomark->pattern_count; i++) {
    d = data;
    /* move to start of bottom left */
    d += stride * (height - ph);
    /* move to i-th pattern */
    d += pw * i;

    if (i & 1)
      /* odd pixels must be white */
      color = 255;
    else
      color = 0;

    /* draw box of width * height */
    gst_video_mark_draw_box (videomark, d, pw, ph, stride, color);
  }

  pattern_shift = 1 << (videomark->pattern_data_count - 1);

  /* get the data of the pattern */
  for (i = 0; i < videomark->pattern_data_count; i++) {
    d = data;
    /* move to start of bottom left, after the pattern */
    d += stride * (height - ph) + (videomark->pattern_count * pw);
    /* move to i-th pattern data */
    d += pw * i;

    if (videomark->pattern_data & pattern_shift)
      color = 255;
    else
      color = 0;

    gst_video_mark_draw_box (videomark, d, pw, ph, stride, color);

    pattern_shift >>= 1;
  }
}

static GstFlowReturn
gst_video_mark_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstVideoMark *videomark;
  GstFlowReturn ret = GST_FLOW_OK;

  videomark = GST_VIDEO_MARK (trans);

  if (videomark->enabled)
    gst_video_mark_420 (videomark, buf);

  return ret;
}

static void
gst_video_mark_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoMark *videomark;

  videomark = GST_VIDEO_MARK (object);

  switch (prop_id) {
    case PROP_PATTERN_WIDTH:
      videomark->pattern_width = g_value_get_int (value);
      break;
    case PROP_PATTERN_HEIGHT:
      videomark->pattern_height = g_value_get_int (value);
      break;
    case PROP_PATTERN_COUNT:
      videomark->pattern_count = g_value_get_int (value);
      break;
    case PROP_PATTERN_DATA_COUNT:
      videomark->pattern_data_count = g_value_get_int (value);
      break;
    case PROP_PATTERN_DATA:
      videomark->pattern_data = g_value_get_int (value);
      break;
    case PROP_ENABLED:
      videomark->enabled = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_mark_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoMark *videomark;

  videomark = GST_VIDEO_MARK (object);

  switch (prop_id) {
    case PROP_PATTERN_WIDTH:
      g_value_set_int (value, videomark->pattern_width);
      break;
    case PROP_PATTERN_HEIGHT:
      g_value_set_int (value, videomark->pattern_height);
      break;
    case PROP_PATTERN_COUNT:
      g_value_set_int (value, videomark->pattern_count);
      break;
    case PROP_PATTERN_DATA_COUNT:
      g_value_set_int (value, videomark->pattern_data_count);
      break;
    case PROP_PATTERN_DATA:
      g_value_set_int (value, videomark->pattern_data);
      break;
    case PROP_ENABLED:
      g_value_set_boolean (value, videomark->enabled);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_mark_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &video_mark_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_video_mark_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_video_mark_src_template));
}

static void
gst_video_mark_class_init (gpointer klass, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_video_mark_set_property;
  gobject_class->get_property = gst_video_mark_get_property;

  g_object_class_install_property (gobject_class, PROP_PATTERN_WIDTH,
      g_param_spec_int ("pattern-width", "Pattern width",
          "The width of the pattern markers", 1, G_MAXINT,
          DEFAULT_PATTERN_WIDTH, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class, PROP_PATTERN_HEIGHT,
      g_param_spec_int ("pattern-height", "Pattern height",
          "The height of the pattern markers", 1, G_MAXINT,
          DEFAULT_PATTERN_HEIGHT, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class, PROP_PATTERN_COUNT,
      g_param_spec_int ("pattern-count", "Pattern count",
          "The number of pattern markers", 1, G_MAXINT,
          DEFAULT_PATTERN_COUNT, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class, PROP_PATTERN_DATA_COUNT,
      g_param_spec_int ("pattern-data-count", "Pattern data count",
          "The number of extra data pattern markers", 0, G_MAXINT,
          DEFAULT_PATTERN_DATA_COUNT, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class, PROP_PATTERN_DATA,
      g_param_spec_int ("pattern-data", "Pattern data",
          "The extra data pattern markers", 0, G_MAXINT,
          DEFAULT_PATTERN_DATA, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class, PROP_ENABLED,
      g_param_spec_boolean ("enabled", "Enabled",
          "Enable or disable the filter",
          DEFAULT_ENABLED, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_video_mark_set_caps);
  trans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_video_mark_transform_ip);

  GST_DEBUG_CATEGORY_INIT (video_mark_debug, "videomark", 0, "Video mark");
}

static void
gst_video_mark_init (GTypeInstance * instance, gpointer g_class)
{
  GstVideoMark *videomark;

  videomark = GST_VIDEO_MARK (instance);

  GST_DEBUG_OBJECT (videomark, "gst_video_mark_init");
}

GType
gst_video_mark_get_type (void)
{
  static GType video_mark_type = 0;

  if (!video_mark_type) {
    static const GTypeInfo video_mark_info = {
      sizeof (GstVideoMarkClass),
      gst_video_mark_base_init,
      NULL,
      gst_video_mark_class_init,
      NULL,
      NULL,
      sizeof (GstVideoMark),
      0,
      gst_video_mark_init,
    };

    video_mark_type = g_type_register_static (GST_TYPE_VIDEO_FILTER,
        "GstVideoMark", &video_mark_info, 0);
  }
  return video_mark_type;
}
