/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2001 Bastien Nocera <hadess@hadess.net>
 *
 * gstgnomevfssink.c: 
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


#include <gst/gst.h>
#include <libgnomevfs/gnome-vfs.h>
#include <string.h>
#include <errno.h>

GstElementDetails gst_gnomevfssink_details;


#define GST_TYPE_GNOMEVFSSINK \
  (gst_gnomevfssink_get_type())
#define GST_GNOMEVFSSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GNOMEVFSSINK,GstGnomeVFSSink))
#define GST_GNOMEVFSSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GNOMEVFSSINK,GstGnomeVFSSinkClass))
#define GST_IS_GNOMEVFSSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GNOMEVFSSINK))
#define GST_IS_GNOMEVFSSINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GNOMEVFSSINK))

typedef struct _GstGnomeVFSSink GstGnomeVFSSink;
typedef struct _GstGnomeVFSSinkClass GstGnomeVFSSinkClass;

typedef enum {
  GST_GNOMEVFSSINK_OPEN             = GST_ELEMENT_FLAG_LAST,

  GST_GNOMEVFSSINK_FLAG_LAST 	= GST_ELEMENT_FLAG_LAST + 2,
} GstGnomeVFSSinkFlags;

struct _GstGnomeVFSSink {
  GstElement element;

  /* filename */
  gchar *filename;
  /* uri */
  GnomeVFSURI *uri;
  /* handle */
  GnomeVFSHandle *handle;
  /* wether to erase a file or not */
  gboolean erase;
};

struct _GstGnomeVFSSinkClass {
  GstElementClass parent_class;

  /* signals */
  void (*handoff) (GstElement *element,GstPad *pad);
  void (*erase_ask) (GstElement *element,GstPad *pad);
};

GType gst_gnomevfssink_get_type(void);

/* elementfactory information */
GstElementDetails gst_gnomevfssink_details = {
  "GnomeVFS Sink",
  "Sink/File",
  "LGPL",
  "Write stream to a GnomeVFS URI",
  VERSION,
  "Bastien Nocera <hadess@hadess.net>",
  "(C) 2001"
};


/* GnomeVFSSink signals and args */
enum {
  /* FILL ME */
  SIGNAL_HANDOFF,
  SIGNAL_ERASE_ASK,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_LOCATION,
  ARG_ERASE
};


static void	gst_gnomevfssink_class_init	(GstGnomeVFSSinkClass *klass);
static void	gst_gnomevfssink_init	(GstGnomeVFSSink *gnomevfssink);

static void	gst_gnomevfssink_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_gnomevfssink_get_property	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean gst_gnomevfssink_open_file 	(GstGnomeVFSSink *sink);
static void 	gst_gnomevfssink_close_file (GstGnomeVFSSink *sink);

static void	gst_gnomevfssink_chain	(GstPad *pad,GstBuffer *buf);

static GstElementStateReturn gst_gnomevfssink_change_state (GstElement *element);

static GstElementClass *parent_class = NULL;
static guint gst_gnomevfssink_signals[LAST_SIGNAL] = { 0 };

GType
gst_gnomevfssink_get_type (void) 
{
  static GType gnomevfssink_type = 0;

  if (!gnomevfssink_type) {
    static const GTypeInfo gnomevfssink_info = {
      sizeof(GstGnomeVFSSinkClass),      NULL,
      NULL,
      (GClassInitFunc)gst_gnomevfssink_class_init,
      NULL,
      NULL,
      sizeof(GstGnomeVFSSink),
      0,
      (GInstanceInitFunc)gst_gnomevfssink_init,
    };
    gnomevfssink_type = g_type_register_static (GST_TYPE_ELEMENT, "GstGnomeVFSSink", &gnomevfssink_info, 0);
  }
  return gnomevfssink_type;
}

static void
gst_gnomevfssink_class_init (GstGnomeVFSSinkClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);


  gst_element_class_install_std_props (
	  GST_ELEMENT_CLASS (klass),
	  "location",     ARG_LOCATION,     G_PARAM_READWRITE,
	  NULL);

  gst_gnomevfssink_signals[SIGNAL_HANDOFF] =
    g_signal_new ("handoff", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   G_STRUCT_OFFSET (GstGnomeVFSSinkClass, handoff), NULL, NULL,
                   g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  gst_gnomevfssink_signals[SIGNAL_ERASE_ASK] =
    g_signal_new ("erase-ask", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   G_STRUCT_OFFSET (GstGnomeVFSSinkClass, erase_ask), NULL, NULL,
                   g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);


  gobject_class->set_property = gst_gnomevfssink_set_property;
  gobject_class->get_property = gst_gnomevfssink_get_property;

  gstelement_class->change_state = gst_gnomevfssink_change_state;

  /* gnome vfs engine init */
  if (gnome_vfs_initialized() == FALSE)
	  gnome_vfs_init();
}

static void 
gst_gnomevfssink_init (GstGnomeVFSSink *gnomevfssink) 
{
  GstPad *pad;
  pad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (gnomevfssink), pad);
  gst_pad_set_chain_function (pad, gst_gnomevfssink_chain);

  gnomevfssink->filename = NULL;
  gnomevfssink->uri = NULL;
  gnomevfssink->handle = NULL;
  gnomevfssink->erase = FALSE;
}

static void
gst_gnomevfssink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstGnomeVFSSink *sink;

  /* it's not null if we got it, but it might not be ours */
  sink = GST_GNOMEVFSSINK (object);

  switch (prop_id) {
    case ARG_LOCATION:
      if (sink->filename)
	g_free (sink->filename);
      if (sink->uri)
	g_free (sink->uri);
      if (sink->handle)
	g_free (sink->handle);
      sink->filename = g_strdup (g_value_get_string (value));
      break;
    case ARG_ERASE:
      sink->erase = g_value_get_boolean (value);
      break;
    default:
      break;
  }
}

static void   
gst_gnomevfssink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstGnomeVFSSink *sink;
 
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_GNOMEVFSSINK (object));
 
  sink = GST_GNOMEVFSSINK (object);
  
  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, sink->filename);
      break;
    case ARG_ERASE:
      g_value_set_boolean (value, sink->erase);
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gnomevfssink_open_file (GstGnomeVFSSink *sink)
{
  GnomeVFSResult result;

  g_return_val_if_fail (!GST_FLAG_IS_SET (sink, GST_GNOMEVFSSINK_OPEN), FALSE);

  /* create the GnomeVFSURI from the url */
  sink->uri = gnome_vfs_uri_new(sink->filename);
  if (!sink->uri) {
    gst_element_error (GST_ELEMENT (sink), "opening file \"%s\" (%s)", sink->filename, strerror (errno));
    return FALSE;
  }

  /* open the file */
  result = gnome_vfs_create_uri(&(sink->handle), sink->uri,
      GNOME_VFS_OPEN_WRITE, sink->erase,
      GNOME_VFS_PERM_USER_READ | GNOME_VFS_PERM_USER_WRITE
        | GNOME_VFS_PERM_GROUP_READ);
  GST_DEBUG (0, "open: %s", gnome_vfs_result_to_string(result));
  if (result != GNOME_VFS_OK) {
    if (sink->erase == FALSE) {
      g_signal_emit (G_OBJECT (sink),
          gst_gnomevfssink_signals[SIGNAL_ERASE_ASK], 0,
          sink->erase);
    }
    gst_element_error (GST_ELEMENT (sink), "opening file \"%s\" (%s)", sink->filename, strerror (errno));
    return FALSE;
  } 

  GST_FLAG_SET (sink, GST_GNOMEVFSSINK_OPEN);

  return TRUE;
}

static void
gst_gnomevfssink_close_file (GstGnomeVFSSink *sink)
{
  GnomeVFSResult result;

  g_return_if_fail (GST_FLAG_IS_SET (sink, GST_GNOMEVFSSINK_OPEN));

  /* close the file */
  result = gnome_vfs_close(sink->handle);

  /* zero out the handle */
  if (sink->handle)
    g_free (sink->handle);

  if (result != GNOME_VFS_OK)
  {
    gst_element_error (GST_ELEMENT (sink), "closing file \"%s\" (%s)", sink->filename, strerror (errno));
  }
  else {
    GST_FLAG_UNSET (sink, GST_GNOMEVFSSINK_OPEN);
  }
}

/**
 * gst_gnomevfssink_chain:
 * @pad: the pad this gnomevfssink is connected to
 * @buf: the buffer that has to be absorbed
 *
 * take the buffer from the pad and write to file if it's open
 */
static void 
gst_gnomevfssink_chain (GstPad *pad, GstBuffer *buf) 
{
  GstGnomeVFSSink *sink;
  GnomeVFSResult result;
  GnomeVFSFileSize bytes_written;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  sink = GST_GNOMEVFSSINK (gst_pad_get_parent (pad));

  if (GST_FLAG_IS_SET (sink, GST_GNOMEVFSSINK_OPEN))
  {
    result = gnome_vfs_write(sink->handle, GST_BUFFER_DATA(buf), GST_BUFFER_SIZE (buf), &bytes_written);
    GST_DEBUG (0, "write: %s, written_bytes: %Lu", gnome_vfs_result_to_string(result), bytes_written);
    if (bytes_written < GST_BUFFER_SIZE (buf))
    {
      printf ("gnomevfssink : Warning : %d bytes should be written, only %Lu bytes written\n",
      		  GST_BUFFER_SIZE (buf), bytes_written);
    }
  }
  gst_buffer_unref (buf);

  g_signal_emit (G_OBJECT (sink), gst_gnomevfssink_signals[SIGNAL_HANDOFF], 0,
	                      sink);
}

static GstElementStateReturn
gst_gnomevfssink_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_GNOMEVFSSINK (element), GST_STATE_FAILURE);

  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_GNOMEVFSSINK_OPEN))
      gst_gnomevfssink_close_file (GST_GNOMEVFSSINK (element));
  } else {
    if (!GST_FLAG_IS_SET (element, GST_GNOMEVFSSINK_OPEN)) {
      if (!gst_gnomevfssink_open_file (GST_GNOMEVFSSINK (element)))
        return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{ 
  GstElementFactory *factory;

  /* create an elementfactory for the aasink element */
  factory = gst_element_factory_new("gnomevfssink", GST_TYPE_GNOMEVFSSINK,
		  &gst_gnomevfssink_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gnomevfssink",
  plugin_init
};
