/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gst-register.c: Plugin subsystem for loading elements, types, and libs
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

#include <stdlib.h>
#include <stdio.h>

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "config.h"

extern gboolean _gst_registry_auto_load;
static gint num_features = 0;
static gint num_plugins = 0;

static void
plugin_added_func (GstRegistry *registry, GstPlugin *plugin, gpointer user_data)
{
  g_print ("added plugin %s with %d feature(s)\n", plugin->name,
           plugin->numfeatures);

  num_features += plugin->numfeatures;
  num_plugins++;
}

int main (int argc,char *argv[])
{
  GList *registries;
  GList *path_spill = NULL; /* used for path spill from failing registries */

    /* Init gst */
  _gst_registry_auto_load = FALSE;
  gst_init (&argc, &argv);

  registries = gst_registry_pool_list ();
  registries = g_list_reverse (registries);

  while (registries) {
    GstRegistry *registry = GST_REGISTRY (registries->data);
    if (path_spill)
    {
      GList *iter;

      /* add spilled paths to this registry;
       * since they're spilled they probably weren't loaded correctly
       * so we should give a lower priority registry the chance to do them */
      for (iter = path_spill; iter; iter = iter->next)
      {
	g_print ("added path   %s to %s \n",
                 (const char *) iter->data, registry->name);
	gst_registry_add_path (registry, (const gchar *) iter->data);
      }
      g_list_free (path_spill);
      path_spill = NULL;
    }

    g_signal_connect (G_OBJECT (registry), "plugin_added",
		      G_CALLBACK (plugin_added_func), NULL);

    if (registry->flags & GST_REGISTRY_WRITABLE) {
      g_print ("rebuilding %s\n", registry->name);
      gst_registry_rebuild (registry);
      gst_registry_save (registry);
    }
    else {
      g_print ("trying to load %s\n", registry->name);
      if (!gst_registry_load (registry))
      {
	g_print ("error loading %s\n", registry->name);
	/* move over paths from this registry to the next one */
	path_spill = g_list_concat (path_spill,
	                            gst_registry_get_path_list (registry));
	g_assert (path_spill != NULL);
      }
    }
    registries = g_list_next (registries);
  }

  g_print ("loaded %d plugins with %d features\n", num_plugins, num_features);

  return (0);
}

