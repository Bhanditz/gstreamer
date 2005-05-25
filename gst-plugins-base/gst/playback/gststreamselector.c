/* GStreamer
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
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
 * !!!!!!!!!!!!!!!!! Big phat warning. !!!!!!!!!!!!!!!!!!!!!!
 *
 * The pads on the sinkside can be filled and the application is
 * supposed to enable/disable them. The plugin will receive input
 * data over the currently active pad and take care of data
 * forwarding and negotiation. This plugin does nothing fancy. It
 * exists to be light-weight and simple.
 *
 * This is not a generic switch element. This is not to be used for
 * any such purpose. Patches to make it do that will be rejected.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gststreamselector.h"

GST_DEBUG_CATEGORY_STATIC (stream_selector_debug);
#define GST_CAT_DEFAULT stream_selector_debug

static GstStaticPadTemplate gst_stream_selector_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_stream_selector_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void gst_stream_selector_dispose (GObject * object);
static void gst_stream_selector_init (GstStreamSelector * sel);
static void gst_stream_selector_base_init (GstStreamSelectorClass * klass);
static void gst_stream_selector_class_init (GstStreamSelectorClass * klass);

static GstCaps *gst_stream_selector_getcaps (GstPad * pad);
static GList *gst_stream_selector_get_linked_pads (GstPad * pad);
static GstPad *gst_stream_selector_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * unused);
static GstFlowReturn gst_stream_selector_chain (GstPad * pad, GstBuffer * buf);

static GstElementClass *parent_class = NULL;

GType
gst_stream_selector_get_type (void)
{
  static GType stream_selector_type = 0;

  if (!stream_selector_type) {
    static const GTypeInfo stream_selector_info = {
      sizeof (GstStreamSelectorClass),
      (GBaseInitFunc) gst_stream_selector_base_init,
      NULL,
      (GClassInitFunc) gst_stream_selector_class_init,
      NULL,
      NULL,
      sizeof (GstStreamSelector),
      0,
      (GInstanceInitFunc) gst_stream_selector_init,
    };

    stream_selector_type =
        g_type_register_static (GST_TYPE_ELEMENT,
        "GstStreamSelector", &stream_selector_info, 0);

    GST_DEBUG_CATEGORY_INIT (stream_selector_debug,
        "streamselector", 0, "A stream-selector element");
  }

  return stream_selector_type;
}

static void
gst_stream_selector_base_init (GstStreamSelectorClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  static GstElementDetails gst_stream_selector_details =
      GST_ELEMENT_DETAILS ("StreamSelector",
      "Generic",
      "N-to-1 input stream_selectoring",
      "Julien Moutte <julien@moutte.net>\n"
      "Ronald S. Bultje <rbultje@ronald.bitfreak.net>");

  gst_element_class_set_details (element_class, &gst_stream_selector_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_stream_selector_sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_stream_selector_src_factory));
}

static void
gst_stream_selector_class_init (GstStreamSelectorClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->dispose = gst_stream_selector_dispose;

  gstelement_class->request_new_pad = gst_stream_selector_request_new_pad;
}

static void
gst_stream_selector_init (GstStreamSelector * sel)
{
  sel->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_internal_link_function (sel->srcpad,
      GST_DEBUG_FUNCPTR (gst_stream_selector_get_linked_pads));
  gst_pad_set_getcaps_function (sel->srcpad,
      GST_DEBUG_FUNCPTR (gst_stream_selector_getcaps));
  gst_element_add_pad (GST_ELEMENT (sel), sel->srcpad);

  /* sinkpad management */
  sel->last_active_sinkpad = NULL;
  sel->nb_sinkpads = 0;

  //GST_FLAG_SET (sel, GST_ELEMENT_WORK_IN_PLACE);
}

static void
gst_stream_selector_dispose (GObject * object)
{
  GstStreamSelector *sel = GST_STREAM_SELECTOR (object);

  sel->last_active_sinkpad = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstPad *
gst_stream_selector_get_linked_pad (GstPad * pad, gboolean strict)
{
  GstStreamSelector *sel = GST_STREAM_SELECTOR (gst_pad_get_parent (pad));
  GstPad *otherpad = NULL;

  if (pad == sel->srcpad)
    otherpad = sel->last_active_sinkpad;
  else if (pad == sel->last_active_sinkpad || !strict)
    otherpad = sel->srcpad;

  return otherpad;
}

static GstCaps *
gst_stream_selector_getcaps (GstPad * pad)
{
  GstPad *otherpad = gst_stream_selector_get_linked_pad (pad, FALSE);

  if (!otherpad) {
    GST_DEBUG_OBJECT (gst_pad_get_parent (pad),
        "Pad %s not linked, returning ANY", gst_pad_get_name (pad));

    return gst_caps_new_any ();
  }

  GST_DEBUG_OBJECT (gst_pad_get_parent (pad),
      "Pad %s is linked (to %s), returning allowed-caps",
      gst_pad_get_name (pad), gst_pad_get_name (otherpad));

  return gst_pad_peer_get_caps (otherpad);
}

static GList *
gst_stream_selector_get_linked_pads (GstPad * pad)
{
  GstPad *otherpad = gst_stream_selector_get_linked_pad (pad, TRUE);

  if (!otherpad)
    return NULL;

  return g_list_append (NULL, otherpad);
}

static GstPad *
gst_stream_selector_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * unused)
{
  GstStreamSelector *sel = GST_STREAM_SELECTOR (element);
  gchar *name = NULL;
  GstPad *sinkpad = NULL;

  g_return_val_if_fail (templ->direction == GST_PAD_SINK, NULL);

  GST_LOG_OBJECT (sel, "Creating new pad %d", sel->nb_sinkpads);

  name = g_strdup_printf ("sink%d", sel->nb_sinkpads++);
  sinkpad = gst_pad_new_from_template (templ, name);
  if (sel->nb_sinkpads == 1)
    sel->last_active_sinkpad = sinkpad;
  g_free (name);

  gst_pad_set_getcaps_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_stream_selector_getcaps));
  gst_pad_set_chain_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_stream_selector_chain));
  gst_pad_set_internal_link_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_stream_selector_get_linked_pads));
  gst_element_add_pad (GST_ELEMENT (sel), sinkpad);

  GST_STATE_LOCK (sel);
  if (GST_STATE (sel) >= GST_STATE_PAUSED) {
    gst_pad_set_active (sinkpad, GST_ACTIVATE_PUSH);
  }
  GST_STATE_UNLOCK (sel);

  return sinkpad;
}

static GstFlowReturn
gst_stream_selector_chain (GstPad * pad, GstBuffer * buf)
{
  GstStreamSelector *sel = GST_STREAM_SELECTOR (gst_pad_get_parent (pad));
  GstFlowReturn res;

  /* first, check if the active pad changed. If so, redo
   * negotiation and fail if that fails. */
  if (pad != sel->last_active_sinkpad) {
    GST_LOG_OBJECT (sel, "stream change detected, switching from %s to %s",
        sel->last_active_sinkpad ?
        gst_pad_get_name (sel->last_active_sinkpad) : "none",
        gst_pad_get_name (pad));
    sel->last_active_sinkpad = pad;
  }

  /* forward */
  GST_DEBUG_OBJECT (sel, "Forwarding buffer %p from pad %s",
      "buf", gst_pad_get_name (pad));
  res = gst_pad_push (sel->srcpad, buf);

  return res;
}
