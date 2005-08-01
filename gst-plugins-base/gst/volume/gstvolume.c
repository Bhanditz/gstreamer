/* -*- c-basic-offset: 2 -*-
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
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
#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/audio/audio.h>
#include <gst/control/control.h>
#include <gst/interfaces/mixer.h>
#include "gstvolume.h"

/* some defines for audio processing */
/* the volume factor is a range from 0.0 to (arbitrary) 4.0
 * we map 1.0 to VOLUME_UNITY_INT
 */
#define VOLUME_UNITY_INT	8192    /* internal int for unity */
#define VOLUME_UNITY_BIT_SHIFT	13      /* number of bits to shift
                                           for unity */
#define VOLUME_MAX_DOUBLE	4.0
#define VOLUME_MAX_INT16	32767
#define VOLUME_MIN_INT16	-32768

/* number of steps we use for the mixer interface to go from 0.0 to 1.0 */
# define VOLUME_STEPS		100

static GstElementDetails volume_details = {
  "Volume",
  "Filter/Effect/Audio",
  "Set volume on audio/raw streams",
  "Andy Wingo <wingo@pobox.com>",
};


/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SILENT,
  PROP_MUTE,
  PROP_VOLUME
};

static GstStaticPadTemplate volume_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 32, "
        "buffer-frames = (int) [ 0, MAX]; "
        "audio/x-raw-int, "
        "channels = (int) [ 1, MAX ], "
        "rate = (int) [ 1,  MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 16, " "depth = (int) 16, " "signed = (bool) TRUE")
    );

static GstStaticPadTemplate volume_src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 32, "
        "buffer-frames = (int) [ 0, MAX]; "
        "audio/x-raw-int, "
        "channels = (int) [ 1, MAX ], "
        "rate = (int) [ 1,  MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 16, " "depth = (int) 16, " "signed = (bool) TRUE")
    );

static void gst_volume_interface_init (GstImplementsInterfaceClass * klass);
static void gst_volume_mixer_init (GstMixerClass * iface);

#define _init_interfaces(type) 							\
  {                                                                             \
    static const GInterfaceInfo voliface_info = {                               \
      (GInterfaceInitFunc) gst_volume_interface_init,                           \
      NULL,                                                                     \
      NULL                                                                      \
    };                                                                          \
    static const GInterfaceInfo volmixer_info = {                               \
      (GInterfaceInitFunc) gst_volume_mixer_init,                               \
      NULL,                                                                     \
      NULL                                                                      \
    };                                                                          \
                                                                                \
    g_type_add_interface_static (type, GST_TYPE_IMPLEMENTS_INTERFACE,           \
        &voliface_info);                                                        \
    g_type_add_interface_static (type, GST_TYPE_MIXER, &volmixer_info);         \
  }

GST_BOILERPLATE_FULL (GstVolume, gst_volume, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, _init_interfaces);


static void volume_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void volume_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void volume_update_volume (const GValue * value, gpointer data);
static void volume_update_mute (const GValue * value, gpointer data);

static GstFlowReturn volume_transform (GstBaseTransform * base,
    GstBuffer * inbuf, GstBuffer * outbuf);
static void volume_process_float (GstVolume * filter, GstClockTime tstamp,
    gpointer bytes, gint n_bytes);
static void volume_process_int16 (GstVolume * filter, GstClockTime tstamp,
    gpointer bytes, gint n_bytes);

static gboolean
gst_volume_interface_supported (GstImplementsInterface * iface, GType type)
{
  g_assert (type == GST_TYPE_MIXER);
  return TRUE;
}

static void
gst_volume_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_volume_interface_supported;
}

static const GList *
gst_volume_list_tracks (GstMixer * mixer)
{
  GstVolume *filter = GST_VOLUME (mixer);

  g_return_val_if_fail (filter != NULL, NULL);
  g_return_val_if_fail (GST_IS_VOLUME (filter), NULL);

  return filter->tracklist;
}

static void
gst_volume_set_volume (GstMixer * mixer, GstMixerTrack * track, gint * volumes)
{
  GstVolume *filter = GST_VOLUME (mixer);

  g_return_if_fail (filter != NULL);
  g_return_if_fail (GST_IS_VOLUME (filter));

  filter->volume_f = (gfloat) volumes[0] / VOLUME_STEPS;
  filter->volume_i = filter->volume_f * VOLUME_UNITY_INT;

  if (filter->mute) {
    filter->real_vol_f = 0.0;
    filter->real_vol_i = 0;
  } else {
    filter->real_vol_f = filter->volume_f;
    filter->real_vol_i = filter->volume_i;
  }
}

static void
gst_volume_get_volume (GstMixer * mixer, GstMixerTrack * track, gint * volumes)
{
  GstVolume *filter = GST_VOLUME (mixer);

  g_return_if_fail (filter != NULL);
  g_return_if_fail (GST_IS_VOLUME (filter));

  volumes[0] = (gint) filter->volume_f * VOLUME_STEPS;
}

static void
gst_volume_set_mute (GstMixer * mixer, GstMixerTrack * track, gboolean mute)
{
  GstVolume *filter = GST_VOLUME (mixer);

  g_return_if_fail (filter != NULL);
  g_return_if_fail (GST_IS_VOLUME (filter));

  filter->mute = mute;

  if (filter->mute) {
    filter->real_vol_f = 0.0;
    filter->real_vol_i = 0;
  } else {
    filter->real_vol_f = filter->volume_f;
    filter->real_vol_i = filter->volume_i;
  }
}

static void
gst_volume_mixer_init (GstMixerClass * klass)
{
  GST_MIXER_TYPE (klass) = GST_MIXER_SOFTWARE;

  /* default virtual functions */
  klass->list_tracks = gst_volume_list_tracks;
  klass->set_volume = gst_volume_set_volume;
  klass->get_volume = gst_volume_get_volume;
  klass->set_mute = gst_volume_set_mute;
}

static void
gst_volume_dispose (GObject * object)
{
  GstVolume *volume = GST_VOLUME (object);

  if (volume->tracklist) {
    if (volume->tracklist->data)
      g_object_unref (volume->tracklist->data);
    g_list_free (volume->tracklist);
    volume->tracklist = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_volume_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&volume_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&volume_sink_factory));
  gst_element_class_set_details (element_class, &volume_details);
}

static void
gst_volume_class_init (GstVolumeClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = volume_set_property;
  gobject_class->get_property = volume_get_property;
  gobject_class->dispose = gst_volume_dispose;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MUTE,
      g_param_spec_boolean ("mute", "mute", "mute", FALSE, G_PARAM_READWRITE));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_VOLUME,
      g_param_spec_double ("volume", "volume", "volume",
          0.0, VOLUME_MAX_DOUBLE, 1.0, G_PARAM_READWRITE));

  GST_BASE_TRANSFORM_CLASS (klass)->transform = volume_transform;
}

static void
gst_volume_init (GstVolume * filter)
{
  GstMixerTrack *track = NULL;

  filter->mute = FALSE;
  filter->volume_i = VOLUME_UNITY_INT;
  filter->volume_f = 1.0;
  filter->real_vol_i = VOLUME_UNITY_INT;
  filter->real_vol_f = 1.0;
  filter->tracklist = NULL;

  track = g_object_new (GST_TYPE_MIXER_TRACK, NULL);

  if (GST_IS_MIXER_TRACK (track)) {
    track->label = g_strdup ("volume");
    track->num_channels = 1;
    track->min_volume = 0;
    track->max_volume = VOLUME_STEPS;
    track->flags = GST_MIXER_TRACK_SOFTWARE;
    filter->tracklist = g_list_append (filter->tracklist, track);
  }
}

static void
volume_typefind (GstVolume * filter, const GstStructure * structure)
{
  const gchar *mimetype;

  mimetype = gst_structure_get_name (structure);

  if (strcmp (mimetype, "audio/x-raw-int") == 0)
    filter->process = volume_process_int16;
  else if (strcmp (mimetype, "audio/x-raw-float") == 0)
    filter->process = volume_process_float;
}

static GstFlowReturn
volume_transform (GstBaseTransform * base, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVolume *filter = GST_VOLUME (base);

  if (G_UNLIKELY (!filter->process)) {
    GstCaps *caps = GST_BUFFER_CAPS (inbuf);

    if (gst_caps_get_size (caps) == 1)
      volume_typefind (filter, gst_caps_get_structure (caps, 0));

    if (!filter->process) {
      GST_ELEMENT_ERROR (filter, CORE, NEGOTIATION,
          ("Invalid caps on first buffer"), NULL);
      return GST_FLOW_UNEXPECTED;
    }
  }

  filter->process (filter, GST_BUFFER_TIMESTAMP (outbuf),
      GST_BUFFER_DATA (outbuf), GST_BUFFER_SIZE (outbuf));

  return GST_FLOW_OK;
}

static void
volume_process_float (GstVolume * filter, GstClockTime tstamp,
    gpointer bytes, gint n_bytes)
{
  gfloat *data;
  gint i, num_samples;

  data = (gfloat *) bytes;
  num_samples = n_bytes / sizeof (gfloat);

  for (i = 0; i < num_samples; i++) {
    *data++ *= filter->real_vol_f;
  }
}

static void
volume_process_int16 (GstVolume * filter, GstClockTime tstamp,
    gpointer bytes, gint n_bytes)
{
  gint16 *data;
  gint i, val, num_samples;

  data = (gint16 *) bytes;
  num_samples = n_bytes / sizeof (gint16);

  /* need... liboil... */
  /* only clamp if the gain is greater than 1.0 */
  if (filter->real_vol_i > VOLUME_UNITY_INT) {
    for (i = 0; i < num_samples; i++) {
      /* we use bitshifting instead of dividing by UNITY_INT for speed */
      val = (gint) * data;
      *data++ =
          (gint16) CLAMP ((filter->real_vol_i *
              val) >> VOLUME_UNITY_BIT_SHIFT, VOLUME_MIN_INT16,
          VOLUME_MAX_INT16);
    }
  } else {
    for (i = 0; i < num_samples; i++) {
      /* we use bitshifting instead of dividing by UNITY_INT for speed */
      val = (gint) * data;
      *data++ = (gint16) ((filter->real_vol_i * val) >> VOLUME_UNITY_BIT_SHIFT);
    }
  }
}

static void
volume_update_mute (const GValue * value, gpointer data)
{
  GstVolume *filter = (GstVolume *) data;

  g_return_if_fail (GST_IS_VOLUME (filter));

  if (G_VALUE_HOLDS_BOOLEAN (value)) {
    filter->mute = g_value_get_boolean (value);
  } else if (G_VALUE_HOLDS_INT (value)) {
    filter->mute = (g_value_get_int (value) == 1);
  }

  if (filter->mute) {
    filter->real_vol_f = 0.0;
    filter->real_vol_i = 0;
  } else {
    filter->real_vol_f = filter->volume_f;
    filter->real_vol_i = filter->volume_i;
  }
}

static void
volume_update_volume (const GValue * value, gpointer data)
{
  GstVolume *filter = (GstVolume *) data;

  g_return_if_fail (GST_IS_VOLUME (filter));

  filter->volume_f = g_value_get_double (value);
  filter->volume_i = filter->volume_f * VOLUME_UNITY_INT;
  if (filter->mute) {
    filter->real_vol_f = 0.0;
    filter->real_vol_i = 0;
  } else {
    filter->real_vol_f = filter->volume_f;
    filter->real_vol_i = filter->volume_i;
  }
}

static void
volume_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstVolume *filter = GST_VOLUME (object);

  switch (prop_id) {
    case PROP_MUTE:
      volume_update_mute (value, filter);
      break;
    case PROP_VOLUME:
      volume_update_volume (value, filter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
volume_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVolume *filter = GST_VOLUME (object);

  switch (prop_id) {
    case PROP_MUTE:
      g_value_set_boolean (value, filter->mute);
      break;
    case PROP_VOLUME:
      g_value_set_double (value, filter->volume_f);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "volume", GST_RANK_NONE,
      GST_TYPE_VOLUME);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "volume",
    "element for controlling audio volume",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
