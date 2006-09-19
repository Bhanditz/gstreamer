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
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
/**
 * SECTION:element-rtspsrc
 *
 * <refsect2>
 * <para>
 * Makes a connection to an RTSP server and read the data.
 * rtspsrc strictly follows RFC 2326 and therefore does not (yet) support
 * RealMedia/Quicktime/Microsoft extensions.
 * </para>
 * <para>
 * RTSP supports transport over TCP or UDP in unicast or multicast mode. By
 * default rtspsrc will negotiate a connection in the following order:
 * UDP unicast/UDP multicast/TCP. The order cannot be changed but the allowed
 * protocols can be controlled with the "protocols" property.
 * </para>
 * <para>
 * rtspsrc currently understands SDP as the format of the session description.
 * For each stream listed in the SDP a new rtp_stream%d pad will be created
 * with caps derived from the SDP media description. This is a caps of mime type
 * "application/x-rtp" that can be connected to any available RTP depayloader
 * element. 
 * </para>
 * <para>
 * rtspsrc will internally instantiate an RTP session manager element
 * that will handle the RTCP messages to and from the server, jitter removal,
 * packet reordering along with providing a clock for the pipeline. 
 * This feature is however currently not yet implemented.
 * </para>
 * <para>
 * rtspsrc acts like a live source and will therefore only generate data in the 
 * PLAYING state.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch rtspsrc location=rtsp://some.server/url ! fakesink
 * </programlisting>
 * Establish a connection to an RTSP server and send the raw RTP packets to a fakesink.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2006-08-18 (0.10.5)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <string.h>

#include "gstrtspsrc.h"
#include "sdp.h"

GST_DEBUG_CATEGORY_STATIC (rtspsrc_debug);
#define GST_CAT_DEFAULT (rtspsrc_debug)

/* elementfactory information */
static const GstElementDetails gst_rtspsrc_details =
GST_ELEMENT_DETAILS ("RTSP packet receiver",
    "Source/Network",
    "Receive data over the network via RTSP (RFC 2326)",
    "Wim Taymans <wim@fluendo.com>\n"
    "Thijs Vermeir <thijs.vermeir@barco.com>\n"
    "Lutz Mueller <lutz@topfrose.de>");

static GstStaticPadTemplate rtptemplate =
GST_STATIC_PAD_TEMPLATE ("rtp_stream%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-rtp"));

enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_LOCATION        NULL
#define DEFAULT_PROTOCOLS       GST_RTSP_PROTO_UDP_UNICAST | GST_RTSP_PROTO_UDP_MULTICAST | GST_RTSP_PROTO_TCP
#define DEFAULT_DEBUG           FALSE
#define DEFAULT_RETRY           20

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_PROTOCOLS,
  PROP_DEBUG,
  PROP_RETRY,
  /* FILL ME */
};

#define GST_TYPE_RTSP_PROTO (gst_rtsp_proto_get_type())
static GType
gst_rtsp_proto_get_type (void)
{
  static GType rtsp_proto_type = 0;
  static const GFlagsValue rtsp_proto[] = {
    {GST_RTSP_PROTO_UDP_UNICAST, "UDP Unicast", "UDP unicast mode"},
    {GST_RTSP_PROTO_UDP_MULTICAST, "UDP Multicast", "UDP Multicast mode"},
    {GST_RTSP_PROTO_TCP, "TCP", "TCP interleaved mode"},
    {0, NULL, NULL},
  };

  if (!rtsp_proto_type) {
    rtsp_proto_type = g_flags_register_static ("GstRTSPProto", rtsp_proto);
  }
  return rtsp_proto_type;
}

static void gst_rtspsrc_base_init (gpointer g_class);
static void gst_rtspsrc_finalize (GObject * object);

static void gst_rtspsrc_uri_handler_init (gpointer g_iface,
    gpointer iface_data);
static GstCaps *gst_rtspsrc_media_to_caps (gint pt, SDPMedia * media);

static GstStateChangeReturn gst_rtspsrc_change_state (GstElement * element,
    GstStateChange transition);

static void gst_rtspsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtspsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_rtspsrc_uri_set_uri (GstURIHandler * handler,
    const gchar * uri);

static void gst_rtspsrc_loop (GstRTSPSrc * src);

/*static guint gst_rtspsrc_signals[LAST_SIGNAL] = { 0 }; */

static void
_do_init (GType rtspsrc_type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_rtspsrc_uri_handler_init,
    NULL,
    NULL
  };

  GST_DEBUG_CATEGORY_INIT (rtspsrc_debug, "rtspsrc", 0, "RTSP src");

  g_type_add_interface_static (rtspsrc_type, GST_TYPE_URI_HANDLER,
      &urihandler_info);
}

GST_BOILERPLATE_FULL (GstRTSPSrc, gst_rtspsrc, GstBin, GST_TYPE_BIN, _do_init);

static void
gst_rtspsrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtptemplate));

  gst_element_class_set_details (element_class, &gst_rtspsrc_details);
}

static void
gst_rtspsrc_class_init (GstRTSPSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbin_class = (GstBinClass *) klass;

  gobject_class->set_property = gst_rtspsrc_set_property;
  gobject_class->get_property = gst_rtspsrc_get_property;

  gobject_class->finalize = gst_rtspsrc_finalize;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "RTSP Location",
          "Location of the RTSP url to read",
          DEFAULT_LOCATION, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_PROTOCOLS,
      g_param_spec_flags ("protocols", "Protocols", "Allowed protocols",
          GST_TYPE_RTSP_PROTO, DEFAULT_PROTOCOLS,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_DEBUG,
      g_param_spec_boolean ("debug", "Debug",
          "Dump request and response messages to stdout",
          DEFAULT_DEBUG, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_RETRY,
      g_param_spec_uint ("retry", "Retry",
          "Max number of retries when allocating RTP ports.",
          0, G_MAXUINT16, DEFAULT_RETRY,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  gstelement_class->change_state = gst_rtspsrc_change_state;
}

static void
gst_rtspsrc_init (GstRTSPSrc * src, GstRTSPSrcClass * g_class)
{
  src->stream_rec_lock = g_new (GStaticRecMutex, 1);
  g_static_rec_mutex_init (src->stream_rec_lock);

  src->location = DEFAULT_LOCATION;
  src->url = NULL;
}

static void
gst_rtspsrc_finalize (GObject * object)
{
  GstRTSPSrc *rtspsrc;

  rtspsrc = GST_RTSPSRC (object);

  g_static_rec_mutex_free (rtspsrc->stream_rec_lock);
  g_free (rtspsrc->stream_rec_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rtspsrc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstRTSPSrc *rtspsrc;

  rtspsrc = GST_RTSPSRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      gst_rtspsrc_uri_set_uri (GST_URI_HANDLER (rtspsrc),
          g_value_get_string (value));
      break;
    case PROP_PROTOCOLS:
      rtspsrc->protocols = g_value_get_flags (value);
      break;
    case PROP_DEBUG:
      rtspsrc->debug = g_value_get_boolean (value);
      break;
    case PROP_RETRY:
      rtspsrc->retry = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtspsrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRTSPSrc *rtspsrc;

  rtspsrc = GST_RTSPSRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, rtspsrc->location);
      break;
    case PROP_PROTOCOLS:
      g_value_set_flags (value, rtspsrc->protocols);
      break;
    case PROP_DEBUG:
      g_value_set_boolean (value, rtspsrc->debug);
      break;
    case PROP_RETRY:
      g_value_set_uint (value, rtspsrc->retry);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gint
find_stream_by_pt (GstRTSPStream * stream, gconstpointer a)
{
  gint pt = GPOINTER_TO_INT (a);

  if (stream->pt == pt)
    return 0;

  return -1;
}

static GstRTSPStream *
gst_rtspsrc_create_stream (GstRTSPSrc * src, SDPMedia * media)
{
  GstRTSPStream *stream;
  gchar *control_url;
  gchar *payload;

  stream = g_new0 (GstRTSPStream, 1);
  stream->parent = src;
  /* we mark the pad as not linked, we will mark it as OK when we add the pad to
   * the element. */
  stream->last_ret = GST_FLOW_NOT_LINKED;
  stream->id = src->numstreams++;

  /* we must have a payload. No payload means we cannot create caps */
  /* FIXME, handle multiple formats. */
  if ((payload = sdp_media_get_format (media, 0))) {
    stream->pt = atoi (payload);
    /* convert caps */
    stream->caps = gst_rtspsrc_media_to_caps (stream->pt, media);

    if (stream->pt >= 96) {
      /* If we have a dynamic payload type, see if we have a stream with the
       * same payload number. If there is one, they are part of the same
       * container and we only need to add one pad. */
      if (g_list_find_custom (src->streams, GINT_TO_POINTER (stream->pt),
              (GCompareFunc) find_stream_by_pt)) {
        stream->container = TRUE;
      }
    }
  }

  /* get control url to construct the setup url. The setup url is used to
   * configure the transport of the stream and is used to identity the stream in
   * the RTP-Info header field returned from PLAY. */
  control_url = sdp_media_get_attribute_val (media, "control");

  GST_DEBUG_OBJECT (src, "stream %d", stream->id);
  GST_DEBUG_OBJECT (src, " pt: %d", stream->pt);
  GST_DEBUG_OBJECT (src, " container: %d", stream->container);
  GST_DEBUG_OBJECT (src, " caps: %" GST_PTR_FORMAT, stream->caps);
  GST_DEBUG_OBJECT (src, " control: %s", GST_STR_NULL (control_url));

  if (control_url != NULL) {
    /* FIXME, what if the control_url starts with a '/' or a non rtsp: protocol? */
    /* check absolute/relative URL */
    if (g_str_has_prefix (control_url, "rtsp://"))
      stream->setup_url = g_strdup (control_url);
    else
      stream->setup_url = g_strdup_printf ("%s/%s", src->location, control_url);
  }
  GST_DEBUG_OBJECT (src, " setup: %s", GST_STR_NULL (stream->setup_url));

  /* we keep track of all streams */
  src->streams = g_list_append (src->streams, stream);

  return stream;
}

#if 0
static void
gst_rtspsrc_free_stream (GstRTSPSrc * src, GstRTSPStream * stream)
{
  if (stream->caps) {
    gst_caps_unref (stream->caps);
  }
  g_free (stream->setup_url);

  src->streams = g_list_remove (src->streams, stream);
  src->numstreams--;

  g_free (stream);
}
#endif

#define PARSE_INT(p, del, res)          \
G_STMT_START {                          \
  gchar *t = p;                         \
  p = strstr (p, del);                  \
  if (p == NULL)                        \
    res = -1;                           \
  else {                                \
    *p = '\0';                          \
    p++;                                \
    res = atoi (t);                     \
  }                                     \
} G_STMT_END

#define PARSE_STRING(p, del, res)       \
G_STMT_START {                          \
  gchar *t = p;                         \
  p = strstr (p, del);                  \
  if (p == NULL)                        \
    res = NULL;                         \
  else {                                \
    *p = '\0';                          \
    p++;                                \
    res = t;                            \
  }                                     \
} G_STMT_END

#define SKIP_SPACES(p)                  \
  while (*p && g_ascii_isspace (*p))    \
    p++;

/* rtpmap contains:
 *
 *  <payload> <encoding_name>/<clock_rate>[/<encoding_params>]
 */
static gboolean
gst_rtspsrc_parse_rtpmap (gchar * rtpmap, gint * payload, gchar ** name,
    gint * rate, gchar ** params)
{
  gchar *p, *t;

  t = p = rtpmap;

  PARSE_INT (p, " ", *payload);
  if (*payload == -1)
    return FALSE;

  SKIP_SPACES (p);
  if (*p == '\0')
    return FALSE;

  PARSE_STRING (p, "/", *name);
  if (*name == NULL)
    return FALSE;

  t = p;
  p = strstr (p, "/");
  if (p == NULL) {
    *rate = atoi (t);
    return TRUE;
  }
  *p = '\0';
  p++;
  *rate = atoi (t);

  t = p;
  if (*p == '\0')
    return TRUE;
  *params = t;

  return TRUE;
}

/*
 *  Mapping of caps to and from SDP fields:
 *
 *   m=<media> <UDP port> RTP/AVP <payload> 
 *   a=rtpmap:<payload> <encoding_name>/<clock_rate>[/<encoding_params>]
 *   a=fmtp:<payload> <param>[=<value>];...
 */
static GstCaps *
gst_rtspsrc_media_to_caps (gint pt, SDPMedia * media)
{
  GstCaps *caps;
  gchar *rtpmap;
  gchar *fmtp;
  gchar *name = NULL;
  gint rate = -1;
  gchar *params = NULL;
  GstStructure *s;

  /* dynamic payloads need rtpmap */
  if (pt >= 96) {
    gint payload = 0;
    gboolean ret;

    if ((rtpmap = sdp_media_get_attribute_val (media, "rtpmap"))) {
      ret = gst_rtspsrc_parse_rtpmap (rtpmap, &payload, &name, &rate, &params);
      if (ret) {
        if (payload != pt) {
          /* FIXME, not fatal? */
          g_warning ("rtpmap of wrong payload type");
          name = NULL;
          rate = -1;
          params = NULL;
        }
      } else {
        /* FIXME, not fatal? */
        g_warning ("error parsing rtpmap");
      }
    } else
      goto no_rtpmap;
  }

  caps = gst_caps_new_simple ("application/x-rtp",
      "media", G_TYPE_STRING, media->media, "payload", G_TYPE_INT, pt, NULL);
  s = gst_caps_get_structure (caps, 0);

  if (rate != -1)
    gst_structure_set (s, "clock-rate", G_TYPE_INT, rate, NULL);

  if (name != NULL)
    gst_structure_set (s, "encoding-name", G_TYPE_STRING, name, NULL);

  if (params != NULL)
    gst_structure_set (s, "encoding-params", G_TYPE_STRING, params, NULL);

  /* parse optional fmtp: field */
  if ((fmtp = sdp_media_get_attribute_val (media, "fmtp"))) {
    gchar *p;
    gint payload = 0;

    p = fmtp;

    /* p is now of the format <payload> <param>[=<value>];... */
    PARSE_INT (p, " ", payload);
    if (payload != -1 && payload == pt) {
      gchar **pairs;
      gint i;

      /* <param>[=<value>] are separated with ';' */
      pairs = g_strsplit (p, ";", 0);
      for (i = 0; pairs[i]; i++) {
        gchar *valpos;
        gchar *val, *key;

        /* the key may not have a '=', the value can have other '='s */
        valpos = strstr (pairs[i], "=");
        if (valpos) {
          /* we have a '=' and thus a value, remove the '=' with \0 */
          *valpos = '\0';
          /* value is everything between '=' and ';'. FIXME, strip? */
          val = g_strstrip (valpos + 1);
        } else {
          /* simple <param>;.. is translated into <param>=1;... */
          val = "1";
        }
        /* strip the key of spaces */
        key = g_strstrip (pairs[i]);

        gst_structure_set (s, key, G_TYPE_STRING, val, NULL);
      }
      g_strfreev (pairs);
    }
  }
  return caps;

  /* ERRORS */
no_rtpmap:
  {
    g_warning ("rtpmap type not given for dynamic payload %d", pt);
    return NULL;
  }
}

static gboolean
gst_rtspsrc_stream_setup_rtp (GstRTSPStream * stream,
    gint * rtpport, gint * rtcpport)
{
  GstStateChangeReturn ret;
  GstRTSPSrc *src;
  GstElement *tmp, *rtpsrc, *rtcpsrc;
  gint tmp_rtp, tmp_rtcp;
  guint count;

  src = stream->parent;

  tmp = NULL;
  rtpsrc = NULL;
  rtcpsrc = NULL;
  count = 0;

  /* try to allocate 2 UDP ports, the RTP port should be an even
   * number and the RTCP port should be the next (uneven) port */
again:
  rtpsrc = gst_element_make_from_uri (GST_URI_SRC, "udp://0.0.0.0:0", NULL);
  if (rtpsrc == NULL)
    goto no_udp_rtp_protocol;

  ret = gst_element_set_state (rtpsrc, GST_STATE_PAUSED);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto start_rtp_failure;

  g_object_get (G_OBJECT (rtpsrc), "port", &tmp_rtp, NULL);
  GST_DEBUG_OBJECT (src, "got RTP port %d", tmp_rtp);

  /* check if port is even */
  if ((tmp_rtp & 0x01) != 0) {
    /* port not even, close and allocate another */
    count++;
    if (count > src->retry)
      goto no_ports;

    GST_DEBUG_OBJECT (src, "RTP port not even, retry %d", count);
    /* have to keep port allocated so we can get a new one */
    if (tmp != NULL) {
      GST_DEBUG_OBJECT (src, "free temp");
      gst_element_set_state (tmp, GST_STATE_NULL);
      gst_object_unref (tmp);
    }
    tmp = rtpsrc;
    GST_DEBUG_OBJECT (src, "retry %d", count);
    goto again;
  }
  /* free leftover temp element/port */
  if (tmp) {
    gst_element_set_state (tmp, GST_STATE_NULL);
    gst_object_unref (tmp);
    tmp = NULL;
  }

  /* allocate port+1 for RTCP now */
  rtcpsrc = gst_element_make_from_uri (GST_URI_SRC, "udp://0.0.0.0", NULL);
  if (rtcpsrc == NULL)
    goto no_udp_rtcp_protocol;

  /* set port */
  tmp_rtcp = tmp_rtp + 1;
  g_object_set (G_OBJECT (rtcpsrc), "port", tmp_rtcp, NULL);

  GST_DEBUG_OBJECT (src, "starting RTCP on port %d", tmp_rtcp);
  ret = gst_element_set_state (rtcpsrc, GST_STATE_PAUSED);
  /* FIXME, this could fail if the next port is not free, we
   * should retry with another port then */
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto start_rtcp_failure;

  /* all fine, do port check */
  g_object_get (G_OBJECT (rtpsrc), "port", rtpport, NULL);
  g_object_get (G_OBJECT (rtcpsrc), "port", rtcpport, NULL);

  /* this should not happen */
  if (*rtpport != tmp_rtp || *rtcpport != tmp_rtcp)
    goto port_error;

  /* we manage these elements, we set the caps in configure_transport */
  stream->rtpsrc = rtpsrc;
  stream->rtcpsrc = rtcpsrc;

  gst_bin_add (GST_BIN_CAST (src), stream->rtpsrc);
  gst_bin_add (GST_BIN_CAST (src), stream->rtcpsrc);

  return TRUE;

  /* ERRORS */
no_udp_rtp_protocol:
  {
    GST_DEBUG_OBJECT (src, "could not get UDP source for RTP");
    goto cleanup;
  }
start_rtp_failure:
  {
    GST_DEBUG_OBJECT (src, "could not start UDP source for RTP");
    goto cleanup;
  }
no_ports:
  {
    GST_DEBUG_OBJECT (src, "could not allocate UDP port pair after %d retries",
        count);
    goto cleanup;
  }
no_udp_rtcp_protocol:
  {
    GST_DEBUG_OBJECT (src, "could not get UDP source for RTCP");
    goto cleanup;
  }
start_rtcp_failure:
  {
    GST_DEBUG_OBJECT (src, "could not start UDP source for RTCP");
    goto cleanup;
  }
port_error:
  {
    GST_DEBUG_OBJECT (src, "ports don't match rtp: %d<->%d, rtcp: %d<->%d",
        tmp_rtp, *rtpport, tmp_rtcp, *rtcpport);
    goto cleanup;
  }
cleanup:
  {
    if (tmp) {
      gst_element_set_state (tmp, GST_STATE_NULL);
      gst_object_unref (tmp);
    }
    if (rtpsrc) {
      gst_element_set_state (rtpsrc, GST_STATE_NULL);
      gst_object_unref (rtpsrc);
    }
    if (rtcpsrc) {
      gst_element_set_state (rtcpsrc, GST_STATE_NULL);
      gst_object_unref (rtcpsrc);
    }
    return FALSE;
  }
}

static gboolean
gst_rtspsrc_stream_configure_transport (GstRTSPStream * stream,
    RTSPTransport * transport)
{
  GstRTSPSrc *src;
  GstPad *pad;
  GstPadTemplate *template;
  GstStateChangeReturn ret;
  gchar *name;

  src = stream->parent;

  GST_DEBUG ("configuring RTP transport for stream %p", stream);

  /* FIXME, the session manager needs to be shared with all the streams */
  if (!(stream->rtpdec = gst_element_factory_make ("rtpdec", NULL)))
    goto no_element;

  /* we manage this element */
  gst_bin_add (GST_BIN_CAST (src), stream->rtpdec);

  ret = gst_element_set_state (stream->rtpdec, GST_STATE_PAUSED);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    goto start_rtpdec_failure;

  stream->rtpdecrtp = gst_element_get_pad (stream->rtpdec, "sinkrtp");
  stream->rtpdecrtcp = gst_element_get_pad (stream->rtpdec, "sinkrtcp");

  if (transport->lower_transport == RTSP_LOWER_TRANS_TCP) {
    /* configure for interleaved delivery, nothing needs to be done
     * here, the loop function will call the chain functions of the
     * RTP session manager. */
    stream->rtpchannel = transport->interleaved.min;
    stream->rtcpchannel = transport->interleaved.max;
    GST_DEBUG ("stream %p on channels %d-%d", stream,
        stream->rtpchannel, stream->rtcpchannel);
  } else {
    /* multicast was selected, create UDP sources and join the multicast
     * group. */
    if (transport->multicast) {
      gchar *uri;

      /* creating RTP source */
      uri =
          g_strdup_printf ("udp://%s:%d", transport->destination,
          transport->port.min);
      stream->rtpsrc = gst_element_make_from_uri (GST_URI_SRC, uri, NULL);
      g_free (uri);
      if (stream->rtpsrc == NULL)
        goto no_element;

      /* creating RTCP source */
      uri =
          g_strdup_printf ("udp://%s:%d", transport->destination,
          transport->port.max);
      stream->rtcpsrc = gst_element_make_from_uri (GST_URI_SRC, uri, NULL);
      g_free (uri);
      if (stream->rtcpsrc == NULL)
        goto no_element;

      /* change state */
      gst_element_set_state (stream->rtpsrc, GST_STATE_PAUSED);
      gst_element_set_state (stream->rtcpsrc, GST_STATE_PAUSED);

      /* we manage these elements */
      gst_bin_add (GST_BIN_CAST (src), stream->rtpsrc);
      gst_bin_add (GST_BIN_CAST (src), stream->rtcpsrc);
    }

    /* set caps */
    g_object_set (G_OBJECT (stream->rtpsrc), "caps", stream->caps, NULL);

    /* configure for UDP delivery, we need to connect the UDP pads to
     * the RTP session plugin. */
    pad = gst_element_get_pad (stream->rtpsrc, "src");
    gst_pad_link (pad, stream->rtpdecrtp);
    gst_object_unref (pad);

    pad = gst_element_get_pad (stream->rtcpsrc, "src");
    gst_pad_link (pad, stream->rtpdecrtcp);
    gst_object_unref (pad);
  }

  pad = gst_element_get_pad (stream->rtpdec, "srcrtp");
  if (stream->caps) {
    gst_pad_use_fixed_caps (pad);
    gst_pad_set_caps (pad, stream->caps);
  }

  /* create ghostpad */
  name = g_strdup_printf ("rtp_stream%d", stream->id);
  template = gst_static_pad_template_get (&rtptemplate);
  stream->srcpad = gst_ghost_pad_new_from_template (name, pad, template);
  gst_object_unref (template);
  g_free (name);

  gst_object_unref (pad);

  /* mark pad as ok */
  stream->last_ret = GST_FLOW_OK;
  /* and add */
  gst_element_add_pad (GST_ELEMENT_CAST (src), stream->srcpad);

  return TRUE;

  /* ERRORS */
no_element:
  {
    GST_DEBUG_OBJECT (src, "no rtpdec element found");
    return FALSE;
  }
start_rtpdec_failure:
  {
    GST_DEBUG_OBJECT (src, "could not start RTP session");
    return FALSE;
  }
}

static gint
find_stream_by_channel (GstRTSPStream * stream, gconstpointer a)
{
  gint channel = GPOINTER_TO_INT (a);

  if (stream->rtpchannel == channel || stream->rtcpchannel == channel)
    return 0;

  return -1;
}

static GstFlowReturn
gst_rtspsrc_combine_flows (GstRTSPSrc * src, GstRTSPStream * stream,
    GstFlowReturn ret)
{
  GList *streams;

  /* store the value */
  stream->last_ret = ret;

  /* if it's success we can return the value right away */
  if (GST_FLOW_IS_SUCCESS (ret))
    goto done;

  /* any other error that is not-linked can be returned right
   * away */
  if (ret != GST_FLOW_NOT_LINKED)
    goto done;

  /* only return NOT_LINKED if all other pads returned NOT_LINKED */
  for (streams = src->streams; streams; streams = g_list_next (streams)) {
    GstRTSPStream *ostream = (GstRTSPStream *) streams->data;

    ret = ostream->last_ret;
    /* some other return value (must be SUCCESS but we can return
     * other values as well) */
    if (ret != GST_FLOW_NOT_LINKED)
      goto done;
  }
  /* if we get here, all other pads were unlinked and we return
   * NOT_LINKED then */
done:
  return ret;
}

static void
gst_rtspsrc_push_event (GstRTSPSrc * src, GstEvent * event)
{
  GList *streams;

  for (streams = src->streams; streams; streams = g_list_next (streams)) {
    GstRTSPStream *ostream = (GstRTSPStream *) streams->data;

    /* only pads that have a connection to the outside world */
    if (ostream->srcpad == NULL)
      continue;

    gst_event_ref (event);
    gst_pad_push_event (ostream->rtpdecrtp, event);
    gst_event_ref (event);
    gst_pad_push_event (ostream->rtpdecrtcp, event);
  }
  gst_event_unref (event);
}

static void
gst_rtspsrc_loop (GstRTSPSrc * src)
{
  RTSPMessage response = { 0 };
  RTSPResult res;
  gint channel;
  GList *lstream;
  GstRTSPStream *stream;
  GstPad *outpad = NULL;
  guint8 *data;
  guint size;
  GstFlowReturn ret = GST_FLOW_OK;
  GstCaps *caps = NULL;

  do {
    GST_DEBUG_OBJECT (src, "doing receive");
    if ((res = rtsp_connection_receive (src->connection, &response)) < 0)
      goto receive_error;
    GST_DEBUG_OBJECT (src, "got packet type %d", response.type);
  }
  while (response.type != RTSP_MESSAGE_DATA);

  channel = response.type_data.data.channel;

  lstream = g_list_find_custom (src->streams, GINT_TO_POINTER (channel),
      (GCompareFunc) find_stream_by_channel);
  if (!lstream)
    goto unknown_stream;

  stream = (GstRTSPStream *) lstream->data;
  if (channel == stream->rtpchannel) {
    outpad = stream->rtpdecrtp;
    caps = stream->caps;
  } else if (channel == stream->rtcpchannel) {
    outpad = stream->rtpdecrtcp;
  }

  /* take a look at the body to figure out what we have */
  rtsp_message_get_body (&response, &data, &size);
  if (size < 2)
    goto invalid_length;

  /* channels are not correct on some servers, do extra check */
  if (data[1] >= 200 && data[1] <= 204) {
    /* hmm RTCP message */
    outpad = stream->rtpdecrtcp;
  }

  /* we have no clue what this is, just ignore then. */
  if (outpad == NULL)
    goto unknown_stream;

  /* and chain buffer to internal element */
  {
    GstBuffer *buf;

    rtsp_message_steal_body (&response, &data, &size);

    /* strip the trailing \0 */
    size -= 1;

    buf = gst_buffer_new_and_alloc (size);
    GST_BUFFER_DATA (buf) = data;
    GST_BUFFER_MALLOCDATA (buf) = data;
    GST_BUFFER_SIZE (buf) = size;

    if (caps)
      gst_buffer_set_caps (buf, caps);

    GST_DEBUG_OBJECT (src, "pushing data of size %d on channel %d", size,
        channel);

    /* chain to the peer pad */
    ret = gst_pad_chain (outpad, buf);

    /* combine all stream flows */
    ret = gst_rtspsrc_combine_flows (src, stream, ret);
    if (ret != GST_FLOW_OK)
      goto need_pause;
  }
  return;

  /* ERRORS */
unknown_stream:
  {
    GST_DEBUG_OBJECT (src, "unknown stream on channel %d, ignored", channel);
    return;
  }
receive_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ,
        ("Could not receive message."), (NULL));
    ret = GST_FLOW_UNEXPECTED;
    goto need_pause;
  }
invalid_length:
  {
    GST_ELEMENT_WARNING (src, RESOURCE, READ,
        ("Short message received."), (NULL));
    return;
  }
need_pause:
  {
    const gchar *reason = gst_flow_get_name (ret);

    GST_DEBUG_OBJECT (src, "pausing task, reason %s", reason);
    src->running = FALSE;
    gst_task_pause (src->task);
    if (GST_FLOW_IS_FATAL (ret) || ret == GST_FLOW_NOT_LINKED) {
      if (ret == GST_FLOW_UNEXPECTED) {
        /* perform EOS logic */
        if (src->segment.flags & GST_SEEK_FLAG_SEGMENT) {
          gst_element_post_message (GST_ELEMENT_CAST (src),
              gst_message_new_segment_done (GST_OBJECT_CAST (src),
                  src->segment.format, src->segment.last_stop));
        } else {
          gst_rtspsrc_push_event (src, gst_event_new_eos ());
        }
      } else {
        /* for fatal errors we post an error message, post the error
         * first so the app knows about the error first. */
        GST_ELEMENT_ERROR (src, STREAM, FAILED,
            ("Internal data flow error."),
            ("streaming task paused, reason %s (%d)", reason, ret));
        gst_rtspsrc_push_event (src, gst_event_new_eos ());
      }
    }
    return;
  }
}

/**
 * gst_rtspsrc_send:
 * @src: the rtsp source
 * @request: must point to a valid request
 * @response: must point to an empty #RTSPMessage
 *
 * send @request and retrieve the response in @response. optionally @code can be
 * non-NULL in which case it will contain the status code of the response.
 *
 * If This function returns TRUE, @response will contain a valid response
 * message that should be cleaned with rtsp_message_unset() after usage. 
 *
 * If @code is NULL, this function will return FALSE (with an invalid @response
 * message) if the response code was not 200 (OK).
 *
 * Returns: TRUE if the processing was successful.
 */
static gboolean
gst_rtspsrc_send (GstRTSPSrc * src, RTSPMessage * request,
    RTSPMessage * response, RTSPStatusCode * code)
{
  RTSPResult res;
  RTSPStatusCode thecode;

  if (src->debug)
    rtsp_message_dump (request);

  if ((res = rtsp_connection_send (src->connection, request)) < 0)
    goto send_error;

  if ((res = rtsp_connection_receive (src->connection, response)) < 0)
    goto receive_error;

  if (src->debug)
    rtsp_message_dump (response);

  thecode = response->type_data.response.code;
  /* if the caller wanted the result code, we store it. Else we check if it's
   * OK. */
  if (code)
    *code = thecode;
  else if (thecode != RTSP_STS_OK)
    goto error_response;

  return TRUE;

  /* ERRORS */
send_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE,
        ("Could not send message."), (NULL));
    return FALSE;
  }
receive_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ,
        ("Could not receive message."), (NULL));
    return FALSE;
  }
error_response:
  {
    switch (response->type_data.response.code) {
      case RTSP_STS_NOT_FOUND:
        GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, ("%s",
                response->type_data.response.reason), (NULL));
        break;
      default:
        GST_ELEMENT_ERROR (src, RESOURCE, READ, ("Got error response: %d (%s).",
                response->type_data.response.code,
                response->type_data.response.reason), (NULL));
        break;
    }
    /* we return FALSE so we should unset the response ourselves */
    rtsp_message_unset (response);
    return FALSE;
  }
}

/* parse the response and collect all the supported methods. We need this
 * information so that we don't try to send an unsupported request to the
 * server.
 */
static gboolean
gst_rtspsrc_parse_methods (GstRTSPSrc * src, RTSPMessage * response)
{
  gchar *respoptions = NULL;
  gchar **options;
  gint i;

  /* clear supported methods */
  src->methods = 0;

  /* Try Allow Header first */
  rtsp_message_get_header (response, RTSP_HDR_ALLOW, &respoptions);
  if (!respoptions)
    /* Then maybe Public Header... */
    rtsp_message_get_header (response, RTSP_HDR_PUBLIC, &respoptions);
  if (!respoptions) {
    /* this field is not required, assume the server supports
     * DESCRIBE, SETUP and PLAY */
    GST_DEBUG_OBJECT (src, "could not get OPTIONS");
    src->methods = RTSP_DESCRIBE | RTSP_SETUP | RTSP_PLAY;
    goto done;
  }

  /* If we get here, the server gave a list of supported methods, parse
   * them here. The string is like: 
   *
   * OPTIONS, DESCRIBE, ANNOUNCE, PLAY, SETUP, ...
   */
  options = g_strsplit (respoptions, ",", 0);

  for (i = 0; options[i]; i++) {
    gchar *stripped;
    gint method;

    stripped = g_strstrip (options[i]);
    method = rtsp_find_method (stripped);

    /* keep bitfield of supported methods */
    if (method != -1)
      src->methods |= method;
  }
  g_strfreev (options);

  /* we need describe and setup */
  if (!(src->methods & RTSP_DESCRIBE))
    goto no_describe;
  if (!(src->methods & RTSP_SETUP))
    goto no_setup;

done:
  return TRUE;

  /* ERRORS */
no_describe:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        ("Server does not support DESCRIBE."), (NULL));
    return FALSE;
  }
no_setup:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        ("Server does not support SETUP."), (NULL));
    return FALSE;
  }
}

static gboolean
gst_rtspsrc_open (GstRTSPSrc * src)
{
  RTSPResult res;
  RTSPMessage request = { 0 };
  RTSPMessage response = { 0 };
  guint8 *data;
  guint size;
  gint i, n_streams;
  SDPMessage sdp = { 0 };
  GstRTSPProto protocols;
  GstRTSPStream *stream = NULL;

  /* can't continue without a valid url */
  if (G_UNLIKELY (src->url == NULL))
    goto no_url;

  /* open connection */
  GST_DEBUG_OBJECT (src, "opening connection (%s)...", src->location);
  if ((res = rtsp_connection_open (src->url, &src->connection)) < 0)
    goto could_not_open;

  /* create OPTIONS */
  GST_DEBUG_OBJECT (src, "create options...");
  res = rtsp_message_init_request (&request, RTSP_OPTIONS, src->location);
  if (res < 0)
    goto create_request_failed;

  /* send OPTIONS */
  GST_DEBUG_OBJECT (src, "send options...");
  if (!gst_rtspsrc_send (src, &request, &response, NULL))
    goto send_error;

  /* parse OPTIONS */
  if (!gst_rtspsrc_parse_methods (src, &response))
    goto methods_error;

  /* create DESCRIBE */
  GST_DEBUG_OBJECT (src, "create describe...");
  res = rtsp_message_init_request (&request, RTSP_DESCRIBE, src->location);
  if (res < 0)
    goto create_request_failed;

  /* we only accept SDP for now */
  rtsp_message_add_header (&request, RTSP_HDR_ACCEPT, "application/sdp");

  /* send DESCRIBE */
  GST_DEBUG_OBJECT (src, "send describe...");
  if (!gst_rtspsrc_send (src, &request, &response, NULL))
    goto send_error;

  /* check if reply is SDP */
  {
    gchar *respcont = NULL;

    rtsp_message_get_header (&response, RTSP_HDR_CONTENT_TYPE, &respcont);
    /* could not be set but since the request returned OK, we assume it
     * was SDP, else check it. */
    if (respcont) {
      if (!g_ascii_strcasecmp (respcont, "application/sdp") == 0)
        goto wrong_content_type;
    }
  }

  /* get message body and parse as SDP */
  rtsp_message_get_body (&response, &data, &size);

  GST_DEBUG_OBJECT (src, "parse sdp...");
  sdp_message_init (&sdp);
  sdp_message_parse_buffer (data, size, &sdp);

  if (src->debug)
    sdp_message_dump (&sdp);

  /* we initially allow all configured protocols. based on the replies from the
   * server we narrow them down. */
  protocols = src->protocols;

  /* setup streams */
  n_streams = sdp_message_medias_len (&sdp);
  for (i = 0; i < n_streams; i++) {
    SDPMedia *media;
    gchar *transports;

    media = sdp_message_get_media (&sdp, i);

    /* create stream from the media */
    stream = gst_rtspsrc_create_stream (src, media);

    /* skip setup if we have no URL for it */
    if (stream->setup_url == NULL)
      continue;

    GST_DEBUG_OBJECT (src, "doing setup of stream %d with %s", i,
        stream->setup_url);

    /* create SETUP request */
    res = rtsp_message_init_request (&request, RTSP_SETUP, stream->setup_url);
    if (res < 0)
      goto create_request_failed;

    transports = g_strdup ("");
    if (protocols & GST_RTSP_PROTO_UDP_UNICAST) {
      gchar *new;
      gint rtpport, rtcpport;
      gchar *trxparams;

      /* allocate two UDP ports */
      if (!gst_rtspsrc_stream_setup_rtp (stream, &rtpport, &rtcpport))
        goto setup_rtp_failed;

      GST_DEBUG_OBJECT (src, "setting up RTP ports %d-%d", rtpport, rtcpport);

      trxparams = g_strdup_printf ("client_port=%d-%d", rtpport, rtcpport);
      new = g_strconcat (transports, "RTP/AVP/UDP;unicast;", trxparams, NULL);
      g_free (trxparams);
      g_free (transports);
      transports = new;
    }
    if (protocols & GST_RTSP_PROTO_UDP_MULTICAST) {
      gchar *new;

      GST_DEBUG_OBJECT (src, "setting up MULTICAST");

      /* we don't hav to allocate any UDP ports yet, if the selected transport
       * turns out to be multicast we can create them and join the multicast
       * group indicated in the transport reply */
      new =
          g_strconcat (transports, transports[0] ? "," : "",
          "RTP/AVP/UDP;multicast", NULL);
      g_free (transports);
      transports = new;
    }
    if (protocols & GST_RTSP_PROTO_TCP) {
      gchar *new, *interleaved;
      gint channel;

      GST_DEBUG_OBJECT (src, "setting up TCP");

      /* the channels for this stream is by default the next available number */
      channel = i * 2;
      interleaved = g_strdup_printf ("interleaved=%d-%d", channel, channel + 1);
      new =
          g_strconcat (transports, transports[0] ? "," : "",
          "RTP/AVP/TCP;unicast;", interleaved, NULL);
      g_free (interleaved);
      g_free (transports);
      transports = new;
    }

    /* select transport, copy is made when adding to header */
    rtsp_message_add_header (&request, RTSP_HDR_TRANSPORT, transports);
    g_free (transports);

    if (!gst_rtspsrc_send (src, &request, &response, NULL))
      goto send_error;

    /* parse response transport */
    {
      gchar *resptrans = NULL;
      RTSPTransport transport = { 0 };

      rtsp_message_get_header (&response, RTSP_HDR_TRANSPORT, &resptrans);
      if (!resptrans)
        goto no_transport;

      /* parse transport */
      rtsp_transport_parse (resptrans, &transport);

      /* update allowed transports for other streams. once the transport of
       * one stream has been determined, we make sure that all other streams
       * are configured in the same way */
      if (transport.lower_transport == RTSP_LOWER_TRANS_TCP) {
        GST_DEBUG_OBJECT (src, "stream %d as TCP", i);
        protocols = GST_RTSP_PROTO_TCP;
        src->interleaved = TRUE;
      } else {
        if (transport.multicast) {
          /* only allow multicast for other streams */
          GST_DEBUG_OBJECT (src, "stream %d as MULTICAST", i);
          protocols = GST_RTSP_PROTO_UDP_MULTICAST;
        } else {
          /* only allow unicast for other streams */
          GST_DEBUG_OBJECT (src, "stream %d as UNICAST", i);
          protocols = GST_RTSP_PROTO_UDP_UNICAST;
        }
      }

      if (!stream->container || !src->interleaved) {
        /* now configure the stream with the transport */
        if (!gst_rtspsrc_stream_configure_transport (stream, &transport)) {
          GST_DEBUG_OBJECT (src,
              "could not configure stream %d transport, skipping stream", i);
        }
      }

      /* clean up our transport struct */
      rtsp_transport_init (&transport);
    }
  }
  /* if we got here all was configured. We have dynamic pads so we notify that
   * we are done */
  gst_element_no_more_pads (GST_ELEMENT_CAST (src));

  /* clean up any messages */
  rtsp_message_unset (&request);
  rtsp_message_unset (&response);

  return TRUE;

  /* ERRORS */
no_url:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
        ("No valid RTSP url was provided"), (NULL));
    goto cleanup_error;
  }
could_not_open:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ_WRITE,
        ("Could not open connection."), (NULL));
    goto cleanup_error;
  }
create_request_failed:
  {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        ("Could not create request."), (NULL));
    goto cleanup_error;
  }
send_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE,
        ("Could not send message."), (NULL));
    goto cleanup_error;
  }
methods_error:
  {
    /* error was posted */
    goto cleanup_error;
  }
wrong_content_type:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS,
        ("Server does not support SDP."), (NULL));
    goto cleanup_error;
  }
setup_rtp_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, ("Could not setup rtp."),
        (NULL));
    goto cleanup_error;
  }
no_transport:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS,
        ("Server did not select transport."), (NULL));
    goto cleanup_error;
  }
cleanup_error:
  {
    rtsp_message_unset (&request);
    rtsp_message_unset (&response);
    return FALSE;
  }
}

static gboolean
gst_rtspsrc_close (GstRTSPSrc * src)
{
  RTSPMessage request = { 0 };
  RTSPMessage response = { 0 };
  RTSPResult res;

  GST_DEBUG_OBJECT (src, "TEARDOWN...");

  /* stop task if any */
  if (src->task) {
    gst_task_stop (src->task);

    /* make sure it is not running */
    g_static_rec_mutex_lock (src->stream_rec_lock);
    g_static_rec_mutex_unlock (src->stream_rec_lock);

    /* no wait for the task to finish */
    gst_task_join (src->task);

    /* and free the task */
    gst_object_unref (GST_OBJECT (src->task));
    src->task = NULL;
  }

  if (src->methods & RTSP_PLAY) {
    /* do TEARDOWN */
    res = rtsp_message_init_request (&request, RTSP_TEARDOWN, src->location);
    if (res < 0)
      goto create_request_failed;

    if (!gst_rtspsrc_send (src, &request, &response, NULL))
      goto send_error;

    /* FIXME, parse result? */
    rtsp_message_unset (&request);
    rtsp_message_unset (&response);
  }

  /* close connection */
  GST_DEBUG_OBJECT (src, "closing connection...");
  if ((res = rtsp_connection_close (src->connection)) < 0)
    goto close_failed;

  return TRUE;

  /* ERRORS */
create_request_failed:
  {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        ("Could not create request."), (NULL));
    return FALSE;
  }
send_error:
  {
    rtsp_message_unset (&request);
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE,
        ("Could not send message."), (NULL));
    return FALSE;
  }
close_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, CLOSE, ("Close failed."), (NULL));
    return FALSE;
  }
}

/* RTP-Info is of the format:
 *
 * url=<URL>;[seq=<seqbase>;rtptime=<timebase>] [, url=...]
 */
static gboolean
gst_rtspsrc_parse_rtpinfo (GstRTSPSrc * src, gchar * rtpinfo)
{
  gchar **infos;
  gint i;

  infos = g_strsplit (rtpinfo, ",", 0);
  for (i = 0; infos[i]; i++) {
    /* FIXME, do something here:
     * parse url, find stream for url.
     * parse seq and rtptime. The seq number should be configured in the rtp
     * depayloader or session manager to detect gaps. Same for the rtptime, it
     * should be used to create an initial time newsegment.
     */
  }
  g_strfreev (infos);

  return TRUE;
}

static gboolean
gst_rtspsrc_play (GstRTSPSrc * src)
{
  RTSPMessage request = { 0 };
  RTSPMessage response = { 0 };
  RTSPResult res;
  gchar *rtpinfo;

  if (!(src->methods & RTSP_PLAY))
    return TRUE;

  GST_DEBUG_OBJECT (src, "PLAY...");

  /* do play */
  res = rtsp_message_init_request (&request, RTSP_PLAY, src->location);
  if (res < 0)
    goto create_request_failed;

  rtsp_message_add_header (&request, RTSP_HDR_RANGE, "npt=0.000-");

  if (!gst_rtspsrc_send (src, &request, &response, NULL))
    goto send_error;

  rtsp_message_unset (&request);

  /* parse the RTP-Info header field (if ANY) to get the base seqnum and timestamp
   * for the RTP packets. If this is not present, we assume all starts from 0... 
   * FIXME, this is info for the RTP session manager ideally. */
  rtsp_message_get_header (&response, RTSP_HDR_RTP_INFO, &rtpinfo);
  if (rtpinfo)
    gst_rtspsrc_parse_rtpinfo (src, rtpinfo);

  rtsp_message_unset (&response);

  /* for interleaved transport, we receive the data on the RTSP connection
   * instead of UDP. We start a task to select and read from that connection. */
  if (src->interleaved) {
    if (src->task == NULL) {
      src->task = gst_task_create ((GstTaskFunction) gst_rtspsrc_loop, src);
      gst_task_set_lock (src->task, src->stream_rec_lock);
    }
    src->running = TRUE;
    gst_task_start (src->task);
  }

  return TRUE;

  /* ERRORS */
create_request_failed:
  {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        ("Could not create request."), (NULL));
    return FALSE;
  }
send_error:
  {
    rtsp_message_unset (&request);
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE,
        ("Could not send message."), (NULL));
    return FALSE;
  }
}

static gboolean
gst_rtspsrc_pause (GstRTSPSrc * src)
{
  RTSPMessage request = { 0 };
  RTSPMessage response = { 0 };
  RTSPResult res;

  if (!(src->methods & RTSP_PAUSE))
    return TRUE;

  GST_DEBUG_OBJECT (src, "PAUSE...");
  /* do pause */
  res = rtsp_message_init_request (&request, RTSP_PAUSE, src->location);
  if (res < 0)
    goto create_request_failed;

  if (!gst_rtspsrc_send (src, &request, &response, NULL))
    goto send_error;

  rtsp_message_unset (&request);
  rtsp_message_unset (&response);

  return TRUE;

  /* ERRORS */
create_request_failed:
  {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        ("Could not create request."), (NULL));
    return FALSE;
  }
send_error:
  {
    rtsp_message_unset (&request);
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE,
        ("Could not send message."), (NULL));
    return FALSE;
  }
}

static GstStateChangeReturn
gst_rtspsrc_change_state (GstElement * element, GstStateChange transition)
{
  GstRTSPSrc *rtspsrc;
  GstStateChangeReturn ret;

  rtspsrc = GST_RTSPSRC (element);


  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      rtspsrc->interleaved = FALSE;
      gst_segment_init (&rtspsrc->segment, GST_FORMAT_TIME);
      if (!gst_rtspsrc_open (rtspsrc))
        goto open_failed;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      gst_rtspsrc_play (rtspsrc);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto done;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      ret = GST_STATE_CHANGE_NO_PREROLL;
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      gst_rtspsrc_pause (rtspsrc);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_rtspsrc_close (rtspsrc);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

done:
  return ret;

open_failed:
  {
    return GST_STATE_CHANGE_FAILURE;
  }
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static guint
gst_rtspsrc_uri_get_type (void)
{
  return GST_URI_SRC;
}
static gchar **
gst_rtspsrc_uri_get_protocols (void)
{
  static gchar *protocols[] = { "rtsp", NULL };

  return protocols;
}

static const gchar *
gst_rtspsrc_uri_get_uri (GstURIHandler * handler)
{
  GstRTSPSrc *src = GST_RTSPSRC (handler);

  /* should not dup */
  return src->location;
}

static gboolean
gst_rtspsrc_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  GstRTSPSrc *src;
  RTSPResult res;
  RTSPUrl *newurl;

  src = GST_RTSPSRC (handler);

  /* same URI, we're fine */
  if (src->location && uri && !strcmp (uri, src->location))
    goto was_ok;

  /* try to parse */
  if ((res = rtsp_url_parse (uri, &newurl)) < 0)
    goto parse_error;

  /* if worked, free previous and store new url object along with the original
   * location. */
  rtsp_url_free (src->url);
  src->url = newurl;
  g_free (src->location);
  src->location = g_strdup (uri);

  GST_DEBUG_OBJECT (src, "set uri: %s", GST_STR_NULL (uri));

  return TRUE;

  /* Special cases */
was_ok:
  {
    GST_DEBUG_OBJECT (src, "URI was ok: '%s'", GST_STR_NULL (uri));
    return TRUE;
  }
parse_error:
  {
    GST_ERROR_OBJECT (src, "Not a valid RTSP url '%s' (%d)",
        GST_STR_NULL (uri), res);
    return FALSE;
  }
}

static void
gst_rtspsrc_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_rtspsrc_uri_get_type;
  iface->get_protocols = gst_rtspsrc_uri_get_protocols;
  iface->get_uri = gst_rtspsrc_uri_get_uri;
  iface->set_uri = gst_rtspsrc_uri_set_uri;
}
