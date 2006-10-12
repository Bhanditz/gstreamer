/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more 
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include <gst/rtp/gstrtpbuffer.h>

#include "gstbasertppayload.h"

GST_DEBUG_CATEGORY_STATIC (basertppayload_debug);
#define GST_CAT_DEFAULT (basertppayload_debug)

/* BaseRTPPayload signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

/* FIXME 0.11, a better default is the Ethernet MTU of 
 * 1500 - sizeof(headers) as pointed out by marcelm in IRC:
 * So an Ethernet MTU of 1500, minus 60 for the max IP, minus 8 for UDP, gives
 * 1432 bytes or so.  And that should be adjusted downward further for other
 * encapsulations like PPPoE, so 1400 at most.
 */
#define DEFAULT_MTU                     1024
#define DEFAULT_PT                      96
#define DEFAULT_SSRC                    -1
#define DEFAULT_TIMESTAMP_OFFSET        -1
#define DEFAULT_SEQNUM_OFFSET           -1
#define DEFAULT_MAX_PTIME               -1

enum
{
  PROP_0,
  PROP_MTU,
  PROP_PT,
  PROP_SSRC,
  PROP_TIMESTAMP_OFFSET,
  PROP_SEQNUM_OFFSET,
  PROP_MAX_PTIME,
  PROP_TIMESTAMP,
  PROP_SEQNUM
};

static void gst_basertppayload_class_init (GstBaseRTPPayloadClass * klass);
static void gst_basertppayload_base_init (GstBaseRTPPayloadClass * klass);
static void gst_basertppayload_init (GstBaseRTPPayload * basertppayload,
    gpointer g_class);
static void gst_basertppayload_finalize (GObject * object);

static gboolean gst_basertppayload_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_basertppayload_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_basertppayload_chain (GstPad * pad,
    GstBuffer * buffer);

static void gst_basertppayload_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_basertppayload_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_basertppayload_change_state (GstElement *
    element, GstStateChange transition);

static GstElementClass *parent_class = NULL;

GType
gst_basertppayload_get_type (void)
{
  static GType basertppayload_type = 0;

  if (!basertppayload_type) {
    static const GTypeInfo basertppayload_info = {
      sizeof (GstBaseRTPPayloadClass),
      (GBaseInitFunc) gst_basertppayload_base_init,
      NULL,
      (GClassInitFunc) gst_basertppayload_class_init,
      NULL,
      NULL,
      sizeof (GstBaseRTPPayload),
      0,
      (GInstanceInitFunc) gst_basertppayload_init,
    };

    basertppayload_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstBaseRTPPayload",
        &basertppayload_info, G_TYPE_FLAG_ABSTRACT);
  }
  return basertppayload_type;
}

static void
gst_basertppayload_base_init (GstBaseRTPPayloadClass * klass)
{
}

static void
gst_basertppayload_class_init (GstBaseRTPPayloadClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_basertppayload_finalize;

  gobject_class->set_property = gst_basertppayload_set_property;
  gobject_class->get_property = gst_basertppayload_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MTU,
      g_param_spec_uint ("mtu", "MTU",
          "Maximum size of one packet",
          28, G_MAXUINT, DEFAULT_MTU, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PT,
      g_param_spec_uint ("pt", "payload type",
          "The payload type of the packets",
          0, 0x80, DEFAULT_PT, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SSRC,
      g_param_spec_uint ("ssrc", "SSRC",
          "The SSRC of the packets (-1 == random)",
          0, G_MAXUINT, DEFAULT_SSRC, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_TIMESTAMP_OFFSET, g_param_spec_int ("timestamp-offset",
          "Timestamp Offset",
          "Offset to add to all outgoing timestamps (-1 = random)", -1,
          G_MAXINT, DEFAULT_TIMESTAMP_OFFSET, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SEQNUM_OFFSET,
      g_param_spec_int ("seqnum-offset", "Sequence number Offset",
          "Offset to add to all outgoing seqnum (-1 = random)", -1, G_MAXINT,
          DEFAULT_SEQNUM_OFFSET, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MAX_PTIME,
      g_param_spec_int64 ("max-ptime", "Max packet time",
          "Maximum duration of the packet data in ns (-1 = unlimited up to MTU)",
          -1, G_MAXINT64, DEFAULT_MAX_PTIME, G_PARAM_READWRITE));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TIMESTAMP,
      g_param_spec_uint ("timestamp", "Timestamp",
          "The RTP timestamp of the last processed packet",
          0, G_MAXUINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SEQNUM,
      g_param_spec_uint ("seqnum", "Sequence number",
          "The RTP sequence number of the last processed packet",
          0, G_MAXUINT, 0, G_PARAM_READABLE));

  gstelement_class->change_state = gst_basertppayload_change_state;

  GST_DEBUG_CATEGORY_INIT (basertppayload_debug, "basertppayload", 0,
      "Base class for RTP Payloaders");
}

static void
gst_basertppayload_init (GstBaseRTPPayload * basertppayload, gpointer g_class)
{
  GstPadTemplate *templ;

  templ =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "src");
  g_return_if_fail (templ != NULL);

  basertppayload->srcpad = gst_pad_new_from_template (templ, "src");
  gst_element_add_pad (GST_ELEMENT (basertppayload), basertppayload->srcpad);

  templ =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "sink");
  g_return_if_fail (templ != NULL);

  basertppayload->sinkpad = gst_pad_new_from_template (templ, "sink");
  gst_pad_set_setcaps_function (basertppayload->sinkpad,
      gst_basertppayload_setcaps);
  gst_pad_set_event_function (basertppayload->sinkpad,
      gst_basertppayload_event);
  gst_pad_set_chain_function (basertppayload->sinkpad,
      gst_basertppayload_chain);
  gst_element_add_pad (GST_ELEMENT (basertppayload), basertppayload->sinkpad);

  basertppayload->seq_rand = g_rand_new ();
  basertppayload->ssrc_rand = g_rand_new ();
  basertppayload->ts_rand = g_rand_new ();

  basertppayload->mtu = DEFAULT_MTU;
  basertppayload->pt = DEFAULT_PT;
  basertppayload->seqnum_offset = DEFAULT_SEQNUM_OFFSET;
  basertppayload->ssrc = DEFAULT_SSRC;
  basertppayload->ts_offset = DEFAULT_TIMESTAMP_OFFSET;
  basertppayload->max_ptime = DEFAULT_MAX_PTIME;

  basertppayload->media = NULL;
  basertppayload->encoding_name = NULL;

  basertppayload->clock_rate = 0;
}

static void
gst_basertppayload_finalize (GObject * object)
{
  GstBaseRTPPayload *basertppayload;

  basertppayload = GST_BASE_RTP_PAYLOAD (object);

  g_rand_free (basertppayload->seq_rand);
  basertppayload->seq_rand = NULL;
  g_rand_free (basertppayload->ssrc_rand);
  basertppayload->ssrc_rand = NULL;
  g_rand_free (basertppayload->ts_rand);
  basertppayload->ts_rand = NULL;

  g_free (basertppayload->media);
  basertppayload->media = NULL;
  g_free (basertppayload->encoding_name);
  basertppayload->encoding_name = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_basertppayload_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseRTPPayload *basertppayload;
  GstBaseRTPPayloadClass *basertppayload_class;
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (pad, "setting caps %" GST_PTR_FORMAT, caps);
  basertppayload = GST_BASE_RTP_PAYLOAD (gst_pad_get_parent (pad));
  basertppayload_class = GST_BASE_RTP_PAYLOAD_GET_CLASS (basertppayload);

  if (basertppayload_class->set_caps)
    ret = basertppayload_class->set_caps (basertppayload, caps);

  gst_object_unref (basertppayload);

  return ret;
}

static gboolean
gst_basertppayload_event (GstPad * pad, GstEvent * event)
{
  GstBaseRTPPayload *basertppayload;
  gboolean res;

  basertppayload = GST_BASE_RTP_PAYLOAD (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      res = gst_pad_event_default (pad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      res = gst_pad_event_default (pad, event);
      gst_segment_init (&basertppayload->segment, GST_FORMAT_UNDEFINED);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      gdouble rate;
      GstFormat fmt;
      gint64 start, stop, position;

      gst_event_parse_new_segment (event, &update, &rate, &fmt, &start, &stop,
          &position);
      gst_segment_set_newsegment (&basertppayload->segment, update, rate, fmt,
          start, stop, position);
    }
      /* fallthrough */
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (basertppayload);

  return res;
}


static GstFlowReturn
gst_basertppayload_chain (GstPad * pad, GstBuffer * buffer)
{
  GstBaseRTPPayload *basertppayload;
  GstBaseRTPPayloadClass *basertppayload_class;
  GstFlowReturn ret;

  basertppayload = GST_BASE_RTP_PAYLOAD (gst_pad_get_parent (pad));
  basertppayload_class = GST_BASE_RTP_PAYLOAD_GET_CLASS (basertppayload);

  if (!basertppayload_class->handle_buffer)
    goto no_function;

  ret = basertppayload_class->handle_buffer (basertppayload, buffer);

  gst_object_unref (basertppayload);

  return ret;

  /* ERRORS */
no_function:
  {
    GST_ELEMENT_ERROR (basertppayload, STREAM, NOT_IMPLEMENTED, (NULL),
        ("subclass did not implement handle_buffer function"));
    gst_object_unref (basertppayload);
    return GST_FLOW_ERROR;
  }
}

void
gst_basertppayload_set_options (GstBaseRTPPayload * payload,
    gchar * media, gboolean dynamic, gchar * encoding_name, guint32 clock_rate)
{
  g_return_if_fail (payload != NULL);
  g_return_if_fail (clock_rate != 0);

  g_free (payload->media);
  payload->media = g_strdup (media);
  payload->dynamic = dynamic;
  g_free (payload->encoding_name);
  payload->encoding_name = g_strdup (encoding_name);
  payload->clock_rate = clock_rate;
}

gboolean
gst_basertppayload_set_outcaps (GstBaseRTPPayload * payload, gchar * fieldname,
    ...)
{
  GstCaps *srccaps;
  GstStructure *s;

  srccaps = gst_caps_new_simple ("application/x-rtp",
      "media", G_TYPE_STRING, payload->media,
      "payload", G_TYPE_INT, GST_BASE_RTP_PAYLOAD_PT (payload),
      "clock-rate", G_TYPE_INT, payload->clock_rate,
      "encoding-name", G_TYPE_STRING, payload->encoding_name,
      "ssrc", G_TYPE_UINT, payload->current_ssrc,
      "clock-base", G_TYPE_UINT, payload->ts_base,
      "seqnum-base", G_TYPE_UINT, payload->seqnum_base, NULL);
  s = gst_caps_get_structure (srccaps, 0);

  if (fieldname) {
    va_list varargs;

    va_start (varargs, fieldname);
    gst_structure_set_valist (s, fieldname, varargs);
    va_end (varargs);
  }

  gst_pad_set_caps (GST_BASE_RTP_PAYLOAD_SRCPAD (payload), srccaps);
  gst_caps_unref (srccaps);

  return TRUE;
}

gboolean
gst_basertppayload_is_filled (GstBaseRTPPayload * payload,
    guint size, GstClockTime duration)
{
  if (size > payload->mtu)
    return TRUE;

  if (payload->max_ptime != -1 && duration >= payload->max_ptime)
    return TRUE;

  return FALSE;
}

GstFlowReturn
gst_basertppayload_push (GstBaseRTPPayload * payload, GstBuffer * buffer)
{
  GstFlowReturn res;
  GstClockTime timestamp;
  guint32 ts;

  if (payload->clock_rate == 0)
    goto no_rate;

  gst_rtp_buffer_set_ssrc (buffer, payload->current_ssrc);

  gst_rtp_buffer_set_payload_type (buffer, payload->pt);

  /* can wrap around, which is perfectly fine */
  /* update first, so that the property is set to the last
   * seqnum pushed */
  payload->seqnum++;
  GST_LOG_OBJECT (payload, "setting RTP seqnum %d", payload->seqnum);
  gst_rtp_buffer_set_seq (buffer, payload->seqnum);

  /* add our random offset to the timestamp */
  ts = payload->ts_base;

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    gint64 rtime;

    rtime =
        gst_segment_to_running_time (&payload->segment, GST_FORMAT_TIME,
        timestamp);
    rtime = gst_util_uint64_scale_int (rtime, payload->clock_rate, GST_SECOND);

    ts += rtime;
  }
  GST_LOG_OBJECT (payload, "setting RTP timestamp %u", (guint) ts);
  gst_rtp_buffer_set_timestamp (buffer, ts);

  payload->timestamp = ts;

  /* set caps */
  gst_buffer_set_caps (buffer, GST_PAD_CAPS (payload->srcpad));

  GST_DEBUG_OBJECT (payload, "Pushing packet size %d, seq=%d, ts=%u",
      GST_BUFFER_SIZE (buffer), payload->seqnum - 1, ts);

  res = gst_pad_push (payload->srcpad, buffer);

  return res;

  /* ERRORS */
no_rate:
  {
    GST_ELEMENT_ERROR (payload, STREAM, NOT_IMPLEMENTED, (NULL),
        ("subclass did not specify clock_rate"));
    gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }
}

static void
gst_basertppayload_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseRTPPayload *basertppayload;

  basertppayload = GST_BASE_RTP_PAYLOAD (object);

  switch (prop_id) {
    case PROP_MTU:
      basertppayload->mtu = g_value_get_uint (value);
      break;
    case PROP_PT:
      basertppayload->pt = g_value_get_uint (value);
      break;
    case PROP_SSRC:
      basertppayload->ssrc = g_value_get_uint (value);
      break;
    case PROP_TIMESTAMP_OFFSET:
      basertppayload->ts_offset = g_value_get_int (value);
      break;
    case PROP_SEQNUM_OFFSET:
      basertppayload->seqnum_offset = g_value_get_int (value);
      break;
    case PROP_MAX_PTIME:
      basertppayload->max_ptime = g_value_get_int64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_basertppayload_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseRTPPayload *basertppayload;

  basertppayload = GST_BASE_RTP_PAYLOAD (object);

  switch (prop_id) {
    case PROP_MTU:
      g_value_set_uint (value, basertppayload->mtu);
      break;
    case PROP_PT:
      g_value_set_uint (value, basertppayload->pt);
      break;
    case PROP_SSRC:
      g_value_set_uint (value, basertppayload->ssrc);
      break;
    case PROP_TIMESTAMP_OFFSET:
      g_value_set_int (value, basertppayload->ts_offset);
      break;
    case PROP_SEQNUM_OFFSET:
      g_value_set_int (value, basertppayload->seqnum_offset);
      break;
    case PROP_MAX_PTIME:
      g_value_set_int64 (value, basertppayload->max_ptime);
      break;
    case PROP_TIMESTAMP:
      g_value_set_uint (value, basertppayload->timestamp);
      break;
    case PROP_SEQNUM:
      g_value_set_uint (value, basertppayload->seqnum);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_basertppayload_change_state (GstElement * element,
    GstStateChange transition)
{
  GstBaseRTPPayload *basertppayload;
  GstStateChangeReturn ret;

  basertppayload = GST_BASE_RTP_PAYLOAD (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_segment_init (&basertppayload->segment, GST_FORMAT_UNDEFINED);

      if (basertppayload->seqnum_offset == -1)
        basertppayload->seqnum_base =
            g_rand_int_range (basertppayload->seq_rand, 0, G_MAXUINT16);
      else
        basertppayload->seqnum_base = basertppayload->seqnum_offset;
      basertppayload->seqnum = basertppayload->seqnum_base;

      if (basertppayload->ssrc == -1)
        basertppayload->current_ssrc = g_rand_int (basertppayload->ssrc_rand);
      else
        basertppayload->current_ssrc = basertppayload->ssrc;

      if (basertppayload->ts_offset == -1)
        basertppayload->ts_base = g_rand_int (basertppayload->ts_rand);
      else
        basertppayload->ts_base = basertppayload->ts_offset;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}
