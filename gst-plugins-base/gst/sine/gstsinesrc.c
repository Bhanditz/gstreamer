/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2001 Steve Baker <stevebaker_org@yahoo.co.uk>
 *
 * gstsinesrc.c: 
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
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <gst/control/control.h>

#include <gstsinesrc.h>

/* elementfactory information */
GstElementDetails gst_sinesrc_details = {
  "Sine-wave src",
  "Source/Audio",
  "Create a sine wave of a given frequency and volume",
  "Erik Walthinsen <omega@cse.ogi.edu>"
};


/* SineSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_TABLESIZE,
  ARG_SAMPLES_PER_BUFFER,
  ARG_FREQ,
  ARG_VOLUME,
};

static GstStaticPadTemplate gst_sinesrc_src_template =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("audio/x-raw-int, "
      "endianness = (int) BYTE_ORDER, "
      "signed = (boolean) true, "
      "width = (int) 16, "
      "depth = (int) 16, "
      "rate = (int) [ 8000, 48000 ], "
      "channels = (int) 1"
  )
);

static void 	    gst_sinesrc_class_init          (GstSineSrcClass *klass);
static void 	    gst_sinesrc_base_init           (GstSineSrcClass *klass);
static void 	    gst_sinesrc_init	            (GstSineSrc *src);
static void 	    gst_sinesrc_set_property        (GObject *object, 
		                                     guint prop_id, 
					             const GValue *value, 
					             GParamSpec *pspec);
static void         gst_sinesrc_get_property        (GObject *object, 
		                                     guint prop_id, 
					             GValue *value, 
					             GParamSpec *pspec);
static GstPadLinkReturn
                    gst_sinesrc_link                (GstPad  *pad,
						     const GstCaps *caps);
static GstElementStateReturn
                    gst_sinesrc_change_state        (GstElement *element);

static void 	    gst_sinesrc_update_freq         (const GValue *value, 
		                                     gpointer data);
static void 	    gst_sinesrc_populate_sinetable  (GstSineSrc *src);
static inline void  gst_sinesrc_update_table_inc    (GstSineSrc *src);

static const GstQueryType *
		    gst_sinesrc_get_query_types     (GstPad      *pad);
static gboolean     gst_sinesrc_src_query           (GstPad      *pad,
						     GstQueryType type,
						     GstFormat   *format,
						     gint64      *value);

static GstData*   gst_sinesrc_get		    (GstPad *pad);
static GstCaps * gst_sinesrc_src_fixate (GstPad *pad, const GstCaps *caps, gpointer user_data);

static GstElementClass *parent_class = NULL;
/*static guint gst_sinesrc_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_sinesrc_get_type (void)
{
  static GType sinesrc_type = 0;

  if (!sinesrc_type) {
    static const GTypeInfo sinesrc_info = {
      sizeof (GstSineSrcClass),
      (GBaseInitFunc) gst_sinesrc_base_init, NULL,
      (GClassInitFunc) gst_sinesrc_class_init, NULL, NULL,
      sizeof (GstSineSrc), 0,
      (GInstanceInitFunc) gst_sinesrc_init,
    };
    sinesrc_type = g_type_register_static (GST_TYPE_ELEMENT, "GstSineSrc", 
		                           &sinesrc_info, 0);
  }
  return sinesrc_type;
}

static void
gst_sinesrc_base_init (GstSineSrcClass *klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_sinesrc_src_template));
  gst_element_class_set_details (element_class, &gst_sinesrc_details);
}

static void
gst_sinesrc_class_init (GstSineSrcClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TABLESIZE,
    g_param_spec_int ("tablesize", "tablesize", "tablesize",
                      1, G_MAXINT, 1024, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), 
		                   ARG_SAMPLES_PER_BUFFER,
    g_param_spec_int ("samplesperbuffer", "Samples per buffer", 
	              "Number of samples in each outgoing buffer",
                      1, G_MAXINT, 1024, G_PARAM_READWRITE)); 
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FREQ,
    g_param_spec_double ("freq", "Frequency", "Frequency of sine source",
                        0.0, 20000.0, 440.0, G_PARAM_READWRITE)); 
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VOLUME,
    g_param_spec_double ("volume", "Volume", "Volume",
                        0.0, 1.0, 0.8, G_PARAM_READWRITE)); 
                                     
  gobject_class->set_property = gst_sinesrc_set_property;
  gobject_class->get_property = gst_sinesrc_get_property;

  gstelement_class->change_state = gst_sinesrc_change_state;
}

static void 
gst_sinesrc_init (GstSineSrc *src) 
{
  src->srcpad = gst_pad_new_from_template (
      gst_static_pad_template_get (&gst_sinesrc_src_template), "src");
  gst_pad_set_link_function (src->srcpad, gst_sinesrc_link);
  gst_pad_set_fixate_function (src->srcpad, gst_sinesrc_src_fixate);
  gst_element_add_pad (GST_ELEMENT(src), src->srcpad);
  
  gst_pad_set_get_function (src->srcpad, gst_sinesrc_get);
  gst_pad_set_link_function (src->srcpad, gst_sinesrc_link);
  gst_pad_set_query_function (src->srcpad, gst_sinesrc_src_query);
  gst_pad_set_query_type_function (src->srcpad, gst_sinesrc_get_query_types);

  src->samplerate = 44100;
  src->volume = 1.0;
  src->freq = 440.0;
  
  src->table_pos = 0.0;
  src->table_size = 1024;
  src->samples_per_buffer=1024;
  src->timestamp=0LLU;
  src->offset=0LLU;
  
  src->seq = 0;

  src->dpman = gst_dpman_new ("sinesrc_dpman", GST_ELEMENT(src));

  gst_dpman_add_required_dparam_callback (
    src->dpman, 
    g_param_spec_double("freq","Frequency (Hz)","Frequency of the tone",
                       10.0, 10000.0, 350.0, G_PARAM_READWRITE),
    "hertz",
    gst_sinesrc_update_freq, 
    src
  );
  
  gst_dpman_add_required_dparam_direct (
    src->dpman, 
    g_param_spec_double("volume","Volume","Volume of the tone",
                       0.0, 1.0, 0.8, G_PARAM_READWRITE),
    "scalar",
    &(src->volume)
  );
  
  gst_dpman_set_rate(src->dpman, src->samplerate);

  gst_sinesrc_populate_sinetable(src);
  gst_sinesrc_update_table_inc(src);

}

static GstCaps *
gst_sinesrc_src_fixate (GstPad *pad, const GstCaps *caps,
    gpointer user_data)
{
  GstStructure *structure;
  GstCaps *newcaps;

  structure = gst_structure_copy (gst_caps_get_structure (caps, 0));
  newcaps = gst_caps_new_full (structure, NULL);

  if (gst_caps_structure_fixate_field_nearest_int (structure, "rate", 44100)) {
    return newcaps;
  }

  gst_caps_free (newcaps);
  return NULL;
}

static GstPadLinkReturn
gst_sinesrc_link (GstPad *pad, const GstCaps *caps)
{
  GstSineSrc *sinesrc;
  const GstStructure *structure;
  gboolean ret;

  GST_DEBUG ("gst_sinesrc_src_link");
  sinesrc = GST_SINESRC (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  ret = gst_structure_get_int (structure, "rate", &sinesrc->samplerate);

  if (!ret) return GST_PAD_LINK_REFUSED;

  return GST_PAD_LINK_OK;
}

static const GstQueryType *
gst_sinesrc_get_query_types (GstPad *pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_POSITION,
    0,
  };

  return query_types;
} 

static gboolean
gst_sinesrc_src_query (GstPad      *pad,
		       GstQueryType type,
		       GstFormat   *format,
		       gint64      *value)
{
  gboolean res = FALSE;
  GstSineSrc *src;
	      
  src = GST_SINESRC (gst_pad_get_parent (pad));
	        
  switch (type) {
    case GST_QUERY_POSITION:
      switch (*format) {
        case GST_FORMAT_TIME:
          *value = src->timestamp;
          res = TRUE;
          break;
        case GST_FORMAT_DEFAULT: /* samples */
          *value = src->offset / 2; /* 16bpp audio */
          res = TRUE;
          break;
        case GST_FORMAT_BYTES:
          *value = src->offset;
          res = TRUE;
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }

  return res;
}

static GstData *
gst_sinesrc_get (GstPad *pad)
{
  GstSineSrc *src;
  GstBuffer *buf;
  guint tdiff;
  
  gint16 *samples;
  gint i=0;
  
  g_return_val_if_fail (pad != NULL, NULL);
  src = GST_SINESRC (gst_pad_get_parent (pad));

  tdiff = src->samples_per_buffer * GST_SECOND / src->samplerate;

  /* FIXME the 1024 is arbitrary */
  buf = gst_buffer_new_and_alloc (1024);

  GST_BUFFER_TIMESTAMP(buf) = src->timestamp;
  GST_BUFFER_OFFSET (buf) = src->offset;
  GST_BUFFER_DURATION (buf) = tdiff;

  samples = (gint16 *) GST_BUFFER_DATA(buf);

  GST_DPMAN_PREPROCESS(src->dpman, src->samples_per_buffer, src->timestamp);
  
  src->timestamp += tdiff;
  src->offset += GST_BUFFER_SIZE (buf);
   
  while(GST_DPMAN_PROCESS(src->dpman, i)) {
#if 0
    src->table_lookup = (gint)(src->table_pos);
    src->table_lookup_next = src->table_lookup + 1;
    src->table_interp = src->table_pos - src->table_lookup;

    /* wrap the array lookups if we're out of bounds */
    if (src->table_lookup_next >= src->table_size){
      src->table_lookup_next -= src->table_size;
      if (src->table_lookup >= src->table_size){
        src->table_lookup -= src->table_size;
        src->table_pos -= src->table_size;
      }
    }
    
    src->table_pos += src->table_inc;

    /*no interpolation */
    /*samples[i] = src->table_data[src->table_lookup] */
    /*               * src->volume * 32767.0; */

    /*linear interpolation */
    samples[i] = ((src->table_interp
                   *(src->table_data[src->table_lookup_next]
                    -src->table_data[src->table_lookup]
                    )
                  )+src->table_data[src->table_lookup]
                 )* src->volume * 32767.0;
#endif
    src->accumulator += 2*M_PI*src->freq / src->samplerate;
    if(src->accumulator >= 2*M_PI){
      src->accumulator -= 2*M_PI;
    }
    samples[i] = sin(src->accumulator) * src->volume * 32767.0;

    i++;
  }

  if (!GST_PAD_CAPS (src->srcpad)) {
    if (gst_sinesrc_link (src->srcpad,
			  gst_pad_get_allowed_caps (src->srcpad)) <= 0) {
      gst_element_error (GST_ELEMENT (src), "Could not set caps");
      return NULL;
    }
  }

  return GST_DATA (buf);
}

static void 
gst_sinesrc_set_property (GObject *object, guint prop_id, 
		          const GValue *value, GParamSpec *pspec) 
{
  GstSineSrc *src;

  g_return_if_fail (GST_IS_SINESRC (object));
  src = GST_SINESRC (object);

  switch (prop_id) {
    case ARG_TABLESIZE:
      src->table_size = g_value_get_int (value);
      gst_sinesrc_populate_sinetable (src);
      gst_sinesrc_update_table_inc (src);
      break;
    case ARG_SAMPLES_PER_BUFFER:
      src->samples_per_buffer = g_value_get_int (value);
      break;
    case ARG_FREQ:
      gst_dpman_bypass_dparam (src->dpman, "freq");
      gst_sinesrc_update_freq (value, src);      
      break;
    case ARG_VOLUME:
      gst_dpman_bypass_dparam (src->dpman, "volume");
      src->volume = g_value_get_double (value);
      break;
    default:
      break;
  }
}

static void 
gst_sinesrc_get_property (GObject *object, guint prop_id, 
		          GValue *value, GParamSpec *pspec) 
{
  GstSineSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_SINESRC(object));
  src = GST_SINESRC(object);

  switch (prop_id) {
    case ARG_TABLESIZE:
      g_value_set_int (value, src->table_size);
      break;
    case ARG_SAMPLES_PER_BUFFER:
      g_value_set_int (value, src->samples_per_buffer);
      break;
    case ARG_FREQ:
      g_value_set_double (value, src->freq);
      break;
    case ARG_VOLUME:
      g_value_set_double (value, src->volume);
      break;      
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_sinesrc_change_state (GstElement *element)
{
  GstSineSrc *src = GST_SINESRC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      src->timestamp = 0LLU;
      src->offset = 0LLU;
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS(parent_class)->change_state)
    return GST_ELEMENT_CLASS(parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static void 
gst_sinesrc_populate_sinetable (GstSineSrc *src)
{
  gint i;
  gdouble pi2scaled = M_PI * 2 / src->table_size;
  gdouble *table = g_new (gdouble, src->table_size);

  for(i=0 ; i < src->table_size ; i++){
    table[i] = (gdouble) sin(i * pi2scaled);
  }
  
  g_free (src->table_data);
  src->table_data = table;
}

static void
gst_sinesrc_update_freq (const GValue *value, gpointer data)
{
  GstSineSrc *src = (GstSineSrc *) data;
  g_return_if_fail (GST_IS_SINESRC (src));

  src->freq = g_value_get_double (value);
  src->table_inc = src->table_size * src->freq / src->samplerate;
  
  /*GST_DEBUG ("freq %f", src->freq); */
}

static inline void 
gst_sinesrc_update_table_inc (GstSineSrc *src)
{
  src->table_inc = src->table_size * src->freq / src->samplerate;
}

#if 0
static gboolean 
gst_sinesrc_force_caps (GstSineSrc *src)
{
  static GstStaticCaps static_caps = GST_STATIC_CAPS ("audio/x-raw-int, "
      "endianness = (int) BYTE_ORDER, "
      "signed = (boolean) true, "
      "width = (int) 16, "
      "depth = (int) 16, "
      "rate = (int) [ 8000, 48000 ], "
      "channels = (int) 1"
  );
  GstCaps *caps;
  GstStructure *structure;

  if (!src->newcaps)
    return TRUE;
  
  caps = gst_caps_copy (gst_static_caps_get (&static_caps));

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_set (structure, "rate", G_TYPE_INT, src->samplerate, NULL);
  
  src->newcaps = gst_pad_try_set_caps (src->srcpad, caps) < GST_PAD_LINK_OK;

  return !src->newcaps;
}
#endif

static gboolean
plugin_init (GstPlugin *plugin)
{
  /* initialize dparam support library */
  gst_control_init(NULL,NULL);

  return gst_element_register (plugin, "sinesrc",
			       GST_RANK_NONE, GST_TYPE_SINESRC);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "sine",
  "Sine audio wave generator",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN
)
