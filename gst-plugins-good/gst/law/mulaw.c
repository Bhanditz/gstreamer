#include "mulaw-encode.h"
#include "mulaw-decode.h"

/* elementfactory information */
static GstElementDetails mulawenc_details = {
  "PCM to Mu Law conversion",
  "Filter/Audio/Conversion",
  "LGPL",
  "Convert 16bit PCM to 8bit mu law",
  VERSION,
  "Zaheer Merali <zaheer@bellworldwide.net>",
  "(C) 2001"
};

/* elementfactory information */
static GstElementDetails mulawdec_details = {
  "Mu Law to PCM conversion",
  "Filter/Audio/Conversion",
  "LGPL",
  "Convert 8bit mu law to 16bit PCM",
  VERSION,
  "Zaheer Merali <zaheer@bellworldwide.net>",
  "(C) 2001"
};

static GstCaps*
mulaw_factory (void)
{
  return 
    gst_caps_new (
  	"test_src",
    	"audio/raw",
	gst_props_new (
    	  "format",   GST_PROPS_STRING ("int"),
    	    "law",    GST_PROPS_INT (1),
    	    "width",  GST_PROPS_INT(8),
    	    "depth",  GST_PROPS_INT(8),
    	    "signed", GST_PROPS_BOOLEAN(FALSE),
	    NULL));
}

static GstCaps*
linear_factory (void)
{
  return 
    gst_caps_new (
  	"test_sink",
    	"audio/raw",
	gst_props_new (
    	  "format",     GST_PROPS_STRING ("int"),
      	    "law",      GST_PROPS_INT(0),
      	    "width",    GST_PROPS_INT(16),
      	    "depth",    GST_PROPS_INT(16),
      	    "signed",   GST_PROPS_BOOLEAN(TRUE),
	    NULL));
}

GstPadTemplate *mulawenc_src_template, *mulawenc_sink_template; 
GstPadTemplate *mulawdec_src_template, *mulawdec_sink_template;

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *mulawenc_factory, *mulawdec_factory;
  GstCaps* mulaw_caps, *linear_caps;

  mulawenc_factory = gst_element_factory_new("mulawenc",GST_TYPE_MULAWENC,
                                            &mulawenc_details);
  g_return_val_if_fail(mulawenc_factory != NULL, FALSE);
  mulawdec_factory = gst_element_factory_new("mulawdec",GST_TYPE_MULAWDEC,
					    &mulawdec_details);
  g_return_val_if_fail(mulawdec_factory != NULL, FALSE);
  gst_element_factory_set_rank (mulawdec_factory, GST_ELEMENT_RANK_PRIMARY);

  mulaw_caps = mulaw_factory ();
  linear_caps = linear_factory ();
 
  mulawenc_src_template = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
		   		               mulaw_caps, NULL);
  mulawenc_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
		   			        linear_caps, NULL);

  gst_element_factory_add_pad_template (mulawenc_factory, mulawenc_src_template);
  gst_element_factory_add_pad_template (mulawenc_factory, mulawenc_sink_template);

  mulawdec_src_template = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
		  				linear_caps, NULL);
  mulawdec_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
		   				mulaw_caps, NULL);
  
  gst_element_factory_add_pad_template (mulawdec_factory, mulawdec_src_template);
  gst_element_factory_add_pad_template (mulawdec_factory, mulawdec_sink_template);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (mulawenc_factory));
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (mulawdec_factory));

    

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "mulaw",
  plugin_init
};

