/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstautoplug.c: Autoplugging of pipelines
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

/* #define GST_DEBUG_ENABLED */

#include <gst/gstconfig.h>

#include "gst_private.h"

#include "gstautoplug.h"
#include "gstregistry.h"
#include "gstlog.h"

enum {
  NEW_OBJECT,
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

static void     gst_autoplug_class_init (GstAutoplugClass *klass);
static void     gst_autoplug_init       (GstAutoplug *autoplug);

static GstObjectClass *parent_class = NULL;
static guint gst_autoplug_signals[LAST_SIGNAL] = { 0 };

GType gst_autoplug_get_type(void)
{
  static GType autoplug_type = 0;

  if (!autoplug_type) {
    static const GTypeInfo autoplug_info = {
      sizeof(GstAutoplugClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_autoplug_class_init,
      NULL,
      NULL,
      sizeof(GstAutoplug),
      4,
      (GInstanceInitFunc)gst_autoplug_init,
      NULL
    };
    autoplug_type = g_type_register_static (GST_TYPE_OBJECT, "GstAutoplug", &autoplug_info, G_TYPE_FLAG_ABSTRACT);
  }
  return autoplug_type;
}

static void
gst_autoplug_class_init(GstAutoplugClass *klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass*) klass;
  gstobject_class = (GstObjectClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_OBJECT);

  gst_autoplug_signals[NEW_OBJECT] =
    g_signal_new ("new_object", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstAutoplugClass, new_object), NULL, NULL,
                    g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1,
                    GST_TYPE_OBJECT);

}

static void gst_autoplug_init(GstAutoplug *autoplug)
{
}

/**
 * gst_autoplug_signal_new_object:
 * @autoplug: The autoplugger to emit the signal 
 * @object: The object that is passed to the signal
 *
 * Emit a new_object signal. autopluggers are supposed to
 * emit this signal whenever a new object has been added to 
 * the autoplugged pipeline.
 * 
 */
void
gst_autoplug_signal_new_object (GstAutoplug *autoplug, GstObject *object)
{
  g_signal_emit (G_OBJECT (autoplug), gst_autoplug_signals[NEW_OBJECT], 0, object);
}


/**
 * gst_autoplug_to_caps:
 * @autoplug: The autoplugger perform the autoplugging
 * @srccaps: The source cpabilities
 * @sinkcaps: The target capabilities
 * @...: more target capabilities
 *
 * Perform the autoplugging procedure on the given autoplugger. 
 * The src caps will be connected to the sink caps.
 * 
 * Returns: A new Element that connects the src caps to the sink caps.
 */
GstElement*
gst_autoplug_to_caps (GstAutoplug *autoplug, GstCaps *srccaps, GstCaps *sinkcaps, ...)
{
  GstAutoplugClass *oclass;
  GstElement *element = NULL;
  va_list args;

  va_start (args, sinkcaps);

  oclass = GST_AUTOPLUG_CLASS (G_OBJECT_GET_CLASS(autoplug));
  if (oclass->autoplug_to_caps)
    element = (oclass->autoplug_to_caps) (autoplug, srccaps, sinkcaps, args);

  va_end (args);

  return element;
}

/**
 * gst_autoplug_to_renderers:
 * @autoplug: The autoplugger perform the autoplugging
 * @srccaps: The source cpabilities
 * @target: The target element 
 * @...: more target elements
 *
 * Perform the autoplugging procedure on the given autoplugger. 
 * The src caps will be connected to the target elements.
 * 
 * Returns: A new Element that connects the src caps to the target elements.
 */
GstElement*
gst_autoplug_to_renderers (GstAutoplug *autoplug, GstCaps *srccaps, GstElement *target, ...)
{
  GstAutoplugClass *oclass;
  GstElement *element = NULL;
  va_list args;

  va_start (args, target);

  oclass = GST_AUTOPLUG_CLASS (G_OBJECT_GET_CLASS(autoplug));
  if (oclass->autoplug_to_renderers)
    element = (oclass->autoplug_to_renderers) (autoplug, srccaps, target, args);

  va_end (args);

  return element;
}

static void 		gst_autoplug_factory_class_init 		(GstAutoplugFactoryClass *klass);
static void 		gst_autoplug_factory_init 		(GstAutoplugFactory *factory);

static GstPluginFeatureClass *factory_parent_class = NULL;
/* static guint gst_autoplug_factory_signals[LAST_SIGNAL] = { 0 }; */

GType 
gst_autoplug_factory_get_type (void) 
{
  static GType autoplugfactory_type = 0;

  if (!autoplugfactory_type) {
    static const GTypeInfo autoplugfactory_info = {
      sizeof (GstAutoplugFactoryClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_autoplug_factory_class_init,
      NULL,
      NULL,
      sizeof(GstAutoplugFactory),
      0,
      (GInstanceInitFunc) gst_autoplug_factory_init,
      NULL
    };
    autoplugfactory_type = g_type_register_static (GST_TYPE_PLUGIN_FEATURE, 
		    				  "GstAutoplugFactory", &autoplugfactory_info, 0);
  }
  return autoplugfactory_type;
}

static void
gst_autoplug_factory_class_init (GstAutoplugFactoryClass *klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstPluginFeatureClass *gstpluginfeature_class;

  gobject_class = (GObjectClass*)klass;
  gstobject_class = (GstObjectClass*)klass;
  gstpluginfeature_class = (GstPluginFeatureClass*) klass;

  factory_parent_class = g_type_class_ref (GST_TYPE_PLUGIN_FEATURE);
}

static void
gst_autoplug_factory_init (GstAutoplugFactory *factory)
{
}
	

/**
 * gst_autoplug_factory_new:
 * @name: name of autoplugfactory to create
 * @longdesc: long description of autoplugfactory to create
 * @type: the gtk type of the GstAutoplug element of this factory
 *
 * Create a new autoplugfactory with the given parameters
 *
 * Returns: a new #GstAutoplugFactory.
 */
GstAutoplugFactory*
gst_autoplug_factory_new (const gchar *name, const gchar *longdesc, GType type)
{
  GstAutoplugFactory *factory;

  g_return_val_if_fail(name != NULL, NULL);
  factory = gst_autoplug_factory_find (name);
  if (!factory) {
    factory = GST_AUTOPLUG_FACTORY (g_object_new (GST_TYPE_AUTOPLUG_FACTORY, NULL));
  }

  GST_PLUGIN_FEATURE_NAME (factory) = g_strdup (name);
  if (factory->longdesc)
    g_free (factory->longdesc);
  factory->longdesc = g_strdup (longdesc);
  factory->type = type;

  return factory;
}

/**
 * gst_autoplug_factory_destroy:
 * @factory: factory to destroy
 *
 * Removes the autoplug from the global list.
 */
void
gst_autoplug_factory_destroy (GstAutoplugFactory *factory)
{
  g_return_if_fail (factory != NULL);

  /* we don't free the struct bacause someone might  have a handle to it.. */
}

/**
 * gst_autoplug_factory_find:
 * @name: name of autoplugfactory to find
 *
 * Search for an autoplugfactory of the given name.
 *
 * Returns: #GstAutoplugFactory if found, NULL otherwise
 */
GstAutoplugFactory*
gst_autoplug_factory_find (const gchar *name)
{
  GstPluginFeature *feature;

  g_return_val_if_fail (name != NULL, NULL);

  GST_DEBUG (0,"gstautoplug: find \"%s\"", name);

  feature = gst_registry_pool_find_feature (name, GST_TYPE_AUTOPLUG_FACTORY);
  if (feature)
    return GST_AUTOPLUG_FACTORY (feature);

  return NULL;
}

/**
 * gst_autoplug_factory_create:
 * @factory: the factory used to create the instance
 *
 * Create a new #GstAutoplug instance from the 
 * given autoplugfactory.
 *
 * Returns: A new #GstAutoplug instance.
 */
GstAutoplug*
gst_autoplug_factory_create (GstAutoplugFactory *factory)
{
  GstAutoplug *new = NULL;

  g_return_val_if_fail (factory != NULL, NULL);

  if (gst_plugin_feature_ensure_loaded (GST_PLUGIN_FEATURE (factory))) {
    g_return_val_if_fail (factory->type != 0, NULL);

    new = GST_AUTOPLUG (g_object_new(factory->type,NULL));
  }

  return new;
}

/**
 * gst_autoplug_factory_make:
 * @name: the name of the factory used to create the instance
 *
 * Create a new #GstAutoplug instance from the 
 * autoplugfactory with the given name.
 *
 * Returns: A new #GstAutoplug instance.
 */
GstAutoplug*
gst_autoplug_factory_make (const gchar *name)
{
  GstAutoplugFactory *factory;

  g_return_val_if_fail (name != NULL, NULL);

  factory = gst_autoplug_factory_find (name);

  if (factory == NULL)
    return NULL;

  return gst_autoplug_factory_create (factory);
}
