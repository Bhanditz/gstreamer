/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * Filter:
 * Copyright (C) 2000 Donald A. Graft
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
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <gst/gst.h>
#include "gstpngenc.h"
#include <gst/video/video.h>

#define MAX_HEIGHT		4096


GstElementDetails gst_pngenc_details = {
  "PNG encoder",
  "Codec/Encoder/Image",
  "Encode a video frame to a .png image",
  "Jeremy SIMON <jsimon13@yahoo.fr>",
};


/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_SNAPSHOT	TRUE

enum
{
  ARG_0,
  ARG_SNAPSHOT,
  ARG_NEWMEDIA
};

static void gst_pngenc_base_init (gpointer g_class);
static void gst_pngenc_class_init (GstPngEncClass * klass);
static void gst_pngenc_init (GstPngEnc * pngenc);
static void gst_pngenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_pngenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_pngenc_chain (GstPad * pad, GstData * _data);

static GstStaticPadTemplate pngenc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/png, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], " "framerate = (double) [ 0.0, MAX ]")
    );

static GstStaticPadTemplate pngenc_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_RGB)
    );

static GstElementClass *parent_class = NULL;

static void
user_error_fn (png_structp png_ptr, png_const_charp error_msg)
{
  g_warning ("%s", error_msg);
}

static void
user_warning_fn (png_structp png_ptr, png_const_charp warning_msg)
{
  g_warning ("%s", warning_msg);
}


GType
gst_pngenc_get_type (void)
{
  static GType pngenc_type = 0;

  if (!pngenc_type) {
    static const GTypeInfo pngenc_info = {
      sizeof (GstPngEncClass),
      gst_pngenc_base_init,
      NULL,
      (GClassInitFunc) gst_pngenc_class_init,
      NULL,
      NULL,
      sizeof (GstPngEnc),
      0,
      (GInstanceInitFunc) gst_pngenc_init,
    };

    pngenc_type = g_type_register_static (GST_TYPE_ELEMENT, "GstPngEnc",
        &pngenc_info, 0);
  }
  return pngenc_type;
}

static void
gst_pngenc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template
      (element_class, gst_static_pad_template_get (&pngenc_sink_template));
  gst_element_class_add_pad_template
      (element_class, gst_static_pad_template_get (&pngenc_src_template));
  gst_element_class_set_details (element_class, &gst_pngenc_details);
}

static void
gst_pngenc_class_init (GstPngEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (gobject_class, ARG_SNAPSHOT,
      g_param_spec_boolean ("snapshot", "Snapshot",
          "Send EOS after encoding a frame, useful for snapshots",
          DEFAULT_SNAPSHOT, (GParamFlags) G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_NEWMEDIA,
      g_param_spec_boolean ("newmedia", "newmedia",
          "Send new media discontinuity after encoding each frame",
          FALSE, (GParamFlags) G_PARAM_READWRITE));

  gstelement_class->get_property = gst_pngenc_get_property;
  gstelement_class->set_property = gst_pngenc_set_property;
}


static GstPadLinkReturn
gst_pngenc_sinklink (GstPad * pad, const GstCaps * caps)
{
  GstPngEnc *pngenc;
  gdouble fps;
  GstStructure *structure;

  pngenc = GST_PNGENC (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &pngenc->width);
  gst_structure_get_int (structure, "height", &pngenc->height);
  gst_structure_get_double (structure, "framerate", &fps);
  gst_structure_get_int (structure, "bpp", &pngenc->bpp);

  caps = gst_caps_new_simple ("image/png",
      "framerate", G_TYPE_DOUBLE, fps,
      "width", G_TYPE_INT, pngenc->width,
      "height", G_TYPE_INT, pngenc->height, NULL);

  return gst_pad_try_set_caps (pngenc->srcpad, caps);
}

static void
gst_pngenc_init (GstPngEnc * pngenc)
{
  pngenc->sinkpad = gst_pad_new_from_template
      (gst_static_pad_template_get (&pngenc_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (pngenc), pngenc->sinkpad);

  pngenc->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (pngenc), pngenc->srcpad);

  gst_pad_set_chain_function (pngenc->sinkpad, gst_pngenc_chain);
  gst_pad_set_link_function (pngenc->sinkpad, gst_pngenc_sinklink);

  pngenc->png_struct_ptr = NULL;
  pngenc->png_info_ptr = NULL;

  pngenc->snapshot = DEFAULT_SNAPSHOT;
  pngenc->newmedia = FALSE;
}

static void
user_flush_data (png_structp png_ptr)
{
  GstPngEnc *pngenc;

  pngenc = (GstPngEnc *) png_get_io_ptr (png_ptr);

  gst_pad_push (pngenc->srcpad, GST_DATA (gst_event_new (GST_EVENT_FLUSH)));
}


static void
user_write_data (png_structp png_ptr, png_bytep data, png_uint_32 length)
{
  GstBuffer *buffer;
  GstPngEnc *pngenc;

  pngenc = (GstPngEnc *) png_get_io_ptr (png_ptr);

  buffer = gst_buffer_new ();
  GST_BUFFER_DATA (buffer) = g_memdup (data, length);
  GST_BUFFER_SIZE (buffer) = length;

  if (pngenc->buffer_out) {
    GstBuffer *merge;

    merge = gst_buffer_merge (pngenc->buffer_out, buffer);
    gst_buffer_unref (buffer);
    gst_buffer_unref (pngenc->buffer_out);
    pngenc->buffer_out = merge;
  } else
    pngenc->buffer_out = buffer;
}

static void
gst_pngenc_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstPngEnc *pngenc;
  gint row_index;
  png_byte *row_pointers[MAX_HEIGHT];

  pngenc = GST_PNGENC (gst_pad_get_parent (pad));

  pngenc->buffer_out = NULL;
  if (!GST_PAD_IS_USABLE (pngenc->srcpad)) {
    gst_buffer_unref (buf);
    return;
  }

  /* initialize png struct stuff */
  pngenc->png_struct_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING,
      (png_voidp) NULL, user_error_fn, user_warning_fn);
  /* FIXME: better error handling */
  if (pngenc->png_struct_ptr == NULL)
    g_warning ("Failed to initialize png structure");

  pngenc->png_info_ptr = png_create_info_struct (pngenc->png_struct_ptr);
  if (!pngenc->png_info_ptr) {
    png_destroy_read_struct (&(pngenc->png_struct_ptr), (png_infopp) NULL,
        (png_infopp) NULL);
  }

  /* non-0 return is from a longjmp inside of libpng */
  if (setjmp (pngenc->png_struct_ptr->jmpbuf) != 0) {
    GST_DEBUG ("returning from longjmp");
    png_destroy_write_struct (&pngenc->png_struct_ptr, &pngenc->png_info_ptr);
    return;
  }

  png_set_filter (pngenc->png_struct_ptr, 0,
      PNG_FILTER_NONE | PNG_FILTER_VALUE_NONE);
  png_set_compression_level (pngenc->png_struct_ptr, 9);

  png_set_IHDR (pngenc->png_struct_ptr,
      pngenc->png_info_ptr,
      pngenc->width,
      pngenc->height,
      8,
      (pngenc->bpp == 32) ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB,
      PNG_INTERLACE_NONE,
      PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

  png_set_write_fn (pngenc->png_struct_ptr, pngenc,
      (png_rw_ptr) user_write_data, user_flush_data);

  for (row_index = 0; row_index < pngenc->height; row_index++)
    row_pointers[row_index] = GST_BUFFER_DATA (buf) +
        (pngenc->width * row_index * pngenc->bpp / 8);

  png_write_info (pngenc->png_struct_ptr, pngenc->png_info_ptr);
  png_write_image (pngenc->png_struct_ptr, row_pointers);
  png_write_end (pngenc->png_struct_ptr, NULL);

  user_flush_data (pngenc->png_struct_ptr);

  png_destroy_info_struct (pngenc->png_struct_ptr, &pngenc->png_info_ptr);
  png_destroy_write_struct (&pngenc->png_struct_ptr, (png_infopp) NULL);
  gst_buffer_stamp (pngenc->buffer_out, buf);
  gst_buffer_unref (buf);

  gst_pad_push (pngenc->srcpad, GST_DATA (pngenc->buffer_out));

  if (pngenc->snapshot) {
    /* send EOS event, since a frame has been pushed out */
    GstEvent *event = gst_event_new (GST_EVENT_EOS);

    gst_pad_push (pngenc->srcpad, GST_DATA (event));
    gst_element_set_eos (GST_ELEMENT (pngenc));
  } else if (pngenc->newmedia) {
    /* send new media discont */
    GstEvent *newmedia_event;

    newmedia_event =
        gst_event_new_discontinuous (TRUE, GST_FORMAT_TIME, (gint64) 0,
        GST_FORMAT_UNDEFINED);
    gst_pad_push (pngenc->srcpad, GST_DATA (newmedia_event));
  }
}


static void
gst_pngenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstPngEnc *pngenc;

  pngenc = GST_PNGENC (object);

  switch (prop_id) {
    case ARG_SNAPSHOT:
      g_value_set_boolean (value, pngenc->snapshot);
      break;
    case ARG_NEWMEDIA:
      g_value_set_boolean (value, pngenc->newmedia);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_pngenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstPngEnc *pngenc;

  pngenc = GST_PNGENC (object);

  switch (prop_id) {
    case ARG_SNAPSHOT:
      pngenc->snapshot = g_value_get_boolean (value);
      break;
    case ARG_NEWMEDIA:
      pngenc->newmedia = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
