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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>

#include <string.h>
#include "gstrtpamrdepay.h"

/* references:
 *
 * RFC 3267 - Real-Time Transport Protocol (RTP) Payload Format and File
 * Storage Format for the Adaptive Multi-Rate (AMR) and Adaptive Multi-Rate
 * Wideband (AMR-WB) Audio Codecs.
 */

/* elementfactory information */
static const GstElementDetails gst_rtp_amrdepay_details =
GST_ELEMENT_DETAILS ("RTP packet parser",
    "Codec/Depayr/Network",
    "Extracts AMR audio from RTP packets (RFC 3267)",
    "Wim Taymans <wim@fluendo.com>");

/* RtpAMRDepay signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
};

/* input is an RTP packet
 *
 * params see RFC 3267, section 8.1
 */
static GstStaticPadTemplate gst_rtp_amr_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "clock-rate = (int) 8000, "
        "encoding-name = (string) \"AMR\", "
        "encoding-params = (string) \"1\", "
        "octet-align = (string) \"1\", "
        "crc = (string) { \"0\", \"1\" }, "
        "robust-sorting = (string) \"0\", " "interleaving = (string) \"0\""
        /* following options are not needed for a decoder
         *
         "mode-set = (int) [ 0, 7 ], "
         "mode-change-period = (int) [ 1, MAX ], "
         "mode-change-neighbor = (boolean) { TRUE, FALSE }, "
         "maxptime = (int) [ 20, MAX ], "
         "ptime = (int) [ 20, MAX ]"
         */
    )
    );

static GstStaticPadTemplate gst_rtp_amr_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/AMR, " "channels = (int) 1," "rate = (int) 8000")
    );

static gboolean gst_rtp_amr_depay_setcaps (GstBaseRTPDepayload * depayload,
    GstCaps * caps);
static GstBuffer *gst_rtp_amr_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);

GST_BOILERPLATE (GstRtpAMRDepay, gst_rtp_amr_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static void
gst_rtp_amr_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_amr_depay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_amr_depay_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_amrdepay_details);
}

static void
gst_rtp_amr_depay_class_init (GstRtpAMRDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstbasertpdepayload_class->process = gst_rtp_amr_depay_process;
  gstbasertpdepayload_class->set_caps = gst_rtp_amr_depay_setcaps;
}

static void
gst_rtp_amr_depay_init (GstRtpAMRDepay * rtpamrdepay,
    GstRtpAMRDepayClass * klass)
{
  GstBaseRTPDepayload *depayload;

  depayload = GST_BASE_RTP_DEPAYLOAD (rtpamrdepay);

  depayload->clock_rate = 8000;
  gst_pad_use_fixed_caps (GST_BASE_RTP_DEPAYLOAD_SRCPAD (depayload));
}

static gboolean
gst_rtp_amr_depay_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{
  GstStructure *structure;
  GstCaps *srccaps;
  GstRtpAMRDepay *rtpamrdepay;
  const gchar *params;
  const gchar *str;
  gint clock_rate;

  rtpamrdepay = GST_RTP_AMR_DEPAY (depayload);

  structure = gst_caps_get_structure (caps, 0);

  if (!(str = gst_structure_get_string (structure, "octet-align")))
    rtpamrdepay->octet_align = FALSE;
  else
    rtpamrdepay->octet_align = (atoi (str) == 1);

  if (!(str = gst_structure_get_string (structure, "crc")))
    rtpamrdepay->crc = FALSE;
  else
    rtpamrdepay->crc = (atoi (str) == 1);

  if (rtpamrdepay->crc) {
    /* crc mode implies octet aligned mode */
    rtpamrdepay->octet_align = TRUE;
  }

  if (!(str = gst_structure_get_string (structure, "robust-sorting")))
    rtpamrdepay->robust_sorting = FALSE;
  else
    rtpamrdepay->robust_sorting = (atoi (str) == 1);

  if (rtpamrdepay->robust_sorting) {
    /* robust_sorting mode implies octet aligned mode */
    rtpamrdepay->octet_align = TRUE;
  }

  if (!(str = gst_structure_get_string (structure, "interleaving")))
    rtpamrdepay->interleaving = FALSE;
  else
    rtpamrdepay->interleaving = (atoi (str) == 1);

  if (rtpamrdepay->interleaving) {
    /* interleaving mode implies octet aligned mode */
    rtpamrdepay->octet_align = TRUE;
  }

  if (!(params = gst_structure_get_string (structure, "encoding-params")))
    rtpamrdepay->channels = 1;
  else {
    rtpamrdepay->channels = atoi (params);
  }

  if (!gst_structure_get_int (structure, "clock-rate", &clock_rate))
    clock_rate = 8000;

  /* we require 1 channel, 8000 Hz, octet aligned, no CRC,
   * no robust sorting, no interleaving for now */
  if (rtpamrdepay->channels != 1)
    return FALSE;
  if (clock_rate != 8000)
    return FALSE;
  if (rtpamrdepay->octet_align != TRUE)
    return FALSE;
  if (rtpamrdepay->robust_sorting != FALSE)
    return FALSE;
  if (rtpamrdepay->interleaving != FALSE)
    return FALSE;

  srccaps = gst_caps_new_simple ("audio/AMR",
      "channels", G_TYPE_INT, rtpamrdepay->channels,
      "rate", G_TYPE_INT, clock_rate, NULL);
  gst_pad_set_caps (GST_BASE_RTP_DEPAYLOAD_SRCPAD (depayload), srccaps);
  gst_caps_unref (srccaps);

  rtpamrdepay->negotiated = TRUE;

  return TRUE;
}

/* -1 is invalid */
static gint frame_size[16] = {
  12, 13, 15, 17, 19, 20, 26, 31,
  5, -1, -1, -1, -1, -1, -1, 0
};

static GstBuffer *
gst_rtp_amr_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstRtpAMRDepay *rtpamrdepay;
  GstBuffer *outbuf = NULL;

  rtpamrdepay = GST_RTP_AMR_DEPAY (depayload);

  if (!rtpamrdepay->negotiated)
    goto not_negotiated;

  if (!gst_rtp_buffer_validate (buf)) {
    GST_ELEMENT_WARNING (rtpamrdepay, STREAM, DECODE,
        (NULL), ("AMR RTP packet did not validate"));
    goto bad_packet;
  }

  /* when we get here, 1 channel, 8000 Hz, octet aligned, no CRC, 
   * no robust sorting, no interleaving data is to be depayloaded */
  {
    gint payload_len;
    guint8 *payload, *p, *dp;
    guint32 timestamp;
    guint8 CMR;
    gint i, num_packets, num_nonempty_packets;
    gint amr_len;
    gint ILL, ILP;

    payload_len = gst_rtp_buffer_get_payload_len (buf);

    /* need at least 2 bytes for the header */
    if (payload_len < 2) {
      GST_ELEMENT_WARNING (rtpamrdepay, STREAM, DECODE,
          (NULL), ("AMR RTP payload too small (%d)", payload_len));
      goto bad_packet;
    }

    payload = gst_rtp_buffer_get_payload (buf);

    /* depay CMR. The CMR is used by the sender to request
     * a new encoding mode.
     *
     *  0 1 2 3 4 5 6 7 
     * +-+-+-+-+-+-+-+-+
     * | CMR   |R|R|R|R|
     * +-+-+-+-+-+-+-+-+
     */
    CMR = (payload[0] & 0xf0) >> 4;

    /* strip CMR header now, pack FT and the data for the decoder */
    payload_len -= 1;
    payload += 1;

    GST_DEBUG_OBJECT (rtpamrdepay, "payload len %d", payload_len);

    if (rtpamrdepay->interleaving) {
      ILL = (payload[0] & 0xf0) >> 4;
      ILP = (payload[0] & 0x0f);

      payload_len -= 1;
      payload += 1;

      if (ILP > ILL) {
        GST_ELEMENT_WARNING (rtpamrdepay, STREAM, DECODE,
            (NULL), ("AMR RTP wrong interleaving"));
        goto bad_packet;
      }
    }

    /* 
     *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 
     * +-+-+-+-+-+-+-+-+..
     * |F|  FT   |Q|P|P| more FT..
     * +-+-+-+-+-+-+-+-+..
     */
    /* count number of packets by counting the FTs. Also
     * count number of amr data bytes and number of non-empty
     * packets (this is also the number of CRCs if present). */
    amr_len = 0;
    num_nonempty_packets = 0;
    num_packets = 0;
    for (i = 0; i < payload_len; i++) {
      gint fr_size;
      guint8 FT;

      FT = (payload[i] & 0x78) >> 3;

      fr_size = frame_size[FT];
      GST_DEBUG_OBJECT (rtpamrdepay, "frame size %d", fr_size);
      if (fr_size == -1) {
        GST_ELEMENT_WARNING (rtpamrdepay, STREAM, DECODE,
            (NULL), ("AMR RTP frame size == -1"));
        goto bad_packet;
      }

      if (fr_size > 0) {
        amr_len += fr_size;
        num_nonempty_packets++;
      }
      num_packets++;

      if ((payload[i] & 0x80) == 0)
        break;
    }

    if (rtpamrdepay->crc) {
      /* data len + CRC len + header bytes should be smaller than payload_len */
      if (num_packets + num_nonempty_packets + amr_len > payload_len) {
        GST_ELEMENT_WARNING (rtpamrdepay, STREAM, DECODE,
            (NULL), ("AMR RTP wrong length 1"));
        goto bad_packet;
      }
    } else {
      /* data len + header bytes should be smaller than payload_len */
      if (num_packets + amr_len > payload_len) {
        GST_ELEMENT_WARNING (rtpamrdepay, STREAM, DECODE,
            (NULL), ("AMR RTP wrong length 2"));
        goto bad_packet;
      }
    }

    timestamp = gst_rtp_buffer_get_timestamp (buf);

    outbuf = gst_buffer_new_and_alloc (payload_len);
    GST_BUFFER_TIMESTAMP (outbuf) =
        gst_util_uint64_scale_int (timestamp, GST_SECOND,
        depayload->clock_rate);

    /* point to destination */
    p = GST_BUFFER_DATA (outbuf);
    /* point to first data packet */
    dp = payload + num_packets;
    if (rtpamrdepay->crc) {
      /* skip CRC if present */
      dp += num_nonempty_packets;
    }

    for (i = 0; i < num_packets; i++) {
      gint fr_size;

      /* copy FT, clear F bit */
      *p++ = payload[i] & 0x7f;

      fr_size = frame_size[(payload[i] & 0x78) >> 3];
      if (fr_size > 0) {
        /* copy data packet, FIXME, calc CRC here. */
        memcpy (p, dp, fr_size);

        p += fr_size;
        dp += fr_size;
      }
    }
    gst_buffer_set_caps (outbuf,
        GST_PAD_CAPS (GST_BASE_RTP_DEPAYLOAD_SRCPAD (depayload)));

    GST_DEBUG ("gst_rtp_amr_depay_chain: pushing buffer of size %d",
        GST_BUFFER_SIZE (outbuf));
  }

  return outbuf;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (rtpamrdepay, STREAM, NOT_IMPLEMENTED,
        (NULL), ("not negotiated"));
    return NULL;
  }
bad_packet:
  {
    /* no fatal error */
    return NULL;
  }
}

gboolean
gst_rtp_amr_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpamrdepay",
      GST_RANK_NONE, GST_TYPE_RTP_AMR_DEPAY);
}
