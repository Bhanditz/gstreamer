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
/* Element-Checklist-Version: 5 */

/* 2001/04/03 - Updated parseau to use caps nego
 *              Zaheer Merali <zaheer@grid9.net
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <string.h>

#include <gstauparse.h>
#include <gst/audio/audio.h>

/* elementfactory information */
static GstElementDetails gst_auparse_details =
GST_ELEMENT_DETAILS (".au parser",
    "Codec/Parser/Audio",
    "Parse an .au file into raw audio",
    "Erik Walthinsen <omega@cse.ogi.edu>");

static GstStaticPadTemplate gst_auparse_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-au")
    );

static GstStaticPadTemplate gst_auparse_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,          /* FIXME: spider */
    GST_STATIC_CAPS (GST_AUDIO_INT_PAD_TEMPLATE_CAPS "; "       /* does not support 24bit without patch */
        GST_AUDIO_FLOAT_PAD_TEMPLATE_CAPS "; "
        "audio/x-alaw, "
        "rate = (int) [ 8000, 48000 ], "
        "channels = (int) [ 1, 2 ]; "
        "audio/x-mulaw, "
        "rate = (int) [ 8000, 48000 ], " "channels = (int) [ 1, 2 ]")
    );

/* AuParse signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  /* FILL ME */
};

static void gst_auparse_base_init (gpointer g_class);
static void gst_auparse_class_init (GstAuParseClass * klass);
static void gst_auparse_init (GstAuParse * auparse);

static void gst_auparse_chain (GstPad * pad, GstData * _data);

static GstElementStateReturn gst_auparse_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

/*static guint gst_auparse_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_auparse_get_type (void)
{
  static GType auparse_type = 0;

  if (!auparse_type) {
    static const GTypeInfo auparse_info = {
      sizeof (GstAuParseClass),
      gst_auparse_base_init,
      NULL,
      (GClassInitFunc) gst_auparse_class_init,
      NULL,
      NULL,
      sizeof (GstAuParse),
      0,
      (GInstanceInitFunc) gst_auparse_init,
    };

    auparse_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstAuParse", &auparse_info,
        0);
  }
  return auparse_type;
}

static void
gst_auparse_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_auparse_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_auparse_src_template));
  gst_element_class_set_details (element_class, &gst_auparse_details);

}

static void
gst_auparse_class_init (GstAuParseClass * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_auparse_change_state;
}

static void
gst_auparse_init (GstAuParse * auparse)
{
  auparse->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_auparse_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (auparse), auparse->sinkpad);
  gst_pad_set_chain_function (auparse->sinkpad, gst_auparse_chain);

  auparse->srcpad = NULL;
#if 0                           /* FIXME: spider */
  gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_auparse_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (auparse), auparse->srcpad);
  gst_pad_use_explicit_caps (auparse->srcpad);
#endif

  auparse->offset = 0;
  auparse->size = 0;
  auparse->encoding = 0;
  auparse->frequency = 0;
  auparse->channels = 0;
}

static void
gst_auparse_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstAuParse *auparse;
  gchar *data;
  glong size;
  GstCaps *tempcaps;
  gint law = 0, depth, ieee = 0;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  auparse = GST_AUPARSE (gst_pad_get_parent (pad));

  GST_DEBUG ("gst_auparse_chain: got buffer in '%s'",
      gst_element_get_name (GST_ELEMENT (auparse)));

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  /* if we haven't seen any data yet... */
  if (auparse->size == 0) {
    GstBuffer *newbuf;
    guint32 *head = (guint32 *) data;

    /* normal format is big endian (au is a Sparc format) */
    if (GUINT32_FROM_BE (*head) == 0x2e736e64) {
      head++;
      auparse->le = 0;
      auparse->offset = GUINT32_FROM_BE (*head);
      head++;
      auparse->size = GUINT32_FROM_BE (*head);
      head++;
      auparse->encoding = GUINT32_FROM_BE (*head);
      head++;
      auparse->frequency = GUINT32_FROM_BE (*head);
      head++;
      auparse->channels = GUINT32_FROM_BE (*head);
      head++;

      /* and of course, someone had to invent a little endian
       * version.  Used by DEC systems. */
    } else if (GUINT32_FROM_LE (*head) == 0x0064732E) {
      head++;
      auparse->le = 1;
      auparse->offset = GUINT32_FROM_LE (*head);
      head++;
      auparse->size = GUINT32_FROM_LE (*head);
      head++;
      auparse->encoding = GUINT32_FROM_LE (*head);
      head++;
      auparse->frequency = GUINT32_FROM_LE (*head);
      head++;
      auparse->channels = GUINT32_FROM_LE (*head);
      head++;

    } else {
      g_warning ("help, dunno what I'm looking at!\n");
      gst_buffer_unref (buf);
      return;
    }

    GST_DEBUG
        ("offset %ld, size %ld, encoding %ld, frequency %ld, channels %ld",
        auparse->offset, auparse->size, auparse->encoding, auparse->frequency,
        auparse->channels);

/*
Docs :
	http://www.opengroup.org/public/pubs/external/auformat.html
	http://astronomy.swin.edu.au/~pbourke/dataformats/au/
Samples :
	http://www.tsp.ece.mcgill.ca/MMSP/Documents/AudioFormats/AU/Samples.html
*/

    switch (auparse->encoding) {

      case 1:                  /* 8-bit ISDN mu-law */
        law = 1;
        depth = 8;
        break;

      case 2:                  /* 8-bit linear PCM */
        depth = 8;
        break;
      case 3:                  /* 16-bit linear PCM */
        depth = 16;
        break;
      case 4:                  /* 24-bit linear PCM */
        depth = 24;
        break;
      case 5:                  /* 32-bit linear PCM */
        depth = 32;
        break;

      case 27:                 /* 8-bit ISDN a-law */
        law = 2;
        depth = 8;
        break;

      case 6:                  /* 32 bit IEEE floating point */
        ieee = 1;
        depth = 32;
        break;
      case 7:                  /* 64 bit IEEE floating point */
        ieee = 1;
        depth = 64;
        break;

      case 23:                 /* 4 bit CCITT G721 ADPCM */
      case 24:                 /* CCITT G722 ADPCM */
      case 25:                 /* CCITT G723 ADPCM */
      case 26:                 /* 5 bit CCITT G723 ADPCM */

      default:
        GST_ELEMENT_ERROR (auparse, STREAM, FORMAT, (NULL), (NULL));
        gst_buffer_unref (buf);
        return;
    }

    auparse->srcpad =
        gst_pad_new_from_template (gst_static_pad_template_get
        (&gst_auparse_src_template), "src");
    gst_pad_use_explicit_caps (auparse->srcpad);

    if (law) {
      tempcaps =
          gst_caps_new_simple ((law == 1) ? "audio/x-mulaw" : "audio/x-alaw",
          "rate", G_TYPE_INT, auparse->frequency, "channels", G_TYPE_INT,
          auparse->channels, NULL);
    } else if (ieee) {
      tempcaps = gst_caps_new_simple ("audio/x-raw-float",
          "width", G_TYPE_INT, depth,
          "endianness", G_TYPE_INT,
          auparse->le ? G_LITTLE_ENDIAN : G_BIG_ENDIAN, NULL);
    } else {
      tempcaps = gst_caps_new_simple ("audio/x-raw-int",
          "endianness", G_TYPE_INT,
          auparse->le ? G_LITTLE_ENDIAN : G_BIG_ENDIAN, "rate", G_TYPE_INT,
          auparse->frequency, "channels", G_TYPE_INT, auparse->channels,
          "depth", G_TYPE_INT, depth, "width", G_TYPE_INT, depth, "signed",
          G_TYPE_BOOLEAN, TRUE, NULL);
    }

    if (!gst_pad_set_explicit_caps (auparse->srcpad, tempcaps)) {
      gst_buffer_unref (buf);
      gst_object_unref (GST_OBJECT (auparse->srcpad));
      auparse->srcpad = NULL;
      return;
    }

    gst_element_add_pad (GST_ELEMENT (auparse), auparse->srcpad);

    newbuf = gst_buffer_new ();
    GST_BUFFER_DATA (newbuf) = (gpointer) malloc (size - (auparse->offset));
    memcpy (GST_BUFFER_DATA (newbuf), data + 24, size - (auparse->offset));
    GST_BUFFER_SIZE (newbuf) = size - (auparse->offset);

    gst_buffer_unref (buf);

    gst_pad_push (auparse->srcpad, GST_DATA (newbuf));
    return;
  }

  gst_pad_push (auparse->srcpad, GST_DATA (buf));
}

static GstElementStateReturn
gst_auparse_change_state (GstElement * element)
{
  GstAuParse *auparse = GST_AUPARSE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      if (auparse->srcpad) {
        gst_element_remove_pad (element, auparse->srcpad);
        auparse->srcpad = NULL;
      }
      break;
    default:
      break;
  }

  if (parent_class->change_state)
    return parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "auparse", GST_RANK_SECONDARY,
          GST_TYPE_AUPARSE)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "auparse",
    "parses au streams", plugin_init, VERSION, "GPL", GST_PACKAGE, GST_ORIGIN)
