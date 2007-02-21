/* -*- c-basic-offset: 2 -*-
 * vi:si:et:sw=2:sts=8:ts=8:expandtab
 *
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2005 Andy Wingo <wingo@pobox.com>
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
 * SECTION:element-volume
 *
 * <refsect2>
 * <title>Example launch line</title>
 * <para>
 * The volume element changes the volume of the audio data.
 * </para>
 * <para>
 * <programlisting>
 * gst-launch -v -m audiotestsrc ! volume volume=0.5 ! level ! fakesink silent=TRUE
 * </programlisting>
 * This pipeline shows that the level of audiotestsrc has been halved
 * (peak values are around -6 dB and RMS around -9 dB) compared to
 * the same pipeline without the volume element.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/audio/audio.h>
#include <gst/interfaces/mixer.h>
#include <gst/controller/gstcontroller.h>
#include <liboil/liboil.h>

#include "gstvolume.h"

/* some defines for audio processing */
/* the volume factor is a range from 0.0 to (arbitrary) VOLUME_MAX_DOUBLE = 10.0
 * we map 1.0 to VOLUME_UNITY_INT16
 */
#define VOLUME_UNITY_INT16           8192       /* internal int for unity 2^13 */
#define VOLUME_UNITY_INT16_BIT_SHIFT 13 /* number of bits to shift for unity */
#define VOLUME_MAX_DOUBLE            10.0
#define VOLUME_MAX_INT16             32767
#define VOLUME_MIN_INT16             -32768

/* number of steps we use for the mixer interface to go from 0.0 to 1.0 */
# define VOLUME_STEPS           100

#define GST_CAT_DEFAULT gst_volume_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static const GstElementDetails volume_details = GST_ELEMENT_DETAILS ("Volume",
    "Filter/Effect/Audio",
    "Set volume on audio/raw streams",
    "Andy Wingo <wingo@pobox.com>");

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

static GstStaticPadTemplate volume_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) {32, 64}; "
        "audio/x-raw-int, "
        "channels = (int) [ 1, MAX ], "
        "rate = (int) [ 1,  MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 16, " "depth = (int) 16, " "signed = (bool) TRUE")
    );

static GstStaticPadTemplate volume_src_template = GST_STATIC_PAD_TEMPLATE
    ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) {32, 64}; "
        "audio/x-raw-int, "
        "channels = (int) [ 1, MAX ], "
        "rate = (int) [ 1,  MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 16, " "depth = (int) 16, " "signed = (bool) TRUE")
    );

static void gst_volume_interface_init (GstImplementsInterfaceClass * klass);
static void gst_volume_mixer_init (GstMixerClass * iface);

#define _init_interfaces(type)                                          \
  {                                                                     \
    static const GInterfaceInfo voliface_info = {                       \
      (GInterfaceInitFunc) gst_volume_interface_init,                   \
      NULL,                                                             \
      NULL                                                              \
    };                                                                  \
    static const GInterfaceInfo volmixer_info = {                       \
      (GInterfaceInitFunc) gst_volume_mixer_init,                       \
      NULL,                                                             \
      NULL                                                              \
    };                                                                  \
                                                                        \
    g_type_add_interface_static (type, GST_TYPE_IMPLEMENTS_INTERFACE,   \
        &voliface_info);                                                \
    g_type_add_interface_static (type, GST_TYPE_MIXER, &volmixer_info); \
  }

GST_BOILERPLATE_FULL (GstVolume, gst_volume, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, _init_interfaces);

static void volume_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void volume_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void volume_update_volume (const GValue * value, gpointer data);
static void volume_update_mute (const GValue * value, gpointer data);

static GstFlowReturn volume_transform_ip (GstBaseTransform * base,
    GstBuffer * outbuf);
static gboolean volume_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps);

static void volume_process_double (GstVolume * this, gpointer bytes,
    guint n_bytes);
static void volume_process_float (GstVolume * this, gpointer bytes,
    guint n_bytes);
static void volume_process_int16 (GstVolume * this, gpointer bytes,
    guint n_bytes);
static void volume_process_int16_clamp (GstVolume * this, gpointer bytes,
    guint n_bytes);

/* helper functions */

static void
volume_choose_func (GstVolume * this)
{
  this->process = NULL;
  switch (this->format) {
    case GST_VOLUME_FORMAT_INT:
      /* FIXME: should we also support gint8, gint32? */
      /* only clamp if the gain is greater than 1.0
       * FIXME: real_vol_i can change while processing the buffer!
       */
      if (this->real_vol_i > VOLUME_UNITY_INT16)
        this->process = volume_process_int16_clamp;
      else
        this->process = volume_process_int16;
      break;
    case GST_VOLUME_FORMAT_FLOAT:
      switch (this->width) {
        case 32:
          this->process = volume_process_float;
          break;
        case 64:
          this->process = volume_process_double;
          break;
      }
      break;
    default:
      break;
  }
}

static void
volume_update_real_volume (GstVolume * this)
{
  gboolean passthrough = FALSE;

  if (this->mute) {
    this->real_vol_f = 0.0;
    this->real_vol_i = 0;
  } else {
    this->real_vol_f = this->volume_f;
    this->real_vol_i = this->volume_i;
    passthrough = (this->volume_i == VOLUME_UNITY_INT16);
  }
  volume_choose_func (this);
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (this), passthrough);
}

/* Mixer interface */

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
  GstVolume *this = GST_VOLUME (mixer);

  g_return_val_if_fail (this != NULL, NULL);
  g_return_val_if_fail (GST_IS_VOLUME (this), NULL);

  return this->tracklist;
}

static void
gst_volume_set_volume (GstMixer * mixer, GstMixerTrack * track, gint * volumes)
{
  GstVolume *this = GST_VOLUME (mixer);

  g_return_if_fail (this != NULL);
  g_return_if_fail (GST_IS_VOLUME (this));

  this->volume_f = (gfloat) volumes[0] / VOLUME_STEPS;
  this->volume_i = this->volume_f * VOLUME_UNITY_INT16;

  volume_update_real_volume (this);
}

static void
gst_volume_get_volume (GstMixer * mixer, GstMixerTrack * track, gint * volumes)
{
  GstVolume *this = GST_VOLUME (mixer);

  g_return_if_fail (this != NULL);
  g_return_if_fail (GST_IS_VOLUME (this));

  volumes[0] = (gint) this->volume_f * VOLUME_STEPS;
}

static void
gst_volume_set_mute (GstMixer * mixer, GstMixerTrack * track, gboolean mute)
{
  GstVolume *this = GST_VOLUME (mixer);

  g_return_if_fail (this != NULL);
  g_return_if_fail (GST_IS_VOLUME (this));

  this->mute = mute;

  volume_update_real_volume (this);
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

/* Element class */

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
      gst_static_pad_template_get (&volume_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&volume_sink_template));
  gst_element_class_set_details (element_class, &volume_details);
}

static void
gst_volume_class_init (GstVolumeClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = volume_set_property;
  gobject_class->get_property = volume_get_property;
  gobject_class->dispose = gst_volume_dispose;

  g_object_class_install_property (gobject_class, PROP_MUTE,
      g_param_spec_boolean ("mute", "Mute", "mute channel",
          FALSE, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_VOLUME,
      g_param_spec_double ("volume", "Volume", "volume factor",
          0.0, VOLUME_MAX_DOUBLE, 1.0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "volume", 0, "Volume gain");

  trans_class->transform_ip = GST_DEBUG_FUNCPTR (volume_transform_ip);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (volume_set_caps);
}

static void
gst_volume_init (GstVolume * this, GstVolumeClass * g_class)
{
  GstMixerTrack *track = NULL;

  this->mute = FALSE;
  this->volume_i = VOLUME_UNITY_INT16;
  this->volume_f = 1.0;
  this->real_vol_i = VOLUME_UNITY_INT16;
  this->real_vol_f = 1.0;
  this->tracklist = NULL;
  this->format = GST_VOLUME_FORMAT_NONE;

  track = g_object_new (GST_TYPE_MIXER_TRACK, NULL);

  if (GST_IS_MIXER_TRACK (track)) {
    track->label = g_strdup ("volume");
    track->num_channels = 1;
    track->min_volume = 0;
    track->max_volume = VOLUME_STEPS;
    track->flags = GST_MIXER_TRACK_SOFTWARE;
    this->tracklist = g_list_append (this->tracklist, track);
  }
}

/* NOTE: although it might be tempting to have volume_process_mute() which uses
 *       memset(bytes, 0, nbytes) for the vol=0 case, this has the downside that
 *       unmuting would unly take place after processing a buffer.
 */

static void
volume_process_double (GstVolume * this, gpointer bytes, guint n_bytes)
{
  gdouble *data = (gdouble *) bytes;
  guint i, num_samples;

  num_samples = n_bytes / sizeof (gdouble);

  for (i = 0; i < num_samples; i++) {
    *data++ *= this->real_vol_f;
  }
}

static void
volume_process_float (GstVolume * this, gpointer bytes, guint n_bytes)
{
  gfloat *data = (gfloat *) bytes;
  guint i, num_samples;

  num_samples = n_bytes / sizeof (gfloat);

  for (i = 0; i < num_samples; i++) {
    *data++ *= this->real_vol_f;
  }
  /* FIXME: seems to be slower than above!
     oil_scalarmultiply_f32_ns (data, data, &this->real_vol_f, num_samples);
   */
}

static void
volume_process_int16 (GstVolume * this, gpointer bytes, guint n_bytes)
{
  gint16 *data = (gint16 *) bytes;
  guint i, num_samples;
  gint val;

  num_samples = n_bytes / sizeof (gint16);

  /* FIXME: need... liboil... 
   * oil_scalarmultiply_s16_ns ?
   * https://bugs.freedesktop.org/show_bug.cgi?id=7060
   */
  for (i = 0; i < num_samples; i++) {
    /* we use bitshifting instead of dividing by UNITY_INT for speed */
    val = (gint) * data;
    *data++ =
        (gint16) ((this->real_vol_i * val) >> VOLUME_UNITY_INT16_BIT_SHIFT);
  }
}

static void
volume_process_int16_clamp (GstVolume * this, gpointer bytes, guint n_bytes)
{
  gint16 *data = (gint16 *) bytes;
  guint i, num_samples;
  gint val;

  num_samples = n_bytes / sizeof (gint16);

  /* FIXME: need... liboil... 
   * oil_scalarmultiply_s16_ns ?
   * https://bugs.freedesktop.org/show_bug.cgi?id=7060
   */
  for (i = 0; i < num_samples; i++) {
    /* we use bitshifting instead of dividing by UNITY_INT for speed */
    val = (gint) * data;
    *data++ =
        (gint16) CLAMP ((this->real_vol_i *
            val) >> VOLUME_UNITY_INT16_BIT_SHIFT, VOLUME_MIN_INT16,
        VOLUME_MAX_INT16);
  }
}

/* GstBaseTransform vmethod implementations */

/* get notified of caps and plug in the correct process function */
static gboolean
volume_set_caps (GstBaseTransform * base, GstCaps * incaps, GstCaps * outcaps)
{
  GstVolume *this = GST_VOLUME (base);
  const gchar *mimetype;
  GstStructure *structure;

  GST_DEBUG_OBJECT (this,
      "set_caps: in %" GST_PTR_FORMAT " out %" GST_PTR_FORMAT, incaps, outcaps);

  structure = gst_caps_get_structure (incaps, 0);
  gst_structure_get_int (structure, "width", &this->width);
  mimetype = gst_structure_get_name (structure);

  /* based on mimetype, choose the correct volume_process format */
  if (strcmp (mimetype, "audio/x-raw-int") == 0) {
    this->format = GST_VOLUME_FORMAT_INT;
    GST_DEBUG_OBJECT (this, "use int: %u", this->width);
  } else if (strcmp (mimetype, "audio/x-raw-float") == 0) {
    this->format = GST_VOLUME_FORMAT_FLOAT;
    GST_DEBUG_OBJECT (this, "use float: %u", this->width);
  } else {
    this->process = NULL;
    goto invalid_format;
  }
  volume_choose_func (this);

  return TRUE;

  /* ERRORS */
invalid_format:
  {
    GST_ELEMENT_ERROR (this, CORE, NEGOTIATION,
        ("Invalid incoming caps: %" GST_PTR_FORMAT, incaps), (NULL));
    return FALSE;
  }
}

/* call the plugged-in process function for this instance
 * needs to be done with this indirection since volume_transform is
 * a class-global method
 */
static GstFlowReturn
volume_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  GstVolume *this = GST_VOLUME (base);
  GstClockTime timestamp;

  /* don't process data in passthrough-mode */
  if (gst_base_transform_is_passthrough (base))
    return GST_FLOW_OK;

  /* FIXME: if controllers are bound, subdivide GST_BUFFER_SIZE into small
   * chunks for smooth fades, what is small? 1/10th sec.
   */
  timestamp = GST_BUFFER_TIMESTAMP (outbuf);
  timestamp =
      gst_segment_to_stream_time (&base->segment, GST_FORMAT_TIME, timestamp);

  GST_DEBUG_OBJECT (base, "sync to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  if (GST_CLOCK_TIME_IS_VALID (timestamp))
    gst_object_sync_values (G_OBJECT (this), timestamp);

  this->process (this, GST_BUFFER_DATA (outbuf), GST_BUFFER_SIZE (outbuf));

  return GST_FLOW_OK;
}

static void
volume_update_mute (const GValue * value, gpointer data)
{
  GstVolume *this = (GstVolume *) data;

  g_return_if_fail (GST_IS_VOLUME (this));

  if (G_VALUE_HOLDS_BOOLEAN (value)) {
    this->mute = g_value_get_boolean (value);
  } else if (G_VALUE_HOLDS_INT (value)) {
    this->mute = (g_value_get_int (value) == 1);
  }

  volume_update_real_volume (this);
}

static void
volume_update_volume (const GValue * value, gpointer data)
{
  GstVolume *this = (GstVolume *) data;

  g_return_if_fail (GST_IS_VOLUME (this));

  this->volume_f = g_value_get_double (value);
  this->volume_i = this->volume_f * VOLUME_UNITY_INT16;

  volume_update_real_volume (this);
}

static void
volume_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstVolume *this = GST_VOLUME (object);

  switch (prop_id) {
    case PROP_MUTE:
      volume_update_mute (value, this);
      break;
    case PROP_VOLUME:
      volume_update_volume (value, this);
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
  GstVolume *this = GST_VOLUME (object);

  switch (prop_id) {
    case PROP_MUTE:
      g_value_set_boolean (value, this->mute);
      break;
    case PROP_VOLUME:
      g_value_set_double (value, this->volume_f);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  /*oil_init (); */

  /* initialize gst controller library */
  gst_controller_init (NULL, NULL);

  return gst_element_register (plugin, "volume", GST_RANK_NONE,
      GST_TYPE_VOLUME);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "volume",
    "plugin for controlling audio volume",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
