/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstdisksrc.c:
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
#include <unistd.h>
#include <sys/mman.h>

//#define GST_DEBUG_ENABLED

#include "gstdisksrc.h"


GstElementDetails gst_disksrc_details = {
  "Disk Source",
  "Source/File",
  "Read from arbitrary point in a file",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


/* DiskSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_LOCATION,
  ARG_BYTESPERREAD,
  ARG_OFFSET,
  ARG_SIZE,
};


static void		gst_disksrc_class_init		(GstDiskSrcClass *klass);
static void		gst_disksrc_init		(GstDiskSrc *disksrc);

static void		gst_disksrc_set_property	(GObject *object, guint prop_id, 
							 const GValue *value, GParamSpec *pspec);
static void		gst_disksrc_get_property	(GObject *object, guint prop_id, 
							 GValue *value, GParamSpec *pspec);

static GstBuffer *	gst_disksrc_get			(GstPad *pad);
static GstBuffer *	gst_disksrc_get_region		(GstPad *pad,GstRegionType type,guint64 offset,guint64 len);

static GstElementStateReturn	
                 	gst_disksrc_change_state	(GstElement *element);

static gboolean		gst_disksrc_open_file		(GstDiskSrc *src);
static void		gst_disksrc_close_file		(GstDiskSrc *src);

static GstElementClass *parent_class = NULL;
//static guint gst_disksrc_signals[LAST_SIGNAL] = { 0 };

GType
gst_disksrc_get_type(void)
{
  static GType disksrc_type = 0;

  if (!disksrc_type) {
    static const GTypeInfo disksrc_info = {
      sizeof(GstDiskSrcClass),      NULL,
      NULL,
      (GClassInitFunc)gst_disksrc_class_init,
      NULL,
      NULL,
      sizeof(GstDiskSrc),
      0,
      (GInstanceInitFunc)gst_disksrc_init,
    };
    disksrc_type = g_type_register_static (GST_TYPE_ELEMENT, "GstDiskSrc", &disksrc_info, 0);
  }
  return disksrc_type;
}

static void
gst_disksrc_class_init (GstDiskSrcClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_LOCATION,
    g_param_spec_string("location","location","location",
                        NULL,G_PARAM_READWRITE)); // CHECKME!
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BYTESPERREAD,
    g_param_spec_int("bytesperread","bytesperread","bytesperread",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_OFFSET,
    g_param_spec_long("offset","offset","offset",
                     G_MINLONG,G_MAXLONG,0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SIZE,
    g_param_spec_long("size","size","size",
                     G_MINLONG,G_MAXLONG,0,G_PARAM_READABLE)); // CHECKME

  gobject_class->set_property = gst_disksrc_set_property;
  gobject_class->get_property = gst_disksrc_get_property;

  gstelement_class->change_state = gst_disksrc_change_state;
}

static void
gst_disksrc_init (GstDiskSrc *disksrc)
{
//  GST_FLAG_SET (disksrc, GST_SRC_);

  disksrc->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_get_function (disksrc->srcpad,gst_disksrc_get);
  gst_pad_set_getregion_function (disksrc->srcpad,gst_disksrc_get_region);
  gst_element_add_pad (GST_ELEMENT (disksrc), disksrc->srcpad);

  disksrc->filename = NULL;
  disksrc->fd = 0;
  disksrc->size = 0;
  disksrc->map = NULL;
  disksrc->curoffset = 0;
  disksrc->bytes_per_read = 4096;
  disksrc->seq = 0;
  disksrc->new_seek = FALSE;
}


static void
gst_disksrc_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstDiskSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_DISKSRC (object));

  src = GST_DISKSRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      /* the element must be stopped or paused in order to do this */
      g_return_if_fail ((GST_STATE (src) < GST_STATE_PLAYING)
		      || (GST_STATE (src) == GST_STATE_PAUSED));

      if (src->filename) g_free (src->filename);
      /* clear the filename if we get a NULL (is that possible?) */
      if (g_value_get_string (value) == NULL) {
        gst_element_set_state (GST_ELEMENT (object), GST_STATE_NULL);
        src->filename = NULL;
      /* otherwise set the new filename */
      } else {
        src->filename = g_strdup (g_value_get_string (value));
      }
      if ((GST_STATE (src) == GST_STATE_PAUSED) && (src->filename != NULL))
      {
	      gst_disksrc_close_file(src);
	      gst_disksrc_open_file(src);
      }
      break;
    case ARG_BYTESPERREAD:
      src->bytes_per_read = g_value_get_int (value);
      break;
    case ARG_OFFSET:
      src->curoffset = g_value_get_long (value);
      src->new_seek = TRUE;
      break;
    default:
      break;
  }
}

static void
gst_disksrc_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstDiskSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_DISKSRC (object));

  src = GST_DISKSRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, src->filename);
      break;
    case ARG_BYTESPERREAD:
      g_value_set_int (value, src->bytes_per_read);
      break;
    case ARG_OFFSET:
      g_value_set_long (value, src->curoffset);
      break;
    case ARG_SIZE:
      g_value_set_long (value, src->size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * gst_disksrc_get:
 * @pad: #GstPad to push a buffer from
 *
 * Push a new buffer from the disksrc at the current offset.
 */
static GstBuffer *
gst_disksrc_get (GstPad *pad)
{
  GstDiskSrc *src;
  GstBuffer *buf;

  g_return_val_if_fail (pad != NULL, NULL);
  src = GST_DISKSRC (gst_pad_get_parent (pad));
  g_return_val_if_fail (GST_FLAG_IS_SET (src, GST_DISKSRC_OPEN), NULL);


  /* deal with EOF state */
  if (src->curoffset >= src->size) {
    GST_DEBUG (0,"map offset %ld >= size %ld --> eos\n", src->curoffset, src->size);
    gst_pad_event(pad,(void *)GST_EVENT_EOS);
    buf =  gst_buffer_new();
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_EOS);
    return buf;
  }

  /* create the buffer */
  // FIXME: should eventually use a bufferpool for this
  buf = gst_buffer_new ();

  g_return_val_if_fail (buf != NULL, NULL);

  /* simply set the buffer to point to the correct region of the file */
  GST_BUFFER_DATA (buf) = src->map + src->curoffset;
  GST_BUFFER_OFFSET (buf) = src->curoffset;
  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_DONTFREE);

  if ((src->curoffset + src->bytes_per_read) > src->size) {
    GST_BUFFER_SIZE (buf) = src->size - src->curoffset;
    // FIXME: set the buffer's EOF bit here
  } else
    GST_BUFFER_SIZE (buf) = src->bytes_per_read;

  GST_DEBUG (0,"map %p, offset %ld, size %d\n", src->map, src->curoffset, GST_BUFFER_SIZE (buf));

  //gst_util_dump_mem (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

  src->curoffset += GST_BUFFER_SIZE (buf);

  if (src->new_seek) {
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLUSH);
    GST_DEBUG (0,"new seek\n");
    src->new_seek = FALSE;
  }

  /* we're done, return the buffer */
  return buf;
}

/**
 * gst_disksrc_get_region:
 * @src: #GstSrc to push a buffer from
 * @offset: offset in file
 * @size: number of bytes
 *
 * Push a new buffer from the disksrc of given size at given offset.
 */
static GstBuffer *
gst_disksrc_get_region (GstPad *pad, GstRegionType type,guint64 offset,guint64 len)
{
  GstDiskSrc *src;
  GstBuffer *buf;

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (type == GST_REGION_OFFSET_LEN, NULL);

  src = GST_DISKSRC (gst_pad_get_parent (pad));

  g_return_val_if_fail (GST_IS_DISKSRC (src), NULL);
  g_return_val_if_fail (GST_FLAG_IS_SET (src, GST_DISKSRC_OPEN), NULL);

  /* deal with EOF state */
  if (offset >= src->size) {
    gst_pad_event (pad, (void*)GST_EVENT_EOS);
    GST_DEBUG (0,"map offset %lld >= size %ld --> eos\n", offset, src->size);
    //FIXME
    buf =  gst_buffer_new();
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_EOS);
    return buf;
  }

  /* create the buffer */
  // FIXME: should eventually use a bufferpool for this
  buf = gst_buffer_new ();
  g_return_val_if_fail (buf != NULL, NULL);

  /* simply set the buffer to point to the correct region of the file */
  GST_BUFFER_DATA (buf) = src->map + offset;
  GST_BUFFER_OFFSET (buf) = offset;
  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_DONTFREE);

  if ((offset + len) > src->size) {
    GST_BUFFER_SIZE (buf) = src->size - offset;
    // FIXME: set the buffer's EOF bit here
  } else
    GST_BUFFER_SIZE (buf) = len;

  GST_DEBUG (0,"map %p, offset %lld, size %d\n", src->map, offset, GST_BUFFER_SIZE (buf));

  /* we're done, return the buffer off now */
  return buf;
}


/* open the file and mmap it, necessary to go to READY state */
static gboolean 
gst_disksrc_open_file (GstDiskSrc *src)
{
  g_return_val_if_fail (!GST_FLAG_IS_SET (src ,GST_DISKSRC_OPEN), FALSE);

  /* open the file */
  src->fd = open (src->filename, O_RDONLY);
  if (src->fd < 0) {
    perror ("open");
    gst_element_error (GST_ELEMENT (src), g_strconcat("opening file \"", src->filename, "\"", NULL));
    return FALSE;
  } else {
    /* find the file length */
    src->size = lseek (src->fd, 0, SEEK_END);
    lseek (src->fd, 0, SEEK_SET);
    /* map the file into memory */
    src->map = mmap (NULL, src->size, PROT_READ, MAP_SHARED, src->fd, 0);
    /* collapse state if that failed */
    if (src->map == MAP_FAILED) {
      perror ("disksrc:mmap");
      close (src->fd);
      gst_element_error (GST_ELEMENT (src),"mmapping file");
      return FALSE;
    }
    madvise (src->map,src->size, 2);

    GST_FLAG_SET (src, GST_DISKSRC_OPEN);
    src->new_seek = TRUE;
  }
  return TRUE;
}

/* unmap and close the file */
static void
gst_disksrc_close_file (GstDiskSrc *src)
{
  g_return_if_fail (GST_FLAG_IS_SET (src, GST_DISKSRC_OPEN));

  /* unmap the file from memory */
  munmap (src->map, src->size);
  /* close the file */
  close (src->fd);

  /* zero out a lot of our state */
  src->fd = 0;
  src->size = 0;
  src->map = NULL;
  src->curoffset = 0;
  src->seq = 0;
  src->new_seek = FALSE;

  GST_FLAG_UNSET (src, GST_DISKSRC_OPEN);
}


static GstElementStateReturn
gst_disksrc_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_DISKSRC (element), GST_STATE_FAILURE);

  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_DISKSRC_OPEN))
      gst_disksrc_close_file (GST_DISKSRC (element));
  } else {
    if (!GST_FLAG_IS_SET (element, GST_DISKSRC_OPEN)) {
      if (!gst_disksrc_open_file (GST_DISKSRC (element)))
        return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
