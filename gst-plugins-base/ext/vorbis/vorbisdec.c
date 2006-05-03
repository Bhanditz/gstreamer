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

/**
 * SECTION:element-vorbisdec
 * @short_description: a decoder that decodes Vorbis to raw audio
 * @see_also: vorbisenc, oggdemux
 *
 * <refsect2>
 * <para>
 * This element decodes a Vorbis stream to raw float audio.
 * <ulink url="http://www.vorbis.com/">Vorbis</ulink> is a royalty-free
 * audio codec maintained by the <ulink url="http://www.xiph.org/">Xiph.org
 * Foundation</ulink>.
 * </para>
 * <title>Example pipelines</title>
 * <para>
 * <programlisting>
 * gst-launch -v filesrc location=sine.ogg ! oggdemux ! vorbisdec ! audioconvert ! alsasink
 * </programlisting>
 * Decode an Ogg/Vorbis. To create an Ogg/Vorbis file refer to the documentation of vorbisenc.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2006-03-01 (0.10.4)
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "vorbisdec.h"
#include <string.h>
#include <gst/audio/audio.h>
#include <gst/tag/tag.h>
#include <gst/audio/multichannel.h>

GST_DEBUG_CATEGORY_EXTERN (vorbisdec_debug);
#define GST_CAT_DEFAULT vorbisdec_debug

static const GstElementDetails vorbis_dec_details =
GST_ELEMENT_DETAILS ("Vorbis audio decoder",
    "Codec/Decoder/Audio",
    "decode raw vorbis streams to float audio",
    "Benjamin Otte <in7y118@public.uni-hamburg.de>");

static GstStaticPadTemplate vorbis_dec_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "rate = (int) [ 8000, 50000 ], "
        "channels = (int) [ 1, 6 ], " "endianness = (int) BYTE_ORDER, "
/* no ifdef in macros, please
#ifdef GST_VORBIS_DEC_SEQUENTIAL
      "layout = \"sequential\", "
#endif
*/
        "width = (int) 32")
    );

static GstStaticPadTemplate vorbis_dec_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-vorbis")
    );

GST_BOILERPLATE (GstVorbisDec, gst_vorbis_dec, GstElement, GST_TYPE_ELEMENT);

static void vorbis_dec_finalize (GObject * object);
static gboolean vorbis_dec_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn vorbis_dec_chain (GstPad * pad, GstBuffer * buffer);
static GstStateChangeReturn vorbis_dec_change_state (GstElement * element,
    GstStateChange transition);

static gboolean vorbis_dec_src_event (GstPad * pad, GstEvent * event);
static gboolean vorbis_dec_src_query (GstPad * pad, GstQuery * query);
static gboolean vorbis_dec_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value);

static gboolean vorbis_dec_sink_query (GstPad * pad, GstQuery * query);

static void
gst_vorbis_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstPadTemplate *src_template, *sink_template;

  src_template = gst_static_pad_template_get (&vorbis_dec_src_factory);
  gst_element_class_add_pad_template (element_class, src_template);

  sink_template = gst_static_pad_template_get (&vorbis_dec_sink_factory);
  gst_element_class_add_pad_template (element_class, sink_template);

  gst_element_class_set_details (element_class, &vorbis_dec_details);
}

static void
gst_vorbis_dec_class_init (GstVorbisDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->finalize = vorbis_dec_finalize;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (vorbis_dec_change_state);
}

static const GstQueryType *
vorbis_get_query_types (GstPad * pad)
{
  static const GstQueryType vorbis_dec_src_query_types[] = {
    GST_QUERY_POSITION,
    0
  };

  return vorbis_dec_src_query_types;
}

static void
gst_vorbis_dec_init (GstVorbisDec * dec, GstVorbisDecClass * g_class)
{
  dec->sinkpad = gst_pad_new_from_static_template (&vorbis_dec_sink_factory,
      "sink");

  gst_pad_set_event_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (vorbis_dec_sink_event));
  gst_pad_set_chain_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (vorbis_dec_chain));
  gst_pad_set_query_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (vorbis_dec_sink_query));
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  dec->srcpad = gst_pad_new_from_static_template (&vorbis_dec_src_factory,
      "src");

  gst_pad_set_event_function (dec->srcpad,
      GST_DEBUG_FUNCPTR (vorbis_dec_src_event));
  gst_pad_set_query_type_function (dec->srcpad,
      GST_DEBUG_FUNCPTR (vorbis_get_query_types));
  gst_pad_set_query_function (dec->srcpad,
      GST_DEBUG_FUNCPTR (vorbis_dec_src_query));
  gst_pad_use_fixed_caps (dec->srcpad);
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  dec->queued = NULL;
}

static void
vorbis_dec_finalize (GObject * object)
{
  /* Release any possibly allocated libvorbis data.
   * _clear functions can safely be called multiple times
   */
  GstVorbisDec *vd = GST_VORBIS_DEC (object);

  vorbis_block_clear (&vd->vb);
  vorbis_dsp_clear (&vd->vd);
  vorbis_comment_clear (&vd->vc);
  vorbis_info_clear (&vd->vi);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
vorbis_dec_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstVorbisDec *dec;
  guint64 scale = 1;

  dec = GST_VORBIS_DEC (GST_PAD_PARENT (pad));

  if (dec->packetno < 1)
    return FALSE;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  if (dec->sinkpad == pad &&
      (src_format == GST_FORMAT_BYTES || *dest_format == GST_FORMAT_BYTES))
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          scale = sizeof (float) * dec->vi.channels;
        case GST_FORMAT_DEFAULT:
          *dest_value =
              scale * gst_util_uint64_scale_int (src_value, dec->vi.rate,
              GST_SECOND);
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
          *dest_value =
              gst_util_uint64_scale_int (src_value, GST_SECOND, dec->vi.rate);
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
          *dest_value = gst_util_uint64_scale_int (src_value, GST_SECOND,
              dec->vi.rate * sizeof (float) * dec->vi.channels);
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
vorbis_dec_src_query (GstPad * pad, GstQuery * query)
{
  gint64 granulepos;
  GstVorbisDec *dec;
  gboolean res = FALSE;

  dec = GST_VORBIS_DEC (GST_PAD_PARENT (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 value;

      granulepos = dec->granulepos;

      gst_query_parse_position (query, &format, NULL);

      /* and convert to the final format */
      if (!(res =
              vorbis_dec_convert (pad, GST_FORMAT_DEFAULT, granulepos, &format,
                  &value)))
        goto error;

      value = (value - dec->segment.start) + dec->segment.time;

      gst_query_set_position (query, format, value);

      GST_LOG_OBJECT (dec,
          "query %u: peer returned granulepos: %llu - we return %llu (format %u)",
          query, granulepos, value, format);

      break;
    }
    case GST_QUERY_DURATION:
    {
      /* query peer for total length */
      if (!gst_pad_is_linked (dec->sinkpad)) {
        GST_WARNING_OBJECT (dec, "sink pad %" GST_PTR_FORMAT " is not linked",
            dec->sinkpad);
        goto error;
      }
      if (!(res = gst_pad_query (GST_PAD_PEER (dec->sinkpad), query)))
        goto error;
      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res =
              vorbis_dec_convert (pad, src_fmt, src_val, &dest_fmt, &dest_val)))
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
  return res;

error:
  {
    GST_WARNING_OBJECT (dec, "error handling query");
    return res;
  }
}

static gboolean
vorbis_dec_sink_query (GstPad * pad, GstQuery * query)
{
  GstVorbisDec *dec;
  gboolean res;

  dec = GST_VORBIS_DEC (GST_PAD_PARENT (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res =
              vorbis_dec_convert (pad, src_fmt, src_val, &dest_fmt, &dest_val)))
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
error:
  return res;
}

static gboolean
vorbis_dec_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstVorbisDec *dec = GST_VORBIS_DEC (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      GstFormat format, tformat;
      gdouble rate;
      GstEvent *real_seek;
      GstSeekFlags flags;
      GstSeekType cur_type, stop_type;
      gint64 cur, stop;
      gint64 tcur, tstop;

      gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &cur,
          &stop_type, &stop);

      /* we have to ask our peer to seek to time here as we know
       * nothing about how to generate a granulepos from the src
       * formats or anything.
       *
       * First bring the requested format to time
       */
      tformat = GST_FORMAT_TIME;
      if (!(res = vorbis_dec_convert (pad, format, cur, &tformat, &tcur)))
        goto error;
      if (!(res = vorbis_dec_convert (pad, format, stop, &tformat, &tstop)))
        goto error;

      /* then seek with time on the peer */
      real_seek = gst_event_new_seek (rate, GST_FORMAT_TIME,
          flags, cur_type, tcur, stop_type, tstop);

      res = gst_pad_push_event (dec->sinkpad, real_seek);

      gst_event_unref (event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  return res;

error:
  gst_event_unref (event);
  return res;
}

static gboolean
vorbis_dec_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret = FALSE;
  GstVorbisDec *dec;

  dec = GST_VORBIS_DEC (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (dec, "handling event");
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      ret = gst_pad_push_event (dec->srcpad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate;
      gint64 start, stop, time;
      gboolean update;

      gst_event_parse_new_segment (event, &update, &rate, &format, &start,
          &stop, &time);

      /* we need time and a positive rate for now */
      if (format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      if (rate <= 0.0)
        goto newseg_wrong_rate;

      /* now configure the values */
      gst_segment_set_newsegment (&dec->segment, update,
          rate, format, start, stop, time);

      /* and forward */
      ret = gst_pad_push_event (dec->srcpad, event);
      break;
    }
    default:
      ret = gst_pad_push_event (dec->srcpad, event);
      break;
  }
done:
  gst_object_unref (dec);

  return ret;
  /* ERRORS */
newseg_wrong_format:
  {
    GST_DEBUG ("received non TIME newsegment");
    goto done;
  }
newseg_wrong_rate:
  {
    GST_DEBUG ("negative rates not supported yet");
    goto done;
  }

}

static GstFlowReturn
vorbis_handle_identification_packet (GstVorbisDec * vd)
{
  GstCaps *caps;
  const GstAudioChannelPosition *pos = NULL;

  caps = gst_caps_new_simple ("audio/x-raw-float",
      "rate", G_TYPE_INT, vd->vi.rate,
      "channels", G_TYPE_INT, vd->vi.channels,
      "endianness", G_TYPE_INT, G_BYTE_ORDER, "width", G_TYPE_INT, 32, NULL);

  switch (vd->vi.channels) {
    case 1:
    case 2:
      /* nothing */
      break;
    case 3:{
      static GstAudioChannelPosition pos3[] = {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT
      };
      pos = pos3;
      break;
    }
    case 4:{
      static GstAudioChannelPosition pos4[] = {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT
      };
      pos = pos4;
      break;
    }
    case 5:{
      static GstAudioChannelPosition pos5[] = {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT
      };
      pos = pos5;
      break;
    }
    case 6:{
      static GstAudioChannelPosition pos6[] = {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_LFE
      };
      pos = pos6;
      break;
    }
    default:
      goto channel_count_error;
  }
  if (pos) {
    gst_audio_set_channel_positions (gst_caps_get_structure (caps, 0), pos);
  }
  gst_pad_set_caps (vd->srcpad, caps);
  gst_caps_unref (caps);

  return GST_FLOW_OK;

  /* ERROR */
channel_count_error:
  {
    gst_caps_unref (caps);
    GST_ELEMENT_ERROR (vd, STREAM, NOT_IMPLEMENTED, (NULL),
        ("Unsupported channel count %d", vd->vi.channels));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
vorbis_handle_comment_packet (GstVorbisDec * vd, ogg_packet * packet)
{
  guint bitrate = 0;
  gchar *encoder = NULL;
  GstTagList *list;
  GstBuffer *buf;

  GST_DEBUG_OBJECT (vd, "parsing comment packet");

  buf = gst_buffer_new_and_alloc (packet->bytes);
  GST_BUFFER_DATA (buf) = packet->packet;

  list =
      gst_tag_list_from_vorbiscomment_buffer (buf, (guint8 *) "\003vorbis", 7,
      &encoder);

  gst_buffer_unref (buf);

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
      GST_TAG_ENCODER_VERSION, vd->vi.version,
      GST_TAG_AUDIO_CODEC, "Vorbis", NULL);
  if (vd->vi.bitrate_nominal > 0) {
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_NOMINAL_BITRATE, (guint) vd->vi.bitrate_nominal, NULL);
    bitrate = vd->vi.bitrate_nominal;
  }
  if (vd->vi.bitrate_upper > 0) {
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_MAXIMUM_BITRATE, (guint) vd->vi.bitrate_upper, NULL);
    if (!bitrate)
      bitrate = vd->vi.bitrate_upper;
  }
  if (vd->vi.bitrate_lower > 0) {
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_MINIMUM_BITRATE, (guint) vd->vi.bitrate_lower, NULL);
    if (!bitrate)
      bitrate = vd->vi.bitrate_lower;
  }
  if (bitrate) {
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_BITRATE, (guint) bitrate, NULL);
  }

  gst_element_found_tags_for_pad (GST_ELEMENT (vd), vd->srcpad, list);

  return GST_FLOW_OK;
}

static GstFlowReturn
vorbis_handle_type_packet (GstVorbisDec * vd)
{
  g_assert (vd->initialized == FALSE);

  vorbis_synthesis_init (&vd->vd, &vd->vi);
  vorbis_block_init (&vd->vd, &vd->vb);
  vd->initialized = TRUE;

  return GST_FLOW_OK;
}

static GstFlowReturn
vorbis_handle_header_packet (GstVorbisDec * vd, ogg_packet * packet)
{
  GstFlowReturn res;

  GST_DEBUG_OBJECT (vd, "parsing header packet");

  /* Packetno = 0 if the first byte is exactly 0x01 */
  packet->b_o_s = (packet->packet[0] == 0x1) ? 1 : 0;

  if (vorbis_synthesis_headerin (&vd->vi, &vd->vc, packet))
    goto header_read_error;

  switch (packet->packet[0]) {
    case 0x01:
      res = vorbis_handle_identification_packet (vd);
      break;
    case 0x03:
      res = vorbis_handle_comment_packet (vd, packet);
      break;
    case 0x05:
      res = vorbis_handle_type_packet (vd);
      break;
    default:
      /* ignore */
      g_warning ("unknown vorbis header packet found");
      res = GST_FLOW_OK;
      break;
  }
  return res;

  /* ERRORS */
header_read_error:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
        (NULL), ("couldn't read header packet"));
    return GST_FLOW_ERROR;
  }
}

/* These samples can be outside of the float -1.0 -- 1.0 range, this
 * is allowed, downstream elements are supposed to clip */
static void
copy_samples (float *out, float **in, guint samples, gint channels)
{
  gint i, j;

#ifdef GST_VORBIS_DEC_SEQUENTIAL
  for (i = 0; i < channels; i++) {
    memcpy (out, in[i], samples * sizeof (float));
    out += samples;
  }
#else
  for (j = 0; j < samples; j++) {
    for (i = 0; i < channels; i++) {
      *out++ = in[i][j];
    }
  }
#endif
}

static GstFlowReturn
vorbis_dec_push (GstVorbisDec * dec, GstBuffer * buf)
{
  GstFlowReturn result;
  gint64 outoffset = GST_BUFFER_OFFSET (buf);

  if (outoffset == -1) {
    dec->queued = g_list_append (dec->queued, buf);
    GST_DEBUG_OBJECT (dec, "queued buffer");
    result = GST_FLOW_OK;
  } else {
    if (dec->queued) {
      gint64 size;
      GList *walk;

      GST_DEBUG_OBJECT (dec, "first buffer with offset %lld", outoffset);

      size = g_list_length (dec->queued);
      for (walk = g_list_last (dec->queued); walk;
          walk = g_list_previous (walk)) {
        GstBuffer *buffer = GST_BUFFER (walk->data);

        outoffset -=
            GST_BUFFER_SIZE (buffer) / (sizeof (float) * dec->vi.channels);

        GST_BUFFER_OFFSET (buffer) = outoffset;
        GST_BUFFER_TIMESTAMP (buffer) =
            gst_util_uint64_scale_int (outoffset, GST_SECOND, dec->vi.rate);
        GST_DEBUG_OBJECT (dec, "patch buffer %" G_GUINT64_FORMAT
            " offset %" G_GUINT64_FORMAT, size, outoffset);
        size--;
      }
      for (walk = dec->queued; walk; walk = g_list_next (walk)) {
        GstBuffer *buffer = GST_BUFFER (walk->data);

        /* ignore the result */
        if (dec->discont) {
          GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
          dec->discont = FALSE;
        }
        gst_pad_push (dec->srcpad, buffer);
      }
      g_list_free (dec->queued);
      dec->queued = NULL;
    }
    if (dec->discont) {
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
      dec->discont = FALSE;
    }
    result = gst_pad_push (dec->srcpad, buf);
  }

  return result;
}

static GstFlowReturn
vorbis_handle_data_packet (GstVorbisDec * vd, ogg_packet * packet)
{
  float **pcm;
  guint sample_count;
  GstBuffer *out;
  GstFlowReturn result;
  gint size;

  if (!vd->initialized)
    goto not_initialized;

  /* normal data packet */
  if (vorbis_synthesis (&vd->vb, packet))
    goto could_not_read;

  if (vorbis_synthesis_blockin (&vd->vd, &vd->vb) < 0)
    goto not_accepted;

  /* assume all goes well here */
  result = GST_FLOW_OK;

  /* count samples ready for reading */
  if ((sample_count = vorbis_synthesis_pcmout (&vd->vd, NULL)) == 0)
    goto done;

  size = sample_count * vd->vi.channels * sizeof (float);

  /* alloc buffer for it */
  result =
      gst_pad_alloc_buffer_and_set_caps (vd->srcpad, GST_BUFFER_OFFSET_NONE,
      size, GST_PAD_CAPS (vd->srcpad), &out);
  if (result != GST_FLOW_OK)
    goto done;

  /* get samples ready for reading now, should be sample_count */
  if ((vorbis_synthesis_pcmout (&vd->vd, &pcm)) != sample_count)
    goto wrong_samples;

  /* copy samples in buffer */
  copy_samples ((float *) GST_BUFFER_DATA (out), pcm, sample_count,
      vd->vi.channels);

  GST_BUFFER_SIZE (out) = size;
  GST_BUFFER_OFFSET (out) = vd->granulepos;
  if (vd->granulepos != -1) {
    GST_BUFFER_OFFSET_END (out) = vd->granulepos + sample_count;
    GST_BUFFER_TIMESTAMP (out) =
        gst_util_uint64_scale_int (vd->granulepos, GST_SECOND, vd->vi.rate);
  } else {
    GST_BUFFER_TIMESTAMP (out) = -1;
  }
  /* this should not overflow */
  GST_BUFFER_DURATION (out) = sample_count * GST_SECOND / vd->vi.rate;

  if (vd->cur_timestamp != GST_CLOCK_TIME_NONE) {
    GST_BUFFER_TIMESTAMP (out) = vd->cur_timestamp;
    GST_DEBUG ("cur_timestamp: %" GST_TIME_FORMAT " + %" GST_TIME_FORMAT " = % "
        GST_TIME_FORMAT, GST_TIME_ARGS (vd->cur_timestamp),
        GST_TIME_ARGS (GST_BUFFER_DURATION (out)),
        GST_TIME_ARGS (vd->cur_timestamp + GST_BUFFER_DURATION (out)));
    vd->cur_timestamp += GST_BUFFER_DURATION (out);
    GST_BUFFER_OFFSET (out) = GST_CLOCK_TIME_TO_FRAMES (vd->cur_timestamp,
        vd->vi.rate);
    GST_BUFFER_OFFSET_END (out) = GST_BUFFER_OFFSET (out) + sample_count;
  }

  if (vd->granulepos != -1)
    vd->granulepos += sample_count;

  result = vorbis_dec_push (vd, out);

done:
  vorbis_synthesis_read (&vd->vd, sample_count);

  /* granulepos is the last sample in the packet */
  if (packet->granulepos != -1)
    vd->granulepos = packet->granulepos;

  return result;

  /* ERRORS */
not_initialized:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
        (NULL), ("no header sent yet (packet no is %d)", packet->packetno));
    return GST_FLOW_ERROR;
  }
could_not_read:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
        (NULL), ("couldn't read data packet"));
    return GST_FLOW_ERROR;
  }
not_accepted:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
        (NULL), ("vorbis decoder did not accept data packet"));
    return GST_FLOW_ERROR;
  }
wrong_samples:
  {
    gst_buffer_unref (out);
    GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
        (NULL), ("vorbis decoder reported wrong number of samples"));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
vorbis_dec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstVorbisDec *vd;
  ogg_packet packet;
  GstFlowReturn result = GST_FLOW_OK;

  vd = GST_VORBIS_DEC (gst_pad_get_parent (pad));

  /* resync on DISCONT */
  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT))) {
    GST_DEBUG_OBJECT (vd, "received DISCONT buffer");
    vd->granulepos = -1;
    vd->cur_timestamp = GST_CLOCK_TIME_NONE;
    vd->prev_timestamp = GST_CLOCK_TIME_NONE;
#ifdef HAVE_VORBIS_SYNTHESIS_RESTART
    vorbis_synthesis_restart (&vd->vd);
#endif
    vd->discont = TRUE;
  }

  /* only ogg has granulepos, demuxers of other container formats 
   * might provide us with timestamps instead (e.g. matroskademux) */
  if (GST_BUFFER_OFFSET_END (buffer) == GST_BUFFER_OFFSET_NONE &&
      GST_BUFFER_TIMESTAMP (buffer) != GST_CLOCK_TIME_NONE) {
    /* we might get multiple consecutive buffers with the same timestamp */
    if (GST_BUFFER_TIMESTAMP (buffer) != vd->prev_timestamp) {
      vd->cur_timestamp = GST_BUFFER_TIMESTAMP (buffer);
      vd->prev_timestamp = GST_BUFFER_TIMESTAMP (buffer);
    }
  } else {
    vd->cur_timestamp = GST_CLOCK_TIME_NONE;
    vd->prev_timestamp = GST_CLOCK_TIME_NONE;
  }

  /* make ogg_packet out of the buffer */
  packet.packet = GST_BUFFER_DATA (buffer);
  packet.bytes = GST_BUFFER_SIZE (buffer);
  packet.granulepos = GST_BUFFER_OFFSET_END (buffer);
  packet.packetno = vd->packetno++;
  /*
   * FIXME. Is there anyway to know that this is the last packet and
   * set e_o_s??
   * Yes there is, keep one packet at all times and only push out when
   * you receive a new one.  Implement this.
   */
  packet.e_o_s = 0;

  if (packet.bytes < 1)
    goto wrong_size;

  GST_DEBUG_OBJECT (vd, "vorbis granule: %" G_GINT64_FORMAT,
      (gint64) packet.granulepos);

  /* switch depending on packet type */
  if (packet.packet[0] & 1) {
    if (vd->initialized) {
      GST_WARNING_OBJECT (vd, "Ignoring header");
      goto done;
    }
    result = vorbis_handle_header_packet (vd, &packet);
  } else {
    result = vorbis_handle_data_packet (vd, &packet);
  }

  GST_DEBUG_OBJECT (vd, "offset end: %" G_GINT64_FORMAT,
      (gint64) GST_BUFFER_OFFSET_END (buffer));

done:
  gst_buffer_unref (buffer);
  gst_object_unref (vd);

  return result;

  /* ERRORS */
wrong_size:
  {
    GST_WARNING_OBJECT (vd, "received empty packet");
    result = GST_FLOW_OK;
    vd->discont = TRUE;
    goto done;
  }
}

static GstStateChangeReturn
vorbis_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstVorbisDec *vd = GST_VORBIS_DEC (element);
  GstStateChangeReturn res;


  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      vorbis_info_init (&vd->vi);
      vorbis_comment_init (&vd->vc);
      vd->initialized = FALSE;
      vd->cur_timestamp = GST_CLOCK_TIME_NONE;
      vd->prev_timestamp = GST_CLOCK_TIME_NONE;
      vd->granulepos = -1;
      vd->packetno = 0;
      vd->discont = TRUE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  res = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_DEBUG_OBJECT (vd, "PAUSED -> READY, clearing vorbis structures");
      vorbis_block_clear (&vd->vb);
      vorbis_dsp_clear (&vd->vd);
      vorbis_comment_clear (&vd->vc);
      vorbis_info_clear (&vd->vi);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return res;
}
