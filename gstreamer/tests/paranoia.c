#include <gst/gst.h>

void paranoia_eos(GstPad *pad) {
  gst_element_set_state(GST_ELEMENT(gst_pad_get_parent(pad)),GST_STATE_READY);
  fprintf(stderr,"PARANOIA: have eos signal\n");
}

int main(int argc,char *argv[]) {
  GstPipeline *pipeline;
  GstElement *paranoia,*queue,*audio_thread,*osssink;
  //int track = (argc == 2) ? atoi(argv[1]) : 1;

  GST_DEBUG_ENTER("(%d)",argc);

  gst_init(&argc,&argv);

  pipeline = GST_PIPELINE(gst_pipeline_new("paranoia"));
  g_return_val_if_fail(pipeline != NULL,1);
  audio_thread = gst_thread_new("audio_thread");
  g_return_val_if_fail(audio_thread != NULL,2);

  paranoia = gst_elementfactory_make("cdparanoia","paranoia");
  g_return_val_if_fail(paranoia != NULL,3);
  g_object_set(G_OBJECT(paranoia),"paranoia_mode",0,NULL);
//  g_object_set(G_OBJECT(paranoia),"start_sector",0,"end_sector",75,NULL);

  queue = gst_elementfactory_make("queue","queue");
  g_object_set(G_OBJECT(queue),"max_level",750,NULL);
  g_return_val_if_fail(queue != NULL,4);

  osssink = gst_elementfactory_make("fakesink","osssink");
  g_return_val_if_fail(osssink != NULL,4);

  gst_bin_add(GST_BIN(pipeline),paranoia);
  gst_bin_add(GST_BIN(pipeline),queue);
  gst_bin_add(GST_BIN(audio_thread),osssink);
  gst_bin_add(GST_BIN(pipeline),audio_thread);
  gst_element_add_ghost_pad(GST_ELEMENT(audio_thread),gst_element_get_pad(osssink,"sink"),"sink");

  gst_element_connect(paranoia,"src",queue,"sink");
  gst_element_connect(queue,"src",audio_thread,"sink");

  g_signal_connect(G_OBJECT(gst_element_get_pad(paranoia,"src")),"eos",
    G_CALLBACK(paranoia_eos),NULL);

  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);
  if (GST_STATE(paranoia) != GST_STATE_PLAYING) fprintf(stderr,"error: state not set\n");

  while (1) {
    gst_bin_iterate(GST_BIN(pipeline));
  }
}
