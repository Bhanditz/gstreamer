/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include "gstpngdec.h"

#include <string.h>
#include <gst/video/video.h>
#include <gst/gst-i18n-plugin.h>

static GstElementDetails gst_pngdec_details =
GST_ELEMENT_DETAILS ("PNG image decoder",
    "Codec/Decoder/Image",
    "Decode a png video frame to a raw image",
    "Wim Taymans <wim@fluendo.com>");

GST_DEBUG_CATEGORY (pngdec_debug);
#define GST_CAT_DEFAULT pngdec_debug

static void gst_pngdec_base_init (gpointer g_class);
static void gst_pngdec_class_init (GstPngDecClass * klass);
static void gst_pngdec_init (GstPngDec * pngdec);

static gboolean gst_pngdec_libpng_init (GstPngDec * pngdec);
static gboolean gst_pngdec_libpng_clear (GstPngDec * pngdec);

static GstStateChangeReturn gst_pngdec_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_pngdec_sink_activate_push (GstPad * sinkpad,
    gboolean active);
static gboolean gst_pngdec_sink_activate_pull (GstPad * sinkpad,
    gboolean active);
static gboolean gst_pngdec_sink_activate (GstPad * sinkpad);

static GstFlowReturn gst_pngdec_caps_create_and_set (GstPngDec * pngdec);

static void gst_pngdec_task (GstPad * pad);
static GstFlowReturn gst_pngdec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_pngdec_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_pngdec_sink_setcaps (GstPad * pad, GstCaps * caps);

static GstElementClass *parent_class = NULL;

GType
gst_pngdec_get_type (void)
{
  static GType pngdec_type = 0;

  if (!pngdec_type) {
    static const GTypeInfo pngdec_info = {
      sizeof (GstPngDecClass),
      gst_pngdec_base_init,
      NULL,
      (GClassInitFunc) gst_pngdec_class_init,
      NULL,
      NULL,
      sizeof (GstPngDec),
      0,
      (GInstanceInitFunc) gst_pngdec_init,
    };

    pngdec_type = g_type_register_static (GST_TYPE_ELEMENT, "GstPngDec",
        &pngdec_info, 0);
  }
  return pngdec_type;
}

static GstStaticPadTemplate gst_pngdec_src_pad_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_RGB)
    );

static GstStaticPadTemplate gst_pngdec_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/png")
    );

static void
gst_pngdec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_pngdec_src_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_pngdec_sink_pad_template));
  gst_element_class_set_details (element_class, &gst_pngdec_details);
}

static void
gst_pngdec_class_init (GstPngDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->change_state = gst_pngdec_change_state;

  GST_DEBUG_CATEGORY_INIT (pngdec_debug, "pngdec", 0, "PNG image decoder");
}

static void
gst_pngdec_init (GstPngDec * pngdec)
{
  pngdec->sinkpad =
      gst_pad_new_from_static_template (&gst_pngdec_sink_pad_template, "sink");
  gst_pad_set_activate_function (pngdec->sinkpad, gst_pngdec_sink_activate);
  gst_pad_set_activatepush_function (pngdec->sinkpad,
      gst_pngdec_sink_activate_push);
  gst_pad_set_activatepull_function (pngdec->sinkpad,
      gst_pngdec_sink_activate_pull);
  gst_pad_set_chain_function (pngdec->sinkpad, gst_pngdec_chain);
  gst_pad_set_event_function (pngdec->sinkpad, gst_pngdec_sink_event);
  gst_pad_set_setcaps_function (pngdec->sinkpad, gst_pngdec_sink_setcaps);
  gst_element_add_pad (GST_ELEMENT (pngdec), pngdec->sinkpad);

  pngdec->srcpad =
      gst_pad_new_from_static_template (&gst_pngdec_src_pad_template, "src");
  gst_pad_use_fixed_caps (pngdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (pngdec), pngdec->srcpad);

  pngdec->buffer_out = NULL;
  pngdec->png = NULL;
  pngdec->info = NULL;
  pngdec->endinfo = NULL;
  pngdec->setup = FALSE;

  pngdec->color_type = -1;
  pngdec->width = -1;
  pngdec->height = -1;
  pngdec->bpp = -1;
  pngdec->fps_n = 0;
  pngdec->fps_d = 1;

  pngdec->in_timestamp = GST_CLOCK_TIME_NONE;
  pngdec->in_duration = GST_CLOCK_TIME_NONE;
}

static void
user_error_fn (png_structp png_ptr, png_const_charp error_msg)
{
  GST_ERROR ("%s", error_msg);
}

static void
user_warning_fn (png_structp png_ptr, png_const_charp warning_msg)
{
  GST_WARNING ("%s", warning_msg);
}

static void
user_info_callback (png_structp png_ptr, png_infop info)
{
  GstPngDec *pngdec = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  size_t buffer_size;
  GstBuffer *buffer = NULL;

  pngdec = GST_PNGDEC (png_ptr->io_ptr);

  GST_LOG ("info ready");

  /* Generate the caps and configure */
  ret = gst_pngdec_caps_create_and_set (pngdec);
  if (ret != GST_FLOW_OK) {
    goto beach;
  }

  /* Allocate output buffer */
  pngdec->rowbytes = png_get_rowbytes (pngdec->png, pngdec->info);
  buffer_size = pngdec->height * GST_ROUND_UP_4 (pngdec->rowbytes);
  ret =
      gst_pad_alloc_buffer_and_set_caps (pngdec->srcpad, GST_BUFFER_OFFSET_NONE,
      buffer_size, GST_PAD_CAPS (pngdec->srcpad), &buffer);
  if (ret != GST_FLOW_OK) {
    goto beach;
  }

  pngdec->buffer_out = buffer;

beach:
  pngdec->ret = ret;
}

static void
user_endrow_callback (png_structp png_ptr, png_bytep new_row,
    png_uint_32 row_num, int pass)
{
  GstPngDec *pngdec = NULL;

  pngdec = GST_PNGDEC (png_ptr->io_ptr);

  /* FIXME: implement interlaced pictures */

  if (GST_IS_BUFFER (pngdec->buffer_out)) {
    size_t offset = row_num * GST_ROUND_UP_4 (pngdec->rowbytes);

    GST_LOG ("got row %d, copying in buffer %p at offset %d", row_num,
        pngdec->buffer_out, offset);
    memcpy (GST_BUFFER_DATA (pngdec->buffer_out) + offset, new_row,
        pngdec->rowbytes);
    pngdec->ret = GST_FLOW_OK;
  } else {
    GST_LOG ("we don't have any output buffer to write this row !");
    pngdec->ret = GST_FLOW_ERROR;
  }
}

static void
user_end_callback (png_structp png_ptr, png_infop info)
{
  GstPngDec *pngdec = NULL;

  pngdec = GST_PNGDEC (png_ptr->io_ptr);

  GST_LOG_OBJECT (pngdec, "and we are done reading this image");

  if (GST_CLOCK_TIME_IS_VALID (pngdec->in_timestamp))
    GST_BUFFER_TIMESTAMP (pngdec->buffer_out) = pngdec->in_timestamp;
  if (GST_CLOCK_TIME_IS_VALID (pngdec->in_duration))
    GST_BUFFER_DURATION (pngdec->buffer_out) = pngdec->in_duration;

  /* Push our buffer and then EOS if needed */
  GST_LOG_OBJECT (pngdec, "pushing buffer with ts=%" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (pngdec->buffer_out)));

  pngdec->ret = gst_pad_push (pngdec->srcpad, pngdec->buffer_out);
  pngdec->buffer_out = NULL;

  if (pngdec->framed) {
    /* Reset ourselves for the next frame */
    gst_pngdec_libpng_clear (pngdec);
    gst_pngdec_libpng_init (pngdec);
    GST_LOG_OBJECT (pngdec, "setting up callbacks for next frame");
    png_set_progressive_read_fn (pngdec->png, pngdec,
        user_info_callback, user_endrow_callback, user_end_callback);
  } else {
    GST_LOG_OBJECT (pngdec, "sending EOS");
    pngdec->ret = gst_pad_push_event (pngdec->srcpad, gst_event_new_eos ());
  }
}

static void
user_read_data (png_structp png_ptr, png_bytep data, png_size_t length)
{
  GstPngDec *pngdec;
  GstBuffer *buffer;
  GstFlowReturn ret = GST_FLOW_OK;

  pngdec = GST_PNGDEC (png_ptr->io_ptr);

  GST_LOG ("reading %d bytes of data at offset %d", length, pngdec->offset);

  ret = gst_pad_pull_range (pngdec->sinkpad, pngdec->offset, length, &buffer);
  if ((ret != GST_FLOW_OK) || (GST_BUFFER_SIZE (buffer) != length))
    goto pause;

  memcpy (data, GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer));

  gst_buffer_unref (buffer);

  pngdec->offset += length;

  return;

pause:
  GST_LOG_OBJECT (pngdec, "pausing task, reason %s", gst_flow_get_name (ret));
  gst_pad_pause_task (pngdec->sinkpad);
  if (GST_FLOW_IS_FATAL (ret)) {
    gst_pad_push_event (pngdec->srcpad, gst_event_new_eos ());
    GST_ELEMENT_ERROR (pngdec, STREAM, FAILED,
        (_("Internal data stream error.")),
        ("stream stopped, reason %s", gst_flow_get_name (ret)));
  }
}

static GstFlowReturn
gst_pngdec_caps_create_and_set (GstPngDec * pngdec)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstCaps *caps = NULL, *res = NULL;
  GstPadTemplate *templ = NULL;
  gint bpc = 0, color_type;
  png_uint_32 width, height;

  g_return_val_if_fail (GST_IS_PNGDEC (pngdec), GST_FLOW_ERROR);

  /* Get bits per channel */
  bpc = png_get_bit_depth (pngdec->png, pngdec->info);

  /* We don't handle 16 bits per color, strip down to 8 */
  if (bpc == 16) {
    GST_LOG_OBJECT (pngdec,
        "this is a 16 bits per channel PNG image, strip down to 8 bits");
    png_set_strip_16 (pngdec->png);
  }

  /* Get Color type */
  color_type = png_get_color_type (pngdec->png, pngdec->info);

  /* HACK: The doc states that it's RGBA but apparently it's not... */
  if (color_type == PNG_COLOR_TYPE_RGB_ALPHA)
    png_set_bgr (pngdec->png);

  /* Gray scale converted to RGB and upscaled to 8 bits */
  if ((color_type == PNG_COLOR_TYPE_GRAY_ALPHA) ||
      (color_type == PNG_COLOR_TYPE_GRAY)) {
    GST_LOG_OBJECT (pngdec, "converting grayscale png to RGB");
    png_set_gray_to_rgb (pngdec->png);
    if (bpc < 8) {              /* Convert to 8 bits */
      GST_LOG_OBJECT (pngdec, "converting grayscale image to 8 bits");
      png_set_gray_1_2_4_to_8 (pngdec->png);
    }
  }

  /* Palette converted to RGB */
  if (color_type == PNG_COLOR_TYPE_PALETTE) {
    GST_LOG_OBJECT (pngdec, "converting palette png to RGB");
    png_set_palette_to_rgb (pngdec->png);
  }

  /* Update the info structure */
  png_read_update_info (pngdec->png, pngdec->info);

  /* Get IHDR header again after transformation settings */

  png_get_IHDR (pngdec->png, pngdec->info, &width, &height,
      &bpc, &pngdec->color_type, NULL, NULL, NULL);

  pngdec->width = width;
  pngdec->height = height;

  GST_LOG_OBJECT (pngdec, "this is a %dx%d PNG image", pngdec->width,
      pngdec->height);

  switch (pngdec->color_type) {
    case PNG_COLOR_TYPE_RGB:
      GST_LOG_OBJECT (pngdec, "we have no alpha channel, depth is 24 bits");
      pngdec->bpp = 24;
      break;
    case PNG_COLOR_TYPE_RGB_ALPHA:
      GST_LOG_OBJECT (pngdec, "we have an alpha channel, depth is 32 bits");
      pngdec->bpp = 32;
      break;
    default:
      GST_ELEMENT_ERROR (pngdec, STREAM, NOT_IMPLEMENTED, (NULL),
          ("pngdec does not support this color type"));
      ret = GST_FLOW_ERROR;
      goto beach;
  }

  caps = gst_caps_new_simple ("video/x-raw-rgb",
      "width", G_TYPE_INT, pngdec->width,
      "height", G_TYPE_INT, pngdec->height,
      "bpp", G_TYPE_INT, pngdec->bpp,
      "framerate", GST_TYPE_FRACTION, pngdec->fps_n, pngdec->fps_d, NULL);

  templ = gst_static_pad_template_get (&gst_pngdec_src_pad_template);

  res = gst_caps_intersect (caps, gst_pad_template_get_caps (templ));

  gst_caps_unref (caps);
  gst_object_unref (templ);

  if (!gst_pad_set_caps (pngdec->srcpad, res)) {
    ret = GST_FLOW_ERROR;
  }

  GST_DEBUG_OBJECT (pngdec, "our caps %" GST_PTR_FORMAT, res);

  gst_caps_unref (res);

  /* Push a newsegment event */
  if (pngdec->need_newsegment) {
    gst_pad_push_event (pngdec->srcpad,
        gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, 0, -1, 0));
    pngdec->need_newsegment = FALSE;
  }

beach:
  return ret;
}

static void
gst_pngdec_task (GstPad * pad)
{
  GstPngDec *pngdec;
  GstBuffer *buffer = NULL;
  size_t buffer_size = 0;
  gint i = 0;
  png_bytep *rows, inp;
  png_uint_32 rowbytes;
  GstFlowReturn ret = GST_FLOW_OK;

  pngdec = GST_PNGDEC (GST_OBJECT_PARENT (pad));

  /* Let libpng come back here on error */
  if (setjmp (png_jmpbuf (pngdec->png))) {
    ret = GST_FLOW_ERROR;
    goto pause;
  }

  /* Set reading callback */
  png_set_read_fn (pngdec->png, pngdec, user_read_data);

  /* Read info */
  png_read_info (pngdec->png, pngdec->info);

  /* Generate the caps and configure */
  ret = gst_pngdec_caps_create_and_set (pngdec);
  if (ret != GST_FLOW_OK) {
    goto pause;
  }

  /* Allocate output buffer */
  rowbytes = png_get_rowbytes (pngdec->png, pngdec->info);
  buffer_size = pngdec->height * GST_ROUND_UP_4 (rowbytes);
  ret =
      gst_pad_alloc_buffer_and_set_caps (pngdec->srcpad, GST_BUFFER_OFFSET_NONE,
      buffer_size, GST_PAD_CAPS (pngdec->srcpad), &buffer);
  if (ret != GST_FLOW_OK) {
    goto pause;
  }

  rows = (png_bytep *) g_malloc (sizeof (png_bytep) * pngdec->height);

  inp = GST_BUFFER_DATA (buffer);

  for (i = 0; i < pngdec->height; i++) {
    rows[i] = inp;
    inp += GST_ROUND_UP_4 (rowbytes);
  }

  /* Read the actual picture */
  png_read_image (pngdec->png, rows);
  free (rows);

  /* Push the raw RGB frame */
  ret = gst_pad_push (pngdec->srcpad, buffer);
  if (ret != GST_FLOW_OK) {
    goto pause;
  }

  /* And we are done */
  gst_pad_pause_task (pngdec->sinkpad);
  gst_pad_push_event (pngdec->srcpad, gst_event_new_eos ());
  return;

pause:
  GST_LOG_OBJECT (pngdec, "pausing task, reason %s", gst_flow_get_name (ret));
  gst_pad_pause_task (pngdec->sinkpad);
  if (GST_FLOW_IS_FATAL (ret)) {
    gst_pad_push_event (pngdec->srcpad, gst_event_new_eos ());
    GST_ELEMENT_ERROR (pngdec, STREAM, FAILED,
        (_("Internal data stream error.")),
        ("stream stopped, reason %s", gst_flow_get_name (ret)));
  }
}

static GstFlowReturn
gst_pngdec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstPngDec *pngdec;
  GstFlowReturn ret = GST_FLOW_OK;

  pngdec = GST_PNGDEC (GST_OBJECT_PARENT (pad));

  GST_LOG_OBJECT (pngdec, "Got buffer, size=%u", GST_BUFFER_SIZE (buffer));

  if (!pngdec->setup) {
    GST_LOG ("we are not configured yet");
    ret = GST_FLOW_WRONG_STATE;
    goto beach;
  }

  /* Something is going wrong in our callbacks */
  if (pngdec->ret != GST_FLOW_OK) {
    ret = pngdec->ret;
    goto beach;
  }

  /* Let libpng come back here on error */
  if (setjmp (png_jmpbuf (pngdec->png))) {
    GST_WARNING ("error during decoding");
    ret = GST_FLOW_ERROR;
    goto beach;
  }

  pngdec->in_timestamp = GST_BUFFER_TIMESTAMP (buffer);
  pngdec->in_duration = GST_BUFFER_DURATION (buffer);

  /* Progressive loading of the PNG image */
  png_process_data (pngdec->png, pngdec->info, GST_BUFFER_DATA (buffer),
      GST_BUFFER_SIZE (buffer));

  /* And release the buffer */
  gst_buffer_unref (buffer);

beach:
  return ret;
}

static gboolean
gst_pngdec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *s;
  GstPngDec *pngdec;
  gint num, denom;

  pngdec = GST_PNGDEC (gst_pad_get_parent (pad));

  s = gst_caps_get_structure (caps, 0);
  if (gst_structure_get_fraction (s, "framerate", &num, &denom)) {
    GST_DEBUG_OBJECT (pngdec, "framed input");
    pngdec->framed = TRUE;
    pngdec->fps_n = num;
    pngdec->fps_d = denom;
  } else {
    pngdec->framed = FALSE;
    pngdec->fps_n = 0;
    pngdec->fps_d = 1;
  }

  gst_object_unref (pngdec);
  return TRUE;
}

static gboolean
gst_pngdec_sink_event (GstPad * pad, GstEvent * event)
{
  GstPngDec *pngdec;
  gboolean res;

  pngdec = GST_PNGDEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:{
      GstFormat fmt;

      gst_event_parse_new_segment (event, NULL, NULL, &fmt, NULL, NULL, NULL);
      GST_LOG_OBJECT (pngdec, "NEWSEGMENT (%s)", gst_format_get_name (fmt));
      if (fmt == GST_FORMAT_TIME) {
        pngdec->need_newsegment = FALSE;
        res = gst_pad_event_default (pad, event);
      } else {
        gst_event_unref (event);
        res = TRUE;
      }
      break;
    }
    case GST_EVENT_EOS:
      GST_LOG_OBJECT (pngdec, "EOS");
      gst_pngdec_libpng_clear (pngdec);
      res = gst_pad_event_default (pad, event);
      break;
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (pngdec);
  return res;
}


/* Clean up the libpng structures */
static gboolean
gst_pngdec_libpng_clear (GstPngDec * pngdec)
{
  png_infopp info = NULL, endinfo = NULL;

  g_return_val_if_fail (GST_IS_PNGDEC (pngdec), FALSE);

  GST_LOG ("cleaning up libpng structures");

  if (pngdec->info) {
    info = &pngdec->info;
  }

  if (pngdec->endinfo) {
    endinfo = &pngdec->endinfo;
  }

  if (pngdec->png) {
    png_destroy_read_struct (&(pngdec->png), info, endinfo);
    pngdec->png = NULL;
    pngdec->info = NULL;
    pngdec->endinfo = NULL;
  }

  pngdec->bpp = pngdec->color_type = pngdec->height = pngdec->width = -1;
  pngdec->offset = 0;
  pngdec->rowbytes = 0;
  pngdec->buffer_out = NULL;

  pngdec->setup = FALSE;

  pngdec->in_timestamp = GST_CLOCK_TIME_NONE;
  pngdec->in_duration = GST_CLOCK_TIME_NONE;

  return TRUE;
}

static gboolean
gst_pngdec_libpng_init (GstPngDec * pngdec)
{
  g_return_val_if_fail (GST_IS_PNGDEC (pngdec), FALSE);

  if (pngdec->setup) {
    goto beach;
  }

  /* initialize png struct stuff */
  pngdec->png = png_create_read_struct (PNG_LIBPNG_VER_STRING,
      (png_voidp) NULL, user_error_fn, user_warning_fn);

  if (pngdec->png == NULL) {
    GST_ELEMENT_ERROR (pngdec, LIBRARY, INIT, (NULL),
        ("Failed to initialize png structure"));
    goto beach;
  }

  pngdec->info = png_create_info_struct (pngdec->png);
  if (pngdec->info == NULL) {
    gst_pngdec_libpng_clear (pngdec);
    GST_ELEMENT_ERROR (pngdec, LIBRARY, INIT, (NULL),
        ("Failed to initialize info structure"));
    goto beach;
  }

  pngdec->endinfo = png_create_info_struct (pngdec->png);
  if (pngdec->endinfo == NULL) {
    gst_pngdec_libpng_clear (pngdec);
    GST_ELEMENT_ERROR (pngdec, LIBRARY, INIT, (NULL),
        ("Failed to initialize endinfo structure"));
    goto beach;
  }

  pngdec->setup = TRUE;

beach:
  return pngdec->setup;
}

static GstStateChangeReturn
gst_pngdec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstPngDec *pngdec;

  pngdec = GST_PNGDEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_pngdec_libpng_init (pngdec);
      pngdec->need_newsegment = TRUE;
      pngdec->framed = FALSE;
      break;
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_pngdec_libpng_clear (pngdec);
      break;
    default:
      break;
  }

  return ret;
}

/* this function gets called when we activate ourselves in push mode. */
static gboolean
gst_pngdec_sink_activate_push (GstPad * sinkpad, gboolean active)
{
  GstPngDec *pngdec;

  pngdec = GST_PNGDEC (GST_OBJECT_PARENT (sinkpad));

  pngdec->ret = GST_FLOW_OK;

  if (active) {
    /* Let libpng come back here on error */
    if (setjmp (png_jmpbuf (pngdec->png))) {
      GST_LOG ("failed setting up libpng jumb");
      gst_pngdec_libpng_clear (pngdec);
      return FALSE;
    }

    GST_LOG ("setting up progressive loading callbacks");
    png_set_progressive_read_fn (pngdec->png, pngdec,
        user_info_callback, user_endrow_callback, user_end_callback);
  }

  return TRUE;
}

/* this function gets called when we activate ourselves in pull mode.
 * We can perform  random access to the resource and we start a task
 * to start reading */
static gboolean
gst_pngdec_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  GstPngDec *pngdec;

  pngdec = GST_PNGDEC (GST_OBJECT_PARENT (sinkpad));

  if (active) {
    return gst_pad_start_task (sinkpad, (GstTaskFunction) gst_pngdec_task,
        sinkpad);
  } else {
    return gst_pad_stop_task (sinkpad);
  }
}

/* this function is called when the pad is activated and should start
 * processing data.
 *
 * We check if we can do random access to decide if we work push or
 * pull based.
 */
static gboolean
gst_pngdec_sink_activate (GstPad * sinkpad)
{
  if (gst_pad_check_pull_range (sinkpad)) {
    return gst_pad_activate_pull (sinkpad, TRUE);
  } else {
    return gst_pad_activate_push (sinkpad, TRUE);
  }
}
