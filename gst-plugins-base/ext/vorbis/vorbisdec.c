/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <in7y118@public.uni-hamburg.de>
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

#include "vorbisdec.h"
#include <string.h>
#include <gst/tag/tag.h>

GST_DEBUG_CATEGORY_EXTERN (vorbisdec_debug);
#define GST_CAT_DEFAULT vorbisdec_debug

static GstElementDetails vorbis_dec_details = {
  "VorbisDec",
  "Codec/Decoder/Audio",
  "decode raw vorbis streams to float audio",
  "Benjamin Otte <in7y118@public.uni-hamburg.de>",
};

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static GstStaticPadTemplate vorbis_dec_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "rate = (int) [ 8000, 50000 ], "
        "channels = (int) [ 1, 2 ], " "endianness = (int) BYTE_ORDER, "
/* no ifdef in macros, please
#ifdef GST_VORBIS_DEC_SEQUENTIAL
      "layout = \"sequential\", "
#endif
*/
        "width = (int) 32, " "buffer-frames = (int) 0")
    );

static GstStaticPadTemplate vorbis_dec_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-vorbis")
    );

GST_BOILERPLATE (GstVorbisDec, gst_vorbis_dec, GstElement, GST_TYPE_ELEMENT);

static void vorbis_dec_chain (GstPad * pad, GstData * data);
static GstElementStateReturn vorbis_dec_change_state (GstElement * element);
static const GstFormat *vorbis_dec_get_formats (GstPad * pad);

static gboolean vorbis_dec_src_event (GstPad * pad, GstEvent * event);
static gboolean vorbis_dec_src_query (GstPad * pad,
    GstQueryType query, GstFormat * format, gint64 * value);
static gboolean vorbis_dec_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value);


static void
gst_vorbis_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&vorbis_dec_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&vorbis_dec_sink_factory));
  gst_element_class_set_details (element_class, &vorbis_dec_details);
}

static void
gst_vorbis_dec_class_init (GstVorbisDecClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gstelement_class->change_state = vorbis_dec_change_state;
}

static const GstFormat *
vorbis_dec_get_formats (GstPad * pad)
{
  static GstFormat src_formats[] = {
    GST_FORMAT_BYTES,
    GST_FORMAT_DEFAULT,         /* samples in the audio case */
    GST_FORMAT_TIME,
    0
  };
  static GstFormat sink_formats[] = {
    /*GST_FORMAT_BYTES, */
    GST_FORMAT_TIME,
    GST_FORMAT_DEFAULT,         /* granulepos or samples */
    0
  };

  return (GST_PAD_IS_SRC (pad) ? src_formats : sink_formats);
}

static const GstEventMask *
vorbis_get_event_masks (GstPad * pad)
{
  static const GstEventMask vorbis_dec_src_event_masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH},
    {0,}
  };

  return vorbis_dec_src_event_masks;
}

static const GstQueryType *
vorbis_get_query_types (GstPad * pad)
{
  static const GstQueryType vorbis_dec_src_query_types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };

  return vorbis_dec_src_query_types;
}

static void
gst_vorbis_dec_init (GstVorbisDec * dec)
{
  dec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&vorbis_dec_sink_factory), "sink");
  gst_pad_set_chain_function (dec->sinkpad, vorbis_dec_chain);
  gst_pad_set_formats_function (dec->sinkpad, vorbis_dec_get_formats);
  gst_pad_set_convert_function (dec->sinkpad, vorbis_dec_convert);
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  dec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&vorbis_dec_src_factory), "src");
  gst_pad_use_explicit_caps (dec->srcpad);
  gst_pad_set_event_mask_function (dec->srcpad, vorbis_get_event_masks);
  gst_pad_set_event_function (dec->srcpad, vorbis_dec_src_event);
  gst_pad_set_query_type_function (dec->srcpad, vorbis_get_query_types);
  gst_pad_set_query_function (dec->srcpad, vorbis_dec_src_query);
  gst_pad_set_formats_function (dec->srcpad, vorbis_dec_get_formats);
  gst_pad_set_convert_function (dec->srcpad, vorbis_dec_convert);
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  GST_FLAG_SET (dec, GST_ELEMENT_EVENT_AWARE);
}

static gboolean
vorbis_dec_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstVorbisDec *dec;
  guint64 scale = 1;

  dec = GST_VORBIS_DEC (gst_pad_get_parent (pad));

  if (dec->packetno < 1)
    return FALSE;

  if (dec->sinkpad == pad &&
      (src_format == GST_FORMAT_BYTES || *dest_format == GST_FORMAT_BYTES))
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          scale = sizeof (float) * dec->vi.channels;
        case GST_FORMAT_DEFAULT:
          *dest_value = scale * (src_value * dec->vi.rate / GST_SECOND);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * sizeof (float) * dec->vi.channels;
          break;
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND / dec->vi.rate;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value / (sizeof (float) * dec->vi.channels);
          break;
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND /
              (dec->vi.rate * sizeof (float) * dec->vi.channels);
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }

  return res;
}

static gboolean
vorbis_dec_src_query (GstPad * pad, GstQueryType query, GstFormat * format,
    gint64 * value)
{
  gint64 granulepos = 0;
  GstVorbisDec *dec = GST_VORBIS_DEC (gst_pad_get_parent (pad));
  GstFormat my_format = GST_FORMAT_DEFAULT;

  if (query == GST_QUERY_POSITION) {
    granulepos = dec->granulepos;
  } else {
    /* query peer in default format */
    if (!gst_pad_query (GST_PAD_PEER (dec->sinkpad), query, &my_format,
            &granulepos))
      return FALSE;
  }

  /* and convert to the final format */
  if (!gst_pad_convert (pad, GST_FORMAT_DEFAULT, granulepos, format, value))
    return FALSE;

  GST_LOG_OBJECT (dec,
      "query %u: peer returned granulepos: %llu - we return %llu (format %u)\n",
      query, granulepos, *value, *format);
  return TRUE;
}

static gboolean
vorbis_dec_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstVorbisDec *dec = GST_VORBIS_DEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      guint64 value;
      GstFormat my_format = GST_FORMAT_DEFAULT;

      /* convert to granulepos */
      res = vorbis_dec_convert (pad, GST_EVENT_SEEK_FORMAT (event),
          GST_EVENT_SEEK_OFFSET (event), &my_format, &value);
      if (res) {
        GstEvent *real_seek = gst_event_new_seek (
            (GST_EVENT_SEEK_TYPE (event) & ~GST_SEEK_FORMAT_MASK) |
            GST_FORMAT_DEFAULT,
            value);

        res = gst_pad_send_event (GST_PAD_PEER (dec->sinkpad), real_seek);
      }
      gst_event_unref (event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  return res;
}

static void
vorbis_dec_event (GstVorbisDec * dec, GstEvent * event)
{
  guint64 value, time, bytes;

  GST_LOG_OBJECT (dec, "handling event");
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_DISCONTINUOUS:
      if (gst_event_discont_get_value (event, GST_FORMAT_DEFAULT,
              (gint64 *) & value)) {
        dec->granulepos = value;
        GST_DEBUG_OBJECT (dec,
            "setting granuleposition to %" G_GUINT64_FORMAT " after discont",
            value);
      } else {
        GST_WARNING_OBJECT (dec,
            "discont event didn't include offset, we might set it wrong now");
      }
      if (dec->packetno < 3) {
        if (dec->granulepos != 0)
          GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL),
              ("can't handle discont before parsing first 3 packets"));
        dec->packetno = 0;
        gst_pad_push (dec->srcpad, GST_DATA (gst_event_new_discontinuous (FALSE,
                    GST_FORMAT_TIME, (guint64) 0, GST_FORMAT_DEFAULT,
                    (guint64) 0, GST_FORMAT_BYTES, (guint64) 0, 0)));
      } else {
        GstFormat time_format, default_format, bytes_format;

        time_format = GST_FORMAT_TIME;
        default_format = GST_FORMAT_DEFAULT;
        bytes_format = GST_FORMAT_BYTES;

        dec->packetno = 3;
        /* if one of them works, all of them work */
        if (vorbis_dec_convert (dec->srcpad, GST_FORMAT_DEFAULT,
                dec->granulepos, &time_format, &time)
            && vorbis_dec_convert (dec->srcpad, GST_FORMAT_DEFAULT,
                dec->granulepos, &bytes_format, &bytes)) {
          gst_pad_push (dec->srcpad,
              GST_DATA (gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME,
                      time, GST_FORMAT_DEFAULT, dec->granulepos,
                      GST_FORMAT_BYTES, bytes, 0)));
        } else {
          GST_ERROR_OBJECT (dec,
              "failed to parse data for DISCONT event, not sending any");
        }
#ifdef HAVE_VORBIS_SYNTHESIS_RESTART
        vorbis_synthesis_restart (&dec->vd);
#endif
      }
      gst_data_unref (GST_DATA (event));
      break;
    default:
      gst_pad_event_default (dec->sinkpad, event);
      break;
  }
}

static void
vorbis_dec_chain (GstPad * pad, GstData * data)
{
  GstBuffer *buf;
  GstVorbisDec *vd;
  ogg_packet packet;            /* lol */

  vd = GST_VORBIS_DEC (gst_pad_get_parent (pad));
  if (GST_IS_EVENT (data)) {
    vorbis_dec_event (vd, GST_EVENT (data));
    return;
  }

  buf = GST_BUFFER (data);
  /* make ogg_packet out of the buffer */
  packet.packet = GST_BUFFER_DATA (buf);
  packet.bytes = GST_BUFFER_SIZE (buf);
  packet.granulepos = GST_BUFFER_OFFSET_END (buf);
  packet.packetno = vd->packetno++;

  if (GST_BUFFER_OFFSET_END_IS_VALID (buf))
    vd->granulepos = GST_BUFFER_OFFSET_END (buf);;

  /* 
   * FIXME. Is there anyway to know that this is the last packet and
   * set e_o_s??
   */
  packet.e_o_s = 0;

  /* switch depending on packet type */
  if (packet.packet[0] & 1) {
    /* header packet */
    if (packet.packet[0] / 2 != packet.packetno) {
      /* FIXME: just skip? */
      GST_WARNING_OBJECT (GST_ELEMENT (vd),
          "unexpected packet type %d, expected %d",
          (gint) packet.packet[0], (gint) packet.packetno);
      gst_data_unref (data);
      return;
    }
    /* Packetno = 0 if the first byte is exactly 0x01 */
    packet.b_o_s = (packet.packet[0] == 0x1) ? 1 : 0;
    if (vorbis_synthesis_headerin (&vd->vi, &vd->vc, &packet)) {
      GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
          (NULL), ("couldn't read header packet"));
      gst_data_unref (data);
      return;
    }
    if (packet.packetno == 1) {
      gchar *encoder = NULL;
      GstTagList *list =
          gst_tag_list_from_vorbiscomment_buffer (buf, "\003vorbis", 7,
          &encoder);

      if (!list) {
        GST_ERROR_OBJECT (vd, "couldn't decode comments");
        list = gst_tag_list_new ();
      }
      if (encoder) {
        gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
            GST_TAG_ENCODER, encoder, NULL);
        g_free (encoder);
      }
      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
          GST_TAG_ENCODER_VERSION, vd->vi.version, NULL);
      if (vd->vi.bitrate_upper > 0)
        gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
            GST_TAG_MAXIMUM_BITRATE, (guint) vd->vi.bitrate_upper, NULL);
      if (vd->vi.bitrate_nominal > 0)
        gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
            GST_TAG_NOMINAL_BITRATE, (guint) vd->vi.bitrate_nominal, NULL);
      if (vd->vi.bitrate_lower > 0)
        gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
            GST_TAG_MINIMUM_BITRATE, (guint) vd->vi.bitrate_lower, NULL);
      gst_element_found_tags_for_pad (GST_ELEMENT (vd), vd->srcpad, 0, list);
    } else if (packet.packetno == 2) {
      GstCaps *caps;

      /* done */
      vorbis_synthesis_init (&vd->vd, &vd->vi);
      vorbis_block_init (&vd->vd, &vd->vb);
      caps = gst_caps_new_simple ("audio/x-raw-float",
          "rate", G_TYPE_INT, vd->vi.rate,
          "channels", G_TYPE_INT, vd->vi.channels,
          "endianness", G_TYPE_INT, G_BYTE_ORDER,
          "width", G_TYPE_INT, 32, "buffer-frames", G_TYPE_INT, 0, NULL);
      if (!gst_pad_set_explicit_caps (vd->srcpad, caps)) {
        gst_caps_free (caps);
        return;
      }
      gst_caps_free (caps);
    }
  } else {
    float **pcm;
    guint sample_count;

    if (packet.packetno < 3) {
      GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
          (NULL), ("no header sent yet (packet no is %d)", packet.packetno));
      gst_data_unref (data);
      return;
    }
    /* normal data packet */
    if (vorbis_synthesis (&vd->vb, &packet)) {
      GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
          (NULL), ("couldn't read data packet"));
      gst_data_unref (data);
      return;
    }
    if (vorbis_synthesis_blockin (&vd->vd, &vd->vb) < 0) {
      GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
          (NULL), ("vorbis decoder did not accept data packet"));
      gst_data_unref (data);
      return;
    }
    sample_count = vorbis_synthesis_pcmout (&vd->vd, &pcm);
    if (sample_count > 0) {
      int i, j;
      GstBuffer *out = gst_pad_alloc_buffer (vd->srcpad, GST_BUFFER_OFFSET_NONE,
          sample_count * vd->vi.channels * sizeof (float));
      float *out_data = (float *) GST_BUFFER_DATA (out);

#ifdef GST_VORBIS_DEC_SEQUENTIAL
      for (i = 0; i < vd->vi.channels; i++) {
        memcpy (out_data, pcm[i], sample_count * sizeof (float));
        out_data += sample_count;
      }
#else
      for (j = 0; j < sample_count; j++) {
        for (i = 0; i < vd->vi.channels; i++) {
          *out_data = pcm[i][j];
          out_data++;
        }
      }
#endif
      GST_BUFFER_OFFSET (out) = vd->granulepos;
      GST_BUFFER_OFFSET_END (out) = vd->granulepos + sample_count;
      GST_BUFFER_TIMESTAMP (out) = vd->granulepos * GST_SECOND / vd->vi.rate;
      GST_BUFFER_DURATION (out) = sample_count * GST_SECOND / vd->vi.rate;
      gst_pad_push (vd->srcpad, GST_DATA (out));
      vorbis_synthesis_read (&vd->vd, sample_count);
      vd->granulepos += sample_count;
    }
  }
  gst_data_unref (data);
}

static GstElementStateReturn
vorbis_dec_change_state (GstElement * element)
{
  GstVorbisDec *vd = GST_VORBIS_DEC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      vorbis_info_init (&vd->vi);
      vorbis_comment_init (&vd->vc);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      vorbis_block_clear (&vd->vb);
      vorbis_dsp_clear (&vd->vd);
      vorbis_comment_clear (&vd->vc);
      vorbis_info_clear (&vd->vi);
      vd->packetno = 0;
      vd->granulepos = 0;
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return parent_class->change_state (element);
}
