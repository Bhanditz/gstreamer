/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gstelementfactory.c: GstElementFactory object, support routines
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

#include "gst_private.h"

#include "gstelement.h"
#include "gstregistrypool.h"
#include "gstinfo.h"

GST_DEBUG_CATEGORY_STATIC (element_factory_debug);
#define GST_CAT_DEFAULT element_factory_debug

static void 		gst_element_factory_class_init 		(GstElementFactoryClass *klass);
static void 		gst_element_factory_init 		(GstElementFactory *factory);

static void 		gst_element_factory_unload_thyself 	(GstPluginFeature *feature);

static GstPluginFeatureClass *parent_class = NULL;
/* static guint gst_element_factory_signals[LAST_SIGNAL] = { 0 }; */

GType 
gst_element_factory_get_type (void) 
{
  static GType elementfactory_type = 0;

  if (!elementfactory_type) {
    static const GTypeInfo elementfactory_info = {
      sizeof (GstElementFactoryClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_element_factory_class_init,
      NULL,
      NULL,
      sizeof(GstElementFactory),
      0,
      (GInstanceInitFunc) gst_element_factory_init,
      NULL
    };
    elementfactory_type = g_type_register_static (GST_TYPE_PLUGIN_FEATURE, 
		    				  "GstElementFactory", &elementfactory_info, 0);
    GST_DEBUG_CATEGORY_INIT (element_factory_debug, "GST_ELEMENT_FACTORY", 
	    GST_DEBUG_BOLD | GST_DEBUG_FG_WHITE | GST_DEBUG_BG_RED,
	    "element factories keep information about installed elements");
  }
  return elementfactory_type;
}
static void
gst_element_factory_class_init (GstElementFactoryClass *klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstPluginFeatureClass *gstpluginfeature_class;

  gobject_class = (GObjectClass*)klass;
  gstobject_class = (GstObjectClass*)klass;
  gstpluginfeature_class = (GstPluginFeatureClass*) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstpluginfeature_class->unload_thyself = 	GST_DEBUG_FUNCPTR (gst_element_factory_unload_thyself);
}
static void
gst_element_factory_init (GstElementFactory *factory)
{
  factory->padtemplates = NULL;
  factory->numpadtemplates = 0;

  factory->interfaces = NULL;
}
/**
 * gst_element_factory_find:
 * @name: name of factory to find
 *
 * Search for an element factory of the given name.
 *
 * Returns: #GstElementFactory if found, NULL otherwise
 */
GstElementFactory*
gst_element_factory_find (const gchar *name)
{
  GstPluginFeature *feature;

  g_return_val_if_fail(name != NULL, NULL);

  feature = gst_registry_pool_find_feature (name, GST_TYPE_ELEMENT_FACTORY);
  if (feature)
    return GST_ELEMENT_FACTORY (feature);

  /* this should be an ERROR */
  GST_DEBUG ("no such elementfactory \"%s\"", name);
  return NULL;
}

void
__gst_element_details_clear (GstElementDetails *dp)
{
  g_free (dp->longname);
  dp->longname = NULL;
  g_free (dp->klass);
  dp->klass = NULL;
  g_free (dp->description);
  dp->description = NULL;
  g_free (dp->author);
  dp->author = NULL;
}
void
__gst_element_details_set (GstElementDetails *dest, const GstElementDetails *src)
{
  dest->longname = g_strdup (src->longname);
  dest->klass = g_strdup (src->klass);
  dest->description = g_strdup (src->description);
  dest->author = g_strdup (src->author);
}
void
__gst_element_details_copy (GstElementDetails *dest, const GstElementDetails *src)
{
  __gst_element_details_clear (dest);
  __gst_element_details_set (dest, src);
}
static void
gst_element_factory_cleanup (GstElementFactory *factory)
{
  __gst_element_details_clear (&factory->details);
  if (factory->type) {
    g_type_class_unref (g_type_class_peek (factory->type));
    factory->type = 0;
  }

  g_list_foreach (factory->padtemplates, (GFunc) g_object_unref, NULL);
  g_list_free (factory->padtemplates);
  factory->padtemplates = NULL;
  factory->numpadtemplates = 0;
  
  g_list_foreach (factory->interfaces, (GFunc) g_free, NULL);
  g_list_free (factory->interfaces);
  factory->interfaces = NULL;
}
/**
 * gst_element_register:
 * @plugin:
 * @name: name of elements of this type
 * @rank: rank of element (higher rank means more importance when autoplugging)
 * @type: GType of element to register
 *
 * Create a new elementfactory capable of insantiating objects of the
 * given type.
 *
 * Returns: TRUE, if the registering succeeded, FALSE on error
 */
gboolean
gst_element_register (GstPlugin *plugin, const gchar *name, guint rank, GType type)
{
  GstElementFactory *factory;
  GType *interfaces;
  guint n_interfaces, i;
  GstElementClass *klass;

  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (g_type_is_a (type, GST_TYPE_ELEMENT), FALSE);

  factory = gst_element_factory_find (name);

  if (!factory) {
    klass = GST_ELEMENT_CLASS (g_type_class_ref (type));
    factory = GST_ELEMENT_FACTORY (g_object_new (GST_TYPE_ELEMENT_FACTORY, NULL));
    gst_plugin_feature_set_name (GST_PLUGIN_FEATURE (factory), name);
    GST_LOG_OBJECT (factory, "Created new elementfactory for type %s", g_type_name (type));
  } else {
    g_return_val_if_fail (factory->type == 0, FALSE);
    klass = GST_ELEMENT_CLASS (g_type_class_ref (type));
    gst_element_factory_cleanup (factory);
    GST_LOG_OBJECT (factory, "Reuse existing elementfactory for type %s", g_type_name (type));
  }

  factory->type = type;
  __gst_element_details_copy (&factory->details, &klass->details);
  factory->padtemplates = g_list_copy (klass->padtemplates);
  g_list_foreach (factory->padtemplates, (GFunc) g_object_ref, NULL);
  factory->numpadtemplates = klass->numpadtemplates;

  interfaces = g_type_interfaces (type, &n_interfaces);
  for (i = 0; i < n_interfaces; i++) {
    __gst_element_factory_add_interface (factory, g_type_name (interfaces[i]));
  }
  g_free (interfaces);
  
  gst_plugin_feature_set_rank (GST_PLUGIN_FEATURE (factory), rank);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}
/**
 * gst_element_factory_create:
 * @factory: factory to instantiate
 * @name: name of new element
 *
 * Create a new element of the type defined by the given elementfactory.
 * It will be given the name supplied, since all elements require a name as
 * their first argument.
 *
 * Returns: new #GstElement or NULL if the element couldn't be created
 */
GstElement*
gst_element_factory_create (GstElementFactory *factory,
                           const gchar *name)
{
  GstElement *element;
  GstElementClass *oclass;

  g_return_val_if_fail (factory != NULL, NULL);

  if (!gst_plugin_feature_ensure_loaded (GST_PLUGIN_FEATURE (factory))) {
    GST_INFO ("could not load element factory for element \"%s\"", name);
    return NULL;
  }

  GST_LOG_OBJECT (factory, "creating element (name \"%s\", type %d)", 
           GST_STR_NULL (name), (gint) factory->type);

  if (factory->type == 0) {
      g_critical ("Factory for `%s' has no type",
		  GST_PLUGIN_FEATURE_NAME (factory));
      return NULL;
  }

  oclass = GST_ELEMENT_CLASS (g_type_class_ref (factory->type)); 	 
  if (oclass->elementfactory == NULL) { 	 
    GST_DEBUG ("class %s", GST_PLUGIN_FEATURE_NAME (factory)); 	 
    oclass->elementfactory = factory;
  }

  /* create an instance of the element */
  element = GST_ELEMENT (g_object_new (factory->type, NULL));
  g_assert (element != NULL);

  g_type_class_unref (oclass);

  gst_object_set_name (GST_OBJECT (element), name);

  return element;
}
/**
 * gst_element_factory_make:
 * @factoryname: a named factory to instantiate
 * @name: name of new element
 *
 * Create a new element of the type defined by the given element factory.
 * If name is NULL, then the element will receive a guaranteed unique name,
 * consisting of the element factory name and a number.
 * If name is given, it will be given the name supplied.
 *
 * Returns: new #GstElement or NULL if unable to create element
 */
GstElement*
gst_element_factory_make (const gchar *factoryname, const gchar *name)
{
  GstElementFactory *factory;
  GstElement *element;

  g_return_val_if_fail (factoryname != NULL, NULL);

  GST_LOG ("gstelementfactory: make \"%s\" \"%s\"", 
           factoryname, GST_STR_NULL (name));

  /* gst_plugin_load_element_factory (factoryname); */
  factory = gst_element_factory_find (factoryname);
  if (factory == NULL) {
    GST_INFO ("no such element factory \"%s\"!",
	      factoryname);
    return NULL;
  }
  element = gst_element_factory_create (factory, name);
  if (element == NULL) {
    GST_INFO_OBJECT (factory, "couldn't create instance!");
    return NULL;
  }

  return element;
}
void
__gst_element_factory_add_pad_template (GstElementFactory *factory,
					GstPadTemplate *templ)
{
  g_return_if_fail (factory != NULL);
  g_return_if_fail (templ != NULL);

  gst_object_ref (GST_OBJECT (templ));
  gst_object_sink (GST_OBJECT (templ));

  factory->padtemplates = g_list_append (factory->padtemplates, templ);
  factory->numpadtemplates++;
}
/**
 * gst_element_factory_get_element_type:
 * @factory: factory to get managed #GType from
 * 
 * Get the #GType for elements managed by this factory
 *
 * Returns: the #GType for elements managed by this factory
 */
GType
gst_element_factory_get_element_type (GstElementFactory *factory)
{
  g_return_val_if_fail (GST_IS_ELEMENT_FACTORY (factory), 0);

  return factory->type;
}
/**
 * gst_element_factory_get_longname:
 * @factory: a #GstElementFactory
 * 
 * Gets the longname for this factory
 *
 * Returns: the longname
 */
G_CONST_RETURN gchar *
gst_element_factory_get_longname (GstElementFactory *factory)
{
  g_return_val_if_fail (GST_IS_ELEMENT_FACTORY (factory), NULL);

  return factory->details.longname;
}
/**
 * gst_element_factory_get_class:
 * @factory: a #GstElementFactory
 * 
 * Gets the class for this factory.
 *
 * Returns: the class
 */
G_CONST_RETURN gchar *
gst_element_factory_get_klass (GstElementFactory *factory)
{
  g_return_val_if_fail (GST_IS_ELEMENT_FACTORY (factory), NULL);

  return factory->details.klass;
}
/**
 * gst_element_factory_get_description:
 * @factory: a #GstElementFactory
 * 
 * Gets the description for this factory.
 *
 * Returns: the description
 */
G_CONST_RETURN gchar *
gst_element_factory_get_description (GstElementFactory *factory)
{
  g_return_val_if_fail (GST_IS_ELEMENT_FACTORY (factory), NULL);

  return factory->details.description;
}
/**
 * gst_element_factory_get_author:
 * @factory: a #GstElementFactory
 * 
 * Gets the author for this factory.
 *
 * Returns: the author
 */
G_CONST_RETURN gchar *
gst_element_factory_get_author (GstElementFactory *factory)
{
  g_return_val_if_fail (GST_IS_ELEMENT_FACTORY (factory), NULL);

  return factory->details.author;
}
/**
 * gst_element_factory_get_num_pad_templates:
 * @factory: a #GstElementFactory
 * 
 * Gets the number of pad_templates in this factory.
 *
 * Returns: the number of pad_templates
 */
guint
gst_element_factory_get_num_pad_templates (GstElementFactory *factory)
{
  g_return_val_if_fail (GST_IS_ELEMENT_FACTORY (factory), 0);

  return factory->numpadtemplates;
}
/**
 * __gst_element_factory_add_interface:
 * @elementfactory: The elementfactory to add the interface to
 * @interfacename: Name of the interface
 *
 * Adds the given interfacename to the list of implemented interfaces of the
 * element.
 */
void
__gst_element_factory_add_interface (GstElementFactory *elementfactory, const gchar *interfacename)
{
  g_return_if_fail (GST_IS_ELEMENT_FACTORY (elementfactory));
  g_return_if_fail (interfacename != NULL);
  g_return_if_fail (interfacename[0] != '\0'); /* no empty string */
  
  elementfactory->interfaces = g_list_prepend (elementfactory->interfaces, g_strdup (interfacename));
}
/**
 * gst_element_factory_get_pad_templates:
 * @factory: a #GstElementFactory
 * 
 * Gets the #Glist of pad templates for this factory.
 *
 * Returns: the padtemplates
 */
G_CONST_RETURN GList *
gst_element_factory_get_pad_templates (GstElementFactory *factory)
{
  g_return_val_if_fail (GST_IS_ELEMENT_FACTORY (factory), NULL);

  return factory->padtemplates;
}
/**
 * gst_element_factory_can_src_caps :
 * @factory: factory to query
 * @caps: the caps to check
 *
 * Checks if the factory can source the given capability.
 *
 * Returns: true if it can src the capabilities
 */
gboolean
gst_element_factory_can_src_caps (GstElementFactory *factory,
		                 GstCaps *caps)
{
  GList *templates;

  g_return_val_if_fail(factory != NULL, FALSE);
  g_return_val_if_fail(caps != NULL, FALSE);

  templates = factory->padtemplates;

  while (templates) {
    GstPadTemplate *template = (GstPadTemplate *)templates->data;

    if (template->direction == GST_PAD_SRC) {
      if (gst_caps_is_always_compatible (GST_PAD_TEMPLATE_CAPS (template), caps))
	return TRUE;
    }
    templates = g_list_next (templates);
  }

  return FALSE;
}
/**
 * gst_element_factory_can_sink_caps :
 * @factory: factory to query
 * @caps: the caps to check
 *
 * Checks if the factory can sink the given capability.
 *
 * Returns: true if it can sink the capabilities
 */
gboolean
gst_element_factory_can_sink_caps (GstElementFactory *factory,
		                  GstCaps *caps)
{
  GList *templates;

  g_return_val_if_fail(factory != NULL, FALSE);
  g_return_val_if_fail(caps != NULL, FALSE);

  templates = factory->padtemplates;

  while (templates) {
    GstPadTemplate *template = (GstPadTemplate *)templates->data;

    if (template->direction == GST_PAD_SINK) {
      if (gst_caps_is_always_compatible (caps, GST_PAD_TEMPLATE_CAPS (template)))
	return TRUE;
    }
    templates = g_list_next (templates);
  }

  return FALSE;
}
static void
gst_element_factory_unload_thyself (GstPluginFeature *feature)
{
  GstElementFactory *factory;

  factory = GST_ELEMENT_FACTORY (feature);

  if (factory->type) {
    g_type_class_unref (g_type_class_peek (factory->type));
    factory->type = 0;
  }
}
