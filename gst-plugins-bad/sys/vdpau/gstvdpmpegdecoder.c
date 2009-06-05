/*
 * GStreamer
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
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
 * SECTION:element-vdpaumpegdec
 *
 * FIXME:Describe vdpaumpegdec here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! vdpaumpegdec ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/base/gstbytereader.h>
#include <gst/base/gstbitreader.h>
#include <string.h>

#include "mpegutil.h"
#include "gstvdpmpegdecoder.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_mpeg_decoder_debug);
#define GST_CAT_DEFAULT gst_vdp_mpeg_decoder_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_DISPLAY
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, mpegversion = (int) [ 1, 2 ], "
        "systemstream = (boolean) false, parsed = (boolean) true")
    );
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-vdpau-video, " "chroma-type = (int) 0")
    );

#define DEBUG_INIT(bla) \
GST_DEBUG_CATEGORY_INIT (gst_vdp_mpeg_decoder_debug, "vdpaumpegdec", 0, "VDPAU powered mpeg decoder");

GST_BOILERPLATE_FULL (GstVdpMpegDecoder, gst_vdp_mpeg_decoder,
    GstElement, GST_TYPE_ELEMENT, DEBUG_INIT);

static void gst_vdp_mpeg_decoder_init_info (VdpPictureInfoMPEG1Or2 * vdp_info);
static void gst_vdp_mpeg_decoder_finalize (GObject * object);
static void gst_vdp_mpeg_decoder_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_vdp_mpeg_decoder_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

guint8 *
mpeg_util_find_start_code (guint32 * sync_word, guint8 * cur, guint8 * end)
{
  guint32 code;

  if (G_UNLIKELY (cur == NULL))
    return NULL;

  code = *sync_word;

  while (cur < end) {
    code <<= 8;

    if (code == 0x00000100) {
      /* Reset the sync word accumulator */
      *sync_word = 0xffffffff;
      return cur;
    }

    /* Add the next available byte to the collected sync word */
    code |= *cur++;
  }

  *sync_word = code;
  return NULL;
}

typedef struct
{
  GstBuffer *buffer;
  guint8 *cur;
  guint8 *end;
} GstVdpMpegPacketizer;

static GstBuffer *
gst_vdp_mpeg_packetizer_get_next_packet (GstVdpMpegPacketizer * packetizer)
{
  guint32 sync_word = 0xffffff;
  guint8 *packet_start;
  guint8 *packet_end;

  if (!packetizer->cur)
    return NULL;

  packet_start = packetizer->cur - 3;
  packetizer->cur = packet_end = mpeg_util_find_start_code (&sync_word,
      packetizer->cur, packetizer->end);

  if (packet_end)
    packet_end -= 3;
  else
    packet_end = packetizer->end;

  return gst_buffer_create_sub (packetizer->buffer,
      packet_start - GST_BUFFER_DATA (packetizer->buffer),
      packet_end - packet_start);
}

static void
gst_vdp_mpeg_packetizer_init (GstVdpMpegPacketizer * packetizer,
    GstBuffer * buffer)
{
  guint32 sync_word = 0xffffffff;

  packetizer->buffer = buffer;
  packetizer->end = GST_BUFFER_DATA (buffer) + GST_BUFFER_SIZE (buffer);
  packetizer->cur = mpeg_util_find_start_code (&sync_word,
      GST_BUFFER_DATA (buffer), packetizer->end);
}

static gboolean
gst_vdp_mpeg_decoder_set_caps (GstPad * pad, GstCaps * caps)
{
  GstVdpMpegDecoder *mpeg_dec = GST_VDP_MPEG_DECODER (GST_OBJECT_PARENT (pad));
  GstStructure *structure;

  gint width, height;
  gint fps_n, fps_d;
  gint par_n, par_d;
  gboolean interlaced;

  GstCaps *src_caps;
  gboolean res;

  const GValue *value;
  VdpDecoderProfile profile;
  GstVdpDevice *device;
  VdpStatus status;

  structure = gst_caps_get_structure (caps, 0);

  /* create src_pad caps */
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);
  gst_structure_get_fraction (structure, "framerate", &fps_n, &fps_d);
  gst_structure_get_fraction (structure, "pixel-aspect-ratio", &par_n, &par_d);
  gst_structure_get_boolean (structure, "interlaced", &interlaced);

  src_caps = gst_caps_new_simple ("video/x-vdpau-video",
      "device", G_TYPE_OBJECT, mpeg_dec->device,
      "chroma_type", G_TYPE_INT, VDP_CHROMA_TYPE_420,
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, fps_n, fps_d,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, par_n, par_d,
      "interlaced", G_TYPE_BOOLEAN, interlaced, NULL);

  res = gst_pad_set_caps (mpeg_dec->src, src_caps);
  gst_caps_unref (src_caps);
  if (!res)
    return FALSE;

  mpeg_dec->width = width;
  mpeg_dec->height = height;
  mpeg_dec->fps_n = fps_n;
  mpeg_dec->fps_d = fps_d;
  mpeg_dec->interlaced = interlaced;

  /* parse caps to setup decoder */
  gst_structure_get_int (structure, "mpegversion", &mpeg_dec->version);
  if (mpeg_dec->version == 1)
    profile = VDP_DECODER_PROFILE_MPEG1;

  value = gst_structure_get_value (structure, "codec_data");
  if (value) {
    GstBuffer *codec_data, *buf;
    GstVdpMpegPacketizer packetizer;

    codec_data = gst_value_get_buffer (value);
    gst_vdp_mpeg_packetizer_init (&packetizer, codec_data);
    if ((buf = gst_vdp_mpeg_packetizer_get_next_packet (&packetizer))) {
      MPEGSeqHdr hdr;
      guint32 bitrate;

      mpeg_util_parse_sequence_hdr (&hdr, buf);

      memcpy (&mpeg_dec->vdp_info.intra_quantizer_matrix,
          &hdr.intra_quantizer_matrix, 64);
      memcpy (&mpeg_dec->vdp_info.non_intra_quantizer_matrix,
          &hdr.non_intra_quantizer_matrix, 64);

      bitrate = hdr.bitrate;
      gst_buffer_unref (buf);

      if ((buf = gst_vdp_mpeg_packetizer_get_next_packet (&packetizer))) {
        MPEGSeqExtHdr ext;

        mpeg_util_parse_sequence_extension (&ext, buf);
        if (mpeg_dec->version != 1) {
          switch (ext.profile) {
            case 5:
              profile = VDP_DECODER_PROFILE_MPEG2_SIMPLE;
              break;
            default:
              profile = VDP_DECODER_PROFILE_MPEG2_MAIN;
              break;
          }
        }

        bitrate += (ext.bitrate_ext << 18);;
        gst_buffer_unref (buf);
      }

      mpeg_dec->byterate = bitrate * 50;
      GST_DEBUG ("byterate: %" G_GINT64_FORMAT, mpeg_dec->byterate);
    }
  }

  device = mpeg_dec->device;

  if (mpeg_dec->decoder != VDP_INVALID_HANDLE) {
    device->vdp_decoder_destroy (mpeg_dec->decoder);
    mpeg_dec->decoder = VDP_INVALID_HANDLE;
  }

  status = device->vdp_decoder_create (device->device, profile, mpeg_dec->width,
      mpeg_dec->height, 2, &mpeg_dec->decoder);
  if (status != VDP_STATUS_OK) {
    GST_ELEMENT_ERROR (mpeg_dec, RESOURCE, READ,
        ("Could not create vdpau decoder"),
        ("Error returned from vdpau was: %s",
            device->vdp_get_error_string (status)));
    return FALSE;
  }
  return TRUE;
}

GstFlowReturn
gst_vdp_mpeg_decoder_push_video_buffer (GstVdpMpegDecoder * mpeg_dec,
    GstVdpVideoBuffer * buffer)
{
  if (GST_BUFFER_TIMESTAMP (buffer) == GST_CLOCK_TIME_NONE
      && GST_CLOCK_TIME_IS_VALID (mpeg_dec->next_timestamp)) {
    GST_BUFFER_TIMESTAMP (buffer) = mpeg_dec->next_timestamp;
  } else if (GST_BUFFER_TIMESTAMP (buffer) == GST_CLOCK_TIME_NONE) {
    GST_BUFFER_TIMESTAMP (buffer) = gst_util_uint64_scale (mpeg_dec->frame_nr,
        GST_SECOND * mpeg_dec->fps_d, mpeg_dec->fps_n);
  }

  if (mpeg_dec->seeking) {
    GstEvent *event;

    event = gst_event_new_new_segment (FALSE,
        mpeg_dec->segment.rate, GST_FORMAT_TIME, GST_BUFFER_TIMESTAMP (buffer),
        mpeg_dec->segment.stop, GST_BUFFER_TIMESTAMP (buffer));

    gst_pad_push_event (mpeg_dec->src, event);
  }
  mpeg_dec->seeking = FALSE;

  mpeg_dec->next_timestamp = GST_BUFFER_TIMESTAMP (buffer) +
      GST_BUFFER_DURATION (buffer);

  gst_buffer_set_caps (GST_BUFFER (buffer), GST_PAD_CAPS (mpeg_dec->src));

  GST_DEBUG_OBJECT (mpeg_dec,
      "Pushing buffer with timestamp: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));

  return gst_pad_push (mpeg_dec->src, GST_BUFFER (buffer));
}

static GstFlowReturn
gst_vdp_mpeg_decoder_decode (GstVdpMpegDecoder * mpeg_dec,
    GstClockTime timestamp)
{
  VdpPictureInfoMPEG1Or2 *info;
  GstBuffer *buffer;
  GstVdpVideoBuffer *outbuf;
  VdpVideoSurface surface;
  GstVdpDevice *device;
  VdpBitstreamBuffer vbit[1];
  VdpStatus status;

  info = &mpeg_dec->vdp_info;

  buffer = gst_adapter_take_buffer (mpeg_dec->adapter,
      gst_adapter_available (mpeg_dec->adapter));

  if (info->picture_coding_type != B_FRAME) {
    if (info->backward_reference != VDP_INVALID_HANDLE) {
      gst_buffer_ref (mpeg_dec->b_buffer);
      gst_vdp_mpeg_decoder_push_video_buffer (mpeg_dec,
          GST_VDP_VIDEO_BUFFER (mpeg_dec->b_buffer));
    }

    if (info->forward_reference != VDP_INVALID_HANDLE) {
      gst_buffer_unref (mpeg_dec->f_buffer);
      info->forward_reference = VDP_INVALID_HANDLE;
    }

    info->forward_reference = info->backward_reference;
    mpeg_dec->f_buffer = mpeg_dec->b_buffer;

    info->backward_reference = VDP_INVALID_HANDLE;
  }

  outbuf = gst_vdp_video_buffer_new (mpeg_dec->device, VDP_CHROMA_TYPE_420,
      mpeg_dec->width, mpeg_dec->height);
  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
  GST_BUFFER_DURATION (outbuf) = mpeg_dec->duration;
  GST_BUFFER_OFFSET (outbuf) = mpeg_dec->frame_nr;

  if (info->forward_reference != VDP_INVALID_HANDLE &&
      info->picture_coding_type != I_FRAME)
    gst_vdp_video_buffer_add_reference (outbuf,
        GST_VDP_VIDEO_BUFFER (mpeg_dec->f_buffer));

  if (info->backward_reference != VDP_INVALID_HANDLE)
    gst_vdp_video_buffer_add_reference (outbuf,
        GST_VDP_VIDEO_BUFFER (mpeg_dec->b_buffer));

  surface = outbuf->surface;

  device = mpeg_dec->device;

  vbit[0].struct_version = VDP_BITSTREAM_BUFFER_VERSION;
  vbit[0].bitstream = GST_BUFFER_DATA (buffer);
  vbit[0].bitstream_bytes = GST_BUFFER_SIZE (buffer);

  status = device->vdp_decoder_render (mpeg_dec->decoder, surface,
      (VdpPictureInfo *) info, 1, vbit);
  gst_buffer_unref (buffer);
  info->slice_count = 0;

  if (status != VDP_STATUS_OK) {
    GST_ELEMENT_ERROR (mpeg_dec, RESOURCE, READ,
        ("Could not decode"),
        ("Error returned from vdpau was: %s",
            device->vdp_get_error_string (status)));

    gst_buffer_unref (GST_BUFFER (outbuf));

    return GST_FLOW_ERROR;
  }

  if (info->picture_coding_type == B_FRAME) {
    gst_vdp_mpeg_decoder_push_video_buffer (mpeg_dec,
        GST_VDP_VIDEO_BUFFER (outbuf));
  } else {
    info->backward_reference = surface;
    mpeg_dec->b_buffer = GST_BUFFER (outbuf);
  }

  return GST_FLOW_OK;
}

static gboolean
gst_vdp_mpeg_decoder_parse_picture_coding (GstVdpMpegDecoder * mpeg_dec,
    GstBuffer * buffer)
{
  MPEGPictureExt pic_ext;
  VdpPictureInfoMPEG1Or2 *info;
  gint fields;

  info = &mpeg_dec->vdp_info;

  if (!mpeg_util_parse_picture_coding_extension (&pic_ext, buffer))
    return FALSE;

  memcpy (&mpeg_dec->vdp_info.f_code, &pic_ext.f_code, 4);

  info->intra_dc_precision = pic_ext.intra_dc_precision;
  info->picture_structure = pic_ext.picture_structure;
  info->top_field_first = pic_ext.top_field_first;
  info->frame_pred_frame_dct = pic_ext.frame_pred_frame_dct;
  info->concealment_motion_vectors = pic_ext.concealment_motion_vectors;
  info->q_scale_type = pic_ext.q_scale_type;
  info->intra_vlc_format = pic_ext.intra_vlc_format;
  info->alternate_scan = pic_ext.alternate_scan;

  fields = 2;
  if (pic_ext.picture_structure == 3) {
    if (mpeg_dec->interlaced) {
      if (pic_ext.progressive_frame == 0)
        fields = 2;
      if (pic_ext.progressive_frame == 0 && pic_ext.repeat_first_field == 0)
        fields = 2;
      if (pic_ext.progressive_frame == 1 && pic_ext.repeat_first_field == 1)
        fields = 3;
    } else {
      if (pic_ext.repeat_first_field == 0)
        fields = 2;
      if (pic_ext.repeat_first_field == 1 && pic_ext.top_field_first == 0)
        fields = 4;
      if (pic_ext.repeat_first_field == 1 && pic_ext.top_field_first == 1)
        fields = 6;
    }
  } else
    fields = 1;

  GST_DEBUG ("fields: %d", fields);

  mpeg_dec->duration = gst_util_uint64_scale (fields,
      GST_SECOND * mpeg_dec->fps_d, 2 * mpeg_dec->fps_n);

  return TRUE;
}

static gboolean
gst_vdp_mpeg_decoder_parse_sequence (GstVdpMpegDecoder * mpeg_dec,
    GstBuffer * buffer)
{
  MPEGSeqHdr hdr;

  if (!mpeg_util_parse_sequence_hdr (&hdr, buffer))
    return FALSE;

  memcpy (&mpeg_dec->vdp_info.intra_quantizer_matrix,
      &hdr.intra_quantizer_matrix, 64);
  memcpy (&mpeg_dec->vdp_info.non_intra_quantizer_matrix,
      &hdr.non_intra_quantizer_matrix, 64);

  return TRUE;
}

static gboolean
gst_vdp_mpeg_decoder_parse_picture (GstVdpMpegDecoder * mpeg_dec,
    GstBuffer * buffer)
{
  MPEGPictureHdr pic_hdr;

  if (!mpeg_util_parse_picture_hdr (&pic_hdr, buffer))
    return FALSE;

  if (pic_hdr.pic_type != I_FRAME
      && mpeg_dec->vdp_info.backward_reference == VDP_INVALID_HANDLE) {
    GST_DEBUG_OBJECT (mpeg_dec,
        "Drop frame since we haven't got an I_FRAME yet");
    return FALSE;
  }
  if (pic_hdr.pic_type == B_FRAME
      && mpeg_dec->vdp_info.forward_reference == VDP_INVALID_HANDLE) {
    GST_DEBUG_OBJECT (mpeg_dec,
        "Drop frame since we haven't got two non B_FRAMES yet");
    return FALSE;
  }

  mpeg_dec->vdp_info.picture_coding_type = pic_hdr.pic_type;

  if (mpeg_dec->version == 1) {
    mpeg_dec->vdp_info.full_pel_forward_vector =
        pic_hdr.full_pel_forward_vector;
    mpeg_dec->vdp_info.full_pel_backward_vector =
        pic_hdr.full_pel_backward_vector;
    memcpy (&mpeg_dec->vdp_info.f_code, &pic_hdr.f_code, 4);
  }

  mpeg_dec->duration = gst_util_uint64_scale (1, GST_SECOND * mpeg_dec->fps_d,
      mpeg_dec->fps_n);

  mpeg_dec->frame_nr = mpeg_dec->gop_frame + pic_hdr.tsn;

  return TRUE;
}

static gboolean
gst_vdp_mpeg_decoder_parse_gop (GstVdpMpegDecoder * mpeg_dec,
    GstBuffer * buffer)
{
  MPEGGop gop;
  GstClockTime time;

  if (!mpeg_util_parse_gop (&gop, buffer))
    return FALSE;

  time = GST_SECOND * (gop.hour * 3600 + gop.minute * 60 + gop.second);

  GST_DEBUG ("gop timestamp: %" GST_TIME_FORMAT, GST_TIME_ARGS (time));

  mpeg_dec->gop_frame =
      gst_util_uint64_scale (time, mpeg_dec->fps_n,
      mpeg_dec->fps_d * GST_SECOND) + gop.frame;

  return TRUE;
}

static gboolean
gst_vdp_mpeg_decoder_parse_quant_matrix (GstVdpMpegDecoder * mpeg_dec,
    GstBuffer * buffer)
{
  MPEGQuantMatrix qm;

  if (!mpeg_util_parse_quant_matrix (&qm, buffer))
    return FALSE;

  memcpy (&mpeg_dec->vdp_info.intra_quantizer_matrix,
      &qm.intra_quantizer_matrix, 64);
  memcpy (&mpeg_dec->vdp_info.non_intra_quantizer_matrix,
      &qm.non_intra_quantizer_matrix, 64);
  return TRUE;
}

static void
gst_vdp_mpeg_decoder_flush (GstVdpMpegDecoder * mpeg_dec)
{
  if (mpeg_dec->vdp_info.forward_reference != VDP_INVALID_HANDLE)
    gst_buffer_unref (mpeg_dec->f_buffer);
  if (mpeg_dec->vdp_info.backward_reference != VDP_INVALID_HANDLE)
    gst_buffer_unref (mpeg_dec->b_buffer);

  gst_vdp_mpeg_decoder_init_info (&mpeg_dec->vdp_info);

  gst_adapter_clear (mpeg_dec->adapter);

  mpeg_dec->next_timestamp = GST_CLOCK_TIME_NONE;
}

static void
gst_vdp_mpeg_decoder_reset (GstVdpMpegDecoder * mpeg_dec)
{
  gst_vdp_mpeg_decoder_flush (mpeg_dec);

  if (mpeg_dec->decoder != VDP_INVALID_HANDLE)
    mpeg_dec->device->vdp_decoder_destroy (mpeg_dec->decoder);
  mpeg_dec->decoder = VDP_INVALID_HANDLE;
  if (mpeg_dec->device)
    g_object_unref (mpeg_dec->device);
  mpeg_dec->device = NULL;

  gst_segment_init (&mpeg_dec->segment, GST_FORMAT_TIME);
  mpeg_dec->seeking = FALSE;
}

static GstFlowReturn
gst_vdp_mpeg_decoder_chain (GstPad * pad, GstBuffer * buffer)
{
  GstVdpMpegDecoder *mpeg_dec;
  GstVdpMpegPacketizer packetizer;
  GstBuffer *buf;
  GstFlowReturn ret = GST_FLOW_OK;

  mpeg_dec = GST_VDP_MPEG_DECODER (GST_OBJECT_PARENT (pad));

  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT))) {
    GST_DEBUG_OBJECT (mpeg_dec, "Received discont buffer");
    gst_vdp_mpeg_decoder_flush (mpeg_dec);
  }

  gst_vdp_mpeg_packetizer_init (&packetizer, buffer);
  while ((buf = gst_vdp_mpeg_packetizer_get_next_packet (&packetizer))) {
    GstBitReader b_reader = GST_BIT_READER_INIT_FROM_BUFFER (buf);
    guint32 sync_code;
    guint8 start_code;

    /* skip sync_code */
    gst_bit_reader_get_bits_uint32 (&b_reader, &sync_code, 8 * 3);

    /* start_code */
    gst_bit_reader_get_bits_uint8 (&b_reader, &start_code, 8);

    if (start_code >= MPEG_PACKET_SLICE_MIN
        && start_code <= MPEG_PACKET_SLICE_MAX) {
      GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_SLICE");

      gst_buffer_ref (buf);
      gst_adapter_push (mpeg_dec->adapter, buf);
      mpeg_dec->vdp_info.slice_count++;
    }

    switch (start_code) {
      case MPEG_PACKET_PICTURE:
        GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_PICTURE");

        if (!gst_vdp_mpeg_decoder_parse_picture (mpeg_dec, buf)) {
          return GST_FLOW_OK;
        }
        break;
      case MPEG_PACKET_SEQUENCE:
        GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_SEQUENCE");
        gst_vdp_mpeg_decoder_parse_sequence (mpeg_dec, buf);
        break;
      case MPEG_PACKET_EXTENSION:
      {
        guint8 ext_code;

        GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_EXTENSION");

        /* ext_code */
        gst_bit_reader_get_bits_uint8 (&b_reader, &ext_code, 4);
        switch (ext_code) {
          case MPEG_PACKET_EXT_PICTURE_CODING:
            GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_EXT_PICTURE_CODING");
            gst_vdp_mpeg_decoder_parse_picture_coding (mpeg_dec, buf);
            break;
          case MPEG_PACKET_EXT_QUANT_MATRIX:
            GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_EXT_QUANT_MATRIX");
            gst_vdp_mpeg_decoder_parse_quant_matrix (mpeg_dec, buf);
            break;
          default:
            break;
        }
        break;
      }
      case MPEG_PACKET_GOP:
        GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_GOP");
        gst_vdp_mpeg_decoder_parse_gop (mpeg_dec, buf);
        break;
      default:
        break;
    }

    gst_buffer_unref (buf);
  }

  if (mpeg_dec->vdp_info.slice_count > 0)
    ret = gst_vdp_mpeg_decoder_decode (mpeg_dec, GST_BUFFER_TIMESTAMP (buffer));

  return ret;
}

static gboolean
gst_vdp_mpeg_decoder_convert (GstVdpMpegDecoder * mpeg_dec,
    GstFormat src_format, gint64 src_value,
    GstFormat dest_format, gint64 * dest_value)
{

  if (src_format == dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  if (mpeg_dec->byterate == -1)
    return FALSE;

  if (src_format == GST_FORMAT_BYTES && dest_format == GST_FORMAT_TIME) {
    *dest_value = gst_util_uint64_scale (GST_SECOND, src_value,
        mpeg_dec->byterate);
    return TRUE;
  }

  if (src_format == GST_FORMAT_TIME && dest_format == GST_FORMAT_BYTES) {
    *dest_value =
        gst_util_uint64_scale_int (src_value, mpeg_dec->byterate, GST_SECOND);
    return TRUE;
  }

  return FALSE;
}

static const GstQueryType *
gst_mpeg_decoder_get_querytypes (GstPad * pad)
{
  static const GstQueryType list[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    0
  };

  return list;
}

static gboolean
gst_vdp_mpeg_decoder_src_query (GstPad * pad, GstQuery * query)
{
  GstVdpMpegDecoder *mpeg_dec = GST_VDP_MPEG_DECODER (GST_OBJECT_PARENT (pad));
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      if (gst_pad_query_default (pad, query))
        return TRUE;

      gst_query_parse_position (query, &format, NULL);
      if (format == GST_FORMAT_TIME &&
          GST_CLOCK_TIME_IS_VALID (mpeg_dec->next_timestamp)) {
        gst_query_set_position (query, GST_FORMAT_TIME,
            mpeg_dec->next_timestamp);
        res = TRUE;
      }
      break;
    }

    case GST_QUERY_DURATION:
    {
      GstFormat format;

      if (gst_pad_query_default (pad, query))
        return TRUE;

      gst_query_parse_duration (query, &format, NULL);
      if (format == GST_FORMAT_TIME) {
        gint64 bytes;

        format = GST_FORMAT_BYTES;
        if (gst_pad_query_duration (pad, &format, &bytes)
            && format == GST_FORMAT_BYTES) {
          gint64 duration;

          if (gst_vdp_mpeg_decoder_convert (mpeg_dec, GST_FORMAT_BYTES,
                  bytes, GST_FORMAT_TIME, &duration)) {
            GST_DEBUG ("duration: %" GST_TIME_FORMAT, GST_TIME_ARGS (duration));
            gst_query_set_duration (query, GST_FORMAT_TIME, duration);
            res = TRUE;
          }
        }
      }
      break;
    }

    default:
      res = gst_pad_query_default (pad, query);
  }

  return res;
}

static gboolean
normal_seek (GstPad * pad, GstEvent * event)
{
  GstVdpMpegDecoder *mpeg_dec = GST_VDP_MPEG_DECODER (GST_OBJECT_PARENT (pad));
  gdouble rate;
  GstFormat format, conv;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  gint64 time_cur, bytes_cur;
  gint64 time_stop, bytes_stop;
  gboolean res;
  GstEvent *peer_event;

  GST_DEBUG ("normal seek");

  gst_event_parse_seek (event, &rate, &format, &flags,
      &cur_type, &cur, &stop_type, &stop);

  conv = GST_FORMAT_TIME;
  if (!gst_vdp_mpeg_decoder_convert (mpeg_dec, format, cur, conv, &time_cur))
    goto convert_failed;
  if (!gst_vdp_mpeg_decoder_convert (mpeg_dec, format, stop, conv, &time_stop))
    goto convert_failed;

  GST_DEBUG ("seek to time %" GST_TIME_FORMAT "-%" GST_TIME_FORMAT,
      GST_TIME_ARGS (time_cur), GST_TIME_ARGS (time_stop));

  peer_event = gst_event_new_seek (rate, GST_FORMAT_TIME, flags,
      cur_type, time_cur, stop_type, time_stop);

  /* try seek on time then */
  if ((res = gst_pad_push_event (mpeg_dec->sink, peer_event)))
    goto done;

  /* else we try to seek on bytes */
  conv = GST_FORMAT_BYTES;
  if (!gst_vdp_mpeg_decoder_convert (mpeg_dec, GST_FORMAT_TIME, time_cur,
          conv, &bytes_cur))
    goto convert_failed;
  if (!gst_vdp_mpeg_decoder_convert (mpeg_dec, GST_FORMAT_TIME, time_stop,
          conv, &bytes_stop))
    goto convert_failed;

  /* conversion succeeded, create the seek */
  peer_event =
      gst_event_new_seek (rate, GST_FORMAT_BYTES, flags,
      cur_type, bytes_cur, stop_type, bytes_stop);

  /* do the seek */
  res = gst_pad_push_event (mpeg_dec->sink, peer_event);

  mpeg_dec->seeking = TRUE;

done:
  return res;

  /* ERRORS */
convert_failed:
  {
    /* probably unsupported seek format */
    GST_DEBUG_OBJECT (mpeg_dec,
        "failed to convert format %u into GST_FORMAT_TIME", format);
    return FALSE;
  }
}

static gboolean
gst_vdp_mpeg_decoder_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      if (gst_pad_event_default (pad, event))
        return TRUE;

      res = normal_seek (pad, event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
  }

  return res;
}

static gboolean
gst_vdp_mpeg_decoder_sink_event (GstPad * pad, GstEvent * event)
{
  GstVdpMpegDecoder *mpeg_dec = GST_VDP_MPEG_DECODER (GST_OBJECT_PARENT (pad));
  gboolean res;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
    {
      GST_DEBUG_OBJECT (mpeg_dec, "flush stop");

      gst_vdp_mpeg_decoder_flush (mpeg_dec);
      res = gst_pad_push_event (mpeg_dec->src, event);

      break;
    }
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      gdouble rate;
      GstFormat format;
      gint64 start;
      gint64 stop;
      gint64 position;

      gst_event_parse_new_segment (event, &update, &rate, &format,
          &start, &stop, &position);

      if (format != GST_FORMAT_TIME) {
        if (!gst_vdp_mpeg_decoder_convert (mpeg_dec, format, start,
                GST_FORMAT_TIME, &start))
          goto convert_error;
        if (!gst_vdp_mpeg_decoder_convert (mpeg_dec, format, stop,
                GST_FORMAT_TIME, &stop))
          goto convert_error;
        if (!gst_vdp_mpeg_decoder_convert (mpeg_dec, format, position,
                GST_FORMAT_TIME, &position))
          goto convert_error;

        gst_segment_set_newsegment (&mpeg_dec->segment, update, rate,
            GST_FORMAT_TIME, start, stop, position);

        gst_event_unref (event);
        event = gst_event_new_new_segment (update, rate, GST_FORMAT_TIME, start,
            stop, position);
      }

      /* if we seek ourselves we don't push out a newsegment now since we
       * use the calculated timestamp of the first frame for this */
      if (mpeg_dec->seeking) {
        gst_event_unref (event);
        return TRUE;
      }

    convert_error:
      res = gst_pad_push_event (mpeg_dec->src, event);

      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
  }

  return res;
}

static GstStateChangeReturn
gst_vdp_mpeg_decoder_change_state (GstElement * element,
    GstStateChange transition)
{
  GstVdpMpegDecoder *mpeg_dec;
  GstStateChangeReturn ret;

  mpeg_dec = GST_VDP_MPEG_DECODER (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      mpeg_dec->device = gst_vdp_get_device (mpeg_dec->display_name);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_vdp_mpeg_decoder_reset (mpeg_dec);
      break;
    default:
      break;
  }

  return ret;
}

/* GObject vmethod implementations */

static void
gst_vdp_mpeg_decoder_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "VDPAU Mpeg Decoder",
      "Decoder",
      "decode mpeg stream with vdpau",
      "Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
}

/* initialize the vdpaumpegdecoder's class */
static void
gst_vdp_mpeg_decoder_class_init (GstVdpMpegDecoderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_vdp_mpeg_decoder_finalize;
  gobject_class->set_property = gst_vdp_mpeg_decoder_set_property;
  gobject_class->get_property = gst_vdp_mpeg_decoder_get_property;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_vdp_mpeg_decoder_change_state);

  g_object_class_install_property (gobject_class, PROP_DISPLAY,
      g_param_spec_string ("display", "Display", "X Display name",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

static void
gst_vdp_mpeg_decoder_init_info (VdpPictureInfoMPEG1Or2 * vdp_info)
{
  vdp_info->forward_reference = VDP_INVALID_HANDLE;
  vdp_info->backward_reference = VDP_INVALID_HANDLE;
  vdp_info->slice_count = 0;
  vdp_info->picture_structure = 3;
  vdp_info->picture_coding_type = 0;
  vdp_info->intra_dc_precision = 0;
  vdp_info->frame_pred_frame_dct = 1;
  vdp_info->concealment_motion_vectors = 0;
  vdp_info->intra_vlc_format = 0;
  vdp_info->alternate_scan = 0;
  vdp_info->q_scale_type = 0;
  vdp_info->top_field_first = 1;
}

static void
gst_vdp_mpeg_decoder_init (GstVdpMpegDecoder * mpeg_dec,
    GstVdpMpegDecoderClass * gclass)
{
  mpeg_dec->src = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_set_event_function (mpeg_dec->src,
      GST_DEBUG_FUNCPTR (gst_vdp_mpeg_decoder_src_event));
  gst_pad_set_query_function (mpeg_dec->src,
      GST_DEBUG_FUNCPTR (gst_vdp_mpeg_decoder_src_query));
  gst_pad_set_query_type_function (mpeg_dec->src,
      GST_DEBUG_FUNCPTR (gst_mpeg_decoder_get_querytypes));
  gst_element_add_pad (GST_ELEMENT (mpeg_dec), mpeg_dec->src);

  mpeg_dec->sink = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_setcaps_function (mpeg_dec->sink,
      GST_DEBUG_FUNCPTR (gst_vdp_mpeg_decoder_set_caps));
  gst_pad_set_chain_function (mpeg_dec->sink,
      GST_DEBUG_FUNCPTR (gst_vdp_mpeg_decoder_chain));
  gst_pad_set_event_function (mpeg_dec->sink,
      GST_DEBUG_FUNCPTR (gst_vdp_mpeg_decoder_sink_event));
  gst_element_add_pad (GST_ELEMENT (mpeg_dec), mpeg_dec->sink);

  mpeg_dec->display_name = NULL;
  mpeg_dec->adapter = gst_adapter_new ();

  mpeg_dec->next_timestamp = 0;

  mpeg_dec->device = NULL;
  mpeg_dec->decoder = VDP_INVALID_HANDLE;
  gst_vdp_mpeg_decoder_init_info (&mpeg_dec->vdp_info);

  gst_segment_init (&mpeg_dec->segment, GST_FORMAT_TIME);
}

static void
gst_vdp_mpeg_decoder_finalize (GObject * object)
{
  GstVdpMpegDecoder *mpeg_dec = (GstVdpMpegDecoder *) object;

  g_object_unref (mpeg_dec->adapter);
}

static void
gst_vdp_mpeg_decoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVdpMpegDecoder *mpeg_dec = GST_VDP_MPEG_DECODER (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      g_free (mpeg_dec->display_name);
      mpeg_dec->display_name = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdp_mpeg_decoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVdpMpegDecoder *mpeg_dec = GST_VDP_MPEG_DECODER (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      g_value_set_string (value, mpeg_dec->display_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
