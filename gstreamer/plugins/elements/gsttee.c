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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gsttee.h"

#include <string.h>

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_tee_debug);
#define GST_CAT_DEFAULT gst_tee_debug

GstElementDetails gst_tee_details = GST_ELEMENT_DETAILS ("Tee pipe fitting",
    "Generic",
    "1-to-N pipe fitting",
    "Erik Walthinsen <omega@cse.ogi.edu>, "
    "Wim Taymans <wim.taymans@chello.be>");

/* Tee signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_SILENT,
  ARG_NUM_PADS,
  ARG_LAST_MESSAGE
      /* FILL ME */
};

GstStaticPadTemplate tee_src_template = GST_STATIC_PAD_TEMPLATE ("src%d",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_tee_debug, "tee", 0, "tee element");

GST_BOILERPLATE_FULL (GstTee, gst_tee, GstElement, GST_TYPE_ELEMENT, _do_init);

static GstPad *gst_tee_request_new_pad (GstElement * element,
    GstPadTemplate * temp, const gchar * unused);

static void gst_tee_finalize (GObject * object);
static void gst_tee_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_tee_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_tee_chain (GstPad * pad, GstData * _data);


static void
gst_tee_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));
  gst_element_class_set_details (gstelement_class, &gst_tee_details);
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&tee_src_template));
}

static void
gst_tee_finalize (GObject * object)
{
  GstTee *tee;

  tee = GST_TEE (object);

  g_free (tee->last_message);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_tee_class_init (GstTeeClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_tee_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_tee_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_NUM_PADS,
      g_param_spec_int ("num_pads", "num_pads", "num_pads",
          0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SILENT,
      g_param_spec_boolean ("silent", "silent", "silent",
          TRUE, G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LAST_MESSAGE,
      g_param_spec_string ("last_message", "last_message", "last_message",
          NULL, G_PARAM_READABLE));


  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_tee_finalize);

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_tee_request_new_pad);
}

static void
gst_tee_init (GstTee * tee)
{
  tee->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sinktemplate),
      "sink");
  gst_element_add_pad (GST_ELEMENT (tee), tee->sinkpad);
  gst_pad_set_chain_function (tee->sinkpad, GST_DEBUG_FUNCPTR (gst_tee_chain));
  //gst_pad_set_link_function (tee->sinkpad, GST_DEBUG_FUNCPTR (gst_pad_proxy_pad_link));
  gst_pad_set_getcaps_function (tee->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));

  tee->last_message = NULL;
}

/* helper compare function */
gint
name_pad_compare (gconstpointer a, gconstpointer b)
{
  GstPad *pad = (GstPad *) a;
  gchar *name = (gchar *) b;

  g_assert (GST_IS_PAD (pad));

  return strcmp (name, gst_pad_get_name (pad)); /* returns 0 if match */
}

static GstCaps *
gst_tee_getcaps (GstPad * _pad)
{
  GstTee *tee = GST_TEE (gst_pad_get_parent (_pad));
  GstCaps *caps = gst_caps_new_any (), *tmp, *res;
  GstPad *pad;
  const GList *pads;

  for (pads = GST_ELEMENT (tee)->pads; pads != NULL; pads = pads->next) {
    pad = GST_PAD (pads->data);
    if (pad == _pad)
      continue;

    tmp = gst_pad_get_allowed_caps (pad);
    res = gst_caps_intersect (caps, tmp);
    gst_caps_unref (tmp);
    gst_caps_unref (caps);
    caps = res;
  }

  return caps;
}

static GstPadLinkReturn
gst_tee_link (GstPad * _pad, const GstCaps * caps)
{
  GstTee *tee = GST_TEE (gst_pad_get_parent (_pad));
  GstPadLinkReturn res;
  GstPad *pad;
  const GList *pads;

  GST_DEBUG_OBJECT (tee, "Forwarding link to all other pads");

  for (pads = GST_ELEMENT (tee)->pads; pads != NULL; pads = pads->next) {
    pad = GST_PAD (pads->data);
    if (pad == _pad)
      continue;

    res = gst_pad_try_set_caps (pad, caps);
    GST_DEBUG_OBJECT (tee, "Pad %s:%s gave response %d",
        GST_DEBUG_PAD_NAME (pad), res);
    if (GST_PAD_LINK_FAILED (res))
      return res;
  }

  return GST_PAD_LINK_OK;
}

static GstPad *
gst_tee_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * unused)
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
  pads = element->pads;

  name = NULL;
  while (!name) {
    name = g_strdup_printf ("src%d", i);
    if (g_list_find_custom ((GList *) pads, (gconstpointer) name,
            name_pad_compare) != NULL) {
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
  gst_pad_set_link_function (srcpad, GST_DEBUG_FUNCPTR (gst_tee_link));
  gst_pad_set_getcaps_function (srcpad, GST_DEBUG_FUNCPTR (gst_tee_getcaps));
  gst_element_add_pad (GST_ELEMENT (tee), srcpad);
  GST_PAD_ELEMENT_PRIVATE (srcpad) = NULL;

  if (GST_PAD_CAPS (tee->sinkpad)) {
    gst_pad_try_set_caps (srcpad, GST_PAD_CAPS (tee->sinkpad));
  }

  return srcpad;
}

static void
gst_tee_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstTee *tee;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_TEE (object));

  tee = GST_TEE (object);

  switch (prop_id) {
    case ARG_SILENT:
      tee->silent = g_value_get_boolean (value);
      g_object_notify (G_OBJECT (tee), "silent");
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_tee_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
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
      g_value_set_string (value, tee->last_message);
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
gst_tee_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstTee *tee;
  const GList *pads;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  tee = GST_TEE (gst_pad_get_parent (pad));

  gst_buffer_ref_by_count (buf, GST_ELEMENT (tee)->numsrcpads - 1);

  pads = GST_ELEMENT (tee)->pads;

  while (pads) {
    GstPad *outpad = GST_PAD (pads->data);

    pads = g_list_next (pads);

    if (GST_PAD_DIRECTION (outpad) != GST_PAD_SRC)
      continue;

    if (!tee->silent) {
      g_free (tee->last_message);
      tee->last_message =
          g_strdup_printf ("chain        ******* (%s:%s)t (%d bytes, %"
          G_GUINT64_FORMAT ") %p", GST_DEBUG_PAD_NAME (outpad),
          GST_BUFFER_SIZE (buf), GST_BUFFER_TIMESTAMP (buf), buf);
      g_object_notify (G_OBJECT (tee), "last_message");
    }

    if (GST_PAD_IS_USABLE (outpad))
      gst_pad_push (outpad, GST_DATA (buf));
    else
      gst_buffer_unref (buf);
  }
}
