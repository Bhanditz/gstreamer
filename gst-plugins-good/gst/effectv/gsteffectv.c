/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * EffecTV:
 * Copyright (C) 2001 FUKUCHI Kentarou
 *
 * EffecTV is free software. This library is free software;
 * you can redistribute it and/or
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

#include <string.h>
#include <gst/gst.h>
#include "gsteffectv.h"


struct _elements_entry {
  gchar *name;
  GType (*type) (void);
  GstElementDetails *details;
  gboolean (*factoryinit) (GstElementFactory *factory);
};

static struct _elements_entry _elements[] = {
  { "edgeTV",  		gst_edgetv_get_type,  		&gst_edgetv_details,  		NULL },
  { "agingTV", 		gst_agingtv_get_type, 		&gst_agingtv_details, 		NULL },
  { "diceTV",  		gst_dicetv_get_type,  		&gst_dicetv_details,  		NULL },
  { "warpTV",  		gst_warptv_get_type,  		&gst_warptv_details,  		NULL },
  { "shagadelicTV",  	gst_shagadelictv_get_type,  	&gst_shagadelictv_details,  	NULL },
  { "vertigoTV",  	gst_vertigotv_get_type, 	&gst_vertigotv_details,  	NULL },
  { "revTV",  		gst_revtv_get_type,  		&gst_revtv_details,  		NULL },
  { "quarkTV", 		gst_quarktv_get_type,  		&gst_quarktv_details,  		NULL },
  { NULL, 0 },
};


GstPadTemplate* 
gst_effectv_src_factory (void)
{
  static GstPadTemplate *templ = NULL;
  if (!templ) {
    templ = GST_PAD_TEMPLATE_NEW ( 
  		"src",
  		GST_PAD_SRC,
  		GST_PAD_ALWAYS,
  		GST_CAPS_NEW (
  		  "effectv_src",
  		  "video/raw",
  		    "format",         GST_PROPS_FOURCC (GST_STR_FOURCC ("RGB ")),
  		    "bpp",            GST_PROPS_INT (32),
  		    "depth",          GST_PROPS_INT (32),
  		    "endianness",     GST_PROPS_INT (G_BYTE_ORDER),
  		    "red_mask",       GST_PROPS_INT (0xff0000),
  		    "green_mask",     GST_PROPS_INT (0xff00),
  		    "blue_mask",      GST_PROPS_INT (0xff),
  		    "width",          GST_PROPS_INT_RANGE (16, 4096),
  		    "height",         GST_PROPS_INT_RANGE (16, 4096)
		)
  	     );
  }
  return templ;
}

GstPadTemplate* 
gst_effectv_sink_factory (void)
{
  static GstPadTemplate *templ = NULL;
  if (!templ) {
    templ = GST_PAD_TEMPLATE_NEW ( 
  		"sink",
  		GST_PAD_SINK,
  		GST_PAD_ALWAYS,
  		GST_CAPS_NEW (
  		  "effectv_sink",
  		  "video/raw",
  		    "format",         GST_PROPS_FOURCC (GST_STR_FOURCC ("RGB ")),
  		    "bpp",            GST_PROPS_INT (32),
  		    "depth",          GST_PROPS_INT (32),
  		    "endianness",     GST_PROPS_INT (G_BYTE_ORDER),
  		    "red_mask",       GST_PROPS_INT (0xff0000),
  		    "green_mask",     GST_PROPS_INT (0xff00),
  		    "blue_mask",      GST_PROPS_INT (0xff),
  		    "width",          GST_PROPS_INT_RANGE (16, 4096),
  		    "height",         GST_PROPS_INT_RANGE (16, 4096)
		)
  	     );
  }
  return templ;
}

static gboolean
plugin_init (GModule * module, GstPlugin * plugin)
{
  GstElementFactory *factory;
  gint i = 0;

  while (_elements[i].name) {
    factory = gst_element_factory_new (_elements[i].name,
                                      (_elements[i].type) (),
                                       _elements[i].details);

    if (!factory) {
      g_warning ("gst_effecttv_new failed for `%s'",
                 _elements[i].name);
      continue;
    }
    gst_element_factory_add_pad_template (factory, gst_effectv_src_factory ());
    gst_element_factory_add_pad_template (factory, gst_effectv_sink_factory ());

    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
    if (_elements[i].factoryinit) {
      _elements[i].factoryinit (factory);
    }
    i++;
  }

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "effectv",
  plugin_init
};
