/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * EffecTV:
 * Copyright (C) 2001 FUKUCHI Kentarou
 *
 * Inspired by Adrian Likin's script for the GIMP.
 * EffecTV is free software.  This library is free software;
 * you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
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

#define GST_TYPE_SHAGADELICTV \
  (gst_shagadelictv_get_type())
#define GST_SHAGADELICTV(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SHAGADELICTV,GstShagadelicTV))
#define GST_SHAGADELICTV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstShagadelicTV))
#define GST_IS_SHAGADELICTV(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SHAGADELICTV))
#define GST_IS_SHAGADELICTV_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SHAGADELICTV))

typedef struct _GstShagadelicTV GstShagadelicTV;
typedef struct _GstShagadelicTVClass GstShagadelicTVClass;

struct _GstShagadelicTV
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gint width, height;
  gint stat;
  gchar *ripple;
  gchar *spiral;
  guchar phase;
  gint rx, ry;
  gint bx, by;
  gint rvx, rvy;
  gint bvx, bvy;
};

struct _GstShagadelicTVClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
GstElementDetails gst_shagadelictv_details = {
  "ShagadelicTV",
  "Filter/Video/Effect",
  "LGPL",
  "Oh behave, ShagedelicTV makes images shagadelic!",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
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
};

static void 	gst_shagadelictv_class_init 	(GstShagadelicTVClass * klass);
static void 	gst_shagadelictv_init 		(GstShagadelicTV * filter);

static void 	gst_shagadelic_initialize 	(GstShagadelicTV *filter);

static void 	gst_shagadelictv_set_property 	(GObject * object, guint prop_id,
					  	 const GValue * value, GParamSpec * pspec);
static void 	gst_shagadelictv_get_property 	(GObject * object, guint prop_id,
					  	 GValue * value, GParamSpec * pspec);

static void 	gst_shagadelictv_chain 		(GstPad * pad, GstBuffer * buf);

static GstElementClass *parent_class = NULL;
/*static guint gst_shagadelictv_signals[LAST_SIGNAL] = { 0 }; */

GType gst_shagadelictv_get_type (void)
{
  static GType shagadelictv_type = 0;

  if (!shagadelictv_type) {
    static const GTypeInfo shagadelictv_info = {
      sizeof (GstShagadelicTVClass), NULL,
      NULL,
      (GClassInitFunc) gst_shagadelictv_class_init,
      NULL,
      NULL,
      sizeof (GstShagadelicTV),
      0,
      (GInstanceInitFunc) gst_shagadelictv_init,
    };

    shagadelictv_type = g_type_register_static (GST_TYPE_ELEMENT, "GstShagadelicTV", &shagadelictv_info, 0);
  }
  return shagadelictv_type;
}

static void
gst_shagadelictv_class_init (GstShagadelicTVClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_shagadelictv_set_property;
  gobject_class->get_property = gst_shagadelictv_get_property;
}

static GstPadLinkReturn
gst_shagadelictv_sinkconnect (GstPad * pad, GstCaps * caps)
{
  GstShagadelicTV *filter;
  gint area;

  filter = GST_SHAGADELICTV (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;

  gst_caps_get_int (caps, "width", &filter->width);
  gst_caps_get_int (caps, "height", &filter->height);

  area = filter->width * filter->height;

  g_free (filter->ripple);
  g_free (filter->spiral);

  filter->ripple = (gchar *) g_malloc (area * 4);
  filter->spiral = (gchar *) g_malloc (area);

  gst_shagadelic_initialize (filter);

  return gst_pad_try_set_caps (filter->srcpad, caps);
}

static void
gst_shagadelictv_init (GstShagadelicTV * filter)
{
  filter->sinkpad = gst_pad_new_from_template (gst_effectv_sink_factory (), "sink");
  gst_pad_set_chain_function (filter->sinkpad, gst_shagadelictv_chain);
  gst_pad_set_link_function (filter->sinkpad, gst_shagadelictv_sinkconnect);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_template (gst_effectv_src_factory (), "src");
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->ripple = NULL;
  filter->spiral = NULL;
}

static unsigned int
fastrand (void)
{   
  static unsigned int fastrand_val;

  return (fastrand_val = fastrand_val * 1103515245 + 12345);
}

static void
gst_shagadelic_initialize (GstShagadelicTV *filter)
{
  int i, x, y;
#ifdef PS2
  float xx, yy;
#else
  double xx, yy;
#endif

  i = 0;
  for(y = 0; y < filter->height * 2; y++) {
    yy = y - filter->height;
    yy *= yy;

    for (x = 0; x < filter->width * 2; x++) {
      xx = x - filter->width;
#ifdef PS2
      filter->ripple[i++] = ((unsigned int)(sqrtf(xx*xx+yy)*8))&255;
#else
      filter->ripple[i++] = ((unsigned int)(sqrt(xx*xx+yy)*8))&255;
#endif
    }
  }

  i = 0;
  for (y = 0; y < filter->height; y++) {
    yy = y - filter->height/2;
    
    for (x = 0; x < filter->width; x++) {
      xx = x - filter->width/2;
#ifdef PS2
      filter->spiral[i++] = ((unsigned int)
	((atan2f(xx, yy)/((float)M_PI)*256*9) + (sqrtf(xx*xx+yy*yy)*5)))&255;
#else
      filter->spiral[i++] = ((unsigned int)
	((atan2(xx, yy)/M_PI*256*9) + (sqrt(xx*xx+yy*yy)*5)))&255;
#endif
/* Here is another Swinger!
 * ((atan2(xx, yy)/M_PI*256) + (sqrt(xx*xx+yy*yy)*10))&255;
 */
    }
  }
  filter->rx = fastrand () % filter->width;
  filter->ry = fastrand () % filter->height;
  filter->bx = fastrand () % filter->width;
  filter->by = fastrand () % filter->height;
  filter->rvx = -2;
  filter->rvy = -2;
  filter->bvx = 2;
  filter->bvy = 2;
  filter->phase = 0;
}

int shagadelicDraw()
{
	return 0;
}
static void
gst_shagadelictv_chain (GstPad * pad, GstBuffer * buf)
{
  GstShagadelicTV *filter;
  guint32 *src, *dest;
  GstBuffer *outbuf;
  gint x, y;
  guint32 v;
  guchar r, g, b;
  gint width, height;

  filter = GST_SHAGADELICTV (gst_pad_get_parent (pad));

  src = (guint32 *) GST_BUFFER_DATA (buf);

  outbuf = gst_buffer_new ();
  GST_BUFFER_SIZE (outbuf) = (filter->width * filter->height * 4);
  dest = (guint32 *) GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);

  width = filter->width;
  height = filter->height;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      v = *src++ | 0x1010100;
      v = (v - 0x707060) & 0x1010100;
      v -= v >> 8;
/* Try another Babe! 
 * v = *src++;
 * *dest++ = v & ((r<<16)|(g<<8)|b);
 */
      r = (gchar) (filter->ripple[(filter->ry + y) * width * 2 + filter->rx + x] + filter->phase * 2) >> 7;
      g = (gchar) (filter->spiral[y * width + x] + filter->phase * 3) >> 7;
      b = (gchar) (filter->ripple[(filter->by + y) * width * 2 + filter->bx + x] - filter->phase) >> 7;
      *dest++ = v & ((r << 16) | (g << 8) | b);
    }
  }

  filter->phase -= 8;
  if ((filter->rx + filter->rvx) < 0 || (filter->rx + filter->rvx) >= width)  filter->rvx =- filter->rvx;
  if ((filter->ry + filter->rvy) < 0 || (filter->ry + filter->rvy) >= height) filter->rvy =- filter->rvy;
  if ((filter->bx + filter->bvx) < 0 || (filter->bx + filter->bvx) >= width)  filter->bvx =- filter->bvx;
  if ((filter->by + filter->bvy) < 0 || (filter->by + filter->bvy) >= height) filter->bvy =- filter->bvy;
  filter->rx += filter->rvx;
  filter->ry += filter->rvy;
  filter->bx += filter->bvx;
  filter->by += filter->bvy;

  gst_buffer_unref (buf);

  gst_pad_push (filter->srcpad, outbuf);
}

static void
gst_shagadelictv_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstShagadelicTV *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SHAGADELICTV (object));

  filter = GST_SHAGADELICTV (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_shagadelictv_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstShagadelicTV *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SHAGADELICTV (object));

  filter = GST_SHAGADELICTV (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
