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

#include "gstspeexenc.h"

extern GstPadTemplate *speexenc_src_template, *speexenc_sink_template;

/* elementfactory information */
GstElementDetails gst_speexenc_details = {
  "speex audio encoder",
  "Codec/Audio/Encoder",
  "LGPL",
  ".speex",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2000",
};

/* SpeexEnc signals and args */
enum {
  FRAME_ENCODED,
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

static void			gst_speexenc_class_init	(GstSpeexEnc *klass);
static void			gst_speexenc_init		(GstSpeexEnc *speexenc);

static void			gst_speexenc_chain	(GstPad *pad,GstBuffer *buf);
static GstPadLinkReturn	gst_speexenc_sinkconnect 	(GstPad *pad, GstCaps *caps);

static GstElementClass *parent_class = NULL;
static guint gst_speexenc_signals[LAST_SIGNAL] = { 0 };

GType
gst_speexenc_get_type (void)
{
  static GType speexenc_type = 0;

  if (!speexenc_type) {
    static const GTypeInfo speexenc_info = {
      sizeof (GstSpeexEncClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_speexenc_class_init,
      NULL,
      NULL,
      sizeof (GstSpeexEnc),
      0,
      (GInstanceInitFunc) gst_speexenc_init,
    };
    speexenc_type = g_type_register_static (GST_TYPE_ELEMENT, "GstSpeexEnc", &speexenc_info, 0);
  }
  return speexenc_type;
}

static void
gst_speexenc_class_init (GstSpeexEnc *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gst_speexenc_signals[FRAME_ENCODED] =
    g_signal_new ("frame_encoded", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   G_STRUCT_OFFSET (GstSpeexEncClass, frame_encoded), NULL, NULL,
                   g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}


static void
gst_speexenc_init (GstSpeexEnc *speexenc)
{
  /* create the sink and src pads */
  speexenc->sinkpad = gst_pad_new_from_template (speexenc_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (speexenc), speexenc->sinkpad);
  gst_pad_set_chain_function (speexenc->sinkpad, gst_speexenc_chain);
  gst_pad_set_link_function (speexenc->sinkpad, gst_speexenc_sinkconnect);

  speexenc->srcpad = gst_pad_new_from_template (speexenc_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (speexenc), speexenc->srcpad);

  speex_bits_init(&speexenc->bits);
  speexenc->mode = &speex_nb_mode;
  speexenc->bufsize = 0;
  speexenc->packet_count = 0;
  speexenc->n_packets = 20;
}

static GstPadLinkReturn
gst_speexenc_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstSpeexEnc *speexenc;

  speexenc = GST_SPEEXENC (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps)) 
    return GST_PAD_LINK_DELAYED;

  gst_caps_get_int (caps, "rate", &speexenc->rate);
  if (gst_pad_try_set_caps (speexenc->srcpad, GST_CAPS_NEW (
                              "speex_speex",
                              "audio/x-speex",
                                "rate",       GST_PROPS_INT (speexenc->rate),
				"channels",   GST_PROPS_INT (1)
                               )))
  {
    speex_init_header(&speexenc->header, speexenc->rate, 1, speexenc->mode);
    speexenc->header.frames_per_packet = speexenc->n_packets;

    speexenc->state = speex_encoder_init(speexenc->mode);
    speex_encoder_ctl(speexenc->state, SPEEX_GET_FRAME_SIZE, &speexenc->frame_size);

    return GST_PAD_LINK_OK;
  }
  return GST_PAD_LINK_REFUSED;

}

static void
gst_speexenc_chain (GstPad *pad, GstBuffer *buf)
{
  GstSpeexEnc *speexenc;
  GstBuffer *outbuf;
  gint16 *data;
  guint8 *header_data;
  gint size;
  float input[1000];
  gint frame_size;
  gint i;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  speexenc = GST_SPEEXENC (GST_OBJECT_PARENT (pad));
	      
  if (!GST_PAD_CAPS (speexenc->srcpad)) {

    if (!gst_pad_try_set_caps (speexenc->srcpad,
		      GST_CAPS_NEW (
    			"speex_enc",
    			"audio/x-speex",
    		 	"rate",     GST_PROPS_INT (speexenc->rate),
			"channels", GST_PROPS_INT (1)
		      )))
    {
      gst_element_gerror(GST_ELEMENT (speexenc), GST_ERROR_UNKNOWN,
        g_strdup ("unconverted error, file a bug"),
        g_strdup_printf("could not negotiate"));
      return;
    }
  }

  if (speexenc->packet_count == 0) {
    header_data = speex_header_to_packet(&speexenc->header, &size);

    outbuf = gst_buffer_new ();
    GST_BUFFER_DATA (outbuf) = header_data;
    GST_BUFFER_SIZE (outbuf) = size;

    gst_pad_push (speexenc->srcpad, outbuf);
  }

  data = (gint16 *) GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf) / sizeof (gint16);

  frame_size = speexenc->frame_size;

  if (speexenc->bufsize && (speexenc->bufsize + size >= frame_size)) {
    memcpy (speexenc->buffer + speexenc->bufsize, data, (frame_size - speexenc->bufsize) * sizeof (gint16));

    for (i = 0; i < frame_size; i++)
      input[i] = speexenc->buffer[i];

    speex_encode (speexenc->state, input, &speexenc->bits);
    speexenc->packet_count++;

    if (speexenc->packet_count % speexenc->n_packets == 0) {
      GstBuffer *outbuf;

      outbuf = gst_buffer_new_and_alloc (frame_size * speexenc->n_packets);
      GST_BUFFER_SIZE (outbuf) = speex_bits_write(&speexenc->bits, 
		                 GST_BUFFER_DATA (outbuf), GST_BUFFER_SIZE (outbuf));
      GST_BUFFER_TIMESTAMP (outbuf) = speexenc->next_ts;
      speex_bits_reset(&speexenc->bits);

      gst_pad_push (speexenc->srcpad, outbuf);
      speexenc->next_ts += frame_size * GST_SECOND / speexenc->rate;
    }

    size -= (speexenc->frame_size - speexenc->bufsize);
    data += (speexenc->frame_size - speexenc->bufsize);

    speexenc->bufsize = 0;
  }

  while (size >= frame_size) {
    for (i = 0; i < frame_size; i++)
      input[i] = data[i];

    speex_encode (speexenc->state, input, &speexenc->bits);
    speexenc->packet_count++;

    if (speexenc->packet_count % speexenc->n_packets == 0) {
      GstBuffer *outbuf;

      outbuf = gst_buffer_new_and_alloc (frame_size * speexenc->n_packets);
      GST_BUFFER_SIZE (outbuf) = speex_bits_write(&speexenc->bits, 
		                 GST_BUFFER_DATA (outbuf), GST_BUFFER_SIZE (outbuf));
      GST_BUFFER_TIMESTAMP (outbuf) = speexenc->next_ts;
      speex_bits_reset(&speexenc->bits);

      gst_pad_push (speexenc->srcpad, outbuf);
      speexenc->next_ts += frame_size * GST_SECOND / speexenc->rate;
    }

    size -= frame_size;
    data += frame_size;
  }

  if (size) {
    memcpy (speexenc->buffer + speexenc->bufsize, data, size * sizeof (gint16));
    speexenc->bufsize += size;
  }
  
  gst_buffer_unref(buf);
}
