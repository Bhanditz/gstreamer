/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gststaticautoplug.c: A static Autoplugger of pipelines
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

#include "gststaticautoplug.h"

#include <gst/gst.h>

#define GST_AUTOPLUG_MAX_COST 999999

typedef guint   	(*GstAutoplugCostFunction) (gpointer src, gpointer dest, gpointer data);
typedef const GList*  	(*GstAutoplugListFunction) (gpointer data);


static void     	gst_static_autoplug_class_init	(GstStaticAutoplugClass *klass);
static void     	gst_static_autoplug_init	(GstStaticAutoplug *autoplug);

static GList*		gst_autoplug_func		(gpointer src, gpointer sink,
						 	 GstAutoplugListFunction list_function,
						 	 GstAutoplugCostFunction cost_function,
						 	 gpointer data);



static GstElement*	gst_static_autoplug_to_caps	(GstAutoplug *autoplug, 
						  	 GstCaps *srccaps, GstCaps *sinkcaps, va_list args);

static GstAutoplugClass *parent_class = NULL;

GType gst_static_autoplug_get_type(void)
{
  static GType static_autoplug_type = 0;

  if (!static_autoplug_type) {
    static const GTypeInfo static_autoplug_info = {
      sizeof(GstElementClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_static_autoplug_class_init,
      NULL,
      NULL,
      sizeof(GstElement),
      0,
      (GInstanceInitFunc)gst_static_autoplug_init,
    };
    static_autoplug_type = g_type_register_static (GST_TYPE_AUTOPLUG, "GstStaticAutoplug", &static_autoplug_info, 0);
  }
  return static_autoplug_type;
}

static void
gst_static_autoplug_class_init(GstStaticAutoplugClass *klass)
{
  GstAutoplugClass *gstautoplug_class;

  gstautoplug_class = (GstAutoplugClass*) klass;

  parent_class = g_type_class_ref(GST_TYPE_AUTOPLUG);

  gstautoplug_class->autoplug_to_caps = gst_static_autoplug_to_caps;
}

static void gst_static_autoplug_init(GstStaticAutoplug *autoplug) {
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstAutoplugFactory *factory;

  gst_plugin_set_longname (plugin, "A static autoplugger");

  factory = gst_autoplug_factory_new ("static",
		  "A static autoplugger, it constructs the complete element before running it",
		  gst_static_autoplug_get_type ());

  if (factory != NULL) {
     gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  }
  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gststaticautoplug",
  plugin_init
};

static gboolean
gst_autoplug_can_match (GstElementFactory *src, GstElementFactory *dest)
{
  GList *srctemps, *desttemps;

  srctemps = src->padtemplates;

  while (srctemps) {
    GstPadTemplate *srctemp = (GstPadTemplate *)srctemps->data;

    desttemps = dest->padtemplates;

    while (desttemps) {
      GstPadTemplate *desttemp = (GstPadTemplate *)desttemps->data;

      if (srctemp->direction == GST_PAD_SRC &&
          desttemp->direction == GST_PAD_SINK) {
	if (gst_caps_is_always_compatible (gst_pad_template_get_caps (srctemp), 
				gst_pad_template_get_caps (desttemp))) {
	  GST_DEBUG (GST_CAT_AUTOPLUG_ATTEMPT,
			  "factory \"%s\" can link with factory \"%s\"\n", GST_OBJECT_NAME (src), 
			  GST_OBJECT_NAME (dest));
          return TRUE;
	}
      }

      desttemps = g_list_next (desttemps);
    }
    srctemps = g_list_next (srctemps);
  }
  GST_DEBUG (GST_CAT_AUTOPLUG_ATTEMPT,
		  "factory \"%s\" cannot link with factory \"%s\"\n", GST_OBJECT_NAME (src),
			  GST_OBJECT_NAME (dest));
  return FALSE;
}

static gboolean
gst_autoplug_pads_autoplug_func (GstElement *src, GstPad *pad, GstElement *sink)
{
  const GList *sinkpads;
  gboolean linked = FALSE;

  GST_DEBUG (0,"gstpipeline: autoplug pad link function for \"%s\" to \"%s\"",
		  GST_ELEMENT_NAME(src), GST_ELEMENT_NAME(sink));

  sinkpads = gst_element_get_pad_list(sink);
  while (sinkpads) {
    GstPad *sinkpad = (GstPad *)sinkpads->data;

    /* if we have a match, link the pads */
    if (gst_pad_get_direction(sinkpad)	 == GST_PAD_SINK &&
        !GST_PAD_IS_LINKED(sinkpad))
    {
      if (gst_caps_is_always_compatible (gst_pad_get_caps(pad), gst_pad_get_caps(sinkpad))) {
        gst_pad_link(pad, sinkpad);
        GST_DEBUG (0,"gstpipeline: autolink pad \"%s\" in element %s <-> ", GST_PAD_NAME (pad),
		       GST_ELEMENT_NAME(src));
        GST_DEBUG (0,"pad \"%s\" in element %s", GST_PAD_NAME (sinkpad),
		      GST_ELEMENT_NAME(sink));
        linked = TRUE;
        break;
      }
      else {
	GST_DEBUG (0,"pads incompatible %s, %s", GST_PAD_NAME (pad), GST_PAD_NAME (sinkpad));
      }
    }
    sinkpads = g_list_next(sinkpads);
  }

  if (!linked) {
    GST_DEBUG (0,"gstpipeline: no path to sinks for type");
  }
  return linked;
}

typedef struct {
  GstElement *result;
  GstCaps *endcap;
  gint i;
} dynamic_pad_struct;

static void
autoplug_dynamic_pad (GstElement *element, GstPad *pad, gpointer data)
{
  dynamic_pad_struct *info = (dynamic_pad_struct *)data;
  const GList *pads = gst_element_get_pad_list (element);

  GST_DEBUG (0,"attempting to dynamically create a ghostpad for %s=%s", GST_ELEMENT_NAME (element),
		  GST_PAD_NAME (pad));

  while (pads) {
    GstPad *pad = GST_PAD (pads->data);
    GstPadTemplate *templ = GST_PAD_PAD_TEMPLATE (pad);
    pads = g_list_next (pads);

    if (gst_caps_is_always_compatible (GST_PAD_TEMPLATE_CAPS (templ), info->endcap)) {
      gchar *name;

      name = g_strdup_printf ("src_%02d", info->i);
      gst_element_add_ghost_pad (info->result, pad, name);
      g_free (name);
      
      GST_DEBUG (0,"gstpipeline: new dynamic pad %s", GST_PAD_NAME (pad));
      break;
    }
  }
}

static void
gst_autoplug_pads_autoplug (GstElement *src, GstElement *sink)
{
  const GList *srcpads;
  gboolean linked = FALSE;

  srcpads = gst_element_get_pad_list(src);

  while (srcpads && !linked) {
    GstPad *srcpad = (GstPad *)srcpads->data;

    if (gst_pad_get_direction(srcpad) == GST_PAD_SRC)
      linked = gst_autoplug_pads_autoplug_func (src, srcpad, sink);

    srcpads = g_list_next(srcpads);
  }

  if (!linked) {
    GST_DEBUG (0,"gstpipeline: delaying pad links for \"%s\" to \"%s\"",
		    GST_ELEMENT_NAME(src), GST_ELEMENT_NAME(sink));
    g_signal_connect (G_OBJECT(src), "new_pad", 
		    G_CALLBACK (gst_autoplug_pads_autoplug_func), sink);
  }
}

static const GList*
gst_autoplug_element_factory_get_list (gpointer data)
{
  return gst_registry_pool_feature_list (GST_TYPE_ELEMENT_FACTORY);
}

typedef struct {
  GstCaps *src;
  GstCaps *sink;
} caps_struct;

#define IS_CAPS(cap) (((cap) == caps->src) || (cap) == caps->sink)

static guint
gst_autoplug_caps_find_cost (gpointer src, gpointer dest, gpointer data)
{
  caps_struct *caps = (caps_struct *)data;
  gboolean res;

  if (IS_CAPS (src) && IS_CAPS (dest)) {
    res = gst_caps_is_always_compatible ((GstCaps *)src, (GstCaps *)dest);
  }
  else if (IS_CAPS (src)) {
    res = gst_element_factory_can_sink_caps ((GstElementFactory *)dest, (GstCaps *)src);
  }
  else if (IS_CAPS (dest)) {
    res = gst_element_factory_can_src_caps ((GstElementFactory *)src, (GstCaps *)dest);
  }
  else {
    res = gst_autoplug_can_match ((GstElementFactory *)src, (GstElementFactory *)dest);
  }

  if (res)
    return 1;
  else
    return GST_AUTOPLUG_MAX_COST;
}

static GstElement*
gst_static_autoplug_to_caps (GstAutoplug *autoplug, GstCaps *srccaps, GstCaps *sinkcaps, va_list args)
{
  caps_struct caps;
  GstCaps *capslist;
  GstElement *result = NULL, *srcelement = NULL;
  GList **factories;
  GList *chains = NULL;
  GList *endcaps = NULL;
  guint numsinks = 0, i;
  gboolean have_common = FALSE;

  capslist = sinkcaps;

  /*
   * We first create a list of elements that are needed
   * to convert the srcpad caps to the different sinkpad caps.
   * and add the list of elementfactories to a list (chains).
   */
  caps.src  = srccaps;

  while (capslist) {
    GList *elements;

    caps.sink = capslist;

    GST_INFO (GST_CAT_AUTOPLUG_ATTEMPT,"autoplugging two caps structures");

    elements =  gst_autoplug_func (caps.src, caps.sink,
				   gst_autoplug_element_factory_get_list,
				   gst_autoplug_caps_find_cost,
				   &caps);

    if (elements) {
      chains = g_list_append (chains, elements);
      endcaps = g_list_append (endcaps, capslist);
      numsinks++;
    }
    else {
    }

    capslist = va_arg (args, GstCaps *);
  }

  /*
   * If no list could be found the pipeline cannot be autoplugged and
   * we return a NULL element
   */
  if (numsinks == 0)
    return NULL;

  /*
   * We now have a list of lists. We will turn this into an array
   * of lists, this will make it much more easy to manipulate it
   * in the next steps.
   */
  factories = g_new0 (GList *, numsinks);

  for (i = 0; chains; i++) {
    GList *elements = (GList *) chains->data;

    factories[i] = elements;

    chains = g_list_next (chains);
  }
  /*FIXME, free the list */

  result = gst_bin_new ("autoplug_bin");

  /*
   * We now hav a list of lists that is probably like:
   *
   *  !
   *  A -> B -> C
   *  !
   *  A -> D -> E
   *
   * we now try to find the common elements (A) and add them to
   * the bin. We remove them from both lists too.
   */
  while (factories[0]) {
    GstElementFactory *factory;
    GstElement *element;
    gchar *name;

    /* fase 3: add common elements */
    factory = (GstElementFactory *) (factories[0]->data);

    /* check to other paths for matching elements (factories) */
    for (i=1; i<numsinks; i++) {
      if (factory != (GstElementFactory *) (factories[i]->data)) {
	goto differ;
      }
    }

    GST_DEBUG (0,"common factory \"%s\"", GST_OBJECT_NAME (factory));
    
    /* it is likely that the plugin is not loaded yet. thus when it loads it
     * will replace the elementfactory that gst built from the cache, and the
     * GST_OBJECT_NAME will no longer be valid. thus we must g_strdup its name.
     *
     * this might be an implementation problem, i don't know, if a program keeps
     * a reference to a cached factory after a factory has been added on plugin
     * initialization. i raelly don't know though.
     */
    name = g_strdup (GST_OBJECT_NAME (factory));
    element = gst_element_factory_create (factory, name);
    g_free(name);
    gst_bin_add (GST_BIN(result), element);

    if (srcelement != NULL) {
      gst_autoplug_pads_autoplug (srcelement, element);
    }
    /* this is the first element, find a good ghostpad */
    else {
      const GList *pads;

      pads = gst_element_get_pad_list (element);

      while (pads) {
	GstPad *pad = GST_PAD (pads->data);
	GstPadTemplate *templ = GST_PAD_PAD_TEMPLATE (pad);

	if (gst_caps_is_always_compatible (srccaps, GST_PAD_TEMPLATE_CAPS (templ))) {
          gst_element_add_ghost_pad (result, pad, "sink");
	  break;
	}

	pads = g_list_next (pads);
      }
    }
    gst_autoplug_signal_new_object (GST_AUTOPLUG (autoplug), GST_OBJECT (element));

    srcelement = element;

    /* advance the pointer in all lists */
    for (i=0; i<numsinks; i++) {
      factories[i] = g_list_next (factories[i]);
    }

    have_common = TRUE;
  }

differ:

  /* loop over all the sink elements */
  for (i = 0; i < numsinks; i++) {
    GstElement *thesrcelement = srcelement;
    GstElement *thebin = GST_ELEMENT(result);

    while (factories[i]) {
      /* fase 4: add other elements... */
      GstElementFactory *factory;
      GstElement *element;

      factory = (GstElementFactory *)(factories[i]->data);

      GST_DEBUG (0,"factory \"%s\"", GST_OBJECT_NAME (factory));
      element = gst_element_factory_create(factory, GST_OBJECT_NAME (factory));

      GST_DEBUG (0,"adding element %s", GST_ELEMENT_NAME (element));
      gst_bin_add(GST_BIN(thebin), element);
      gst_autoplug_signal_new_object (GST_AUTOPLUG (autoplug), GST_OBJECT (element));
      
      gst_autoplug_pads_autoplug(thesrcelement, element);

      /* this element is now the new source element */
      thesrcelement = element;

      factories[i] = g_list_next(factories[i]);
    }
    /*
     * we're at the last element in the chain,
     * find a suitable pad to turn into a ghostpad
     */
    {
      GstCaps *endcap = (GstCaps *)(endcaps->data);
      const GList *pads = gst_element_get_pad_list (thesrcelement);
      gboolean have_pad = FALSE;
      endcaps = g_list_next (endcaps);

      GST_DEBUG (0,"attempting to create a ghostpad for %s", GST_ELEMENT_NAME (thesrcelement));

      while (pads) {
	GstPad *pad = GST_PAD (pads->data);
	GstPadTemplate *templ = GST_PAD_PAD_TEMPLATE (pad);
	pads = g_list_next (pads);

	if (gst_caps_is_always_compatible (GST_PAD_TEMPLATE_CAPS (templ), endcap)) {
	  gchar *name;

	  name = g_strdup_printf ("src_%02d", i);
          gst_element_add_ghost_pad (result, pad, name);
	  g_free (name);
	  
	  have_pad = TRUE;
	  break;
	}
      }
      if (!have_pad) {
	dynamic_pad_struct *data = g_new0(dynamic_pad_struct, 1);

	data->result = result;
	data->endcap = endcap;
	data->i = i;

        GST_DEBUG (0,"delaying the creation of a ghostpad for %s", GST_ELEMENT_NAME (thesrcelement));
	g_signal_connect (G_OBJECT (thesrcelement), "new_pad", 
			G_CALLBACK (autoplug_dynamic_pad), data);
      }
    }
  }

  return result;
}

/*
 * shortest path algorithm
 *
 */
struct _gst_autoplug_node
{
  gpointer iNode;
  gpointer iPrev;
  gint iDist;
};

typedef struct _gst_autoplug_node gst_autoplug_node;

static gint
find_factory (gst_autoplug_node *rgnNodes, gpointer factory)
{
  gint i=0;

  while (rgnNodes[i].iNode) {
    if (rgnNodes[i].iNode == factory) return i;
    i++;
  }
  return 0;
}

static GList*
construct_path (gst_autoplug_node *rgnNodes, gpointer factory)
{
  GstElementFactory *current;
  GList *factories = NULL;

  current = rgnNodes[find_factory(rgnNodes, factory)].iPrev;

  GST_INFO (GST_CAT_AUTOPLUG_ATTEMPT,"factories found in autoplugging (reversed order)");

  while (current != NULL)
  {
    gpointer next = NULL;

    next = rgnNodes[find_factory(rgnNodes, current)].iPrev;
    if (next) {
      factories = g_list_prepend (factories, current);
      GST_INFO (GST_CAT_AUTOPLUG_ATTEMPT,"factory: \"%s\"", GST_OBJECT_NAME (current));
    }
    current = next;
  }
  return factories;
}

static GList*
gst_autoplug_enqueue (GList *queue, gpointer iNode, gint iDist, gpointer iPrev)
{
  gst_autoplug_node *node = g_malloc (sizeof (gst_autoplug_node));

  node->iNode = iNode;
  node->iDist = iDist;
  node->iPrev = iPrev;

  queue = g_list_append (queue, node);

  return queue;
}

static GList*
gst_autoplug_dequeue (GList *queue, gpointer *iNode, gint *iDist, gpointer *iPrev)
{
  GList *head;
  gst_autoplug_node *node;

  head = g_list_first (queue);

  if (head) {
    node = (gst_autoplug_node *)head->data;
    *iNode = node->iNode;
    *iPrev = node->iPrev;
    *iDist = node->iDist;
    head = g_list_remove (queue, node);
  }

  return head;
}

static GList*
gst_autoplug_func (gpointer src, gpointer sink,
		   GstAutoplugListFunction list_function,
		   GstAutoplugCostFunction cost_function,
		   gpointer data)
{
  gst_autoplug_node *rgnNodes;
  GList *queue = NULL;
  gpointer iNode, iPrev;
  gint iDist, i, iCost;

  GList *elements = g_list_copy ((GList *)list_function(data));
  GList *factories;
  guint num_factories;

  elements = g_list_append (elements, sink);
  elements = g_list_append (elements, src);

  factories = elements;

  num_factories = g_list_length (factories);

  rgnNodes = g_new0 (gst_autoplug_node, num_factories+1);

  for (i=0; i< num_factories; i++) {
    gpointer fact = factories->data;

    rgnNodes[i].iNode = fact;
    rgnNodes[i].iPrev = NULL;

    if (fact == src) {
      rgnNodes[i].iDist = 0;
    }
    else {
      rgnNodes[i].iDist = GST_AUTOPLUG_MAX_COST;
    }

    factories = g_list_next (factories);
  }
  rgnNodes[num_factories].iNode = NULL;

  queue = gst_autoplug_enqueue (queue, src, 0, NULL);

  while (g_list_length (queue) > 0) {
    GList *factories2 = elements;

    queue = gst_autoplug_dequeue (queue, &iNode, &iDist, &iPrev);

    for (i=0; i< num_factories; i++) {
      gpointer current = factories2->data;

      iCost = cost_function (iNode, current, data);
      if (iCost != GST_AUTOPLUG_MAX_COST) {
        if ((GST_AUTOPLUG_MAX_COST == rgnNodes[i].iDist) ||
            (rgnNodes[i].iDist > (iCost + iDist))) {
          rgnNodes[i].iDist = iDist + iCost;
          rgnNodes[i].iPrev = iNode;

          queue = gst_autoplug_enqueue (queue, current, iDist + iCost, iNode);
        }
      }

      factories2 = g_list_next (factories2);
    }
  }

  return construct_path (rgnNodes, sink);
}

