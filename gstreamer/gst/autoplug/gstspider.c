/* GStreamer
 * Copyright (C) 2002 Erik Walthinsen <omega@cse.ogi.edu>
 *               2002 Wim Taymans <wtay@chello.be>
 *
 * gstspider.c: element to automatically link sinks and sources
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
 
/*
 * TODO:
 * - handle automatic removal of unneeded elements
 * - make the spider handle and send events (esp. new media)
 * - decide if we plug pads or elements, currently it's a mess
 * - allow unlinking
 * - implement proper saving/loading from xml
 * - implement a way to allow merging/splitting (aka tee)
 * - find ways to define which elements to use when plugging
 * - remove pads
 * - improve typefinding
 * - react to errors inside the pipeline
 * - implement more properties, change the current
 * - emit signals (most important: "NOT PLUGGABLE")
 * - implement something for reporting the state of the spider
 *   to allow easier debugging.
 *   (could be useful for bins in general)
 * - fix bugs
 * ...
 */
 
#include "gstspider.h"
#include "gstspideridentity.h"
#include "gstsearchfuncs.h"
 
/* signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_FACTORIES,
  /* FILL ME TOO */
};

/* generic templates */
GST_PAD_TEMPLATE_FACTORY (spider_src_factory,
  "src_%d",
  GST_PAD_SRC,
  GST_PAD_REQUEST,
  NULL      /* no caps */
);

/* standard GObject stuff */
static void                     gst_spider_class_init                   (GstSpiderClass *klass);
static void                     gst_spider_init                         (GstSpider *spider);
static void                     gst_spider_dispose                      (GObject *object);

/* element class functions */
static GstPad*                  gst_spider_request_new_pad              (GstElement *element, GstPadTemplate *templ, const gchar *name);
static void			gst_spider_set_property                 (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void			gst_spider_get_property			(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

/* link functions */
static GstSpiderConnection *	gst_spider_link_new		(GstSpiderIdentity *src);
static void			gst_spider_link_destroy		(GstSpiderConnection *conn);
static void			gst_spider_link_reset		(GstSpiderConnection *conn, GstElement *to);
static void			gst_spider_link_add		(GstSpiderConnection *conn, GstElement *element);
static GstSpiderConnection *	gst_spider_link_find		(GstSpiderIdentity *src);
static GstSpiderConnection *	gst_spider_link_get		(GstSpiderIdentity *src);

/* autoplugging functions */
static GstElement *             gst_spider_find_element_to_plug      	(GstElement *src, GstElementFactory *fac, GstPadDirection dir);
static GstPadLinkReturn		gst_spider_plug				(GstSpiderConnection *conn);
static GstPadLinkReturn		gst_spider_plug_from_srcpad		(GstSpiderConnection *conn, GstPad *srcpad);
/*static GstPadLinkReturn      gst_spider_plug_peers			(GstSpider *spider, GstPad *srcpad, GstPad *sinkpad); */
static GstPadLinkReturn		gst_spider_create_and_plug		(GstSpiderConnection *conn, GList *plugpath);

/* random functions */
static gchar *			gst_spider_unused_elementname		(GstBin *bin, const gchar *startwith);

/* debugging stuff
static void			print_spider_contents			(GstSpider *spider);
static void			print_spider_link			(GstSpiderConnection *conn); */

/* === variables === */
static                          GstElementClass	*			parent_class = NULL;

/* no signals yet
static guint gst_spider_signals[LAST_SIGNAL] = { 0 };*/

/* let gstreamer know that we have some request pads available */
GST_PAD_TEMPLATE_FACTORY (gst_spider_src_template_factory,
  "src%d",
  GST_PAD_SRC,
  GST_PAD_REQUEST,
  NULL
);  

/* GObject and GStreamer init functions */
GType
gst_spider_get_type(void)
{
  static GType spider_type = 0;

  if (!spider_type) {
    static const GTypeInfo spider_info = {
      sizeof(GstSpiderClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_spider_class_init,
      NULL,
      NULL,
      sizeof(GstSpider),
      0,
      (GInstanceInitFunc)gst_spider_init,
    };
    spider_type = g_type_register_static (GST_TYPE_BIN, "GstSpider", &spider_info, 0);
  }
  return spider_type;
}

static void
gst_spider_class_init (GstSpiderClass *klass)
{
  GObjectClass     *gobject_class;
  GstElementClass  *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref(GST_TYPE_BIN);

  /* properties */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FACTORIES,
  g_param_spec_pointer ("factories", "allowed factories", "allowed factories for autoplugging", G_PARAM_READWRITE));

  gobject_class->set_property = gst_spider_set_property;
  gobject_class->get_property = gst_spider_get_property;
  gobject_class->dispose = gst_spider_dispose;

  gst_element_class_add_pad_template (gstelement_class, gst_spider_src_template_factory());
  
  gstelement_class->request_new_pad = GST_DEBUG_FUNCPTR(gst_spider_request_new_pad);
}
static void 
gst_spider_init (GstSpider *spider) 
{
  /* use only elements which have sources and sinks and where the sinks have caps */
  /* FIXME: How do we handle factories that are added after the spider was constructed? */
  spider->factories = gst_autoplug_factories_filters_with_sink_caps ((GList *) 
		  gst_registry_pool_feature_list (GST_TYPE_ELEMENT_FACTORY));

  spider->links = NULL;

  spider->sink_ident = gst_spider_identity_new_sink ("sink_ident");
  gst_bin_add (GST_BIN (spider), GST_ELEMENT (spider->sink_ident));
  gst_element_add_ghost_pad (GST_ELEMENT(spider), spider->sink_ident->sink, "sink");
  
}

static void
gst_spider_dispose (GObject *object)
{
  GstSpider *spider;
  
  spider = GST_SPIDER (object);
  g_list_free (spider->factories);
  
  ((GObjectClass *) parent_class)->dispose (object);
}
static GstPad *
gst_spider_request_new_pad (GstElement *element, GstPadTemplate *templ, const gchar *name)
{
  GstPad *returnpad;
  gchar *padname;
  GstSpiderIdentity *identity;
  GstSpider *spider;
  
  g_return_val_if_fail (templ != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD_TEMPLATE (templ), NULL);
  g_return_val_if_fail (GST_PAD_TEMPLATE_DIRECTION (templ) == GST_PAD_SRC, NULL);
  
  spider = GST_SPIDER (element);
  
  /* create an identity object, so we have a pad */
  padname = gst_spider_unused_elementname ((GstBin *)spider, "src_");
  identity = gst_spider_identity_new_src (padname);
  returnpad = identity->src;
  
  /* FIXME: use the requested name for the pad */

  gst_object_ref (GST_OBJECT (templ));
  GST_PAD_PAD_TEMPLATE (returnpad) = templ;
  
  gst_bin_add (GST_BIN (element), GST_ELEMENT (identity));
  
  returnpad = gst_element_add_ghost_pad (element, returnpad, padname);
  gst_spider_link_new (identity);
  GST_DEBUG (GST_CAT_ELEMENT_PADS, "successuflly created requested pad %s:%s", GST_DEBUG_PAD_NAME (returnpad));
  
  return returnpad;
}

static void
gst_spider_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstSpider *spider;
  GList *list;
  
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SPIDER (object));

  spider = GST_SPIDER (object);

  switch (prop_id) {
    case ARG_FACTORIES:
      list = (GList *) g_value_get_pointer (value);
      while (list)
      {
	g_return_if_fail (list->data != NULL);
	g_return_if_fail (GST_IS_ELEMENT_FACTORY (list->data));
	list = g_list_next (list);
      }
      g_list_free (spider->factories);
      spider->factories = (GList *) g_value_get_pointer (value);
      break;
    default:
      break;
  }
}
static void
gst_spider_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstSpider *spider;

  /* it's not null if we got it, but it might not be ours */
  spider = GST_SPIDER(object);

  switch (prop_id) {
    case ARG_FACTORIES:
      g_value_set_pointer (value, spider->factories);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
/* get a name for an element that isn't used yet */
static gchar *
gst_spider_unused_elementname (GstBin *bin, const gchar *startwith)
{
  gchar * name = g_strdup_printf ("%s%d", startwith, 0);
  guint i;
  
  for (i = 0; gst_bin_get_by_name (bin, name) != NULL; )
  {
    g_free (name);
    name = g_strdup_printf ("%s%d", startwith, ++i);
  }
  
  return name;
}
static void
gst_spider_link_sometimes (GstElement *src, GstPad *pad, GstSpiderConnection *conn)
{
  gboolean restart = FALSE;
  gulong signal_id = conn->signal_id;
  GstPad *sinkpad = conn->src->sink;
  GstSpider *spider = GST_SPIDER (GST_OBJECT_PARENT (conn->src));
  
  /* check if restarting is necessary */
  if (gst_element_get_state ((GstElement *) spider) == GST_STATE_PLAYING)
  {
    restart = TRUE;
    gst_element_set_state ((GstElement *) spider, GST_STATE_PAUSED);
  }
  
  /* try to autoplug the elements */
  if (gst_spider_plug_from_srcpad (conn, pad) != GST_PAD_LINK_REFUSED) {
    GST_DEBUG (GST_CAT_AUTOPLUG_ATTEMPT, "%s:%s was autoplugged to %s:%s, removing callback", GST_DEBUG_PAD_NAME (pad), GST_DEBUG_PAD_NAME (sinkpad));
    g_signal_handler_disconnect (src, signal_id);
    signal_id = 0;
  }
  
  /* restart if needed */
  if (restart)
  {
    gst_element_set_state ((GstElement *) spider, GST_STATE_PLAYING);
  }
}
/* create a new link from those two elements */
static GstSpiderConnection *
gst_spider_link_new (GstSpiderIdentity *src)
{
  GstSpider *spider = GST_SPIDER (GST_OBJECT_PARENT (src));
  
  GstSpiderConnection *conn = g_new0 (GstSpiderConnection, 1);
  conn->src = src;
  conn->path = NULL;
  conn->current = (GstElement *) spider->sink_ident;
  spider->links = g_list_prepend (spider->links, conn);
  
  return conn;
}
static void
gst_spider_link_destroy (GstSpiderConnection *conn)
{
  GstSpider *spider = GST_SPIDER (GST_OBJECT_PARENT (conn->src));
  /* reset link to unplugged */
  gst_spider_link_reset (conn, (GstElement *) spider->sink_ident);
  g_free (conn);
}
static void
gst_spider_link_reset (GstSpiderConnection *conn, GstElement *to)
{
  GstSpider *spider = GST_SPIDER (GST_OBJECT_PARENT (conn->src));
  GST_DEBUG (GST_CAT_AUTOPLUG, "resetting link from %s to %s, currently at %s to %s", GST_ELEMENT_NAME (spider->sink_ident), 
	     GST_ELEMENT_NAME (conn->src), GST_ELEMENT_NAME (conn->current), GST_ELEMENT_NAME (to));
  while ((conn->path != NULL) && ((GstElement *) conn->path->data != to))
  {
    gst_object_unref ((GstObject *) conn->path->data);
    conn->path = g_list_delete_link (conn->path, conn->path);
  }
  if (conn->path == NULL)
  {
    conn->current = (GstElement *) spider->sink_ident;
  } else {
    conn->current = to;
  }
}
/* add an element to the link */
static void
gst_spider_link_add (GstSpiderConnection *conn, GstElement *element)
{
  gst_object_ref ((GstObject *) element);
  gst_object_sink ((GstObject *) element);
  conn->path = g_list_prepend (conn->path, element);
  conn->current = element;
}
/* find the link from those two elements */
static GstSpiderConnection *
gst_spider_link_find (GstSpiderIdentity *src)
{
  GstSpider *spider = (GstSpider *) GST_OBJECT_PARENT (src);
  GList *list = spider->links;
  
  while (list)
  {
    GstSpiderConnection *conn = (GstSpiderConnection *) list->data;
    if (conn->src == src){
      return conn;
    }
    list = g_list_next(list);
  }
  return NULL;
}
/* get a new link from those two elements
 * search first; if none is found, create a new one */
static GstSpiderConnection *
gst_spider_link_get (GstSpiderIdentity *src)
{
  GstSpiderConnection *ret;
  
  if ((ret = gst_spider_link_find (src)) != NULL)
  {
    return ret;
  }
  return gst_spider_link_new (src);
}  
void			
gst_spider_identity_plug (GstSpiderIdentity *ident)
{
  GstSpider *spider;
  const GList *padlist;
  GstPadDirection dir;
  GstSpiderConnection *conn;
  
  /* checks */
  g_return_if_fail (ident != NULL);
  g_return_if_fail (GST_IS_SPIDER_IDENTITY (ident));
  spider = GST_SPIDER (GST_ELEMENT_PARENT (ident));
  g_assert (spider != NULL);
  g_assert (GST_IS_SPIDER (spider));
  
  /* return if we're already plugged */
  if (ident->plugged) return;

  /* get the direction of our ident */
  if (GST_PAD_PEER (ident->sink))
  {
    if (GST_PAD_PEER (ident->src))
    {
      /* Hey, the ident is linked on both sides */
      g_warning ("Trying to autoplug a linked element. Aborting...");
      return;
    } else {
      dir = GST_PAD_SINK;
    }
  } else {
    if (GST_PAD_PEER (ident->src))
    {
      dir = GST_PAD_SRC;
    } else {
      /* the ident isn't linked on either side */
      g_warning ("Trying to autoplug an unlinked element. Aborting...");
      return;
    }
  }

  /* now iterate all possible pads and link when needed */
  padlist = gst_element_get_pad_list (GST_ELEMENT (spider));
  while (padlist)
  {
    GstPad *otherpad;
    GstSpiderIdentity *peer;

    g_assert (GST_IS_PAD (padlist->data));
    otherpad = (GstPad *) GST_GPAD_REALPAD (padlist->data);
    peer = (GstSpiderIdentity *) GST_PAD_PARENT (otherpad);
    /* we only want to link to the other side */
    if (dir != GST_PAD_DIRECTION (otherpad))
    {
      /* we only link to plugged in elements */
      if (peer->plugged == TRUE) 
      {
        /* plug in the right direction */
        if (dir == GST_PAD_SINK)
        {
	  conn = gst_spider_link_get (peer);
        } else {
	  conn = gst_spider_link_get (ident);
        }
	if ((GstElement *) spider->sink_ident == conn->current)
	{
          gboolean restart = FALSE;
          /* check if restarting is necessary */
          if (gst_element_get_state ((GstElement *) spider) == GST_STATE_PLAYING)
          {
            restart = TRUE;
            gst_element_set_state ((GstElement *) spider, GST_STATE_PAUSED);
          }
	  gst_spider_plug (conn);
          /* restart if needed */
          if (restart)
          {
            gst_element_set_state ((GstElement *) spider, GST_STATE_PLAYING);
          }
	}
      }
    }
    padlist = g_list_next (padlist);
  }
  
  ident->plugged = TRUE; 
}
void
gst_spider_identity_unplug (GstSpiderIdentity *ident)
{
  GstSpider *spider = (GstSpider *) GST_OBJECT_PARENT (ident);
  GList *list = spider->links;
  
  while (list)
  {
    GstSpiderConnection *conn = list->data;
    GList *cur = list;
    list = g_list_next (list);
    if (conn->src == ident)
    {
      g_list_delete_link (spider->links, cur);
      gst_spider_link_destroy (conn);
    }
  }
  ident->plugged = FALSE;
}
/* links src to sink using the elementfactories in plugpath
 * plugpath will be removed afterwards */
static GstPadLinkReturn
gst_spider_create_and_plug (GstSpiderConnection *conn, GList *plugpath)
{
  GstSpider *spider = (GstSpider *) GST_OBJECT_PARENT (conn->src);
  GList *endelements = NULL, *templist = NULL;
  GstElement *element;
  
  /* exit if plugging is already done */
  if ((GstElement *) conn->src == conn->current)
    return GST_PAD_LINK_DONE;
  
  /* try to shorten the list at the end and not duplicate link code */
  if (plugpath != NULL)
  {
    templist = g_list_last (plugpath);
    element = (GstElement *) conn->src;
    while ((plugpath != NULL) && (element = gst_spider_find_element_to_plug (element, (GstElementFactory *) plugpath->data, GST_PAD_SINK)))
    {
      GList *cur = templist;
      endelements = g_list_prepend (endelements, element);
      templist = g_list_previous (templist);
      g_list_delete_link (cur, cur);    
    }
  }
  
  /* do the linking */
  while (conn->current != (GstElement *) (endelements == NULL ? conn->src : endelements->data))
  {
    /* get sink element to plug, src is conn->current */
    if (plugpath == NULL)
    {
      element = (GstElement *) (endelements == NULL ? conn->src : endelements->data);
    } else {
      element = gst_element_factory_create ((GstElementFactory *) plugpath->data, NULL);
      gst_bin_add (GST_BIN (spider), element);
    }
    /* insert and link new element */
    if (!gst_element_link (conn->current, element))
    {
      /* check if the src has SOMETIMES templates. If so, link a callback */
      GList *templs = gst_element_get_pad_template_list (conn->current);
	 
      /* remove element that couldn't be linked, if it wasn't the endpoint */
      if (element != (GstElement *) conn->src)
        gst_bin_remove (GST_BIN (spider), element);
    
      while (templs) {
        GstPadTemplate *templ = (GstPadTemplate *) templs->data;
        if ((GST_PAD_TEMPLATE_DIRECTION (templ) == GST_PAD_SRC) && (GST_PAD_TEMPLATE_PRESENCE(templ) == GST_PAD_SOMETIMES))
        {
          GST_DEBUG (GST_CAT_AUTOPLUG_ATTEMPT, "adding callback to link element %s to %s", GST_ELEMENT_NAME (conn->current), GST_ELEMENT_NAME (conn->src));
          conn->signal_id = g_signal_connect (G_OBJECT (conn->current), "new_pad", 
					      G_CALLBACK (gst_spider_link_sometimes), conn);
	  g_list_free (plugpath);
	  return GST_PAD_LINK_DELAYED;
        }
        templs = g_list_next (templs);
      }
      GST_DEBUG (GST_CAT_AUTOPLUG_ATTEMPT, "no chance to link element %s to %s", GST_ELEMENT_NAME (conn->current), GST_ELEMENT_NAME (conn->src));
      g_list_free (plugpath);
      return GST_PAD_LINK_REFUSED;
    }
    GST_DEBUG (GST_CAT_AUTOPLUG_ATTEMPT, "added element %s and attached it to element %s", GST_ELEMENT_NAME (element), GST_ELEMENT_NAME (conn->current));
    gst_spider_link_add (conn, element);
    if (plugpath != NULL)
      plugpath = g_list_delete_link (plugpath, plugpath);
  }
  
  /* ref all elements at the end */
  while (endelements)
  {
    gst_spider_link_add (conn, endelements->data);
    endelements = g_list_delete_link (endelements, endelements);
  }
  
  return GST_PAD_LINK_DONE;
}
/* checks, if src is already linked to an element from factory fac on direction dir */
static GstElement *
gst_spider_find_element_to_plug (GstElement *src, GstElementFactory *fac, GstPadDirection dir)
{
  GList *padlist = GST_ELEMENT_PADS (src);
  
  while (padlist)
  {
    GstPad *pad = (GstPad *) GST_PAD_REALIZE (padlist->data);
    /* is the pad on the right side and is it linked? */
    if ((GST_PAD_DIRECTION (pad) == dir) && (pad = (GstPad *) (GST_RPAD_PEER (pad))))
    {
      /* is the element the pad is linked to of the right type? */
      GstElement *element = GST_PAD_PARENT (pad);
      if (GST_ELEMENT_CLASS (G_OBJECT_GET_CLASS (element))->elementfactory == fac) {
        return element;
      }
    }
    padlist = g_list_next (padlist);
  }
  
  return NULL;
}
/* try to establish the link */
static GstPadLinkReturn
gst_spider_plug	(GstSpiderConnection *conn)
{
  GstSpider *spider = (GstSpider *) GST_OBJECT_PARENT (conn->src);
  if ((GstElement *) conn->src == conn->current)
    return GST_PAD_LINK_DONE;
  if ((GstElement *) spider->sink_ident == conn->current)
    return gst_spider_plug_from_srcpad (conn, spider->sink_ident->src);
  g_warning ("FIXME: autoplugging only possible from GstSpiderIdentity conn->sink yet (yep, that's technical)\n");
  return GST_PAD_LINK_REFUSED;
}
/* try to establish the link using this pad */
static GstPadLinkReturn
gst_spider_plug_from_srcpad (GstSpiderConnection *conn, GstPad *srcpad)
{
  GstElement *element;
  GList *plugpath;
  gboolean result = TRUE;
  GstSpider *spider = (GstSpider *) GST_OBJECT_PARENT (conn->src);
  GstElement *startelement = conn->current;

  g_assert ((GstElement *) GST_OBJECT_PARENT (srcpad) == conn->current);
  GST_DEBUG (GST_CAT_AUTOPLUG_ATTEMPT, "trying to plug from %s:%s to %s", 
	     GST_DEBUG_PAD_NAME (srcpad), GST_ELEMENT_NAME (conn->src));
  
  /* find a path from src to sink */
  /* FIXME: make that if go away and work anyway */
  if (srcpad == spider->sink_ident->src)
  {
    g_assert (GST_RPAD_PEER (spider->sink_ident->sink) != NULL);
    plugpath = gst_autoplug_sp (gst_pad_get_caps ((GstPad *) GST_RPAD_PEER (spider->sink_ident->sink)), gst_pad_get_caps (conn->src->sink), spider->factories);
  } else {
    plugpath = gst_autoplug_sp (gst_pad_get_caps (srcpad), gst_pad_get_caps (conn->src->sink), spider->factories);
  }
  
  /* prints out the path that was found for plugging */
  /* g_print ("found path from %s to %s:\n", GST_ELEMENT_NAME (conn->current), GST_ELEMENT_NAME (conn->src));
  templist = plugpath;
  while (templist)
  {
    g_print("%s\n", GST_OBJECT_NAME (templist->data));
    templist = g_list_next (templist);
  }*/
    
  /* if there is no way to plug: return */
  if (plugpath == NULL) {
    GST_DEBUG (GST_CAT_AUTOPLUG_ATTEMPT, "no chance to plug from %s to %s", GST_ELEMENT_NAME (conn->current), GST_ELEMENT_NAME (conn->src));
    return GST_PAD_LINK_REFUSED;
  }
  GST_DEBUG (GST_CAT_AUTOPLUG_ATTEMPT, "found a link that needs %d elements", g_list_length (plugpath));

  /* now remove non-needed elements from the beginning of the path 
   * alter src to point to the new element where we need to start 
   * plugging and alter the plugpath to represent the elements, that must be plugged
   */
  element = conn->current;
  while ((plugpath != NULL) && (element = gst_spider_find_element_to_plug (element, (GstElementFactory *) plugpath->data, GST_PAD_SRC)))
  {
    gst_spider_link_add (conn, element);
    plugpath = g_list_delete_link (plugpath, plugpath);
  }
  
  GST_DEBUG (GST_CAT_AUTOPLUG_ATTEMPT, "%d elements must be inserted to establish the link", g_list_length (plugpath));
  /* create the elements and plug them */
  result = gst_spider_create_and_plug (conn, plugpath);
  
  /* reset the "current" element */
  if (result == GST_PAD_LINK_REFUSED)
  {
     gst_spider_link_reset (conn, startelement);
  }
  
  return result;  
}

GstElementDetails gst_spider_details = {
  "Spider",
  "Generic",
  "LGPL",
  "Automatically link sinks and sources",
  VERSION,
  "Benjamin Otte <in7y118@public.uni-hamburg.de>",
  "(C) 2002",
};

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_element_factory_new("spider", GST_TYPE_SPIDER,
                                   &gst_spider_details);
  gst_plugin_set_longname (plugin, "Spider autoplugging elements");
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (spider_src_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  /* add spideridentity */
  factory = gst_element_factory_new ("spideridentity", GST_TYPE_SPIDER_IDENTITY,
                                     &gst_spider_identity_details);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gstspider",
  plugin_init
};

  
  
