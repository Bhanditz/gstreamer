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
#include <vorbis/vorbisenc.h>

#include <gst/gsttaginterface.h>
#include <gst/tag/tag.h>
#include "vorbisenc.h"

GST_DEBUG_CATEGORY_EXTERN (vorbisenc_debug);
#define GST_CAT_DEFAULT vorbisenc_debug

static GstPadTemplate *gst_vorbisenc_src_template, *gst_vorbisenc_sink_template;

/* elementfactory information */
GstElementDetails vorbisenc_details = {
  "Vorbis encoder",
  "Codec/Encoder/Audio",
  "Encodes audio in Vorbis format",
  "Monty <monty@xiph.org>, " "Wim Taymans <wim@fluendo.com>",
};

/* VorbisEnc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_MAX_BITRATE,
  ARG_BITRATE,
  ARG_MIN_BITRATE,
  ARG_QUALITY,
  ARG_MANAGED,
  ARG_LAST_MESSAGE
};

/* FIXME:
 * vorbis_granule_time was added between 1.0 and 1.0.1; it's too silly
 * to require a new version for such a simple function, but once we move
 * beyond 1.0 for other reasons we can remove this copy */

static double
vorbis_granule_time_copy (vorbis_dsp_state * v, ogg_int64_t granulepos)
{
  if (granulepos >= 0)
    return ((double) granulepos / v->vi->rate);
  return (-1);
}

#if 0
static const GstFormat *
gst_vorbisenc_get_formats (GstPad * pad)
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

#define MAX_BITRATE_DEFAULT 	-1
#define BITRATE_DEFAULT 	-1
#define MIN_BITRATE_DEFAULT 	-1
#define QUALITY_DEFAULT 	0.3
#define LOWEST_BITRATE 		6000    /* lowest allowed for a 8 kHz stream */
#define HIGHEST_BITRATE 	250001  /* highest allowed for a 44 kHz stream */

static void gst_vorbisenc_base_init (gpointer g_class);
static void gst_vorbisenc_class_init (VorbisEncClass * klass);
static void gst_vorbisenc_init (VorbisEnc * vorbisenc);

static gboolean gst_vorbisenc_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_vorbisenc_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_vorbisenc_setup (VorbisEnc * vorbisenc);

static void gst_vorbisenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_vorbisenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static GstElementStateReturn gst_vorbisenc_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

/*static guint gst_vorbisenc_signals[LAST_SIGNAL] = { 0 }; */

GType
vorbisenc_get_type (void)
{
  static GType vorbisenc_type = 0;

  if (!vorbisenc_type) {
    static const GTypeInfo vorbisenc_info = {
      sizeof (VorbisEncClass),
      gst_vorbisenc_base_init,
      NULL,
      (GClassInitFunc) gst_vorbisenc_class_init,
      NULL,
      NULL,
      sizeof (VorbisEnc),
      0,
      (GInstanceInitFunc) gst_vorbisenc_init,
    };
    static const GInterfaceInfo tag_setter_info = {
      NULL,
      NULL,
      NULL
    };

    vorbisenc_type =
        g_type_register_static (GST_TYPE_ELEMENT, "VorbisEnc", &vorbisenc_info,
        0);

    g_type_add_interface_static (vorbisenc_type, GST_TYPE_TAG_SETTER,
        &tag_setter_info);
  }
  return vorbisenc_type;
}

static GstCaps *
vorbis_caps_factory (void)
{
  return gst_caps_new_simple ("audio/x-vorbis", NULL);
}

static GstCaps *
raw_caps_factory (void)
{
  /* lowest sample rate is in vorbis/lib/modes/setup_8.h, 8000 Hz
   * highest sample rate is in vorbis/lib/modes/setup_44.h, 50000 Hz */
  return
      gst_caps_new_simple ("audio/x-raw-float",
      "rate", GST_TYPE_INT_RANGE, 8000, 50000,
      "channels", GST_TYPE_INT_RANGE, 1, 2,
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "width", G_TYPE_INT, 32, "buffer-frames", G_TYPE_INT, 0, NULL);
}

static void
gst_vorbisenc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *raw_caps, *vorbis_caps;

  raw_caps = raw_caps_factory ();
  vorbis_caps = vorbis_caps_factory ();

  gst_vorbisenc_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, raw_caps);
  gst_vorbisenc_src_template = gst_pad_template_new ("src", GST_PAD_SRC,
      GST_PAD_ALWAYS, vorbis_caps);
  gst_element_class_add_pad_template (element_class,
      gst_vorbisenc_sink_template);
  gst_element_class_add_pad_template (element_class,
      gst_vorbisenc_src_template);
  gst_element_class_set_details (element_class, &vorbisenc_details);
}

static void
gst_vorbisenc_class_init (VorbisEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_vorbisenc_set_property;
  gobject_class->get_property = gst_vorbisenc_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MAX_BITRATE,
      g_param_spec_int ("max-bitrate", "Maximum Bitrate",
          "Specify a maximum bitrate (in bps). Useful for streaming "
          "applications. (-1 == disabled)",
          -1, HIGHEST_BITRATE, MAX_BITRATE_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BITRATE,
      g_param_spec_int ("bitrate", "Target Bitrate",
          "Attempt to encode at a bitrate averaging this (in bps). "
          "This uses the bitrate management engine, and is not recommended for most users. "
          "Quality is a better alternative. (-1 == disabled)",
          -1, HIGHEST_BITRATE, BITRATE_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MIN_BITRATE,
      g_param_spec_int ("min_bitrate", "Minimum Bitrate",
          "Specify a minimum bitrate (in bps). Useful for encoding for a "
          "fixed-size channel. (-1 == disabled)",
          -1, HIGHEST_BITRATE, MIN_BITRATE_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_QUALITY,
      g_param_spec_float ("quality", "Quality",
          "Specify quality instead of specifying a particular bitrate.",
          0.0, 1.0, QUALITY_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MANAGED,
      g_param_spec_boolean ("managed", "Managed",
          "Enable bitrate management engine", FALSE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LAST_MESSAGE,
      g_param_spec_string ("last-message", "last-message",
          "The last status message", NULL, G_PARAM_READABLE));

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_vorbisenc_change_state;
}

static gboolean
gst_vorbisenc_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  VorbisEnc *vorbisenc;
  GstStructure *structure;

  vorbisenc = GST_VORBISENC (GST_PAD_PARENT (pad));
  vorbisenc->setup = FALSE;

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "channels", &vorbisenc->channels);
  gst_structure_get_int (structure, "rate", &vorbisenc->frequency);

  gst_vorbisenc_setup (vorbisenc);

  if (vorbisenc->setup)
    return TRUE;

  return FALSE;
}

static gboolean
gst_vorbisenc_convert_src (GstPad * pad, GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  VorbisEnc *vorbisenc;
  gint64 avg;

  vorbisenc = GST_VORBISENC (gst_pad_get_parent (pad));

  if (vorbisenc->samples_in == 0 ||
      vorbisenc->bytes_out == 0 || vorbisenc->frequency == 0)
    return FALSE;

  avg = (vorbisenc->bytes_out * vorbisenc->frequency) / (vorbisenc->samples_in);

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
gst_vorbisenc_convert_sink (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  guint scale = 1;
  gint bytes_per_sample;
  VorbisEnc *vorbisenc;

  vorbisenc = GST_VORBISENC (gst_pad_get_parent (pad));

  bytes_per_sample = vorbisenc->channels * 2;

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
          gint byterate = bytes_per_sample * vorbisenc->frequency;

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
          if (vorbisenc->frequency == 0)
            return FALSE;
          *dest_value = src_value * GST_SECOND / vorbisenc->frequency;
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
          *dest_value = src_value * scale * vorbisenc->frequency / GST_SECOND;
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
gst_vorbisenc_get_query_types (GstPad * pad)
{
  static const GstQueryType gst_vorbisenc_src_query_types[] = {
    GST_QUERY_POSITION,
    0
  };

  return gst_vorbisenc_src_query_types;
}

static gboolean
gst_vorbisenc_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  VorbisEnc *vorbisenc;

  vorbisenc = GST_VORBISENC (GST_PAD_PARENT (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
#if 0
      switch (*format) {
        case GST_FORMAT_BYTES:
        case GST_FORMAT_TIME:
        {
          gint64 peer_value;
          const GstFormat *peer_formats;

          res = FALSE;

          peer_formats =
              gst_pad_get_formats (GST_PAD_PEER (vorbisenc->sinkpad));

          while (peer_formats && *peer_formats && !res) {

            GstFormat peer_format = *peer_formats;

            /* do the probe */
            if (gst_pad_query (GST_PAD_PEER (vorbisenc->sinkpad),
                    GST_QUERY_TOTAL, &peer_format, &peer_value)) {
              GstFormat conv_format;

              /* convert to TIME */
              conv_format = GST_FORMAT_TIME;
              res = gst_vorbisenc_convert_sink (vorbisenc->sinkpad,
                  peer_format, peer_value, &conv_format, value);
              /* and to final format */
              res &= gst_vorbisenc_convert_src (pad,
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
#endif
      res = FALSE;
      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res =
              gst_vorbisenc_convert_src (pad, src_fmt, src_val, &dest_fmt,
                  &dest_val)))
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = FALSE;
      break;
  }

error:
  return res;
}

static gboolean
gst_vorbisenc_sink_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  VorbisEnc *vorbisenc;

  vorbisenc = GST_VORBISENC (GST_PAD_PARENT (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res =
              gst_vorbisenc_convert_sink (pad, src_fmt, src_val, &dest_fmt,
                  &dest_val)))
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = FALSE;
      break;
  }

error:
  return res;
}

static void
gst_vorbisenc_init (VorbisEnc * vorbisenc)
{
  vorbisenc->sinkpad =
      gst_pad_new_from_template (gst_vorbisenc_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (vorbisenc), vorbisenc->sinkpad);
  gst_pad_set_event_function (vorbisenc->sinkpad, gst_vorbisenc_sink_event);
  gst_pad_set_chain_function (vorbisenc->sinkpad, gst_vorbisenc_chain);
  gst_pad_set_setcaps_function (vorbisenc->sinkpad, gst_vorbisenc_sink_setcaps);
  gst_pad_set_query_function (vorbisenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vorbisenc_sink_query));

  vorbisenc->srcpad =
      gst_pad_new_from_template (gst_vorbisenc_src_template, "src");
  gst_pad_set_query_function (vorbisenc->srcpad,
      GST_DEBUG_FUNCPTR (gst_vorbisenc_src_query));
  gst_pad_set_query_type_function (vorbisenc->srcpad,
      GST_DEBUG_FUNCPTR (gst_vorbisenc_get_query_types));
  gst_element_add_pad (GST_ELEMENT (vorbisenc), vorbisenc->srcpad);

  vorbisenc->channels = -1;
  vorbisenc->frequency = -1;

  vorbisenc->managed = FALSE;
  vorbisenc->max_bitrate = MAX_BITRATE_DEFAULT;
  vorbisenc->bitrate = BITRATE_DEFAULT;
  vorbisenc->min_bitrate = MIN_BITRATE_DEFAULT;
  vorbisenc->quality = QUALITY_DEFAULT;
  vorbisenc->quality_set = FALSE;
  vorbisenc->last_message = NULL;

  vorbisenc->setup = FALSE;
  vorbisenc->eos = FALSE;
  vorbisenc->header_sent = FALSE;

  vorbisenc->tags = gst_tag_list_new ();
}


static gchar *
gst_vorbisenc_get_tag_value (const GstTagList * list, const gchar * tag,
    int index)
{
  gchar *vorbisvalue = NULL;

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
    vorbisvalue = g_strdup_printf ("%u", track_no);
  } else if (strcmp (tag, GST_TAG_DATE) == 0) {
    /* FIXME: how are dates represented in vorbis files? */
    GDate *date;
    guint u;

    if (!gst_tag_list_get_uint_index (list, tag, index, &u))
      g_assert_not_reached ();
    date = g_date_new_julian (u);
    vorbisvalue =
        g_strdup_printf ("%04d-%02d-%02d", (gint) g_date_get_year (date),
        (gint) g_date_get_month (date), (gint) g_date_get_day (date));
    g_date_free (date);
  } else if (gst_tag_get_type (tag) == G_TYPE_STRING) {
    if (!gst_tag_list_get_string_index (list, tag, index, &vorbisvalue))
      g_assert_not_reached ();
  }

  return vorbisvalue;
}

static void
gst_vorbisenc_metadata_set1 (const GstTagList * list, const gchar * tag,
    gpointer vorbisenc)
{
  const gchar *vorbistag = NULL;
  gchar *vorbisvalue = NULL;
  guint i, count;
  VorbisEnc *enc = GST_VORBISENC (vorbisenc);

  vorbistag = gst_tag_to_vorbis_tag (tag);
  if (vorbistag == NULL) {
    return;
  }

  count = gst_tag_list_get_tag_size (list, tag);
  for (i = 0; i < count; i++) {
    vorbisvalue = gst_vorbisenc_get_tag_value (list, tag, i);

    if (vorbisvalue != NULL) {
      vorbis_comment_add_tag (&enc->vc, g_strdup (vorbistag), vorbisvalue);
    }
  }
}

static void
gst_vorbisenc_set_metadata (VorbisEnc * vorbisenc)
{
  GstTagList *copy;
  const GstTagList *user_tags;

  user_tags = gst_tag_setter_get_list (GST_TAG_SETTER (vorbisenc));
  if (!(vorbisenc->tags || user_tags))
    return;

  copy =
      gst_tag_list_merge (user_tags, vorbisenc->tags,
      gst_tag_setter_get_merge_mode (GST_TAG_SETTER (vorbisenc)));
  vorbis_comment_init (&vorbisenc->vc);
  gst_tag_list_foreach (copy, gst_vorbisenc_metadata_set1, vorbisenc);
  gst_tag_list_free (copy);
}

static gchar *
get_constraints_string (VorbisEnc * vorbisenc)
{
  gint min = vorbisenc->min_bitrate;
  gint max = vorbisenc->max_bitrate;
  gchar *result;

  if (min > 0 && max > 0)
    result = g_strdup_printf ("(min %d bps, max %d bps)", min, max);
  else if (min > 0)
    result = g_strdup_printf ("(min %d bps, no max)", min);
  else if (max > 0)
    result = g_strdup_printf ("(no min, max %d bps)", max);
  else
    result = g_strdup_printf ("(no min or max)");

  return result;
}

static void
update_start_message (VorbisEnc * vorbisenc)
{
  gchar *constraints;

  g_free (vorbisenc->last_message);

  if (vorbisenc->bitrate > 0) {
    if (vorbisenc->managed) {
      constraints = get_constraints_string (vorbisenc);
      vorbisenc->last_message =
          g_strdup_printf ("encoding at average bitrate %d bps %s",
          vorbisenc->bitrate, constraints);
      g_free (constraints);
    } else {
      vorbisenc->last_message =
          g_strdup_printf
          ("encoding at approximate bitrate %d bps (VBR encoding enabled)",
          vorbisenc->bitrate);
    }
  } else {
    if (vorbisenc->quality_set) {
      if (vorbisenc->managed) {
        constraints = get_constraints_string (vorbisenc);
        vorbisenc->last_message =
            g_strdup_printf
            ("encoding at quality level %2.2f using constrained VBR %s",
            vorbisenc->quality, constraints);
        g_free (constraints);
      } else {
        vorbisenc->last_message =
            g_strdup_printf ("encoding at quality level %2.2f",
            vorbisenc->quality);
      }
    } else {
      constraints = get_constraints_string (vorbisenc);
      vorbisenc->last_message =
          g_strdup_printf ("encoding using bitrate management %s", constraints);
      g_free (constraints);
    }
  }

  g_object_notify (G_OBJECT (vorbisenc), "last_message");
}

static gboolean
gst_vorbisenc_setup (VorbisEnc * vorbisenc)
{
  vorbisenc->setup = FALSE;

  if (vorbisenc->bitrate < 0 && vorbisenc->min_bitrate < 0
      && vorbisenc->max_bitrate < 0) {
    vorbisenc->quality_set = TRUE;
  }

  update_start_message (vorbisenc);

  /* choose an encoding mode */
  /* (mode 0: 44kHz stereo uncoupled, roughly 128kbps VBR) */
  vorbis_info_init (&vorbisenc->vi);

  if (vorbisenc->quality_set) {
    if (vorbis_encode_setup_vbr (&vorbisenc->vi,
            vorbisenc->channels, vorbisenc->frequency,
            vorbisenc->quality) != 0) {
      GST_ERROR_OBJECT (vorbisenc,
          "vorbisenc: initialisation failed: invalid parameters for quality");
      vorbis_info_clear (&vorbisenc->vi);
      return FALSE;
    }

    /* do we have optional hard quality restrictions? */
    if (vorbisenc->max_bitrate > 0 || vorbisenc->min_bitrate > 0) {
      struct ovectl_ratemanage_arg ai;

      vorbis_encode_ctl (&vorbisenc->vi, OV_ECTL_RATEMANAGE_GET, &ai);

      /* the bitrates used by libvorbisenc are in kbit/sec, ours in bit/sec
       * also remember that in telecom kbit/sec is 1000 bit/sec */
      ai.bitrate_hard_min = vorbisenc->min_bitrate / 1000;
      ai.bitrate_hard_max = vorbisenc->max_bitrate / 1000;
      ai.management_active = 1;

      vorbis_encode_ctl (&vorbisenc->vi, OV_ECTL_RATEMANAGE_SET, &ai);
    }
  } else {
    long min_bitrate, max_bitrate;

    min_bitrate = vorbisenc->min_bitrate > 0 ? vorbisenc->min_bitrate : -1;
    max_bitrate = vorbisenc->max_bitrate > 0 ? vorbisenc->max_bitrate : -1;

    if (vorbis_encode_setup_managed (&vorbisenc->vi,
            vorbisenc->channels,
            vorbisenc->frequency,
            max_bitrate, vorbisenc->bitrate, min_bitrate) != 0) {
      GST_ERROR_OBJECT (vorbisenc,
          "vorbis_encode_setup_managed "
          "(c %d, rate %d, max br %ld, br %ld, min br %ld) failed",
          vorbisenc->channels, vorbisenc->frequency, max_bitrate,
          vorbisenc->bitrate, min_bitrate);
      vorbis_info_clear (&vorbisenc->vi);
      return FALSE;
    }
  }

  if (vorbisenc->managed && vorbisenc->bitrate < 0) {
    vorbis_encode_ctl (&vorbisenc->vi, OV_ECTL_RATEMANAGE_AVG, NULL);
  } else if (!vorbisenc->managed) {
    /* Turn off management entirely (if it was turned on). */
    vorbis_encode_ctl (&vorbisenc->vi, OV_ECTL_RATEMANAGE_SET, NULL);
  }
  vorbis_encode_setup_init (&vorbisenc->vi);

  /* set up the analysis state and auxiliary encoding storage */
  vorbis_analysis_init (&vorbisenc->vd, &vorbisenc->vi);
  vorbis_block_init (&vorbisenc->vd, &vorbisenc->vb);

  vorbisenc->setup = TRUE;

  return TRUE;
}

/* prepare a buffer for transmission by passing data through libvorbis */
static GstBuffer *
gst_vorbisenc_buffer_from_packet (VorbisEnc * vorbisenc, ogg_packet * packet)
{
  GstBuffer *outbuf;

  outbuf = gst_buffer_new_and_alloc (packet->bytes);
  memcpy (GST_BUFFER_DATA (outbuf), packet->packet, packet->bytes);
  GST_BUFFER_OFFSET (outbuf) = vorbisenc->bytes_out;
  GST_BUFFER_OFFSET_END (outbuf) = packet->granulepos;
  GST_BUFFER_TIMESTAMP (outbuf) =
      vorbis_granule_time_copy (&vorbisenc->vd,
      packet->granulepos) * GST_SECOND;

  GST_DEBUG ("encoded buffer of %d bytes", GST_BUFFER_SIZE (outbuf));
  return outbuf;
}

/* push out the buffer and do internal bookkeeping */
static void
gst_vorbisenc_push_buffer (VorbisEnc * vorbisenc, GstBuffer * buffer)
{
  vorbisenc->bytes_out += GST_BUFFER_SIZE (buffer);

  if (GST_PAD_IS_USABLE (vorbisenc->srcpad)) {
    gst_pad_push (vorbisenc->srcpad, buffer);
  } else {
    gst_buffer_unref (buffer);
  }
}

static void
gst_vorbisenc_push_packet (VorbisEnc * vorbisenc, ogg_packet * packet)
{
  GstBuffer *outbuf;

  outbuf = gst_vorbisenc_buffer_from_packet (vorbisenc, packet);
  gst_vorbisenc_push_buffer (vorbisenc, outbuf);
}

static GstCaps *
gst_vorbisenc_set_header_on_caps (GstCaps * caps, GstBuffer * buf1,
    GstBuffer * buf2, GstBuffer * buf3)
{
  GstStructure *structure;
  GValue list = { 0 };
  GValue value = { 0 };

  caps = gst_caps_make_writable (caps);
  structure = gst_caps_get_structure (caps, 0);

  /* mark buffers */
  GST_BUFFER_FLAG_SET (buf1, GST_BUFFER_FLAG_IN_CAPS);
  GST_BUFFER_FLAG_SET (buf2, GST_BUFFER_FLAG_IN_CAPS);
  GST_BUFFER_FLAG_SET (buf3, GST_BUFFER_FLAG_IN_CAPS);

  /* put buffers in a fixed list */
  g_value_init (&list, GST_TYPE_ARRAY);
  g_value_init (&value, GST_TYPE_BUFFER);
  gst_value_set_buffer (&value, buf1);
  gst_value_list_append_value (&list, &value);
  g_value_unset (&value);
  g_value_init (&value, GST_TYPE_BUFFER);
  gst_value_set_buffer (&value, buf2);
  gst_value_list_append_value (&list, &value);
  g_value_unset (&value);
  g_value_init (&value, GST_TYPE_BUFFER);
  gst_value_set_buffer (&value, buf3);
  gst_value_list_append_value (&list, &value);
  gst_structure_set_value (structure, "streamheader", &list);
  g_value_unset (&value);
  g_value_unset (&list);

  return caps;
}

static gboolean
gst_vorbisenc_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  VorbisEnc *vorbisenc;

  vorbisenc = GST_VORBISENC (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      /* end of file.  this can be done implicitly in the mainline,
         but it's easier to see here in non-clever fashion.
         Tell the library we're at end of stream so that it can handle
         the last frame and mark end of stream in the output properly */
      vorbis_analysis_wrote (&vorbisenc->vd, 0);
      vorbisenc->eos = TRUE;
      gst_event_unref (event);
      break;
    case GST_EVENT_TAG:
      if (vorbisenc->tags) {
        GstTagList *list;

        gst_event_parse_tag (event, &list);
        gst_tag_list_insert (vorbisenc->tags, list,
            gst_tag_setter_get_merge_mode (GST_TAG_SETTER (vorbisenc)));
      } else {
        g_assert_not_reached ();
      }
      res = gst_pad_event_default (pad, event);
      break;
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }
  return res;
}

static GstFlowReturn
gst_vorbisenc_chain (GstPad * pad, GstBuffer * buffer)
{
  GstBuffer *buf = GST_BUFFER (buffer);
  VorbisEnc *vorbisenc;

  vorbisenc = GST_VORBISENC (GST_PAD_PARENT (pad));

  {
    gfloat *data;
    gulong size;
    gulong i, j;
    float **buffer;

    if (!vorbisenc->setup) {
      gst_buffer_unref (buf);
      GST_ELEMENT_ERROR (vorbisenc, CORE, NEGOTIATION, (NULL),
          ("encoder not initialized (input is not audio?)"));
      return GST_FLOW_UNEXPECTED;
    }

    if (!vorbisenc->header_sent) {
      //gint result;

      /* Vorbis streams begin with three headers; the initial header (with
         most of the codec setup parameters) which is mandated by the Ogg
         bitstream spec.  The second header holds any comment fields.  The
         third header holds the bitstream codebook.  We merely need to
         make the headers, then pass them to libvorbis one at a time;
         libvorbis handles the additional Ogg bitstream constraints */
      ogg_packet header;
      ogg_packet header_comm;
      ogg_packet header_code;
      GstBuffer *buf1, *buf2, *buf3;
      GstCaps *caps;

      gst_vorbisenc_set_metadata (vorbisenc);
      vorbis_analysis_headerout (&vorbisenc->vd, &vorbisenc->vc, &header,
          &header_comm, &header_code);

      /* create header buffers */
      buf1 = gst_vorbisenc_buffer_from_packet (vorbisenc, &header);
      buf2 = gst_vorbisenc_buffer_from_packet (vorbisenc, &header_comm);
      buf3 = gst_vorbisenc_buffer_from_packet (vorbisenc, &header_code);

      /* mark and put on caps */
      caps = gst_pad_get_caps (vorbisenc->srcpad);
      caps = gst_vorbisenc_set_header_on_caps (caps, buf1, buf2, buf3);

      /* negotiate with these caps */
      GST_DEBUG ("here are the caps: %" GST_PTR_FORMAT, caps);
      gst_pad_set_caps (vorbisenc->srcpad, caps);

      gst_buffer_set_caps (buf1, caps);
      gst_buffer_set_caps (buf2, caps);
      gst_buffer_set_caps (buf3, caps);

      /* push out buffers */
      gst_vorbisenc_push_buffer (vorbisenc, buf1);
      gst_vorbisenc_push_buffer (vorbisenc, buf2);
      gst_vorbisenc_push_buffer (vorbisenc, buf3);

      vorbisenc->header_sent = TRUE;
    }

    /* data to encode */
    data = (gfloat *) GST_BUFFER_DATA (buf);
    size = GST_BUFFER_SIZE (buf) / (vorbisenc->channels * sizeof (float));

    /* expose the buffer to submit data */
    buffer = vorbis_analysis_buffer (&vorbisenc->vd, size);

    /* uninterleave samples */
    for (i = 0; i < size; i++) {
      for (j = 0; j < vorbisenc->channels; j++) {
        buffer[j][i] = *data++;
      }
    }

    /* tell the library how much we actually submitted */
    vorbis_analysis_wrote (&vorbisenc->vd, size);

    vorbisenc->samples_in += size;

    gst_buffer_unref (buf);
  }

  /* vorbis does some data preanalysis, then divides up blocks for
     more involved (potentially parallel) processing.  Get a single
     block for encoding now */
  while (vorbis_analysis_blockout (&vorbisenc->vd, &vorbisenc->vb) == 1) {
    ogg_packet op;

    /* analysis */
    vorbis_analysis (&vorbisenc->vb, NULL);
    vorbis_bitrate_addblock (&vorbisenc->vb);

    while (vorbis_bitrate_flushpacket (&vorbisenc->vd, &op)) {
      gst_vorbisenc_push_packet (vorbisenc, &op);
    }
  }

  if (vorbisenc->eos) {
    /* clean up and exit.  vorbis_info_clear() must be called last */
    vorbis_block_clear (&vorbisenc->vb);
    vorbis_dsp_clear (&vorbisenc->vd);
    vorbis_info_clear (&vorbisenc->vi);
    gst_pad_push_event (vorbisenc->srcpad, gst_event_new_eos ());
    //gst_element_set_eos (GST_ELEMENT (vorbisenc));
  }
  return GST_FLOW_OK;
}

static void
gst_vorbisenc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  VorbisEnc *vorbisenc;

  g_return_if_fail (GST_IS_VORBISENC (object));

  vorbisenc = GST_VORBISENC (object);

  switch (prop_id) {
    case ARG_MAX_BITRATE:
      g_value_set_int (value, vorbisenc->max_bitrate);
      break;
    case ARG_BITRATE:
      g_value_set_int (value, vorbisenc->bitrate);
      break;
    case ARG_MIN_BITRATE:
      g_value_set_int (value, vorbisenc->min_bitrate);
      break;
    case ARG_QUALITY:
      g_value_set_float (value, vorbisenc->quality);
      break;
    case ARG_MANAGED:
      g_value_set_boolean (value, vorbisenc->managed);
      break;
    case ARG_LAST_MESSAGE:
      g_value_set_string (value, vorbisenc->last_message);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vorbisenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  VorbisEnc *vorbisenc;

  g_return_if_fail (GST_IS_VORBISENC (object));

  vorbisenc = GST_VORBISENC (object);

  switch (prop_id) {
    case ARG_MAX_BITRATE:
    {
      gboolean old_value = vorbisenc->managed;

      vorbisenc->max_bitrate = g_value_get_int (value);
      if (vorbisenc->max_bitrate >= 0
          && vorbisenc->max_bitrate < LOWEST_BITRATE) {
        g_warning ("Lowest allowed bitrate is %d", LOWEST_BITRATE);
        vorbisenc->max_bitrate = LOWEST_BITRATE;
      }
      if (vorbisenc->min_bitrate > 0 && vorbisenc->max_bitrate > 0)
        vorbisenc->managed = TRUE;
      else
        vorbisenc->managed = FALSE;

      if (old_value != vorbisenc->managed)
        g_object_notify (object, "managed");
      break;
    }
    case ARG_BITRATE:
      vorbisenc->bitrate = g_value_get_int (value);
      if (vorbisenc->bitrate >= 0 && vorbisenc->bitrate < LOWEST_BITRATE) {
        g_warning ("Lowest allowed bitrate is %d", LOWEST_BITRATE);
        vorbisenc->bitrate = LOWEST_BITRATE;
      }
      break;
    case ARG_MIN_BITRATE:
    {
      gboolean old_value = vorbisenc->managed;

      vorbisenc->min_bitrate = g_value_get_int (value);
      if (vorbisenc->min_bitrate >= 0
          && vorbisenc->min_bitrate < LOWEST_BITRATE) {
        g_warning ("Lowest allowed bitrate is %d", LOWEST_BITRATE);
        vorbisenc->min_bitrate = LOWEST_BITRATE;
      }
      if (vorbisenc->min_bitrate > 0 && vorbisenc->max_bitrate > 0)
        vorbisenc->managed = TRUE;
      else
        vorbisenc->managed = FALSE;

      if (old_value != vorbisenc->managed)
        g_object_notify (object, "managed");
      break;
    }
    case ARG_QUALITY:
      vorbisenc->quality = g_value_get_float (value);
      if (vorbisenc->quality >= 0.0)
        vorbisenc->quality_set = TRUE;
      else
        vorbisenc->quality_set = FALSE;
      break;
    case ARG_MANAGED:
      vorbisenc->managed = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_vorbisenc_change_state (GstElement * element)
{
  VorbisEnc *vorbisenc = GST_VORBISENC (element);
  GstElementState transition;
  GstElementStateReturn res;

  transition = GST_STATE_TRANSITION (element);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
    case GST_STATE_READY_TO_PAUSED:
      vorbisenc->eos = FALSE;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
    default:
      break;
  }

  res = GST_ELEMENT_CLASS (parent_class)->change_state (element);

  switch (transition) {
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      vorbisenc->setup = FALSE;
      vorbisenc->header_sent = FALSE;
      gst_tag_list_free (vorbisenc->tags);
      vorbisenc->tags = gst_tag_list_new ();
      break;
    case GST_STATE_READY_TO_NULL:
    default:
      break;
  }

  return res;
}
