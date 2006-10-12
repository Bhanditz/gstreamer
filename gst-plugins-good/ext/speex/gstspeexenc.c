/* GStreamer Speex Encoder
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
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <speex/speex.h>
#include <speex/speex_stereo.h>

#include <gst/gsttagsetter.h>
#include <gst/tag/tag.h>
#include "gstspeexenc.h"

GST_DEBUG_CATEGORY_STATIC (speexenc_debug);
#define GST_CAT_DEFAULT speexenc_debug

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 6000, 48000 ], "
        "channels = (int) [ 1, 2 ], "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, " "width = (int) 16, " "depth = (int) 16")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-speex")
    );

static const GstElementDetails speexenc_details =
GST_ELEMENT_DETAILS ("Speex audio encoder",
    "Codec/Encoder/Audio",
    "Encodes audio in Speex format",
    "Wim Taymans <wim@fluendo.com>");

#define DEFAULT_QUALITY         8.0
#define DEFAULT_BITRATE         0
#define DEFAULT_VBR             FALSE
#define DEFAULT_ABR             0
#define DEFAULT_VAD             FALSE
#define DEFAULT_DTX             FALSE
#define DEFAULT_COMPLEXITY      3
#define DEFAULT_NFRAMES         1

enum
{
  ARG_0,
  ARG_QUALITY,
  ARG_BITRATE,
  ARG_VBR,
  ARG_ABR,
  ARG_VAD,
  ARG_DTX,
  ARG_COMPLEXITY,
  ARG_NFRAMES,
  ARG_LAST_MESSAGE
};

#if 0
static const GstFormat *
gst_speexenc_get_formats (GstPad * pad)
{
  static const GstFormat src_formats[] = {
    GST_FORMAT_BYTES,
    GST_FORMAT_TIME,
    0
  };
  static const GstFormat sink_formats[] = {
    GST_FORMAT_BYTES,
    GST_FORMAT_DEFAULT,
    GST_FORMAT_TIME,
    0
  };

  return (GST_PAD_IS_SRC (pad) ? src_formats : sink_formats);
}
#endif

static void gst_speexenc_base_init (gpointer g_class);
static void gst_speexenc_class_init (GstSpeexEncClass * klass);
static void gst_speexenc_init (GstSpeexEnc * speexenc);
static void gst_speexenc_finalize (GObject * object);

static gboolean gst_speexenc_sinkevent (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_speexenc_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_speexenc_setup (GstSpeexEnc * speexenc);

static void gst_speexenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_speexenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_speexenc_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

GType
gst_speexenc_get_type (void)
{
  static GType speexenc_type = 0;

  if (!speexenc_type) {
    static const GTypeInfo speexenc_info = {
      sizeof (GstSpeexEncClass),
      gst_speexenc_base_init,
      NULL,
      (GClassInitFunc) gst_speexenc_class_init,
      NULL,
      NULL,
      sizeof (GstSpeexEnc),
      0,
      (GInstanceInitFunc) gst_speexenc_init,
    };
    static const GInterfaceInfo tag_setter_info = {
      NULL,
      NULL,
      NULL
    };

    speexenc_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstSpeexEnc", &speexenc_info,
        0);

    g_type_add_interface_static (speexenc_type, GST_TYPE_TAG_SETTER,
        &tag_setter_info);

    GST_DEBUG_CATEGORY_INIT (speexenc_debug, "speexenc", 0, "Speex encoder");
  }
  return speexenc_type;
}

static void
gst_speexenc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details (element_class, &speexenc_details);
}

static void
gst_speexenc_class_init (GstSpeexEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_speexenc_set_property;
  gobject_class->get_property = gst_speexenc_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_QUALITY,
      g_param_spec_float ("quality", "Quality", "Encoding quality",
          0.0, 10.0, DEFAULT_QUALITY, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BITRATE,
      g_param_spec_int ("bitrate", "Encoding Bit-rate",
          "Specify an encoding bit-rate (in bps). (0 = automatic)",
          0, G_MAXINT, DEFAULT_BITRATE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VBR,
      g_param_spec_boolean ("vbr", "VBR",
          "Enable variable bit-rate", DEFAULT_VBR, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ABR,
      g_param_spec_int ("abr", "ABR",
          "Enable average bit-rate (0 = disabled)",
          0, G_MAXINT, DEFAULT_ABR, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VAD,
      g_param_spec_boolean ("vad", "VAD",
          "Enable voice activity detection", DEFAULT_VAD, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DTX,
      g_param_spec_boolean ("dtx", "DTX",
          "Enable discontinuous transmission", DEFAULT_DTX, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_COMPLEXITY,
      g_param_spec_int ("complexity", "Complexity",
          "Set encoding complexity",
          0, G_MAXINT, DEFAULT_COMPLEXITY, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_NFRAMES,
      g_param_spec_int ("nframes", "NFrames",
          "Number of frames per buffer",
          0, G_MAXINT, DEFAULT_NFRAMES, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LAST_MESSAGE,
      g_param_spec_string ("last-message", "last-message",
          "The last status message", NULL, G_PARAM_READABLE));

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_speexenc_finalize);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_speexenc_change_state);
}

static void
gst_speexenc_finalize (GObject * object)
{
  GstSpeexEnc *speexenc;

  speexenc = GST_SPEEXENC (object);

  g_free (speexenc->last_message);
  g_object_unref (speexenc->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_speexenc_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstSpeexEnc *speexenc;
  GstStructure *structure;

  speexenc = GST_SPEEXENC (gst_pad_get_parent (pad));
  speexenc->setup = FALSE;

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "channels", &speexenc->channels);
  gst_structure_get_int (structure, "rate", &speexenc->rate);

  gst_speexenc_setup (speexenc);

  gst_object_unref (speexenc);

  return speexenc->setup;
}

static gboolean
gst_speexenc_convert_src (GstPad * pad, GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstSpeexEnc *speexenc;
  gint64 avg;

  speexenc = GST_SPEEXENC (GST_PAD_PARENT (pad));

  if (speexenc->samples_in == 0 ||
      speexenc->bytes_out == 0 || speexenc->rate == 0)
    return FALSE;

  avg = (speexenc->bytes_out * speexenc->rate) / (speexenc->samples_in);

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND / avg;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * avg / GST_SECOND;
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
gst_speexenc_convert_sink (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  guint scale = 1;
  gint bytes_per_sample;
  GstSpeexEnc *speexenc;

  speexenc = GST_SPEEXENC (GST_PAD_PARENT (pad));

  bytes_per_sample = speexenc->channels * 2;

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          if (bytes_per_sample == 0)
            return FALSE;
          *dest_value = src_value / bytes_per_sample;
          break;
        case GST_FORMAT_TIME:
        {
          gint byterate = bytes_per_sample * speexenc->rate;

          if (byterate == 0)
            return FALSE;
          *dest_value = src_value * GST_SECOND / byterate;
          break;
        }
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * bytes_per_sample;
          break;
        case GST_FORMAT_TIME:
          if (speexenc->rate == 0)
            return FALSE;
          *dest_value = src_value * GST_SECOND / speexenc->rate;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          scale = bytes_per_sample;
          /* fallthrough */
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value * scale * speexenc->rate / GST_SECOND;
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

static const GstQueryType *
gst_speexenc_get_query_types (GstPad * pad)
{
  static const GstQueryType gst_speexenc_src_query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    0
  };

  return gst_speexenc_src_query_types;
}

static gboolean
gst_speexenc_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstSpeexEnc *speexenc;
  GstPad *peerpad;

  speexenc = GST_SPEEXENC (gst_pad_get_parent (pad));
  peerpad = gst_pad_get_peer (GST_PAD (speexenc->sinkpad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat fmt, req_fmt;
      gint64 pos, val;

      gst_query_parse_position (query, &req_fmt, NULL);
      if ((res = gst_pad_query_position (peerpad, &req_fmt, &val))) {
        gst_query_set_position (query, req_fmt, val);
        break;
      }

      fmt = GST_FORMAT_TIME;
      if (!(res = gst_pad_query_position (peerpad, &fmt, &pos)))
        break;

      if ((res = gst_pad_query_convert (peerpad, fmt, pos, &req_fmt, &val)))
        gst_query_set_position (query, req_fmt, val);

      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat fmt, req_fmt;
      gint64 dur, val;

      gst_query_parse_duration (query, &req_fmt, NULL);
      if ((res = gst_pad_query_duration (peerpad, &req_fmt, &val))) {
        gst_query_set_duration (query, req_fmt, val);
        break;
      }

      fmt = GST_FORMAT_TIME;
      if (!(res = gst_pad_query_duration (peerpad, &fmt, &dur)))
        break;

      if ((res = gst_pad_query_convert (peerpad, fmt, dur, &req_fmt, &val))) {
        gst_query_set_duration (query, req_fmt, val);
      }
      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res = gst_speexenc_convert_src (pad, src_fmt, src_val, &dest_fmt,
                  &dest_val)))
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

error:
  gst_object_unref (peerpad);
  gst_object_unref (speexenc);
  return res;
}

static gboolean
gst_speexenc_sink_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstSpeexEnc *speexenc;

  speexenc = GST_SPEEXENC (GST_PAD_PARENT (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res =
              gst_speexenc_convert_sink (pad, src_fmt, src_val, &dest_fmt,
                  &dest_val)))
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

static void
gst_speexenc_init (GstSpeexEnc * speexenc)
{
  speexenc->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_element_add_pad (GST_ELEMENT (speexenc), speexenc->sinkpad);
  gst_pad_set_event_function (speexenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_speexenc_sinkevent));
  gst_pad_set_chain_function (speexenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_speexenc_chain));
  gst_pad_set_setcaps_function (speexenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_speexenc_sink_setcaps));
  gst_pad_set_query_function (speexenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_speexenc_sink_query));

  speexenc->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_query_function (speexenc->srcpad,
      GST_DEBUG_FUNCPTR (gst_speexenc_src_query));
  gst_pad_set_query_type_function (speexenc->srcpad,
      GST_DEBUG_FUNCPTR (gst_speexenc_get_query_types));
  gst_element_add_pad (GST_ELEMENT (speexenc), speexenc->srcpad);

  speexenc->channels = -1;
  speexenc->rate = -1;

  speexenc->quality = DEFAULT_QUALITY;
  speexenc->bitrate = DEFAULT_BITRATE;
  speexenc->vbr = DEFAULT_VBR;
  speexenc->abr = DEFAULT_ABR;
  speexenc->vad = DEFAULT_VAD;
  speexenc->dtx = DEFAULT_DTX;
  speexenc->complexity = DEFAULT_COMPLEXITY;
  speexenc->nframes = DEFAULT_NFRAMES;

  speexenc->setup = FALSE;
  speexenc->header_sent = FALSE;

  speexenc->adapter = gst_adapter_new ();
}

static GstBuffer *
gst_speexenc_create_metadata_buffer (GstSpeexEnc * enc)
{
  const GstTagList *user_tags;
  GstTagList *merged_tags;
  GstBuffer *comments = NULL;

  user_tags = gst_tag_setter_get_tag_list (GST_TAG_SETTER (enc));

  GST_DEBUG_OBJECT (enc, "upstream tags = %" GST_PTR_FORMAT, enc->tags);
  GST_DEBUG_OBJECT (enc, "user-set tags = %" GST_PTR_FORMAT, user_tags);

  /* gst_tag_list_merge() will handle NULL for either or both lists fine */
  merged_tags = gst_tag_list_merge (user_tags, enc->tags,
      gst_tag_setter_get_tag_merge_mode (GST_TAG_SETTER (enc)));

  if (merged_tags == NULL)
    merged_tags = gst_tag_list_new ();

  GST_DEBUG_OBJECT (enc, "merged   tags = %" GST_PTR_FORMAT, merged_tags);
  comments = gst_tag_list_to_vorbiscomment_buffer (merged_tags, NULL,
      0, "Encoded with GStreamer Speexenc");
  gst_tag_list_free (merged_tags);

  GST_BUFFER_OFFSET (comments) = enc->bytes_out;
  GST_BUFFER_OFFSET_END (comments) = 0;

  return comments;
}

static void
gst_speexenc_set_last_msg (GstSpeexEnc * enc, const gchar * msg)
{
  g_free (enc->last_message);
  enc->last_message = g_strdup (msg);
  GST_WARNING_OBJECT (enc, "%s", msg);
  g_object_notify (G_OBJECT (enc), "last-message");
}

static gboolean
gst_speexenc_setup (GstSpeexEnc * speexenc)
{
  speexenc->setup = FALSE;

  switch (speexenc->mode) {
    case GST_SPEEXENC_MODE_UWB:
      speexenc->speex_mode = (SpeexMode *) & speex_uwb_mode;
      break;
    case GST_SPEEXENC_MODE_WB:
      speexenc->speex_mode = (SpeexMode *) & speex_wb_mode;
      break;
    case GST_SPEEXENC_MODE_NB:
      speexenc->speex_mode = (SpeexMode *) & speex_nb_mode;
      break;
    case GST_SPEEXENC_MODE_AUTO:
      /* fall through */
    default:
      break;
  }

  if (speexenc->rate > 25000) {
    if (speexenc->mode == GST_SPEEXENC_MODE_AUTO) {
      speexenc->speex_mode = (SpeexMode *) & speex_uwb_mode;
    } else {
      if (speexenc->speex_mode != &speex_uwb_mode) {
        gst_speexenc_set_last_msg (speexenc,
            "Warning: suggest to use ultra wide band mode for this rate");
      }
    }
  } else if (speexenc->rate > 12500) {
    if (speexenc->mode == GST_SPEEXENC_MODE_AUTO) {
      speexenc->speex_mode = (SpeexMode *) & speex_wb_mode;
    } else {
      if (speexenc->speex_mode != &speex_wb_mode) {
        gst_speexenc_set_last_msg (speexenc,
            "Warning: suggest to use wide band mode for this rate");
      }
    }
  } else {
    if (speexenc->mode == GST_SPEEXENC_MODE_AUTO) {
      speexenc->speex_mode = (SpeexMode *) & speex_nb_mode;
    } else {
      if (speexenc->speex_mode != &speex_nb_mode) {
        gst_speexenc_set_last_msg (speexenc,
            "Warning: suggest to use narrow band mode for this rate");
      }
    }
  }

  if (speexenc->rate != 8000 && speexenc->rate != 16000
      && speexenc->rate != 32000) {
    gst_speexenc_set_last_msg (speexenc,
        "Warning: speex is optimized for 8, 16 and 32 KHz");
  }

  speex_init_header (&speexenc->header, speexenc->rate, 1,
      speexenc->speex_mode);
  speexenc->header.frames_per_packet = speexenc->nframes;
  speexenc->header.vbr = speexenc->vbr;
  speexenc->header.nb_channels = speexenc->channels;

  /*Initialize Speex encoder */
  speexenc->state = speex_encoder_init (speexenc->speex_mode);

  speex_encoder_ctl (speexenc->state, SPEEX_GET_FRAME_SIZE,
      &speexenc->frame_size);
  speex_encoder_ctl (speexenc->state, SPEEX_SET_COMPLEXITY,
      &speexenc->complexity);
  speex_encoder_ctl (speexenc->state, SPEEX_SET_SAMPLING_RATE, &speexenc->rate);

  if (speexenc->vbr)
    speex_encoder_ctl (speexenc->state, SPEEX_SET_VBR_QUALITY,
        &speexenc->quality);
  else {
    gint tmp = floor (speexenc->quality);

    speex_encoder_ctl (speexenc->state, SPEEX_SET_QUALITY, &tmp);
  }
  if (speexenc->bitrate) {
    if (speexenc->quality >= 0.0 && speexenc->vbr) {
      gst_speexenc_set_last_msg (speexenc,
          "Warning: bitrate option is overriding quality");
    }
    speex_encoder_ctl (speexenc->state, SPEEX_SET_BITRATE, &speexenc->bitrate);
  }
  if (speexenc->vbr) {
    gint tmp = 1;

    speex_encoder_ctl (speexenc->state, SPEEX_SET_VBR, &tmp);
  } else if (speexenc->vad) {
    gint tmp = 1;

    speex_encoder_ctl (speexenc->state, SPEEX_SET_VAD, &tmp);
  }

  if (speexenc->dtx) {
    gint tmp = 1;

    speex_encoder_ctl (speexenc->state, SPEEX_SET_DTX, &tmp);
  }

  if (speexenc->dtx && !(speexenc->vbr || speexenc->abr || speexenc->vad)) {
    gst_speexenc_set_last_msg (speexenc,
        "Warning: dtx is useless without vad, vbr or abr");
  } else if ((speexenc->vbr || speexenc->abr) && (speexenc->vad)) {
    gst_speexenc_set_last_msg (speexenc,
        "Warning: vad is already implied by vbr or abr");
  }

  if (speexenc->abr) {
    speex_encoder_ctl (speexenc->state, SPEEX_SET_ABR, &speexenc->abr);
  }

  speex_encoder_ctl (speexenc->state, SPEEX_GET_LOOKAHEAD,
      &speexenc->lookahead);

  speexenc->setup = TRUE;

  return TRUE;
}

/* prepare a buffer for transmission */
static GstBuffer *
gst_speexenc_buffer_from_data (GstSpeexEnc * speexenc, guchar * data,
    gint data_len, guint64 granulepos)
{
  GstBuffer *outbuf;

  outbuf = gst_buffer_new_and_alloc (data_len);
  memcpy (GST_BUFFER_DATA (outbuf), data, data_len);
  GST_BUFFER_OFFSET (outbuf) = speexenc->bytes_out;
  GST_BUFFER_OFFSET_END (outbuf) = granulepos;

  GST_DEBUG ("encoded buffer of %d bytes", GST_BUFFER_SIZE (outbuf));
  return outbuf;
}


/* push out the buffer and do internal bookkeeping */
static GstFlowReturn
gst_speexenc_push_buffer (GstSpeexEnc * speexenc, GstBuffer * buffer)
{
  speexenc->bytes_out += GST_BUFFER_SIZE (buffer);

  return gst_pad_push (speexenc->srcpad, buffer);

}

static GstCaps *
gst_speexenc_set_header_on_caps (GstCaps * caps, GstBuffer * buf1,
    GstBuffer * buf2)
{
  GstStructure *structure = NULL;
  GstBuffer *buf;
  GValue array = { 0 };
  GValue value = { 0 };

  caps = gst_caps_make_writable (caps);
  structure = gst_caps_get_structure (caps, 0);

  g_assert (gst_buffer_is_metadata_writable (buf1));
  g_assert (gst_buffer_is_metadata_writable (buf2));

  /* mark buffers */
  GST_BUFFER_FLAG_SET (buf1, GST_BUFFER_FLAG_IN_CAPS);
  GST_BUFFER_FLAG_SET (buf2, GST_BUFFER_FLAG_IN_CAPS);

  /* put buffers in a fixed list */
  g_value_init (&array, GST_TYPE_ARRAY);
  g_value_init (&value, GST_TYPE_BUFFER);
  buf = gst_buffer_copy (buf1);
  gst_value_set_buffer (&value, buf);
  gst_buffer_unref (buf);
  gst_value_array_append_value (&array, &value);
  g_value_unset (&value);
  g_value_init (&value, GST_TYPE_BUFFER);
  buf = gst_buffer_copy (buf2);
  gst_value_set_buffer (&value, buf);
  gst_buffer_unref (buf);
  gst_value_array_append_value (&array, &value);
  gst_structure_set_value (structure, "streamheader", &array);
  g_value_unset (&value);
  g_value_unset (&array);

  return caps;
}


static gboolean
gst_speexenc_sinkevent (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstSpeexEnc *speexenc;

  speexenc = GST_SPEEXENC (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      speexenc->eos = TRUE;
      res = gst_pad_event_default (pad, event);
      break;
    case GST_EVENT_TAG:
    {
      GstTagList *list;

      gst_event_parse_tag (event, &list);
      if (speexenc->tags) {
        gst_tag_list_insert (speexenc->tags, list,
            gst_tag_setter_get_tag_merge_mode (GST_TAG_SETTER (speexenc)));
      } else {
        g_assert_not_reached ();
      }
      res = gst_pad_event_default (pad, event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }
  return res;
}


static GstFlowReturn
gst_speexenc_chain (GstPad * pad, GstBuffer * buf)
{
  GstSpeexEnc *speexenc;
  GstFlowReturn ret = GST_FLOW_OK;

  speexenc = GST_SPEEXENC (GST_PAD_PARENT (pad));

  if (!speexenc->setup)
    goto not_setup;

  if (!speexenc->header_sent) {
    /* Speex streams begin with two headers; the initial header (with
       most of the codec setup parameters) which is mandated by the Ogg
       bitstream spec.  The second header holds any comment fields.
       We merely need to make the headers, then pass them to libspeex 
       one at a time; libspeex handles the additional Ogg bitstream 
       constraints */
    GstBuffer *buf1, *buf2;
    GstCaps *caps;
    guchar *data;
    gint data_len;

    /* create header buffer */
    data = (guint8 *) speex_header_to_packet (&speexenc->header, &data_len);
    buf1 = gst_speexenc_buffer_from_data (speexenc, data, data_len, 0);
    free (data);

    /* create comment buffer */
    buf2 = gst_speexenc_create_metadata_buffer (speexenc);

    /* mark and put on caps */
    caps = gst_pad_get_caps (speexenc->srcpad);
    caps = gst_speexenc_set_header_on_caps (caps, buf1, buf2);

    /* negotiate with these caps */
    GST_DEBUG ("here are the caps: %" GST_PTR_FORMAT, caps);
    gst_pad_set_caps (speexenc->srcpad, caps);

    gst_buffer_set_caps (buf1, caps);
    gst_buffer_set_caps (buf2, caps);
    gst_caps_unref (caps);

    /* push out buffers */
    ret = gst_speexenc_push_buffer (speexenc, buf1);

    if (ret != GST_FLOW_OK) {
      gst_buffer_unref (buf2);
      goto done;
    }

    ret = gst_speexenc_push_buffer (speexenc, buf2);

    if (ret != GST_FLOW_OK)
      goto done;

    speex_bits_reset (&speexenc->bits);

    speexenc->header_sent = TRUE;
  }

  {
    gint frame_size = speexenc->frame_size;
    gint bytes = frame_size * 2 * speexenc->channels;

    /* push buffer to adapter */
    gst_adapter_push (speexenc->adapter, buf);

    while (gst_adapter_available (speexenc->adapter) >= bytes) {
      gint16 *data;
      gint i;
      gint outsize, written;
      GstBuffer *outbuf;

      data = (gint16 *) gst_adapter_peek (speexenc->adapter, bytes);

      for (i = 0; i < frame_size * speexenc->channels; i++) {
        speexenc->input[i] = (gfloat) data[i];
      }
      gst_adapter_flush (speexenc->adapter, bytes);

      speexenc->samples_in += frame_size;

      if (speexenc->channels == 2) {
        speex_encode_stereo (speexenc->input, frame_size, &speexenc->bits);
      }
      speex_encode (speexenc->state, speexenc->input, &speexenc->bits);

      speexenc->frameno++;

      if ((speexenc->frameno % speexenc->nframes) != 0)
        continue;

      speex_bits_insert_terminator (&speexenc->bits);
      outsize = speex_bits_nbytes (&speexenc->bits);

      ret = gst_pad_alloc_buffer_and_set_caps (speexenc->srcpad,
          GST_BUFFER_OFFSET_NONE, outsize, GST_PAD_CAPS (speexenc->srcpad),
          &outbuf);

      if ((GST_FLOW_OK != ret))
        goto done;

      written = speex_bits_write (&speexenc->bits,
          (gchar *) GST_BUFFER_DATA (outbuf), outsize);
      g_assert (written == outsize);
      speex_bits_reset (&speexenc->bits);

      GST_BUFFER_TIMESTAMP (outbuf) =
          gst_util_uint64_scale_int (speexenc->frameno * frame_size -
          speexenc->lookahead, GST_SECOND, speexenc->rate);
      GST_BUFFER_DURATION (outbuf) = gst_util_uint64_scale_int (frame_size,
          GST_SECOND, speexenc->rate);
      /* set gp time and granulepos; see gst-plugins-base/ext/ogg/README */
      GST_BUFFER_OFFSET_END (outbuf) =
          ((speexenc->frameno + 1) * frame_size - speexenc->lookahead);
      GST_BUFFER_OFFSET (outbuf) =
          gst_util_uint64_scale_int (GST_BUFFER_OFFSET_END (outbuf), GST_SECOND,
          speexenc->rate);

      ret = gst_speexenc_push_buffer (speexenc, outbuf);

      if ((GST_FLOW_OK != ret) && (GST_FLOW_NOT_LINKED != ret))
        goto done;
    }
  }

done:
  return ret;

  /* ERRORS */
not_setup:
  {
    gst_buffer_unref (buf);
    GST_ELEMENT_ERROR (speexenc, CORE, NEGOTIATION, (NULL),
        ("encoder not initialized (input is not audio?)"));
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }

}


static void
gst_speexenc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSpeexEnc *speexenc;

  g_return_if_fail (GST_IS_SPEEXENC (object));

  speexenc = GST_SPEEXENC (object);

  switch (prop_id) {
    case ARG_QUALITY:
      g_value_set_float (value, speexenc->quality);
      break;
    case ARG_BITRATE:
      g_value_set_int (value, speexenc->bitrate);
      break;
    case ARG_VBR:
      g_value_set_boolean (value, speexenc->vbr);
      break;
    case ARG_ABR:
      g_value_set_int (value, speexenc->abr);
      break;
    case ARG_VAD:
      g_value_set_boolean (value, speexenc->vad);
      break;
    case ARG_DTX:
      g_value_set_boolean (value, speexenc->dtx);
      break;
    case ARG_COMPLEXITY:
      g_value_set_int (value, speexenc->complexity);
      break;
    case ARG_NFRAMES:
      g_value_set_int (value, speexenc->nframes);
      break;
    case ARG_LAST_MESSAGE:
      g_value_set_string (value, speexenc->last_message);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_speexenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSpeexEnc *speexenc;

  g_return_if_fail (GST_IS_SPEEXENC (object));

  speexenc = GST_SPEEXENC (object);

  switch (prop_id) {
    case ARG_QUALITY:
      speexenc->quality = g_value_get_float (value);
      break;
    case ARG_BITRATE:
      speexenc->bitrate = g_value_get_int (value);
      break;
    case ARG_VBR:
      speexenc->vbr = g_value_get_boolean (value);
      break;
    case ARG_ABR:
      speexenc->abr = g_value_get_int (value);
      break;
    case ARG_VAD:
      speexenc->vad = g_value_get_boolean (value);
      break;
    case ARG_DTX:
      speexenc->dtx = g_value_get_boolean (value);
      break;
    case ARG_COMPLEXITY:
      speexenc->complexity = g_value_get_int (value);
      break;
    case ARG_NFRAMES:
      speexenc->nframes = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_speexenc_change_state (GstElement * element, GstStateChange transition)
{
  GstSpeexEnc *speexenc = GST_SPEEXENC (element);
  GstStateChangeReturn res;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      speexenc->tags = gst_tag_list_new ();
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      speex_bits_init (&speexenc->bits);
      speexenc->frameno = 0;
      speexenc->samples_in = 0;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      /* fall through */
    default:
      break;
  }

  res = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      speexenc->setup = FALSE;
      speexenc->header_sent = FALSE;
      if (speexenc->state) {
        speex_encoder_destroy (speexenc->state);
        speexenc->state = NULL;
      }
      speex_bits_destroy (&speexenc->bits);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_tag_list_free (speexenc->tags);
      speexenc->tags = NULL;
    default:
      break;
  }

  return res;
}
