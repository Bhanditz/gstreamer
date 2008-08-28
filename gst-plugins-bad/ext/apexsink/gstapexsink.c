/* GStreamer - AirPort Express Audio Sink -
 *
 * Remote Audio Access Protocol (RAOP) as used in Apple iTunes to stream music to the Airport Express (ApEx) -
 * RAOP is based on the Real Time Streaming Protocol (RTSP) but with an extra challenge-response RSA based authentication step.
 *
 * RAW PCM input only as defined by the following GST_STATIC_PAD_TEMPLATE
 *
 * Copyright (C) 2008 Jérémie Bernard [GRemi] <gremimail@gmail.com>
 *
 * gstapexsink.c 
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

#include "gstapexsink.h"

GST_DEBUG_CATEGORY_STATIC (apexsink_debug);
#define GST_CAT_DEFAULT apexsink_debug

static GstStaticPadTemplate gst_apexsink_sink_factory = GST_STATIC_PAD_TEMPLATE
    ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    (GST_APEX_RAOP_INPUT_TYPE ","
        "width = (int) " GST_APEX_RAOP_INPUT_WIDTH ","
        "depth = (int) " GST_APEX_RAOP_INPUT_DEPTH ","
        "endianness = (int) " GST_APEX_RAOP_INPUT_ENDIAN ","
        "channels = (int) " GST_APEX_RAOP_INPUT_CHANNELS ","
        "rate = (int) " GST_APEX_RAOP_INPUT_BIT_RATE ","
        "signed = (boolean) " GST_APEX_RAOP_INPUT_SIGNED)
    );


enum
{
  APEX_PROP_HOST = 1,
  APEX_PROP_PORT,
  APEX_PROP_VOLUME,
  APEX_PROP_JACK_TYPE,
  APEX_PROP_JACK_STATUS,
};

#define DEFAULT_APEX_HOST		""
#define DEFAULT_APEX_PORT		5000
#define DEFAULT_APEX_VOLUME		75
#define DEFAULT_APEX_JACK_TYPE		GST_APEX_JACK_TYPE_UNDEFINED
#define DEFAULT_APEX_JACK_STATUS	GST_APEX_JACK_STATUS_UNDEFINED

/* genum apex jack resolution */
GType
gst_apexsink_jackstatus_get_type (void)
{
  static GType jackstatus_type = 0;
  static GEnumValue jackstatus[] = {
    {GST_APEX_JACK_STATUS_UNDEFINED, "GST_APEX_JACK_STATUS_UNDEFINED",
        "Jack status undefined"},
    {GST_APEX_JACK_STATUS_DISCONNECTED, "GST_APEX_JACK_STATUS_DISCONNECTED",
        "Jack disconnected"},
    {GST_APEX_JACK_STATUS_CONNECTED, "GST_APEX_JACK_STATUS_CONNECTED",
        "Jack connected"},
    {0, NULL, NULL},
  };

  if (!jackstatus_type) {
    jackstatus_type = g_enum_register_static ("GstApExJackStatus", jackstatus);
  }

  return jackstatus_type;
}

GType
gst_apexsink_jacktype_get_type (void)
{
  static GType jacktype_type = 0;
  static GEnumValue jacktype[] = {
    {GST_APEX_JACK_TYPE_UNDEFINED, "GST_APEX_JACK_TYPE_UNDEFINED",
        "Undefined jack type"},
    {GST_APEX_JACK_TYPE_ANALOG, "GST_APEX_JACK_TYPE_ANALOG", "Analog jack"},
    {GST_APEX_JACK_TYPE_DIGITAL, "GST_APEX_JACK_TYPE_DIGITAL", "Digital jack"},
    {0, NULL, NULL},
  };

  if (!jacktype_type) {
    jacktype_type = g_enum_register_static ("GstApExJackType", jacktype);
  }

  return jacktype_type;
}


static void gst_apexsink_base_init (gpointer g_class);
static void gst_apexsink_class_init (GstApExSinkClass * klass);
static void gst_apexsink_init (GstApExSink * apexsink,
    GstApExSinkClass * g_class);

static void gst_apexsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_apexsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_apexsink_finalise (GObject * object);

static gboolean gst_apexsink_open (GstAudioSink * asink);
static gboolean gst_apexsink_prepare (GstAudioSink * asink,
    GstRingBufferSpec * spec);
static guint gst_apexsink_write (GstAudioSink * asink, gpointer data,
    guint length);
static gboolean gst_apexsink_unprepare (GstAudioSink * asink);
static guint gst_apexsink_delay (GstAudioSink * asink);
static void gst_apexsink_reset (GstAudioSink * asink);
static gboolean gst_apexsink_close (GstAudioSink * asink);

/* mixer interface standard api */
static void gst_apexsink_interfaces_init (GType type);
static void gst_apexsink_implements_interface_init (GstImplementsInterfaceClass
    * iface);
static void gst_apexsink_mixer_interface_init (GstMixerClass * iface);

static gboolean gst_apexsink_interface_supported (GstImplementsInterface *
    iface, GType iface_type);
static const GList *gst_apexsink_mixer_list_tracks (GstMixer * mixer);
static void gst_apexsink_mixer_set_volume (GstMixer * mixer,
    GstMixerTrack * track, gint * volumes);
static void gst_apexsink_mixer_get_volume (GstMixer * mixer,
    GstMixerTrack * track, gint * volumes);

GST_BOILERPLATE_FULL (GstApExSink, gst_apexsink, GstAudioSink,
    GST_TYPE_AUDIO_SINK, gst_apexsink_interfaces_init);

/* apex sink interface(s) stuff */
static void
gst_apexsink_interfaces_init (GType type)
{
  static const GInterfaceInfo implements_interface_info =
      { (GInterfaceInitFunc) gst_apexsink_implements_interface_init, NULL,
    NULL
  };
  static const GInterfaceInfo mixer_interface_info =
      { (GInterfaceInitFunc) gst_apexsink_mixer_interface_init, NULL, NULL };

  g_type_add_interface_static (type, GST_TYPE_IMPLEMENTS_INTERFACE,
      &implements_interface_info);
  g_type_add_interface_static (type, GST_TYPE_MIXER, &mixer_interface_info);
}

static void
gst_apexsink_implements_interface_init (GstImplementsInterfaceClass * iface)
{
  iface->supported = gst_apexsink_interface_supported;
}

static void
gst_apexsink_mixer_interface_init (GstMixerClass * iface)
{
  GST_MIXER_TYPE (iface) = GST_MIXER_SOFTWARE;

  iface->list_tracks = gst_apexsink_mixer_list_tracks;
  iface->set_volume = gst_apexsink_mixer_set_volume;
  iface->get_volume = gst_apexsink_mixer_get_volume;
}

static gboolean
gst_apexsink_interface_supported (GstImplementsInterface * iface,
    GType iface_type)
{
  g_return_val_if_fail (iface_type == GST_TYPE_MIXER, FALSE);

  return TRUE;
}

static const GList *
gst_apexsink_mixer_list_tracks (GstMixer * mixer)
{
  GstApExSink *apexsink = GST_APEX_SINK (mixer);

  return apexsink->tracks;
}

static void
gst_apexsink_mixer_set_volume (GstMixer * mixer, GstMixerTrack * track,
    gint * volumes)
{
  GstApExSink *apexsink = GST_APEX_SINK (mixer);

  apexsink->volume = volumes[0];

  if (apexsink->gst_apexraop != NULL)
    gst_apexraop_set_volume (apexsink->gst_apexraop, apexsink->volume);
}

static void
gst_apexsink_mixer_get_volume (GstMixer * mixer, GstMixerTrack * track,
    gint * volumes)
{
  GstApExSink *apexsink = GST_APEX_SINK (mixer);

  volumes[0] = apexsink->volume;
}

/* sink base init */
static void
gst_apexsink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "Apple AirPort Express Audio Sink", "Sink/Audio/Wireless",
      "Output stream to an AirPort Express",
      "Jérémie Bernard [GRemi] <gremimail@gmail.com>");
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_apexsink_sink_factory));
}

/* sink class init */
static void
gst_apexsink_class_init (GstApExSinkClass * klass)
{
  GST_DEBUG_CATEGORY_INIT (apexsink_debug, GST_APEX_SINK_NAME, 0,
      "AirPort Express sink");

  parent_class = g_type_class_peek_parent (klass);

  ((GObjectClass *) klass)->get_property =
      GST_DEBUG_FUNCPTR (gst_apexsink_get_property);
  ((GObjectClass *) klass)->set_property =
      GST_DEBUG_FUNCPTR (gst_apexsink_set_property);
  ((GObjectClass *) klass)->finalize =
      GST_DEBUG_FUNCPTR (gst_apexsink_finalise);

  ((GstAudioSinkClass *) klass)->open = GST_DEBUG_FUNCPTR (gst_apexsink_open);
  ((GstAudioSinkClass *) klass)->prepare =
      GST_DEBUG_FUNCPTR (gst_apexsink_prepare);
  ((GstAudioSinkClass *) klass)->write = GST_DEBUG_FUNCPTR (gst_apexsink_write);
  ((GstAudioSinkClass *) klass)->unprepare =
      GST_DEBUG_FUNCPTR (gst_apexsink_unprepare);
  ((GstAudioSinkClass *) klass)->delay = GST_DEBUG_FUNCPTR (gst_apexsink_delay);
  ((GstAudioSinkClass *) klass)->reset = GST_DEBUG_FUNCPTR (gst_apexsink_reset);
  ((GstAudioSinkClass *) klass)->close = GST_DEBUG_FUNCPTR (gst_apexsink_close);

  g_object_class_install_property ((GObjectClass *) klass, APEX_PROP_HOST,
      g_param_spec_string ("host", "Host", "AirPort Express target host",
          DEFAULT_APEX_HOST, G_PARAM_READWRITE));
  g_object_class_install_property ((GObjectClass *) klass, APEX_PROP_PORT,
      g_param_spec_uint ("port", "Port", "AirPort Express target port", 0,
          32000, DEFAULT_APEX_PORT, G_PARAM_READWRITE));
  g_object_class_install_property ((GObjectClass *) klass, APEX_PROP_VOLUME,
      g_param_spec_uint ("volume", "Volume", "AirPort Express target volume", 0,
          100, DEFAULT_APEX_VOLUME, G_PARAM_READWRITE));
  g_object_class_install_property ((GObjectClass *) klass, APEX_PROP_JACK_TYPE,
      g_param_spec_enum ("jack_type", "Jack Type",
          "AirPort Express connected jack type", GST_APEX_SINK_JACKTYPE_TYPE,
          DEFAULT_APEX_JACK_TYPE, G_PARAM_READABLE));
  g_object_class_install_property ((GObjectClass *) klass,
      APEX_PROP_JACK_STATUS, g_param_spec_enum ("jack_status", "Jack Status",
          "AirPort Express jack connection status",
          GST_APEX_SINK_JACKSTATUS_TYPE, DEFAULT_APEX_JACK_STATUS,
          G_PARAM_READABLE));
}

/* sink plugin instance init */
static void
gst_apexsink_init (GstApExSink * apexsink, GstApExSinkClass * g_class)
{
  GstMixerTrack *track = NULL;

  track = g_object_new (GST_TYPE_MIXER_TRACK, NULL);
  track->label = g_strdup ("Airport Express");
  track->num_channels = GST_APEX_RAOP_CHANNELS;
  track->min_volume = 0;
  track->max_volume = 100;
  track->flags = GST_MIXER_TRACK_OUTPUT;

  apexsink->host = g_strdup (DEFAULT_APEX_HOST);
  apexsink->port = DEFAULT_APEX_PORT;
  apexsink->volume = DEFAULT_APEX_VOLUME;
  apexsink->gst_apexraop = NULL;
  apexsink->tracks = g_list_append (apexsink->tracks, track);

  GST_INFO_OBJECT (apexsink,
      "ApEx sink default initialization, target=\"%s\", port=\"%d\", volume=\"%d\%\"",
      apexsink->host, apexsink->port, apexsink->volume);
}

/* apex sink set property */
static void
gst_apexsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstApExSink *sink = GST_APEX_SINK (object);

  switch (prop_id) {
    case APEX_PROP_HOST:
    {
      if (sink->gst_apexraop == NULL) {
        g_free (sink->host);
        sink->host = g_value_dup_string (value);

        GST_INFO_OBJECT (sink, "ApEx sink target set to \"%s\"", sink->host);
      } else
        G_OBJECT_WARN_INVALID_PSPEC (object, "host", prop_id, pspec);
    }
      break;
    case APEX_PROP_PORT:
    {
      if (sink->gst_apexraop == NULL) {
        sink->port = g_value_get_uint (value);

        GST_INFO_OBJECT (sink, "ApEx port set to \"%d\"", sink->port);
      } else
        G_OBJECT_WARN_INVALID_PSPEC (object, "port", prop_id, pspec);
    }
      break;
    case APEX_PROP_VOLUME:
    {
      sink->volume = g_value_get_uint (value);

      if (sink->gst_apexraop != NULL)
        gst_apexraop_set_volume (sink->gst_apexraop, sink->volume);

      GST_INFO_OBJECT (sink, "ApEx volume set to \"%d\%\"", sink->volume);
    }
      break;
    default:
    {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
      break;
  }
}

/* apex sink get property */
static void
gst_apexsink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstApExSink *sink = GST_APEX_SINK (object);

  switch (prop_id) {
    case APEX_PROP_HOST:
    {
      g_value_set_string (value, sink->host);
    }
      break;
    case APEX_PROP_PORT:
    {
      g_value_set_uint (value, sink->port);
    }
      break;
    case APEX_PROP_VOLUME:
    {
      g_value_set_uint (value, sink->volume);
    }
      break;
    case APEX_PROP_JACK_TYPE:
    {
      g_value_set_enum (value, gst_apexraop_get_jacktype (sink->gst_apexraop));
    }
      break;
    case APEX_PROP_JACK_STATUS:
    {
      g_value_set_enum (value,
          gst_apexraop_get_jackstatus (sink->gst_apexraop));
    }
      break;
    default:
    {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
      break;
  }
}

/* apex sink finalize */
static void
gst_apexsink_finalise (GObject * object)
{
  GstApExSink *sink = GST_APEX_SINK (object);

  if (sink->tracks) {
    g_list_foreach (sink->tracks, (GFunc) g_object_unref, NULL);
    g_list_free (sink->tracks);
    sink->tracks = NULL;
  }

  g_free (sink->host);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* sink open : open the device */
static gboolean
gst_apexsink_open (GstAudioSink * asink)
{
  int res;
  GstApExSink *apexsink = (GstApExSink *) asink;

  apexsink->gst_apexraop = gst_apexraop_new (apexsink->host, apexsink->port);

  if ((res = gst_apexraop_connect (apexsink->gst_apexraop)) != GST_RTSP_STS_OK) {
    GST_ERROR_OBJECT (apexsink,
        "%s : network or RAOP failure, connection refused or timeout, RTSP code=%d",
        apexsink->host, res);
    return FALSE;
  }

  GST_INFO_OBJECT (apexsink,
      "OPEN : ApEx sink successfully connected to \"%s:%d\", ANNOUNCE, SETUP and RECORD requests performed",
      apexsink->host, apexsink->port);

  switch (gst_apexraop_get_jackstatus (apexsink->gst_apexraop)) {
    case GST_APEX_JACK_STATUS_CONNECTED:
    {
      GST_INFO_OBJECT (apexsink, "OPEN : ApEx jack is connected");
    }
      break;
    case GST_APEX_JACK_STATUS_DISCONNECTED:
    {
      GST_WARNING_OBJECT (apexsink, "OPEN : ApEx jack is disconnected !");
    }
      break;
    default:
    {
      GST_WARNING_OBJECT (apexsink, "OPEN : ApEx jack status is undefined !");
    }
      break;
  }

  switch (gst_apexraop_get_jacktype (apexsink->gst_apexraop)) {
    case GST_APEX_JACK_TYPE_ANALOG:
    {
      GST_INFO_OBJECT (apexsink, "OPEN : ApEx jack type is analog");
    }
      break;
    case GST_APEX_JACK_TYPE_DIGITAL:
    {
      GST_INFO_OBJECT (apexsink, "OPEN : ApEx jack type is digital");
    }
      break;
    default:
    {
      GST_WARNING_OBJECT (apexsink, "OPEN : ApEx jack type is undefined !");
    }
      break;
  }

  if ((res =
          gst_apexraop_set_volume (apexsink->gst_apexraop,
              apexsink->volume)) != GST_RTSP_STS_OK) {
    GST_WARNING_OBJECT (apexsink,
        "%s : could not set initial volume to \"%d\%\", RTSP code=%d",
        apexsink->host, apexsink->volume, res);
  } else {
    GST_INFO_OBJECT (apexsink,
        "OPEN : ApEx sink successfully set volume to \"%d\%\"",
        apexsink->volume);
  }

  return TRUE;
}

/* prepare sink : configure the device with the specified format */
static gboolean
gst_apexsink_prepare (GstAudioSink * asink, GstRingBufferSpec * spec)
{
  GstApExSink *apexsink = (GstApExSink *) asink;

  apexsink->latency_time = spec->latency_time;

  spec->segsize =
      GST_APEX_RAOP_SAMPLES_PER_FRAME * GST_APEX_RAOP_BYTES_PER_SAMPLE;
  spec->segtotal = 1;

  bzero (spec->silence_sample, sizeof (spec->silence_sample));

  GST_INFO_OBJECT (apexsink,
      "PREPARE : ApEx sink ready to stream at %dHz, %d bytes per sample, %d channels, %d bytes segments (%dkB/s)",
      spec->rate, spec->bytes_per_sample, spec->channels, spec->segsize,
      spec->rate * spec->bytes_per_sample / 1000);

  return TRUE;
}

/* sink write : write samples to the device */
static guint
gst_apexsink_write (GstAudioSink * asink, gpointer data, guint length)
{
  GstApExSink *apexsink = (GstApExSink *) asink;

  if (gst_apexraop_write (apexsink->gst_apexraop, data, length) != length) {
    GST_INFO_OBJECT (apexsink,
        "WRITE : %d bytes not fully sended, skipping frame samples...", length);
  } else {
    GST_INFO_OBJECT (apexsink, "WRITE : %d bytes sended", length);

    usleep ((gulong) ((length * 1000000.) / (GST_APEX_RAOP_BITRATE *
                GST_APEX_RAOP_BYTES_PER_SAMPLE) - apexsink->latency_time));
  }

  return length;
}

/* unprepare sink : undo operations done by prepare */
static gboolean
gst_apexsink_unprepare (GstAudioSink * asink)
{
  GstApExSink *apexsink = (GstApExSink *) asink;

  GST_INFO_OBJECT (apexsink, "UNPREPARE");

  return TRUE;
}

/* delay sink : get the estimated number of samples written but not played yet by the device */
static guint
gst_apexsink_delay (GstAudioSink * asink)
{
  GstApExSink *apexsink = (GstApExSink *) asink;

  GST_INFO_OBJECT (apexsink, "DELAY");

  return 0;
}

/* reset sink : unblock writes and flush the device */
static void
gst_apexsink_reset (GstAudioSink * asink)
{
  int res;
  GstApExSink *apexsink = (GstApExSink *) asink;

  GST_INFO_OBJECT (apexsink, "RESET : flushing buffer...");

  if ((res = gst_apexraop_flush (apexsink->gst_apexraop)) == GST_RTSP_STS_OK) {
    GST_INFO_OBJECT (apexsink, "RESET : ApEx buffer flush success");
  } else {
    GST_WARNING_OBJECT (apexsink,
        "RESET : could not flush ApEx buffer, RTSP code=%d", res);
  }
}

/* sink close : close the device */
static gboolean
gst_apexsink_close (GstAudioSink * asink)
{
  GstApExSink *apexsink = (GstApExSink *) asink;

  gst_apexraop_close (apexsink->gst_apexraop);
  gst_apexraop_free (apexsink->gst_apexraop);

  GST_INFO_OBJECT (apexsink, "CLOSE : ApEx sink closed connection");

  return TRUE;
}
