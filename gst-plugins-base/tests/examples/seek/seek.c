#include <stdlib.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <string.h>

static GList *seekable_pads = NULL;
static GList *rate_pads = NULL;
static GList *seekable_elements = NULL;

static GstElement *pipeline;
static gint64 position;
static gint64 duration;
static GtkAdjustment *adjustment;
static GtkWidget *hscale;
static gboolean stats = FALSE;
static gboolean elem_seek = FALSE;
static gboolean verbose = FALSE;

static guint update_id;
static guint seek_timeout_id = 0;
static gulong changed_id;

//#define SOURCE "filesrc"
#define SOURCE "gnomevfssrc"
#define ASINK "alsasink"
//#define ASINK "osssink"
#define VSINK "xvimagesink"
//#define VSINK "ximagesink"
//#define VSINK "aasink"
//#define VSINK "cacasink"

#define UPDATE_INTERVAL 500

/* number of milliseconds to play for after a seek */
//#define SCRUB_TIME 250
//#define SCRUB

#define THREAD
#define PAD_SEEK

typedef struct
{
  const gchar *padname;
  GstPad *target;
  GstElement *bin;
}
dyn_link;

static GstElement *
gst_element_factory_make_or_warn (gchar * type, gchar * name)
{
  GstElement *element = gst_element_factory_make (type, name);

  if (!element) {
    g_warning ("Failed to create element %s of type %s", name, type);
  }

  return element;
}

static void
dynamic_link (GstPadTemplate * templ, GstPad * newpad, gpointer data)
{
  dyn_link *connect = (dyn_link *) data;

  if (connect->padname == NULL ||
      !strcmp (gst_pad_get_name (newpad), connect->padname)) {
    if (connect->bin)
      gst_bin_add (GST_BIN (pipeline), connect->bin);
    gst_pad_link (newpad, connect->target);

    //seekable_pads = g_list_prepend (seekable_pads, newpad);
    rate_pads = g_list_prepend (rate_pads, newpad);
  }
}

static void
setup_dynamic_link (GstElement * element, const gchar * padname,
    GstPad * target, GstElement * bin)
{
  dyn_link *connect;

  connect = g_new0 (dyn_link, 1);
  connect->padname = g_strdup (padname);
  connect->target = target;
  connect->bin = bin;

  g_signal_connect (G_OBJECT (element), "pad-added", G_CALLBACK (dynamic_link),
      connect);
}

static GstElement *
make_mod_pipeline (const gchar * location)
{
  GstElement *pipeline;
  GstElement *src, *decoder, *audiosink;
  GstPad *seekable;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  decoder = gst_element_factory_make_or_warn ("modplug", "decoder");
  audiosink = gst_element_factory_make_or_warn (ASINK, "sink");
  //g_object_set (G_OBJECT (audiosink), "sync", FALSE, NULL);

  g_object_set (G_OBJECT (src), "location", location, NULL);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), decoder);
  gst_bin_add (GST_BIN (pipeline), audiosink);

  gst_element_link (src, decoder);
  gst_element_link (decoder, audiosink);

  seekable = gst_element_get_pad (decoder, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, gst_element_get_pad (decoder, "sink"));

  return pipeline;
}

static GstElement *
make_dv_pipeline (const gchar * location)
{
  GstElement *pipeline;
  GstElement *src, *demux, *decoder, *audiosink, *videosink;
  GstElement *a_queue, *v_queue;
  GstPad *seekable;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  demux = gst_element_factory_make_or_warn ("dvdemux", "demuxer");
  v_queue = gst_element_factory_make_or_warn ("queue", "v_queue");
  decoder = gst_element_factory_make_or_warn ("ffdec_dvvideo", "decoder");
  videosink = gst_element_factory_make_or_warn ("xvimagesink", "v_sink");
  a_queue = gst_element_factory_make_or_warn ("queue", "a_queue");
  audiosink = gst_element_factory_make_or_warn ("alsasink", "a_sink");

  g_object_set (G_OBJECT (src), "location", location, NULL);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), demux);
  gst_bin_add (GST_BIN (pipeline), a_queue);
  gst_bin_add (GST_BIN (pipeline), audiosink);
  gst_bin_add (GST_BIN (pipeline), v_queue);
  gst_bin_add (GST_BIN (pipeline), decoder);
  gst_bin_add (GST_BIN (pipeline), videosink);

  gst_element_link (src, demux);
  gst_element_link (a_queue, audiosink);
  gst_element_link (v_queue, decoder);
  gst_element_link (decoder, videosink);

  setup_dynamic_link (demux, "video", gst_element_get_pad (v_queue, "sink"),
      NULL);
  setup_dynamic_link (demux, "audio", gst_element_get_pad (a_queue, "sink"),
      NULL);

  seekable = gst_element_get_pad (decoder, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);

  return pipeline;
}

static GstElement *
make_wav_pipeline (const gchar * location)
{
  GstElement *pipeline;
  GstElement *src, *decoder, *audiosink;
  GstPad *seekable;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  decoder = gst_element_factory_make_or_warn ("wavparse", "decoder");
  audiosink = gst_element_factory_make_or_warn (ASINK, "sink");
  //g_object_set (G_OBJECT (audiosink), "sync", FALSE, NULL);

  g_object_set (G_OBJECT (src), "location", location, NULL);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), decoder);
  gst_bin_add (GST_BIN (pipeline), audiosink);

  gst_element_link (src, decoder);
  gst_element_link (decoder, audiosink);

  seekable = gst_element_get_pad (decoder, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, gst_element_get_pad (decoder, "sink"));

  return pipeline;
}

static GstElement *
make_flac_pipeline (const gchar * location)
{
  GstElement *pipeline;
  GstElement *src, *decoder, *audiosink;
  GstPad *seekable;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  decoder = gst_element_factory_make_or_warn ("flacdec", "decoder");
  audiosink = gst_element_factory_make_or_warn (ASINK, "sink");
  g_object_set (G_OBJECT (audiosink), "sync", FALSE, NULL);

  g_object_set (G_OBJECT (src), "location", location, NULL);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), decoder);
  gst_bin_add (GST_BIN (pipeline), audiosink);

  gst_element_link (src, decoder);
  gst_element_link (decoder, audiosink);

  seekable = gst_element_get_pad (decoder, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, gst_element_get_pad (decoder, "sink"));

  return pipeline;
}

static GstElement *
make_sid_pipeline (const gchar * location)
{
  GstElement *pipeline;
  GstElement *src, *decoder, *audiosink;
  GstPad *seekable;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  decoder = gst_element_factory_make_or_warn ("siddec", "decoder");
  audiosink = gst_element_factory_make_or_warn (ASINK, "sink");
  //g_object_set (G_OBJECT (audiosink), "sync", FALSE, NULL);

  g_object_set (G_OBJECT (src), "location", location, NULL);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), decoder);
  gst_bin_add (GST_BIN (pipeline), audiosink);

  gst_element_link (src, decoder);
  gst_element_link (decoder, audiosink);

  seekable = gst_element_get_pad (decoder, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, gst_element_get_pad (decoder, "sink"));

  return pipeline;
}

static GstElement *
make_parse_pipeline (const gchar * location)
{
  GstElement *pipeline;
  GstElement *src, *parser, *fakesink;
  GstPad *seekable;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  parser = gst_element_factory_make_or_warn ("mpegparse", "parse");
  fakesink = gst_element_factory_make_or_warn ("fakesink", "sink");
  g_object_set (G_OBJECT (fakesink), "silent", TRUE, NULL);
  g_object_set (G_OBJECT (fakesink), "sync", TRUE, NULL);

  g_object_set (G_OBJECT (src), "location", location, NULL);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), parser);
  gst_bin_add (GST_BIN (pipeline), fakesink);

  gst_element_link (src, parser);
  gst_element_link (parser, fakesink);

  seekable = gst_element_get_pad (parser, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, gst_element_get_pad (parser, "sink"));

  return pipeline;
}

static GstElement *
make_vorbis_pipeline (const gchar * location)
{
  GstElement *pipeline, *audio_bin;
  GstElement *src, *demux, *decoder, *convert, *audiosink;
  GstPad *seekable;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  demux = gst_element_factory_make_or_warn ("oggdemux", "demux");
  decoder = gst_element_factory_make_or_warn ("vorbisdec", "decoder");
  convert = gst_element_factory_make_or_warn ("audioconvert", "convert");
  audiosink = gst_element_factory_make_or_warn (ASINK, "sink");
  g_object_set (G_OBJECT (audiosink), "sync", TRUE, NULL);

  g_object_set (G_OBJECT (src), "location", location, NULL);

  audio_bin = gst_bin_new ("a_decoder_bin");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), demux);
  gst_bin_add (GST_BIN (audio_bin), decoder);
  gst_bin_add (GST_BIN (audio_bin), convert);
  gst_bin_add (GST_BIN (audio_bin), audiosink);
  gst_bin_add (GST_BIN (pipeline), audio_bin);

  gst_element_link (src, demux);
  gst_element_link (decoder, convert);
  gst_element_link (convert, audiosink);

  setup_dynamic_link (demux, NULL, gst_element_get_pad (decoder, "sink"), NULL);

  seekable = gst_element_get_pad (decoder, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, gst_element_get_pad (decoder, "sink"));

  return pipeline;
}

static GstElement *
make_theora_pipeline (const gchar * location)
{
  GstElement *pipeline, *video_bin;
  GstElement *src, *demux, *decoder, *convert, *videosink;
  GstElement *queue;
  GstPad *seekable;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  demux = gst_element_factory_make_or_warn ("oggdemux", "demux");
  queue = gst_element_factory_make_or_warn ("queue", "queue");
  decoder = gst_element_factory_make_or_warn ("theoradec", "decoder");
  convert = gst_element_factory_make_or_warn ("ffmpegcolorspace", "convert");
  videosink = gst_element_factory_make_or_warn (VSINK, "sink");

  g_object_set (G_OBJECT (src), "location", location, NULL);

  video_bin = gst_bin_new ("v_decoder_bin");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), demux);
  gst_bin_add (GST_BIN (pipeline), queue);
  gst_bin_add (GST_BIN (video_bin), decoder);
  gst_bin_add (GST_BIN (video_bin), convert);
  gst_bin_add (GST_BIN (video_bin), videosink);
  gst_bin_add (GST_BIN (pipeline), video_bin);

  gst_element_link (src, demux);
  gst_element_link (queue, decoder);
  gst_element_link (decoder, convert);
  gst_element_link (convert, videosink);

  setup_dynamic_link (demux, NULL, gst_element_get_pad (queue, "sink"), NULL);

  seekable = gst_element_get_pad (decoder, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, gst_element_get_pad (decoder, "sink"));

  return pipeline;
}

static GstElement *
make_vorbis_theora_pipeline (const gchar * location)
{
  GstElement *pipeline, *audio_bin, *video_bin;
  GstElement *src, *demux, *a_decoder, *a_convert, *v_decoder, *v_convert;
  GstElement *audiosink, *videosink;
  GstElement *a_queue, *v_queue;
  GstPad *seekable;
  GstPad *pad;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  g_object_set (G_OBJECT (src), "location", location, NULL);

  demux = gst_element_factory_make_or_warn ("oggdemux", "demux");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), demux);
  gst_element_link (src, demux);

  audio_bin = gst_bin_new ("a_decoder_bin");
  a_queue = gst_element_factory_make_or_warn ("queue", "a_queue");
  a_decoder = gst_element_factory_make_or_warn ("vorbisdec", "a_dec");
  a_convert = gst_element_factory_make_or_warn ("audioconvert", "a_convert");
  audiosink = gst_element_factory_make_or_warn (ASINK, "a_sink");

  gst_bin_add (GST_BIN (pipeline), audio_bin);

  gst_bin_add (GST_BIN (audio_bin), a_queue);
  gst_bin_add (GST_BIN (audio_bin), a_decoder);
  gst_bin_add (GST_BIN (audio_bin), a_convert);
  gst_bin_add (GST_BIN (audio_bin), audiosink);

  gst_element_link (a_queue, a_decoder);
  gst_element_link (a_decoder, a_convert);
  gst_element_link (a_convert, audiosink);

  pad = gst_element_get_pad (a_queue, "sink");
  gst_element_add_pad (audio_bin, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (GST_OBJECT_CAST (pad));

  setup_dynamic_link (demux, NULL, gst_element_get_pad (audio_bin, "sink"),
      NULL);

  video_bin = gst_bin_new ("v_decoder_bin");
  v_queue = gst_element_factory_make_or_warn ("queue", "v_queue");
  v_decoder = gst_element_factory_make_or_warn ("theoradec", "v_dec");
  v_convert =
      gst_element_factory_make_or_warn ("ffmpegcolorspace", "v_convert");
  videosink = gst_element_factory_make_or_warn (VSINK, "v_sink");

  gst_bin_add (GST_BIN (pipeline), video_bin);

  gst_bin_add (GST_BIN (video_bin), v_queue);
  gst_bin_add (GST_BIN (video_bin), v_decoder);
  gst_bin_add (GST_BIN (video_bin), v_convert);
  gst_bin_add (GST_BIN (video_bin), videosink);

  gst_element_link_many (v_queue, v_decoder, v_convert, videosink, NULL);

  pad = gst_element_get_pad (v_queue, "sink");
  gst_element_add_pad (video_bin, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (GST_OBJECT_CAST (pad));

  setup_dynamic_link (demux, NULL, gst_element_get_pad (video_bin, "sink"),
      NULL);

  seekable = gst_element_get_pad (a_decoder, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads =
      g_list_prepend (rate_pads, gst_element_get_pad (a_decoder, "sink"));

  return pipeline;
}

static GstElement *
make_avi_msmpeg4v3_mp3_pipeline (const gchar * location)
{
  GstElement *pipeline, *audio_bin, *video_bin;
  GstElement *src, *demux, *a_decoder, *a_convert, *v_decoder, *v_convert;
  GstElement *audiosink, *videosink;
  GstElement *a_queue, *v_queue;
  GstPad *seekable;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  g_object_set (G_OBJECT (src), "location", location, NULL);

  demux = gst_element_factory_make_or_warn ("avidemux", "demux");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), demux);
  gst_element_link (src, demux);

  audio_bin = gst_bin_new ("a_decoder_bin");
  a_queue = gst_element_factory_make_or_warn ("queue", "a_queue");
  a_decoder = gst_element_factory_make_or_warn ("mad", "a_dec");
  a_convert = gst_element_factory_make_or_warn ("audioconvert", "a_convert");
  audiosink = gst_element_factory_make_or_warn (ASINK, "a_sink");

  gst_element_link (a_queue, a_decoder);
  gst_element_link (a_decoder, a_convert);
  gst_element_link (a_convert, audiosink);

  gst_bin_add (GST_BIN (audio_bin), a_queue);
  gst_bin_add (GST_BIN (audio_bin), a_decoder);
  gst_bin_add (GST_BIN (audio_bin), a_convert);
  gst_bin_add (GST_BIN (audio_bin), audiosink);

  gst_bin_add (GST_BIN (pipeline), audio_bin);

  setup_dynamic_link (demux, NULL, gst_element_get_pad (a_queue, "sink"), NULL);

  video_bin = gst_bin_new ("v_decoder_bin");
  v_queue = gst_element_factory_make_or_warn ("queue", "v_queue");
  v_decoder = gst_element_factory_make_or_warn ("ffdec_msmpeg4", "v_dec");
  v_convert =
      gst_element_factory_make_or_warn ("ffmpegcolorspace", "v_convert");
  videosink = gst_element_factory_make_or_warn (VSINK, "v_sink");
  gst_element_link_many (v_queue, v_decoder, v_convert, videosink, NULL);

  gst_bin_add (GST_BIN (video_bin), v_queue);
  gst_bin_add (GST_BIN (video_bin), v_decoder);
  gst_bin_add (GST_BIN (video_bin), v_convert);
  gst_bin_add (GST_BIN (video_bin), videosink);

  gst_bin_add (GST_BIN (pipeline), video_bin);

  setup_dynamic_link (demux, NULL, gst_element_get_pad (v_queue, "sink"), NULL);

  seekable = gst_element_get_pad (a_decoder, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads =
      g_list_prepend (rate_pads, gst_element_get_pad (a_decoder, "sink"));

  return pipeline;
}

static GstElement *
make_mp3_pipeline (const gchar * location)
{
  GstElement *pipeline;
  GstElement *src, *decoder, *osssink, *queue;
  GstPad *seekable;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  decoder = gst_element_factory_make_or_warn ("mad", "dec");
  queue = gst_element_factory_make_or_warn ("queue", "queue");
  osssink = gst_element_factory_make_or_warn (ASINK, "sink");

  seekable_elements = g_list_prepend (seekable_elements, osssink);

  g_object_set (G_OBJECT (src), "location", location, NULL);
  g_object_set (G_OBJECT (osssink), "fragment", 0x00180008, NULL);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), decoder);
  gst_bin_add (GST_BIN (pipeline), queue);
  gst_bin_add (GST_BIN (pipeline), osssink);

  gst_element_link (src, decoder);
  gst_element_link (decoder, queue);
  gst_element_link (queue, osssink);

  seekable = gst_element_get_pad (queue, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, gst_element_get_pad (decoder, "sink"));

  return pipeline;
}

static GstElement *
make_avi_pipeline (const gchar * location)
{
  GstElement *pipeline, *audio_bin, *video_bin;
  GstElement *src, *demux, *a_decoder, *v_decoder, *audiosink, *videosink;
  GstElement *a_queue = NULL, *v_queue = NULL;
  GstPad *seekable;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  g_object_set (G_OBJECT (src), "location", location, NULL);

  demux = gst_element_factory_make_or_warn ("avidemux", "demux");
  seekable_elements = g_list_prepend (seekable_elements, demux);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), demux);
  gst_element_link (src, demux);

  audio_bin = gst_bin_new ("a_decoder_bin");
  a_decoder = gst_element_factory_make_or_warn ("mad", "a_dec");
  audiosink = gst_element_factory_make_or_warn (ASINK, "a_sink");
  //g_object_set (G_OBJECT (audiosink), "fragment", 0x00180008, NULL);
  a_queue = gst_element_factory_make_or_warn ("queue", "a_queue");
  gst_element_link (a_decoder, a_queue);
  gst_element_link (a_queue, audiosink);
  gst_bin_add (GST_BIN (audio_bin), a_decoder);
  gst_bin_add (GST_BIN (audio_bin), a_queue);
  gst_bin_add (GST_BIN (audio_bin), audiosink);
  gst_element_set_state (audio_bin, GST_STATE_PAUSED);

  setup_dynamic_link (demux, "audio_00", gst_element_get_pad (a_decoder,
          "sink"), audio_bin);

  seekable = gst_element_get_pad (a_queue, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads =
      g_list_prepend (rate_pads, gst_element_get_pad (a_decoder, "sink"));

  video_bin = gst_bin_new ("v_decoder_bin");
  //v_decoder = gst_element_factory_make_or_warn ("identity", "v_dec");
  //v_decoder = gst_element_factory_make_or_warn ("windec", "v_dec");
  v_decoder = gst_element_factory_make_or_warn ("ffmpegdecall", "v_dec");
  videosink = gst_element_factory_make_or_warn ("ximagesink", "v_sink");
  //videosink = gst_element_factory_make_or_warn ("fakesink", "v_sink");
  //g_object_set (G_OBJECT (videosink), "sync", TRUE, NULL);
  v_queue = gst_element_factory_make_or_warn ("queue", "v_queue");
  //g_object_set (G_OBJECT (v_queue), "max_level", 10, NULL);
  gst_element_link (v_decoder, v_queue);
  gst_element_link (v_queue, videosink);
  gst_bin_add (GST_BIN (video_bin), v_decoder);
  gst_bin_add (GST_BIN (video_bin), v_queue);
  gst_bin_add (GST_BIN (video_bin), videosink);

  gst_element_set_state (video_bin, GST_STATE_PAUSED);

  setup_dynamic_link (demux, "video_00", gst_element_get_pad (v_decoder,
          "sink"), video_bin);

  seekable = gst_element_get_pad (v_queue, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads =
      g_list_prepend (rate_pads, gst_element_get_pad (v_decoder, "sink"));

  return pipeline;
}

static GstElement *
make_mpeg_pipeline (const gchar * location)
{
  GstElement *pipeline, *audio_bin, *video_bin;
  GstElement *src, *demux, *a_decoder, *v_decoder, *v_filter;
  GstElement *audiosink, *videosink;
  GstElement *a_queue, *v_queue;
  GstPad *seekable;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  g_object_set (G_OBJECT (src), "location", location, NULL);

  demux = gst_element_factory_make_or_warn ("mpegdemux", "demux");
  g_object_set (G_OBJECT (demux), "sync", FALSE, NULL);

  seekable_elements = g_list_prepend (seekable_elements, demux);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), demux);
  gst_element_link (src, demux);

  audio_bin = gst_bin_new ("a_decoder_bin");
  a_decoder = gst_element_factory_make_or_warn ("mad", "a_dec");
  a_queue = gst_element_factory_make_or_warn ("queue", "a_queue");
  audiosink = gst_element_factory_make_or_warn (ASINK, "a_sink");
  g_object_set (G_OBJECT (audiosink), "fragment", 0x00180008, NULL);
  gst_element_link (a_decoder, a_queue);
  gst_element_link (a_queue, audiosink);
  gst_bin_add (GST_BIN (audio_bin), a_decoder);
  gst_bin_add (GST_BIN (audio_bin), a_queue);
  gst_bin_add (GST_BIN (audio_bin), audiosink);

  setup_dynamic_link (demux, "audio_00", gst_element_get_pad (a_decoder,
          "sink"), audio_bin);

  seekable = gst_element_get_pad (a_queue, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads =
      g_list_prepend (rate_pads, gst_element_get_pad (a_decoder, "sink"));

  video_bin = gst_bin_new ("v_decoder_bin");
  v_decoder = gst_element_factory_make_or_warn ("mpeg2dec", "v_dec");
  //g_object_set (G_OBJECT (video_thread), "priority", 2, NULL);
  v_queue = gst_element_factory_make_or_warn ("queue", "v_queue");
  v_filter = gst_element_factory_make_or_warn ("ffmpegcolorspace", "v_filter");
  videosink = gst_element_factory_make_or_warn ("ximagesink", "v_sink");
  gst_element_link_many (v_decoder, v_queue, v_filter, NULL);

  gst_element_link (v_filter, videosink);
  gst_bin_add_many (GST_BIN (video_bin), v_decoder, NULL);
  gst_bin_add_many (GST_BIN (video_bin), v_queue, v_filter, videosink, NULL);

  setup_dynamic_link (demux, "video_00", gst_element_get_pad (v_decoder,
          "sink"), video_bin);

  seekable = gst_element_get_pad (v_queue, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads =
      g_list_prepend (rate_pads, gst_element_get_pad (v_decoder, "sink"));

  return pipeline;
}

static GstElement *
make_mpegnt_pipeline (const gchar * location)
{
  GstElement *pipeline, *audio_bin, *video_bin;
  GstElement *src, *demux, *a_decoder, *v_decoder, *v_filter;
  GstElement *audiosink, *videosink;
  GstElement *a_queue;
  GstPad *seekable;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  g_object_set (G_OBJECT (src), "location", location, NULL);

  demux = gst_element_factory_make_or_warn ("mpegdemux", "demux");
  //g_object_set (G_OBJECT (demux), "sync", TRUE, NULL);

  seekable_elements = g_list_prepend (seekable_elements, demux);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), demux);
  gst_element_link (src, demux);

  audio_bin = gst_bin_new ("a_decoder_bin");
  a_decoder = gst_element_factory_make_or_warn ("mad", "a_dec");
  a_queue = gst_element_factory_make_or_warn ("queue", "a_queue");
  audiosink = gst_element_factory_make_or_warn (ASINK, "a_sink");
  //g_object_set (G_OBJECT (audiosink), "fragment", 0x00180008, NULL);
  g_object_set (G_OBJECT (audiosink), "sync", FALSE, NULL);
  gst_element_link (a_decoder, a_queue);
  gst_element_link (a_queue, audiosink);
  gst_bin_add (GST_BIN (audio_bin), a_decoder);
  gst_bin_add (GST_BIN (audio_bin), a_queue);
  gst_bin_add (GST_BIN (audio_bin), audiosink);

  setup_dynamic_link (demux, "audio_00", gst_element_get_pad (a_decoder,
          "sink"), audio_bin);

  seekable = gst_element_get_pad (a_queue, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads =
      g_list_prepend (rate_pads, gst_element_get_pad (a_decoder, "sink"));

  video_bin = gst_bin_new ("v_decoder_bin");
  v_decoder = gst_element_factory_make_or_warn ("mpeg2dec", "v_dec");
  v_filter = gst_element_factory_make_or_warn ("ffmpegcolorspace", "v_filter");
  videosink = gst_element_factory_make_or_warn ("ximagesink", "v_sink");
  gst_element_link_many (v_decoder, v_filter, videosink, NULL);

  gst_bin_add_many (GST_BIN (video_bin), v_decoder, v_filter, videosink, NULL);

  setup_dynamic_link (demux, "video_00", gst_element_get_pad (v_decoder,
          "sink"), video_bin);

  seekable = gst_element_get_pad (v_decoder, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads =
      g_list_prepend (rate_pads, gst_element_get_pad (v_decoder, "sink"));

  return pipeline;
}

static GstElement *
make_playerbin_pipeline (const gchar * location)
{
  GstElement *player;

  player = gst_element_factory_make ("playbin", "player");
  g_assert (player);

  g_object_set (G_OBJECT (player), "uri", location, NULL);

  seekable_elements = g_list_prepend (seekable_elements, player);

  /* force element seeking on this pipeline */
  elem_seek = TRUE;

  return player;
}

static gchar *
format_value (GtkScale * scale, gdouble value)
{
  gint64 real;
  gint64 seconds;
  gint64 subseconds;

  real = value * duration / 100;
  seconds = (gint64) real / GST_SECOND;
  subseconds = (gint64) real / (GST_SECOND / 100);

  return g_strdup_printf ("%02" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT ":%02"
      G_GINT64_FORMAT, seconds / 60, seconds % 60, subseconds % 100);
}

typedef struct
{
  const gchar *name;
  const GstFormat format;
}
seek_format;

static seek_format seek_formats[] = {
  {"tim", GST_FORMAT_TIME},
  {"byt", GST_FORMAT_BYTES},
  {"buf", GST_FORMAT_BUFFERS},
  {"def", GST_FORMAT_DEFAULT},
  {NULL, 0},
};

G_GNUC_UNUSED static void
query_rates (void)
{
  GList *walk = rate_pads;

  while (walk) {
    GstPad *pad = GST_PAD (walk->data);
    gint i = 0;

    g_print ("rate/sec  %8.8s: ", GST_PAD_NAME (pad));
    while (seek_formats[i].name) {
      gint64 value;
      GstFormat format;

      format = seek_formats[i].format;

      if (gst_pad_query_convert (pad, GST_FORMAT_TIME, GST_SECOND, &format,
              &value)) {
        g_print ("%s %13" G_GINT64_FORMAT " | ", seek_formats[i].name, value);
      } else {
        g_print ("%s %13.13s | ", seek_formats[i].name, "*NA*");
      }

      i++;
    }
    g_print (" %s:%s\n", GST_DEBUG_PAD_NAME (pad));

    walk = g_list_next (walk);
  }
}

G_GNUC_UNUSED static void
query_positions_elems ()
{
  GList *walk = seekable_elements;

  while (walk) {
    GstElement *element = GST_ELEMENT (walk->data);
    gint i = 0;

    g_print ("positions %8.8s: ", GST_ELEMENT_NAME (element));
    while (seek_formats[i].name) {
      gint64 position, total;
      GstFormat format;

      format = seek_formats[i].format;

      if (gst_element_query_position (element, &format, &position, &total)) {
        g_print ("%s %13" G_GINT64_FORMAT " / %13" G_GINT64_FORMAT " | ",
            seek_formats[i].name, position, total);
      } else {
        g_print ("%s %13.13s / %13.13s | ", seek_formats[i].name, "*NA*",
            "*NA*");
      }
      i++;
    }
    g_print (" %s\n", GST_ELEMENT_NAME (element));

    walk = g_list_next (walk);
  }
}

G_GNUC_UNUSED static void
query_positions_pads ()
{
  GList *walk = seekable_pads;

  while (walk) {
    GstPad *pad = GST_PAD (walk->data);
    gint i = 0;

    g_print ("positions %8.8s: ", GST_PAD_NAME (pad));
    while (seek_formats[i].name) {
      GstFormat format;
      gint64 position, total;

      format = seek_formats[i].format;

      if (gst_pad_query_position (pad, &format, &position, &total)) {
        g_print ("%s %13" G_GINT64_FORMAT " / %13" G_GINT64_FORMAT " | ",
            seek_formats[i].name, position, total);
      } else {
        g_print ("%s %13.13s / %13.13s | ", seek_formats[i].name, "*NA*",
            "*NA*");
      }

      i++;
    }
    g_print (" %s:%s\n", GST_DEBUG_PAD_NAME (pad));

    walk = g_list_next (walk);
  }
}

static gboolean
update_scale (gpointer data)
{
  GstClock *clock;
  GstFormat format;

  position = 0;
  duration = 0;
  clock = gst_pipeline_get_clock (GST_PIPELINE (pipeline));

  format = GST_FORMAT_TIME;

  if (elem_seek) {
    if (seekable_elements) {
      GstElement *element = GST_ELEMENT (seekable_elements->data);

      gst_element_query_position (element, &format, &position, &duration);
    }
  } else {
    if (seekable_pads) {
      GstPad *pad = GST_PAD (seekable_pads->data);

      gst_pad_query_position (pad, &format, &position, &duration);
    }
  }

  if (stats) {
    if (clock) {
      g_print ("clock:                  %13" G_GUINT64_FORMAT "  (%s)\n",
          position, gst_object_get_name (GST_OBJECT (clock)));
    }

    if (elem_seek) {
      query_positions_elems ();
    } else {
      query_positions_pads ();
    }
    query_rates ();
  }
  if (position >= duration)
    duration = position;

  if (duration > 0) {
    gtk_adjustment_set_value (adjustment, position * 100.0 / duration);
    gtk_widget_queue_draw (hscale);
  }

  gst_object_unref (clock);

  return TRUE;
}

static void do_seek (GtkWidget * widget);

#ifdef SCRUB
static gboolean
end_scrub (GtkWidget * widget)
{
  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  seek_timeout_id = 0;

  return FALSE;
}
#endif

static void
do_seek (GtkWidget * widget)
{
  gint64 real = gtk_range_get_value (GTK_RANGE (widget)) * duration / 100;
  gboolean res = FALSE;
  GstEvent *s_event;

  if (!elem_seek) {
    GList *walk = seekable_pads;

    while (walk) {
      GstPad *seekable = GST_PAD (walk->data);

      g_print ("seek to %" GST_TIME_FORMAT " on pad %s:%s\n",
          GST_TIME_ARGS (real), GST_DEBUG_PAD_NAME (seekable));


      s_event = gst_event_new_seek (1.0,
          GST_FORMAT_TIME,
          GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, real, GST_SEEK_TYPE_NONE, 0);

      res = gst_pad_send_event (seekable, s_event);

      walk = g_list_next (walk);
    }
  } else {
    GList *walk = seekable_elements;

    while (walk) {
      GstElement *seekable = GST_ELEMENT (walk->data);

      g_print ("seek to %" GST_TIME_FORMAT " on element %s\n",
          GST_TIME_ARGS (real), GST_ELEMENT_NAME (seekable));

      s_event = gst_event_new_seek (1.0,
          GST_FORMAT_TIME,
          GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, real, GST_SEEK_TYPE_NONE, 0);

      res = gst_element_send_event (seekable, s_event);

      walk = g_list_next (walk);
    }
  }

  if (res)
    gst_pipeline_set_new_stream_time (GST_PIPELINE (pipeline), 0);
  else
    g_print ("seek failed\n");
}

static void
seek_cb (GtkWidget * widget)
{
#ifndef SCRUB
  GTimeVal timeval;
#else
  /* If the timer hasn't expired yet, then the pipeline is running */
  if (seek_timeout_id != 0) {
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
  }
#endif

  do_seek (widget);

#ifndef SCRUB
  /* wait for preroll */
  GST_TIME_TO_TIMEVAL (50 * GST_MSECOND, timeval);
  gst_element_get_state (pipeline, NULL, NULL, &timeval);
#endif

#ifdef SCRUB
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  if (seek_timeout_id == 0) {
    seek_timeout_id =
        gtk_timeout_add (SCRUB_TIME, (GSourceFunc) end_scrub, widget);
  }
#endif
}

static gboolean
start_seek (GtkWidget * widget, GdkEventButton * event, gpointer user_data)
{
  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  gtk_timeout_remove (update_id);

  if (changed_id == 0) {
    changed_id = gtk_signal_connect (GTK_OBJECT (hscale),
        "value_changed", G_CALLBACK (seek_cb), pipeline);
  }

  return FALSE;
}

static gboolean
stop_seek (GtkWidget * widget, gpointer user_data)
{
  g_signal_handler_disconnect (GTK_OBJECT (hscale), changed_id);
  changed_id = 0;
  if (seek_timeout_id != 0) {
    gtk_timeout_remove (seek_timeout_id);
    seek_timeout_id = 0;
    /* Still scrubbing, so the pipeline is already playing */
  } else {
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
  }

  update_id =
      gtk_timeout_add (UPDATE_INTERVAL, (GtkFunction) update_scale, pipeline);

  return FALSE;
}

static void
play_cb (GtkButton * button, gpointer data)
{
  GstElementState state;

  gst_element_get_state (pipeline, &state, NULL, NULL);
  if (state != GST_STATE_PLAYING) {
    g_print ("PLAY pipeline\n");
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    update_id =
        gtk_timeout_add (UPDATE_INTERVAL, (GtkFunction) update_scale, pipeline);
  }
}

static void
pause_cb (GtkButton * button, gpointer data)
{
  GstElementState state;

  gst_element_get_state (pipeline, &state, NULL, NULL);
  if (state != GST_STATE_PAUSED) {
    g_print ("PAUSE pipeline\n");
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    gtk_timeout_remove (update_id);
  }
}

static void
stop_cb (GtkButton * button, gpointer data)
{
  GstElementState state;

  gst_element_get_state (pipeline, &state, NULL, NULL);
  if (state != GST_STATE_READY) {
    g_print ("READY pipeline\n");
    gst_element_set_state (pipeline, GST_STATE_READY);
    gtk_adjustment_set_value (adjustment, 0.0);
    gtk_timeout_remove (update_id);
  }
}

typedef struct
{
  gchar *name;
  GstElement *(*func) (const gchar * location);
}
Pipeline;

static Pipeline pipelines[] = {
  {"mp3", make_mp3_pipeline},
  {"avi", make_avi_pipeline},
  {"mpeg1", make_mpeg_pipeline},
  {"mpegparse", make_parse_pipeline},
  {"vorbis", make_vorbis_pipeline},
  {"theora", make_theora_pipeline},
  {"ogg/v/t", make_vorbis_theora_pipeline},
  {"avi/msmpeg4v3/mp3", make_avi_msmpeg4v3_mp3_pipeline},
  {"sid", make_sid_pipeline},
  {"flac", make_flac_pipeline},
  {"wav", make_wav_pipeline},
  {"mod", make_mod_pipeline},
  {"dv", make_dv_pipeline},
  {"mpeg1nothreads", make_mpegnt_pipeline},
  {"playerbin", make_playerbin_pipeline},
  {NULL, NULL},
};

#define NUM_TYPES	((sizeof (pipelines) / sizeof (Pipeline)) - 1)

static void
print_usage (int argc, char **argv)
{
  gint i;

  g_print ("usage: %s <type> <filename>\n", argv[0]);
  g_print ("   possible types:\n");

  for (i = 0; i < NUM_TYPES; i++) {
    g_print ("     %d = %s\n", i, pipelines[i].name);
  }
}

int
main (int argc, char **argv)
{
  GtkWidget *window, *hbox, *vbox, *play_button, *pause_button, *stop_button;
  struct poptOption options[] = {
    {"stats", 's', POPT_ARG_NONE | POPT_ARGFLAG_STRIP, &stats, 0,
        "Show pad stats", NULL},
    {"elem", 'e', POPT_ARG_NONE | POPT_ARGFLAG_STRIP, &elem_seek, 0,
        "Seek on elements instead of pads", NULL},
    {"verbose", 'v', POPT_ARG_NONE | POPT_ARGFLAG_STRIP, &verbose, 0,
        "Verbose properties", NULL},
    POPT_TABLEEND
  };
  gint type;

  gst_init_with_popt_table (&argc, &argv, options);
  gtk_init (&argc, &argv);

  if (argc != 3) {
    print_usage (argc, argv);
    exit (-1);
  }

  type = atoi (argv[1]);

  if (type < 0 || type >= NUM_TYPES) {
    print_usage (argc, argv);
    exit (-1);
  }

  pipeline = pipelines[type].func (argv[2]);
  g_assert (pipeline);

  /* initialize gui elements ... */
  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  hbox = gtk_hbox_new (FALSE, 0);
  vbox = gtk_vbox_new (FALSE, 0);
  play_button = gtk_button_new_with_label ("play");
  pause_button = gtk_button_new_with_label ("pause");
  stop_button = gtk_button_new_with_label ("stop");

  adjustment =
      GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.00, 100.0, 0.1, 1.0, 1.0));
  hscale = gtk_hscale_new (adjustment);
  gtk_scale_set_digits (GTK_SCALE (hscale), 2);
  gtk_range_set_update_policy (GTK_RANGE (hscale), GTK_UPDATE_CONTINUOUS);

  gtk_signal_connect (GTK_OBJECT (hscale),
      "button_press_event", G_CALLBACK (start_seek), pipeline);
  gtk_signal_connect (GTK_OBJECT (hscale),
      "button_release_event", G_CALLBACK (stop_seek), pipeline);
  gtk_signal_connect (GTK_OBJECT (hscale),
      "format_value", G_CALLBACK (format_value), pipeline);

  /* do the packing stuff ... */
  gtk_window_set_default_size (GTK_WINDOW (window), 96, 96);
  gtk_container_add (GTK_CONTAINER (window), vbox);
  gtk_container_add (GTK_CONTAINER (vbox), hbox);
  gtk_box_pack_start (GTK_BOX (hbox), play_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (hbox), pause_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (hbox), stop_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (vbox), hscale, TRUE, TRUE, 2);

  /* connect things ... */
  g_signal_connect (G_OBJECT (play_button), "clicked", G_CALLBACK (play_cb),
      pipeline);
  g_signal_connect (G_OBJECT (pause_button), "clicked", G_CALLBACK (pause_cb),
      pipeline);
  g_signal_connect (G_OBJECT (stop_button), "clicked", G_CALLBACK (stop_cb),
      pipeline);
  g_signal_connect (G_OBJECT (window), "delete_event", gtk_main_quit, NULL);

  /* show the gui. */
  gtk_widget_show_all (window);

  if (verbose) {
    g_signal_connect (pipeline, "deep_notify",
        G_CALLBACK (gst_object_default_deep_notify), NULL);
  }
  gtk_main ();

  g_print ("NULL pipeline\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_print ("free pipeline\n");
  gst_object_unref (pipeline);

  return 0;
}
