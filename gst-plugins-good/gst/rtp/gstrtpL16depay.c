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
#include "gstrtpL16depay.h"
#include "gstrtp-common.h"

/* elementfactory information */
static const GstElementDetails gst_rtp_L16depay_details =
GST_ELEMENT_DETAILS ("RTP packet depayloader",
    "Codec/Depayloader/Network",
    "Extracts raw audio from RTP packets",
    "Zeeshan Ali <zak147@yahoo.com>");

/* RtpL16Depay signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_FREQUENCY,
  ARG_PAYLOAD_TYPE
};

static GstStaticPadTemplate gst_rtp_L16depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) true, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 1000, 48000 ], " "channels = (int) [ 1, 2 ]")
    );

static GstStaticPadTemplate gst_rtp_L16depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static void gst_rtp_L16depay_class_init (GstRtpL16DepayClass * klass);
static void gst_rtp_L16depay_base_init (GstRtpL16DepayClass * klass);
static void gst_rtp_L16depay_init (GstRtpL16Depay * rtpL16depay);

static void gst_rtp_L16depay_chain (GstPad * pad, GstData * _data);

static void gst_rtp_L16depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_L16depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_rtp_L16depay_change_state (GstElement *
    element);

static GstElementClass *parent_class = NULL;

static GType
gst_rtp_L16depay_get_type (void)
{
  static GType rtpL16depay_type = 0;

  if (!rtpL16depay_type) {
    static const GTypeInfo rtpL16depay_info = {
      sizeof (GstRtpL16DepayClass),
      (GBaseInitFunc) gst_rtp_L16depay_base_init,
      NULL,
      (GClassInitFunc) gst_rtp_L16depay_class_init,
      NULL,
      NULL,
      sizeof (GstRtpL16Depay),
      0,
      (GInstanceInitFunc) gst_rtp_L16depay_init,
    };

    rtpL16depay_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRtpL16Depay",
        &rtpL16depay_info, 0);
  }
  return rtpL16depay_type;
}

static void
gst_rtp_L16depay_base_init (GstRtpL16DepayClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_L16depay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_L16depay_sink_template));
  gst_element_class_set_details (element_class, &gst_rtp_L16depay_details);
}

static void
gst_rtp_L16depay_class_init (GstRtpL16DepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PAYLOAD_TYPE,
      g_param_spec_int ("payload_type", "payload_type", "payload type",
          G_MININT, G_MAXINT, PAYLOAD_L16_STEREO, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FREQUENCY,
      g_param_spec_int ("frequency", "frequency", "frequency",
          G_MININT, G_MAXINT, 44100, G_PARAM_READWRITE));

  gobject_class->set_property = gst_rtp_L16depay_set_property;
  gobject_class->get_property = gst_rtp_L16depay_get_property;

  gstelement_class->change_state = gst_rtp_L16depay_change_state;
}

static void
gst_rtp_L16depay_init (GstRtpL16Depay * rtpL16depay)
{
  rtpL16depay->srcpad =
      gst_pad_new_from_static_template (&gst_rtp_L16depay_src_template, "src");
  rtpL16depay->sinkpad =
      gst_pad_new_from_static_template (&gst_rtp_L16depay_sink_template,
      "sink");
  gst_element_add_pad (GST_ELEMENT (rtpL16depay), rtpL16depay->srcpad);
  gst_element_add_pad (GST_ELEMENT (rtpL16depay), rtpL16depay->sinkpad);
  gst_pad_set_chain_function (rtpL16depay->sinkpad, gst_rtp_L16depay_chain);

  rtpL16depay->frequency = 44100;
  rtpL16depay->channels = 2;

  rtpL16depay->payload_type = PAYLOAD_L16_STEREO;
}

void
gst_rtp_L16depay_ntohs (GstBuffer * buf)
{
  gint16 *i, *len;

  /* FIXME: is this code correct or even sane at all? */
  i = (gint16 *) GST_BUFFER_DATA (buf);
  len = i + GST_BUFFER_SIZE (buf) / sizeof (gint16 *);

  for (; i < len; i++) {
    *i = g_ntohs (*i);
  }
}

void
gst_rtp_L16_caps_nego (GstRtpL16Depay * rtpL16depay)
{
  GstCaps *caps;

  caps =
      gst_caps_copy (gst_static_caps_get (&gst_rtp_L16depay_src_template.
          static_caps));

  gst_caps_set_simple (caps,
      "rate", G_TYPE_INT, rtpL16depay->frequency,
      "channel", G_TYPE_INT, rtpL16depay->channels, NULL);

  gst_pad_try_set_caps (rtpL16depay->srcpad, caps);
}

void
gst_rtp_L16depay_payloadtype_change (GstRtpL16Depay * rtpL16depay,
    rtp_payload_t pt)
{
  rtpL16depay->payload_type = pt;

  switch (pt) {
    case PAYLOAD_L16_MONO:
      rtpL16depay->channels = 1;
      break;
    case PAYLOAD_L16_STEREO:
      rtpL16depay->channels = 2;
      break;
    default:
      g_warning ("unknown payload_t %d\n", pt);
  }

  gst_rtp_L16_caps_nego (rtpL16depay);
}

static void
gst_rtp_L16depay_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstRtpL16Depay *rtpL16depay;
  GstBuffer *outbuf;
  Rtp_Packet packet;
  rtp_payload_t pt;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  rtpL16depay = GST_RTP_L16_DEPAY (GST_OBJECT_PARENT (pad));

  g_return_if_fail (rtpL16depay != NULL);
  g_return_if_fail (GST_IS_RTP_L16_DEPAY (rtpL16depay));

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);

    gst_pad_event_default (pad, event);

    return;
  }

  if (GST_PAD_CAPS (rtpL16depay->srcpad) == NULL) {
    gst_rtp_L16_caps_nego (rtpL16depay);
  }

  packet =
      rtp_packet_new_copy_data (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

  pt = rtp_packet_get_payload_type (packet);

  if (pt != rtpL16depay->payload_type) {
    gst_rtp_L16depay_payloadtype_change (rtpL16depay, pt);
  }

  outbuf = gst_buffer_new ();
  GST_BUFFER_SIZE (outbuf) = rtp_packet_get_payload_len (packet);
  GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));
  GST_BUFFER_TIMESTAMP (outbuf) =
      g_ntohl (rtp_packet_get_timestamp (packet)) * GST_SECOND;

  memcpy (GST_BUFFER_DATA (outbuf), rtp_packet_get_payload (packet),
      GST_BUFFER_SIZE (outbuf));

  GST_DEBUG ("gst_rtp_L16depay_chain: pushing buffer of size %d",
      GST_BUFFER_SIZE (outbuf));

  /* FIXME: According to RFC 1890, this is required, right? */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  gst_rtp_L16depay_ntohs (outbuf);
#endif

  gst_pad_push (rtpL16depay->srcpad, GST_DATA (outbuf));

  rtp_packet_free (packet);
  gst_buffer_unref (buf);
}

static void
gst_rtp_L16depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpL16Depay *rtpL16depay;

  g_return_if_fail (GST_IS_RTP_L16_DEPAY (object));
  rtpL16depay = GST_RTP_L16_DEPAY (object);

  switch (prop_id) {
    case ARG_PAYLOAD_TYPE:
      gst_rtp_L16depay_payloadtype_change (rtpL16depay,
          g_value_get_int (value));
      break;
    case ARG_FREQUENCY:
      rtpL16depay->frequency = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_rtp_L16depay_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRtpL16Depay *rtpL16depay;

  g_return_if_fail (GST_IS_RTP_L16_DEPAY (object));
  rtpL16depay = GST_RTP_L16_DEPAY (object);

  switch (prop_id) {
    case ARG_PAYLOAD_TYPE:
      g_value_set_int (value, rtpL16depay->payload_type);
      break;
    case ARG_FREQUENCY:
      g_value_set_int (value, rtpL16depay->frequency);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtp_L16depay_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpL16Depay *rtpL16depay;
  GstStateChangeReturn ret;

  rtpL16depay = GST_RTP_L16_DEPAY (element);

  GST_DEBUG ("state pending %d\n", GST_STATE_PENDING (element));


  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    default:
      break;
  }
  /* if we haven't failed already, give the parent class a chance to ;-) */
  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_rtp_L16depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpL16depay",
      GST_RANK_MARGINAL, GST_TYPE_RTP_L16_DEPAY);
}
