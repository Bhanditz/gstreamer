/* GStreamer
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 * PCM - A-Law conversion
 *   Copyright (C) 2000 by Abramo Bagnara <abramo@alsa-project.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include <gst/gst.h>
#include "alaw-decode.h"

extern GstPadTemplate *alawdec_src_template, *alawdec_sink_template;

/* Stereo signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static void gst_alawdec_class_init (GstALawDecClass * klass);
static void gst_alawdec_base_init (GstALawDecClass * klass);
static void gst_alawdec_init (GstALawDec * alawdec);

static void gst_alawdec_chain (GstPad * pad, GstData * _data);

static GstElementClass *parent_class = NULL;

/*static guint gst_stereo_signals[LAST_SIGNAL] = { 0 }; */

/*
 * alaw_to_s16() - Convert an A-law value to 16-bit linear PCM
 *
 */
static gint
alaw_to_s16 (guint8 a_val)
{
  gint t;
  gint seg;

  a_val ^= 0x55;
  t = a_val & 0x7f;
  if (t < 16)
    t = (t << 4) + 8;
  else {
    seg = (t >> 4) & 0x07;
    t = ((t & 0x0f) << 4) + 0x108;
    t <<= seg - 1;
  }
  return ((a_val & 0x80) ? t : -t);
}

static GstCaps *
alawdec_getcaps (GstPad * pad)
{
  GstALawDec *alawdec = GST_ALAWDEC (gst_pad_get_parent (pad));
  GstPad *otherpad;
  GstCaps *base_caps, *othercaps;
  GstStructure *structure;
  const GValue *rate, *chans;

  if (pad == alawdec->sinkpad) {
    otherpad = alawdec->srcpad;
    base_caps = gst_caps_new_simple ("audio/x-alaw", NULL);
  } else {
    otherpad = alawdec->sinkpad;
    base_caps = gst_caps_new_simple ("audio/x-raw-int",
        "width", G_TYPE_INT, 16, "depth", G_TYPE_INT, 16,
        "signed", G_TYPE_BOOLEAN, TRUE,
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "signed", G_TYPE_BOOLEAN, TRUE, NULL);
  }
  othercaps = gst_pad_get_allowed_caps (otherpad);

  /* Not fully correct, but usually, all structures in a caps have
   * the same samplerate and channels range. */
  structure = gst_caps_get_structure (othercaps, 0);
  rate = gst_structure_get_value (structure, "rate");
  chans = gst_structure_get_value (structure, "channels");
  if (!rate || !chans)
    return gst_caps_new_empty ();

  /* Set the samplerate/channels on the to-be-returned caps */
  structure = gst_caps_get_structure (base_caps, 0);
  gst_structure_set_value (structure, "rate", rate);
  gst_structure_set_value (structure, "channels", chans);
  gst_caps_free (othercaps);

  return base_caps;
}

static GstPadLinkReturn
alawdec_link (GstPad * pad, const GstCaps * caps)
{
  GstALawDec *alawdec = GST_ALAWDEC (gst_pad_get_parent (pad));
  GstPad *otherpad;
  GstStructure *structure;
  const GValue *rate, *chans;
  GstCaps *base_caps;

  structure = gst_caps_get_structure (caps, 0);
  rate = gst_structure_get_value (structure, "rate");
  chans = gst_structure_get_value (structure, "channels");
  if (!rate || !chans)
    return GST_PAD_LINK_REFUSED;

  if (pad == alawdec->srcpad) {
    otherpad = alawdec->sinkpad;
    base_caps = gst_caps_new_simple ("audio/x-alaw", NULL);
  } else {
    otherpad = alawdec->srcpad;
    base_caps = gst_caps_new_simple ("audio/x-raw-int",
        "width", G_TYPE_INT, 16, "depth", G_TYPE_INT, 16,
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "signed", G_TYPE_BOOLEAN, TRUE, NULL);
  }

  structure = gst_caps_get_structure (base_caps, 0);
  gst_structure_set_value (structure, "rate", rate);
  gst_structure_set_value (structure, "channels", chans);

  return gst_pad_try_set_caps (otherpad, base_caps);
}

GType
gst_alawdec_get_type (void)
{
  static GType alawdec_type = 0;

  if (!alawdec_type) {
    static const GTypeInfo alawdec_info = {
      sizeof (GstALawDecClass),
      (GBaseInitFunc) gst_alawdec_base_init,
      NULL,
      (GClassInitFunc) gst_alawdec_class_init,
      NULL,
      NULL,
      sizeof (GstALawDec),
      0,
      (GInstanceInitFunc) gst_alawdec_init,
    };

    alawdec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstALawDec", &alawdec_info,
        0);
  }
  return alawdec_type;
}

static void
gst_alawdec_base_init (GstALawDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstElementDetails alawdec_details = {
    "A Law to PCM conversion",
    "Codec/Decoder/Audio",
    "Convert 8bit A law to 16bit PCM",
    "Zaheer Merali <zaheer@bellworldwide.net>"
  };

  gst_element_class_add_pad_template (element_class, alawdec_src_template);
  gst_element_class_add_pad_template (element_class, alawdec_sink_template);
  gst_element_class_set_details (element_class, &alawdec_details);
}

static void
gst_alawdec_class_init (GstALawDecClass * klass)
{
  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
}

static void
gst_alawdec_init (GstALawDec * alawdec)
{
  alawdec->sinkpad = gst_pad_new_from_template (alawdec_sink_template, "sink");
  gst_pad_set_link_function (alawdec->sinkpad, alawdec_link);
  gst_pad_set_getcaps_function (alawdec->sinkpad, alawdec_getcaps);
  gst_pad_set_chain_function (alawdec->sinkpad, gst_alawdec_chain);
  gst_element_add_pad (GST_ELEMENT (alawdec), alawdec->sinkpad);

  alawdec->srcpad = gst_pad_new_from_template (alawdec_src_template, "src");
  gst_pad_set_link_function (alawdec->srcpad, alawdec_link);
  gst_pad_set_getcaps_function (alawdec->srcpad, alawdec_getcaps);
  gst_element_add_pad (GST_ELEMENT (alawdec), alawdec->srcpad);
}

static void
gst_alawdec_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstALawDec *alawdec;
  gint16 *linear_data;
  guint8 *alaw_data;
  GstBuffer *outbuf;
  gint i;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  alawdec = GST_ALAWDEC (GST_OBJECT_PARENT (pad));
  g_return_if_fail (alawdec != NULL);
  g_return_if_fail (GST_IS_ALAWDEC (alawdec));

  alaw_data = (guint8 *) GST_BUFFER_DATA (buf);
  outbuf = gst_buffer_new_and_alloc (GST_BUFFER_SIZE (buf) * 2);
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buf);
  linear_data = (gint16 *) GST_BUFFER_DATA (outbuf);

  for (i = 0; i < GST_BUFFER_SIZE (buf); i++) {
    *linear_data = alaw_to_s16 (*alaw_data);
    linear_data++;
    alaw_data++;
  }

  gst_buffer_unref (buf);
  gst_pad_push (alawdec->srcpad, GST_DATA (outbuf));
}
