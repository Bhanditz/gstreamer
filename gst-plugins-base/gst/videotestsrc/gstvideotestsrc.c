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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstvideotestsrc.h"
#include "videotestsrc.h"

#include <string.h>
#include <stdlib.h>
#include <liboil/liboil.h>

GST_DEBUG_CATEGORY (videotestsrc_debug);
#define GST_CAT_DEFAULT videotestsrc_debug

/* elementfactory information */
static GstElementDetails videotestsrc_details =
GST_ELEMENT_DETAILS ("Video test source",
    "Source/Video",
    "Creates a test video stream",
    "David A. Schleef <ds@schleef.org>");

/* GstVideotestsrc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_TYPE,
  ARG_SYNC,
  ARG_NUM_BUFFERS
      /* FILL ME */
};

static GstCaps *capslist = NULL;

static void gst_videotestsrc_base_init (gpointer g_class);
static void gst_videotestsrc_class_init (GstVideotestsrcClass * klass);
static void gst_videotestsrc_init (GstVideotestsrc * videotestsrc);
static GstElementStateReturn gst_videotestsrc_change_state (GstElement *
    element);
static void gst_videotestsrc_set_clock (GstElement * element, GstClock * clock);

static void gst_videotestsrc_set_pattern (GstVideotestsrc * videotestsrc,
    int pattern_type);
static void gst_videotestsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_videotestsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_videotestsrc_loop (GstPad * pad);

static const GstQueryType *gst_videotestsrc_get_query_types (GstPad * pad);
static gboolean gst_videotestsrc_src_query (GstPad * pad, GstQuery * query);

#if 0
static const GstEventMask *gst_videotestsrc_get_event_masks (GstPad * pad);
#endif
static gboolean gst_videotestsrc_handle_src_event (GstPad * pad,
    GstEvent * event);

static GstElementClass *parent_class = NULL;

static GstCaps *gst_videotestsrc_get_capslist (void);

#if 0
static GstCaps *gst_videotestsrc_get_capslist_size (int width, int height,
    double rate);
#endif


static GstPadTemplate *
gst_videotestsrc_src_template_factory (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_videotestsrc_get_capslist ());
}

GType
gst_videotestsrc_get_type (void)
{
  static GType videotestsrc_type = 0;

  if (!videotestsrc_type) {
    static const GTypeInfo videotestsrc_info = {
      sizeof (GstVideotestsrcClass),
      gst_videotestsrc_base_init,
      NULL,
      (GClassInitFunc) gst_videotestsrc_class_init,
      NULL,
      NULL,
      sizeof (GstVideotestsrc),
      0,
      (GInstanceInitFunc) gst_videotestsrc_init,
    };

    videotestsrc_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstVideotestsrc",
        &videotestsrc_info, 0);
  }
  return videotestsrc_type;
}

#define GST_TYPE_VIDEOTESTSRC_PATTERN (gst_videotestsrc_pattern_get_type ())
static GType
gst_videotestsrc_pattern_get_type (void)
{
  static GType videotestsrc_pattern_type = 0;
  static GEnumValue pattern_types[] = {
    {GST_VIDEOTESTSRC_SMPTE, "smpte", "SMPTE 100% color bars"},
    {GST_VIDEOTESTSRC_SNOW, "snow", "Random (television snow)"},
    {GST_VIDEOTESTSRC_BLACK, "black", "100% Black"},
    {0, NULL, NULL},
  };

  if (!videotestsrc_pattern_type) {
    videotestsrc_pattern_type =
        g_enum_register_static ("GstVideotestsrcPattern", pattern_types);
  }
  return videotestsrc_pattern_type;
}

static void
gst_videotestsrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &videotestsrc_details);

  gst_element_class_add_pad_template (element_class,
      gst_videotestsrc_src_template_factory ());
}
static void
gst_videotestsrc_class_init (GstVideotestsrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_videotestsrc_set_property;
  gobject_class->get_property = gst_videotestsrc_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TYPE,
      g_param_spec_enum ("pattern", "Pattern",
          "Type of test pattern to generate", GST_TYPE_VIDEOTESTSRC_PATTERN, 1,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SYNC,
      g_param_spec_boolean ("sync", "Sync", "Synchronize to clock", TRUE,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_NUM_BUFFERS,
      g_param_spec_int ("num-buffers", "num-buffers",
          "Number of buffers to output before sending EOS", -1, G_MAXINT,
          0, G_PARAM_READWRITE));

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_videotestsrc_change_state;
  gstelement_class->set_clock = gst_videotestsrc_set_clock;

  GST_DEBUG_CATEGORY_INIT (videotestsrc_debug, "videotestsrc", 0,
      "Video Test Source");
}

static void
gst_videotestsrc_set_clock (GstElement * element, GstClock * clock)
{
  GstVideotestsrc *v;

  v = GST_VIDEOTESTSRC (element);

  gst_object_replace ((GstObject **) & v->clock, (GstObject *) clock);
}

static GstCaps *
gst_videotestsrc_src_negotiate (GstPad * pad)
{
  GstCaps *caps;
  GstCaps *temp;

  /* get all possible caps on this link */
  caps = gst_pad_get_allowed_caps (pad);
  temp = gst_caps_normalize (caps);
  gst_caps_unref (caps);
  caps = temp;

  if (gst_caps_get_size (caps) > 0) {
    GstStructure *structure;

    /* pick the first one */
    temp = gst_caps_copy_nth (caps, 0);
    gst_caps_unref (caps);
    caps = temp;

    structure = gst_caps_get_structure (caps, 0);

    gst_caps_structure_fixate_field_nearest_int (structure, "width", 320);
    gst_caps_structure_fixate_field_nearest_int (structure, "height", 240);
    gst_caps_structure_fixate_field_nearest_double (structure, "framerate",
        30.0);
  }

  return caps;
}

static GstPadLinkReturn
gst_videotestsrc_src_link (GstPad * pad, GstPad * peer)
{
  return GST_PAD_LINK_OK;
}

static gboolean
gst_videotestsrc_parse_caps (const GstCaps * caps,
    gint * width, gint * height, gdouble * rate,
    struct fourcc_list_struct **fourcc)
{
  const GstStructure *structure;
  GstPadLinkReturn ret;

  GST_DEBUG ("parsing caps");

  if (gst_caps_get_size (caps) < 1)
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);

  *fourcc = paintinfo_find_by_structure (structure);
  if (!*fourcc) {
    g_critical ("videotestsrc format not found");
    return FALSE;
  }

  ret = gst_structure_get_int (structure, "width", width);
  ret &= gst_structure_get_int (structure, "height", height);
  ret &= gst_structure_get_double (structure, "framerate", rate);

  return ret;
}

#if 0
static gboolean
gst_videotestsrc_src_accept_caps (GstPad * pad, const GstCaps * caps)
{
  gint width, height;
  gdouble rate;
  struct fourcc_list_struct *fourcc;

  return gst_videotestsrc_parse_caps (caps, &width, &height, &rate, &fourcc);
}
#endif

static gboolean
gst_videotestsrc_setcaps (GstPad * pad, GstCaps * caps)
{
  gboolean res;
  gint width, height;
  gdouble rate;
  struct fourcc_list_struct *fourcc;
  GstVideotestsrc *videotestsrc;

  videotestsrc = GST_VIDEOTESTSRC (GST_PAD_PARENT (pad));

  res = gst_videotestsrc_parse_caps (caps, &width, &height, &rate, &fourcc);
  if (res) {
    /* looks ok here */
    videotestsrc->fourcc = fourcc;
    videotestsrc->width = width;
    videotestsrc->height = height;
    videotestsrc->rate = rate;
    videotestsrc->bpp = videotestsrc->fourcc->bitspp;

    GST_DEBUG_OBJECT (videotestsrc, "size %dx%d, %f fps", videotestsrc->width,
        videotestsrc->height, videotestsrc->rate);
  }

  return res;
}

static void
gst_videotestsrc_src_unlink (GstPad * pad)
{
  GstVideotestsrc *videotestsrc;

  videotestsrc = GST_VIDEOTESTSRC (GST_PAD_PARENT (pad));
}

static gboolean
gst_videotestsrc_activate (GstPad * pad, GstActivateMode mode)
{
  gboolean result = FALSE;
  GstVideotestsrc *videotestsrc;

  videotestsrc = GST_VIDEOTESTSRC (GST_OBJECT_PARENT (pad));

  switch (mode) {
    case GST_ACTIVATE_PULL:
      break;
    case GST_ACTIVATE_PUSH:
      /* if we have a scheduler we can start the task */
      if (GST_ELEMENT_SCHEDULER (videotestsrc)) {
        GST_STREAM_LOCK (pad);
        GST_RPAD_TASK (pad) =
            gst_scheduler_create_task (GST_ELEMENT_SCHEDULER (videotestsrc),
            (GstTaskFunction) gst_videotestsrc_loop, pad);

        gst_task_start (GST_RPAD_TASK (pad));
        GST_STREAM_UNLOCK (pad);
        result = TRUE;
      }
      break;
    case GST_ACTIVATE_NONE:
      /* step 1, unblock clock sync (if any) */

      /* step 2, make sure streaming finishes */
      GST_STREAM_LOCK (pad);
      /* step 3, stop the task */
      gst_task_stop (GST_RPAD_TASK (pad));
      gst_object_unref (GST_OBJECT (GST_RPAD_TASK (pad)));
      GST_STREAM_UNLOCK (pad);

      result = TRUE;
      break;
    default:
      result = FALSE;
      break;
  }
  return result;
}


static GstElementStateReturn
gst_videotestsrc_change_state (GstElement * element)
{
  GstVideotestsrc *videotestsrc;
  GstElementStateReturn result;

  videotestsrc = GST_VIDEOTESTSRC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      videotestsrc->num_buffers_left = videotestsrc->num_buffers;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
  }

  result = parent_class->change_state (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      videotestsrc->timestamp_offset = 0;
      videotestsrc->running_time = 0;
      videotestsrc->n_frames = 0;
      break;
    case GST_STATE_READY_TO_NULL:
      break;
  }

  return result;
}

static GstCaps *
gst_videotestsrc_get_capslist (void)
{
  return gst_caps_copy (capslist);
}

static void
generate_capslist (void)
{
  if (!capslist) {
    GstCaps *caps;
    GstStructure *structure;
    int i;

    caps = gst_caps_new_empty ();
    for (i = 0; i < n_fourccs; i++) {
      structure = paint_get_structure (fourcc_list + i);
      gst_structure_set (structure,
          "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "framerate", GST_TYPE_DOUBLE_RANGE, 0.0, G_MAXDOUBLE, NULL);
      gst_caps_append_structure (caps, structure);
    }

    capslist = caps;
  }
}

#if 0
static GstCaps *
gst_videotestsrc_get_capslist_size (int width, int height, double rate)
{
  GstCaps *caps;
  GstStructure *structure;
  int i;

  caps = gst_caps_new_empty ();
  for (i = 0; i < n_fourccs; i++) {
    structure = paint_get_structure (fourcc_list + i);
    gst_structure_set (structure,
        "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height, "framerate", G_TYPE_INT, rate, NULL);
    gst_caps_append_structure (caps, structure);
  }

  return caps;
}
#endif

static GstCaps *
gst_videotestsrc_getcaps (GstPad * pad)
{
  GstVideotestsrc *vts;

  vts = GST_VIDEOTESTSRC (GST_PAD_PARENT (pad));

  return gst_videotestsrc_get_capslist ();
}

static void
gst_videotestsrc_init (GstVideotestsrc * videotestsrc)
{
  videotestsrc->srcpad =
      gst_pad_new_from_template (gst_videotestsrc_src_template_factory (),
      "src");
  gst_pad_set_getcaps_function (videotestsrc->srcpad, gst_videotestsrc_getcaps);
  gst_pad_set_setcaps_function (videotestsrc->srcpad, gst_videotestsrc_setcaps);
  gst_element_add_pad (GST_ELEMENT (videotestsrc), videotestsrc->srcpad);
  gst_pad_set_activate_function (videotestsrc->srcpad,
      gst_videotestsrc_activate);
  gst_pad_set_loop_function (videotestsrc->srcpad, gst_videotestsrc_loop);
  gst_pad_set_link_function (videotestsrc->srcpad, gst_videotestsrc_src_link);
  gst_pad_set_unlink_function (videotestsrc->srcpad,
      gst_videotestsrc_src_unlink);
  gst_pad_set_query_function (videotestsrc->srcpad, gst_videotestsrc_src_query);
  gst_pad_set_query_type_function (videotestsrc->srcpad,
      gst_videotestsrc_get_query_types);
  gst_pad_set_event_function (videotestsrc->srcpad,
      gst_videotestsrc_handle_src_event);


  gst_videotestsrc_set_pattern (videotestsrc, GST_VIDEOTESTSRC_SMPTE);

  videotestsrc->num_buffers = -1;
  videotestsrc->num_buffers_left = -1;
  videotestsrc->sync = TRUE;
  videotestsrc->need_discont = FALSE;
  videotestsrc->loop = FALSE;
  videotestsrc->segment_start_frame = -1;
  videotestsrc->segment_end_frame = -1;
  videotestsrc->timestamp_offset = 0;
}


static const GstQueryType *
gst_videotestsrc_get_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_POSITION,
    0,
  };

  return query_types;
}

static gboolean
gst_videotestsrc_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = FALSE;
  GstVideotestsrc *videotestsrc = GST_VIDEOTESTSRC (GST_PAD_PARENT (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 current;

      gst_query_parse_position (query, &format, NULL, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          current = videotestsrc->running_time;
          res = TRUE;
          break;
        case GST_FORMAT_DEFAULT:       /* frames */
          current = videotestsrc->n_frames;
          res = TRUE;
          break;
        default:
          break;
      }
      if (res) {
        gst_query_set_position (query, format, current, -1);
      }
      break;
    }
    default:
      break;
  }

  return res;
}

#if 0
static const GstEventMask *
gst_videotestsrc_get_event_masks (GstPad * pad)
{
  static const GstEventMask src_event_masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH},
    {0,}
  };

  return src_event_masks;
}
#endif

static gboolean
gst_videotestsrc_handle_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstVideotestsrc *videotestsrc;
  gint64 new_n_frames;

  g_return_val_if_fail (pad != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  videotestsrc = GST_VIDEOTESTSRC (GST_PAD_PARENT (pad));
  new_n_frames = videotestsrc->n_frames;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      switch (GST_EVENT_SEEK_FORMAT (event)) {
        case GST_FORMAT_TIME:
          new_n_frames =
              GST_EVENT_SEEK_OFFSET (event) * (double) videotestsrc->rate /
              GST_SECOND;
          videotestsrc->segment_start_frame = new_n_frames;
          videotestsrc->segment_end_frame =
              GST_EVENT_SEEK_ENDOFFSET (event) * (double) videotestsrc->rate /
              GST_SECOND;
          videotestsrc->loop =
              GST_EVENT_SEEK_TYPE (event) & GST_SEEK_FLAG_SEGMENT_LOOP;
          break;
        case GST_FORMAT_DEFAULT:
          new_n_frames = GST_EVENT_SEEK_OFFSET (event);
          videotestsrc->segment_start_frame = new_n_frames;
          videotestsrc->segment_end_frame = GST_EVENT_SEEK_ENDOFFSET (event);
          videotestsrc->loop =
              GST_EVENT_SEEK_TYPE (event) & GST_SEEK_FLAG_SEGMENT_LOOP;
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    }
    default:
      res = FALSE;
      break;
  }

  if (videotestsrc->n_frames != new_n_frames) {
    videotestsrc->n_frames = new_n_frames;
    videotestsrc->need_discont = TRUE;
  }
  gst_event_unref (event);

  return res;
}

static void
gst_videotestsrc_loop (GstPad * pad)
{
  GstVideotestsrc *videotestsrc;
  gulong newsize;
  GstBuffer *outbuf;

  videotestsrc = GST_VIDEOTESTSRC (GST_PAD_PARENT (pad));
  GST_LOG_OBJECT (videotestsrc, "get");

  /* no format on this pad, find a format */
  if (GST_PAD_CAPS (pad) == NULL) {
    GstCaps *caps;

    caps = gst_videotestsrc_src_negotiate (pad);
    if (!gst_pad_set_caps (videotestsrc->srcpad, caps)) {
      GST_ELEMENT_ERROR (videotestsrc, CORE, NEGOTIATION, (NULL),
          ("format could not be negotiated"));
      gst_pad_push_event (pad, gst_event_new (GST_EVENT_EOS));
      goto need_pause;
    }
  }

  if (videotestsrc->fourcc == NULL) {
    GST_ELEMENT_ERROR (videotestsrc, CORE, NEGOTIATION, (NULL),
        ("format wasn't negotiated before get function"));
    gst_pad_push_event (pad, gst_event_new (GST_EVENT_EOS));
    goto need_pause;
  }

  if (videotestsrc->need_discont) {
    GstClockTime ts = videotestsrc->timestamp_offset +
        (videotestsrc->n_frames * GST_SECOND) / (double) videotestsrc->rate;

    videotestsrc->need_discont = FALSE;
    gst_pad_push_event (pad, gst_event_new_discontinuous (FALSE,
            GST_FORMAT_TIME, ts, NULL));
  }

  if ((videotestsrc->segment_end_frame != -1) &&
      (videotestsrc->n_frames > videotestsrc->segment_end_frame)) {
    if (videotestsrc->loop) {
      //gst_pad_push_event (pad, gst_event_new (GST_EVENT_SEGMENT_DONE));
    } else {
      gst_pad_push_event (pad, gst_event_new (GST_EVENT_EOS));
      goto need_pause;
    }
  }

  if (videotestsrc->num_buffers_left == 0) {
    gst_pad_push_event (pad, gst_event_new (GST_EVENT_EOS));
    goto need_pause;
  } else {
    if (videotestsrc->num_buffers_left > 0)
      videotestsrc->num_buffers_left--;
  }

  newsize = gst_videotestsrc_get_size (videotestsrc, videotestsrc->width,
      videotestsrc->height);
  g_return_if_fail (newsize > 0);

  GST_LOG_OBJECT (videotestsrc, "creating buffer of %ld bytes for %dx%d image",
      newsize, videotestsrc->width, videotestsrc->height);

  outbuf =
      gst_pad_alloc_buffer (pad, GST_BUFFER_OFFSET_NONE, newsize,
      GST_RPAD_CAPS (pad));

  if (GST_BUFFER_CAPS (outbuf) != GST_PAD_CAPS (pad)) {
    if (!gst_pad_set_caps (pad, GST_BUFFER_CAPS (outbuf))) {
      GST_ELEMENT_ERROR (videotestsrc, CORE, NEGOTIATION, (NULL),
          ("format wasn't accepted"));
      gst_pad_push_event (pad, gst_event_new (GST_EVENT_EOS));
      goto need_pause;
    }
  }

  videotestsrc->make_image (videotestsrc, (void *) GST_BUFFER_DATA (outbuf),
      videotestsrc->width, videotestsrc->height);

  GST_BUFFER_TIMESTAMP (outbuf) = videotestsrc->timestamp_offset +
      videotestsrc->running_time;
  GST_BUFFER_DURATION (outbuf) = GST_SECOND / (double) videotestsrc->rate;
  if (videotestsrc->sync) {
    /* FIXME this is not correct if we do QoS */
    if (videotestsrc->clock) {
      //gst_element_wait (GST_ELEMENT (videotestsrc), GST_BUFFER_TIMESTAMP (outbuf));
    }
  }
  videotestsrc->n_frames++;
  videotestsrc->running_time += GST_BUFFER_DURATION (outbuf);

  if (gst_pad_push (pad, outbuf) != GST_FLOW_OK)
    goto need_pause;

  return;

need_pause:
  {
    gst_task_pause (GST_RPAD_TASK (pad));
  }
}

static void
gst_videotestsrc_set_pattern (GstVideotestsrc * videotestsrc, int pattern_type)
{
  videotestsrc->type = pattern_type;

  GST_DEBUG_OBJECT (videotestsrc, "setting pattern to %d", pattern_type);
  switch (pattern_type) {
    case GST_VIDEOTESTSRC_SMPTE:
      videotestsrc->make_image = gst_videotestsrc_smpte;
      break;
    case GST_VIDEOTESTSRC_SNOW:
      videotestsrc->make_image = gst_videotestsrc_snow;
      break;
    case GST_VIDEOTESTSRC_BLACK:
      videotestsrc->make_image = gst_videotestsrc_black;
      break;
    default:
      g_assert_not_reached ();
  }
}

static void
gst_videotestsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideotestsrc *videotestsrc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VIDEOTESTSRC (object));
  videotestsrc = GST_VIDEOTESTSRC (object);

  switch (prop_id) {
    case ARG_TYPE:
      gst_videotestsrc_set_pattern (videotestsrc, g_value_get_enum (value));
      break;
    case ARG_SYNC:
      videotestsrc->sync = g_value_get_boolean (value);
      break;
    case ARG_NUM_BUFFERS:
      videotestsrc->num_buffers = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_videotestsrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideotestsrc *videotestsrc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VIDEOTESTSRC (object));
  videotestsrc = GST_VIDEOTESTSRC (object);

  switch (prop_id) {
    case ARG_TYPE:
      g_value_set_enum (value, videotestsrc->type);
      break;
    case ARG_SYNC:
      g_value_set_boolean (value, videotestsrc->sync);
      break;
    case ARG_NUM_BUFFERS:
      g_value_set_int (value, videotestsrc->num_buffers);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  oil_init ();

  generate_capslist ();

  return gst_element_register (plugin, "videotestsrc", GST_RANK_NONE,
      GST_TYPE_VIDEOTESTSRC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videotestsrc",
    "Creates a test video stream",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
