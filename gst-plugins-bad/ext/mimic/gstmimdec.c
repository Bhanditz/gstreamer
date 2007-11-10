/*
 * GStreamer
 * Copyright (c) 2005 INdT.
 * @author Andre Moreira Magalhaes <andre.magalhaes@indt.org.br>
 * @author Rob Taylor <robtaylor@fastmail.fm>
 * @author Philippe Khalaf <burger@speedy.org>
 * @author Ole André Vadla Ravnås <oleavr@gmail.com>
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

#include <gst/gst.h>

#include "gstmimdec.h"

GST_DEBUG_CATEGORY (mimdec_debug);
#define GST_CAT_DEFAULT (mimdec_debug)

static GstStaticPadTemplate sink_factory =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("video/x-msnmsgr-webcam")
);

static GstStaticPadTemplate src_factory =
GST_STATIC_PAD_TEMPLATE (
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "bpp = (int) 24, "
        "depth = (int) 24, "
        "endianness = (int) 4321, "
        "framerate = (double) [1.0, 30.0], "
        "red_mask = (int) 16711680, "
        "green_mask = (int) 65280, "
        "blue_mask = (int) 255, "
        "height = (int) [16, 4096], "
        "width = (int) [16, 4096]" 
    )
);

static void          gst_mimdec_class_init   (GstMimDecClass *klass);
static void          gst_mimdec_base_init    (GstMimDecClass *klass);
static void          gst_mimdec_init	     (GstMimDec      *mimdec);
static void          gst_mimdec_finalize      (GObject        *object);

static GstFlowReturn gst_mimdec_chain        (GstPad         *pad, 
                                              GstBuffer      *in);
static GstCaps      *gst_mimdec_src_getcaps  (GstPad         *pad);

static GstStateChangeReturn
                     gst_mimdec_change_state (GstElement     *element, 
                                              GstStateChange  transition);

static GstElementClass *parent_class = NULL;

GType
gst_gst_mimdec_get_type (void)
{
  static GType plugin_type = 0;

  if (!plugin_type)
  {
    static const GTypeInfo plugin_info =
    {
      sizeof (GstMimDecClass),
      (GBaseInitFunc) gst_mimdec_base_init,
      NULL,
      (GClassInitFunc) gst_mimdec_class_init,
      NULL,
      NULL,
      sizeof (GstMimDec),
      0,
      (GInstanceInitFunc) gst_mimdec_init,
    };
    plugin_type = g_type_register_static (GST_TYPE_ELEMENT,
                                          "GstMimDec",
                                          &plugin_info, 0);
  }
  return plugin_type;
}

static void
gst_mimdec_base_init (GstMimDecClass *klass)
{
  static GstElementDetails plugin_details = {
    "MimDec",
    "Codec/Decoder/Video",
    "Mimic decoder",
    "Andre Moreira Magalhaes <andre.magalhaes@indt.org.br>, "
    "Rob Taylor <robtaylor@fastmail.fm>, "
    "Philippe Khalaf <burger@speedy.org>, "
    "Ole André Vadla Ravnås <oleavr@gmail.com>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
          gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
          gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_details (element_class, &plugin_details);
}

static void
gst_mimdec_class_init (GstMimDecClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;
  gstelement_class->change_state = gst_mimdec_change_state;

  gobject_class->finalize = gst_mimdec_finalize;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  GST_DEBUG_CATEGORY_INIT (mimdec_debug, "mimdec", 0, "Mimic decoder plugin");
}

static void
gst_mimdec_init (GstMimDec *mimdec)
{
  mimdec->sinkpad = gst_pad_new_from_template (
          gst_static_pad_template_get (&sink_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (mimdec), mimdec->sinkpad);
  gst_pad_set_chain_function (mimdec->sinkpad, gst_mimdec_chain);

  mimdec->srcpad = gst_pad_new_from_template (
          gst_static_pad_template_get (&src_factory), "src");
  gst_pad_set_getcaps_function (mimdec->srcpad, gst_mimdec_src_getcaps);
  gst_element_add_pad (GST_ELEMENT (mimdec), mimdec->srcpad);

  mimdec->adapter = gst_adapter_new ();

  mimdec->dec = NULL;
  mimdec->buffer_size = -1;
  mimdec->have_header = FALSE;
  mimdec->payload_size = -1;
  mimdec->last_ts = -1;
  mimdec->current_ts = -1;
  mimdec->gst_timestamp = -1;
}

static void
gst_mimdec_finalize (GObject *object)
{
    GstMimDec *mimdec = GST_MIMDEC (object);

    gst_adapter_clear (mimdec->adapter);
    g_object_unref (mimdec->adapter);
}

static GstFlowReturn
gst_mimdec_chain (GstPad *pad, GstBuffer *in)
{
  GstMimDec *mimdec;
  GstBuffer *out_buf, *buf;
  guchar *header, *frame_body;
  guint32 fourcc;
  guint16 header_size;
  gint width, height;
  GstCaps * caps;
  GstFlowReturn res = GST_FLOW_OK;

  GST_DEBUG ("in gst_mimdec_chain");

  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);

  mimdec = GST_MIMDEC (gst_pad_get_parent (pad));
  g_return_val_if_fail (GST_IS_MIMDEC (mimdec), GST_FLOW_ERROR);

  g_return_val_if_fail(GST_PAD_IS_LINKED(mimdec->srcpad), GST_FLOW_ERROR);

  buf = GST_BUFFER (in);
  gst_adapter_push (mimdec->adapter, buf);


  if (mimdec->gst_timestamp == -1) {
    GstClock *clock;
    GstClockTime base_time;

    base_time = gst_element_get_base_time (GST_ELEMENT (mimdec));

    clock = gst_element_get_clock (GST_ELEMENT (mimdec));
    if (clock != NULL) {
      mimdec->gst_timestamp = gst_clock_get_time (clock) - base_time;
      gst_object_unref (clock);
    }
  }


  // do we have enough bytes to read a header
  while (gst_adapter_available (mimdec->adapter) >= (mimdec->have_header ? mimdec->payload_size : 24)) {
      if (!mimdec->have_header) {
          header = (guchar *) gst_adapter_peek (mimdec->adapter, 24);
          header_size = GUINT16_FROM_LE (*(guint16 *) (header + 0));
          if (header_size != 24) {
              GST_WARNING_OBJECT (mimdec, 
                "invalid frame: header size %d incorrect", header_size);
              gst_adapter_flush (mimdec->adapter, 24);
              res = GST_FLOW_ERROR;
              goto out;
          }

          fourcc = GST_MAKE_FOURCC ('M', 'L', '2', '0');
          if (GUINT32_FROM_LE (*((guint32 *) (header + 12))) != fourcc) {
              GST_WARNING_OBJECT (mimdec, "invalid frame: unknown FOURCC code %d", fourcc);
              gst_adapter_flush (mimdec->adapter, 24);
              res = GST_FLOW_ERROR;
              goto out;
          }

          mimdec->payload_size = GUINT32_FROM_LE (*((guint32 *) (header + 8)));
          GST_DEBUG ("Got packet, payload size %d", mimdec->payload_size);

          gst_adapter_flush (mimdec->adapter, 24);

          mimdec->have_header = TRUE;
      }

      if (gst_adapter_available (mimdec->adapter) < mimdec->payload_size)
      {
        goto out;
      }

      frame_body = (guchar *) gst_adapter_peek (mimdec->adapter, mimdec->payload_size);

      if (mimdec->dec == NULL) {
          GstSegment segment;
          GstEvent * event;
          gboolean result;

          mimdec->dec = mimic_open ();
          if (mimdec->dec == NULL) {
              GST_WARNING_OBJECT (mimdec, "mimic_open error\n");

              gst_adapter_flush (mimdec->adapter, mimdec->payload_size);
              mimdec->have_header = FALSE;
              res = GST_FLOW_ERROR;
              goto out;
          }

          if (!mimic_decoder_init (mimdec->dec, frame_body)) {
              GST_WARNING_OBJECT (mimdec, "mimic_decoder_init error\n");
              mimic_close (mimdec->dec);
              mimdec->dec = NULL;

              gst_adapter_flush (mimdec->adapter, mimdec->payload_size);
              mimdec->have_header = FALSE;
              res = GST_FLOW_ERROR;
              goto out;
          }

          if (!mimic_get_property (mimdec->dec, "buffer_size", &mimdec->buffer_size)) {
              GST_WARNING_OBJECT (mimdec,
                  "mimic_get_property('buffer_size') error\n");
              mimic_close (mimdec->dec);
              mimdec->dec = NULL;

              gst_adapter_flush (mimdec->adapter, mimdec->payload_size);
              mimdec->have_header = FALSE;
              res = GST_FLOW_ERROR;
              goto out;
          }

          gst_segment_init (&segment, GST_FORMAT_TIME);
          event = gst_event_new_new_segment (FALSE, segment.rate,
              segment.format, segment.start, segment.stop, segment.time);

          result = gst_pad_push_event (mimdec->srcpad, event);
          if (!result)
          {
              GST_WARNING_OBJECT (mimdec, "gst_pad_push_event failed");
              res = GST_FLOW_ERROR;
              goto out;
          }
      }

      out_buf = gst_buffer_new_and_alloc (mimdec->buffer_size);
      GST_BUFFER_TIMESTAMP(out_buf) = GST_BUFFER_TIMESTAMP(buf);
      if (!mimic_decode_frame (mimdec->dec, frame_body, GST_BUFFER_DATA (out_buf))) {
          GST_WARNING_OBJECT (mimdec, "mimic_decode_frame error\n");

          gst_adapter_flush (mimdec->adapter, mimdec->payload_size);
          mimdec->have_header = FALSE;

          gst_buffer_unref (out_buf);
          res = GST_FLOW_ERROR;
          goto out;
      }
      
      mimic_get_property(mimdec->dec, "width", &width);
      mimic_get_property(mimdec->dec, "height", &height);
      GST_DEBUG_OBJECT (mimdec, 
          "got WxH %d x %d payload size %d buffer_size %d",
          width, height, mimdec->payload_size, mimdec->buffer_size);
      caps = gst_caps_new_simple ("video/x-raw-rgb",
              "bpp", G_TYPE_INT, 24,
              "depth", G_TYPE_INT, 24,
              "endianness", G_TYPE_INT, 4321,
              "framerate", G_TYPE_DOUBLE, 30.0,
              "red_mask", G_TYPE_INT, 16711680,
              "green_mask", G_TYPE_INT, 65280,
              "blue_mask", G_TYPE_INT, 255,
              "width", G_TYPE_INT, width,
              "height", G_TYPE_INT, height, NULL);
      gst_buffer_set_caps (out_buf, caps);
      gst_caps_unref (caps);
      res = gst_pad_push (mimdec->srcpad, out_buf);

      gst_adapter_flush (mimdec->adapter, mimdec->payload_size);
      mimdec->have_header = FALSE;
  }

 out:
  gst_object_unref (mimdec);

  return res;
}

static GstStateChangeReturn
gst_mimdec_change_state (GstElement *element, GstStateChange transition)
{
  GstMimDec *mimdec;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      mimdec = GST_MIMDEC (element);
      if (mimdec->dec != NULL) {
        mimic_close (mimdec->dec);
        mimdec->dec = NULL;
        mimdec->buffer_size = -1;
        mimdec->have_header = FALSE;
        mimdec->payload_size = -1;
      }
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static GstCaps *
gst_mimdec_src_getcaps (GstPad *pad)
{
  GstCaps *caps;

  if (!(caps = GST_PAD_CAPS (pad)))
    caps = (GstCaps *) gst_pad_get_pad_template_caps (pad);
  caps = gst_caps_ref (caps);

  return caps;
}
