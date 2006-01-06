/* GStreamer
 * (c) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * (c) 2005 Tim-Philipp Müller <tim centricular net>
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
#include "config.h"
#endif

#include "gstgconfelements.h"
#include "gstgconfvideosrc.h"

static void gst_gconf_video_src_dispose (GObject * object);
static void cb_toggle_element (GConfClient * client,
    guint connection_id, GConfEntry * entry, gpointer data);
static GstStateChangeReturn
gst_gconf_video_src_change_state (GstElement * element,
    GstStateChange transition);

GST_BOILERPLATE (GstGConfVideoSrc, gst_gconf_video_src, GstBin, GST_TYPE_BIN);

static void
gst_gconf_video_src_base_init (gpointer klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);
  GstElementDetails gst_gconf_video_src_details = {
    "GConf video source",
    "Source/Video",
    "Video source embedding the GConf-settings for video input",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>"
  };
  GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS_ANY);

  gst_element_class_add_pad_template (eklass,
      gst_static_pad_template_get (&src_template));
  gst_element_class_set_details (eklass, &gst_gconf_video_src_details);
}

static void
gst_gconf_video_src_class_init (GstGConfVideoSrcClass * klass)
{
  GObjectClass *oklass = G_OBJECT_CLASS (klass);
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  oklass->dispose = gst_gconf_video_src_dispose;
  eklass->change_state = gst_gconf_video_src_change_state;
}

/*
 * Hack to make negotiation work.
 */

static void
gst_gconf_video_src_reset (GstGConfVideoSrc * src)
{
  GstPad *targetpad;

  /* fakesrc */
  if (src->kid) {
    gst_element_set_state (src->kid, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (src), src->kid);
  }
  src->kid = gst_element_factory_make ("fakesrc", "testsrc");
  gst_bin_add (GST_BIN (src), src->kid);

  targetpad = gst_element_get_pad (src->kid, "src");
  gst_ghost_pad_set_target (GST_GHOST_PAD (src->pad), targetpad);
  gst_object_unref (targetpad);
}

static void
gst_gconf_video_src_init (GstGConfVideoSrc * src,
    GstGConfVideoSrcClass * g_class)
{
  src->pad = gst_ghost_pad_new_no_target ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (src), src->pad);

  gst_gconf_video_src_reset (src);

  src->client = gconf_client_get_default ();
  gconf_client_add_dir (src->client, GST_GCONF_DIR,
      GCONF_CLIENT_PRELOAD_RECURSIVE, NULL);
  gconf_client_notify_add (src->client, GST_GCONF_DIR "/default/videosrc",
      cb_toggle_element, src, NULL, NULL);
}

static void
gst_gconf_video_src_dispose (GObject * object)
{
  GstGConfVideoSrc *src = GST_GCONF_VIDEO_SRC (object);

  if (src->client) {
    g_object_unref (G_OBJECT (src->client));
    src->client = NULL;
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static gboolean
do_toggle_element (GstGConfVideoSrc * src)
{
  GstPad *targetpad;

  /* kill old element */
  if (src->kid) {
    GST_DEBUG_OBJECT (src, "Removing old kid");
    gst_element_set_state (src->kid, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (src), src->kid);
    src->kid = NULL;
  }

  GST_DEBUG_OBJECT (src, "Creating new kid");
  if (!(src->kid = gst_gconf_get_default_video_src ())) {
    GST_ELEMENT_ERROR (src, LIBRARY, SETTINGS, (NULL),
        ("Failed to render video source from GConf"));
    return FALSE;
  }
  gst_element_set_state (src->kid, GST_STATE (src));
  gst_bin_add (GST_BIN (src), src->kid);

  /* re-attach ghostpad */
  GST_DEBUG_OBJECT (src, "Creating new ghostpad");
  targetpad = gst_element_get_pad (src->kid, "src");
  gst_ghost_pad_set_target (GST_GHOST_PAD (src->pad), targetpad);
  gst_object_unref (targetpad);
  GST_DEBUG_OBJECT (src, "done changing gconf video source");

  return TRUE;
}

static void
cb_toggle_element (GConfClient * client,
    guint connection_id, GConfEntry * entry, gpointer data)
{
  do_toggle_element (GST_GCONF_VIDEO_SRC (data));
}

static GstStateChangeReturn
gst_gconf_video_src_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstGConfVideoSrc *src = GST_GCONF_VIDEO_SRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!do_toggle_element (src))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  ret = GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS, change_state,
      (element, transition), GST_STATE_CHANGE_SUCCESS);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_gconf_video_src_reset (src);
      break;
    default:
      break;
  }

  return ret;
}
