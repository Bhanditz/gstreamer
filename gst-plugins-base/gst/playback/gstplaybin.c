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
#include <gst/gst.h>

#include "gstplaybasebin.h"

GST_DEBUG_CATEGORY_STATIC (gst_play_bin_debug);
#define GST_CAT_DEFAULT gst_play_bin_debug

#define GST_TYPE_PLAY_BIN 		(gst_play_bin_get_type())
#define GST_PLAY_BIN(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PLAY_BIN,GstPlayBin))
#define GST_PLAY_BIN_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PLAY_BIN,GstPlayBinClass))
#define GST_IS_PLAY_BIN(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PLAY_BIN))
#define GST_IS_PLAY_BIN_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PLAY_BIN))

#define VOLUME_MAX_DOUBLE 4.0

typedef struct _GstPlayBin GstPlayBin;
typedef struct _GstPlayBinClass GstPlayBinClass;

struct _GstPlayBin
{
  GstPlayBaseBin parent;

  /* the configurable elements */
  GstElement *audio_sink;
  GstElement *video_sink;
  GstElement *visualisation;
  GstElement *volume_element;
  gfloat volume;

  /* these are the currently active sinks */
  GList *sinks;

  /* these are the sink elements used for seeking/query etc.. */
  GList *seekables;

  /* the last captured frame for snapshots */
  GstBuffer *frame;

  /* our cache for the sinks */
  GHashTable *cache;
};

struct _GstPlayBinClass
{
  GstPlayBaseBinClass parent_class;
};

/* props */
enum
{
  ARG_0,
  ARG_AUDIO_SINK,
  ARG_VIDEO_SINK,
  ARG_VIS_PLUGIN,
  ARG_VOLUME,
  ARG_FRAME
};

/* signals */
enum
{
  LAST_SIGNAL
};

static void gst_play_bin_class_init (GstPlayBinClass * klass);
static void gst_play_bin_init (GstPlayBin * play_bin);
static void gst_play_bin_dispose (GObject * object);

static void setup_sinks (GstPlayBaseBin * play_base_bin);
static void remove_sinks (GstPlayBin * play_bin);

static void gst_play_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);
static void gst_play_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);
static GstElementStateReturn gst_play_bin_change_state (GstElement * element);

static const GstEventMask *gst_play_bin_get_event_masks (GstElement * element);
static gboolean gst_play_bin_send_event (GstElement * element,
    GstEvent * event);
static const GstFormat *gst_play_bin_get_formats (GstElement * element);
static gboolean gst_play_bin_convert (GstElement * element,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value);
static const GstQueryType *gst_play_bin_get_query_types (GstElement * element);
static gboolean gst_play_bin_query (GstElement * element, GstQueryType type,
    GstFormat * format, gint64 * value);


static GstElementClass *parent_class;

//static guint gst_play_bin_signals[LAST_SIGNAL] = { 0 };

static GstElementDetails gst_play_bin_details = {
  "Player Bin",
  "Generic/Bin/Player",
  "Autoplug and play media from an uri",
  "Wim Taymans <wim@fluendo.com>"
};

static GType
gst_play_bin_get_type (void)
{
  static GType gst_play_bin_type = 0;

  if (!gst_play_bin_type) {
    static const GTypeInfo gst_play_bin_info = {
      sizeof (GstPlayBinClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_play_bin_class_init,
      NULL,
      NULL,
      sizeof (GstPlayBin),
      0,
      (GInstanceInitFunc) gst_play_bin_init,
      NULL
    };

    gst_play_bin_type = g_type_register_static (GST_TYPE_PLAY_BASE_BIN,
        "GstPlayBin", &gst_play_bin_info, 0);
  }

  return gst_play_bin_type;
}

static void
gst_play_bin_class_init (GstPlayBinClass * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *gstelement_klass;
  GstBinClass *gstbin_klass;
  GstPlayBaseBinClass *playbasebin_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;
  gstbin_klass = (GstBinClass *) klass;
  playbasebin_klass = (GstPlayBaseBinClass *) klass;

  parent_class = g_type_class_ref (gst_play_base_bin_get_type ());

  gobject_klass->set_property = gst_play_bin_set_property;
  gobject_klass->get_property = gst_play_bin_get_property;

  g_object_class_install_property (gobject_klass, ARG_VIDEO_SINK,
      g_param_spec_object ("video-sink", "Video Sink",
          "the video output element to use (NULL = default sink)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, ARG_AUDIO_SINK,
      g_param_spec_object ("audio-sink", "Audio Sink",
          "the audio output element to use (NULL = default sink)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, ARG_VIS_PLUGIN,
      g_param_spec_object ("vis-plugin", "Vis plugin",
          "the visualization element to use (NULL = none)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (gobject_klass), ARG_VOLUME,
      g_param_spec_double ("volume", "volume", "volume",
          0.0, VOLUME_MAX_DOUBLE, 1.0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (gobject_klass), ARG_FRAME,
      g_param_spec_boxed ("frame", "Frame",
          "The last frame (NULL = no video available)",
          GST_TYPE_BUFFER, G_PARAM_READABLE));

  gobject_klass->dispose = GST_DEBUG_FUNCPTR (gst_play_bin_dispose);

  gst_element_class_set_details (gstelement_klass, &gst_play_bin_details);

  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_play_bin_change_state);
  gstelement_klass->get_event_masks =
      GST_DEBUG_FUNCPTR (gst_play_bin_get_event_masks);
  gstelement_klass->send_event = GST_DEBUG_FUNCPTR (gst_play_bin_send_event);
  gstelement_klass->get_formats = GST_DEBUG_FUNCPTR (gst_play_bin_get_formats);
  gstelement_klass->convert = GST_DEBUG_FUNCPTR (gst_play_bin_convert);
  gstelement_klass->get_query_types =
      GST_DEBUG_FUNCPTR (gst_play_bin_get_query_types);
  gstelement_klass->query = GST_DEBUG_FUNCPTR (gst_play_bin_query);

  playbasebin_klass->setup_output_pads = setup_sinks;
}

static void
gst_play_bin_init (GstPlayBin * play_bin)
{
  play_bin->video_sink = NULL;
  play_bin->audio_sink = NULL;
  play_bin->visualisation = NULL;
  play_bin->volume_element = NULL;
  play_bin->volume = 1.0;
  play_bin->seekables = NULL;
  play_bin->sinks = NULL;
  play_bin->frame = NULL;
  play_bin->cache = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) gst_object_unref);

  /* no iterate is needed */
  GST_FLAG_SET (play_bin, GST_BIN_SELF_SCHEDULABLE);
}

static void
gst_play_bin_dispose (GObject * object)
{
  GstPlayBin *play_bin;

  play_bin = GST_PLAY_BIN (object);

  if (play_bin->cache != NULL) {
    remove_sinks (play_bin);
    g_hash_table_destroy (play_bin->cache);
    play_bin->cache = NULL;
  }

  if (play_bin->audio_sink != NULL) {
    gst_object_unref (GST_OBJECT (play_bin->audio_sink));
    play_bin->audio_sink = NULL;
  }
  if (play_bin->video_sink != NULL) {
    gst_object_unref (GST_OBJECT (play_bin->video_sink));
    play_bin->video_sink = NULL;
  }
  if (play_bin->visualisation != NULL) {
    gst_object_unref (GST_OBJECT (play_bin->visualisation));
    play_bin->visualisation = NULL;
  }


  if (G_OBJECT_CLASS (parent_class)->dispose) {
    G_OBJECT_CLASS (parent_class)->dispose (object);
  }
}


static void
gst_play_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPlayBin *play_bin;

  g_return_if_fail (GST_IS_PLAY_BIN (object));

  play_bin = GST_PLAY_BIN (object);

  switch (prop_id) {
    case ARG_VIDEO_SINK:
      if (play_bin->video_sink != NULL) {
        gst_object_unref (GST_OBJECT (play_bin->video_sink));
      }
      play_bin->video_sink = g_value_get_object (value);
      if (play_bin->video_sink != NULL) {
        gst_object_ref (GST_OBJECT (play_bin->video_sink));
        gst_object_sink (GST_OBJECT (play_bin->video_sink));
      }
      /* when changing the videosink, we just remove the
       * video pipeline from the cache so that it will be 
       * regenerated with the new sink element */
      g_hash_table_remove (play_bin->cache, "vbin");
      break;
    case ARG_AUDIO_SINK:
      if (play_bin->audio_sink != NULL) {
        gst_object_unref (GST_OBJECT (play_bin->audio_sink));
      }
      play_bin->audio_sink = g_value_get_object (value);
      if (play_bin->audio_sink != NULL) {
        gst_object_ref (GST_OBJECT (play_bin->audio_sink));
        gst_object_sink (GST_OBJECT (play_bin->audio_sink));
      }
      g_hash_table_remove (play_bin->cache, "abin");
      break;
    case ARG_VIS_PLUGIN:
      if (play_bin->visualisation != NULL) {
        gst_object_unref (GST_OBJECT (play_bin->visualisation));
      }
      play_bin->visualisation = g_value_get_object (value);
      if (play_bin->visualisation != NULL) {
        gst_object_ref (GST_OBJECT (play_bin->visualisation));
        gst_object_sink (GST_OBJECT (play_bin->visualisation));
      }
      break;
    case ARG_VOLUME:
      if (play_bin->volume_element) {
        play_bin->volume = g_value_get_double (value);
        g_object_set (G_OBJECT (play_bin->volume_element), "volume",
            play_bin->volume, NULL);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_play_bin_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstPlayBin *play_bin;

  g_return_if_fail (GST_IS_PLAY_BIN (object));

  play_bin = GST_PLAY_BIN (object);

  switch (prop_id) {
    case ARG_VIDEO_SINK:
      g_value_set_object (value, play_bin->video_sink);
      break;
    case ARG_AUDIO_SINK:
      g_value_set_object (value, play_bin->audio_sink);
      break;
    case ARG_VIS_PLUGIN:
      g_value_set_object (value, play_bin->visualisation);
      break;
    case ARG_VOLUME:
      g_value_set_double (value, play_bin->volume);
      break;
    case ARG_FRAME:
      g_value_set_boxed (value, play_bin->frame);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* signal fired when the identity has received a new buffer. This is used for
 * making screenshots.
 */
static void
handoff (GstElement * identity, GstBuffer * frame, gpointer data)
{
  GstPlayBin *play_bin = GST_PLAY_BIN (data);

  if (play_bin->frame) {
    gst_buffer_unref (play_bin->frame);
  }
  play_bin->frame = gst_buffer_ref (frame);
}

/* make the element (bin) that contains the elements needed to perform
 * video display. We connect a handoff signal to identity so that we
 * can grab snapshots. Identity's sinkpad is ghosted to vbin.
 *
 *  +-------------------------------------------------------------+
 *  | vbin                                                        |
 *  |      +--------+   +----------+   +----------+   +---------+ |
 *  |      |identity|   |colorspace|   |videoscale|   |videosink| |
 *  |   +-sink     src-sink       src-sink       src-sink       | |
 *  |   |  +---+----+   +----------+   +----------+   +---------+ |
 * sink-+      |                                                  |
 *  +----------|--------------------------------------------------+
 *           handoff
 */
static GstElement *
gen_video_element (GstPlayBin * play_bin)
{
  GstElement *element;
  GstElement *conv;
  GstElement *scale;
  GstElement *sink;
  GstElement *identity;

  /* first see if we have it in the cache */
  element = g_hash_table_lookup (play_bin->cache, "vbin");
  if (element != NULL) {
    /* need to get the video sink element as we need to add it to the
     * list of seekable elements */
    sink = g_hash_table_lookup (play_bin->cache, "video_sink");
    goto done;
  }

  element = gst_bin_new ("vbin");
  identity = gst_element_factory_make ("identity", "id");
  g_signal_connect (identity, "handoff", G_CALLBACK (handoff), play_bin);
  conv = gst_element_factory_make ("ffmpegcolorspace", "vconv");
  scale = gst_element_factory_make ("videoscale", "vscale");
  if (play_bin->video_sink) {
    sink = play_bin->video_sink;
  } else {
    sink = gst_element_factory_make ("xvimagesink", "sink");
  }
  gst_object_ref (GST_OBJECT (sink));
  g_hash_table_insert (play_bin->cache, "video_sink", sink);

  gst_bin_add (GST_BIN (element), identity);
  gst_bin_add (GST_BIN (element), conv);
  gst_bin_add (GST_BIN (element), scale);
  gst_bin_add (GST_BIN (element), sink);
  gst_element_link_pads (identity, "src", conv, "sink");
  gst_element_link_pads (conv, "src", scale, "sink");
  gst_element_link_pads (scale, "src", sink, "sink");

  gst_element_add_ghost_pad (element, gst_element_get_pad (identity, "sink"),
      "sink");

  gst_element_set_state (element, GST_STATE_READY);

  /* since we're gonna add it to a bin but don't want to lose it,
   * we keep a reference. */
  gst_object_ref (GST_OBJECT (element));
  g_hash_table_insert (play_bin->cache, "vbin", element);

done:
  play_bin->seekables = g_list_append (play_bin->seekables, sink);

  return element;
}

/* make an element for playback of video with subtitles embedded.
 *
 *  +--------------------------------------------------+
 *  | tbin                  +-------------+            |
 *  |          +-----+      | textoverlay |   +------+ |
 *  |          | csp | +--video_sink      |   | vbin | |
 * video_sink-sink  src+ +-text_sink     src-sink    | |
 *  |          +-----+   |  +-------------+   +------+ |
 * text_sink-------------+                             |
 *  +--------------------------------------------------+
 */

static GstElement *
gen_text_element (GstPlayBin * play_bin)
{
  GstElement *element, *csp, *overlay, *vbin;

  overlay = gst_element_factory_make ("textoverlay", "overlay");
  g_object_set (G_OBJECT (overlay),
      "halign", "center", "valign", "bottom", NULL);
  vbin = gen_video_element (play_bin);
  if (!overlay) {
    g_warning ("No overlay (pango) element, subtitles disabled");
    return vbin;
  }
  csp = gst_element_factory_make ("ffmpegcolorspace", "subtitlecsp");
  element = gst_bin_new ("textbin");
  gst_element_link_many (csp, overlay, vbin, NULL);
  gst_bin_add_many (GST_BIN (element), csp, overlay, vbin, NULL);

  gst_element_add_ghost_pad (element,
      gst_element_get_pad (overlay, "text_sink"), "text_sink");
  gst_element_add_ghost_pad (element,
      gst_element_get_pad (csp, "sink"), "sink");

  return element;
}

/* make the element (bin) that contains the elements needed to perform
 * audio playback. 
 *
 *  +-------------------------------------------------------------+
 *  | abin                                                        |
 *  |      +---------+   +----------+   +---------+   +---------+ |
 *  |      |audioconv|   |audioscale|   | volume  |   |audiosink| |
 *  |   +-sink      src-sink       src-sink      src-sink       | |
 *  |   |  +---------+   +----------+   +---------+   +---------+ |
 * sink-+                                                         |
 *  +-------------------------------------------------------------+
 *                  
 */
static GstElement *
gen_audio_element (GstPlayBin * play_bin)
{
  GstElement *element;
  GstElement *conv;
  GstElement *sink;
  GstElement *volume;
  GstElement *scale;

  element = g_hash_table_lookup (play_bin->cache, "abin");
  if (element != NULL) {
    sink = g_hash_table_lookup (play_bin->cache, "audio_sink");
    goto done;
  }
  element = gst_bin_new ("abin");
  conv = gst_element_factory_make ("audioconvert", "aconv");
  scale = gst_element_factory_make ("audioscale", "ascale");

  volume = gst_element_factory_make ("volume", "volume");
  g_object_set (G_OBJECT (volume), "volume", play_bin->volume, NULL);
  play_bin->volume_element = volume;

  if (play_bin->audio_sink) {
    sink = play_bin->audio_sink;
  } else {
    sink = gst_element_factory_make ("osssink", "sink");
    play_bin->audio_sink = GST_ELEMENT (gst_object_ref (GST_OBJECT (sink)));
  }

  gst_object_ref (GST_OBJECT (sink));
  g_hash_table_insert (play_bin->cache, "audio_sink", sink);

  gst_bin_add (GST_BIN (element), conv);
  gst_bin_add (GST_BIN (element), scale);
  gst_bin_add (GST_BIN (element), volume);
  gst_bin_add (GST_BIN (element), sink);

  gst_element_link_pads (conv, "src", scale, "sink");
  gst_element_link_pads (scale, "src", volume, "sink");
  gst_element_link_pads (volume, "src", sink, "sink");

  gst_element_add_ghost_pad (element,
      gst_element_get_pad (conv, "sink"), "sink");

  gst_element_set_state (element, GST_STATE_READY);

  /* since we're gonna add it to a bin but don't want to lose it,
   * we keep a reference. */
  gst_object_ref (GST_OBJECT (element));
  g_hash_table_insert (play_bin->cache, "abin", element);

done:
  play_bin->seekables = g_list_prepend (play_bin->seekables, sink);

  return element;
}

/* make the element (bin) that contains the elements needed to perform
 * visualisation ouput.  The idea is to split the audio using tee, then 
 * sending the output to the regular audio bin and the other output to
 * the vis plugin that transforms it into a video that is rendered with the
 * normal video bin. The video bin is run in a thread to make sure it does
 * not block the audio playback pipeline.
 *
 *  +--------------------------------------------------------------------------+
 *  | visbin                                                                   |
 *  |      +------+   +----------------+                                       |
 *  |      | tee  |   |   abin ...     |                                       |
 *  |   +-sink   src-sink              |                                       |
 *  |   |  |      |   +----------------+                 +-------------------+ |
 *  |   |  |      |                                      | vthread           | |
 *  |   |  |      |   +---------+   +------+   +------+  | +--------------+  | |
 *  |   |  |      |   |audioconv|   | vis  |   |vqueue|  | | vbin ...     |  | |
 *  |   |  |     src-sink      src-sink   src-sink   src-sink             |  | |
 *  |   |  |      |   +---------+   +------+   +------+  | +--------------+  | |
 *  |   |  |      |                                      +-------------------+ |
 *  |   |  +------+                                                            |
 * sink-+                                                                      |
   +--------------------------------------------------------------------------+
 */
static GstElement *
gen_vis_element (GstPlayBin * play_bin)
{
  GstElement *element;
  GstElement *tee;
  GstElement *asink;
  GstElement *vsink;
  GstElement *conv;
  GstElement *vis;
  GstElement *vqueue;
  GstElement *vthread;

  element = gst_bin_new ("visbin");
  tee = gst_element_factory_make ("tee", "tee");

  vqueue = gst_element_factory_make ("queue", "vqueue");
  vthread = gst_element_factory_make ("thread", "vthread");

  asink = gen_audio_element (play_bin);
  vsink = gen_video_element (play_bin);

  gst_bin_add (GST_BIN (element), asink);
  gst_bin_add (GST_BIN (element), vqueue);
  gst_bin_add (GST_BIN (vthread), vsink);
  gst_bin_add (GST_BIN (element), vthread);
  gst_bin_add (GST_BIN (element), tee);

  conv = gst_element_factory_make ("audioconvert", "aconv");
  if (play_bin->visualisation) {
    gst_object_ref (GST_OBJECT (play_bin->visualisation));
    vis = play_bin->visualisation;
  } else {
    vis = gst_element_factory_make ("goom", "vis");
  }

  gst_bin_add (GST_BIN (element), conv);
  gst_bin_add (GST_BIN (element), vis);

  gst_element_link_pads (conv, "src", vis, "sink");
  gst_element_link_pads (vis, "src", vqueue, "sink");

  gst_element_link_pads (vqueue, "src", vsink, "sink");

  gst_pad_link (gst_element_get_request_pad (tee, "src%d"),
      gst_element_get_pad (asink, "sink"));

  gst_pad_link (gst_element_get_request_pad (tee, "src%d"),
      gst_element_get_pad (conv, "sink"));

  gst_element_add_ghost_pad (element,
      gst_element_get_pad (tee, "sink"), "sink");

  return element;
}

/* get rid of all installed sinks */
static void
remove_sinks (GstPlayBin * play_bin)
{
  GList *sinks;
  GstObject *parent;
  GstElement *element;

  GST_DEBUG ("removesinks");
  element = g_hash_table_lookup (play_bin->cache, "abin");
  if (element != NULL) {
    parent = gst_element_get_parent (element);
    if (parent != NULL) {
      /* we remove the element from the parent so that
       * there is no unwanted state change when the parent
       * is disposed */
      gst_bin_remove (GST_BIN (parent), element);
    }
  }
  element = g_hash_table_lookup (play_bin->cache, "vbin");
  if (element != NULL) {
    parent = gst_element_get_parent (element);
    if (parent != NULL) {
      gst_bin_remove (GST_BIN (parent), element);
    }
  }

  for (sinks = play_bin->sinks; sinks; sinks = g_list_next (sinks)) {
    GstElement *element = GST_ELEMENT (sinks->data);
    GstPad *pad = gst_element_get_pad (element, "sink");

    GST_LOG ("removing sink %p", element);
    if (GST_PAD_PEER (pad))
      gst_pad_unlink (GST_PAD_PEER (pad), pad);
    gst_bin_remove (GST_BIN (play_bin), element);
  }
  g_list_free (play_bin->sinks);
  g_list_free (play_bin->seekables);
  play_bin->sinks = NULL;
  play_bin->seekables = NULL;

  if (play_bin->frame) {
    gst_buffer_unref (play_bin->frame);
    play_bin->frame = NULL;
  }
}

/* loop over the streams and set up the pipeline to play this
 * media file. First we count the number of audio and video streams.
 * If there is no video stream but there exists an audio stream,
 * we install a visualisation pipeline.
 * 
 * Also make sure to only connect the first audio and video pad. FIXME
 * this should eventually be handled with a tuner interface so that
 * one can switch the streams.
 */
static gboolean
add_sink (GstPlayBin * play_bin, GstElement * sink, GstPad * srcpad)
{
  GstPad *sinkpad;
  gboolean res;

  /* we found a sink for this stream, now try to install it */
  gst_bin_add (GST_BIN (play_bin), sink);
  GST_DEBUG ("Adding sink with state %d (parent: %d, peer: %d)\n",
      GST_STATE (sink), GST_STATE (play_bin),
      GST_STATE (gst_pad_get_parent (srcpad)));
  sinkpad = gst_element_get_pad (sink, "sink");

  /* try to link the pad of the sink to the stream */
  res = gst_pad_link (srcpad, sinkpad);
  if (!res) {
    gchar *capsstr;

    /* could not link this stream */
    capsstr = gst_caps_to_string (gst_pad_get_caps (srcpad));
    g_warning ("could not link %s", capsstr);
    g_free (capsstr);
    GST_LOG ("removing sink %p", sink);
    gst_bin_remove (GST_BIN (play_bin), sink);
  } else {
    /* we got the sink succesfully linked, now keep the sink
     * in out internal list */
    play_bin->sinks = g_list_prepend (play_bin->sinks, sink);
  }

  return res;
}

static void
setup_sinks (GstPlayBaseBin * play_base_bin)
{
  GstPlayBin *play_bin = GST_PLAY_BIN (play_base_bin);
  GstPlayBaseGroup *group;
  GList *streaminfo = NULL, *s;
  gboolean need_vis = FALSE;
  gboolean need_text = FALSE;
  GstPad *textsrcpad = NULL, *textsinkpad = NULL;
  GstElement *sink;

  /* get rid of existing sinks */
  if (play_bin->sinks) {
    remove_sinks (play_bin);
  }
  GST_DEBUG ("setupsinks");

  /* find out what to do */
  group = play_base_bin->queued_groups->data;
  if (group->type[GST_STREAM_TYPE_VIDEO - 1].npads > 0 &&
      group->type[GST_STREAM_TYPE_TEXT - 1].npads > 0) {
    need_text = TRUE;
  } else if (group->type[GST_STREAM_TYPE_VIDEO - 1].npads == 0 &&
      group->type[GST_STREAM_TYPE_AUDIO - 1].npads > 0 &&
      play_bin->visualisation != NULL) {
    need_vis = TRUE;
  }

  /* now actually connect everything */
  g_object_get (G_OBJECT (play_base_bin), "stream-info", &streaminfo, NULL);
  for (s = streaminfo; s; s = g_list_next (s)) {
    GObject *obj = G_OBJECT (s->data);
    gint type;
    GstObject *object;

    g_object_get (obj, "type", &type, NULL);
    g_object_get (obj, "object", &object, NULL);

    /* use the sink elements as seek entry point */
    if (type == 4) {
      play_bin->seekables = g_list_prepend (play_bin->seekables, object);
    }
  }

  /* link audio */
  if (group->type[GST_STREAM_TYPE_AUDIO - 1].npads > 0) {
    if (need_vis) {
      sink = gen_vis_element (play_bin);
    } else {
      sink = gen_audio_element (play_bin);
    }
    //gst_element_link (group->type[GST_STREAM_TYPE_AUDIO - 1].preroll, sink);
    add_sink (play_bin, sink,
        gst_element_get_pad (group->type[GST_STREAM_TYPE_AUDIO - 1].preroll,
            "src"));
  }

  /* link video */
  if (group->type[GST_STREAM_TYPE_VIDEO - 1].npads > 0) {
    if (need_text) {
      sink = gen_text_element (play_bin);

      textsinkpad = gst_element_get_pad (sink, "text_sink");
      textsrcpad =
          gst_element_get_pad (group->type[GST_STREAM_TYPE_TEXT - 1].preroll,
          "src");
      if (textsinkpad && textsrcpad) {
        gst_pad_link (textsrcpad, textsinkpad);
      }
    } else {
      sink = gen_video_element (play_bin);
    }
    //gst_element_link (group->type[GST_STREAM_TYPE_VIDEO - 1].preroll, sink);
    add_sink (play_bin, sink,
        gst_element_get_pad (group->type[GST_STREAM_TYPE_VIDEO - 1].preroll,
            "src"));
  }
}

static GstElementStateReturn
gst_play_bin_change_state (GstElement * element)
{
  GstElementStateReturn ret;
  GstPlayBin *play_bin;
  int transition;

  play_bin = GST_PLAY_BIN (element);

  transition = GST_STATE_TRANSITION (element);

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);
  if (ret == GST_STATE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_PLAYING_TO_PAUSED:
      /* Set audio sink state to NULL to release the sound device,
       * but only if we own it (else we might be in chain-transition). */
      if (play_bin->audio_sink != NULL &&
          GST_STATE (play_bin->audio_sink) == GST_STATE_PAUSED) {
        gst_element_set_state (play_bin->audio_sink, GST_STATE_NULL);
      }
      break;
    case GST_STATE_PAUSED_TO_READY:
      /* Check for NULL because the state transition may be done by
       * gst_bin_dispose which is called by gst_play_bin_dispose, and in that
       * case, we don't want to run remove_sinks.
       * FIXME: should the NULL test be done in remove_sinks? Should we just
       * set the state to NULL in gst_play_bin_dispose?
       */
      if (play_bin->cache != NULL) {
        remove_sinks (play_bin);
      }
      break;
    default:
      break;
  }

  return ret;
}


static const GstEventMask *
gst_play_bin_get_event_masks (GstElement * element)
{
  /* FIXME, get the list from the number of installed sinks */
  return NULL;
}

/* send an event to all the sinks */
static gboolean
gst_play_bin_send_event (GstElement * element, GstEvent * event)
{
  gboolean res = FALSE;
  GList *s;
  GstPlayBin *play_bin;
  GstElementState state;
  gboolean need_pause = FALSE;

  play_bin = GST_PLAY_BIN (element);

  state = gst_element_get_state (element);
  /* we pause the pipeline first before sending the event. We only
   * do this if the pipeline was playing. */
  if (state == GST_STATE_PLAYING) {
    need_pause = TRUE;
    gst_element_set_state (element, GST_STATE_PAUSED);
  }

  /* loop over all seekables and send the event to them */
  for (s = play_bin->seekables; s; s = g_list_next (s)) {
    GstElement *element = GST_ELEMENT (s->data);
    gboolean ok;

    /* ref each event before sending it */
    gst_event_ref (event);
    ok = gst_element_send_event (element, event);
    res |= ok;
  }
  gst_event_unref (event);

  /* and restart the pipeline if we paused it */
  if (need_pause)
    gst_element_set_state (element, GST_STATE_PLAYING);

  return res;
}

static const GstFormat *
gst_play_bin_get_formats (GstElement * element)
{
  /* FIXME, compile this list from the installed sinks */
  static GstFormat formats[] = {
    GST_FORMAT_TIME,
    0,
  };

  return formats;
}

static gboolean
gst_play_bin_convert (GstElement * element,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = FALSE;
  GList *s;
  GstPlayBin *play_bin;

  play_bin = GST_PLAY_BIN (element);

  /* do a conversion, loop over all sinks, stop as soon as one of the
   * sinks returns a successful result */
  for (s = play_bin->seekables; s; s = g_list_next (s)) {
    GstElement *element = GST_ELEMENT (s->data);

    res = gst_element_convert (element, src_format, src_value,
        dest_format, dest_value);
    if (res)
      break;
  }
  return res;
}

static const GstQueryType *
gst_play_bin_get_query_types (GstElement * element)
{
  /* FIXME, compile from the installed sinks */
  static const GstQueryType query_types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };

  return query_types;
}

static gboolean
gst_play_bin_query (GstElement * element, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  gboolean res = FALSE;
  GList *s;
  GstPlayBin *play_bin;

  play_bin = GST_PLAY_BIN (element);

  for (s = play_bin->seekables; s; s = g_list_next (s)) {
    GstElement *element = GST_ELEMENT (s->data);

    res = gst_element_query (element, type, format, value);
    if (res)
      break;
  }
  return res;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_play_bin_debug, "playbin", 0, "play bin");

  return gst_element_register (plugin, "playbin", GST_RANK_NONE,
      GST_TYPE_PLAY_BIN);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "playbin",
    "player bin", plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
