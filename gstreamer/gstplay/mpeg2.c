
#define BUFFER 20
#define VIDEO_DECODER "mpeg2play"

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gnome.h>
#include <gst/gst.h>

#include "codecs.h"


extern gboolean _gst_plugin_spew;
extern GstElement *video_render_queue, *audio_render_queue;
GstElement *merge_subtitles;

void mpeg2_new_pad_created(GstElement *parse,GstPad *pad,GstElement *pipeline) 
{
  GstElement *parse_audio, *decode;
  GstElement *audio_queue;
  GstElement *audio_thread;

  g_print("***** a new pad %s was created\n", gst_pad_get_name(pad));

  // connect to audio pad
  if (strncmp(gst_pad_get_name(pad), "video_", 6) == 0) {
    gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PAUSED);
    mpeg2_setup_video_thread(pad, video_render_queue, pipeline);
    gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);
    return;
  }
  else if (strncmp(gst_pad_get_name(pad), "private_stream_1.0", 18) == 0) {
    gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PAUSED);
    gst_plugin_load("ac3parse");
    gst_plugin_load("ac3dec");
    // construct internal pipeline elements
    parse_audio = gst_elementfactory_make("ac3parse","parse_audio");
    g_return_if_fail(parse_audio != NULL);
    decode = gst_elementfactory_make("ac3dec","decode_audio");
    g_return_if_fail(decode != NULL);
  } else if (strncmp(gst_pad_get_name(pad), "subtitle_stream_4", 17) == 0) {
    gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PAUSED);
    gst_pad_connect(pad,
                    gst_element_get_pad(merge_subtitles,"subtitle"));
    gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);
    return;
  }
  else if (strncmp(gst_pad_get_name(pad), "audio_", 6) == 0) {
    gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PAUSED);
    gst_plugin_load("mp3parse");
    gst_plugin_load("mpg123");
    // construct internal pipeline elements
    parse_audio = gst_elementfactory_make("mp3parse","parse_audio");
    g_return_if_fail(parse_audio != NULL);
    decode = gst_elementfactory_make("mpg123","decode_audio");
    g_return_if_fail(decode != NULL);
  }
  else {
    return;
  }

  // create the thread and pack stuff into it
  audio_thread = gst_thread_new("audio_thread");
  g_return_if_fail(audio_thread != NULL);
  gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(parse_audio));
  gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(decode));

  // set up pad connections
  gst_element_add_ghost_pad(GST_ELEMENT(audio_thread),
                            gst_element_get_pad(parse_audio,"sink"));
  gst_pad_connect(gst_element_get_pad(parse_audio,"src"),
                  gst_element_get_pad(decode,"sink"));
  gst_pad_connect(gst_element_get_pad(decode,"src"),
                  gst_element_get_pad(audio_render_queue,"sink"));

  // construct queue and connect everything in the main pipelie
  audio_queue = gst_elementfactory_make("queue","audio_queue");
  gtk_object_set(GTK_OBJECT(audio_queue),"max_level",30,NULL);
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(audio_queue));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(audio_thread));
  gst_pad_connect(pad,
                  gst_element_get_pad(audio_queue,"sink"));
  gst_pad_connect(gst_element_get_pad(audio_queue,"src"),
                  gst_element_get_pad(audio_thread,"sink"));

  // set up thread state and kick things off
  gtk_object_set(GTK_OBJECT(audio_thread),"create_thread",TRUE,NULL);
  g_print("setting to READY state\n");
  gst_element_set_state(GST_ELEMENT(audio_thread),GST_STATE_READY);

  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);
}

void mpeg2_setup_video_thread(GstPad *pad, GstElement *show, GstElement *pipeline)
{
  GstElement *parse_video, *decode_video;
  GstElement *video_queue;
  GstElement *video_thread;

  gst_plugin_load("mp1videoparse");
  gst_plugin_load(VIDEO_DECODER);
  gst_plugin_load("mpeg2subt");
  // construct internal pipeline elements
  parse_video = gst_elementfactory_make("mp1videoparse","parse_video");
  g_return_if_fail(parse_video != NULL);
  decode_video = gst_elementfactory_make(VIDEO_DECODER,"decode_video");
  g_return_if_fail(decode_video != NULL);
  merge_subtitles = gst_elementfactory_make("mpeg2subt","merge_subtitles");
  g_return_if_fail(merge_subtitles != NULL);

  // create the thread and pack stuff into it
  video_thread = gst_thread_new("video_thread");
  g_return_if_fail(video_thread != NULL);
  gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(parse_video));
  gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(decode_video));
  gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(merge_subtitles));
  gst_bin_use_cothreads(GST_BIN(video_thread), FALSE);

  // set up pad connections
  gst_element_add_ghost_pad(GST_ELEMENT(video_thread),
                            gst_element_get_pad(parse_video,"sink"));
  gst_pad_connect(gst_element_get_pad(parse_video,"src"),
                  gst_element_get_pad(decode_video,"sink"));
  gst_pad_connect(gst_element_get_pad(decode_video,"src"),
                  gst_element_get_pad(merge_subtitles,"video"));
  gst_pad_connect(gst_element_get_pad(merge_subtitles,"src"),
                  gst_element_get_pad(video_render_queue,"sink"));

  // construct queue and connect everything in the main pipeline
  video_queue = gst_elementfactory_make("queue","video_queue");
  gtk_object_set(GTK_OBJECT(video_queue),"max_level",BUFFER,NULL);
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(video_queue));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(video_thread));
  gst_pad_connect(pad,
                  gst_element_get_pad(video_queue,"sink"));
  gst_pad_connect(gst_element_get_pad(video_queue,"src"),
                  gst_element_get_pad(video_thread,"sink"));

  // set up thread state and kick things off
  gtk_object_set(GTK_OBJECT(video_thread),"create_thread",TRUE,NULL);
  g_print("setting to RUNNING state\n");
  gst_element_set_state(GST_ELEMENT(video_thread),GST_STATE_READY);
  
  g_print("\n");
}

