/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2001 Bastien Nocera <hadess@hadess.net>
 *                    2002 Kristian Rietveld <kris@gtk.org>
 *                    2002,2003 Colin Walters <walters@gnu.org>
 *
 * gnomevfssrc.c:
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

/**
 * SECTION:element-gnomevfssrc
 * @short_description: Read from any GnomeVFS-supported location
 * @see_also: #GstFileSrc, #GstGnomeVFSSink
 *
 * <refsect2>
 * <para>
 * This plugin reads data from a local or remote location specified
 * by an URI. This location can be specified using any protocol supported by
 * the GnomeVFS library. Common protocols are 'file', 'http', 'ftp', or 'smb'.
 * </para>
 * <para>
 * Example pipeline:
 * <programlisting>
 * gst-launch -v gnomevfssrc location=file:///home/joe/foo.xyz ! fakesink
 * </programlisting>
 * The above pipeline will simply read a local file and do nothing with the
 * data read. Instead of gnomevfssrc, we could just as well have used the
 * filesrc element here.
 * </para>
 * <para>
 * Another example pipeline:
 * <programlisting>
 * gst-launch -v gnomevfssrc location=smb://othercomputer/foo.xyz ! filesink location=/home/joe/foo.xyz
 * </programlisting>
 * The above pipeline will copy a file from a remote host to the local file
 * system using the Samba protocol.
 * </para>
 * <para>
 * Yet another example pipeline:
 * <programlisting>
 * gst-launch -v gnomevfssrc location=http://music.foobar.com/demo.mp3 ! mad ! audioconvert ! audioresample ! alsasink
 * </programlisting>
 * The above pipeline will read and decode and play an mp3 file from a
 * web server using the http protocol.
 * </para>
 * </refsect2>
 *
 */


#define BROKEN_SIG 1
/*#undef BROKEN_SIG */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst/gst-i18n-plugin.h"

#include "gstgnomevfssrc.h"

#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

#include <gst/gst.h>
/* gnome-vfs.h doesn't include the following header, which we need: */
#include <libgnomevfs/gnome-vfs-standard-callbacks.h>

GST_DEBUG_CATEGORY_STATIC (gnomevfssrc_debug);
#define GST_CAT_DEFAULT gnomevfssrc_debug

static GstElementDetails gst_gnome_vfs_src_details =
GST_ELEMENT_DETAILS ("GnomeVFS Source",
    "Source/File",
    "Read from any GnomeVFS-supported file",
    "Bastien Nocera <hadess@hadess.net>\n"
    "Ronald S. Bultje <rbultje@ronald.bitfreak.net>");

static GStaticMutex count_lock = G_STATIC_MUTEX_INIT;
static gint ref_count = 0;
static gboolean vfs_owner = FALSE;


static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

enum
{
  ARG_0,
  ARG_HANDLE,
  ARG_LOCATION,
  ARG_IRADIO_MODE,
  ARG_IRADIO_NAME,
  ARG_IRADIO_GENRE,
  ARG_IRADIO_URL,
  ARG_IRADIO_TITLE
};

static void gst_gnome_vfs_src_base_init (gpointer g_class);
static void gst_gnome_vfs_src_class_init (GstGnomeVFSSrcClass * klass);
static void gst_gnome_vfs_src_init (GstGnomeVFSSrc * gnomevfssrc);
static void gst_gnome_vfs_src_finalize (GObject * object);
static void gst_gnome_vfs_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

static void gst_gnome_vfs_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gnome_vfs_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gnome_vfs_src_stop (GstBaseSrc * src);
static gboolean gst_gnome_vfs_src_start (GstBaseSrc * src);
static gboolean gst_gnome_vfs_src_is_seekable (GstBaseSrc * src);
static gboolean gst_gnome_vfs_src_check_get_range (GstBaseSrc * src);
static gboolean gst_gnome_vfs_src_get_size (GstBaseSrc * src, guint64 * size);
static GstFlowReturn gst_gnome_vfs_src_create (GstBaseSrc * basesrc,
    guint64 offset, guint size, GstBuffer ** buffer);

static GstElementClass *parent_class = NULL;

GType
gst_gnome_vfs_src_get_type (void)
{
  static GType gnomevfssrc_type = 0;

  if (!gnomevfssrc_type) {
    static const GTypeInfo gnomevfssrc_info = {
      sizeof (GstGnomeVFSSrcClass),
      gst_gnome_vfs_src_base_init,
      NULL,
      (GClassInitFunc) gst_gnome_vfs_src_class_init,
      NULL,
      NULL,
      sizeof (GstGnomeVFSSrc),
      0,
      (GInstanceInitFunc) gst_gnome_vfs_src_init,
    };
    static const GInterfaceInfo urihandler_info = {
      gst_gnome_vfs_src_uri_handler_init,
      NULL,
      NULL
    };

    gnomevfssrc_type =
        g_type_register_static (GST_TYPE_BASE_SRC,
        "GstGnomeVFSSrc", &gnomevfssrc_info, 0);
    g_type_add_interface_static (gnomevfssrc_type, GST_TYPE_URI_HANDLER,
        &urihandler_info);
  }
  return gnomevfssrc_type;
}

static void
gst_gnome_vfs_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_set_details (element_class, &gst_gnome_vfs_src_details);

  GST_DEBUG_CATEGORY_INIT (gnomevfssrc_debug, "gnomevfssrc", 0,
      "Gnome-VFS Source");
}

static void
gst_gnome_vfs_src_class_init (GstGnomeVFSSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbasesrc_class = GST_BASE_SRC_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_gnome_vfs_src_finalize;
  gobject_class->set_property = gst_gnome_vfs_src_set_property;
  gobject_class->get_property = gst_gnome_vfs_src_get_property;

  /* properties */
  gst_element_class_install_std_props (GST_ELEMENT_CLASS (klass),
      "location", ARG_LOCATION, G_PARAM_READWRITE, NULL);
  g_object_class_install_property (gobject_class,
      ARG_HANDLE,
      g_param_spec_boxed ("handle",
          "GnomeVFSHandle", "Handle for GnomeVFS",
          GST_TYPE_GNOME_VFS_HANDLE, G_PARAM_READWRITE));

  /* icecast stuff */
  g_object_class_install_property (gobject_class,
      ARG_IRADIO_MODE,
      g_param_spec_boolean ("iradio-mode",
          "iradio-mode",
          "Enable internet radio mode (extraction of shoutcast/icecast metadata)",
          FALSE, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class,
      ARG_IRADIO_NAME,
      g_param_spec_string ("iradio-name",
          "iradio-name", "Name of the stream", NULL, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class,
      ARG_IRADIO_GENRE,
      g_param_spec_string ("iradio-genre",
          "iradio-genre", "Genre of the stream", NULL, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class,
      ARG_IRADIO_URL,
      g_param_spec_string ("iradio-url",
          "iradio-url",
          "Homepage URL for radio stream", NULL, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class,
      ARG_IRADIO_TITLE,
      g_param_spec_string ("iradio-title",
          "iradio-title",
          "Name of currently playing song", NULL, G_PARAM_READABLE));

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_gnome_vfs_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_gnome_vfs_src_stop);
  gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_gnome_vfs_src_get_size);
  gstbasesrc_class->is_seekable =
      GST_DEBUG_FUNCPTR (gst_gnome_vfs_src_is_seekable);
  gstbasesrc_class->check_get_range =
      GST_DEBUG_FUNCPTR (gst_gnome_vfs_src_check_get_range);
  gstbasesrc_class->create = GST_DEBUG_FUNCPTR (gst_gnome_vfs_src_create);
}

static void
gst_gnome_vfs_src_init (GstGnomeVFSSrc * gnomevfssrc)
{
  gnomevfssrc->uri = NULL;
  gnomevfssrc->uri_name = NULL;
  gnomevfssrc->handle = NULL;
  gnomevfssrc->curoffset = 0;
  gnomevfssrc->seekable = FALSE;

  gnomevfssrc->icy_metaint = 0;
  gnomevfssrc->icy_caps = NULL;
  gnomevfssrc->iradio_mode = FALSE;
  gnomevfssrc->http_callbacks_pushed = FALSE;
  gnomevfssrc->iradio_name = NULL;
  gnomevfssrc->iradio_genre = NULL;
  gnomevfssrc->iradio_url = NULL;
  gnomevfssrc->iradio_title = NULL;

  g_static_mutex_lock (&count_lock);
  if (ref_count == 0) {
    /* gnome vfs engine init */
    if (gnome_vfs_initialized () == FALSE) {
      gnome_vfs_init ();
      vfs_owner = TRUE;
    }
  }
  ref_count++;
  g_static_mutex_unlock (&count_lock);
}

static void
gst_gnome_vfs_src_finalize (GObject * object)
{
  GstGnomeVFSSrc *src = GST_GNOME_VFS_SRC (object);

  g_static_mutex_lock (&count_lock);
  ref_count--;
  if (ref_count == 0 && vfs_owner) {
    if (gnome_vfs_initialized () == TRUE) {
      gnome_vfs_shutdown ();
    }
  }
  g_static_mutex_unlock (&count_lock);

  if (src->uri) {
    gnome_vfs_uri_unref (src->uri);
    src->uri = NULL;
  }

  g_free (src->uri_name);
  src->uri_name = NULL;

  g_free (src->iradio_name);
  src->iradio_name = NULL;

  g_free (src->iradio_genre);
  src->iradio_genre = NULL;

  g_free (src->iradio_url);
  src->iradio_url = NULL;

  g_free (src->iradio_title);
  src->iradio_title = NULL;

  if (src->icy_caps) {
    gst_caps_unref (src->icy_caps);
    src->icy_caps = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/*
 * URI interface support.
 */

static guint
gst_gnome_vfs_src_uri_get_type (void)
{
  return GST_URI_SRC;
}

static gchar **
gst_gnome_vfs_src_uri_get_protocols (void)
{
  static gchar **protocols = NULL;

  if (!protocols)
    protocols = gst_gnomevfs_get_supported_uris ();

  return protocols;
}

static const gchar *
gst_gnome_vfs_src_uri_get_uri (GstURIHandler * handler)
{
  GstGnomeVFSSrc *src = GST_GNOME_VFS_SRC (handler);

  return src->uri_name;
}

static gboolean
gst_gnome_vfs_src_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  GstGnomeVFSSrc *src = GST_GNOME_VFS_SRC (handler);

  if (GST_STATE (src) == GST_STATE_PLAYING ||
      GST_STATE (src) == GST_STATE_PAUSED)
    return FALSE;

  g_object_set (G_OBJECT (src), "location", uri, NULL);

  return TRUE;
}

static void
gst_gnome_vfs_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_gnome_vfs_src_uri_get_type;
  iface->get_protocols = gst_gnome_vfs_src_uri_get_protocols;
  iface->get_uri = gst_gnome_vfs_src_uri_get_uri;
  iface->set_uri = gst_gnome_vfs_src_uri_set_uri;
}

static void
gst_gnome_vfs_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGnomeVFSSrc *src;

  src = GST_GNOME_VFS_SRC (object);

  switch (prop_id) {
    case ARG_LOCATION:{
      const gchar *new_location;

      /* the element must be stopped or paused in order to do this */
      if (GST_STATE (src) == GST_STATE_PLAYING ||
          GST_STATE (src) == GST_STATE_PAUSED)
        break;

      if (src->uri) {
        gnome_vfs_uri_unref (src->uri);
        src->uri = NULL;
      }
      if (src->uri_name) {
        g_free (src->uri_name);
        src->uri_name = NULL;
      }

      new_location = g_value_get_string (value);
      if (new_location) {
        src->uri_name = gst_gnome_vfs_location_to_uri_string (new_location);
        src->uri = gnome_vfs_uri_new (src->uri_name);
      }
      break;
    }
    case ARG_HANDLE:
      if (GST_STATE (src) == GST_STATE_NULL ||
          GST_STATE (src) == GST_STATE_READY) {
        if (src->uri) {
          gnome_vfs_uri_unref (src->uri);
          src->uri = NULL;
        }
        if (src->uri_name) {
          g_free (src->uri_name);
          src->uri_name = NULL;
        }
        src->handle = g_value_get_boxed (value);
      }
      break;
    case ARG_IRADIO_MODE:
      src->iradio_mode = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gnome_vfs_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstGnomeVFSSrc *src;

  src = GST_GNOME_VFS_SRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, src->uri_name);
      break;
    case ARG_HANDLE:
      g_value_set_boxed (value, src->handle);
      break;
    case ARG_IRADIO_MODE:
      g_value_set_boolean (value, src->iradio_mode);
      break;
    case ARG_IRADIO_NAME:
      g_value_set_string (value, src->iradio_name);
      break;
    case ARG_IRADIO_GENRE:
      g_value_set_string (value, src->iradio_genre);
      break;
    case ARG_IRADIO_URL:
      g_value_set_string (value, src->iradio_url);
      break;
    case ARG_IRADIO_TITLE:
      g_value_set_string (value, src->iradio_title);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static char *
unicodify (const char *str, int len, ...)
{
  char *ret = NULL, *cset;
  va_list args;
  gsize bytes_read, bytes_written;

  if (g_utf8_validate (str, len, NULL))
    return g_strndup (str, len >= 0 ? len : strlen (str));

  va_start (args, len);
  while ((cset = va_arg (args, char *)) != NULL)
  {
    if (!strcmp (cset, "locale"))
      ret = g_locale_to_utf8 (str, len, &bytes_read, &bytes_written, NULL);
    else
      ret = g_convert (str, len, "UTF-8", cset,
          &bytes_read, &bytes_written, NULL);
    if (ret)
      break;
  }
  va_end (args);

  return ret;
}

static char *
gst_gnome_vfs_src_unicodify (const char *str)
{
  return unicodify (str, -1, "locale", "ISO-8859-1", NULL);
}

static void
gst_gnome_vfs_src_send_additional_headers_callback (gconstpointer in,
    gsize in_size, gpointer out, gsize out_size, gpointer callback_data)
{
  GstGnomeVFSSrc *src = GST_GNOME_VFS_SRC (callback_data);
  GnomeVFSModuleCallbackAdditionalHeadersOut *out_args =
      (GnomeVFSModuleCallbackAdditionalHeadersOut *) out;

  if (!src->iradio_mode)
    return;
  GST_DEBUG_OBJECT (src, "sending headers\n");

  out_args->headers = g_list_append (out_args->headers,
      g_strdup ("icy-metadata:1\r\n"));
}

static void
gst_gnome_vfs_src_received_headers_callback (gconstpointer in,
    gsize in_size, gpointer out, gsize out_size, gpointer callback_data)
{
  GList *i;
  gint icy_metaint;
  GstGnomeVFSSrc *src = GST_GNOME_VFS_SRC (callback_data);
  GnomeVFSModuleCallbackReceivedHeadersIn *in_args =
      (GnomeVFSModuleCallbackReceivedHeadersIn *) in;

  /* This is only used for internet radio stuff right now */
  if (!src->iradio_mode)
    return;

  for (i = in_args->headers; i; i = i->next) {
    char *data = (char *) i->data;
    char *key = data;
    char *value = strchr (data, ':');

    if (!value)
      continue;

    value++;
    g_strstrip (value);
    if (!strlen (value))
      continue;

    /* Icecast stuff */
    if (strncmp (data, "icy-metaint:", 12) == 0) {      /* ugh */
      if (sscanf (data + 12, "%d", &icy_metaint) == 1) {
        src->icy_metaint = icy_metaint;
        src->icy_caps = gst_caps_new_simple ("application/x-icy",
            "metadata-interval", G_TYPE_INT, src->icy_metaint, NULL);
        continue;
      }
    }

    if (!strncmp (data, "icy-", 4))
      key = data + 4;
    else
      continue;

    GST_DEBUG_OBJECT (src, "key: %s", key);
    if (!strncmp (key, "name", 4)) {
      g_free (src->iradio_name);
      src->iradio_name = gst_gnome_vfs_src_unicodify (value);
      if (src->iradio_name)
        g_object_notify (G_OBJECT (src), "iradio-name");
    } else if (!strncmp (key, "genre", 5)) {
      g_free (src->iradio_genre);
      src->iradio_genre = gst_gnome_vfs_src_unicodify (value);
      if (src->iradio_genre)
        g_object_notify (G_OBJECT (src), "iradio-genre");
    } else if (!strncmp (key, "url", 3)) {
      g_free (src->iradio_url);
      src->iradio_url = gst_gnome_vfs_src_unicodify (value);
      if (src->iradio_url)
        g_object_notify (G_OBJECT (src), "iradio-url");
    }
  }
}

static void
gst_gnome_vfs_src_push_callbacks (GstGnomeVFSSrc * src)
{
  if (src->http_callbacks_pushed)
    return;

  GST_DEBUG_OBJECT (src, "pushing callbacks");
  gnome_vfs_module_callback_push
      (GNOME_VFS_MODULE_CALLBACK_HTTP_SEND_ADDITIONAL_HEADERS,
      gst_gnome_vfs_src_send_additional_headers_callback, src, NULL);
  gnome_vfs_module_callback_push
      (GNOME_VFS_MODULE_CALLBACK_HTTP_RECEIVED_HEADERS,
      gst_gnome_vfs_src_received_headers_callback, src, NULL);

  src->http_callbacks_pushed = TRUE;
}

static void
gst_gnome_vfs_src_pop_callbacks (GstGnomeVFSSrc * src)
{
  if (!src->http_callbacks_pushed)
    return;

  GST_DEBUG_OBJECT (src, "popping callbacks");
  gnome_vfs_module_callback_pop
      (GNOME_VFS_MODULE_CALLBACK_HTTP_SEND_ADDITIONAL_HEADERS);
  gnome_vfs_module_callback_pop
      (GNOME_VFS_MODULE_CALLBACK_HTTP_RECEIVED_HEADERS);

  src->http_callbacks_pushed = FALSE;
}

/*
 * Read a new buffer from src->reqoffset, takes care of events
 * and seeking and such.
 */
static GstFlowReturn
gst_gnome_vfs_src_create (GstBaseSrc * basesrc, guint64 offset, guint size,
    GstBuffer ** buffer)
{
  GnomeVFSResult res;
  GstBuffer *buf;
  GnomeVFSFileSize readbytes;
  guint8 *data;
  GstGnomeVFSSrc *src;

  src = GST_GNOME_VFS_SRC (basesrc);

  GST_DEBUG ("now at %llu, reading %lld, size %u", src->curoffset, offset,
      size);

  /* seek if required */
  if (src->curoffset != offset) {
    GST_DEBUG ("need to seek");
    if (src->seekable) {
      GST_DEBUG ("seeking to %lld", offset);
      res = gnome_vfs_seek (src->handle, GNOME_VFS_SEEK_START, offset);
      if (res != GNOME_VFS_OK)
        goto seek_failed;
      src->curoffset = offset;
    } else {
      goto cannot_seek;
    }
  }

  buf = gst_buffer_new_and_alloc (size);

  if (src->icy_caps)
    gst_buffer_set_caps (buf, src->icy_caps);

  data = GST_BUFFER_DATA (buf);
  GST_BUFFER_OFFSET (buf) = src->curoffset;

  res = gnome_vfs_read (src->handle, data, size, &readbytes);

  if (res == GNOME_VFS_ERROR_EOF || (res == GNOME_VFS_OK && readbytes == 0))
    goto eos;

  GST_BUFFER_SIZE (buf) = readbytes;

  if (res != GNOME_VFS_OK)
    goto read_failed;

  src->curoffset += readbytes;

  /* we're done, return the buffer */
  *buffer = buf;

  return GST_FLOW_OK;

seek_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SEEK, (NULL),
        ("Failed to seek to requested position %" G_GINT64_FORMAT ": %s",
            offset, gnome_vfs_result_to_string (res)));
    return GST_FLOW_ERROR;
  }
cannot_seek:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SEEK, (NULL),
        ("Requested seek from %" G_GINT64_FORMAT " to %" G_GINT64_FORMAT
            "on non-seekable stream", src->curoffset, offset));
    return GST_FLOW_ERROR;
  }
read_failed:
  {
    gst_buffer_unref (buf);
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("Failed to read data: %s", gnome_vfs_result_to_string (res)));
    return GST_FLOW_ERROR;
  }
eos:
  {
    gst_buffer_unref (buf);
    GST_DEBUG_OBJECT (src, "Reading data gave EOS");
    return GST_FLOW_UNEXPECTED;
  }
}

static gboolean
gst_gnome_vfs_src_is_seekable (GstBaseSrc * basesrc)
{
  GstGnomeVFSSrc *src;

  src = GST_GNOME_VFS_SRC (basesrc);

  return src->seekable;
}

static gboolean
gst_gnome_vfs_src_check_get_range (GstBaseSrc * basesrc)
{
  GstGnomeVFSSrc *src;
  const gchar *protocol;

  src = GST_GNOME_VFS_SRC (basesrc);

  if (src->uri == NULL) {
    GST_WARNING_OBJECT (src, "no URI set yet");
    return FALSE;
  }

  if (gnome_vfs_uri_is_local (src->uri)) {
    GST_LOG_OBJECT (src, "local URI (%s), assuming random access is possible",
        GST_STR_NULL (src->uri_name));
    return TRUE;
  }

  /* blacklist certain protocols we know won't work getrange-based */
  protocol = gnome_vfs_uri_get_scheme (src->uri);
  if (protocol == NULL)
    goto undecided;

  if (strcmp (protocol, "http") == 0) {
    GST_LOG_OBJECT (src, "blacklisted protocol '%s', no random access possible"
        " (URI=%s)", protocol, GST_STR_NULL (src->uri_name));
    return FALSE;
  }

  /* fall through to undecided */

undecided:
  {
    /* don't know what to do, let the basesrc class decide for us */
    GST_LOG_OBJECT (src, "undecided about URI '%s', let base class handle it",
        GST_STR_NULL (src->uri_name));

    if (GST_BASE_SRC_CLASS (parent_class)->check_get_range)
      return GST_BASE_SRC_CLASS (parent_class)->check_get_range (basesrc);

    return FALSE;
  }
}

static gboolean
gst_gnome_vfs_src_get_size (GstBaseSrc * basesrc, guint64 * size)
{
  GstGnomeVFSSrc *src;

  src = GST_GNOME_VFS_SRC (basesrc);

  GST_DEBUG_OBJECT (src, "size %" G_GUINT64_FORMAT, src->size);

  if (src->size == (GnomeVFSFileSize) - 1)
    return FALSE;

  *size = src->size;

  return TRUE;
}

/* open the file, do stuff necessary to go to PAUSED state */
static gboolean
gst_gnome_vfs_src_start (GstBaseSrc * basesrc)
{
  GnomeVFSResult res;
  GnomeVFSFileInfo *info;
  GstGnomeVFSSrc *src;

  src = GST_GNOME_VFS_SRC (basesrc);

  gst_gnome_vfs_src_push_callbacks (src);

  if (src->uri != NULL) {
    /* this can block... */
    if ((res = gnome_vfs_open_uri (&src->handle, src->uri,
                GNOME_VFS_OPEN_READ)) != GNOME_VFS_OK)
      goto open_failed;
    src->own_handle = TRUE;
  } else if (!src->handle) {
    goto no_filename;
  } else {
    src->own_handle = FALSE;
  }

  src->size = (GnomeVFSFileSize) - 1;
  info = gnome_vfs_file_info_new ();
  if ((res = gnome_vfs_get_file_info_from_handle (src->handle,
              info, GNOME_VFS_FILE_INFO_DEFAULT)) == GNOME_VFS_OK) {
    if (info->valid_fields & GNOME_VFS_FILE_INFO_FIELDS_SIZE) {
      src->size = info->size;
      GST_DEBUG_OBJECT (src, "size: %llu bytes", src->size);
    } else
      GST_LOG_OBJECT (src, "filesize not known");
  } else {
    GST_WARNING_OBJECT (src, "getting info failed: %s",
        gnome_vfs_result_to_string (res));
  }
  gnome_vfs_file_info_unref (info);

  if (gnome_vfs_seek (src->handle, GNOME_VFS_SEEK_CURRENT, 0)
      == GNOME_VFS_OK) {
    src->seekable = TRUE;
  } else {
    src->seekable = FALSE;
  }

  return TRUE;

  /* ERRORS */
open_failed:
  {
    gchar *filename = gnome_vfs_uri_to_string (src->uri,
        GNOME_VFS_URI_HIDE_PASSWORD);

    gst_gnome_vfs_src_pop_callbacks (src);

    if (res == GNOME_VFS_ERROR_NOT_FOUND ||
        res == GNOME_VFS_ERROR_SERVICE_NOT_AVAILABLE) {
      GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (NULL),
          ("Could not open vfs file \"%s\" for reading: %s",
              filename, gnome_vfs_result_to_string (res)));
    } else {
      GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
          ("Could not open vfs file \"%s\" for reading: %s",
              filename, gnome_vfs_result_to_string (res)));
    }
    g_free (filename);
    return FALSE;
  }
no_filename:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL), ("No filename given"));
    return FALSE;
  }
}

static gboolean
gst_gnome_vfs_src_stop (GstBaseSrc * basesrc)
{
  GstGnomeVFSSrc *src;

  src = GST_GNOME_VFS_SRC (basesrc);

  gst_gnome_vfs_src_pop_callbacks (src);

  if (src->own_handle) {
    gnome_vfs_close (src->handle);
    src->handle = NULL;
  }
  src->size = (GnomeVFSFileSize) - 1;
  src->curoffset = 0;

  if (src->icy_caps) {
    gst_caps_unref (src->icy_caps);
    src->icy_caps = NULL;
  }

  return TRUE;
}
