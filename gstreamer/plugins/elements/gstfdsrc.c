/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstfdsrc.c: 
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <gstfdsrc.h>

#define DEFAULT_BLOCKSIZE	4096

GstElementDetails gst_fdsrc_details = 
{
  "Disk Source",
  "Source/File",
  "LGPL",
  "Synchronous read from a file",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


/* FdSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_FD,
  ARG_BLOCKSIZE,
};


static void		gst_fdsrc_class_init	(GstFdSrcClass *klass);
static void		gst_fdsrc_init		(GstFdSrc *fdsrc);

static void		gst_fdsrc_set_property	(GObject *object, guint prop_id, 
						 const GValue *value, GParamSpec *pspec);
static void		gst_fdsrc_get_property	(GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);

static GstBuffer *	gst_fdsrc_get		(GstPad *pad);


static GstElementClass *parent_class = NULL;
/*static guint gst_fdsrc_signals[LAST_SIGNAL] = { 0 };*/

GType
gst_fdsrc_get_type (void) 
{
  static GType fdsrc_type = 0;

  if (!fdsrc_type) {
    static const GTypeInfo fdsrc_info = {
      sizeof(GstFdSrcClass),      
      NULL,
      NULL,
      (GClassInitFunc)gst_fdsrc_class_init,
      NULL,
      NULL,
      sizeof(GstFdSrc),
      0,
      (GInstanceInitFunc)gst_fdsrc_init,
    };
    fdsrc_type = g_type_register_static (GST_TYPE_ELEMENT, "GstFdSrc", &fdsrc_info, 0);
  }
  return fdsrc_type;
}

static void
gst_fdsrc_class_init (GstFdSrcClass *klass) 
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FD,
    g_param_spec_int ("fd", "fd", "An open file descriptor to read from",
                      0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BLOCKSIZE,
    g_param_spec_ulong ("blocksize", "Block size", "Size in bytes to read per buffer",
                        1, G_MAXULONG, DEFAULT_BLOCKSIZE, G_PARAM_READWRITE));

  gobject_class->set_property = gst_fdsrc_set_property;
  gobject_class->get_property = gst_fdsrc_get_property;
}

static void gst_fdsrc_init(GstFdSrc *fdsrc) {
  fdsrc->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  
  gst_pad_set_get_function (fdsrc->srcpad, gst_fdsrc_get);
  gst_element_add_pad (GST_ELEMENT (fdsrc), fdsrc->srcpad);

  fdsrc->fd = 0;
  fdsrc->curoffset = 0;
  fdsrc->blocksize = DEFAULT_BLOCKSIZE;
  fdsrc->seq = 0;
}


static void 
gst_fdsrc_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) 
{
  GstFdSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FDSRC (object));
  
  src = GST_FDSRC (object);

  switch (prop_id) {
    case ARG_FD:
      src->fd = g_value_get_int (value);
      break;
    case ARG_BLOCKSIZE:
      src->blocksize = g_value_get_ulong (value);
      break;
    default:
      break;
  }
}

static void 
gst_fdsrc_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) 
{
  GstFdSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FDSRC (object));
  
  src = GST_FDSRC (object);

  switch (prop_id) {
    case ARG_BLOCKSIZE:
      g_value_set_ulong (value, src->blocksize);
      break;
    case ARG_FD:
      g_value_set_int (value, src->fd);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstBuffer *
gst_fdsrc_get(GstPad *pad)
{
  GstFdSrc *src;
  GstBuffer *buf;
  glong readbytes;

  src = GST_FDSRC (gst_pad_get_parent (pad));

  /* create the buffer */
  buf = gst_buffer_new_and_alloc (src->blocksize);

  /* read it in from the file */
  readbytes = read (src->fd, GST_BUFFER_DATA (buf), src->blocksize);

  /* if nothing was read, we're in eos */
  if (readbytes == 0) {
    gst_element_set_eos (GST_ELEMENT (src));
    return GST_BUFFER (gst_event_new (GST_EVENT_EOS));
  }

  if (readbytes == -1) {
	  g_error ("Error reading from file descriptor. Ending stream.\n");
	  gst_element_set_eos (GST_ELEMENT (src));
	  return GST_BUFFER (gst_event_new (GST_EVENT_EOS));
	  }
  
  GST_BUFFER_OFFSET (buf) = src->curoffset;
  GST_BUFFER_SIZE (buf) = readbytes;
  GST_BUFFER_TIMESTAMP (buf) = GST_CLOCK_TIME_NONE;
  src->curoffset += readbytes;

  /* we're done, return the buffer */
  return buf;
}
