/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstplugin.c: Plugin subsystem for loading elements, types, and libs
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

/**
 * SECTION:gstplugin
 * @short_description: Container for features loaded from a shared object module
 * @see_also: #GstPluginFeature, #GstElementFactory
 *
 * GStreamer is extensible, so #GstElement instances can be loaded at runtime.
 * A plugin system can provide one or more of the basic
 * <application>GStreamer</application> #GstPluginFeature subclasses.
 *
 * A plugin should export a symbol <symbol>gst_plugin_desc</symbol> that is a
 * struct of type #GstPluginDesc.
 * the plugin loader will check the version of the core library the plugin was
 * linked against and will create a new #GstPlugin. It will then call the
 * #GstPluginInitFunc function that was provided in the
 * <symbol>gst_plugin_desc</symbol>.
 *
 * Once you have a handle to a #GstPlugin (e.g. from the #GstRegistry), you
 * can add any object that subclasses #GstPluginFeature.
 *
 * Usually plugins are always automaticlly loaded so you don't need to call
 * gst_plugin_load() explicitly to bring it into memory. There are options to
 * statically link plugins to an app or even use GStreamer without a plugin
 * repository in which case gst_plugin_load() can be needed to bring the plugin
 * into memory.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <glib/gstdio.h>
#include <sys/types.h>
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <signal.h>
#include <errno.h>

#include "gst_private.h"
#include "glib-compat-private.h"

#include <gst/gst.h>

#define GST_CAT_DEFAULT GST_CAT_PLUGIN_LOADING

static guint _num_static_plugins;       /* 0    */
static GstPluginDesc *_static_plugins;  /* NULL */
static gboolean _gst_plugin_inited;

/* static variables for segfault handling of plugin loading */
static char *_gst_plugin_fault_handler_filename = NULL;

/* list of valid licenses.
 * One of these must be specified or the plugin won't be loaded
 * Contact gstreamer-devel@lists.sourceforge.net if your license should be
 * added.
 *
 * GPL: http://www.gnu.org/copyleft/gpl.html
 * LGPL: http://www.gnu.org/copyleft/lesser.html
 * QPL: http://www.trolltech.com/licenses/qpl.html
 * MPL: http://www.opensource.org/licenses/mozilla1.1.php
 * MIT/X11: http://www.opensource.org/licenses/mit-license.php
 * 3-clause BSD: http://www.opensource.org/licenses/bsd-license.php
 */
static const gchar *valid_licenses[] = {
  "LGPL",                       /* GNU Lesser General Public License */
  "GPL",                        /* GNU General Public License */
  "QPL",                        /* Trolltech Qt Public License */
  "GPL/QPL",                    /* Combi-license of GPL + QPL */
  "MPL",                        /* MPL 1.1 license */
  "BSD",                        /* 3-clause BSD license */
  "MIT/X11",                    /* MIT/X11 license */
  "Proprietary",                /* Proprietary license */
  GST_LICENSE_UNKNOWN,          /* some other license */
  NULL
};

static GstPlugin *gst_plugin_register_func (GstPlugin * plugin,
    const GstPluginDesc * desc, gpointer user_data);
static void gst_plugin_desc_copy (GstPluginDesc * dest,
    const GstPluginDesc * src);
static void gst_plugin_desc_free (GstPluginDesc * desc);

static void gst_plugin_ext_dep_free (GstPluginDep * dep);

G_DEFINE_TYPE (GstPlugin, gst_plugin, GST_TYPE_OBJECT);

static void
gst_plugin_init (GstPlugin * plugin)
{
  plugin->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (plugin, GST_TYPE_PLUGIN, GstPluginPrivate);
}

static void
gst_plugin_finalize (GObject * object)
{
  GstPlugin *plugin = GST_PLUGIN_CAST (object);
  GstRegistry *registry = gst_registry_get_default ();
  GList *g;

  GST_DEBUG ("finalizing plugin %p", plugin);
  for (g = registry->plugins; g; g = g->next) {
    if (g->data == (gpointer) plugin) {
      g_warning ("removing plugin that is still in registry");
    }
  }
  g_free (plugin->filename);
  g_free (plugin->basename);
  gst_plugin_desc_free (&plugin->desc);

  g_list_foreach (plugin->priv->deps, (GFunc) gst_plugin_ext_dep_free, NULL);
  g_list_free (plugin->priv->deps);
  plugin->priv->deps = NULL;

  G_OBJECT_CLASS (gst_plugin_parent_class)->finalize (object);
}

static void
gst_plugin_class_init (GstPluginClass * klass)
{
  G_OBJECT_CLASS (klass)->finalize = GST_DEBUG_FUNCPTR (gst_plugin_finalize);

  g_type_class_add_private (klass, sizeof (GstPluginPrivate));
}

GQuark
gst_plugin_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("gst_plugin_error");
  return quark;
}

#ifndef GST_REMOVE_DEPRECATED
/* this function can be called in the GCC constructor extension, before
 * the _gst_plugin_initialize() was called. In that case, we store the
 * plugin description in a list to initialize it when we open the main
 * module later on.
 * When the main module is known, we can register the plugin right away.
 */
void
_gst_plugin_register_static (GstPluginDesc * desc)
{
  g_return_if_fail (desc != NULL);

  if (!_gst_plugin_inited) {
    /* We can't use any GLib functions here, since g_thread_init hasn't been
     * called yet, and we can't call it here either, or programs that don't
     * guard their g_thread_init calls in main() will just abort */
    ++_num_static_plugins;
    _static_plugins =
        realloc (_static_plugins, _num_static_plugins * sizeof (GstPluginDesc));
    /* assume strings in the GstPluginDesc are static const or live forever */
    _static_plugins[_num_static_plugins - 1] = *desc;
  } else {
    gst_plugin_register_static (desc->major_version, desc->minor_version,
        desc->name, desc->description, desc->plugin_init, desc->version,
        desc->license, desc->source, desc->package, desc->origin);
  }
}
#endif

/**
 * gst_plugin_register_static:
 * @major_version: the major version number of the GStreamer core that the
 *     plugin was compiled for, you can just use GST_VERSION_MAJOR here
 * @minor_version: the minor version number of the GStreamer core that the
 *     plugin was compiled for, you can just use GST_VERSION_MINOR here
 * @name: a unique name of the plugin (ideally prefixed with an application- or
 *     library-specific namespace prefix in order to avoid name conflicts in
 *     case a similar plugin with the same name ever gets added to GStreamer)
 * @description: description of the plugin
 * @init_func: pointer to the init function of this plugin.
 * @version: version string of the plugin
 * @license: effective license of plugin. Must be one of the approved licenses
 *     (see #GstPluginDesc above) or the plugin will not be registered.
 * @source: source module plugin belongs to
 * @package: shipped package plugin belongs to
 * @origin: URL to provider of plugin
 *
 * Registers a static plugin, ie. a plugin which is private to an application
 * or library and contained within the application or library (as opposed to
 * being shipped as a separate module file).
 *
 * You must make sure that GStreamer has been initialised (with gst_init() or
 * via gst_init_get_option_group()) before calling this function.
 *
 * Returns: TRUE if the plugin was registered correctly, otherwise FALSE.
 *
 * Since: 0.10.16
 */
gboolean
gst_plugin_register_static (gint major_version, gint minor_version,
    const gchar * name, gchar * description, GstPluginInitFunc init_func,
    const gchar * version, const gchar * license, const gchar * source,
    const gchar * package, const gchar * origin)
{
  GstPluginDesc desc = { major_version, minor_version, name, description,
    init_func, version, license, source, package, origin,
  };
  GstPlugin *plugin;
  gboolean res = FALSE;

  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (description != NULL, FALSE);
  g_return_val_if_fail (init_func != NULL, FALSE);
  g_return_val_if_fail (version != NULL, FALSE);
  g_return_val_if_fail (license != NULL, FALSE);
  g_return_val_if_fail (source != NULL, FALSE);
  g_return_val_if_fail (package != NULL, FALSE);
  g_return_val_if_fail (origin != NULL, FALSE);

  /* make sure gst_init() has been called */
  g_return_val_if_fail (_gst_plugin_inited != FALSE, FALSE);

  GST_LOG ("attempting to load static plugin \"%s\" now...", name);
  plugin = g_object_new (GST_TYPE_PLUGIN, NULL);
  if (gst_plugin_register_func (plugin, &desc, NULL) != NULL) {
    GST_INFO ("registered static plugin \"%s\"", name);
    res = gst_default_registry_add_plugin (plugin);
    GST_INFO ("added static plugin \"%s\", result: %d", name, res);
  }
  return res;
}

/**
 * gst_plugin_register_static_full:
 * @major_version: the major version number of the GStreamer core that the
 *     plugin was compiled for, you can just use GST_VERSION_MAJOR here
 * @minor_version: the minor version number of the GStreamer core that the
 *     plugin was compiled for, you can just use GST_VERSION_MINOR here
 * @name: a unique name of the plugin (ideally prefixed with an application- or
 *     library-specific namespace prefix in order to avoid name conflicts in
 *     case a similar plugin with the same name ever gets added to GStreamer)
 * @description: description of the plugin
 * @init_full_func: pointer to the init function with user data of this plugin.
 * @version: version string of the plugin
 * @license: effective license of plugin. Must be one of the approved licenses
 *     (see #GstPluginDesc above) or the plugin will not be registered.
 * @source: source module plugin belongs to
 * @package: shipped package plugin belongs to
 * @origin: URL to provider of plugin
 * @user_data: gpointer to user data
 *
 * Registers a static plugin, ie. a plugin which is private to an application
 * or library and contained within the application or library (as opposed to
 * being shipped as a separate module file) with a #GstPluginInitFullFunc
 * which allows user data to be passed to the callback function (useful
 * for bindings).
 *
 * You must make sure that GStreamer has been initialised (with gst_init() or
 * via gst_init_get_option_group()) before calling this function.
 *
 * Returns: TRUE if the plugin was registered correctly, otherwise FALSE.
 *
 * Since: 0.10.24
 *
 */
gboolean
gst_plugin_register_static_full (gint major_version, gint minor_version,
    const gchar * name, gchar * description,
    GstPluginInitFullFunc init_full_func, const gchar * version,
    const gchar * license, const gchar * source, const gchar * package,
    const gchar * origin, gpointer user_data)
{
  GstPluginDesc desc = { major_version, minor_version, name, description,
    (GstPluginInitFunc) init_full_func, version, license, source, package,
    origin,
  };
  GstPlugin *plugin;
  gboolean res = FALSE;

  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (description != NULL, FALSE);
  g_return_val_if_fail (init_full_func != NULL, FALSE);
  g_return_val_if_fail (version != NULL, FALSE);
  g_return_val_if_fail (license != NULL, FALSE);
  g_return_val_if_fail (source != NULL, FALSE);
  g_return_val_if_fail (package != NULL, FALSE);
  g_return_val_if_fail (origin != NULL, FALSE);

  /* make sure gst_init() has been called */
  g_return_val_if_fail (_gst_plugin_inited != FALSE, FALSE);

  GST_LOG ("attempting to load static plugin \"%s\" now...", name);
  plugin = g_object_new (GST_TYPE_PLUGIN, NULL);
  if (gst_plugin_register_func (plugin, &desc, user_data) != NULL) {
    GST_INFO ("registered static plugin \"%s\"", name);
    res = gst_default_registry_add_plugin (plugin);
    GST_INFO ("added static plugin \"%s\", result: %d", name, res);
  }
  return res;
}

void
_gst_plugin_initialize (void)
{
  guint i;

  _gst_plugin_inited = TRUE;

  /* now register all static plugins */
  GST_INFO ("registering %u static plugins", _num_static_plugins);
  for (i = 0; i < _num_static_plugins; ++i) {
    gst_plugin_register_static (_static_plugins[i].major_version,
        _static_plugins[i].minor_version, _static_plugins[i].name,
        _static_plugins[i].description, _static_plugins[i].plugin_init,
        _static_plugins[i].version, _static_plugins[i].license,
        _static_plugins[i].source, _static_plugins[i].package,
        _static_plugins[i].origin);
  }

  if (_static_plugins) {
    free (_static_plugins);
    _static_plugins = NULL;
    _num_static_plugins = 0;
  }
}

/* this function could be extended to check if the plugin license matches the
 * applications license (would require the app to register its license somehow).
 * We'll wait for someone who's interested in it to code it :)
 */
static gboolean
gst_plugin_check_license (const gchar * license)
{
  const gchar **check_license = valid_licenses;

  g_assert (check_license);

  while (*check_license) {
    if (strcmp (license, *check_license) == 0)
      return TRUE;
    check_license++;
  }
  return FALSE;
}

static gboolean
gst_plugin_check_version (gint major, gint minor)
{
  /* return NULL if the major and minor version numbers are not compatible */
  /* with ours. */
  if (major != GST_VERSION_MAJOR || minor != GST_VERSION_MINOR)
    return FALSE;

  return TRUE;
}

static GstPlugin *
gst_plugin_register_func (GstPlugin * plugin, const GstPluginDesc * desc,
    gpointer user_data)
{
  if (!gst_plugin_check_version (desc->major_version, desc->minor_version)) {
    if (GST_CAT_DEFAULT)
      GST_WARNING ("plugin \"%s\" has incompatible version, not loading",
          plugin->filename);
    return NULL;
  }

  if (!desc->license || !desc->description || !desc->source ||
      !desc->package || !desc->origin) {
    if (GST_CAT_DEFAULT)
      GST_WARNING ("plugin \"%s\" has incorrect GstPluginDesc, not loading",
          plugin->filename);
    return NULL;
  }

  if (!gst_plugin_check_license (desc->license)) {
    if (GST_CAT_DEFAULT)
      GST_WARNING ("plugin \"%s\" has invalid license \"%s\", not loading",
          plugin->filename, desc->license);
    return NULL;
  }

  if (GST_CAT_DEFAULT)
    GST_LOG ("plugin \"%s\" looks good", GST_STR_NULL (plugin->filename));

  gst_plugin_desc_copy (&plugin->desc, desc);

  if (user_data) {
    if (!(((GstPluginInitFullFunc) (desc->plugin_init)) (plugin, user_data))) {
      if (GST_CAT_DEFAULT)
        GST_WARNING ("plugin \"%s\" failed to initialise", plugin->filename);
      plugin->module = NULL;
      return NULL;
    }
  } else {
    if (!((desc->plugin_init) (plugin))) {
      if (GST_CAT_DEFAULT)
        GST_WARNING ("plugin \"%s\" failed to initialise", plugin->filename);
      plugin->module = NULL;
      return NULL;
    }
  }

  if (GST_CAT_DEFAULT)
    GST_LOG ("plugin \"%s\" initialised", GST_STR_NULL (plugin->filename));

  return plugin;
}

#ifdef HAVE_SIGACTION
static struct sigaction oldaction;
static gboolean _gst_plugin_fault_handler_is_setup = FALSE;

/*
 * _gst_plugin_fault_handler_restore:
 * segfault handler restorer
 */
static void
_gst_plugin_fault_handler_restore (void)
{
  if (!_gst_plugin_fault_handler_is_setup)
    return;

  _gst_plugin_fault_handler_is_setup = FALSE;

  sigaction (SIGSEGV, &oldaction, NULL);
}

/*
 * _gst_plugin_fault_handler_sighandler:
 * segfault handler implementation
 */
static void
_gst_plugin_fault_handler_sighandler (int signum)
{
  /* We need to restore the fault handler or we'll keep getting it */
  _gst_plugin_fault_handler_restore ();

  switch (signum) {
    case SIGSEGV:
      g_print ("\nERROR: ");
      g_print ("Caught a segmentation fault while loading plugin file:\n");
      g_print ("%s\n\n", _gst_plugin_fault_handler_filename);
      g_print ("Please either:\n");
      g_print ("- remove it and restart.\n");
      g_print ("- run with --gst-disable-segtrap and debug.\n");
      exit (-1);
      break;
    default:
      g_print ("Caught unhandled signal on plugin loading\n");
      break;
  }
}

/*
 * _gst_plugin_fault_handler_setup:
 * sets up the segfault handler
 */
static void
_gst_plugin_fault_handler_setup (void)
{
  struct sigaction action;

  /* if asked to leave segfaults alone, just return */
  if (!gst_segtrap_is_enabled ())
    return;

  if (_gst_plugin_fault_handler_is_setup)
    return;

  _gst_plugin_fault_handler_is_setup = TRUE;

  memset (&action, 0, sizeof (action));
  action.sa_handler = _gst_plugin_fault_handler_sighandler;

  sigaction (SIGSEGV, &action, &oldaction);
}
#else /* !HAVE_SIGACTION */
static void
_gst_plugin_fault_handler_restore (void)
{
}

static void
_gst_plugin_fault_handler_setup (void)
{
}
#endif /* HAVE_SIGACTION */

static void _gst_plugin_fault_handler_setup ();

static GStaticMutex gst_plugin_loading_mutex = G_STATIC_MUTEX_INIT;

#define CHECK_PLUGIN_DESC_FIELD(desc,field,fn)                               \
  if (G_UNLIKELY ((desc)->field == NULL)) {                                  \
    GST_ERROR ("GstPluginDesc for '%s' has no %s", fn, G_STRINGIFY (field)); \
  }

/**
 * gst_plugin_load_file:
 * @filename: the plugin filename to load
 * @error: pointer to a NULL-valued GError
 *
 * Loads the given plugin and refs it.  Caller needs to unref after use.
 *
 * Returns: a reference to the existing loaded GstPlugin, a reference to the
 * newly-loaded GstPlugin, or NULL if an error occurred.
 */
GstPlugin *
gst_plugin_load_file (const gchar * filename, GError ** error)
{
  GstPlugin *plugin;
  GModule *module;
  gboolean ret;
  gpointer ptr;
  struct stat file_status;
  GstRegistry *registry;

  g_return_val_if_fail (filename != NULL, NULL);

  registry = gst_registry_get_default ();
  g_static_mutex_lock (&gst_plugin_loading_mutex);

  plugin = gst_registry_lookup (registry, filename);
  if (plugin) {
    if (plugin->module) {
      g_static_mutex_unlock (&gst_plugin_loading_mutex);
      return plugin;
    } else {
      gst_object_unref (plugin);
      plugin = NULL;
    }
  }

  GST_CAT_DEBUG (GST_CAT_PLUGIN_LOADING, "attempt to load plugin \"%s\"",
      filename);

  if (g_module_supported () == FALSE) {
    GST_CAT_DEBUG (GST_CAT_PLUGIN_LOADING, "module loading not supported");
    g_set_error (error,
        GST_PLUGIN_ERROR,
        GST_PLUGIN_ERROR_MODULE, "Dynamic loading not supported");
    goto return_error;
  }

  if (g_stat (filename, &file_status)) {
    GST_CAT_DEBUG (GST_CAT_PLUGIN_LOADING, "problem accessing file");
    g_set_error (error,
        GST_PLUGIN_ERROR,
        GST_PLUGIN_ERROR_MODULE, "Problem accessing file %s: %s", filename,
        g_strerror (errno));
    goto return_error;
  }

  module = g_module_open (filename, G_MODULE_BIND_LOCAL);
  if (module == NULL) {
    GST_CAT_WARNING (GST_CAT_PLUGIN_LOADING, "module_open failed: %s",
        g_module_error ());
    g_set_error (error,
        GST_PLUGIN_ERROR, GST_PLUGIN_ERROR_MODULE, "Opening module failed: %s",
        g_module_error ());
    /* If we failed to open the shared object, then it's probably because a
     * plugin is linked against the wrong libraries. Print out an easy-to-see
     * message in this case. */
    g_warning ("Failed to load plugin '%s': %s", filename, g_module_error ());
    goto return_error;
  }

  plugin = g_object_new (GST_TYPE_PLUGIN, NULL);

  plugin->module = module;
  plugin->filename = g_strdup (filename);
  plugin->basename = g_path_get_basename (filename);
  plugin->file_mtime = file_status.st_mtime;
  plugin->file_size = file_status.st_size;

  ret = g_module_symbol (module, "gst_plugin_desc", &ptr);
  if (!ret) {
    GST_DEBUG ("Could not find plugin entry point in \"%s\"", filename);
    g_set_error (error,
        GST_PLUGIN_ERROR,
        GST_PLUGIN_ERROR_MODULE,
        "File \"%s\" is not a GStreamer plugin", filename);
    g_module_close (module);
    goto return_error;
  }
  plugin->orig_desc = (GstPluginDesc *) ptr;

  /* check plugin description: complain about bad values but accept them, to
   * maintain backwards compatibility (FIXME: 0.11) */
  CHECK_PLUGIN_DESC_FIELD (plugin->orig_desc, name, filename);
  CHECK_PLUGIN_DESC_FIELD (plugin->orig_desc, description, filename);
  CHECK_PLUGIN_DESC_FIELD (plugin->orig_desc, version, filename);
  CHECK_PLUGIN_DESC_FIELD (plugin->orig_desc, license, filename);
  CHECK_PLUGIN_DESC_FIELD (plugin->orig_desc, source, filename);
  CHECK_PLUGIN_DESC_FIELD (plugin->orig_desc, package, filename);
  CHECK_PLUGIN_DESC_FIELD (plugin->orig_desc, origin, filename);

  GST_LOG ("Plugin %p for file \"%s\" prepared, calling entry function...",
      plugin, filename);

  /* this is where we load the actual .so, so let's trap SIGSEGV */
  _gst_plugin_fault_handler_setup ();
  _gst_plugin_fault_handler_filename = plugin->filename;

  GST_LOG ("Plugin %p for file \"%s\" prepared, registering...",
      plugin, filename);

  if (!gst_plugin_register_func (plugin, plugin->orig_desc, NULL)) {
    /* remove signal handler */
    _gst_plugin_fault_handler_restore ();
    GST_DEBUG ("gst_plugin_register_func failed for plugin \"%s\"", filename);
    /* plugin == NULL */
    g_set_error (error,
        GST_PLUGIN_ERROR,
        GST_PLUGIN_ERROR_MODULE,
        "File \"%s\" appears to be a GStreamer plugin, but it failed to initialize",
        filename);
    g_module_close (module);
    goto return_error;
  }

  /* remove signal handler */
  _gst_plugin_fault_handler_restore ();
  _gst_plugin_fault_handler_filename = NULL;
  GST_INFO ("plugin \"%s\" loaded", plugin->filename);

  gst_object_ref (plugin);
  gst_default_registry_add_plugin (plugin);

  g_static_mutex_unlock (&gst_plugin_loading_mutex);
  return plugin;

return_error:
  {
    if (plugin)
      gst_object_unref (plugin);
    g_static_mutex_unlock (&gst_plugin_loading_mutex);
    return NULL;
  }
}

static void
gst_plugin_desc_copy (GstPluginDesc * dest, const GstPluginDesc * src)
{
  dest->major_version = src->major_version;
  dest->minor_version = src->minor_version;
  dest->name = g_intern_string (src->name);
  /* maybe intern the description too, just for convenience? */
  dest->description = g_strdup (src->description);
  dest->plugin_init = src->plugin_init;
  dest->version = g_intern_string (src->version);
  dest->license = g_intern_string (src->license);
  dest->source = g_intern_string (src->source);
  dest->package = g_intern_string (src->package);
  dest->origin = g_intern_string (src->origin);
}

/* unused */
static void
gst_plugin_desc_free (GstPluginDesc * desc)
{
  g_free (desc->description);
  memset (desc, 0, sizeof (GstPluginDesc));
}

/**
 * gst_plugin_get_name:
 * @plugin: plugin to get the name of
 *
 * Get the short name of the plugin
 *
 * Returns: the name of the plugin
 */
const gchar *
gst_plugin_get_name (GstPlugin * plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->desc.name;
}

/**
 * gst_plugin_get_description:
 * @plugin: plugin to get long name of
 *
 * Get the long descriptive name of the plugin
 *
 * Returns: the long name of the plugin
 */
G_CONST_RETURN gchar *
gst_plugin_get_description (GstPlugin * plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->desc.description;
}

/**
 * gst_plugin_get_filename:
 * @plugin: plugin to get the filename of
 *
 * get the filename of the plugin
 *
 * Returns: the filename of the plugin
 */
G_CONST_RETURN gchar *
gst_plugin_get_filename (GstPlugin * plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->filename;
}

/**
 * gst_plugin_get_version:
 * @plugin: plugin to get the version of
 *
 * get the version of the plugin
 *
 * Returns: the version of the plugin
 */
G_CONST_RETURN gchar *
gst_plugin_get_version (GstPlugin * plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->desc.version;
}

/**
 * gst_plugin_get_license:
 * @plugin: plugin to get the license of
 *
 * get the license of the plugin
 *
 * Returns: the license of the plugin
 */
G_CONST_RETURN gchar *
gst_plugin_get_license (GstPlugin * plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->desc.license;
}

/**
 * gst_plugin_get_source:
 * @plugin: plugin to get the source of
 *
 * get the source module the plugin belongs to.
 *
 * Returns: the source of the plugin
 */
G_CONST_RETURN gchar *
gst_plugin_get_source (GstPlugin * plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->desc.source;
}

/**
 * gst_plugin_get_package:
 * @plugin: plugin to get the package of
 *
 * get the package the plugin belongs to.
 *
 * Returns: the package of the plugin
 */
G_CONST_RETURN gchar *
gst_plugin_get_package (GstPlugin * plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->desc.package;
}

/**
 * gst_plugin_get_origin:
 * @plugin: plugin to get the origin of
 *
 * get the URL where the plugin comes from
 *
 * Returns: the origin of the plugin
 */
G_CONST_RETURN gchar *
gst_plugin_get_origin (GstPlugin * plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->desc.origin;
}

/**
 * gst_plugin_get_module:
 * @plugin: plugin to query
 *
 * Gets the #GModule of the plugin. If the plugin isn't loaded yet, NULL is
 * returned.
 *
 * Returns: module belonging to the plugin or NULL if the plugin isn't
 *          loaded yet.
 */
GModule *
gst_plugin_get_module (GstPlugin * plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->module;
}

/**
 * gst_plugin_is_loaded:
 * @plugin: plugin to query
 *
 * queries if the plugin is loaded into memory
 *
 * Returns: TRUE is loaded, FALSE otherwise
 */
gboolean
gst_plugin_is_loaded (GstPlugin * plugin)
{
  g_return_val_if_fail (plugin != NULL, FALSE);

  return (plugin->module != NULL || plugin->filename == NULL);
}

#if 0
/**
 * gst_plugin_feature_list:
 * @plugin: plugin to query
 * @filter: the filter to use
 * @first: only return first match
 * @user_data: user data passed to the filter function
 *
 * Runs a filter against all plugin features and returns a GList with
 * the results. If the first flag is set, only the first match is
 * returned (as a list with a single object).
 *
 * Returns: a GList of features, g_list_free after use.
 */
GList *
gst_plugin_feature_filter (GstPlugin * plugin,
    GstPluginFeatureFilter filter, gboolean first, gpointer user_data)
{
  GList *list;
  GList *g;

  list = gst_filter_run (plugin->features, (GstFilterFunc) filter, first,
      user_data);
  for (g = list; g; g = g->next) {
    gst_object_ref (plugin);
  }

  return list;
}

typedef struct
{
  GstPluginFeatureFilter filter;
  gboolean first;
  gpointer user_data;
  GList *result;
}
FeatureFilterData;

static gboolean
_feature_filter (GstPlugin * plugin, gpointer user_data)
{
  GList *result;
  FeatureFilterData *data = (FeatureFilterData *) user_data;

  result = gst_plugin_feature_filter (plugin, data->filter, data->first,
      data->user_data);
  if (result) {
    data->result = g_list_concat (data->result, result);
    return TRUE;
  }
  return FALSE;
}

/**
 * gst_plugin_list_feature_filter:
 * @list: a #GList of plugins to query
 * @filter: the filter function to use
 * @first: only return first match
 * @user_data: user data passed to the filter function
 *
 * Runs a filter against all plugin features of the plugins in the given
 * list and returns a GList with the results.
 * If the first flag is set, only the first match is
 * returned (as a list with a single object).
 *
 * Returns: a GList of features, g_list_free after use.
 */
GList *
gst_plugin_list_feature_filter (GList * list,
    GstPluginFeatureFilter filter, gboolean first, gpointer user_data)
{
  FeatureFilterData data;
  GList *result;

  data.filter = filter;
  data.first = first;
  data.user_data = user_data;
  data.result = NULL;

  result = gst_filter_run (list, (GstFilterFunc) _feature_filter, first, &data);
  g_list_free (result);

  return data.result;
}
#endif

/**
 * gst_plugin_name_filter:
 * @plugin: the plugin to check
 * @name: the name of the plugin
 *
 * A standard filter that returns TRUE when the plugin is of the
 * given name.
 *
 * Returns: TRUE if the plugin is of the given name.
 */
gboolean
gst_plugin_name_filter (GstPlugin * plugin, const gchar * name)
{
  return (plugin->desc.name && !strcmp (plugin->desc.name, name));
}

#if 0
/**
 * gst_plugin_find_feature:
 * @plugin: plugin to get the feature from
 * @name: The name of the feature to find
 * @type: The type of the feature to find
 *
 * Find a feature of the given name and type in the given plugin.
 *
 * Returns: a GstPluginFeature or NULL if the feature was not found.
 */
GstPluginFeature *
gst_plugin_find_feature (GstPlugin * plugin, const gchar * name, GType type)
{
  GList *walk;
  GstPluginFeature *result = NULL;
  GstTypeNameData data;

  g_return_val_if_fail (name != NULL, NULL);

  data.type = type;
  data.name = name;

  walk = gst_filter_run (plugin->features,
      (GstFilterFunc) gst_plugin_feature_type_name_filter, TRUE, &data);

  if (walk) {
    result = GST_PLUGIN_FEATURE (walk->data);

    gst_object_ref (result);
    gst_plugin_feature_list_free (walk);
  }

  return result;
}
#endif

#if 0
static gboolean
gst_plugin_feature_name_filter (GstPluginFeature * feature, const gchar * name)
{
  return !strcmp (name, GST_PLUGIN_FEATURE_NAME (feature));
}
#endif

#if 0
/**
 * gst_plugin_find_feature_by_name:
 * @plugin: plugin to get the feature from
 * @name: The name of the feature to find
 *
 * Find a feature of the given name in the given plugin.
 *
 * Returns: a GstPluginFeature or NULL if the feature was not found.
 */
GstPluginFeature *
gst_plugin_find_feature_by_name (GstPlugin * plugin, const gchar * name)
{
  GList *walk;
  GstPluginFeature *result = NULL;

  g_return_val_if_fail (name != NULL, NULL);

  walk = gst_filter_run (plugin->features,
      (GstFilterFunc) gst_plugin_feature_name_filter, TRUE, (void *) name);

  if (walk) {
    result = GST_PLUGIN_FEATURE (walk->data);

    gst_object_ref (result);
    gst_plugin_feature_list_free (walk);
  }

  return result;
}
#endif

/**
 * gst_plugin_load_by_name:
 * @name: name of plugin to load
 *
 * Load the named plugin. Refs the plugin.
 *
 * Returns: A reference to a loaded plugin, or NULL on error.
 */
GstPlugin *
gst_plugin_load_by_name (const gchar * name)
{
  GstPlugin *plugin, *newplugin;
  GError *error = NULL;

  GST_DEBUG ("looking up plugin %s in default registry", name);
  plugin = gst_registry_find_plugin (gst_registry_get_default (), name);
  if (plugin) {
    GST_DEBUG ("loading plugin %s from file %s", name, plugin->filename);
    newplugin = gst_plugin_load_file (plugin->filename, &error);
    gst_object_unref (plugin);

    if (!newplugin) {
      GST_WARNING ("load_plugin error: %s", error->message);
      g_error_free (error);
      return NULL;
    }
    /* newplugin was reffed by load_file */
    return newplugin;
  }

  GST_DEBUG ("Could not find plugin %s in registry", name);
  return NULL;
}

/**
 * gst_plugin_load:
 * @plugin: plugin to load
 *
 * Loads @plugin. Note that the *return value* is the loaded plugin; @plugin is
 * untouched. The normal use pattern of this function goes like this:
 *
 * <programlisting>
 * GstPlugin *loaded_plugin;
 * loaded_plugin = gst_plugin_load (plugin);
 * // presumably, we're no longer interested in the potentially-unloaded plugin
 * gst_object_unref (plugin);
 * plugin = loaded_plugin;
 * </programlisting>
 *
 * Returns: A reference to a loaded plugin, or NULL on error.
 */
GstPlugin *
gst_plugin_load (GstPlugin * plugin)
{
  GError *error = NULL;
  GstPlugin *newplugin;

  if (gst_plugin_is_loaded (plugin)) {
    return plugin;
  }

  if (!(newplugin = gst_plugin_load_file (plugin->filename, &error)))
    goto load_error;

  return newplugin;

load_error:
  {
    GST_WARNING ("load_plugin error: %s", error->message);
    g_error_free (error);
    return NULL;
  }
}

/**
 * gst_plugin_list_free:
 * @list: list of #GstPlugin
 *
 * Unrefs each member of @list, then frees the list.
 */
void
gst_plugin_list_free (GList * list)
{
  GList *g;

  for (g = list; g; g = g->next) {
    gst_object_unref (GST_PLUGIN_CAST (g->data));
  }
  g_list_free (list);
}

/* ===== plugin dependencies ===== */

/* Scenarios:
 * ENV + xyz     where ENV can contain multiple values separated by SEPARATOR
 *               xyz may be "" (if ENV contains path to file rather than dir)
 * ENV + *xyz   same as above, but xyz acts as suffix filter
 * ENV + xyz*   same as above, but xyz acts as prefix filter (is this needed?)
 * ENV + *xyz*  same as above, but xyz acts as strstr filter (is this needed?)
 * 
 * same as above, with additional paths hard-coded at compile-time:
 *   - only check paths + ... if ENV is not set or yields not paths
 *   - always check paths + ... in addition to ENV
 *
 * When user specifies set of environment variables, he/she may also use e.g.
 * "HOME/.mystuff/plugins", and we'll expand the content of $HOME with the
 * remainder 
 */

/* we store in registry:
 *  sets of:
 *   { 
 *     - environment variables (array of strings)
 *     - last hash of env variable contents (uint) (so we can avoid doing stats
 *       if one of the env vars has changed; premature optimisation galore)
 *     - hard-coded paths (array of strings)
 *     - xyz filename/suffix/prefix strings (array of strings)
 *     - flags (int)
 *     - last hash of file/dir stats (int)
 *   }
 *   (= struct GstPluginDep)
 */

static guint
gst_plugin_ext_dep_get_env_vars_hash (GstPlugin * plugin, GstPluginDep * dep)
{
  gchar **e;
  guint hash;

  /* there's no deeper logic to what we do here; all we want to know (when
   * checking if the plugin needs to be rescanned) is whether the content of
   * one of the environment variables in the list is different from when it
   * was last scanned */
  hash = 0;
  for (e = dep->env_vars; e != NULL && *e != NULL; ++e) {
    const gchar *val;
    gchar env_var[256];

    /* order matters: "val",NULL needs to yield a different hash than
     * NULL,"val", so do a shift here whether the var is set or not */
    hash = hash << 5;

    /* want environment variable at beginning of string */
    if (!g_ascii_isalnum (**e)) {
      GST_WARNING_OBJECT (plugin, "string prefix is not a valid environment "
          "variable string: %s", *e);
      continue;
    }

    /* user is allowed to specify e.g. "HOME/.pitivi/plugins" */
    g_strlcpy (env_var, *e, sizeof (env_var));
    g_strdelimit (env_var, "/\\", '\0');

    if ((val = g_getenv (env_var)))
      hash += g_str_hash (val);
  }

  return hash;
}

gboolean
_priv_plugin_deps_env_vars_changed (GstPlugin * plugin)
{
  GList *l;

  for (l = plugin->priv->deps; l != NULL; l = l->next) {
    GstPluginDep *dep = l->data;

    if (dep->env_hash != gst_plugin_ext_dep_get_env_vars_hash (plugin, dep))
      return TRUE;
  }

  return FALSE;
}

static GList *
gst_plugin_ext_dep_extract_env_vars_paths (GstPlugin * plugin,
    GstPluginDep * dep)
{
  gchar **evars;
  GList *paths = NULL;

  for (evars = dep->env_vars; evars != NULL && *evars != NULL; ++evars) {
    const gchar *e;
    gchar **components;

    /* want environment variable at beginning of string */
    if (!g_ascii_isalnum (**evars)) {
      GST_WARNING_OBJECT (plugin, "string prefix is not a valid environment "
          "variable string: %s", *evars);
      continue;
    }

    /* user is allowed to specify e.g. "HOME/.pitivi/plugins", which we want to
     * split into the env_var name component and the path component */
    components = g_strsplit_set (*evars, "/\\", 2);
    g_assert (components != NULL);

    e = g_getenv (components[0]);
    GST_LOG_OBJECT (plugin, "expanding %s = '%s' (path suffix: %s)",
        components[0], GST_STR_NULL (e), GST_STR_NULL (components[1]));

    if (components[1] != NULL) {
      g_strdelimit (components[1], "/\\", G_DIR_SEPARATOR);
    }

    if (e != NULL && *e != '\0') {
      gchar **arr;
      guint i;

      arr = g_strsplit (e, G_SEARCHPATH_SEPARATOR_S, -1);

      for (i = 0; arr != NULL && arr[i] != NULL; ++i) {
        gchar *full_path;

        if (!g_path_is_absolute (arr[i])) {
          GST_INFO_OBJECT (plugin, "ignoring environment variable content '%s'"
              ": either not an absolute path or not a path at all", arr[i]);
          continue;
        }

        if (components[1] != NULL) {
          full_path = g_build_filename (arr[i], components[1], NULL);
        } else {
          full_path = g_strdup (arr[i]);
        }

        if (!g_list_find_custom (paths, full_path, (GCompareFunc) strcmp)) {
          GST_LOG_OBJECT (plugin, "path: '%s'", full_path);
          paths = g_list_prepend (paths, full_path);
          full_path = NULL;
        } else {
          GST_LOG_OBJECT (plugin, "path: '%s' (duplicate,ignoring)", full_path);
          g_free (full_path);
        }
      }

      g_strfreev (arr);
    }

    g_strfreev (components);
  }

  GST_LOG_OBJECT (plugin, "Extracted %d paths from environment",
      g_list_length (paths));

  return paths;
}

static guint
gst_plugin_ext_dep_get_hash_from_stat_entry (struct stat *s)
{
  if (!(s->st_mode & (S_IFDIR | S_IFREG)))
    return (guint) - 1;

  /* completely random formula */
  return ((s->st_size << 3) + (s->st_mtime << 5)) ^ s->st_ctime;
}

static gboolean
gst_plugin_ext_dep_direntry_matches (GstPlugin * plugin, const gchar * entry,
    const gchar ** filenames, GstPluginDependencyFlags flags)
{
  /* no filenames specified, match all entries for now (could probably
   * optimise by just taking the dir stat hash or so) */
  if (filenames == NULL || *filenames == NULL || **filenames == '\0')
    return TRUE;

  while (*filenames != NULL) {
    /* suffix match? */
    if (((flags & GST_PLUGIN_DEPENDENCY_FLAG_FILE_NAME_IS_SUFFIX)) &&
        g_str_has_suffix (entry, *filenames)) {
      return TRUE;
      /* else it's an exact match that's needed */
    } else if (strcmp (entry, *filenames) == 0) {
      return TRUE;
    }
    GST_LOG ("%s does not match %s, flags=0x%04x", entry, *filenames, flags);
    ++filenames;
  }
  return FALSE;
}

static guint
gst_plugin_ext_dep_scan_dir_and_match_names (GstPlugin * plugin,
    const gchar * path, const gchar ** filenames,
    GstPluginDependencyFlags flags, int depth)
{
  const gchar *entry;
  gboolean recurse_dirs;
  GError *err = NULL;
  GDir *dir;
  guint hash = 0;

  recurse_dirs = !!(flags & GST_PLUGIN_DEPENDENCY_FLAG_RECURSE);

  dir = g_dir_open (path, 0, &err);
  if (dir == NULL) {
    GST_DEBUG_OBJECT (plugin, "g_dir_open(%s) failed: %s", path, err->message);
    g_error_free (err);
    return (guint) - 1;
  }

  /* FIXME: we're assuming here that we always get the directory entries in
   * the same order, and not in a random order */
  while ((entry = g_dir_read_name (dir))) {
    gboolean have_match;
    struct stat s;
    gchar *full_path;
    guint fhash;

    have_match =
        gst_plugin_ext_dep_direntry_matches (plugin, entry, filenames, flags);

    /* avoid the stat if possible */
    if (!have_match && !recurse_dirs)
      continue;

    full_path = g_build_filename (path, entry, NULL);
    if (g_stat (full_path, &s) < 0) {
      fhash = (guint) - 1;
      GST_LOG_OBJECT (plugin, "stat: %s (error: %s)", full_path,
          g_strerror (errno));
    } else if (have_match) {
      fhash = gst_plugin_ext_dep_get_hash_from_stat_entry (&s);
      GST_LOG_OBJECT (plugin, "stat: %s (result: %u)", full_path, fhash);
    } else if ((s.st_mode & (S_IFDIR))) {
      fhash = gst_plugin_ext_dep_scan_dir_and_match_names (plugin, full_path,
          filenames, flags, depth + 1);
    } else {
      /* it's not a name match, we want to recurse, but it's not a directory */
      g_free (full_path);
      continue;
    }

    hash = (hash + fhash) << 1;
    g_free (full_path);
  }

  g_dir_close (dir);
  return hash;
}

static guint
gst_plugin_ext_dep_scan_path_with_filenames (GstPlugin * plugin,
    const gchar * path, const gchar ** filenames,
    GstPluginDependencyFlags flags)
{
  const gchar *empty_filenames[] = { "", NULL };
  gboolean recurse_into_dirs, partial_names;
  guint i, hash = 0;

  /* to avoid special-casing below (FIXME?) */
  if (filenames == NULL || *filenames == NULL)
    filenames = empty_filenames;

  recurse_into_dirs = !!(flags & GST_PLUGIN_DEPENDENCY_FLAG_RECURSE);
  partial_names = !!(flags & GST_PLUGIN_DEPENDENCY_FLAG_FILE_NAME_IS_SUFFIX);

  /* if we can construct the exact paths to check with the data we have, just
   * stat them one by one; this is more efficient than opening the directory
   * and going through each entry to see if it matches one of our filenames. */
  if (!recurse_into_dirs && !partial_names) {
    for (i = 0; filenames[i] != NULL; ++i) {
      struct stat s;
      gchar *full_path;
      guint fhash;

      full_path = g_build_filename (path, filenames[i], NULL);
      if (g_stat (full_path, &s) < 0) {
        fhash = (guint) - 1;
        GST_LOG_OBJECT (plugin, "stat: %s (error: %s)", full_path,
            g_strerror (errno));
      } else {
        fhash = gst_plugin_ext_dep_get_hash_from_stat_entry (&s);
        GST_LOG_OBJECT (plugin, "stat: %s (result: %08x)", full_path, fhash);
      }
      hash = (hash + fhash) << 1;
      g_free (full_path);
    }
  } else {
    hash = gst_plugin_ext_dep_scan_dir_and_match_names (plugin, path,
        filenames, flags, 0);
  }

  return hash;
}

static guint
gst_plugin_ext_dep_get_stat_hash (GstPlugin * plugin, GstPluginDep * dep)
{
  gboolean paths_are_default_only;
  GList *scan_paths;
  guint scan_hash = 0;

  GST_LOG_OBJECT (plugin, "start");

  paths_are_default_only =
      dep->flags & GST_PLUGIN_DEPENDENCY_FLAG_PATHS_ARE_DEFAULT_ONLY;

  scan_paths = gst_plugin_ext_dep_extract_env_vars_paths (plugin, dep);

  if (scan_paths == NULL || !paths_are_default_only) {
    gchar **paths;

    for (paths = dep->paths; paths != NULL && *paths != NULL; ++paths) {
      const gchar *path = *paths;

      if (!g_list_find_custom (scan_paths, path, (GCompareFunc) strcmp)) {
        GST_LOG_OBJECT (plugin, "path: '%s'", path);
        scan_paths = g_list_prepend (scan_paths, g_strdup (path));
      } else {
        GST_LOG_OBJECT (plugin, "path: '%s' (duplicate, ignoring)", path);
      }
    }
  }

  /* not that the order really matters, but it makes debugging easier */
  scan_paths = g_list_reverse (scan_paths);

  while (scan_paths != NULL) {
    const gchar *path = scan_paths->data;

    scan_hash += gst_plugin_ext_dep_scan_path_with_filenames (plugin, path,
        (const gchar **) dep->names, dep->flags);
    scan_hash = scan_hash << 1;

    g_free (scan_paths->data);
    scan_paths = g_list_delete_link (scan_paths, scan_paths);
  }

  GST_LOG_OBJECT (plugin, "done, scan_hash: %08x", scan_hash);
  return scan_hash;
}

gboolean
_priv_plugin_deps_files_changed (GstPlugin * plugin)
{
  GList *l;

  for (l = plugin->priv->deps; l != NULL; l = l->next) {
    GstPluginDep *dep = l->data;

    if (dep->stat_hash != gst_plugin_ext_dep_get_stat_hash (plugin, dep))
      return TRUE;
  }

  return FALSE;
}

static void
gst_plugin_ext_dep_free (GstPluginDep * dep)
{
  g_strfreev (dep->env_vars);
  g_strfreev (dep->paths);
  g_strfreev (dep->names);
  g_free (dep);
}

static gboolean
gst_plugin_ext_dep_strv_equal (gchar ** arr1, gchar ** arr2)
{
  if (arr1 == arr2)
    return TRUE;
  if (arr1 == NULL || arr2 == NULL)
    return FALSE;
  for (; *arr1 != NULL && *arr2 != NULL; ++arr1, ++arr2) {
    if (strcmp (*arr1, *arr2) != 0)
      return FALSE;
  }
  return (*arr1 == *arr2);
}

static gboolean
gst_plugin_ext_dep_equals (GstPluginDep * dep, const gchar ** env_vars,
    const gchar ** paths, const gchar ** names, GstPluginDependencyFlags flags)
{
  if (dep->flags != flags)
    return FALSE;

  return gst_plugin_ext_dep_strv_equal (dep->env_vars, (gchar **) env_vars) &&
      gst_plugin_ext_dep_strv_equal (dep->paths, (gchar **) paths) &&
      gst_plugin_ext_dep_strv_equal (dep->names, (gchar **) names);
}

/**
 * gst_plugin_add_dependency:
 * @plugin: a #GstPlugin
 * @env_vars: NULL-terminated array of environent variables affecting the
 *     feature set of the plugin (e.g. an environment variable containing
 *     paths where to look for additional modules/plugins of a library),
 *     or NULL. Environment variable names may be followed by a path component
 *      which will be added to the content of the environment variable, e.g.
 *      "HOME/.mystuff/plugins".
 * @paths: NULL-terminated array of directories/paths where dependent files
 *     may be.
 * @names: NULL-terminated array of file names (or file name suffixes,
 *     depending on @flags) to be used in combination with the paths from
 *     @paths and/or the paths extracted from the environment variables in
 *     @env_vars, or NULL.
 * @flags: optional flags, or #GST_PLUGIN_DEPENDENCY_FLAG_NONE
 *
 * Make GStreamer aware of external dependencies which affect the feature
 * set of this plugin (ie. the elements or typefinders associated with it).
 *
 * GStreamer will re-inspect plugins with external dependencies whenever any
 * of the external dependencies change. This is useful for plugins which wrap
 * other plugin systems, e.g. a plugin which wraps a plugin-based visualisation
 * library and makes visualisations available as GStreamer elements, or a
 * codec loader which exposes elements and/or caps dependent on what external
 * codec libraries are currently installed.
 *
 * Since: 0.10.22
 */
void
gst_plugin_add_dependency (GstPlugin * plugin, const gchar ** env_vars,
    const gchar ** paths, const gchar ** names, GstPluginDependencyFlags flags)
{
  GstPluginDep *dep;
  GList *l;

  g_return_if_fail (GST_IS_PLUGIN (plugin));
  g_return_if_fail (env_vars != NULL || paths != NULL);

  for (l = plugin->priv->deps; l != NULL; l = l->next) {
    if (gst_plugin_ext_dep_equals (l->data, env_vars, paths, names, flags)) {
      GST_LOG_OBJECT (plugin, "dependency already registered");
      return;
    }
  }

  dep = g_new0 (GstPluginDep, 1);

  dep->env_vars = g_strdupv ((gchar **) env_vars);
  dep->paths = g_strdupv ((gchar **) paths);
  dep->names = g_strdupv ((gchar **) names);
  dep->flags = flags;

  dep->env_hash = gst_plugin_ext_dep_get_env_vars_hash (plugin, dep);
  dep->stat_hash = gst_plugin_ext_dep_get_stat_hash (plugin, dep);

  plugin->priv->deps = g_list_append (plugin->priv->deps, dep);

  GST_DEBUG_OBJECT (plugin, "added dependency:");
  for (; env_vars != NULL && *env_vars != NULL; ++env_vars)
    GST_DEBUG_OBJECT (plugin, " evar: %s", *env_vars);
  for (; paths != NULL && *paths != NULL; ++paths)
    GST_DEBUG_OBJECT (plugin, " path: %s", *paths);
  for (; names != NULL && *names != NULL; ++names)
    GST_DEBUG_OBJECT (plugin, " name: %s", *names);
}

/**
 * gst_plugin_add_dependency_simple:
 * @plugin: the #GstPlugin
 * @env_vars: one or more environent variables (separated by ':', ';' or ','),
 *      or NULL. Environment variable names may be followed by a path component
 *      which will be added to the content of the environment variable, e.g.
 *      "HOME/.mystuff/plugins:MYSTUFF_PLUGINS_PATH"
 * @paths: one ore more directory paths (separated by ':' or ';' or ','),
 *      or NULL. Example: "/usr/lib/mystuff/plugins"
 * @names: one or more file names or file name suffixes (separated by commas),
 *   or NULL
 * @flags: optional flags, or #GST_PLUGIN_DEPENDENCY_FLAG_NONE
 *
 * Make GStreamer aware of external dependencies which affect the feature
 * set of this plugin (ie. the elements or typefinders associated with it).
 *
 * GStreamer will re-inspect plugins with external dependencies whenever any
 * of the external dependencies change. This is useful for plugins which wrap
 * other plugin systems, e.g. a plugin which wraps a plugin-based visualisation
 * library and makes visualisations available as GStreamer elements, or a
 * codec loader which exposes elements and/or caps dependent on what external
 * codec libraries are currently installed.
 *
 * Convenience wrapper function for gst_plugin_add_dependency() which
 * takes simple strings as arguments instead of string arrays, with multiple
 * arguments separated by predefined delimiters (see above).
 *
 * Since: 0.10.22
 */
void
gst_plugin_add_dependency_simple (GstPlugin * plugin,
    const gchar * env_vars, const gchar * paths, const gchar * names,
    GstPluginDependencyFlags flags)
{
  gchar **a_evars = NULL;
  gchar **a_paths = NULL;
  gchar **a_names = NULL;

  if (env_vars)
    a_evars = g_strsplit_set (env_vars, ":;,", -1);
  if (paths)
    a_paths = g_strsplit_set (paths, ":;,", -1);
  if (names)
    a_names = g_strsplit_set (names, ",", -1);

  gst_plugin_add_dependency (plugin, (const gchar **) a_evars,
      (const gchar **) a_paths, (const gchar **) a_names, flags);

  if (a_evars)
    g_strfreev (a_evars);
  if (a_paths)
    g_strfreev (a_paths);
  if (a_names)
    g_strfreev (a_names);
}
