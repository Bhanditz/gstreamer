/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim@fluendo.com>
 *
 * RTP SSRC demuxer
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
 * SECTION:element-gstrtpssrcdemux
 * @short_description: separate RTP payloads based on the SSRC
 *
 * <refsect2>
 * <para>
 * gstrtpssrcdemux acts as a demuxer for RTP packets based on the SSRC of the
 * packets. Its main purpose is to allow an application to easily receive and
 * decode an RTP stream with multiple SSRCs.
 * </para>
 * <para>
 * For each SSRC that is detected, a new pad will be created and the
 * ::new-ssrc-pad signal will be emitted. 
 * </para>
 * <title>Example pipelines</title>
 * <para>
 * <programlisting>
 * gst-launch udpsrc caps="application/x-rtp" ! gstrtpssrcdemux ! fakesink
 * </programlisting>
 * Takes an RTP stream and send the RTP packets with the first detected SSRC
 * to fakesink, discarding the other SSRCs.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2007-05-28 (0.10.5)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpbin-marshal.h"
#include "gstrtpssrcdemux.h"

GST_DEBUG_CATEGORY_STATIC (gst_rtp_ssrc_demux_debug);
#define GST_CAT_DEFAULT gst_rtp_ssrc_demux_debug

/* generic templates */
static GstStaticPadTemplate rtp_ssrc_demux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate rtp_ssrc_demux_src_template =
GST_STATIC_PAD_TEMPLATE ("src_%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstElementDetails gst_rtp_ssrc_demux_details = {
  "RTP SSRC Demux",
  "Demux/Network/RTP",
  "Splits RTP streams based on the SSRC",
  "Wim Taymans <wim@fluendo.com>"
};

/* signals */
enum
{
  SIGNAL_NEW_SSRC_PAD,
  LAST_SIGNAL
};

GST_BOILERPLATE (GstRTPSsrcDemux, gst_rtp_ssrc_demux, GstElement,
    GST_TYPE_ELEMENT);


/* GObject vmethods */
static void gst_rtp_ssrc_demux_finalize (GObject * object);

/* GstElement vmethods */
static GstStateChangeReturn gst_rtp_ssrc_demux_change_state (GstElement *
    element, GstStateChange transition);

/* sinkpad stuff */
static GstFlowReturn gst_rtp_ssrc_demux_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_rtp_ssrc_demux_sink_event (GstPad * pad, GstEvent * event);

/* srcpad stuff */
static gboolean gst_rtp_ssrc_demux_src_event (GstPad * pad, GstEvent * event);

static guint gst_rtp_ssrc_demux_signals[LAST_SIGNAL] = { 0 };

/**
 * Item for storing GstPad <-> SSRC pairs.
 */
struct _GstRTPSsrcDemuxPad
{
  GstPad *pad;
  guint32 ssrc;
  GstCaps *caps;
};

/* find a src pad for a given SSRC, returns NULL if the SSRC was not found
 */
static GstPad *
find_rtp_pad_for_ssrc (GstRTPSsrcDemux * demux, guint32 ssrc)
{
  GSList *walk;

  for (walk = demux->rtp_srcpads; walk; walk = g_slist_next (walk)) {
    GstRTPSsrcDemuxPad *pad = (GstRTPSsrcDemuxPad *) walk->data;

    if (pad->ssrc == ssrc)
      return pad->pad;
  }
  return NULL;
}

static GstPad *
create_rtp_pad_for_ssrc (GstRTPSsrcDemux * demux, guint32 ssrc)
{
  GstPad *result;
  GstElementClass *klass;
  GstPadTemplate *templ;
  gchar *padname;
  GstRTPSsrcDemuxPad *demuxpad;

  klass = GST_ELEMENT_GET_CLASS (demux);
  templ = gst_element_class_get_pad_template (klass, "src_%d");
  padname = g_strdup_printf ("src_%d", ssrc);
  result = gst_pad_new_from_template (templ, padname);
  g_free (padname);

  /* wrap in structure and add to list */
  demuxpad = g_new0 (GstRTPSsrcDemuxPad, 1);
  demuxpad->ssrc = ssrc;
  demuxpad->pad = result;
  demux->rtp_srcpads = g_slist_prepend (demux->rtp_srcpads, demuxpad);

  /* copy caps from input */
  gst_pad_set_caps (result, GST_PAD_CAPS (demux->rtp_sink));

  gst_pad_set_event_function (result, gst_rtp_ssrc_demux_src_event);
  gst_pad_set_active (result, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (demux), result);

  g_signal_emit (G_OBJECT (demux),
      gst_rtp_ssrc_demux_signals[SIGNAL_NEW_SSRC_PAD], 0, ssrc, result);

  return result;
}

static void
gst_rtp_ssrc_demux_base_init (gpointer g_class)
{
  GstElementClass *gstelement_klass = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&rtp_ssrc_demux_sink_template));
  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&rtp_ssrc_demux_src_template));

  gst_element_class_set_details (gstelement_klass, &gst_rtp_ssrc_demux_details);
}

static void
gst_rtp_ssrc_demux_class_init (GstRTPSsrcDemuxClass * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *gstelement_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;

  gobject_klass->finalize = GST_DEBUG_FUNCPTR (gst_rtp_ssrc_demux_finalize);

  /**
   * GstRTPSsrcDemux::new-ssrc-pad:
   * @demux: the object which received the signal
   * @ssrc: the SSRC of the pad
   * @pad: the new pad.
   *
   * Emited when a new SSRC pad has been created.
   */
  gst_rtp_ssrc_demux_signals[SIGNAL_NEW_SSRC_PAD] =
      g_signal_new ("new-ssrc-pad",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRTPSsrcDemuxClass, new_ssrc_pad),
      NULL, NULL, gst_rtp_bin_marshal_VOID__UINT_OBJECT,
      G_TYPE_NONE, 2, G_TYPE_UINT, GST_TYPE_PAD);

  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_rtp_ssrc_demux_change_state);

  GST_DEBUG_CATEGORY_INIT (gst_rtp_ssrc_demux_debug,
      "rtpssrcdemux", 0, "RTP SSRC demuxer");
}

static void
gst_rtp_ssrc_demux_init (GstRTPSsrcDemux * demux,
    GstRTPSsrcDemuxClass * g_class)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (demux);

  demux->rtp_sink =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");
  gst_pad_set_chain_function (demux->rtp_sink, gst_rtp_ssrc_demux_chain);
  gst_pad_set_event_function (demux->rtp_sink, gst_rtp_ssrc_demux_sink_event);
  gst_element_add_pad (GST_ELEMENT_CAST (demux), demux->rtp_sink);
}

static void
gst_rtp_ssrc_demux_finalize (GObject * object)
{
  GstRTPSsrcDemux *demux;

  demux = GST_RTP_SSRC_DEMUX (object);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_rtp_ssrc_demux_sink_event (GstPad * pad, GstEvent * event)
{
  GstRTPSsrcDemux *demux;
  gboolean res = FALSE;

  demux = GST_RTP_SSRC_DEMUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (demux);
  return res;
}

static GstFlowReturn
gst_rtp_ssrc_demux_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn ret;
  GstRTPSsrcDemux *demux;
  guint32 ssrc;
  GstPad *srcpad;

  demux = GST_RTP_SSRC_DEMUX (GST_OBJECT_PARENT (pad));

  if (!gst_rtp_buffer_validate (buf))
    goto invalid_payload;

  ssrc = gst_rtp_buffer_get_ssrc (buf);

  GST_DEBUG_OBJECT (demux, "received buffer of SSRC %08x", ssrc);

  srcpad = find_rtp_pad_for_ssrc (demux, ssrc);
  if (srcpad == NULL) {
    GST_DEBUG_OBJECT (demux, "creating pad for SSRC %08x", ssrc);
    srcpad = create_rtp_pad_for_ssrc (demux, ssrc);
    if (!srcpad)
      goto create_failed;
  }

  /* push to srcpad */
  ret = gst_pad_push (srcpad, buf);

  return ret;

  /* ERRORS */
invalid_payload:
  {
    /* this is fatal and should be filtered earlier */
    GST_ELEMENT_ERROR (demux, STREAM, DECODE, (NULL),
        ("Dropping invalid RTP payload"));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
create_failed:
  {
    /* this is not fatal yet */
    GST_ELEMENT_ERROR (demux, STREAM, DECODE, (NULL),
        ("Could not create new pad"));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_rtp_ssrc_demux_src_event (GstPad * pad, GstEvent * event)
{
  GstRTPSsrcDemux *demux;
  gboolean res = FALSE;

  demux = GST_RTP_SSRC_DEMUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }
  gst_object_unref (demux);
  return res;
}

static GstStateChangeReturn
gst_rtp_ssrc_demux_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRTPSsrcDemux *demux;

  demux = GST_RTP_SSRC_DEMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_NULL:
    default:
      break;
  }
  return ret;
}
