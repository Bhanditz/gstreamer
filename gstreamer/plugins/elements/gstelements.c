/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstelements.c:
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


#include <gst/gst.h>

#include "gstfilesink.h"
#include "gstidentity.h"
#include "gstfakesink.h"
#include "gstfakesrc.h"
#include "gstfdsink.h"
#include "gstfdsrc.h"
#include "gstmultidisksrc.h"
#include "gstpipefilter.h"
#include "gsttee.h"
#include "gstaggregator.h"
#include "gststatistics.h"
#include "gstmd5sink.h"


struct _elements_entry {
  gchar *name;
  GType (*type) (void);
  GstElementDetails *details;
  gboolean (*factoryinit) (GstElementFactory *factory);
};


extern GType gst_filesrc_get_type(void);
extern GstElementDetails gst_filesrc_details;

static struct _elements_entry _elements[] = {
  { "fakesrc", 	    gst_fakesrc_get_type, 	&gst_fakesrc_details,		gst_fakesrc_factory_init },
  { "fakesink",     gst_fakesink_get_type, 	&gst_fakesink_details,		gst_fakesink_factory_init },
  { "filesrc", 	    gst_filesrc_get_type, 	&gst_filesrc_details,		NULL },
  { "filesink",	    gst_filesink_get_type,      &gst_filesink_details, 		NULL },
  { "identity",     gst_identity_get_type,  	&gst_identity_details,		NULL },
  { "fdsink",       gst_fdsink_get_type, 	&gst_fdsink_details,		NULL },
  { "fdsrc", 	    gst_fdsrc_get_type, 	&gst_fdsrc_details,		NULL },
  { "multidisksrc", gst_multidisksrc_get_type,	&gst_multidisksrc_details,	NULL },
  { "pipefilter",   gst_pipefilter_get_type, 	&gst_pipefilter_details,	NULL },
  { "tee",     	    gst_tee_get_type, 		&gst_tee_details,		gst_tee_factory_init },
  { "aggregator",   gst_aggregator_get_type, 	&gst_aggregator_details,	gst_aggregator_factory_init },
  { "statistics",   gst_statistics_get_type, 	&gst_statistics_details,	NULL },
  { "md5sink",      gst_md5sink_get_type, 	&gst_md5sink_details,		gst_md5sink_factory_init },
  { NULL, 0 },
};

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
  gint i = 0;

  gst_plugin_set_longname (plugin, "Standard GST Elements");

  while (_elements[i].name) {
    factory = gst_element_factory_new (_elements[i].name,
                                      (_elements[i].type) (),
                                      _elements[i].details);

    if (!factory)
      {
	g_warning ("gst_element_factory_new failed for `%s'",
		   _elements[i].name);
	continue;
      }

    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
    if (_elements[i].factoryinit) {
      _elements[i].factoryinit (factory);
    }
/*      g_print("added factory '%s'\n",_elements[i].name); */

    i++;
  }

/*  INFO (GST_INFO_PLUGIN_LOAD,"gstelements: loaded %d standard elements", i);*/

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gstelements",
  plugin_init
};

