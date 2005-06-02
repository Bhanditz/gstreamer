/* gstgoom.c: implementation of goom drawing element
 * Copyright (C) <2001> Richard Boulton <richard@tartarus.org>
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
#include <gst/video/video.h>
#include <gst/base/gstadapter.h>
#include "goom_core.h"

GST_DEBUG_CATEGORY_STATIC (goom_debug);
#define GST_CAT_DEFAULT goom_debug

#define GOOM_SAMPLES 512

#define GST_TYPE_GOOM (gst_goom_get_type())
#define GST_GOOM(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GOOM,GstGOOM))
#define GST_GOOM_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GOOM,GstGOOM))
#define GST_IS_GOOM(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GOOM))
#define GST_IS_GOOM_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GOOM))

typedef struct _GstGOOM GstGOOM;
typedef struct _GstGOOMClass GstGOOMClass;

struct _GstGOOM
{
  GstElement element;

  /* pads */
  GstPad *sinkpad, *srcpad;
  GstAdapter *adapter;

  /* input tracking */
  gint sample_rate;

  gint16 datain[2][GOOM_SAMPLES];
  /* the timestamp of the next frame */
  GstClockTime audio_basetime;
  guint64 samples_consumed;

  /* video state */
  gdouble fps;
  gint width;
  gint height;
  gint channels;

  gboolean disposed;
};

struct _GstGOOMClass
{
  GstElementClass parent_class;
};

GType gst_goom_get_type (void);


/* elementfactory information */
static GstElementDetails gst_goom_details = {
  "GOOM: what a GOOM!",
  "Visualization",
  "Takes frames of data and outputs video frames using the GOOM filter",
  "Wim Taymans <wim.taymans@chello.be>"
};

/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
      /* FILL ME */
};

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_xRGB_HOST_ENDIAN)
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",    /* the name of the pads */
    GST_PAD_SINK,               /* type of the pad */
    GST_PAD_ALWAYS,             /* ALWAYS/SOMETIMES */
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 8000, 96000 ], " "channels = (int) [ 1, 2 ]")
    );


static void gst_goom_class_init (GstGOOMClass * klass);
static void gst_goom_base_init (GstGOOMClass * klass);
static void gst_goom_init (GstGOOM * goom);
static void gst_goom_dispose (GObject * object);

static GstElementStateReturn gst_goom_change_state (GstElement * element);

static GstFlowReturn gst_goom_chain (GstPad * pad, GstBuffer * buffer);

static GstPadLinkReturn gst_goom_sink_setcaps (GstPad * pad, GstCaps * caps);
static GstPadLinkReturn gst_goom_src_setcaps (GstPad * pad, GstCaps * caps);

static GstElementClass *parent_class = NULL;

GType
gst_goom_get_type (void)
{
  static GType type = 0;

  if (!type) {
    static const GTypeInfo info = {
      sizeof (GstGOOMClass),
      (GBaseInitFunc) gst_goom_base_init,
      NULL,
      (GClassInitFunc) gst_goom_class_init,
      NULL,
      NULL,
      sizeof (GstGOOM),
      0,
      (GInstanceInitFunc) gst_goom_init,
    };

    type = g_type_register_static (GST_TYPE_ELEMENT, "GstGOOM", &info, 0);
  }
  return type;
}

static void
gst_goom_base_init (GstGOOMClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &gst_goom_details);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
}

static void
gst_goom_class_init (GstGOOMClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->dispose = gst_goom_dispose;

  gstelement_class->change_state = gst_goom_change_state;

  GST_DEBUG_CATEGORY_INIT (goom_debug, "goom", 0, "goom visualisation element");
}

static void
gst_goom_init (GstGOOM * goom)
{
  /* create the sink and src pads */
  goom->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_template),
      "sink");
  goom->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_template),
      "src");
  gst_element_add_pad (GST_ELEMENT (goom), goom->sinkpad);
  gst_element_add_pad (GST_ELEMENT (goom), goom->srcpad);

  gst_pad_set_chain_function (goom->sinkpad, gst_goom_chain);
  gst_pad_set_setcaps_function (goom->sinkpad, gst_goom_sink_setcaps);
  gst_pad_set_setcaps_function (goom->srcpad, gst_goom_src_setcaps);

  goom->adapter = gst_adapter_new ();

  goom->width = 320;
  goom->height = 200;
  goom->fps = 25.;              /* desired frame rate */
  goom->channels = 0;
  goom->sample_rate = 0;
  goom->audio_basetime = GST_CLOCK_TIME_NONE;
  goom->samples_consumed = 0;
  goom->disposed = FALSE;

  goom_init (goom->width, goom->height);
}

static void
gst_goom_dispose (GObject * object)
{
  GstGOOM *goom = GST_GOOM (object);

  if (!goom->disposed) {
    goom_close ();
    goom->disposed = TRUE;

    g_object_unref (goom->adapter);
    goom->adapter = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
gst_goom_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstGOOM *goom;
  GstStructure *structure;

  goom = GST_GOOM (GST_PAD_PARENT (pad));

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "channels", &goom->channels);
  gst_structure_get_int (structure, "rate", &goom->sample_rate);

  return TRUE;
}

static gboolean
gst_goom_src_setcaps (GstPad * pad, GstCaps * caps)
{
  GstGOOM *goom;
  GstStructure *structure;

  goom = GST_GOOM (GST_PAD_PARENT (pad));

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", &goom->width);
  gst_structure_get_int (structure, "height", &goom->height);
  gst_structure_get_double (structure, "framerate", &goom->fps);

  goom_set_resolution (goom->width, goom->height);

  return TRUE;
}

static gboolean
gst_goom_src_negotiate (GstGOOM * goom)
{
  GstCaps *othercaps, *target, *intersect;
  GstStructure *structure;
  const GstCaps *templ;

  templ = gst_pad_get_pad_template_caps (goom->srcpad);

  /* see what the peer can do */
  othercaps = gst_pad_peer_get_caps (goom->srcpad);
  if (othercaps) {
    intersect = gst_caps_intersect (othercaps, templ);
    gst_caps_unref (othercaps);

    if (gst_caps_is_empty (intersect))
      goto no_format;

    target = gst_caps_copy_nth (intersect, 0);
    gst_caps_unref (intersect);
  } else {
    target = gst_caps_ref ((GstCaps *) templ);
  }

  structure = gst_caps_get_structure (target, 0);
  gst_caps_structure_fixate_field_nearest_int (structure, "width", 320);
  gst_caps_structure_fixate_field_nearest_int (structure, "height", 240);
  gst_caps_structure_fixate_field_nearest_double (structure, "framerate", 30.0);

  gst_pad_set_caps (goom->srcpad, target);
  gst_caps_unref (target);

  return TRUE;

no_format:
  {
    gst_caps_unref (intersect);
    gst_caps_unref (othercaps);
    return FALSE;
  }
}

static gboolean
gst_goom_event (GstPad * pad, GstEvent * event)
{
  gboolean res;
  GstGOOM *goom;

  goom = GST_GOOM (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_DISCONTINUOUS:
    {
      gint64 start = 0, stop = 0;

      gst_event_discont_get_value (event, GST_FORMAT_TIME, &start, &stop);
      gst_adapter_clear (goom->adapter);
      goom->audio_basetime = start;
      goom->samples_consumed = 0;
      GST_DEBUG ("Got discont. Adjusting time to=%" G_GUINT64_FORMAT, start);
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }
  return res;
}

static GstFlowReturn
gst_goom_chain (GstPad * pad, GstBuffer * bufin)
{
  GstGOOM *goom;
  guint32 bytesperread;
  gint16 *data;
  gint samples_per_frame;
  GstFlowReturn ret;

  goom = GST_GOOM (GST_PAD_PARENT (pad));

  if (goom->channels == 0)
    goto not_negotiated;

  if (goom->audio_basetime == GST_CLOCK_TIME_NONE)
    goom->audio_basetime = GST_BUFFER_TIMESTAMP (bufin);

  if (goom->audio_basetime == GST_CLOCK_TIME_NONE)
    goom->audio_basetime = 0;

  bytesperread = GOOM_SAMPLES * goom->channels * sizeof (gint16);
  samples_per_frame = goom->sample_rate / goom->fps;
  data = (gint16 *) GST_BUFFER_DATA (bufin);

  gst_adapter_push (goom->adapter, bufin);

  GST_DEBUG ("Input buffer has %d samples, time=%" G_GUINT64_FORMAT,
      GST_BUFFER_SIZE (bufin) * sizeof (gint16) * goom->channels,
      GST_BUFFER_TIMESTAMP (bufin));

  ret = GST_FLOW_OK;

  if (GST_PAD_CAPS (goom->srcpad) == NULL) {
    if (!gst_goom_src_negotiate (goom))
      goto no_format;
  }

  /* Collect samples until we have enough for an output frame */
  while (gst_adapter_available (goom->adapter) > MAX (bytesperread,
          samples_per_frame * goom->channels * sizeof (gint16))) {
    const guint16 *data;
    GstBuffer *bufout;
    guchar *out_frame;
    GstClockTimeDiff frame_duration;
    gint i;

    frame_duration = GST_SECOND / goom->fps;
    data = (const guint16 *) gst_adapter_peek (goom->adapter, bytesperread);

    if (goom->channels == 2) {
      for (i = 0; i < GOOM_SAMPLES; i++) {
        goom->datain[0][i] = *data++;
        goom->datain[1][i] = *data++;
      }
    } else {
      for (i = 0; i < GOOM_SAMPLES; i++) {
        goom->datain[0][i] = *data;
        goom->datain[1][i] = *data++;
      }
    }

    ret = gst_pad_alloc_buffer (goom->srcpad, GST_BUFFER_OFFSET_NONE,
        goom->width * goom->height * 4, GST_PAD_CAPS (goom->srcpad), &bufout);
    if (ret != GST_FLOW_OK)
      break;

    GST_BUFFER_TIMESTAMP (bufout) =
        goom->audio_basetime +
        (GST_SECOND * goom->samples_consumed / goom->sample_rate);
    GST_BUFFER_DURATION (bufout) = frame_duration;
    GST_BUFFER_SIZE (bufout) = goom->width * goom->height * 4;

    out_frame = (guchar *) goom_update (goom->datain);
    memcpy (GST_BUFFER_DATA (bufout), out_frame, GST_BUFFER_SIZE (bufout));

    GST_DEBUG ("Pushing frame with time=%" G_GUINT64_FORMAT ", duration=%"
        G_GUINT64_FORMAT, GST_BUFFER_TIMESTAMP (bufout),
        GST_BUFFER_DURATION (bufout));
    ret = gst_pad_push (goom->srcpad, bufout);

    goom->samples_consumed += samples_per_frame;
    gst_adapter_flush (goom->adapter,
        samples_per_frame * goom->channels * sizeof (gint16));

    if (ret != GST_FLOW_OK)
      break;
  }
  return ret;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (goom, CORE, NEGOTIATION, (NULL),
        ("Format wasn't negotiated before chain function"));
    gst_buffer_unref (bufin);
    return GST_FLOW_NOT_NEGOTIATED;
  }
no_format:
  {
    GST_ELEMENT_ERROR (goom, CORE, NEGOTIATION, (NULL),
        ("Could not negotiate format"));
    gst_buffer_unref (bufin);
    return GST_FLOW_ERROR;
  }
}

static GstElementStateReturn
gst_goom_change_state (GstElement * element)
{
  GstGOOM *goom = GST_GOOM (element);
  gint transition;
  GstElementStateReturn ret;

  transition = GST_STATE_TRANSITION (element);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      goom->audio_basetime = GST_CLOCK_TIME_NONE;
      gst_adapter_clear (goom->adapter);
      goom->channels = 0;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);

  switch (transition) {
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "goom", GST_RANK_NONE, GST_TYPE_GOOM);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "goom",
    "GOOM visualization filter",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
