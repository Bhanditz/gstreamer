/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


/*#define DEBUG_ENABLED */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gstvideoflip.h>
#include <videoflip.h>



/* elementfactory information */
static GstElementDetails videoflip_details = {
  "Video scaler",
  "Filter/Video",
  "LGPL",
  "Resizes video",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2000",
};

/* GstVideoflip signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_METHOD,
  /* FILL ME */
};

static void	gst_videoflip_class_init	(GstVideoflipClass *klass);
static void	gst_videoflip_init		(GstVideoflip *videoflip);

static void	gst_videoflip_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_videoflip_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void	gst_videoflip_chain		(GstPad *pad, GstData *_data);
static GstCaps * gst_videoflip_get_capslist(void);

static GstElementClass *parent_class = NULL;

#define GST_TYPE_VIDEOFLIP_METHOD (gst_videoflip_method_get_type())

static GType
gst_videoflip_method_get_type(void)
{
  static GType videoflip_method_type = 0;
  static GEnumValue videoflip_methods[] = {
    { GST_VIDEOFLIP_METHOD_IDENTITY,	"0", "Identity (no rotation)" },
    { GST_VIDEOFLIP_METHOD_90R,		"1", "Rotate right 90 degrees" },
    { GST_VIDEOFLIP_METHOD_180,		"2", "Rotate 180 degrees" },
    { GST_VIDEOFLIP_METHOD_90L,		"3", "Rotate left 90 degrees" },
    { GST_VIDEOFLIP_METHOD_HORIZ,	"4", "Flip horizontally" },
    { GST_VIDEOFLIP_METHOD_VERT,	"5", "Flip vertically" },
    { GST_VIDEOFLIP_METHOD_TRANS,	"6", "Flip across upper left/lower right diagonal" },
    { GST_VIDEOFLIP_METHOD_OTHER,	"7", "Flip across upper right/lower left diagonal" },
    { 0, NULL, NULL },
  };
  if(!videoflip_method_type){
    videoflip_method_type = g_enum_register_static("GstVideoflipMethod",
	videoflip_methods);
  }
  return videoflip_method_type;
}

GType
gst_videoflip_get_type (void)
{
  static GType videoflip_type = 0;

  if (!videoflip_type) {
    static const GTypeInfo videoflip_info = {
      sizeof(GstVideoflipClass),      NULL,
      NULL,
      (GClassInitFunc)gst_videoflip_class_init,
      NULL,
      NULL,
      sizeof(GstVideoflip),
      0,
      (GInstanceInitFunc)gst_videoflip_init,
    };
    videoflip_type = g_type_register_static(GST_TYPE_ELEMENT, "GstVideoflip", &videoflip_info, 0);
  }
  return videoflip_type;
}

static void
gst_videoflip_class_init (GstVideoflipClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_METHOD,
      g_param_spec_enum("method","method","method",
      GST_TYPE_VIDEOFLIP_METHOD, GST_VIDEOFLIP_METHOD_90R,
      G_PARAM_READWRITE));

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_videoflip_set_property;
  gobject_class->get_property = gst_videoflip_get_property;

}

static GstPadTemplate *
gst_videoflip_src_template_factory(void)
{
  static GstPadTemplate *templ = NULL;

  if(!templ){
    /* well, actually RGB too, but since there's no RGB format anyway */
    GstCaps *caps = GST_CAPS_NEW("src","video/x-raw-yuv",
		"width", GST_PROPS_INT_RANGE (0, G_MAXINT),
		"height", GST_PROPS_INT_RANGE (0, G_MAXINT),
                "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT));

    caps = gst_caps_intersect(caps, gst_videoflip_get_capslist ());

    templ = GST_PAD_TEMPLATE_NEW("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
  }
  return templ;
}

static GstPadTemplate *
gst_videoflip_sink_template_factory(void)
{
  static GstPadTemplate *templ = NULL;

  if(!templ){
    GstCaps *caps = GST_CAPS_NEW("sink","video/x-raw-yuv",
		"width", GST_PROPS_INT_RANGE (0, G_MAXINT),
		"height", GST_PROPS_INT_RANGE (0, G_MAXINT),
                "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT));

    caps = gst_caps_intersect(caps, gst_videoflip_get_capslist ());

    templ = GST_PAD_TEMPLATE_NEW("src", GST_PAD_SINK, GST_PAD_ALWAYS, caps);
  }
  return templ;
}

static GstCaps *
gst_videoflip_get_capslist(void)
{
  static GstCaps *capslist = NULL;
  GstCaps *caps;
  int i;

  if (capslist){
    gst_caps_ref(capslist);
    return capslist;
  }

  for(i=0;i<videoflip_n_formats;i++){
    caps = videoflip_get_caps(videoflip_formats + i);
    capslist = gst_caps_append(capslist, caps);
  }

  gst_caps_ref(capslist);
  return capslist;
}

static GstCaps *
gst_videoflip_sink_getcaps (GstPad *pad, GstCaps *caps)
{
  GstVideoflip *videoflip;
  GstCaps *capslist = NULL;
  GstCaps *peercaps;
  GstCaps *sizecaps;
  int i;

  GST_DEBUG ("gst_videoflip_src_link");
  videoflip = GST_VIDEOFLIP (gst_pad_get_parent (pad));
  
  /* get list of peer's caps */
  if(pad == videoflip->srcpad){
    peercaps = gst_pad_get_allowed_caps (videoflip->sinkpad);
  }else{
    peercaps = gst_pad_get_allowed_caps (videoflip->srcpad);
  }

  /* FIXME videoflip doesn't allow passthru of video formats it
   * doesn't understand. */
  /* Look through our list of caps and find those that match with
   * the peer's formats.  Create a list of them. */
  for(i=0;i<videoflip_n_formats;i++){
    GstCaps *fromcaps = videoflip_get_caps(videoflip_formats + i);
    if(gst_caps_is_always_compatible(fromcaps, peercaps)){
      capslist = gst_caps_append(capslist, fromcaps);
    }
    gst_caps_unref (fromcaps);
  }
  gst_caps_unref (peercaps);

  sizecaps = GST_CAPS_NEW("videoflip_size","video/x-raw-yuv",
		"width", GST_PROPS_INT_RANGE (0, G_MAXINT),
		"height", GST_PROPS_INT_RANGE (0, G_MAXINT),
                "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT));

  caps = gst_caps_intersect(caps, gst_videoflip_get_capslist ());
  gst_caps_unref (sizecaps);

  return caps;
}


static GstPadLinkReturn
gst_videoflip_src_link (GstPad *pad, GstCaps *caps)
{
  GstVideoflip *videoflip;
  GstPadLinkReturn ret;
  GstCaps *peercaps;

  GST_DEBUG ("gst_videoflip_src_link");
  videoflip = GST_VIDEOFLIP (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps)) {
    return GST_PAD_LINK_DELAYED;
  }

  gst_caps_debug(caps,"ack");

  videoflip->format = videoflip_find_by_caps (caps);
  g_return_val_if_fail(videoflip->format, GST_PAD_LINK_REFUSED);

  gst_caps_get_int (caps, "width", &videoflip->to_width);
  gst_caps_get_int (caps, "height", &videoflip->to_height);

  GST_DEBUG ("width %d height %d",videoflip->to_width,videoflip->to_height);

  peercaps = gst_caps_copy(caps);

  gst_caps_set(peercaps, "width", GST_PROPS_INT_RANGE (0, G_MAXINT));
  gst_caps_set(peercaps, "height", GST_PROPS_INT_RANGE (0, G_MAXINT));

  ret = gst_pad_try_set_caps (videoflip->srcpad, peercaps);

  gst_caps_unref(peercaps);

  if(ret==GST_PAD_LINK_OK){
    caps = gst_pad_get_caps (videoflip->srcpad);

    gst_caps_get_int (caps, "width", &videoflip->from_width);
    gst_caps_get_int (caps, "height", &videoflip->from_height);
    gst_videoflip_setup(videoflip);
  }

  return ret;
}

static GstPadLinkReturn
gst_videoflip_sink_link (GstPad *pad, GstCaps *caps)
{
  GstVideoflip *videoflip;
  GstPadLinkReturn ret;
  GstCaps *peercaps;

  GST_DEBUG ("gst_videoflip_src_link");
  videoflip = GST_VIDEOFLIP (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps)) {
    return GST_PAD_LINK_DELAYED;
  }

  videoflip->format = videoflip_find_by_caps (caps);
  gst_caps_debug(caps,"ack");
  g_return_val_if_fail(videoflip->format, GST_PAD_LINK_REFUSED);

  gst_caps_get_int (caps, "width", &videoflip->from_width);
  gst_caps_get_int (caps, "height", &videoflip->from_height);

  gst_videoflip_setup(videoflip);

  peercaps = gst_caps_copy(caps);

  gst_caps_set(peercaps, "width", GST_PROPS_INT (videoflip->to_width));
  gst_caps_set(peercaps, "height", GST_PROPS_INT (videoflip->to_height));

  ret = gst_pad_try_set_caps (videoflip->srcpad, peercaps);

  gst_caps_unref(peercaps);

  if(ret==GST_PAD_LINK_OK){
    caps = gst_pad_get_caps (videoflip->srcpad);

    gst_caps_get_int (caps, "width", &videoflip->to_width);
    gst_caps_get_int (caps, "height", &videoflip->to_height);
    gst_videoflip_setup(videoflip);
  }

  return ret;
}

static void
gst_videoflip_init (GstVideoflip *videoflip)
{
  GST_DEBUG ("gst_videoflip_init");
  videoflip->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (gst_videoflip_sink_template_factory),
		  "sink");
  gst_element_add_pad(GST_ELEMENT(videoflip),videoflip->sinkpad);
  gst_pad_set_chain_function(videoflip->sinkpad,gst_videoflip_chain);
  gst_pad_set_link_function(videoflip->sinkpad,gst_videoflip_sink_link);
  gst_pad_set_getcaps_function(videoflip->sinkpad,gst_videoflip_sink_getcaps);

  videoflip->srcpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (gst_videoflip_src_template_factory),
		  "src");
  gst_element_add_pad(GST_ELEMENT(videoflip),videoflip->srcpad);
  gst_pad_set_link_function(videoflip->srcpad,gst_videoflip_src_link);
  //gst_pad_set_getcaps_function(videoflip->srcpad,gst_videoflip_getcaps);

  videoflip->inited = FALSE;
  videoflip->force_size = FALSE;
}


static void
gst_videoflip_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstVideoflip *videoflip;
  guchar *data;
  gulong size;
  GstBuffer *outbuf;

  GST_DEBUG ("gst_videoflip_chain");

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  videoflip = GST_VIDEOFLIP (gst_pad_get_parent (pad));
  g_return_if_fail (videoflip->inited);

  data = GST_BUFFER_DATA(buf);
  size = GST_BUFFER_SIZE(buf);

  if(videoflip->passthru){
    gst_pad_push(videoflip->srcpad, GST_DATA (buf));
    return;
  }

  GST_DEBUG ("gst_videoflip_chain: got buffer of %ld bytes in '%s'",size,
		              GST_OBJECT_NAME (videoflip));
 
  GST_DEBUG ("size=%ld from=%dx%d to=%dx%d fromsize=%ld (should be %d) tosize=%d",
	size,
	videoflip->from_width, videoflip->from_height,
	videoflip->to_width, videoflip->to_height,
  	size, videoflip->from_buf_size,
  	videoflip->to_buf_size);

  g_return_if_fail (size == videoflip->from_buf_size);

  outbuf = gst_buffer_new();
  /* FIXME: handle bufferpools */
  GST_BUFFER_SIZE(outbuf) = videoflip->to_buf_size;
  GST_BUFFER_DATA(outbuf) = g_malloc (videoflip->to_buf_size);
  GST_BUFFER_TIMESTAMP(outbuf) = GST_BUFFER_TIMESTAMP(buf);

  g_return_if_fail(videoflip->format);
  GST_DEBUG ("format %s",videoflip->format->fourcc);
  g_return_if_fail(videoflip->format->scale);

  videoflip->format->scale(videoflip, GST_BUFFER_DATA(outbuf), data);

  GST_DEBUG ("gst_videoflip_chain: pushing buffer of %d bytes in '%s'",GST_BUFFER_SIZE(outbuf),
	              GST_OBJECT_NAME (videoflip));

  gst_pad_push(videoflip->srcpad, GST_DATA (outbuf));

  gst_buffer_unref(buf);
}

static void
gst_videoflip_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstVideoflip *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_VIDEOFLIP(object));
  src = GST_VIDEOFLIP(object);

  GST_DEBUG ("gst_videoflip_set_property");
  switch (prop_id) {
    case ARG_METHOD:
      src->method = g_value_get_enum (value);
      break;
    default:
      break;
  }
}

static void
gst_videoflip_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstVideoflip *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_VIDEOFLIP(object));
  src = GST_VIDEOFLIP(object);

  switch (prop_id) {
    case ARG_METHOD:
      g_value_set_enum (value, src->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the videoflip element */
  factory = gst_element_factory_new("videoflip",GST_TYPE_VIDEOFLIP,
                                   &videoflip_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (gst_videoflip_sink_template_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (gst_videoflip_src_template_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "videoflip",
  plugin_init
};
