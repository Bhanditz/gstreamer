/*
 * GStreamer
 * Copyright 2005,2006 Zaheer Abbas Merali  <zaheerabbas at merali dot org>
 * Copyright 2007 Pioneers of the Inevitable <songbird@songbirdnest.com>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 *  The development of this code was made possible due to the involvement of 
 *  Pioneers of the Inevitable, the creators of the Songbird Music player.
 *
 */

/**
 * SECTION:element-plugin
 *
 * <refsect2>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch -v -m audiotestsrc ! audioconvert ! osxaudiosink
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreAudio/AudioHardware.h>
#include "gstosxaudiosink.h"
#include "gstosxaudiosrc.h"

#include "gstosxaudioelement.h"

GST_DEBUG_CATEGORY_STATIC (osx_audiosink_debug);
#define GST_CAT_DEFAULT osx_audiosink_debug

static GstElementDetails gst_osx_audio_sink_details =
GST_ELEMENT_DETAILS ("Audio Sink (OSX)",
    "Sink/Audio",
    "Output to a sound card in OS X",
    "Zaheer Abbas Merali <zaheerabbas at merali dot org>");

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_DEVICE
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "endianness = (int) {" G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { TRUE }, "
        "width = (int) 32, "
        "depth = (int) 32, "
        "rate = (int) [1, MAX], " "channels = (int) [1, 2]")
    );

static void gst_osx_audio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_osx_audio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstCaps *gst_osx_audio_sink_getcaps (GstBaseSink * sink);

static GstRingBuffer *gst_osx_audio_sink_create_ringbuffer (GstBaseAudioSink *
    sink);
static void gst_osx_audio_sink_osxelement_init (gpointer g_iface,
    gpointer iface_data);
OSStatus gst_osx_audio_sink_io_proc (AudioDeviceID inDevice,
    const AudioTimeStamp * inNow, const AudioBufferList * inInputData,
    const AudioTimeStamp * inInputTime, AudioBufferList * outOutputData,
    const AudioTimeStamp * inOutputTime, void *inClientData);
static void gst_osx_audio_sink_select_device (GstOsxAudioSink * osxsink);

static void
gst_osx_audio_sink_osxelement_do_init (GType type)
{
  static const GInterfaceInfo osxelement_info = {
    gst_osx_audio_sink_osxelement_init,
    NULL,
    NULL
  };

  GST_DEBUG_CATEGORY_INIT (osx_audiosink_debug, "osxaudiosink", 0,
      "OSX Audio Sink");
  GST_DEBUG ("Adding static interface");
  g_type_add_interface_static (type, GST_OSX_AUDIO_ELEMENT_TYPE,
      &osxelement_info);
}

GST_BOILERPLATE_FULL (GstOsxAudioSink, gst_osx_audio_sink, GstBaseAudioSink,
    GST_TYPE_BASE_AUDIO_SINK, gst_osx_audio_sink_osxelement_do_init);


static void
gst_osx_audio_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_details (element_class, &gst_osx_audio_sink_details);
}

/* initialize the plugin's class */
static void
gst_osx_audio_sink_class_init (GstOsxAudioSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstBaseAudioSinkClass *gstbaseaudiosink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstbaseaudiosink_class = (GstBaseAudioSinkClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_osx_audio_sink_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_osx_audio_sink_get_property);

  g_object_class_install_property (gobject_class, ARG_DEVICE,
      g_param_spec_int ("device", "Device ID", "Device ID of output device",
          0, G_MAXINT, 0, G_PARAM_READWRITE));

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_osx_audio_sink_getcaps);
  gstbaseaudiosink_class->create_ringbuffer =
      GST_DEBUG_FUNCPTR (gst_osx_audio_sink_create_ringbuffer);

}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_osx_audio_sink_init (GstOsxAudioSink * sink, GstOsxAudioSinkClass * gclass)
{
/*  GstElementClass *klass = GST_ELEMENT_GET_CLASS (sink); */
  GST_DEBUG ("Initialising object");

  sink->device_id = kAudioDeviceUnknown;
  sink->stream_id = kAudioStreamUnknown;
}

static void
gst_osx_audio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOsxAudioSink *sink = GST_OSX_AUDIO_SINK (object);

  switch (prop_id) {
    case ARG_DEVICE:
      sink->device_id = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_osx_audio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOsxAudioSink *sink = GST_OSX_AUDIO_SINK (object);
  switch (prop_id) {
    case ARG_DEVICE:
      g_value_set_int (value, sink->device_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

/* GstBaseSink vmethod implementations */
static GstCaps *
gst_osx_audio_sink_getcaps (GstBaseSink * sink)
{
  GstCaps *caps = NULL;
  GstOsxAudioSink *osxsink;
  OSStatus status;
  AudioValueRange *rates = NULL;
  UInt32 propertySize;
  int i;
  gboolean foundFixedRate = FALSE;
  GstStructure *structure;
  GValue rate_v = { 0 };
  GValue rates_v = { 0 };

  osxsink = GST_OSX_AUDIO_SINK (sink);

  gst_osx_audio_sink_select_device (osxsink);

  GST_DEBUG_OBJECT (osxsink, "Using device_id %d", (int) osxsink->device_id);

  status = AudioDeviceGetPropertyInfo (osxsink->device_id, 0,   /* Master channel */
      FALSE,                    /* isInput */
      kAudioDevicePropertyAvailableNominalSampleRates, &propertySize, NULL);

  if (status) {
    GST_WARNING_OBJECT (osxsink, "Failed to get sample rates size: %ld",
        status);
    goto done;
  }

  GST_DEBUG_OBJECT (osxsink, "Allocating %d bytes for sizes",
      (int) propertySize);
  rates = g_malloc (propertySize);

  status = AudioDeviceGetProperty (osxsink->device_id, 0,       /* Master channel */
      FALSE,                    /* isInput */
      kAudioDevicePropertyAvailableNominalSampleRates, &propertySize, rates);

  if (status) {
    GST_WARNING_OBJECT (osxsink, "Failed to get sample rates: %ld", status);
    goto done;
  }

  GST_DEBUG_OBJECT (osxsink, "Used %d bytes for sizes", (int) propertySize);

  if (propertySize < sizeof (AudioValueRange)) {
    GST_WARNING_OBJECT (osxsink, "Zero sample rates available");
    goto done;
  }

  /* Create base caps object, then modify to suit. */
  caps = gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD
          (sink)));
  structure = gst_caps_get_structure (caps, 0);

  GST_DEBUG
      ("Getting available sample rates: Status: %ld number of ranges: %lu",
      status, propertySize / sizeof (AudioValueRange));

  g_value_init (&rates_v, GST_TYPE_LIST);
  g_value_init (&rate_v, G_TYPE_INT);

  for (i = 0; i < propertySize / sizeof (AudioValueRange); i++) {
    GST_LOG_OBJECT (osxsink, "Range from %f to %f", rates[i].mMinimum,
        rates[i].mMaximum);
    if (rates[i].mMinimum == rates[i].mMaximum) {
      /* For now, we only support these in this form. If there are none
       * in this form, we use the first (only) as a range. */
      foundFixedRate = TRUE;

      g_value_set_int (&rate_v, rates[i].mMinimum);
      gst_value_list_append_value (&rates_v, &rate_v);
    }
  }

  g_value_unset (&rate_v);

  if (foundFixedRate) {
    gst_structure_set_value (structure, "rate", &rates_v);
  } else {
    gst_structure_set (structure, "rate", GST_TYPE_INT_RANGE,
        rates[0].mMinimum, rates[0].mMaximum, NULL);
  }

  g_value_unset (&rates_v);

done:
  if (rates)
    g_free (rates);

  return caps;
}

/* GstBaseAudioSink vmethod implementations */
static GstRingBuffer *
gst_osx_audio_sink_create_ringbuffer (GstBaseAudioSink * sink)
{
  GstOsxAudioSink *osxsink;
  GstOsxRingBuffer *ringbuffer;

  osxsink = GST_OSX_AUDIO_SINK (sink);

  gst_osx_audio_sink_select_device (osxsink);

  GST_DEBUG ("Creating ringbuffer");
  ringbuffer = g_object_new (GST_TYPE_OSX_RING_BUFFER, NULL);
  GST_DEBUG ("osx sink 0x%p element 0x%p  ioproc 0x%p", osxsink,
      GST_OSX_AUDIO_ELEMENT_GET_INTERFACE (osxsink),
      (void *) gst_osx_audio_sink_io_proc);
  ringbuffer->element = GST_OSX_AUDIO_ELEMENT_GET_INTERFACE (osxsink);
  ringbuffer->device_id = osxsink->device_id;
  ringbuffer->stream_id = osxsink->stream_id;

  return GST_RING_BUFFER (ringbuffer);
}

OSStatus
gst_osx_audio_sink_io_proc (AudioDeviceID inDevice,
    const AudioTimeStamp * inNow, const AudioBufferList * inInputData,
    const AudioTimeStamp * inInputTime, AudioBufferList * outOutputData,
    const AudioTimeStamp * inOutputTime, void *inClientData)
{
  GstOsxRingBuffer *buf = GST_OSX_RING_BUFFER (inClientData);

  guint8 *readptr;
  gint readseg;
  gint len;

  if (gst_ring_buffer_prepare_read (GST_RING_BUFFER (buf), &readseg, &readptr,
          &len)) {
    outOutputData->mBuffers[0].mDataByteSize = len;
    memcpy ((char *) outOutputData->mBuffers[0].mData, readptr, len);

    /* clear written samples */
    gst_ring_buffer_clear (GST_RING_BUFFER (buf), readseg);

    /* we wrote one segment */
    gst_ring_buffer_advance (GST_RING_BUFFER (buf), 1);
  }
  return 0;
}

static void
gst_osx_audio_sink_osxelement_init (gpointer g_iface, gpointer iface_data)
{
  GstOsxAudioElementInterface *iface = (GstOsxAudioElementInterface *) g_iface;

  iface->io_proc = gst_osx_audio_sink_io_proc;
}


static void
gst_osx_audio_sink_select_device (GstOsxAudioSink * osxsink)
{
  OSStatus status;
  UInt32 propertySize;

  if (osxsink->device_id == kAudioDeviceUnknown) {
    GST_DEBUG_OBJECT (osxsink, "Selecting device for OSXAudioSink");
    propertySize = sizeof (osxsink->device_id);
    status =
        AudioHardwareGetProperty (kAudioHardwarePropertyDefaultOutputDevice,
        &propertySize, &osxsink->device_id);

    if (status)
      GST_WARNING_OBJECT (osxsink,
          "AudioHardwareGetProperty returned %d", (int) status);
    else
      GST_DEBUG_OBJECT (osxsink, "AudioHardwareGetProperty returned 0");

    if (osxsink->device_id == kAudioDeviceUnknown)
      GST_WARNING_OBJECT (osxsink,
          "AudioHardwareGetProperty: device_id is kAudioDeviceUnknown");

    GST_DEBUG_OBJECT (osxsink, "AudioHardwareGetProperty: device_id is %lu",
        (long) osxsink->device_id);
  }

  if (osxsink->stream_id == kAudioStreamUnknown) {
    AudioStreamID *streams;

    GST_DEBUG_OBJECT (osxsink, "Getting streamid");
    status = AudioDeviceGetPropertyInfo (osxsink->device_id, 0, /* Master channel */
        FALSE,                  /* isInput */
        kAudioDevicePropertyStreams, &propertySize, NULL);

    if (status) {
      GST_WARNING_OBJECT (osxsink,
          "AudioDeviceGetProperty returned %d", (int) status);
      return;
    }

    GST_DEBUG_OBJECT (osxsink,
        "Getting available streamids from %d (%d bytes)",
        (int) (propertySize / sizeof (AudioStreamID)), propertySize);
    streams = g_malloc (propertySize);
    status = AudioDeviceGetProperty (osxsink->device_id, 0,     /* Master channel */
        FALSE,                  /* isInput */
        kAudioDevicePropertyStreams, &propertySize, streams);

    if (status) {
      GST_WARNING_OBJECT (osxsink,
          "AudioDeviceGetProperty returned %d", (int) status);
      g_free (streams);
      return;
    }

    GST_DEBUG_OBJECT (osxsink, "Getting streamid from %d (%d bytes)",
        (int) (propertySize / sizeof (AudioStreamID)), propertySize);

    if (propertySize >= sizeof (AudioStreamID)) {
      osxsink->stream_id = streams[0];
      GST_DEBUG_OBJECT (osxsink, "Selected stream %d of %d: %d", 0,
          (int) (propertySize / sizeof (AudioStreamID)),
          (int) osxsink->stream_id);
    }

    g_free (streams);
  }
}
