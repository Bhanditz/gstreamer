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

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <gst/control/control.h>

#include <gstsinesrc.h>

/* elementfactory information */
GstElementDetails gst_sinesrc_details = {
  "Sine-wave src",
  "Source/Audio",
  "LGPL",
  "Create a sine wave of a given frequency and volume",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


/* SineSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* ARG_WIDTH, */ /* width is not even implemented so no use in having this */
  ARG_SAMPLERATE,
  ARG_TABLESIZE,
  ARG_SAMPLES_PER_BUFFER,
  ARG_FREQ,
  ARG_VOLUME,
};

GST_PAD_TEMPLATE_FACTORY (sinesrc_src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "sinesrc_src",
    "audio/raw",
      "format",   	GST_PROPS_STRING ("int"),
      "law",     	GST_PROPS_INT (0),
      "endianness",	GST_PROPS_INT (G_BYTE_ORDER),
      "signed",   	GST_PROPS_BOOLEAN (TRUE),
      "width",   	GST_PROPS_INT (16),
      "depth",    	GST_PROPS_INT (16),
      "rate",     	GST_PROPS_INT_RANGE (8000, 48000),
      "channels", 	GST_PROPS_INT (1)
  )
);

static void 	    gst_sinesrc_class_init          (GstSineSrcClass *klass);
static void 	    gst_sinesrc_init	            (GstSineSrc *src);
static void 	    gst_sinesrc_set_property        (GObject *object, 
		                                     guint prop_id, 
					             const GValue *value, 
					             GParamSpec *pspec);
static void         gst_sinesrc_get_property        (GObject *object, 
		                                     guint prop_id, 
					             GValue *value, 
					             GParamSpec *pspec);
/*static gboolean gst_sinesrc_change_state(GstElement *element, */
/*                                          GstElementState state); */
/*static void gst_sinesrc_close_audio(GstSineSrc *src); */
/*static gboolean gst_sinesrc_open_audio(GstSineSrc *src); */

static void 	    gst_sinesrc_update_freq         (const GValue *value, 
		                                     gpointer data);
static void 	    gst_sinesrc_populate_sinetable  (GstSineSrc *src);
static inline void  gst_sinesrc_update_table_inc    (GstSineSrc *src);
static void 	    gst_sinesrc_force_caps	    (GstSineSrc *src);

static GstBuffer*   gst_sinesrc_get		    (GstPad *pad);

static GstElementClass *parent_class = NULL;
/*static guint gst_sinesrc_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_sinesrc_get_type (void)
{
  static GType sinesrc_type = 0;

  if (!sinesrc_type) {
    static const GTypeInfo sinesrc_info = {
      sizeof (GstSineSrcClass), NULL, NULL,
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
gst_sinesrc_class_init (GstSineSrcClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  /*
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_WIDTH,
    g_param_spec_int("width", "Width", "Width of audio data in bits",
                     1, 32, 0, G_PARAM_READWRITE));
		     */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SAMPLERATE,
    g_param_spec_int ("samplerate","Sample Rate","Sample Rate (in Hz)",
                      8000, 48000, 44100, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TABLESIZE,
    g_param_spec_int ("tablesize", "tablesize", "tablesize",
                      1, G_MAXINT, 1024, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), 
		                   ARG_SAMPLES_PER_BUFFER,
    g_param_spec_int ("samplesperbuffer", "Samples per buffer", 
	              "Number of samples in each outgoing buffer",
                      1, G_MAXINT, 1024, G_PARAM_READWRITE)); 
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FREQ,
    g_param_spec_float ("freq", "Frequency", "Frequency of sine source",
                        0.0, 20000.0, 440.0, G_PARAM_READWRITE)); 
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VOLUME,
    g_param_spec_float ("volume", "Volume", "Volume",
                        0.0, 1.0, 0.8, G_PARAM_READWRITE)); 
                                     
  gobject_class->set_property = gst_sinesrc_set_property;
  gobject_class->get_property = gst_sinesrc_get_property;

/*  gstelement_class->change_state = gst_sinesrc_change_state; */
}

static void 
gst_sinesrc_init (GstSineSrc *src) 
{
 
  src->srcpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (sinesrc_src_factory), "src");
  gst_element_add_pad (GST_ELEMENT(src), src->srcpad);
  
  gst_pad_set_get_function (src->srcpad, gst_sinesrc_get);

  src->width = 16;
  src->samplerate = 44100;
  src->volume = 1.0;
  src->freq = 440.0;
  
  src->newcaps = TRUE;
  
  src->table_pos = 0.0;
  src->table_size = 1024;
  src->samples_per_buffer=1024;
  src->timestamp=0LL;
  src->bufpool=NULL;
  
  src->seq = 0;

  src->dpman = gst_dpman_new ("sinesrc_dpman", GST_ELEMENT(src));

  gst_dpman_add_required_dparam_callback (
    src->dpman, 
    g_param_spec_float("freq","Frequency (Hz)","Frequency of the tone",
                       10.0, 10000.0, 350.0, G_PARAM_READWRITE),
    "hertz",
    gst_sinesrc_update_freq, 
    src
  );
  
  gst_dpman_add_required_dparam_direct (
    src->dpman, 
    g_param_spec_float("volume","Volume","Volume of the tone",
                       0.0, 1.0, 0.8, G_PARAM_READWRITE),
    "scalar",
    &(src->volume)
  );
  
  gst_dpman_set_rate(src->dpman, src->samplerate);

  gst_sinesrc_populate_sinetable(src);
  gst_sinesrc_update_table_inc(src);

}

static GstBuffer *
gst_sinesrc_get (GstPad *pad)
{
  GstSineSrc *src;
  GstBuffer *buf;
  
  gint16 *samples;
  gint i=0;
  
  g_return_val_if_fail (pad != NULL, NULL);
  src = GST_SINESRC(gst_pad_get_parent (pad));

  if (src->bufpool == NULL) {
    src->bufpool = gst_buffer_pool_get_default (2 * src->samples_per_buffer, 8);
  }
  
  buf = (GstBuffer *) gst_buffer_new_from_pool (src->bufpool, 0, 0);
  GST_BUFFER_TIMESTAMP(buf) = src->timestamp;

  samples = (gint16*)GST_BUFFER_DATA(buf);
  GST_BUFFER_DATA(buf) = (gpointer) samples;

  GST_DPMAN_PREPROCESS(src->dpman, src->samples_per_buffer, src->timestamp);
  
  src->timestamp += (gint64)src->samples_per_buffer * GST_SECOND / (gint64)src->samplerate;
   
  while(GST_DPMAN_PROCESS(src->dpman, i)) {

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
    i++;
  }

  
  if (src->newcaps) {
    gst_sinesrc_force_caps(src);
  }
  return buf;
}

static void 
gst_sinesrc_set_property (GObject *object, guint prop_id, 
		          const GValue *value, GParamSpec *pspec) 
{
  GstSineSrc *src;

  g_return_if_fail (GST_IS_SINESRC (object));
  src = GST_SINESRC (object);

  switch (prop_id) {
/*
    case ARG_WIDTH:
      src->width = g_value_get_int (value);
      src->newcaps = TRUE;
      break;
*/
    case ARG_SAMPLERATE:
      src->samplerate = g_value_get_int (value);
      gst_dpman_set_rate (src->dpman, src->samplerate);
      src->newcaps = TRUE;
      gst_sinesrc_update_table_inc (src);
      break;
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
      src->volume = g_value_get_float (value);
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
/*
    case ARG_WIDTH:
      g_value_set_int (value, src->width);
      break;
*/
    case ARG_SAMPLERATE:
      g_value_set_int (value, src->samplerate);
      break;
    case ARG_TABLESIZE:
      g_value_set_int (value, src->table_size);
      break;
    case ARG_SAMPLES_PER_BUFFER:
      g_value_set_int (value, src->samples_per_buffer);
      break;
    case ARG_FREQ:
      g_value_set_float (value, src->freq);
      break;
    case ARG_VOLUME:
      g_value_set_float (value, src->volume);
      break;      
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/*
static gboolean gst_sinesrc_change_state(GstElement *element,
                                          GstElementState state) {
  g_return_if_fail(GST_IS_SINESRC(element));

  switch (state) {
    case GST_STATE_RUNNING:
      if (!gst_sinesrc_open_audio(GST_SINESRC(element)))
        return FALSE;
      break;
    case ~GST_STATE_RUNNING:
      gst_sinesrc_close_audio(GST_SINESRC(element));
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS(parent_class)->change_state)
    return GST_ELEMENT_CLASS(parent_class)->change_state(element,state);
  return TRUE;
}
*/

static void 
gst_sinesrc_populate_sinetable (GstSineSrc *src)
{
  gint i;
  gdouble pi2scaled = M_PI * 2 / src->table_size;
  gfloat *table = g_new (gfloat, src->table_size);

  for(i=0 ; i < src->table_size ; i++){
    table[i] = (gfloat) sin(i * pi2scaled);
  }
  
  g_free (src->table_data);
  src->table_data = table;
}

static void
gst_sinesrc_update_freq (const GValue *value, gpointer data)
{
  GstSineSrc *src = (GstSineSrc *) data;
  g_return_if_fail (GST_IS_SINESRC (src));

  src->freq = g_value_get_float (value);
  src->table_inc = src->table_size * src->freq / src->samplerate;
  
  /*GST_DEBUG(GST_CAT_PARAMS, "freq %f", src->freq); */
}

static inline void 
gst_sinesrc_update_table_inc (GstSineSrc *src)
{
  src->table_inc = src->table_size * src->freq / src->samplerate;
}

static void 
gst_sinesrc_force_caps (GstSineSrc *src) {
  GstCaps *caps;

  if (!src->newcaps)
    return;
  
  src->newcaps = FALSE;

  caps = GST_CAPS_NEW (
	   "sinesrc_src_caps",
	   "audio/raw",
	    "format", 		GST_PROPS_STRING ("int"),
    	     "law",     	GST_PROPS_INT (0),
    	     "endianness",     	GST_PROPS_INT (G_BYTE_ORDER),
    	     "signed",   	GST_PROPS_BOOLEAN (TRUE),
    	     "width",   	GST_PROPS_INT (16),
	     "depth", 		GST_PROPS_INT (16),
	     "rate", 		GST_PROPS_INT (src->samplerate),
	     "channels", 	GST_PROPS_INT (1)
	  );
  
  gst_pad_try_set_caps (src->srcpad, caps);
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_element_factory_new ("sinesrc", GST_TYPE_SINESRC,
                                     &gst_sinesrc_details);
  g_return_val_if_fail (factory != NULL, FALSE);
  
  gst_element_factory_add_pad_template (factory, 
		              GST_PAD_TEMPLATE_GET (sinesrc_src_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  
  /* initialize dparam support library */
  gst_control_init(NULL,NULL);
  
  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "sinesrc",
  plugin_init
};
