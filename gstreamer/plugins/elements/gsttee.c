/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gsttee.c: Tee element, one in N out
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

#include "gsttee.h"


GstElementDetails gst_tee_details = {
  "Tee pipe fitting",
  "Generic",
  "LGPL",
  "1-to-N pipe fitting",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>\n"
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 1999, 2000",
};

/* Tee signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_SILENT,
  ARG_NUM_PADS,
  ARG_LAST_MESSAGE,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (tee_src_factory,
  "src%d",
  GST_PAD_SRC,
  GST_PAD_REQUEST,
  NULL			/* no caps */
);

static void 	gst_tee_class_init	(GstTeeClass *klass);
static void 	gst_tee_init		(GstTee *tee);

static GstPad* 	gst_tee_request_new_pad (GstElement *element, GstPadTemplate *temp, const gchar *unused);

static void 	gst_tee_set_property 	(GObject *object, guint prop_id, 
					 const GValue *value, GParamSpec *pspec);
static void 	gst_tee_get_property 	(GObject *object, guint prop_id, 
					 GValue *value, GParamSpec *pspec);

static void  	gst_tee_chain 		(GstPad *pad, GstBuffer *buf);


static GstElementClass *parent_class = NULL;
/*static guint gst_tee_signals[LAST_SIGNAL] = { 0 };*/

GType
gst_tee_get_type(void) {
  static GType tee_type = 0;

  if (!tee_type) {
    static const GTypeInfo tee_info = {
      sizeof(GstTeeClass),      NULL,
      NULL,
      (GClassInitFunc)gst_tee_class_init,
      NULL,
      NULL,
      sizeof(GstTee),
      0,
      (GInstanceInitFunc)gst_tee_init,
    };
    tee_type = g_type_register_static (GST_TYPE_ELEMENT, "GstTee", &tee_info, 0);
  }
  return tee_type;
}

static void
gst_tee_class_init (GstTeeClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_NUM_PADS,
    g_param_spec_int ("num_pads", "num_pads", "num_pads",
                      0, G_MAXINT, 0, G_PARAM_READABLE)); 
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SILENT,
    g_param_spec_boolean ("silent", "silent", "silent",
                      FALSE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LAST_MESSAGE,
    g_param_spec_string ("last_message", "last_message", "last_message",
			 NULL, G_PARAM_READABLE));
  

  gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_tee_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_tee_get_property);

  gstelement_class->request_new_pad = GST_DEBUG_FUNCPTR(gst_tee_request_new_pad);
}

static GstPadLinkReturn 
gst_tee_sinklink (GstPad *pad, GstCaps *caps) 
{
  GstTee *tee;
  const GList *pads;
  GstPadLinkReturn set_retval;
  
  tee = GST_TEE (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps)) {
    return GST_PAD_LINK_DELAYED;
  }

  /* go through all the src pads */
  pads = gst_element_get_pad_list (GST_ELEMENT (tee));

  while (pads) {
    GstPad *outpad = GST_PAD (pads->data);
    pads = g_list_next (pads);
		     
    if (GST_PAD_DIRECTION (outpad) != GST_PAD_SRC || !GST_PAD_IS_USABLE (outpad))
      continue;

    if ((set_retval = gst_pad_try_set_caps (outpad, caps)) <= 0) {
      return set_retval;
    }
  }
  return GST_PAD_LINK_OK;
}

static void 
gst_tee_init (GstTee *tee) 
{
  tee->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (tee), tee->sinkpad);
  gst_pad_set_chain_function (tee->sinkpad, GST_DEBUG_FUNCPTR (gst_tee_chain));
  gst_pad_set_link_function (tee->sinkpad, GST_DEBUG_FUNCPTR (gst_tee_sinklink));

  tee->silent = FALSE;
  tee->last_message = NULL;
}

/* helper compare function */
gint name_pad_compare (gconstpointer a, gconstpointer b)
{
  GstPad* pad = (GstPad*) a;
  gchar *name = (gchar *) b;
  
  g_assert (GST_IS_PAD (pad));

  return g_strcasecmp (name, gst_pad_get_name (pad)); /* returns 0 if match */
}

static GstPad*
gst_tee_request_new_pad (GstElement *element, GstPadTemplate *templ, const gchar *unused) 
{
  gchar *name;
  GstPad *srcpad;
  GstTee *tee;
  gint i = 0;
  const GList *pads;

  g_return_val_if_fail (GST_IS_TEE (element), NULL);
  
  if (templ->direction != GST_PAD_SRC) {
    g_warning ("gsttee: request new pad that is not a SRC pad\n");
    return NULL;
  }

  tee = GST_TEE (element);

  /* try names in order and find one that's not in use atm */
  pads = gst_element_get_pad_list (element);
    
  name = NULL;
  while (!name)
  {
    name = g_strdup_printf ("src%d", i);
    if (g_list_find_custom ((GList *)pads, (gconstpointer) name, name_pad_compare) != NULL)
    {
      /* this name is taken, use the next one */
      ++i;
      g_free (name);
      name = NULL;
    }
  }
  if (!tee->silent) {
    g_free (tee->last_message);
    tee->last_message = g_strdup_printf ("new pad %s", name);
    g_object_notify (G_OBJECT (tee), "last_message");
  }
  
  srcpad = gst_pad_new_from_template (templ, name);
  g_free (name);
  gst_element_add_pad (GST_ELEMENT (tee), srcpad);
  GST_PAD_ELEMENT_PRIVATE (srcpad) = NULL;

  if (GST_PAD_CAPS (tee->sinkpad)) {
    gst_pad_try_set_caps (srcpad, GST_PAD_CAPS (tee->sinkpad));
  }

  return srcpad;
}

static void
gst_tee_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstTee *tee;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_TEE (object));

  tee = GST_TEE (object);

  switch (prop_id) {
    case ARG_SILENT:
      tee->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_tee_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstTee *tee;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_TEE (object));

  tee = GST_TEE (object);

  switch (prop_id) {
    case ARG_NUM_PADS:
      g_value_set_int (value, GST_ELEMENT (tee)->numsrcpads);
      break;
    case ARG_SILENT:
      g_value_set_boolean (value, tee->silent);
      break;
    case ARG_LAST_MESSAGE:
      g_value_set_string ((GValue *) value, tee->last_message);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * gst_tee_chain:
 * @pad: the pad to follow
 * @buf: the buffer to pass
 *
 * Chain a buffer on a pad.
 */
static void 
gst_tee_chain (GstPad *pad, GstBuffer *buf) 
{
  GstTee *tee;
  const GList *pads;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  tee = GST_TEE (gst_pad_get_parent (pad));

  gst_buffer_ref_by_count (buf, GST_ELEMENT (tee)->numsrcpads - 1);
  
  pads = gst_element_get_pad_list (GST_ELEMENT (tee));

  while (pads) {
    GstPad *outpad = GST_PAD (pads->data);
    pads = g_list_next (pads);

    if (GST_PAD_DIRECTION (outpad) != GST_PAD_SRC)
      continue;

    if (!tee->silent) {
      g_free (tee->last_message);
      tee->last_message = g_strdup_printf ("chain        ******* (%s:%s)t (%d bytes, %llu) %p",
              GST_DEBUG_PAD_NAME (outpad), GST_BUFFER_SIZE (buf), GST_BUFFER_TIMESTAMP (buf), buf);
      g_object_notify (G_OBJECT (tee), "last_message");
    }

    if (GST_PAD_IS_USABLE (outpad))
      gst_pad_push (outpad, buf);
    else
      gst_buffer_unref (buf);
  }
}

gboolean
gst_tee_factory_init (GstElementFactory *factory)
{
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (tee_src_factory));

  return TRUE;
}

