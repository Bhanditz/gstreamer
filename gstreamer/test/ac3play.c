#include <gst/gst.h>

#include "mem.h"

extern gboolean _gst_plugin_spew;

int main(int argc,char *argv[]) {
  GstElement *pipeline, *decodethread;
  GstElement *src, *parse, *decode, *play;
  GstElement *queue;

//  g_print("have %d args\n",argc);

//  _gst_plugin_spew = TRUE;
  gst_init(&argc,&argv);

  pipeline = gst_elementfactory_make("pipeline","ac3player");
  g_return_val_if_fail(pipeline != NULL, -1);
  decodethread = gst_elementfactory_make("thread","decodethread");
  g_return_val_if_fail(decodethread != NULL, -1);
  queue = gst_elementfactory_make("queue","queue");
  g_return_val_if_fail(queue != NULL, -1);

  src = gst_elementfactory_make("disksrc","src");
  g_return_val_if_fail(src != NULL, -1);
  g_object_set(G_OBJECT(src),"location",argv[1],NULL);
//  g_print("should be using file '%s'\n",argv[1]);
  parse = gst_elementfactory_make("ac3parse","parse");
  g_return_val_if_fail(parse != NULL, -1);
  decode = gst_elementfactory_make("ac3dec","decode");
  g_return_val_if_fail(decode != NULL, -1);
  play = gst_elementfactory_make("osssink","play");
  g_return_val_if_fail(play != NULL, -1);

  // construct the decode thread
  g_print("constructing the decode thread\n");
  gst_bin_add(GST_BIN(decodethread),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(decodethread),GST_ELEMENT(parse));
  gst_bin_add(GST_BIN(decodethread),GST_ELEMENT(decode));
  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(parse,"sink"));
  gst_pad_connect(gst_element_get_pad(parse,"src"),
                  gst_element_get_pad(decode,"sink"));
  gst_element_add_ghost_pad(GST_ELEMENT(decodethread),
                            gst_element_get_pad(decode,"src"),"src");

  // construct the outer pipeline
  g_print("constructing the main pipeline\n");
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(decodethread));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(queue));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(play));
  g_print("connecting main pipeline\n");
  gst_pad_connect(gst_element_get_pad(decodethread,"src"),
                  gst_element_get_pad(queue,"sink"));
  gst_pad_connect(gst_element_get_pad(queue,"src"),
                  gst_element_get_pad(play,"sink"));

#ifndef GST_DISABLE_LOADSAVE
  xmlSaveFile("ac3play.gst", gst_xml_write(GST_ELEMENT(pipeline)));
#endif

  // set thread start state
  g_object_set(G_OBJECT(decodethread),"create_thread",TRUE,NULL);

  g_print("setting to READY state\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);

//  sleep(1);
//  g_print("about to enter loop\n");
  while (1) {
    gst_bin_iterate(GST_BIN(pipeline));
  }
}
