/* GStreamer
 * Copyright (C) 2005 Wim Taymans <wim@fluendo.com>
 *
 * gstalsasink.c: 
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
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <alsa/asoundlib.h>


#include "gstalsasink.h"

/* elementfactory information */
static GstElementDetails gst_alsasink_details =
GST_ELEMENT_DETAILS ("Audio Sink (ALSA)",
    "Sink/Audio",
    "Output to a sound card via ALSA",
    "Wim Taymans <wim@fluendo.com>");

static void gst_alsasink_base_init (gpointer g_class);
static void gst_alsasink_class_init (GstAlsaSinkClass * klass);
static void gst_alsasink_init (GstAlsaSink * alsasink);
static void gst_alsasink_dispose (GObject * object);

static GstCaps *gst_alsasink_getcaps (GstBaseSink * bsink);

static gboolean gst_alsasink_open (GstAudioSink * asink,
    GstRingBufferSpec * spec);
static gboolean gst_alsasink_close (GstAudioSink * asink);
static guint gst_alsasink_write (GstAudioSink * asink, gpointer data,
    guint length);
static guint gst_alsasink_delay (GstAudioSink * asink);
static void gst_alsasink_reset (GstAudioSink * asink);

/* AlsaSink signals and args */
enum
{
  LAST_SIGNAL
};

static GstStaticPadTemplate alsasink_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        //"endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, "
        //"signed = (boolean) { TRUE, FALSE }, "
        "endianness = (int) LITTLE_ENDIAN, "
        "signed = (boolean) TRUE, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]; "
        "audio/x-raw-int, "
        "signed = (boolean) { TRUE, FALSE }, "
        "width = (int) 8, "
        "depth = (int) 8, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]")
    );

static GstElementClass *parent_class = NULL;

/* static guint gst_alsasink_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_alsasink_get_type (void)
{
  static GType alsasink_type = 0;

  if (!alsasink_type) {
    static const GTypeInfo alsasink_info = {
      sizeof (GstAlsaSinkClass),
      gst_alsasink_base_init,
      NULL,
      (GClassInitFunc) gst_alsasink_class_init,
      NULL,
      NULL,
      sizeof (GstAlsaSink),
      0,
      (GInstanceInitFunc) gst_alsasink_init,
    };

    alsasink_type =
        g_type_register_static (GST_TYPE_AUDIOSINK, "GstAlsaSink",
        &alsasink_info, 0);
  }

  return alsasink_type;
}

static void
gst_alsasink_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_alsasink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_alsasink_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&alsasink_sink_factory));
}
static void
gst_alsasink_class_init (GstAlsaSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstBaseAudioSinkClass *gstbaseaudiosink_class;
  GstAudioSinkClass *gstaudiosink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstbaseaudiosink_class = (GstBaseAudioSinkClass *) klass;
  gstaudiosink_class = (GstAudioSinkClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASEAUDIOSINK);

  gobject_class->dispose = gst_alsasink_dispose;

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_alsasink_getcaps);

  gstaudiosink_class->open = GST_DEBUG_FUNCPTR (gst_alsasink_open);
  gstaudiosink_class->close = GST_DEBUG_FUNCPTR (gst_alsasink_close);
  gstaudiosink_class->write = GST_DEBUG_FUNCPTR (gst_alsasink_write);
  gstaudiosink_class->delay = GST_DEBUG_FUNCPTR (gst_alsasink_delay);
  gstaudiosink_class->reset = GST_DEBUG_FUNCPTR (gst_alsasink_reset);
}

static void
gst_alsasink_init (GstAlsaSink * alsasink)
{
  GST_DEBUG ("initializing alsasink");

  alsasink->device = g_strdup ("default");
}

static GstCaps *
gst_alsasink_getcaps (GstBaseSink * bsink)
{
  return NULL;
}

#define CHECK(call, error) \
G_STMT_START {			\
if ((err = call) < 0)		\
  goto error;			\
} G_STMT_END;

static int
set_hwparams (GstAlsaSink * alsa)
{
  guint rrate;
  gint err, dir;
  snd_pcm_hw_params_t *params;

  snd_pcm_hw_params_alloca (&params);

  /* choose all parameters */
  CHECK (snd_pcm_hw_params_any (alsa->handle, params), no_config);
  /* set the interleaved read/write format */
  CHECK (snd_pcm_hw_params_set_access (alsa->handle, params, alsa->access),
      wrong_access);
  /* set the sample format */
  CHECK (snd_pcm_hw_params_set_format (alsa->handle, params, alsa->format),
      no_sample_format);
  /* set the count of channels */
  CHECK (snd_pcm_hw_params_set_channels (alsa->handle, params, alsa->channels),
      no_channels);
  /* set the stream rate */
  rrate = alsa->rate;
  CHECK (snd_pcm_hw_params_set_rate_near (alsa->handle, params, &rrate, 0),
      no_rate);
  if (rrate != alsa->rate)
    goto rate_match;

  if (alsa->buffer_time != -1) {
    /* set the buffer time */
    CHECK (snd_pcm_hw_params_set_buffer_time_near (alsa->handle, params,
            &alsa->buffer_time, &dir), buffer_time);
  }
  CHECK (snd_pcm_hw_params_get_buffer_size (params, &alsa->buffer_size),
      buffer_size);

  if (alsa->period_time != -1) {
    /* set the period time */
    CHECK (snd_pcm_hw_params_set_period_time_near (alsa->handle, params,
            &alsa->period_time, &dir), period_time);
  }
  CHECK (snd_pcm_hw_params_get_period_size (params, &alsa->period_size, &dir),
      period_size);

  /* write the parameters to device */
  CHECK (snd_pcm_hw_params (alsa->handle, params), set_hw_params);

  return 0;

  /* ERRORS */
no_config:
  {
    GST_ELEMENT_ERROR (alsa, RESOURCE, OPEN_READ,
        ("Broken configuration for playback: no configurations available: %s",
            snd_strerror (err)), (NULL));
    return err;
  }
wrong_access:
  {
    GST_ELEMENT_ERROR (alsa, RESOURCE, OPEN_READ,
        ("Access type not available for playback: %s", snd_strerror (err)),
        (NULL));
    return err;
  }
no_sample_format:
  {
    GST_ELEMENT_ERROR (alsa, RESOURCE, OPEN_READ,
        ("Sample format not available for playback: %s", snd_strerror (err)),
        (NULL));
    return err;
  }
no_channels:
  {
    GST_ELEMENT_ERROR (alsa, RESOURCE, OPEN_READ,
        ("Channels count (%i) not available for playbacks: %s",
            alsa->channels, snd_strerror (err)), (NULL));
    return err;
  }
no_rate:
  {
    GST_ELEMENT_ERROR (alsa, RESOURCE, OPEN_READ,
        ("Rate %iHz not available for playback: %s",
            alsa->rate, snd_strerror (err)), (NULL));
    return err;
  }
rate_match:
  {
    GST_ELEMENT_ERROR (alsa, RESOURCE, OPEN_READ,
        ("Rate doesn't match (requested %iHz, get %iHz)",
            alsa->rate, err), (NULL));
    return -EINVAL;
  }
buffer_time:
  {
    GST_ELEMENT_ERROR (alsa, RESOURCE, OPEN_READ,
        ("Unable to set buffer time %i for playback: %s",
            alsa->buffer_time, snd_strerror (err)), (NULL));
    return err;
  }
buffer_size:
  {
    GST_ELEMENT_ERROR (alsa, RESOURCE, OPEN_READ,
        ("Unable to get buffer size for playback: %s", snd_strerror (err)),
        (NULL));
    return err;
  }
period_time:
  {
    GST_ELEMENT_ERROR (alsa, RESOURCE, OPEN_READ,
        ("Unable to set period time %i for playback: %s", alsa->period_time,
            snd_strerror (err)), (NULL));
    return err;
  }
period_size:
  {
    GST_ELEMENT_ERROR (alsa, RESOURCE, OPEN_READ,
        ("Unable to get period size for playback: %s", snd_strerror (err)),
        (NULL));
    return err;
  }
set_hw_params:
  {
    GST_ELEMENT_ERROR (alsa, RESOURCE, OPEN_READ,
        ("Unable to set hw params for playback: %s", snd_strerror (err)),
        (NULL));
    return err;
  }
}

static int
set_swparams (GstAlsaSink * alsa)
{
  int err;
  snd_pcm_sw_params_t *params;

  snd_pcm_sw_params_alloca (&params);

  /* get the current swparams */
  CHECK (snd_pcm_sw_params_current (alsa->handle, params), no_config);
  /* start the transfer when the buffer is almost full: */
  /* (buffer_size / avail_min) * avail_min */
  CHECK (snd_pcm_sw_params_set_start_threshold (alsa->handle, params,
          (alsa->buffer_size / alsa->period_size) * alsa->period_size),
      start_threshold);

  /* allow the transfer when at least period_size samples can be processed */
  CHECK (snd_pcm_sw_params_set_avail_min (alsa->handle, params,
          alsa->period_size), set_avail);
  /* align all transfers to 1 sample */
  CHECK (snd_pcm_sw_params_set_xfer_align (alsa->handle, params, 1), set_align);

  /* write the parameters to the playback device */
  CHECK (snd_pcm_sw_params (alsa->handle, params), set_sw_params);

  return 0;

  /* ERRORS */
no_config:
  {
    GST_ELEMENT_ERROR (alsa, RESOURCE, OPEN_READ,
        ("Unable to determine current swparams for playback: %s",
            snd_strerror (err)), (NULL));
    return err;
  }
start_threshold:
  {
    GST_ELEMENT_ERROR (alsa, RESOURCE, OPEN_READ,
        ("Unable to set start threshold mode for playback: %s",
            snd_strerror (err)), (NULL));
    return err;
  }
set_avail:
  {
    GST_ELEMENT_ERROR (alsa, RESOURCE, OPEN_READ,
        ("Unable to set avail min for playback: %s", snd_strerror (err)),
        (NULL));
    return err;
  }
set_align:
  {
    GST_ELEMENT_ERROR (alsa, RESOURCE, OPEN_READ,
        ("Unable to set transfer align for playback: %s", snd_strerror (err)),
        (NULL));
    return err;
  }
set_sw_params:
  {
    GST_ELEMENT_ERROR (alsa, RESOURCE, OPEN_READ,
        ("Unable to set sw params for playback: %s", snd_strerror (err)),
        (NULL));
    return err;
  }
}

static gboolean
alsasink_parse_spec (GstAlsaSink * alsa, GstRingBufferSpec * spec)
{
  switch (spec->type) {
    case GST_BUFTYPE_LINEAR:
      alsa->format = snd_pcm_build_linear_format (spec->depth, spec->width,
          spec->sign ? 0 : 1, spec->bigend ? 1 : 0);
      break;
    case GST_BUFTYPE_FLOAT:
      switch (spec->format) {
        case GST_FLOAT32_LE:
          alsa->format = SND_PCM_FORMAT_FLOAT_LE;
          break;
        case GST_FLOAT32_BE:
          alsa->format = SND_PCM_FORMAT_FLOAT_BE;
          break;
        case GST_FLOAT64_LE:
          alsa->format = SND_PCM_FORMAT_FLOAT64_LE;
          break;
        case GST_FLOAT64_BE:
          alsa->format = SND_PCM_FORMAT_FLOAT64_BE;
          break;
        default:
          goto error;
      }
      break;
    case GST_BUFTYPE_A_LAW:
      alsa->format = SND_PCM_FORMAT_A_LAW;
      break;
    case GST_BUFTYPE_MU_LAW:
      alsa->format = SND_PCM_FORMAT_MU_LAW;
      break;
    default:
      goto error;

  }
  alsa->rate = spec->rate;
  alsa->channels = spec->channels;
  alsa->buffer_time = spec->buffer_time;
  alsa->period_time = spec->latency_time;
  alsa->access = SND_PCM_ACCESS_RW_INTERLEAVED;

  return TRUE;

  /* ERRORS */
error:
  {
    return FALSE;
  }
}

static gboolean
gst_alsasink_open (GstAudioSink * asink, GstRingBufferSpec * spec)
{
  GstAlsaSink *alsa;
  gint err;

  alsa = GST_ALSA_SINK (asink);

  if (!alsasink_parse_spec (alsa, spec))
    goto spec_parse;

  CHECK (snd_pcm_open (&alsa->handle, alsa->device, SND_PCM_STREAM_PLAYBACK, 0),
      open_error);

  CHECK (set_hwparams (alsa), hw_params_failed);
  CHECK (set_swparams (alsa), sw_params_failed);

  spec->bytes_per_sample = 4;
  alsa->bytes_per_sample = 4;
  spec->segsize = alsa->period_size * 4;
  spec->segtotal = alsa->buffer_size / alsa->period_size;
  spec->silence_sample[0] = 0;
  spec->silence_sample[1] = 0;
  spec->silence_sample[2] = 0;
  spec->silence_sample[3] = 0;

  return TRUE;

  /* ERRORS */
spec_parse:
  {
    GST_ELEMENT_ERROR (alsa, RESOURCE, OPEN_READ,
        ("Error parsing spec"), (NULL));
    return FALSE;
  }
open_error:
  {
    GST_ELEMENT_ERROR (alsa, RESOURCE, OPEN_READ,
        ("Playback open error: %s", snd_strerror (err)), (NULL));
    return FALSE;
  }
hw_params_failed:
  {
    GST_ELEMENT_ERROR (alsa, RESOURCE, OPEN_READ,
        ("Setting of hwparams failed: %s", snd_strerror (err)), (NULL));
    return FALSE;
  }
sw_params_failed:
  {
    GST_ELEMENT_ERROR (alsa, RESOURCE, OPEN_READ,
        ("Setting of swparams failed: %s", snd_strerror (err)), (NULL));
    return FALSE;
  }
}

static gboolean
gst_alsasink_close (GstAudioSink * asink)
{
  GstAlsaSink *alsa;

  alsa = GST_ALSA_SINK (asink);

  snd_pcm_close (alsa->handle);

  return TRUE;
}


/*
 *   Underrun and suspend recovery
 */
static gint
xrun_recovery (snd_pcm_t * handle, gint err)
{
  GST_DEBUG ("xrun recovery %d", err);

  if (err == -EPIPE) {          /* under-run */
    err = snd_pcm_prepare (handle);
    if (err < 0)
      GST_WARNING ("Can't recovery from underrun, prepare failed: %s",
          snd_strerror (err));
    return 0;
  } else if (err == -ESTRPIPE) {
    while ((err = snd_pcm_resume (handle)) == -EAGAIN)
      g_usleep (100);           /* wait until the suspend flag is released */

    if (err < 0) {
      err = snd_pcm_prepare (handle);
      if (err < 0)
        GST_WARNING ("Can't recovery from suspend, prepare failed: %s",
            snd_strerror (err));
    }
    return 0;
  }
  return err;
}

static guint
gst_alsasink_write (GstAudioSink * asink, gpointer data, guint length)
{
  GstAlsaSink *alsa;
  gint err;
  gint cptr;
  gint16 *ptr;

  alsa = GST_ALSA_SINK (asink);

  cptr = length / alsa->bytes_per_sample;
  ptr = data;

  while (cptr > 0) {
    err = snd_pcm_writei (alsa->handle, ptr, cptr);

    if (err < 0) {
      if (err == -EAGAIN) {
        GST_DEBUG ("Write error: %s", snd_strerror (err));
        continue;
      } else if (xrun_recovery (alsa->handle, err) < 0) {
        goto write_error;
      }
      continue;
    }

    ptr += err * alsa->channels;
    cptr -= err;
  }

  return length - cptr;

write_error:
  {
    return length;              /* skip one period */
  }
}

static guint
gst_alsasink_delay (GstAudioSink * asink)
{
  GstAlsaSink *alsa;
  snd_pcm_sframes_t delay;

  alsa = GST_ALSA_SINK (asink);

  snd_pcm_delay (alsa->handle, &delay);

  return delay;
}

static void
gst_alsasink_reset (GstAudioSink * asink)
{

#if 0
  GstAlsaSink *alsa;
  gint err;

  alsa = GST_ALSA_SINK (asink);

  CHECK (snd_pcm_drop (alsa->handle), drop_error);
  CHECK (snd_pcm_prepare (alsa->handle), prepare_error);

  return;

  /* ERRORS */
drop_error:
  {
    GST_ELEMENT_ERROR (alsa, RESOURCE, OPEN_READ,
        ("alsa-reset: pcm drop error: %s", snd_strerror (err)), (NULL));
    return;
  }
prepare_error:
  {
    GST_ELEMENT_ERROR (alsa, RESOURCE, OPEN_READ,
        ("alsa-reset: pcm prepare error: %s", snd_strerror (err)), (NULL));
    return;
  }
#endif
}
