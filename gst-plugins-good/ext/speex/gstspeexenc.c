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
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <speex/speex.h>
#include <speex/speex_stereo.h>

#include <gst/gsttaginterface.h>
#include <gst/tag/tag.h>
#include "gstspeexenc.h"

GST_DEBUG_CATEGORY (speexenc_debug);
#define GST_CAT_DEFAULT speexenc_debug

static GstPadTemplate *gst_speexenc_src_template, *gst_speexenc_sink_template;

/* elementfactory information */
GstElementDetails speexenc_details = {
  "Speex encoder",
  "Codec/Encoder/Audio",
  "Encodes audio in Speex format",
  "Wim Taymans <wim@fluendo.com>",
};

/* GstSpeexEnc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_QUALITY		8.0
#define DEFAULT_BITRATE		0
#define DEFAULT_VBR		FALSE
#define DEFAULT_ABR		0
#define DEFAULT_VAD		FALSE
#define DEFAULT_DTX		FALSE
#define DEFAULT_COMPLEXITY	3
#define DEFAULT_NFRAMES		1

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

static void gst_speexenc_base_init (gpointer g_class);
static void gst_speexenc_class_init (GstSpeexEncClass * klass);
static void gst_speexenc_init (GstSpeexEnc * speexenc);

static void gst_speexenc_chain (GstPad * pad, GstData * _data);
static gboolean gst_speexenc_setup (GstSpeexEnc * speexenc);

static void gst_speexenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_speexenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static GstElementStateReturn gst_speexenc_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

/*static guint gst_speexenc_signals[LAST_SIGNAL] = { 0 }; */

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

static GstCaps *
speex_caps_factory (void)
{
  return gst_caps_new_simple ("audio/x-speex", NULL);
}

static GstCaps *
raw_caps_factory (void)
{
  return
      gst_caps_new_simple ("audio/x-raw-int",
      "rate", GST_TYPE_INT_RANGE, 6000, 48000,
      "channels", GST_TYPE_INT_RANGE, 1, 2,
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "width", G_TYPE_INT, 16, "depth", G_TYPE_INT, 16, NULL);
}

static void
gst_speexenc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *raw_caps, *speex_caps;

  raw_caps = raw_caps_factory ();
  speex_caps = speex_caps_factory ();

  gst_speexenc_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, raw_caps);
  gst_speexenc_src_template = gst_pad_template_new ("src", GST_PAD_SRC,
      GST_PAD_ALWAYS, speex_caps);
  gst_element_class_add_pad_template (element_class,
      gst_speexenc_sink_template);
  gst_element_class_add_pad_template (element_class, gst_speexenc_src_template);
  gst_element_class_set_details (element_class, &speexenc_details);
}

static void
gst_speexenc_class_init (GstSpeexEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_QUALITY,
      g_param_spec_float ("quality", "Quality", "Encoding quality",
          0.0, 10.0, DEFAULT_QUALITY, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BITRATE,
      g_param_spec_int ("bitrate", "Encoding Bit-rate",
          "Specify an encoding bit-rate (in bps). ",
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

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_speexenc_set_property;
  gobject_class->get_property = gst_speexenc_get_property;

  gstelement_class->change_state = gst_speexenc_change_state;
}

static GstPadLinkReturn
gst_speexenc_sinkconnect (GstPad * pad, const GstCaps * caps)
{
  GstSpeexEnc *speexenc;
  GstStructure *structure;

  speexenc = GST_SPEEXENC (gst_pad_get_parent (pad));
  speexenc->setup = FALSE;

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "channels", &speexenc->channels);
  gst_structure_get_int (structure, "rate", &speexenc->rate);

  gst_speexenc_setup (speexenc);

  if (speexenc->setup)
    return GST_PAD_LINK_OK;

  return GST_PAD_LINK_REFUSED;
}

static gboolean
gst_speexenc_convert_src (GstPad * pad, GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstSpeexEnc *speexenc;
  gint64 avg;

  speexenc = GST_SPEEXENC (gst_pad_get_parent (pad));

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

  speexenc = GST_SPEEXENC (gst_pad_get_parent (pad));

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
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };

  return gst_speexenc_src_query_types;
}

static gboolean
gst_speexenc_src_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  gboolean res = TRUE;
  GstSpeexEnc *speexenc;

  speexenc = GST_SPEEXENC (gst_pad_get_parent (pad));

  switch (type) {
    case GST_QUERY_TOTAL:
    {
      switch (*format) {
        case GST_FORMAT_BYTES:
        case GST_FORMAT_TIME:
        {
          gint64 peer_value;
          const GstFormat *peer_formats;

          res = FALSE;

          peer_formats = gst_pad_get_formats (GST_PAD_PEER (speexenc->sinkpad));

          while (peer_formats && *peer_formats && !res) {

            GstFormat peer_format = *peer_formats;

            /* do the probe */
            if (gst_pad_query (GST_PAD_PEER (speexenc->sinkpad),
                    GST_QUERY_TOTAL, &peer_format, &peer_value)) {
              GstFormat conv_format;

              /* convert to TIME */
              conv_format = GST_FORMAT_TIME;
              res = gst_pad_convert (speexenc->sinkpad,
                  peer_format, peer_value, &conv_format, value);
              /* and to final format */
              res &= gst_pad_convert (pad,
                  GST_FORMAT_TIME, *value, format, value);
            }
            peer_formats++;
          }
          break;
        }
        default:
          res = FALSE;
          break;
      }
      break;
    }
    case GST_QUERY_POSITION:
      switch (*format) {
        default:
        {
          /* we only know about our samples, convert to requested format */
          res = gst_pad_convert (pad,
              GST_FORMAT_BYTES, speexenc->bytes_out, format, value);
          break;
        }
      }
      break;
    default:
      res = FALSE;
      break;
  }
  return res;
}

static void
gst_speexenc_init (GstSpeexEnc * speexenc)
{
  speexenc->sinkpad =
      gst_pad_new_from_template (gst_speexenc_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (speexenc), speexenc->sinkpad);
  gst_pad_set_chain_function (speexenc->sinkpad, gst_speexenc_chain);
  gst_pad_set_link_function (speexenc->sinkpad, gst_speexenc_sinkconnect);
  gst_pad_set_convert_function (speexenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_speexenc_convert_sink));
  gst_pad_set_formats_function (speexenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_speexenc_get_formats));

  speexenc->srcpad =
      gst_pad_new_from_template (gst_speexenc_src_template, "src");
  gst_pad_set_query_function (speexenc->srcpad,
      GST_DEBUG_FUNCPTR (gst_speexenc_src_query));
  gst_pad_set_query_type_function (speexenc->srcpad,
      GST_DEBUG_FUNCPTR (gst_speexenc_get_query_types));
  gst_pad_set_convert_function (speexenc->srcpad,
      GST_DEBUG_FUNCPTR (gst_speexenc_convert_src));
  gst_pad_set_formats_function (speexenc->srcpad,
      GST_DEBUG_FUNCPTR (gst_speexenc_get_formats));
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
  speexenc->eos = FALSE;
  speexenc->header_sent = FALSE;

  speexenc->tags = gst_tag_list_new ();
  speexenc->adapter = gst_adapter_new ();

  /* we're chained and we can deal with events */
  GST_FLAG_SET (speexenc, GST_ELEMENT_EVENT_AWARE);
}


static gchar *
gst_speexenc_get_tag_value (const GstTagList * list, const gchar * tag,
    int index)
{
  gchar *speexvalue = NULL;

  if (tag == NULL) {
    return NULL;
  }

  /* get tag name right */
  if ((strcmp (tag, GST_TAG_TRACK_NUMBER) == 0)
      || (strcmp (tag, GST_TAG_ALBUM_VOLUME_NUMBER) == 0)
      || (strcmp (tag, GST_TAG_TRACK_COUNT) == 0)
      || (strcmp (tag, GST_TAG_ALBUM_VOLUME_COUNT) == 0)) {
    guint track_no;

    if (!gst_tag_list_get_uint_index (list, tag, index, &track_no))
      g_assert_not_reached ();
    speexvalue = g_strdup_printf ("%u", track_no);
  } else if (strcmp (tag, GST_TAG_DATE) == 0) {
    /* FIXME: how are dates represented in speex files? */
    GDate *date;
    guint u;

    if (!gst_tag_list_get_uint_index (list, tag, index, &u))
      g_assert_not_reached ();
    date = g_date_new_julian (u);
    speexvalue =
        g_strdup_printf ("%04d-%02d-%02d", (gint) g_date_get_year (date),
        (gint) g_date_get_month (date), (gint) g_date_get_day (date));
    g_date_free (date);
  } else if (gst_tag_get_type (tag) == G_TYPE_STRING) {
    if (!gst_tag_list_get_string_index (list, tag, index, &speexvalue))
      g_assert_not_reached ();
  }

  return speexvalue;
}

/*
 *  Comments will be stored in the Vorbis style.
 *  It is describled in the "Structure" section of
 *  http://www.xiph.org/ogg/vorbis/doc/v-comment.html
 *
 *  The comment header is decoded as follows:
 *  1) [vendor_length] = read an unsigned integer of 32 bits
 *  2) [vendor_string] = read a UTF-8 vector as [vendor_length] octets
 *  3) [user_comment_list_length] = read an unsigned integer of 32 bits
 *  4) iterate [user_comment_list_length] times {
 *     5) [length] = read an unsigned integer of 32 bits
 *     6) this iteration's user comment = read a UTF-8 vector as [length] octets
 *     }
 *  7) [framing_bit] = read a single bit as boolean
 *  8) if ( [framing_bit]  unset or end of packet ) then ERROR
 *  9) done.
 *
 *  If you have troubles, please write to ymnk@jcraft.com.
 */
#define readint(buf, base) (((buf[base+3]<<24) & 0xff000000)| \
		            ((buf[base+2]<<16) & 0xff0000)|   \
		            ((buf[base+1]<< 8) & 0xff00)|     \
		             (buf[base  ]      & 0xff))
#define writeint(buf, base, val) do{ buf[base+3] = ((val)>>24) & 0xff; \
	                             buf[base+2] = ((val)>>16) & 0xff; \
	                             buf[base+1] = ((val)>> 8) & 0xff; \
	                             buf[base  ] =  (val)      & 0xff; \
	                          }while(0)

static void
comment_init (char **comments, int *length, char *vendor_string)
{
  int vendor_length = strlen (vendor_string);
  int user_comment_list_length = 0;
  int len = 4 + vendor_length + 4;
  char *p = (char *) malloc (len);

  if (p == NULL) {
  }
  writeint (p, 0, vendor_length);
  memcpy (p + 4, vendor_string, vendor_length);
  writeint (p, 4 + vendor_length, user_comment_list_length);
  *length = len;
  *comments = p;
}
static void
comment_add (char **comments, int *length, const char *tag, char *val)
{
  char *p = *comments;
  int vendor_length = readint (p, 0);
  int user_comment_list_length = readint (p, 4 + vendor_length);
  int tag_len = (tag ? strlen (tag) : 0);
  int val_len = strlen (val);
  int len = (*length) + 4 + tag_len + val_len;

  p = (char *) realloc (p, len);

  writeint (p, *length, tag_len + val_len);     /* length of comment */
  if (tag)
    memcpy (p + *length + 4, tag, tag_len);     /* comment */
  memcpy (p + *length + 4 + tag_len, val, val_len);     /* comment */
  writeint (p, 4 + vendor_length, user_comment_list_length + 1);

  *comments = p;
  *length = len;
}

#undef readint
#undef writeint

static void
gst_speexenc_metadata_set1 (const GstTagList * list, const gchar * tag,
    gpointer speexenc)
{
  const gchar *speextag = NULL;
  gchar *speexvalue = NULL;
  guint i, count;
  GstSpeexEnc *enc = GST_SPEEXENC (speexenc);

  speextag = gst_tag_to_vorbis_tag (tag);
  if (speextag == NULL) {
    return;
  }

  count = gst_tag_list_get_tag_size (list, tag);
  for (i = 0; i < count; i++) {
    speexvalue = gst_speexenc_get_tag_value (list, tag, i);

    if (speexvalue != NULL) {
      comment_add (&enc->comments, &enc->comment_len, speextag, speexvalue);
    }
  }
}

static void
gst_speexenc_set_metadata (GstSpeexEnc * speexenc)
{
  GstTagList *copy;
  const GstTagList *user_tags;

  user_tags = gst_tag_setter_get_list (GST_TAG_SETTER (speexenc));
  if (!(speexenc->tags || user_tags))
    return;

  comment_init (&speexenc->comments, &speexenc->comment_len,
      "Encoded with GStreamer Speexenc");
  copy =
      gst_tag_list_merge (user_tags, speexenc->tags,
      gst_tag_setter_get_merge_mode (GST_TAG_SETTER (speexenc)));
  gst_tag_list_foreach (copy, gst_speexenc_metadata_set1, speexenc);
  gst_tag_list_free (copy);
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
    default:
      break;
  }

  if (speexenc->rate > 25000) {
    if (speexenc->mode == GST_SPEEXENC_MODE_AUTO) {
      speexenc->speex_mode = (SpeexMode *) & speex_uwb_mode;
    } else {
      if (speexenc->speex_mode != &speex_uwb_mode) {
        speexenc->last_message =
            g_strdup_printf
            ("Warning: suggest to use ultra wide band mode for this rate");
        g_object_notify (G_OBJECT (speexenc), "last_message");
      }
    }
  } else if (speexenc->rate > 12500) {
    if (speexenc->mode == GST_SPEEXENC_MODE_AUTO) {
      speexenc->speex_mode = (SpeexMode *) & speex_wb_mode;
    } else {
      if (speexenc->speex_mode != &speex_wb_mode) {
        speexenc->last_message =
            g_strdup_printf
            ("Warning: suggest to use wide band mode for this rate");
        g_object_notify (G_OBJECT (speexenc), "last_message");
      }
    }
  } else {
    if (speexenc->mode == GST_SPEEXENC_MODE_AUTO) {
      speexenc->speex_mode = (SpeexMode *) & speex_nb_mode;
    } else {
      if (speexenc->speex_mode != &speex_nb_mode) {
        speexenc->last_message =
            g_strdup_printf
            ("Warning: suggest to use narrow band mode for this rate");
        g_object_notify (G_OBJECT (speexenc), "last_message");
      }
    }
  }

  if (speexenc->rate != 8000 && speexenc->rate != 16000
      && speexenc->rate != 32000) {
    speexenc->last_message =
        g_strdup_printf ("Warning: speex is optimized for 8, 16 and 32 KHz");
    g_object_notify (G_OBJECT (speexenc), "last_message");
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
      speexenc->last_message =
          g_strdup_printf ("Warning: bitrate option is overriding quality");
      g_object_notify (G_OBJECT (speexenc), "last_message");
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
    speexenc->last_message =
        g_strdup_printf ("Warning: dtx is useless without vad, vbr or abr");
    g_object_notify (G_OBJECT (speexenc), "last_message");
  } else if ((speexenc->vbr || speexenc->abr) && (speexenc->vad)) {
    speexenc->last_message =
        g_strdup_printf ("Warning: vad is already implied by vbr or abr");
    g_object_notify (G_OBJECT (speexenc), "last_message");
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
static void
gst_speexenc_push_buffer (GstSpeexEnc * speexenc, GstBuffer * buffer)
{
  speexenc->bytes_out += GST_BUFFER_SIZE (buffer);

  if (GST_PAD_IS_USABLE (speexenc->srcpad)) {
    gst_pad_push (speexenc->srcpad, GST_DATA (buffer));
  } else {
    gst_buffer_unref (buffer);
  }
}

static void
gst_speexenc_set_header_on_caps (GstCaps * caps, GstBuffer * buf1,
    GstBuffer * buf2)
{
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GValue list = { 0 };
  GValue value = { 0 };

  /* mark buffers */
  GST_BUFFER_FLAG_SET (buf1, GST_BUFFER_IN_CAPS);
  GST_BUFFER_FLAG_SET (buf2, GST_BUFFER_IN_CAPS);

  /* put buffers in a fixed list */
  g_value_init (&list, GST_TYPE_FIXED_LIST);
  g_value_init (&value, GST_TYPE_BUFFER);
  g_value_set_boxed (&value, buf1);
  gst_value_list_append_value (&list, &value);
  g_value_unset (&value);
  g_value_init (&value, GST_TYPE_BUFFER);
  g_value_set_boxed (&value, buf2);
  gst_value_list_append_value (&list, &value);
  gst_structure_set_value (structure, "streamheader", &list);
  g_value_unset (&value);
  g_value_unset (&list);
}

static void
gst_speexenc_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstSpeexEnc *speexenc;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  speexenc = GST_SPEEXENC (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        speexenc->eos = TRUE;
        gst_event_unref (event);
        break;
      case GST_EVENT_TAG:
        if (speexenc->tags) {
          gst_tag_list_insert (speexenc->tags, gst_event_tag_get_list (event),
              gst_tag_setter_get_merge_mode (GST_TAG_SETTER (speexenc)));
        } else {
          g_assert_not_reached ();
        }
        gst_pad_event_default (pad, event);
        return;
      default:
        gst_pad_event_default (pad, event);
        return;
    }
  } else {
    if (!speexenc->setup) {
      gst_buffer_unref (buf);
      GST_ELEMENT_ERROR (speexenc, CORE, NEGOTIATION, (NULL),
          ("encoder not initialized (input is not audio?)"));
      return;
    }

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

      gst_speexenc_set_metadata (speexenc);

      /* create header buffer */
      data = speex_header_to_packet (&speexenc->header, &data_len);
      buf1 = gst_speexenc_buffer_from_data (speexenc, data, data_len, 0);

      /* create comment buffer */
      buf2 =
          gst_speexenc_buffer_from_data (speexenc, speexenc->comments,
          speexenc->comment_len, 0);

      /* mark and put on caps */
      caps = gst_pad_get_caps (speexenc->srcpad);
      gst_speexenc_set_header_on_caps (caps, buf1, buf2);

      /* negotiate with these caps */
      GST_DEBUG ("here are the caps: %" GST_PTR_FORMAT, caps);
      gst_pad_try_set_caps (speexenc->srcpad, caps);

      /* push out buffers */
      gst_speexenc_push_buffer (speexenc, buf1);
      gst_speexenc_push_buffer (speexenc, buf2);

      speex_bits_init (&speexenc->bits);
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
        outbuf =
            gst_pad_alloc_buffer (speexenc->srcpad, GST_BUFFER_OFFSET_NONE,
            outsize);
        written =
            speex_bits_write (&speexenc->bits, GST_BUFFER_DATA (outbuf),
            outsize);
        g_assert (written == outsize);
        speex_bits_reset (&speexenc->bits);

        GST_BUFFER_OFFSET (outbuf) = speexenc->bytes_out;
        GST_BUFFER_OFFSET_END (outbuf) =
            speexenc->frameno * frame_size - speexenc->lookahead;

        gst_speexenc_push_buffer (speexenc, outbuf);
      }
    }
  }

  if (speexenc->eos) {
    /* clean up and exit. */
    gst_pad_push (speexenc->srcpad, GST_DATA (gst_event_new (GST_EVENT_EOS)));
    gst_element_set_eos (GST_ELEMENT (speexenc));
  }
}

static void
gst_speexenc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSpeexEnc *speexenc;

  /* it's not null if we got it, but it might not be ours */
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

  /* it's not null if we got it, but it might not be ours */
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

static GstElementStateReturn
gst_speexenc_change_state (GstElement * element)
{
  GstSpeexEnc *speexenc = GST_SPEEXENC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      speexenc->eos = FALSE;
      speexenc->frameno = 0;
      speexenc->samples_in = 0;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      speexenc->setup = FALSE;
      speexenc->header_sent = FALSE;
      gst_tag_list_free (speexenc->tags);
      speexenc->tags = gst_tag_list_new ();
      break;
    case GST_STATE_READY_TO_NULL:
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
