/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * EffecTV:
 * Copyright (C) 2001 FUKUCHI Kentarou
 *
 *  EffecTV is free software. This library is free software;
 * you can redistribute it and/or
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

#include <math.h>
#include <string.h>
#include <gst/gst.h>
#include "gsteffectv.h"

#define GST_TYPE_QUARKTV \
  (gst_quarktv_get_type())
#define GST_QUARKTV(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QUARKTV,GstQuarkTV))
#define GST_QUARKTV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstQuarkTV))
#define GST_IS_QUARKTV(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QUARKTV))
#define GST_IS_QUARKTV_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QUARKTV))

/* number of frames of time-buffer. It should be as a configurable paramter */
/* This number also must be 2^n just for the speed. */
#define PLANES 16

typedef struct _GstQuarkTV GstQuarkTV;
typedef struct _GstQuarkTVClass GstQuarkTVClass;

struct _GstQuarkTV
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gint width, height;
  gint area;
  gint planes;
  gint current_plane;
  GstBuffer **planetable;
};

struct _GstQuarkTVClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
GstElementDetails gst_quarktv_details = {
  "QuarkTV",
  "Filter/Video/Effect",
  "LGPL",
  "Motion disolver",
  VERSION,
  "FUKUCHI, Kentarou <fukuchi@users.sourceforge.net>",
  "(C) 2001 FUKUCHI Kentarou",
};


/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_PLANES,
};

static void 	gst_quarktv_class_init 		(GstQuarkTVClass * klass);
static void 	gst_quarktv_init 		(GstQuarkTV * filter);

static GstElementStateReturn
		gst_quarktv_change_state 	(GstElement *element);
		
static void 	gst_quarktv_set_property 	(GObject * object, guint prop_id,
					  	 const GValue * value, GParamSpec * pspec);
static void 	gst_quarktv_get_property 	(GObject * object, guint prop_id,
					  	 GValue * value, GParamSpec * pspec);

static void 	gst_quarktv_chain 		(GstPad * pad, GstBuffer * buf);

static GstElementClass *parent_class = NULL;
/* static guint gst_quarktv_signals[LAST_SIGNAL] = { 0 }; */

static inline guint32
fastrand (void)
{
  static unsigned int fastrand_val;

  return (fastrand_val = fastrand_val * 1103515245 + 12345);
}

GType gst_quarktv_get_type (void)
{
  static GType quarktv_type = 0;

  if (!quarktv_type) {
    static const GTypeInfo quarktv_info = {
      sizeof (GstQuarkTVClass), 
      NULL,
      NULL,
      (GClassInitFunc) gst_quarktv_class_init,
      NULL,
      NULL,
      sizeof (GstQuarkTV),
      0,
      (GInstanceInitFunc) gst_quarktv_init,
    };

    quarktv_type = g_type_register_static (GST_TYPE_ELEMENT, "GstQuarkTV", &quarktv_info, 0);
  }
  return quarktv_type;
}

static void
gst_quarktv_class_init (GstQuarkTVClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PLANES,
    g_param_spec_int ("planes","Planes","Number of frames in the buffer",
                      1, 32, PLANES, G_PARAM_READWRITE));
       
  gobject_class->set_property = gst_quarktv_set_property;
  gobject_class->get_property = gst_quarktv_get_property;

  gstelement_class->change_state = gst_quarktv_change_state;
}

static GstPadLinkReturn
gst_quarktv_sinkconnect (GstPad * pad, GstCaps * caps)
{
  GstQuarkTV *filter;
  gint i;

  filter = GST_QUARKTV (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;

  gst_caps_get_int (caps, "width", &filter->width);
  gst_caps_get_int (caps, "height", &filter->height);

  filter->area = filter->width * filter->height;

  g_free (filter->planetable);
  filter->planetable = (GstBuffer **) g_malloc(filter->planes * sizeof(GstBuffer *));

  for(i = 0; i < filter->planes; i++) {
    filter->planetable[i] = NULL;
  }

  return gst_pad_try_set_caps (filter->srcpad, caps);
}

static void
gst_quarktv_init (GstQuarkTV * filter)
{
  filter->sinkpad = gst_pad_new_from_template (gst_effectv_sink_factory (), "sink");
  gst_pad_set_chain_function (filter->sinkpad, gst_quarktv_chain);
  gst_pad_set_link_function (filter->sinkpad, gst_quarktv_sinkconnect);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_template (gst_effectv_src_factory (), "src");
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->planes = PLANES;
  filter->current_plane = filter->planes - 1;
  filter->planetable = NULL;
}

static void
gst_quarktv_chain (GstPad * pad, GstBuffer * buf)
{
  GstQuarkTV *filter;
  guint32 *src, *dest;
  GstBuffer *outbuf;
  gint area;

  filter = GST_QUARKTV (gst_pad_get_parent (pad));

  src = (guint32 *) GST_BUFFER_DATA (buf);

  area = filter->area;

  outbuf = gst_buffer_new ();
  GST_BUFFER_SIZE (outbuf) = area * sizeof(guint32);
  dest = (guint32 *) GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
  
  if (filter->planetable[filter->current_plane]) 
    gst_buffer_unref (filter->planetable[filter->current_plane]);

  filter->planetable[filter->current_plane] = buf;

  while (--area) {
    GstBuffer *rand;
    
    /* pick a random buffer */
    rand = filter->planetable[(filter->current_plane + (fastrand () >> 24)) & (filter->planes - 1)];
    
    dest[area] = (rand ? ((guint32 *)GST_BUFFER_DATA (rand))[area] : 0);
  }

  gst_pad_push (filter->srcpad, outbuf);

  filter->current_plane--;
  
  if (filter->current_plane < 0) 
    filter->current_plane = filter->planes - 1;
}

static GstElementStateReturn
gst_quarktv_change_state (GstElement *element)
{ 
  GstQuarkTV *filter = GST_QUARKTV (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
    {
      gint i;

      for (i = 0; i < filter->planes; i++) {
        if (filter->planetable[i])
          gst_buffer_unref (filter->planetable[i]);
        filter->planetable[i] = NULL;
      }
      g_free (filter->planetable);
      filter->planetable = NULL;
      break;
    }
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element);
}


static void
gst_quarktv_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstQuarkTV *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_QUARKTV (object));

  filter = GST_QUARKTV (object);

  switch (prop_id) {
    case ARG_PLANES:
      filter->planes = g_value_get_int (value);
      filter->current_plane = filter->planes - 1;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_quarktv_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstQuarkTV *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_QUARKTV (object));

  filter = GST_QUARKTV (object);

  switch (prop_id) {
    case ARG_PLANES:
      g_value_set_int (value, filter->planes);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
