/* GStreamer
 * Copyright (C) <2006> Wim Taymans <wim@fluendo.com>
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
#  include "config.h"
#endif

#include <string.h>

#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpvorbispay.h"

GST_DEBUG_CATEGORY_STATIC (rtpvorbispay_debug);
#define GST_CAT_DEFAULT (rtpvorbispay_debug)

/* references:
 * http://svn.xiph.org/trunk/vorbis/doc/draft-ietf-avt-rtp-vorbis-01.txt
 */

/* elementfactory information */
static const GstElementDetails gst_rtp_vorbispay_details =
GST_ELEMENT_DETAILS ("RTP packet parser",
    "Codec/Payloader/Network",
    "Payload-encode Vorbis audio into RTP packets (draft-01 RFC XXXX)",
    "Wim Taymans <wim@fluendo.com>");

static GstStaticPadTemplate gst_rtp_vorbis_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) [ 96, 127 ], "
        "clock-rate = (int) [1, MAX ], " "encoding-name = (string) \"vorbis\""
        /* All required parameters
         *
         * "encoding-params = (string) <num channels>"
         * "delivery-method = (string) { inline, in_band, out_band/<specific_name> } "
         * "configuration = (string) ANY"
         */
        /* All optional parameters
         *
         * "configuration-uri ="
         */
    )
    );

static GstStaticPadTemplate gst_rtp_vorbis_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-vorbis")
    );

GST_BOILERPLATE (GstRtpVorbisPay, gst_rtp_vorbis_pay, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD);

static gboolean gst_rtp_vorbis_pay_setcaps (GstBaseRTPPayload * basepayload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_vorbis_pay_handle_buffer (GstBaseRTPPayload * pad,
    GstBuffer * buffer);

static void
gst_rtp_vorbis_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_vorbis_pay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_vorbis_pay_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_vorbispay_details);
}

static void
gst_rtp_vorbis_pay_class_init (GstRtpVorbisPayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstbasertppayload_class->set_caps = gst_rtp_vorbis_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_vorbis_pay_handle_buffer;

  GST_DEBUG_CATEGORY_INIT (rtpvorbispay_debug, "rtpvorbispay", 0,
      "Vorbis RTP Payloader");
}

static void
gst_rtp_vorbis_pay_init (GstRtpVorbisPay * rtpvorbispay,
    GstRtpVorbisPayClass * klass)
{
}

static gboolean
gst_rtp_vorbis_pay_setcaps (GstBaseRTPPayload * basepayload, GstCaps * caps)
{
  GstRtpVorbisPay *rtpvorbispay;

  rtpvorbispay = GST_RTP_VORBIS_PAY (basepayload);

  rtpvorbispay->need_headers = TRUE;

  return TRUE;
}

static void
gst_rtp_vorbis_pay_reset_packet (GstRtpVorbisPay * rtpvorbispay, guint8 VDT)
{
  guint payload_len;

  GST_DEBUG_OBJECT (rtpvorbispay, "reset packet");

  rtpvorbispay->payload_pos = 4;
  payload_len = gst_rtp_buffer_get_payload_len (rtpvorbispay->packet);
  rtpvorbispay->payload_left = payload_len - 4;
  rtpvorbispay->payload_duration = 0;
  rtpvorbispay->payload_ident = 0;
  rtpvorbispay->payload_F = 0;
  rtpvorbispay->payload_VDT = VDT;
  rtpvorbispay->payload_pkts = 0;
}

static void
gst_rtp_vorbis_pay_init_packet (GstRtpVorbisPay * rtpvorbispay, guint8 VDT)
{
  GST_DEBUG_OBJECT (rtpvorbispay, "starting new packet, VDT: %d", VDT);

  if (rtpvorbispay->packet)
    gst_buffer_unref (rtpvorbispay->packet);

  /* new packet allocate max packet size */
  rtpvorbispay->packet =
      gst_rtp_buffer_new_allocate_len (GST_BASE_RTP_PAYLOAD_MTU
      (rtpvorbispay), 0, 0);
  gst_rtp_vorbis_pay_reset_packet (rtpvorbispay, VDT);
}

static GstFlowReturn
gst_rtp_vorbis_pay_flush_packet (GstRtpVorbisPay * rtpvorbispay)
{
  GstFlowReturn ret;
  guint8 *payload;
  guint hlen;

  /* check for empty packet */
  if (!rtpvorbispay->packet || rtpvorbispay->payload_pos <= 4)
    return GST_FLOW_OK;

  GST_DEBUG_OBJECT (rtpvorbispay, "flushing packet");

  /* fix header */
  payload = gst_rtp_buffer_get_payload (rtpvorbispay->packet);
  /*
   *  0                   1                   2                   3
   *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                     Ident                     | F |VDT|# pkts.|
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   *
   * F: Fragment type (0=none, 1=start, 2=cont, 3=end)
   * VDT: Vorbis data type (0=vorbis, 1=config, 2=comment, 3=reserved)
   * pkts: number of packets.
   */
  payload[0] = (rtpvorbispay->payload_ident >> 16) & 0xff;
  payload[1] = (rtpvorbispay->payload_ident >> 8) & 0xff;
  payload[2] = (rtpvorbispay->payload_ident) & 0xff;
  payload[3] = (rtpvorbispay->payload_F & 0x3) << 6 |
      (rtpvorbispay->payload_VDT & 0x3) << 4 |
      (rtpvorbispay->payload_pkts & 0xf);

  /* shrink the buffer size to the last written byte */
  hlen = gst_rtp_buffer_calc_header_len (0);
  GST_BUFFER_SIZE (rtpvorbispay->packet) = hlen + rtpvorbispay->payload_pos;

  /* push, this gives away our ref to the packet, so clear it. */
  ret =
      gst_basertppayload_push (GST_BASE_RTP_PAYLOAD (rtpvorbispay),
      rtpvorbispay->packet);
  rtpvorbispay->packet = NULL;

  return ret;
}

static gboolean
gst_rtp_vorbis_pay_finish_headers (GstBaseRTPPayload * basepayload)
{
  GstRtpVorbisPay *rtpvorbispay = GST_RTP_VORBIS_PAY (basepayload);
  GList *walk;
  guint length;
  gchar *cstr, *configuration;
  guint8 *data;
  guint32 ident;
  GValue v = { 0 };
  GstBuffer *config;

  GST_DEBUG_OBJECT (rtpvorbispay, "finish headers");

  if (!rtpvorbispay->headers)
    goto no_headers;

  /* we need exactly 2 header packets */
  if (g_list_length (rtpvorbispay->headers) != 2)
    goto no_headers;

  /* +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                     Number of packed headers                  |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                          Packed header                        |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                          Packed header                        |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                          ....                                 |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   *
   * We only construct a config containing 1 packed header like this:
   *
   *  0                   1                   2                   3
   *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                   Ident                       |              ..
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ..   length     |              Identification Header           ..
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ..                    Identification Header                     |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                          Setup Header                        ..
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ..                         Setup Header                         |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   *
   */

  /* count the size of the headers first */
  length = 0;
  for (walk = rtpvorbispay->headers; walk; walk = g_list_next (walk)) {
    GstBuffer *buf = GST_BUFFER_CAST (walk->data);

    length += GST_BUFFER_SIZE (buf);
  }

  /* total config length is the size of the headers + 2 bytes length +
   * 3 bytes for the ident */
  config = gst_buffer_new_and_alloc (length + 2 + 3);
  data = GST_BUFFER_DATA (config);

  /* we generate a random ident for this configuration */
  ident = g_random_int ();

  /* take lower 3 bytes */
  data[0] = ident & 0xff;
  data[1] = (ident >> 8) & 0xff;
  data[2] = (ident >> 16) & 0xff;

  data[3] = (length >> 8) & 0xff;
  data[4] = length & 0xff;

  /* copy header data */
  data += 5;
  for (walk = rtpvorbispay->headers; walk; walk = g_list_next (walk)) {
    GstBuffer *buf = GST_BUFFER_CAST (walk->data);

    memcpy (data, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
    data += GST_BUFFER_SIZE (buf);
  }

  /* serialize buffer to base16 */
  g_value_init (&v, GST_TYPE_BUFFER);
  gst_value_take_buffer (&v, config);
  configuration = gst_value_serialize (&v);

  /* configure payloader settings */
  cstr = g_strdup_printf ("%d", rtpvorbispay->channels);
  gst_basertppayload_set_options (basepayload, "audio", TRUE, "vorbis",
      rtpvorbispay->rate);
  gst_basertppayload_set_outcaps (basepayload, "encoding-params", G_TYPE_STRING,
      cstr, "configuration", G_TYPE_STRING, configuration,
      /* don't set the defaults 
       */
      NULL);
  g_free (cstr);
  g_free (configuration);
  g_value_unset (&v);

  return TRUE;

  /* ERRORS */
no_headers:
  {
    GST_DEBUG_OBJECT (rtpvorbispay, "finish headers");
    return FALSE;
  }
}

static gboolean
gst_rtp_vorbis_pay_parse_id (GstBaseRTPPayload * basepayload, guint8 * data,
    guint size)
{
  GstRtpVorbisPay *rtpvorbispay = GST_RTP_VORBIS_PAY (basepayload);
  guint8 channels;
  gint32 rate, version;

  if (G_UNLIKELY (size < 16))
    goto too_short;

  if (G_UNLIKELY (memcmp (data, "\001vorbis", 7)))
    goto invalid_start;
  data += 7;

  if (G_UNLIKELY ((version = GST_READ_UINT32_LE (data)) != 0))
    goto invalid_version;
  data += 4;

  if (G_UNLIKELY ((channels = *data++) < 1))
    goto invalid_channels;

  if (G_UNLIKELY ((rate = GST_READ_UINT32_LE (data)) < 1))
    goto invalid_rate;

  /* all fine, store the values */
  rtpvorbispay->channels = channels;
  rtpvorbispay->rate = rate;

  return TRUE;

  /* ERRORS */
too_short:
  {
    GST_ELEMENT_ERROR (basepayload, STREAM, DECODE,
        ("Identification packet is too short, need at least 16, got %d", size),
        (NULL));
    return FALSE;
  }
invalid_start:
  {
    GST_ELEMENT_ERROR (basepayload, STREAM, DECODE,
        ("Invalid header start in identification packet"), (NULL));
    return FALSE;
  }
invalid_version:
  {
    GST_ELEMENT_ERROR (basepayload, STREAM, DECODE,
        ("Invalid version, expected 0, got %d", version), (NULL));
    return FALSE;
  }
invalid_rate:
  {
    GST_ELEMENT_ERROR (basepayload, STREAM, DECODE,
        ("Invalid rate %d", rate), (NULL));
    return FALSE;
  }
invalid_channels:
  {
    GST_ELEMENT_ERROR (basepayload, STREAM, DECODE,
        ("Invalid channels %d", channels), (NULL));
    return FALSE;
  }
}

static GstFlowReturn
gst_rtp_vorbis_pay_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpVorbisPay *rtpvorbispay;
  GstFlowReturn ret;
  guint size, newsize;
  guint8 *data;
  guint packet_len;
  GstClockTime duration, newduration;
  gboolean flush;
  guint8 VDT;
  guint plen;
  guint8 *ppos, *payload;
  gboolean fragmented;

  rtpvorbispay = GST_RTP_VORBIS_PAY (basepayload);

  size = GST_BUFFER_SIZE (buffer);
  data = GST_BUFFER_DATA (buffer);
  duration = GST_BUFFER_DURATION (buffer);

  GST_DEBUG_OBJECT (rtpvorbispay, "size %u, duration %" GST_TIME_FORMAT,
      size, GST_TIME_ARGS (duration));

  if (G_UNLIKELY (size < 1 || size > 0xffff))
    goto wrong_size;

  /* find packet type */
  if (data[0] & 1) {
    /* header */
    if (data[0] == 1) {
      /* identification, we need to parse this in order to get the clock rate. */
      if (G_UNLIKELY (!gst_rtp_vorbis_pay_parse_id (basepayload, data, size)))
        goto parse_id_failed;
      VDT = 1;
    } else if (data[0] == 5)
      /* setup */
      VDT = 1;
    else if (data[0] == 3) {
      /* comment */
      VDT = 2;
    } else
      goto unknown_header;
  } else
    /* data */
    VDT = 0;

  if (rtpvorbispay->need_headers) {
    /* we need to collect the headers and construct a config string from them */
    if (VDT == 2) {
      GST_DEBUG_OBJECT (rtpvorbispay,
          "discard comment packet while collecting headers");
      ret = GST_FLOW_OK;
      goto done;
    } else if (VDT != 0) {
      GST_DEBUG_OBJECT (rtpvorbispay, "collecting header");
      /* append header to the list of headers */
      rtpvorbispay->headers = g_list_append (rtpvorbispay->headers, buffer);
      ret = GST_FLOW_OK;
      goto done;
    } else {
      if (!gst_rtp_vorbis_pay_finish_headers (basepayload))
        goto header_error;
      rtpvorbispay->need_headers = FALSE;
    }
  }

  /* size increases with packet length and 2 bytes size eader. */
  newduration = rtpvorbispay->payload_duration;
  if (duration != GST_CLOCK_TIME_NONE)
    newduration += duration;

  newsize = rtpvorbispay->payload_pos + 2 + size;
  packet_len = gst_rtp_buffer_calc_packet_len (newsize, 0, 0);

  /* check buffer filled against length and max latency */
  flush = gst_basertppayload_is_filled (basepayload, packet_len, newduration);
  /* we can store up to 15 vorbis packets in one RTP packet. */
  flush |= (rtpvorbispay->payload_pkts == 15);
  /* flush if we have a new VDT */
  if (rtpvorbispay->packet)
    flush |= (rtpvorbispay->payload_VDT != VDT);
  if (flush)
    ret = gst_rtp_vorbis_pay_flush_packet (rtpvorbispay);

  /* create new packet if we must */
  if (!rtpvorbispay->packet)
    gst_rtp_vorbis_pay_init_packet (rtpvorbispay, VDT);

  payload = gst_rtp_buffer_get_payload (rtpvorbispay->packet);
  ppos = payload + rtpvorbispay->payload_pos;
  fragmented = FALSE;

  ret = GST_FLOW_OK;

  /* put buffer in packet, it either fits completely or needs to be fragmented
   * over multiple RTP packets. */
  while (size) {
    plen = MIN (rtpvorbispay->payload_left - 2, size);

    GST_DEBUG_OBJECT (rtpvorbispay, "append %u bytes", plen);

    /* data is copied in the payload with a 2 byte length header */
    ppos[0] = (plen >> 8) & 0xff;
    ppos[1] = (plen & 0xff);
    memcpy (&ppos[2], data, plen);

    size -= plen;
    data += plen;

    rtpvorbispay->payload_pos += plen + 2;
    rtpvorbispay->payload_left -= plen + 2;

    if (fragmented) {
      if (size == 0)
        /* last fragment, set F to 0x3. */
        rtpvorbispay->payload_F = 0x3;
      else
        /* fragment continues, set F to 0x2. */
        rtpvorbispay->payload_F = 0x2;
    } else {
      if (size > 0) {
        /* fragmented packet starts, set F to 0x1, mark ourselves as
         * fragmented. */
        rtpvorbispay->payload_F = 0x1;
        fragmented = TRUE;
      }
    }
    if (fragmented) {
      /* fragmented packets are always flushed and have ptks of 0 */
      rtpvorbispay->payload_pkts = 0;
      ret = gst_rtp_vorbis_pay_flush_packet (rtpvorbispay);

      if (size > 0) {
        /* start new packet and get pointers. VDT stays the same. */
        gst_rtp_vorbis_pay_init_packet (rtpvorbispay,
            rtpvorbispay->payload_VDT);
        payload = gst_rtp_buffer_get_payload (rtpvorbispay->packet);
        ppos = payload + rtpvorbispay->payload_pos;
      }
    } else {
      /* unfragmented packet, update stats for next packet, size == 0 and we
       * exit the while loop */
      rtpvorbispay->payload_pkts++;
      if (duration != GST_CLOCK_TIME_NONE)
        rtpvorbispay->payload_duration += duration;
    }
  }
done:
  return ret;

  /* ERRORS */
wrong_size:
  {
    GST_ELEMENT_WARNING (rtpvorbispay, STREAM, DECODE,
        ("Invalid packet size (1 < %d <= 0xffff)", size), (NULL));
    return GST_FLOW_OK;
  }
parse_id_failed:
  {
    return GST_FLOW_ERROR;
  }
unknown_header:
  {
    GST_ELEMENT_WARNING (rtpvorbispay, STREAM, DECODE,
        (NULL), ("Ignoring unknown header received"));
    return GST_FLOW_OK;
  }
header_error:
  {
    GST_ELEMENT_WARNING (rtpvorbispay, STREAM, DECODE,
        (NULL), ("Error initializing header config"));
    return GST_FLOW_OK;
  }
}

gboolean
gst_rtp_vorbis_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpvorbispay",
      GST_RANK_NONE, GST_TYPE_RTP_VORBIS_PAY);
}
