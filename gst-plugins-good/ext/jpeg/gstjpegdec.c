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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include "gstjpegdec.h"
#include <gst/video/video.h>
#include "gst/gst-i18n-plugin.h"
#include <jerror.h>

GstElementDetails gst_jpeg_dec_details = {
  "JPEG image decoder",
  "Codec/Decoder/Image",
  "Decode images from JPEG format",
  "Wim Taymans <wim.taymans@tvd.be>",
};

#define MIN_WIDTH  16
#define MAX_WIDTH  4096
#define MIN_HEIGHT 16
#define MAX_HEIGHT 4096

static GstStaticPadTemplate gst_jpeg_dec_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate gst_jpeg_dec_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg, "
        "width = (int) [ " G_STRINGIFY (MIN_WIDTH) ", " G_STRINGIFY (MAX_WIDTH)
        " ], " "height = (int) [ " G_STRINGIFY (MIN_HEIGHT) ", "
        G_STRINGIFY (MAX_HEIGHT) " ], " "framerate = (double) [ 1, MAX ]")
    );

GST_DEBUG_CATEGORY (jpeg_dec_debug);
#define GST_CAT_DEFAULT jpeg_dec_debug

/* These macros are adapted from videotestsrc.c 
 *  and/or gst-plugins/gst/games/gstvideoimage.c */
#define ROUND_UP_2(x)  (((x)+1)&~1)
#define ROUND_UP_4(x)  (((x)+3)&~3)
#define ROUND_UP_8(x)  (((x)+7)&~7)

/* I420 */
#define I420_Y_ROWSTRIDE(width) (ROUND_UP_4(width))
#define I420_U_ROWSTRIDE(width) (ROUND_UP_8(width)/2)
#define I420_V_ROWSTRIDE(width) ((ROUND_UP_8(I420_Y_ROWSTRIDE(width)))/2)

#define I420_Y_OFFSET(w,h) (0)
#define I420_U_OFFSET(w,h) (I420_Y_OFFSET(w,h)+(I420_Y_ROWSTRIDE(w)*ROUND_UP_2(h)))
#define I420_V_OFFSET(w,h) (I420_U_OFFSET(w,h)+(I420_U_ROWSTRIDE(w)*ROUND_UP_2(h)/2))

#define I420_SIZE(w,h)     (I420_V_OFFSET(w,h)+(I420_V_ROWSTRIDE(w)*ROUND_UP_2(h)/2))

static GstElementClass *parent_class;   /* NULL */

static void gst_jpeg_dec_base_init (gpointer g_class);
static void gst_jpeg_dec_class_init (GstJpegDecClass * klass);
static void gst_jpeg_dec_init (GstJpegDec * jpegdec);

static GstFlowReturn gst_jpeg_dec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_jpeg_dec_setcaps (GstPad * pad, GstCaps * caps);
static GstElementStateReturn gst_jpeg_dec_change_state (GstElement * element);

GType
gst_jpeg_dec_get_type (void)
{
  static GType type = 0;

  if (!type) {
    static const GTypeInfo jpeg_dec_info = {
      sizeof (GstJpegDecClass),
      (GBaseInitFunc) gst_jpeg_dec_base_init,
      NULL,
      (GClassInitFunc) gst_jpeg_dec_class_init,
      NULL,
      NULL,
      sizeof (GstJpegDec),
      0,
      (GInstanceInitFunc) gst_jpeg_dec_init,
    };

    type = g_type_register_static (GST_TYPE_ELEMENT, "GstJpegDec",
        &jpeg_dec_info, 0);
  }
  return type;
}

static void
gst_jpeg_dec_finalize (GObject * object)
{
  GstJpegDec *dec = GST_JPEG_DEC (object);

  jpeg_destroy_decompress (&dec->cinfo);

  if (dec->tempbuf)
    gst_buffer_unref (dec->tempbuf);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_jpeg_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_jpeg_dec_src_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_jpeg_dec_sink_pad_template));
  gst_element_class_set_details (element_class, &gst_jpeg_dec_details);
}

static void
gst_jpeg_dec_class_init (GstJpegDecClass * klass)
{
  GstElementClass *gstelement_class;
  GObjectClass *gobject_class;

  gstelement_class = (GstElementClass *) klass;
  gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_jpeg_dec_finalize;
  gstelement_class->change_state = gst_jpeg_dec_change_state;

  GST_DEBUG_CATEGORY_INIT (jpeg_dec_debug, "jpegdec", 0, "JPEG decoder");
}

static gboolean
gst_jpeg_dec_fill_input_buffer (j_decompress_ptr cinfo)
{
/*
  struct GstJpegDecSourceMgr *src_mgr;
  GstJpegDec *dec;

  src_mgr = (struct GstJpegDecSourceMgr*) &cinfo->src;
  dec = GST_JPEG_DEC (src_mgr->dec);
*/
  GST_DEBUG ("fill_input_buffer");
/*
  g_return_val_if_fail (dec != NULL, TRUE);

  src_mgr->pub.next_input_byte = GST_BUFFER_DATA (dec->tempbuf);
  src_mgr->pub.bytes_in_buffer = GST_BUFFER_SIZE (dec->tempbuf);
*/
  return TRUE;
}

static void
gst_jpeg_dec_init_source (j_decompress_ptr cinfo)
{
  GST_DEBUG ("init_source");
}


static void
gst_jpeg_dec_skip_input_data (j_decompress_ptr cinfo, glong num_bytes)
{
  GST_DEBUG ("skip_input_data: %ld bytes", num_bytes);

  if (num_bytes > 0 && cinfo->src->bytes_in_buffer >= num_bytes) {
    cinfo->src->next_input_byte += (size_t) num_bytes;
    cinfo->src->bytes_in_buffer -= (size_t) num_bytes;
  }
}

static gboolean
gst_jpeg_dec_resync_to_restart (j_decompress_ptr cinfo, gint desired)
{
  GST_DEBUG ("resync_to_start");
  return TRUE;
}

static void
gst_jpeg_dec_term_source (j_decompress_ptr cinfo)
{
  GST_DEBUG ("term_source");
  return;
}

METHODDEF (void)
    gst_jpeg_dec_my_output_message (j_common_ptr cinfo)
{
  return;                       /* do nothing */
}

METHODDEF (void)
    gst_jpeg_dec_my_emit_message (j_common_ptr cinfo, int msg_level)
{
  /* GST_DEBUG ("emit_message: msg_level = %d", msg_level); */
  return;
}

METHODDEF (void)
    gst_jpeg_dec_my_error_exit (j_common_ptr cinfo)
{
  struct GstJpegDecErrorMgr *err_mgr = (struct GstJpegDecErrorMgr *) cinfo->err;

  (*cinfo->err->output_message) (cinfo);
  longjmp (err_mgr->setjmp_buffer, 1);
}

static void
gst_jpeg_dec_init (GstJpegDec * dec)
{
  GST_DEBUG ("initializing");

  /* create the sink and src pads */
  dec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_jpeg_dec_sink_pad_template), "sink");
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);
  gst_pad_set_setcaps_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_jpeg_dec_setcaps));
  gst_pad_set_chain_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_jpeg_dec_chain));

  dec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_jpeg_dec_src_pad_template), "src");
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  dec->next_ts = 0;
  dec->fps = 1.0;

  /* reset the initial video state */
  dec->width = -1;
  dec->height = -1;

  dec->line[0] = NULL;
  dec->line[1] = NULL;
  dec->line[2] = NULL;

  /* setup jpeglib */
  memset (&dec->cinfo, 0, sizeof (dec->cinfo));
  memset (&dec->jerr, 0, sizeof (dec->jerr));
  dec->cinfo.err = jpeg_std_error (&dec->jerr.pub);
  dec->jerr.pub.output_message = gst_jpeg_dec_my_output_message;
  dec->jerr.pub.emit_message = gst_jpeg_dec_my_emit_message;
  dec->jerr.pub.error_exit = gst_jpeg_dec_my_error_exit;

  jpeg_create_decompress (&dec->cinfo);

  dec->cinfo.src = (struct jpeg_source_mgr *) &dec->jsrc;
  dec->cinfo.src->init_source = gst_jpeg_dec_init_source;
  dec->cinfo.src->fill_input_buffer = gst_jpeg_dec_fill_input_buffer;
  dec->cinfo.src->skip_input_data = gst_jpeg_dec_skip_input_data;
  dec->cinfo.src->resync_to_restart = gst_jpeg_dec_resync_to_restart;
  dec->cinfo.src->term_source = gst_jpeg_dec_term_source;
  dec->jsrc.dec = dec;
}

static inline gboolean
is_jpeg_start_marker (const guint8 * data)
{
  return (data[0] == 0xff && data[1] == 0xd8);
}

static inline gboolean
is_jpeg_end_marker (const guint8 * data)
{
  return (data[0] == 0xff && data[1] == 0xd9);
}

static gboolean
gst_jpeg_dec_find_jpeg_header (GstJpegDec * dec)
{
  const guint8 *data;
  guint size;

  data = GST_BUFFER_DATA (dec->tempbuf);
  size = GST_BUFFER_SIZE (dec->tempbuf);

  g_return_val_if_fail (size >= 2, FALSE);

  while (!is_jpeg_start_marker (data) || data[2] != 0xff) {
    const guint8 *marker;
    GstBuffer *tmp;
    guint off;

    marker = memchr (data + 1, 0xff, size - 1 - 2);
    if (marker == NULL) {
      off = size - 1;           /* keep last byte */
    } else {
      off = marker - data;
    }

    tmp = gst_buffer_create_sub (dec->tempbuf, off, size - off);
    gst_buffer_unref (dec->tempbuf);
    dec->tempbuf = tmp;

    data = GST_BUFFER_DATA (dec->tempbuf);
    size = GST_BUFFER_SIZE (dec->tempbuf);

    if (size < 2)
      return FALSE;             /* wait for more data */
  }

  return TRUE;                  /* got header */
}

static gboolean
gst_jpeg_dec_ensure_header (GstJpegDec * dec)
{
  g_return_val_if_fail (dec->tempbuf != NULL, FALSE);

check_header:

  /* we need at least a start marker (0xff 0xd8)
   *   and an end marker (0xff 0xd9) */
  if (GST_BUFFER_SIZE (dec->tempbuf) <= 4) {
    GST_DEBUG ("Not enough data");
    return FALSE;               /* we need more data */
  }

  if (!is_jpeg_start_marker (GST_BUFFER_DATA (dec->tempbuf))) {
    GST_DEBUG ("Not a JPEG header, resyncing to header...");
    if (!gst_jpeg_dec_find_jpeg_header (dec)) {
      GST_DEBUG ("No JPEG header in current buffer");
      return FALSE;             /* we need more data */
    }
    GST_DEBUG ("Found JPEG header");
    goto check_header;          /* buffer might have changed */
  }

  return TRUE;
}

#if 0
static gboolean
gst_jpeg_dec_have_end_marker (GstJpegDec * dec)
{
  guint8 *data = GST_BUFFER_DATA (dec->tempbuf);
  guint size = GST_BUFFER_SIZE (dec->tempbuf);

  return (size > 2 && data && is_jpeg_end_marker (data + size - 2));
}
#endif

static inline gboolean
gst_jpeg_dec_parse_tag_has_entropy_segment (guint8 tag)
{
  if (tag == 0xda || (tag >= 0xd0 && tag <= 0xd7))
    return TRUE;
  return FALSE;
}

/* returns image length in bytes if parsed 
 * successfully, otherwise 0 */
static guint
gst_jpeg_dec_parse_image_data (GstJpegDec * dec)
{
  guint8 *start, *data, *end;
  guint size;

  size = GST_BUFFER_SIZE (dec->tempbuf);
  start = GST_BUFFER_DATA (dec->tempbuf);
  end = start + size;
  data = start;

  g_return_val_if_fail (is_jpeg_start_marker (data), 0);

  GST_DEBUG ("Parsing jpeg image data (%u bytes)", size);

  /* skip start marker */
  data += 2;

  while (1) {
    guint frame_len;

    /* enough bytes left for EOI marker? (we need 0xff 0xNN, thus end-1) */
    if (data >= end - 1) {
      GST_DEBUG ("at end of input and no EOI marker found, need more data");
      return 0;
    }

    if (is_jpeg_end_marker (data)) {
      GST_DEBUG ("0x%08x: end marker", data - start);
      goto found_eoi;
    }

    /* do we need to resync? */
    if (*data != 0xff) {
      GST_DEBUG ("Lost sync at 0x%08x, resyncing", data - start);
      /* at the very least we expect 0xff 0xNN, thus end-1 */
      while (*data != 0xff && data < end - 1)
        ++data;
      if (is_jpeg_end_marker (data)) {
        GST_DEBUG ("resynced to end marker");
        goto found_eoi;
      }
      /* we need 0xFF 0xNN 0xLL 0xLL */
      if (data >= end - 1 - 2) {
        GST_DEBUG ("at end of input, without new sync, need more data");
        return 0;
      }
      /* check if we will still be in sync if we interpret
       * this as a sync point and skip this frame */
      frame_len = GST_READ_UINT16_BE (data + 2);
      GST_DEBUG ("possible sync at 0x%08x, frame_len=%u", data - start,
          frame_len);
      if (data + 2 + frame_len >= end - 1 || data[2 + frame_len] != 0xff) {
        /* ignore and continue resyncing until we hit the end
         * of our data or find a sync point that looks okay */
        ++data;
        continue;
      }
      GST_DEBUG ("found sync at 0x%08x", data - size);
    }
    while (*data == 0xff)
      ++data;
    if (data + 2 >= end)
      return 0;
    if (*data >= 0xd0 && *data <= 0xd7)
      frame_len = 0;
    else
      frame_len = GST_READ_UINT16_BE (data + 1);
    GST_DEBUG ("0x%08x: tag %02x, frame_len=%u", data - start - 1, *data,
        frame_len);
    /* the frame length includes the 2 bytes for the length; here we want at
     * least 2 more bytes at the end for an end marker, thus end-2 */
    if (data + 1 + frame_len >= end - 2) {
      /* theoretically we could have lost sync and not really need more
       * data, but that's just tough luck and a broken image then */
      GST_DEBUG ("at end of input and no EOI marker found, need more data");
      return 0;
    }
    if (gst_jpeg_dec_parse_tag_has_entropy_segment (*data)) {
      guint8 *d2 = data + 1 + frame_len;
      guint eseglen = 0;

      GST_DEBUG ("0x%08x: finding entropy segment length", data - start - 1);
      while (1) {
        if (d2[eseglen] == 0xff && d2[eseglen + 1] != 0x00)
          break;
        if (d2 + eseglen >= end - 1)
          return 0;             /* need more data */
        ++eseglen;
      }
      frame_len += eseglen;
      GST_DEBUG ("entropy segment length=%u => frame_len=%u", eseglen,
          frame_len);
    }
    data += 1 + frame_len;
  }

found_eoi:
  /* data is assumed to point to the 0xff sync point of the
   *  EOI marker (so there is one more byte after that) */
  g_assert (is_jpeg_end_marker (data));
  return ((data + 1) - start + 1);
}

/* shamelessly ripped from jpegutils.c in mjpegtools */
static void
add_huff_table (j_decompress_ptr dinfo,
    JHUFF_TBL ** htblptr, const UINT8 * bits, const UINT8 * val)
/* Define a Huffman table */
{
  int nsymbols, len;

  if (*htblptr == NULL)
    *htblptr = jpeg_alloc_huff_table ((j_common_ptr) dinfo);

  /* Copy the number-of-symbols-of-each-code-length counts */
  memcpy ((*htblptr)->bits, bits, sizeof ((*htblptr)->bits));

  /* Validate the counts.  We do this here mainly so we can copy the right
   * number of symbols from the val[] array, without risking marching off
   * the end of memory.  jchuff.c will do a more thorough test later.
   */
  nsymbols = 0;
  for (len = 1; len <= 16; len++)
    nsymbols += bits[len];
  if (nsymbols < 1 || nsymbols > 256)
    g_error ("jpegutils.c:  add_huff_table failed badly. ");

  memcpy ((*htblptr)->huffval, val, nsymbols * sizeof (UINT8));
}



static void
std_huff_tables (j_decompress_ptr dinfo)
/* Set up the standard Huffman tables (cf. JPEG standard section K.3) */
/* IMPORTANT: these are only valid for 8-bit data precision! */
{
  static const UINT8 bits_dc_luminance[17] =
      { /* 0-base */ 0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
  static const UINT8 val_dc_luminance[] =
      { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

  static const UINT8 bits_dc_chrominance[17] =
      { /* 0-base */ 0, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };
  static const UINT8 val_dc_chrominance[] =
      { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

  static const UINT8 bits_ac_luminance[17] =
      { /* 0-base */ 0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d };
  static const UINT8 val_ac_luminance[] =
      { 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
    0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
    0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
    0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
    0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
    0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
    0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
    0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
    0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa
  };

  static const UINT8 bits_ac_chrominance[17] =
      { /* 0-base */ 0, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 };
  static const UINT8 val_ac_chrominance[] =
      { 0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
    0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
    0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
    0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
    0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
    0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
    0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
    0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
    0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
    0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa
  };

  add_huff_table (dinfo, &dinfo->dc_huff_tbl_ptrs[0],
      bits_dc_luminance, val_dc_luminance);
  add_huff_table (dinfo, &dinfo->ac_huff_tbl_ptrs[0],
      bits_ac_luminance, val_ac_luminance);
  add_huff_table (dinfo, &dinfo->dc_huff_tbl_ptrs[1],
      bits_dc_chrominance, val_dc_chrominance);
  add_huff_table (dinfo, &dinfo->ac_huff_tbl_ptrs[1],
      bits_ac_chrominance, val_ac_chrominance);
}



static void
guarantee_huff_tables (j_decompress_ptr dinfo)
{
  if ((dinfo->dc_huff_tbl_ptrs[0] == NULL) &&
      (dinfo->dc_huff_tbl_ptrs[1] == NULL) &&
      (dinfo->ac_huff_tbl_ptrs[0] == NULL) &&
      (dinfo->ac_huff_tbl_ptrs[1] == NULL)) {
    GST_DEBUG ("Generating standard Huffman tables for this frame.");
    std_huff_tables (dinfo);
  }
}

static gboolean
gst_jpeg_dec_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *s;
  GstJpegDec *dec;
  gdouble fps;
  gint width, height;

  dec = GST_JPEG_DEC (GST_OBJECT_PARENT (pad));
  s = gst_caps_get_structure (caps, 0);

  if (gst_structure_get_double (s, "framerate", &fps))
    dec->fps = fps;

  if (gst_structure_get_int (s, "width", &width)
      && gst_structure_get_int (s, "height", &height)) {
    dec->width = width;
    dec->height = height;
  }

  return TRUE;
}

static GstFlowReturn
gst_jpeg_dec_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn ret;
  GstJpegDec *dec;
  GstBuffer *outbuf;
  GstCaps *caps;
  gulong size, outsize;
  guchar *data, *outdata;
  guchar *base[3];
  guint img_len;
  gint width, height;
  gint r_h, r_v;
  gint i, j, k;

  dec = GST_JPEG_DEC (GST_OBJECT_PARENT (pad));

  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buf))) {
    dec->next_ts = GST_BUFFER_TIMESTAMP (buf);
  }

  if (dec->tempbuf) {
    dec->tempbuf = gst_buffer_join (dec->tempbuf, buf);
  } else {
    dec->tempbuf = buf;
  }
  buf = NULL;

  if (!gst_jpeg_dec_ensure_header (dec))
    return GST_FLOW_OK;         /* need more data */

#if 0
  /* This is a very crude way to handle 'progressive
   *  loading' without parsing the image, and thus
   *  considerably cheaper. It should work for most
   *  most use cases, as with most use cases the end
   *  of the image is also the end of a buffer. */
  if (!gst_jpeg_dec_have_end_marker (dec))
    return GST_FLOW_OK;         /* need more data */
#endif

  /* Parse jpeg image to handle jpeg input that
   * is not aligned to buffer boundaries */
  img_len = gst_jpeg_dec_parse_image_data (dec);

  if (img_len == 0)
    return GST_FLOW_OK;         /* need more data */

  data = (guchar *) GST_BUFFER_DATA (dec->tempbuf);
  size = img_len;
  GST_LOG_OBJECT (dec, "image size = %u", img_len);

  dec->jsrc.pub.next_input_byte = data;
  dec->jsrc.pub.bytes_in_buffer = size;

  if (setjmp (dec->jerr.setjmp_buffer)) {
    if (dec->jerr.pub.msg_code == JERR_INPUT_EOF) {
      GST_DEBUG ("jpeg input EOF error, we probably need more data");
      return GST_FLOW_OK;
    }
    GST_ELEMENT_ERROR (dec, LIBRARY, TOO_LAZY,
        (_("Failed to decode JPEG image")), (NULL));
    ret = GST_FLOW_ERROR;
    goto done;
  }

  GST_LOG_OBJECT (dec, "reading header %02x %02x %02x %02x", data[0], data[1],
      data[2], data[3]);
  jpeg_read_header (&dec->cinfo, TRUE);

  r_h = dec->cinfo.cur_comp_info[0]->h_samp_factor;
  r_v = dec->cinfo.cur_comp_info[0]->v_samp_factor;

  GST_DEBUG ("num_components=%d, comps_in_scan=%d\n",
      dec->cinfo.num_components, dec->cinfo.comps_in_scan);
  for (i = 0; i < 3; ++i) {
    GST_DEBUG ("[%d] h_samp_factor=%d, v_samp_factor=%d\n", i,
        dec->cinfo.cur_comp_info[i]->h_samp_factor,
        dec->cinfo.cur_comp_info[i]->v_samp_factor);
  }

  dec->cinfo.do_fancy_upsampling = FALSE;
  dec->cinfo.do_block_smoothing = FALSE;
  dec->cinfo.out_color_space = JCS_YCbCr;
  dec->cinfo.dct_method = JDCT_IFAST;
  dec->cinfo.raw_data_out = TRUE;
  GST_LOG_OBJECT (dec, "starting decompress");
  guarantee_huff_tables (&dec->cinfo);
  jpeg_start_decompress (&dec->cinfo);
  width = dec->cinfo.output_width;
  height = dec->cinfo.output_height;

  if (width < MIN_WIDTH || width > MAX_WIDTH ||
      height < MIN_HEIGHT || height > MAX_HEIGHT) {
    GST_ELEMENT_ERROR (dec, STREAM, DECODE,
        ("Picture is too small or too big (%ux%u)", width, height),
        ("Picture is too small or too big (%ux%u)", width, height));
    ret = GST_FLOW_ERROR;
    goto done;
  }

  if (height != dec->height) {
    dec->line[0] = g_realloc (dec->line[0], height * sizeof (guint8 *));
    dec->line[1] = g_realloc (dec->line[1], height * sizeof (guint8 *));
    dec->line[2] = g_realloc (dec->line[2], height * sizeof (guint8 *));
    dec->height = height;
  }

  caps = gst_caps_new_simple ("video/x-raw-yuv",
      "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('I', '4', '2', '0'),
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", G_TYPE_DOUBLE, (double) dec->fps, NULL);

  GST_DEBUG_OBJECT (dec, "setting caps %" GST_PTR_FORMAT, caps);

  /* FIXME: someone needs to do the work to figure out how to correctly
   * calculate an output size that takes into account everything libjpeg
   * needs, like padding for DCT size and so on.  */
  outsize = I420_SIZE (width, height);

  if (gst_pad_alloc_buffer (dec->srcpad, GST_BUFFER_OFFSET_NONE, outsize,
          caps, &outbuf) != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (dec, "failed to alloc buffer");
    gst_caps_unref (caps);
    return GST_FLOW_ERROR;
  }

  {
    GstStructure *str = gst_caps_get_structure (caps, 0);

    if (gst_structure_get_double (str, "framerate", &dec->fps))
      GST_DEBUG ("framerate = %f\n", dec->fps);

  }

  gst_caps_unref (caps);

  outdata = GST_BUFFER_DATA (outbuf);
  GST_BUFFER_TIMESTAMP (outbuf) = dec->next_ts;
  GST_BUFFER_DURATION (outbuf) = GST_SECOND / dec->fps;
  GST_LOG_OBJECT (dec, "width %d, height %d, buffer size %d", width,
      height, outsize);

  dec->next_ts += GST_BUFFER_DURATION (outbuf);

  /* mind the swap, jpeglib outputs blue chroma first */
  base[0] = outdata + I420_Y_OFFSET (width, height);
  base[1] = outdata + I420_U_OFFSET (width, height);
  base[2] = outdata + I420_V_OFFSET (width, height);

  GST_LOG_OBJECT (dec, "decompressing %u", dec->cinfo.rec_outbuf_height);

  for (i = 0; i < height; i += r_v * DCTSIZE) {
    for (j = 0, k = 0; j < (r_v * DCTSIZE); j += r_v, k++) {
      dec->line[0][j] = base[0];
      base[0] += I420_Y_ROWSTRIDE (width);
      if (r_v == 2) {
        dec->line[0][j + 1] = base[0];
        base[0] += I420_Y_ROWSTRIDE (width);
      }
      dec->line[1][k] = base[1];
      dec->line[2][k] = base[2];
      if (r_v == 2 || k & 1) {
        base[1] += I420_U_ROWSTRIDE (width);
        base[2] += I420_V_ROWSTRIDE (width);
      }
    }
    /* GST_DEBUG ("output_scanline = %d", dec->cinfo.output_scanline); */
    jpeg_read_raw_data (&dec->cinfo, dec->line, r_v * DCTSIZE);
  }

  GST_LOG_OBJECT (dec, "decompressing finished");
  jpeg_finish_decompress (&dec->cinfo);

  GST_LOG_OBJECT (dec, "sending buffer");
  gst_pad_push (dec->srcpad, outbuf);

  ret = GST_FLOW_OK;

done:
  if (GST_BUFFER_SIZE (dec->tempbuf) == img_len) {
    gst_buffer_unref (dec->tempbuf);
    dec->tempbuf = NULL;
  } else {
    GstBuffer *buf = gst_buffer_create_sub (dec->tempbuf, img_len,
        GST_BUFFER_SIZE (dec->tempbuf) - img_len);

    gst_buffer_unref (dec->tempbuf);
    dec->tempbuf = buf;
  }

  return ret;
}

static GstElementStateReturn
gst_jpeg_dec_change_state (GstElement * element)
{
  GstJpegDec *dec = GST_JPEG_DEC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      dec->line[0] = NULL;
      dec->line[1] = NULL;
      dec->line[2] = NULL;
      dec->next_ts = 0;
      dec->width = -1;
      dec->height = -1;
      break;
    case GST_STATE_READY_TO_NULL:
      g_free (dec->line[0]);
      g_free (dec->line[1]);
      g_free (dec->line[2]);
      dec->line[0] = NULL;
      dec->line[1] = NULL;
      dec->line[2] = NULL;
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (dec->tempbuf) {
        gst_buffer_unref (dec->tempbuf);
        dec->tempbuf = NULL;
      }
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
