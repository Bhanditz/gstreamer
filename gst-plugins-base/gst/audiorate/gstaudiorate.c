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
#include <string.h>

#include <gst/gst.h>
#include <gst/audio/audio.h>

#define GST_CAT_DEFAULT audio_rate_debug
GST_DEBUG_CATEGORY_STATIC (audio_rate_debug);

#define GST_TYPE_AUDIO_RATE \
  (gst_audio_rate_get_type())
#define GST_AUDIO_RATE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIO_RATE,GstAudioRate))
#define GST_AUDIO_RATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIO_RATE,GstAudioRate))
#define GST_IS_AUDIO_RATE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIO_RATE))
#define GST_IS_AUDIO_RATE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIO_RATE))

typedef struct _GstAudioRate GstAudioRate;
typedef struct _GstAudioRateClass GstAudioRateClass;

struct _GstAudioRate
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  /* audio format */
  gint bytes_per_sample;
  gint rate;

  /* stats */
  guint64 in, out, add, drop;
  gboolean silent;

  /* audio state */
  guint64 offset;
  guint64 next_offset;

  gboolean discont;
  GstSegment segment;
};

struct _GstAudioRateClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
static const GstElementDetails audio_rate_details =
GST_ELEMENT_DETAILS ("Audio rate adjuster",
    "Filter/Effect/Audio",
    "Drops/duplicates/adjusts timestamps on audio samples to make a perfect stream",
    "Wim Taymans <wim@fluendo.com>");

/* GstAudioRate signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_SILENT  TRUE

enum
{
  ARG_0,
  ARG_IN,
  ARG_OUT,
  ARG_ADD,
  ARG_DROP,
  ARG_SILENT,
  /* FILL ME */
};

static GstStaticPadTemplate gst_audio_rate_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_INT_PAD_TEMPLATE_CAPS ";"
        GST_AUDIO_FLOAT_PAD_TEMPLATE_CAPS)
    );

static GstStaticPadTemplate gst_audio_rate_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_INT_PAD_TEMPLATE_CAPS ";"
        GST_AUDIO_FLOAT_PAD_TEMPLATE_CAPS)
    );

static void gst_audio_rate_base_init (gpointer g_class);
static void gst_audio_rate_class_init (GstAudioRateClass * klass);
static void gst_audio_rate_init (GstAudioRate * audiorate);
static gboolean gst_audio_rate_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_audio_rate_src_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_audio_rate_chain (GstPad * pad, GstBuffer * buf);

static void gst_audio_rate_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_audio_rate_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_audio_rate_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

/*static guint gst_audio_rate_signals[LAST_SIGNAL] = { 0 }; */

static GType
gst_audio_rate_get_type (void)
{
  static GType audio_rate_type = 0;

  if (!audio_rate_type) {
    static const GTypeInfo audio_rate_info = {
      sizeof (GstAudioRateClass),
      gst_audio_rate_base_init,
      NULL,
      (GClassInitFunc) gst_audio_rate_class_init,
      NULL,
      NULL,
      sizeof (GstAudioRate),
      0,
      (GInstanceInitFunc) gst_audio_rate_init,
    };

    audio_rate_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstAudioRate", &audio_rate_info, 0);
  }

  return audio_rate_type;
}

static void
gst_audio_rate_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &audio_rate_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_audio_rate_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_audio_rate_src_template));
}
static void
gst_audio_rate_class_init (GstAudioRateClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  object_class->set_property = gst_audio_rate_set_property;
  object_class->get_property = gst_audio_rate_get_property;

  g_object_class_install_property (object_class, ARG_IN,
      g_param_spec_uint64 ("in", "In",
          "Number of input samples", 0, G_MAXUINT64, 0, G_PARAM_READABLE));
  g_object_class_install_property (object_class, ARG_OUT,
      g_param_spec_uint64 ("out", "Out",
          "Number of output samples", 0, G_MAXUINT64, 0, G_PARAM_READABLE));
  g_object_class_install_property (object_class, ARG_ADD,
      g_param_spec_uint64 ("add", "Add",
          "Number of added samples", 0, G_MAXUINT64, 0, G_PARAM_READABLE));
  g_object_class_install_property (object_class, ARG_DROP,
      g_param_spec_uint64 ("drop", "Drop",
          "Number of dropped samples", 0, G_MAXUINT64, 0, G_PARAM_READABLE));
  g_object_class_install_property (object_class, ARG_SILENT,
      g_param_spec_boolean ("silent", "silent",
          "Don't emit notify for dropped and duplicated frames",
          DEFAULT_SILENT, G_PARAM_READWRITE));

  element_class->change_state = gst_audio_rate_change_state;
}

static void
gst_audio_rate_reset (GstAudioRate * audiorate)
{
  audiorate->offset = -1;
  audiorate->next_offset = -1;
  audiorate->discont = TRUE;
  gst_segment_init (&audiorate->segment, GST_FORMAT_UNDEFINED);

  GST_DEBUG_OBJECT (audiorate, "handle reset");
}

static gboolean
gst_audio_rate_setcaps (GstPad * pad, GstCaps * caps)
{
  GstAudioRate *audiorate;
  GstStructure *structure;
  GstPad *otherpad;
  gboolean ret = FALSE;
  gint channels, width, rate;

  audiorate = GST_AUDIO_RATE (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "channels", &channels))
    goto wrong_caps;
  if (!gst_structure_get_int (structure, "width", &width))
    goto wrong_caps;
  if (!gst_structure_get_int (structure, "rate", &rate))
    goto wrong_caps;

  audiorate->bytes_per_sample = channels * (width / 8);
  if (audiorate->bytes_per_sample == 0)
    goto wrong_format;

  audiorate->rate = rate;

  /* the format is correct, configure caps on other pad */
  otherpad = (pad == audiorate->srcpad) ? audiorate->sinkpad :
      audiorate->srcpad;

  ret = gst_pad_set_caps (otherpad, caps);

done:
  gst_object_unref (audiorate);
  return ret;

  /* ERRORS */
wrong_caps:
  {
    GST_DEBUG_OBJECT (audiorate, "could not get channels/width from caps");
    goto done;
  }
wrong_format:
  {
    GST_DEBUG_OBJECT (audiorate, "bytes_per_samples gave 0");
    goto done;
  }
}

static void
gst_audio_rate_init (GstAudioRate * audiorate)
{
  audiorate->sinkpad =
      gst_pad_new_from_static_template (&gst_audio_rate_sink_template, "sink");
  gst_pad_set_event_function (audiorate->sinkpad, gst_audio_rate_sink_event);
  gst_pad_set_chain_function (audiorate->sinkpad, gst_audio_rate_chain);
  gst_pad_set_setcaps_function (audiorate->sinkpad, gst_audio_rate_setcaps);
  gst_pad_set_getcaps_function (audiorate->sinkpad, gst_pad_proxy_getcaps);
  gst_element_add_pad (GST_ELEMENT (audiorate), audiorate->sinkpad);

  audiorate->srcpad =
      gst_pad_new_from_static_template (&gst_audio_rate_src_template, "src");
  gst_pad_set_event_function (audiorate->srcpad, gst_audio_rate_src_event);
  gst_pad_set_setcaps_function (audiorate->srcpad, gst_audio_rate_setcaps);
  gst_pad_set_getcaps_function (audiorate->srcpad, gst_pad_proxy_getcaps);
  gst_element_add_pad (GST_ELEMENT (audiorate), audiorate->srcpad);

  audiorate->in = 0;
  audiorate->out = 0;
  audiorate->drop = 0;
  audiorate->add = 0;
  audiorate->silent = DEFAULT_SILENT;
}

static gboolean
gst_audio_rate_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res;
  GstAudioRate *audiorate;

  audiorate = GST_AUDIO_RATE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT (audiorate, "handling FLUSH_STOP");
      gst_audio_rate_reset (audiorate);
      res = gst_pad_push_event (audiorate->srcpad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;
      gboolean update;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      GST_DEBUG_OBJECT (audiorate, "handle NEWSEGMENT");
      /* FIXME:
       * - sparse stream support. For this, the update flag is TRUE and the
       *   start/time positions are updated, meaning that time progressed by
       *   time - old_time amount and we need to fill that gap with empty
       *   samples.
       * - fill the current segment if it has a valid stop position. This
       *   happens when the update flag is FALSE. With the segment helper we can
       *   calculate the accumulated time and compare this to the next_offset.
       */
      if (!update) {
        /* a new segment starts. We need to figure out what will be the next
         * sample offset. We mark the offsets as invalid so that the _chain
         * function will perform this calculation. */
        audiorate->offset = -1;
        audiorate->next_offset = -1;
      }

      gst_segment_set_newsegment_full (&audiorate->segment, update, rate, arate,
          format, start, stop, time);

      res = gst_pad_push_event (audiorate->srcpad, event);
      break;
    }
    case GST_EVENT_EOS:
    default:
      res = gst_pad_push_event (audiorate->srcpad, event);
      break;
  }

  gst_object_unref (audiorate);

  return res;
}

static gboolean
gst_audio_rate_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res;
  GstAudioRate *audiorate;

  audiorate = GST_AUDIO_RATE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    default:
      res = gst_pad_push_event (audiorate->sinkpad, event);
      break;
  }

  gst_object_unref (audiorate);

  return res;
}

static GstFlowReturn
gst_audio_rate_chain (GstPad * pad, GstBuffer * buf)
{
  GstAudioRate *audiorate;
  GstClockTime in_time, in_duration;
  guint64 in_offset, in_offset_end;
  guint in_size;
  GstFlowReturn ret = GST_FLOW_OK;

  audiorate = GST_AUDIO_RATE (gst_pad_get_parent (pad));

  if (audiorate->bytes_per_sample == 0)
    goto not_negotiated;

  if (audiorate->offset == -1) {
    gint64 pos;

    /* first buffer, we are negotiated and we have a segment, calculate the
     * current expected offsets based on the segment.time, which is the first
     * media time of the segment and should match the media time of the first
     * buffer in that segment, which is the offset expressed in DEFAULT units.
     */
    pos = audiorate->segment.time;
    if (pos != 0) {
      if (audiorate->segment.format == GST_FORMAT_TIME) {
        /* convert first timestamp of segment to sample position */
        pos = gst_util_uint64_scale_int (pos, audiorate->rate, GST_SECOND);
      } else {
        /* FIXME, we don't know, start from 0 then... */
        pos = 0;
      }
    }
    GST_DEBUG_OBJECT (audiorate, "resync to offset %" G_GINT64_FORMAT, pos);
    audiorate->offset = pos;
    audiorate->next_offset = pos;
  }

  audiorate->in++;

  in_time = GST_BUFFER_TIMESTAMP (buf);
  in_duration = GST_BUFFER_DURATION (buf);
  in_size = GST_BUFFER_SIZE (buf);
  in_offset = GST_BUFFER_OFFSET (buf);
  in_offset_end = GST_BUFFER_OFFSET_END (buf);

  GST_LOG_OBJECT (audiorate,
      "in_time:%" GST_TIME_FORMAT ", in_duration:%" GST_TIME_FORMAT
      ", in_size:%u, in_offset:%lld, in_offset_end:%lld" ", ->next_offset:%lld",
      GST_TIME_ARGS (in_time), GST_TIME_ARGS (in_duration), in_size, in_offset,
      in_offset_end, audiorate->next_offset);

  if (in_offset == GST_CLOCK_TIME_NONE || in_offset_end == GST_CLOCK_TIME_NONE) {
    GST_WARNING_OBJECT (audiorate, "audiorate got buffer without offsets");
    in_offset = audiorate->offset;
    in_offset_end = audiorate->offset + in_size / audiorate->bytes_per_sample;
    GST_WARNING_OBJECT (audiorate, "in_offset:%lld, in_offset_end:%lld",
        in_offset, in_offset_end);
  }

  /* do we need to insert samples */
  if (in_offset > audiorate->next_offset) {
    GstBuffer *fill;
    gint fillsize;
    guint64 fillsamples;

    fillsamples = in_offset - audiorate->next_offset;
    fillsize = fillsamples * audiorate->bytes_per_sample;

    fill = gst_buffer_new_and_alloc (fillsize);
    memset (GST_BUFFER_DATA (fill), 0, fillsize);

    GST_LOG_OBJECT (audiorate, "inserting %lld samples", fillsamples);

    GST_BUFFER_DURATION (fill) = in_duration * fillsize / in_size;
    GST_BUFFER_TIMESTAMP (fill) = in_time - GST_BUFFER_DURATION (fill);
    GST_BUFFER_OFFSET (fill) = audiorate->next_offset;
    GST_BUFFER_OFFSET_END (fill) = in_offset;

    /* we created this buffer to filla gap */
    GST_BUFFER_FLAG_SET (fill, GST_BUFFER_FLAG_GAP);
    /* set discont if it's pending, this is mostly done for the first buffer and
     * after a flushing seek */
    if (audiorate->discont) {
      GST_BUFFER_FLAG_SET (fill, GST_BUFFER_FLAG_DISCONT);
      audiorate->discont = FALSE;
    }

    ret = gst_pad_push (audiorate->srcpad, fill);
    if (ret != GST_FLOW_OK)
      goto beach;
    audiorate->out++;
    audiorate->add += fillsamples;

    if (!audiorate->silent)
      g_object_notify (G_OBJECT (audiorate), "add");
  } else if (in_offset < audiorate->next_offset) {
    /* need to remove samples */
    if (in_offset_end <= audiorate->next_offset) {
      guint64 drop = in_size / audiorate->bytes_per_sample;

      audiorate->drop += drop;

      GST_LOG_OBJECT (audiorate, "dropping %lld samples", drop);

      /* we can drop the buffer completely */
      gst_buffer_unref (buf);

      if (!audiorate->silent)
        g_object_notify (G_OBJECT (audiorate), "drop");

      goto beach;
    } else {
      guint64 truncsamples;
      guint truncsize, leftsize;
      GstBuffer *trunc;

      /* truncate buffer */
      truncsamples = audiorate->next_offset - in_offset;
      truncsize = truncsamples * audiorate->bytes_per_sample;
      leftsize = in_size - truncsize;

      trunc = gst_buffer_create_sub (buf, truncsize, leftsize);
      GST_BUFFER_DURATION (trunc) = in_duration * leftsize / in_size;
      GST_BUFFER_TIMESTAMP (trunc) =
          in_time + in_duration - GST_BUFFER_DURATION (trunc);
      GST_BUFFER_OFFSET (trunc) = audiorate->next_offset;
      GST_BUFFER_OFFSET_END (trunc) = in_offset_end;

      GST_LOG_OBJECT (audiorate, "truncating %lld samples", truncsamples);

      gst_buffer_unref (buf);
      buf = trunc;

      audiorate->drop += truncsamples;
    }
  }
  if (audiorate->discont) {
    /* we need to output a discont buffer, do so now */
    GST_DEBUG_OBJECT (audiorate, "marking DISCONT on output buffer");
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
    audiorate->discont = FALSE;
  } else if (GST_BUFFER_IS_DISCONT (buf)) {
    /* else we make everything continuous so we can safely remove the DISCONT
     * flag from the buffer if there was one */
    GST_DEBUG_OBJECT (audiorate, "removing DISCONT from buffer");
    buf = gst_buffer_make_metadata_writable (buf);
    GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DISCONT);
  }
  ret = gst_pad_push (audiorate->srcpad, buf);
  audiorate->out++;

  audiorate->next_offset = in_offset_end;
beach:
  audiorate->offset += in_size / audiorate->bytes_per_sample;

  gst_object_unref (audiorate);

  return ret;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (audiorate, STREAM, FORMAT,
        (NULL), ("pipeline error, format was not negotiated"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static void
gst_audio_rate_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstAudioRate *audiorate = GST_AUDIO_RATE (object);

  switch (prop_id) {
    case ARG_SILENT:
      audiorate->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audio_rate_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstAudioRate *audiorate = GST_AUDIO_RATE (object);

  switch (prop_id) {
    case ARG_IN:
      g_value_set_uint64 (value, audiorate->in);
      break;
    case ARG_OUT:
      g_value_set_uint64 (value, audiorate->out);
      break;
    case ARG_ADD:
      g_value_set_uint64 (value, audiorate->add);
      break;
    case ARG_DROP:
      g_value_set_uint64 (value, audiorate->drop);
      break;
    case ARG_SILENT:
      g_value_set_boolean (value, audiorate->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_audio_rate_change_state (GstElement * element, GstStateChange transition)
{
  GstAudioRate *audiorate = GST_AUDIO_RATE (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      audiorate->in = 0;
      audiorate->out = 0;
      audiorate->drop = 0;
      audiorate->bytes_per_sample = 0;
      audiorate->add = 0;
      gst_audio_rate_reset (audiorate);
      break;
    default:
      break;
  }

  if (parent_class->change_state)
    return parent_class->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (audio_rate_debug, "audiorate", 0,
      "AudioRate stream fixer");

  return gst_element_register (plugin, "audiorate", GST_RANK_NONE,
      GST_TYPE_AUDIO_RATE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "audiorate",
    "Adjusts audio frames",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
