/* -*- c-basic-offset: 2 -*-
 * 
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
 *               2006 Dreamlab Technologies Ltd. <mathis.hofer@dreamlab.net>
 *               2007-2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <math.h>
#include <gst/gst.h>
#include <gst/audio/gstaudiofilter.h>
#include <gst/controller/gstcontroller.h>

/* FIXME: Remove this once we depend on gst-plugins-base 0.10.26 */
#ifndef GST_AUDIO_FILTER_CAST
#define GST_AUDIO_FILTER_CAST(obj) ((GstAudioFilter *) (obj))
#endif

#include "audiofxbasefirfilter.h"

#define GST_CAT_DEFAULT gst_audio_fx_base_fir_filter_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define ALLOWED_CAPS \
    "audio/x-raw-float, "                                             \
    " width = (int) { 32, 64 }, "                                     \
    " endianness = (int) BYTE_ORDER, "                                \
    " rate = (int) [ 1, MAX ], "                                      \
    " channels = (int) [ 1, MAX ]"

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_audio_fx_base_fir_filter_debug, "audiofxbasefirfilter", 0, \
      "FIR filter base class");

/* Switch from time-domain to FFT convolution for kernels >= this */
#define FFT_THRESHOLD 32

GST_BOILERPLATE_FULL (GstAudioFXBaseFIRFilter, gst_audio_fx_base_fir_filter,
    GstAudioFilter, GST_TYPE_AUDIO_FILTER, DEBUG_INIT);

static GstFlowReturn gst_audio_fx_base_fir_filter_transform (GstBaseTransform *
    base, GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_audio_fx_base_fir_filter_start (GstBaseTransform * base);
static gboolean gst_audio_fx_base_fir_filter_stop (GstBaseTransform * base);
static gboolean gst_audio_fx_base_fir_filter_event (GstBaseTransform * base,
    GstEvent * event);
static gboolean gst_audio_fx_base_fir_filter_transform_size (GstBaseTransform *
    base, GstPadDirection direction, GstCaps * caps, guint size,
    GstCaps * othercaps, guint * othersize);
static gboolean gst_audio_fx_base_fir_filter_setup (GstAudioFilter * base,
    GstRingBufferSpec * format);

static gboolean gst_audio_fx_base_fir_filter_query (GstPad * pad,
    GstQuery * query);
static const GstQueryType *gst_audio_fx_base_fir_filter_query_type (GstPad *
    pad);

/* Element class */

static void
gst_audio_fx_base_fir_filter_dispose (GObject * object)
{
  GstAudioFXBaseFIRFilter *self = GST_AUDIO_FX_BASE_FIR_FILTER (object);

  g_free (self->buffer);
  self->buffer = NULL;
  self->buffer_length = 0;

  g_free (self->kernel);
  self->kernel = NULL;

  gst_fft_f64_free (self->fft);
  self->fft = NULL;
  gst_fft_f64_free (self->ifft);
  self->ifft = NULL;

  g_free (self->frequency_response);
  self->frequency_response = NULL;

  g_free (self->fft_buffer);
  self->fft_buffer = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_audio_fx_base_fir_filter_base_init (gpointer g_class)
{
  GstCaps *caps;

  caps = gst_caps_from_string (ALLOWED_CAPS);
  gst_audio_filter_class_add_pad_templates (GST_AUDIO_FILTER_CLASS (g_class),
      caps);
  gst_caps_unref (caps);
}

static void
gst_audio_fx_base_fir_filter_class_init (GstAudioFXBaseFIRFilterClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;
  GstAudioFilterClass *filter_class = (GstAudioFilterClass *) klass;

  gobject_class->dispose = gst_audio_fx_base_fir_filter_dispose;

  trans_class->transform =
      GST_DEBUG_FUNCPTR (gst_audio_fx_base_fir_filter_transform);
  trans_class->start = GST_DEBUG_FUNCPTR (gst_audio_fx_base_fir_filter_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_audio_fx_base_fir_filter_stop);
  trans_class->event = GST_DEBUG_FUNCPTR (gst_audio_fx_base_fir_filter_event);
  trans_class->transform_size =
      GST_DEBUG_FUNCPTR (gst_audio_fx_base_fir_filter_transform_size);
  filter_class->setup = GST_DEBUG_FUNCPTR (gst_audio_fx_base_fir_filter_setup);
}

static void
gst_audio_fx_base_fir_filter_init (GstAudioFXBaseFIRFilter * self,
    GstAudioFXBaseFIRFilterClass * g_class)
{
  self->kernel = NULL;
  self->buffer = NULL;
  self->buffer_length = 0;

  self->start_ts = GST_CLOCK_TIME_NONE;
  self->start_off = GST_BUFFER_OFFSET_NONE;
  self->nsamples_out = 0;
  self->nsamples_in = 0;

  gst_pad_set_query_function (GST_BASE_TRANSFORM (self)->srcpad,
      gst_audio_fx_base_fir_filter_query);
  gst_pad_set_query_type_function (GST_BASE_TRANSFORM (self)->srcpad,
      gst_audio_fx_base_fir_filter_query_type);
}

/* This implements FFT convolution and uses the overlap-save algorithm.
 * See http://cnx.org/content/m12022/latest/ or your favorite
 * digital signal processing book for details.
 *
 * In every pass the following is calculated:
 *
 * y = IFFT (FFT(x) * FFT(h))
 *
 * where y is the output in the time domain, x the
 * input and h the filter kernel. * is the multiplication
 * of complex numbers.
 *
 * Due to the circular convolution theorem this
 * gives in the time domain:
 *
 * y[t] = \sum_{u=0}^{M-1} x[t - u] * h[u]
 *
 * where y is the output, M is the kernel length,
 * x the periodically extended[0] input and h the
 * filter kernel.
 *
 * ([0] Periodically extended means:    )
 * (    x[t] = x[t+kN] \forall k \in Z  )
 * (    where N is the length of x      )
 *
 * This means:
 * - Obviously x and h need to be of the same size for the FFT
 * - The first M-1 output values are useless because they're
 *   built from 1 up to M-1 values from the end of the input
 *   (circular convolusion!).
 * - The last M-1 input values are only used for 1 up to M-1
 *   output values, i.e. they need to be used again in the
 *   next pass for the first M-1 input values.
 *
 * => The first pass needs M-1 zeroes at the beginning of the
 * input and the last M-1 input values of every pass need to
 * be used as the first M-1 input values of the next pass.
 *
 * => x must be larger than h to give a useful number of output
 * samples and h needs to be padded by zeroes at the end to give
 * it virtually the same size as x (by M we denote the number of
 * non-padding samples of h). If len(x)==len(h)==M only 1 output
 * sample would be calculated per pass, len(x)==2*len(h) would
 * give M+1 output samples, etc. Usually a factor between 4 and 8
 * gives a low number of operations per output samples (see website
 * given above).
 *
 * Overall this gives a runtime complexity per sample of
 *
 *   (  N log N  )
 * O ( --------- ) compared to O (M) for the direct calculation.
 *   ( N - M + 1 )
 */
#define DEFINE_FFT_PROCESS_FUNC(width,ctype) \
static guint \
process_fft_##width (GstAudioFXBaseFIRFilter * self, const g##ctype * src, \
    g##ctype * dst, guint input_samples) \
{ \
  gint channels = GST_AUDIO_FILTER_CAST (self)->format.channels; \
  gint i, j; \
  guint pass; \
  guint kernel_length = self->kernel_length; \
  guint block_length = self->block_length; \
  guint buffer_length = self->buffer_length; \
  guint real_buffer_length = buffer_length + kernel_length - 1; \
  guint buffer_fill = self->buffer_fill; \
  GstFFTF64 *fft = self->fft; \
  GstFFTF64 *ifft = self->ifft; \
  GstFFTF64Complex *frequency_response = self->frequency_response; \
  GstFFTF64Complex *fft_buffer = self->fft_buffer; \
  guint frequency_response_length = self->frequency_response_length; \
  gdouble *buffer = self->buffer; \
  guint generated = 0; \
  gdouble re, im; \
  \
  if (!fft_buffer) \
    self->fft_buffer = fft_buffer = \
        g_new (GstFFTF64Complex, frequency_response_length); \
  \
  /* Buffer contains the time domain samples of input data for one chunk \
   * plus some more space for the inverse FFT below. \
   * \
   * The samples are put at offset kernel_length, the inverse FFT \
   * overwrites everthing from offset 0 to length-kernel_length+1, keeping \
   * the last kernel_length-1 samples for copying to the next processing \
   * step. \
   */ \
  if (!buffer) { \
    self->buffer_length = buffer_length = block_length; \
    real_buffer_length = buffer_length + kernel_length - 1; \
    \
    self->buffer = buffer = g_new0 (gdouble, real_buffer_length * channels); \
    \
    /* Beginning has kernel_length-1 zeroes at the beginning */ \
    self->buffer_fill = buffer_fill = kernel_length - 1; \
  } \
  \
  while (input_samples) { \
    pass = MIN (buffer_length - buffer_fill, input_samples); \
    \
    /* Deinterleave channels */ \
    for (i = 0; i < pass; i++) { \
      for (j = 0; j < channels; j++) { \
        buffer[real_buffer_length * j + buffer_fill + kernel_length - 1 + i] = \
            src[i * channels + j]; \
      } \
    } \
    buffer_fill += pass; \
    src += channels * pass; \
    input_samples -= pass; \
    \
    /* If we don't have a complete buffer go out */ \
    if (buffer_fill < buffer_length) \
      break; \
    \
    for (j = 0; j < channels; j++) { \
      /* Calculate FFT of input block */ \
      gst_fft_f64_fft (fft, \
          buffer + real_buffer_length * j + kernel_length - 1, fft_buffer); \
      \
      /* Complex multiplication of input and filter spectrum */ \
      for (i = 0; i < frequency_response_length; i++) { \
	re = fft_buffer[i].r; \
	im = fft_buffer[i].i; \
        \
        fft_buffer[i].r = \
            re * frequency_response[i].r - \
            im * frequency_response[i].i; \
        fft_buffer[i].i = \
            re * frequency_response[i].i + \
            im * frequency_response[i].r; \
      } \
      \
      /* Calculate inverse FFT of the result */ \
      gst_fft_f64_inverse_fft (ifft, fft_buffer, \
          buffer + real_buffer_length * j); \
      \
      /* Copy all except the first kernel_length-1 samples to the output */ \
      for (i = 0; i < buffer_length - kernel_length + 1; i++) { \
        dst[i * channels + j] = \
            buffer[real_buffer_length * j + kernel_length - 1 + i]; \
      } \
      \
      /* Copy the last kernel_length-1 samples to the beginning for the next block */ \
      for (i = 0; i < kernel_length - 1; i++) { \
        buffer[real_buffer_length * j + kernel_length - 1 + i] = \
            buffer[real_buffer_length * j + buffer_length + i]; \
      } \
    } \
    \
    generated += buffer_length - kernel_length + 1; \
    dst += channels * (buffer_length - kernel_length + 1); \
    \
    /* The the first kernel_length-1 samples are there already */ \
    buffer_fill = kernel_length - 1; \
  } \
  \
  /* Write back cached buffer_fill value */ \
  self->buffer_fill = buffer_fill; \
  \
  return generated; \
}

DEFINE_FFT_PROCESS_FUNC (32, float);
DEFINE_FFT_PROCESS_FUNC (64, double);

#undef DEFINE_FFT_PROCESS_FUNC

/* 
 * The code below calculates the linear convolution:
 *
 * y[t] = \sum_{u=0}^{M-1} x[t - u] * h[u]
 *
 * where y is the output, x is the input, M is the length
 * of the filter kernel and h is the filter kernel. For x
 * holds: x[t] == 0 \forall t < 0.
 *
 * The runtime complexity of this is O (M) per sample.
 *
 */
#define DEFINE_PROCESS_FUNC(width,ctype) \
static guint \
process_##width (GstAudioFXBaseFIRFilter * self, const g##ctype * src, g##ctype * dst, guint input_samples) \
{ \
  gint kernel_length = self->kernel_length; \
  gint i, j, k, l; \
  gint channels = GST_AUDIO_FILTER_CAST (self)->format.channels; \
  gint res_start; \
  gint from_input; \
  gint off; \
  gdouble *buffer = self->buffer; \
  gdouble *kernel = self->kernel; \
  guint buffer_length = self->buffer_length; \
  \
  if (!buffer) { \
    self->buffer_length = buffer_length = kernel_length * channels; \
    self->buffer = buffer = g_new0 (gdouble, self->buffer_length); \
  } \
  \
  /* convolution */ \
  for (i = 0; i < input_samples; i++) { \
    dst[i] = 0.0; \
    k = i % channels; \
    l = i / channels; \
    from_input = MIN (l, kernel_length-1); \
    off = l * channels + k; \
    for (j = 0; j <= from_input; j++) { \
      dst[i] += src[off] * kernel[j]; \
      off -= channels; \
    } \
    /* j == from_input && off == (l - j) * channels + k */ \
    off += kernel_length * channels; \
    for (; j < kernel_length; j++) { \
      dst[i] += buffer[off] * kernel[j]; \
      off -= channels; \
    } \
  } \
  \
  /* copy the tail of the current input buffer to the residue, while \
   * keeping parts of the residue if the input buffer is smaller than \
   * the kernel length */ \
  /* from now on take kernel length as length over all channels */ \
  kernel_length *= channels; \
  if (input_samples < kernel_length) \
    res_start = kernel_length - input_samples; \
  else \
    res_start = 0; \
  \
  for (i = 0; i < res_start; i++) \
    buffer[i] = buffer[i + input_samples]; \
  /* i == res_start */ \
  for (; i < kernel_length; i++) \
    buffer[i] = src[input_samples - kernel_length + i]; \
  \
  self->buffer_fill += kernel_length - res_start; \
  if (self->buffer_fill > kernel_length) \
    self->buffer_fill = kernel_length; \
  \
  return input_samples / channels; \
}

DEFINE_PROCESS_FUNC (32, float);
DEFINE_PROCESS_FUNC (64, double);

#undef DEFINE_PROCESS_FUNC

void
gst_audio_fx_base_fir_filter_push_residue (GstAudioFXBaseFIRFilter * self)
{
  GstBuffer *outbuf;
  GstFlowReturn res;
  gint rate = GST_AUDIO_FILTER_CAST (self)->format.rate;
  gint channels = GST_AUDIO_FILTER_CAST (self)->format.channels;
  gint width = GST_AUDIO_FILTER_CAST (self)->format.width / 8;
  guint outsize, outsamples;
  guint8 *in, *out;

  if (channels == 0 || rate == 0 || self->nsamples_in == 0) {
    self->buffer_fill = 0;
    g_free (self->buffer);
    self->buffer = NULL;
    return;
  }

  /* Calculate the number of samples and their memory size that
   * should be pushed from the residue */
  outsamples = self->nsamples_in - (self->nsamples_out - self->latency);
  if (outsamples == 0) {
    self->buffer_fill = 0;
    g_free (self->buffer);
    self->buffer = NULL;
    return;
  }
  outsize = outsamples * channels * width;

  if (!self->fft) {
    gint64 diffsize, diffsamples;

    /* Process the difference between latency and residue length samples
     * to start at the actual data instead of starting at the zeros before
     * when we only got one buffer smaller than latency */
    diffsamples =
        ((gint64) self->latency) - ((gint64) self->buffer_fill) / channels;
    if (diffsamples > 0) {
      diffsize = diffsamples * channels * width;
      in = g_new0 (guint8, diffsize);
      out = g_new0 (guint8, diffsize);
      self->nsamples_out += self->process (self, in, out, diffsamples);
      g_free (in);
      g_free (out);
    }

    res = gst_pad_alloc_buffer (GST_BASE_TRANSFORM_CAST (self)->srcpad,
        GST_BUFFER_OFFSET_NONE, outsize,
        GST_PAD_CAPS (GST_BASE_TRANSFORM_CAST (self)->srcpad), &outbuf);

    if (G_UNLIKELY (res != GST_FLOW_OK)) {
      GST_WARNING_OBJECT (self, "failed allocating buffer of %d bytes",
          outsize);
      self->buffer_fill = 0;
      return;
    }

    /* Convolve the residue with zeros to get the actual remaining data */
    in = g_new0 (guint8, outsize);
    self->nsamples_out +=
        self->process (self, in, GST_BUFFER_DATA (outbuf), outsamples);
    g_free (in);
  } else {
    guint gensamples = 0;
    guint8 *data;

    outbuf = gst_buffer_new_and_alloc (outsize);
    data = GST_BUFFER_DATA (outbuf);

    while (gensamples < outsamples) {
      guint step_insamples = self->block_length - self->buffer_fill;
      guint8 *zeroes = g_new0 (guint8, step_insamples * channels * width);
      guint8 *out = g_new (guint8, self->block_length * channels * width);
      guint step_gensamples;

      step_gensamples = self->process (self, zeroes, out, step_insamples);
      g_free (zeroes);

      memcpy (data + gensamples * width, out, MIN (step_gensamples,
              outsamples - gensamples) * width);
      gensamples += MIN (step_gensamples, outsamples - gensamples);

      g_free (out);
    }
    self->nsamples_out += gensamples;
  }

  /* Set timestamp, offset, etc from the values we
   * saved when processing the regular buffers */
  if (GST_CLOCK_TIME_IS_VALID (self->start_ts))
    GST_BUFFER_TIMESTAMP (outbuf) = self->start_ts;
  else
    GST_BUFFER_TIMESTAMP (outbuf) = 0;
  GST_BUFFER_TIMESTAMP (outbuf) +=
      gst_util_uint64_scale_int (self->nsamples_out - outsamples -
      self->latency, GST_SECOND, rate);

  GST_BUFFER_DURATION (outbuf) =
      gst_util_uint64_scale_int (outsamples, GST_SECOND, rate);

  if (self->start_off != GST_BUFFER_OFFSET_NONE) {
    GST_BUFFER_OFFSET (outbuf) =
        self->start_off + self->nsamples_out - outsamples - self->latency;
    GST_BUFFER_OFFSET_END (outbuf) = GST_BUFFER_OFFSET (outbuf) + outsamples;
  }

  GST_DEBUG_OBJECT (self, "Pushing residue buffer of size %d with timestamp: %"
      GST_TIME_FORMAT ", duration: %" GST_TIME_FORMAT ", offset: %"
      G_GUINT64_FORMAT ", offset_end: %" G_GUINT64_FORMAT ", nsamples_out: %d",
      GST_BUFFER_SIZE (outbuf), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)), GST_BUFFER_OFFSET (outbuf),
      GST_BUFFER_OFFSET_END (outbuf), outsamples);

  res = gst_pad_push (GST_BASE_TRANSFORM_CAST (self)->srcpad, outbuf);

  if (G_UNLIKELY (res != GST_FLOW_OK)) {
    GST_WARNING_OBJECT (self, "failed to push residue");
  }

  self->buffer_fill = 0;
}

/* GstAudioFilter vmethod implementations */

/* get notified of caps and plug in the correct process function */
static gboolean
gst_audio_fx_base_fir_filter_setup (GstAudioFilter * base,
    GstRingBufferSpec * format)
{
  GstAudioFXBaseFIRFilter *self = GST_AUDIO_FX_BASE_FIR_FILTER (base);
  gboolean ret = TRUE;

  if (self->buffer) {
    gst_audio_fx_base_fir_filter_push_residue (self);
    g_free (self->buffer);
    self->buffer = NULL;
    self->buffer_fill = 0;
    self->buffer_length = 0;
    self->start_ts = GST_CLOCK_TIME_NONE;
    self->start_off = GST_BUFFER_OFFSET_NONE;
    self->nsamples_out = 0;
    self->nsamples_in = 0;
  }

  if (format->width == 32 && self->fft)
    self->process = (GstAudioFXBaseFIRFilterProcessFunc) process_fft_32;
  else if (format->width == 64 && self->fft)
    self->process = (GstAudioFXBaseFIRFilterProcessFunc) process_fft_64;
  else if (format->width == 32)
    self->process = (GstAudioFXBaseFIRFilterProcessFunc) process_32;
  else if (format->width == 64)
    self->process = (GstAudioFXBaseFIRFilterProcessFunc) process_64;
  ret = FALSE;

  return TRUE;
}

/* GstBaseTransform vmethod implementations */

static gboolean
gst_audio_fx_base_fir_filter_transform_size (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, guint size, GstCaps * othercaps,
    guint * othersize)
{
  GstAudioFXBaseFIRFilter *self = GST_AUDIO_FX_BASE_FIR_FILTER (base);
  guint blocklen;
  GstStructure *s;
  gint width, channels;

  if (!self->fft || direction == GST_PAD_SRC) {
    *othersize = size;
    return TRUE;
  }

  s = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (s, "width", &width) ||
      !gst_structure_get_int (s, "channels", &channels))
    return FALSE;

  width /= 8;

  size /= width * channels;

  blocklen = self->block_length - self->kernel_length + 1;
  *othersize = ((size + blocklen - 1) / blocklen) * blocklen;

  *othersize *= width * channels;

  return TRUE;
}

static GstFlowReturn
gst_audio_fx_base_fir_filter_transform (GstBaseTransform * base,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstAudioFXBaseFIRFilter *self = GST_AUDIO_FX_BASE_FIR_FILTER (base);
  GstClockTime timestamp, expected_timestamp;
  gint channels = GST_AUDIO_FILTER_CAST (self)->format.channels;
  gint rate = GST_AUDIO_FILTER_CAST (self)->format.rate;
  gint width = GST_AUDIO_FILTER_CAST (self)->format.width / 8;
  guint input_samples = (GST_BUFFER_SIZE (inbuf) / width) / channels;
  guint output_samples = (GST_BUFFER_SIZE (outbuf) / width) / channels;
  guint generated_samples;
  guint64 output_offset;
  gint64 diff = 0;

  timestamp = GST_BUFFER_TIMESTAMP (outbuf);
  if (!GST_CLOCK_TIME_IS_VALID (timestamp)
      && !GST_CLOCK_TIME_IS_VALID (self->start_ts)) {
    GST_ERROR_OBJECT (self, "Invalid timestamp");
    return GST_FLOW_ERROR;
  }

  gst_object_sync_values (G_OBJECT (self), timestamp);

  g_return_val_if_fail (self->kernel != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (channels != 0, GST_FLOW_ERROR);

  if (GST_CLOCK_TIME_IS_VALID (self->start_ts))
    expected_timestamp =
        self->start_ts + gst_util_uint64_scale_int (self->nsamples_in,
        GST_SECOND, rate);
  else
    expected_timestamp = GST_CLOCK_TIME_NONE;

  /* Reset the residue if already existing on discont buffers */
  if (GST_BUFFER_IS_DISCONT (inbuf)
      || (GST_CLOCK_TIME_IS_VALID (expected_timestamp)
          && (ABS (GST_CLOCK_DIFF (timestamp,
                      expected_timestamp) > 5 * GST_MSECOND)))) {
    GST_DEBUG_OBJECT (self, "Discontinuity detected - flushing");
    if (GST_CLOCK_TIME_IS_VALID (expected_timestamp))
      gst_audio_fx_base_fir_filter_push_residue (self);
    self->buffer_fill = 0;
    g_free (self->buffer);
    self->buffer = NULL;
    expected_timestamp = self->start_ts = timestamp;
    self->start_off = GST_BUFFER_OFFSET (inbuf);
    self->nsamples_out = 0;
    self->nsamples_in = 0;
  } else if (!GST_CLOCK_TIME_IS_VALID (self->start_ts)) {
    expected_timestamp = self->start_ts = timestamp;
    self->start_off = GST_BUFFER_OFFSET (inbuf);
  }

  self->nsamples_in += input_samples;

  generated_samples =
      self->process (self, GST_BUFFER_DATA (inbuf), GST_BUFFER_DATA (outbuf),
      input_samples);

  g_assert (generated_samples <= output_samples);
  self->nsamples_out += generated_samples;
  if (generated_samples == 0)
    return GST_BASE_TRANSFORM_FLOW_DROPPED;

  /* Calculate the number of samples we can push out now without outputting
   * latency zeros in the beginning */
  diff = ((gint64) self->nsamples_out) - ((gint64) self->latency);
  if (diff < 0) {
    return GST_BASE_TRANSFORM_FLOW_DROPPED;
  } else if (diff < generated_samples) {
    gint64 tmp = diff;
    diff = generated_samples - diff;
    generated_samples = tmp;
    GST_BUFFER_DATA (outbuf) += diff * width * channels;
  }
  GST_BUFFER_SIZE (outbuf) = generated_samples * width * channels;

  output_offset = self->nsamples_out - self->latency - generated_samples;
  GST_BUFFER_TIMESTAMP (outbuf) =
      self->start_ts + gst_util_uint64_scale_int (output_offset, GST_SECOND,
      rate);
  GST_BUFFER_DURATION (outbuf) =
      gst_util_uint64_scale_int (output_samples, GST_SECOND, rate);
  if (self->start_off != GST_BUFFER_OFFSET_NONE) {
    GST_BUFFER_OFFSET (outbuf) = self->start_off + output_offset;
    GST_BUFFER_OFFSET_END (outbuf) =
        GST_BUFFER_OFFSET (outbuf) + generated_samples;
  } else {
    GST_BUFFER_OFFSET (outbuf) = GST_BUFFER_OFFSET_NONE;
    GST_BUFFER_OFFSET_END (outbuf) = GST_BUFFER_OFFSET_NONE;
  }

  GST_DEBUG_OBJECT (self, "Pushing buffer of size %d with timestamp: %"
      GST_TIME_FORMAT ", duration: %" GST_TIME_FORMAT ", offset: %"
      G_GUINT64_FORMAT ", offset_end: %" G_GUINT64_FORMAT ", nsamples_out: %d",
      GST_BUFFER_SIZE (outbuf), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)), GST_BUFFER_OFFSET (outbuf),
      GST_BUFFER_OFFSET_END (outbuf), generated_samples);

  return GST_FLOW_OK;
}

static gboolean
gst_audio_fx_base_fir_filter_start (GstBaseTransform * base)
{
  GstAudioFXBaseFIRFilter *self = GST_AUDIO_FX_BASE_FIR_FILTER (base);

  self->buffer_fill = 0;
  g_free (self->buffer);
  self->buffer = NULL;
  self->start_ts = GST_CLOCK_TIME_NONE;
  self->start_off = GST_BUFFER_OFFSET_NONE;
  self->nsamples_out = 0;
  self->nsamples_in = 0;

  return TRUE;
}

static gboolean
gst_audio_fx_base_fir_filter_stop (GstBaseTransform * base)
{
  GstAudioFXBaseFIRFilter *self = GST_AUDIO_FX_BASE_FIR_FILTER (base);

  g_free (self->buffer);
  self->buffer = NULL;
  self->buffer_length = 0;

  return TRUE;
}

static gboolean
gst_audio_fx_base_fir_filter_query (GstPad * pad, GstQuery * query)
{
  GstAudioFXBaseFIRFilter *self =
      GST_AUDIO_FX_BASE_FIR_FILTER (gst_pad_get_parent (pad));
  gboolean res = TRUE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GstClockTime min, max;
      gboolean live;
      guint64 latency;
      GstPad *peer;
      gint rate = GST_AUDIO_FILTER (self)->format.rate;

      if (rate == 0) {
        res = FALSE;
      } else if ((peer = gst_pad_get_peer (GST_BASE_TRANSFORM (self)->sinkpad))) {
        if ((res = gst_pad_query (peer, query))) {
          gst_query_parse_latency (query, &live, &min, &max);

          GST_DEBUG_OBJECT (self, "Peer latency: min %"
              GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
              GST_TIME_ARGS (min), GST_TIME_ARGS (max));

          if (self->fft)
            latency = self->block_length - self->kernel_length + 1;
          else
            latency = self->latency;

          /* add our own latency */
          latency = gst_util_uint64_scale_round (latency, GST_SECOND, rate);

          GST_DEBUG_OBJECT (self, "Our latency: %"
              GST_TIME_FORMAT, GST_TIME_ARGS (latency));

          min += latency;
          if (max != GST_CLOCK_TIME_NONE)
            max += latency;

          GST_DEBUG_OBJECT (self, "Calculated total latency : min %"
              GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
              GST_TIME_ARGS (min), GST_TIME_ARGS (max));

          gst_query_set_latency (query, live, min, max);
        }
        gst_object_unref (peer);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
  gst_object_unref (self);
  return res;
}

static const GstQueryType *
gst_audio_fx_base_fir_filter_query_type (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_LATENCY,
    0
  };

  return types;
}

static gboolean
gst_audio_fx_base_fir_filter_event (GstBaseTransform * base, GstEvent * event)
{
  GstAudioFXBaseFIRFilter *self = GST_AUDIO_FX_BASE_FIR_FILTER (base);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      gst_audio_fx_base_fir_filter_push_residue (self);
      self->start_ts = GST_CLOCK_TIME_NONE;
      self->start_off = GST_BUFFER_OFFSET_NONE;
      self->nsamples_out = 0;
      self->nsamples_in = 0;
      break;
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->event (base, event);
}

void
gst_audio_fx_base_fir_filter_set_kernel (GstAudioFXBaseFIRFilter * self,
    gdouble * kernel, guint kernel_length, guint64 latency)
{
  gdouble *kernel_tmp;
  guint i;
  gboolean latency_changed;
  gint width;

  g_return_if_fail (kernel != NULL);
  g_return_if_fail (self != NULL);

  GST_BASE_TRANSFORM_LOCK (self);
  if (self->buffer) {
    gst_audio_fx_base_fir_filter_push_residue (self);
    self->start_ts = GST_CLOCK_TIME_NONE;
    self->start_off = GST_BUFFER_OFFSET_NONE;
    self->nsamples_out = 0;
    self->nsamples_in = 0;
    self->buffer_fill = 0;
  }

  latency_changed = (self->latency != latency
      || (self->kernel_length < FFT_THRESHOLD && kernel_length >= FFT_THRESHOLD)
      || (self->kernel_length >= FFT_THRESHOLD
          && kernel_length < FFT_THRESHOLD));

  g_free (self->kernel);
  g_free (self->buffer);
  self->buffer = NULL;
  self->buffer_fill = 0;
  self->buffer_length = 0;

  gst_fft_f64_free (self->fft);
  self->fft = NULL;
  gst_fft_f64_free (self->ifft);
  self->ifft = NULL;
  g_free (self->frequency_response);
  self->frequency_response_length = 0;
  g_free (self->fft_buffer);
  self->fft_buffer = NULL;

  self->kernel = kernel;
  self->kernel_length = kernel_length;

  if (kernel_length >= FFT_THRESHOLD) {
    /* We process 4 * kernel_length samples per pass in FFT mode */
    kernel_length = 4 * kernel_length;
    kernel_length = gst_fft_next_fast_length (kernel_length);
    self->block_length = kernel_length;

    kernel_tmp = g_new0 (gdouble, kernel_length);
    memcpy (kernel_tmp, kernel, self->kernel_length * sizeof (gdouble));

    self->fft = gst_fft_f64_new (kernel_length, FALSE);
    self->ifft = gst_fft_f64_new (kernel_length, TRUE);
    self->frequency_response_length = kernel_length / 2 + 1;
    self->frequency_response =
        g_new (GstFFTF64Complex, self->frequency_response_length);
    gst_fft_f64_fft (self->fft, kernel_tmp, self->frequency_response);
    g_free (kernel_tmp);

    /* Normalize to make sure IFFT(FFT(x)) == x */
    for (i = 0; i < self->frequency_response_length; i++) {
      self->frequency_response[i].r /= kernel_length;
      self->frequency_response[i].i /= kernel_length;
    }
  }

  width = GST_AUDIO_FILTER_CAST (self)->format.width;
  if (width == 32 && self->fft)
    self->process = (GstAudioFXBaseFIRFilterProcessFunc) process_fft_32;
  else if (width == 64 && self->fft)
    self->process = (GstAudioFXBaseFIRFilterProcessFunc) process_fft_64;
  else if (width == 32)
    self->process = (GstAudioFXBaseFIRFilterProcessFunc) process_32;
  else if (width == 64)
    self->process = (GstAudioFXBaseFIRFilterProcessFunc) process_64;

  if (latency_changed) {
    self->latency = latency;
    gst_element_post_message (GST_ELEMENT (self),
        gst_message_new_latency (GST_OBJECT (self)));
  }

  GST_BASE_TRANSFORM_UNLOCK (self);
}
