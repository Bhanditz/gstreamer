#include <gnome.h>
#include <gst/gst.h>

GstPipeline *pipeline;
GstElement *v_queue, *a_queue, *v_thread, *a_thread;
GtkWidget *appwindow;
GtkWidget  *gtk_socket;

void eof(GstElement *src) {
  g_print("have eos, quitting\n");
  exit(0);
}

gboolean idle_func(gpointer data) {
  gst_bin_iterate(GST_BIN(data));
  return TRUE;
}

void mpeg2parse_newpad(GstElement *parser,GstPad *pad, GstElement *pipeline) {

  g_print("***** a new pad %s was created\n", gst_pad_get_name(pad));

  if (strncmp(gst_pad_get_name(pad), "video_", 6) == 0) {
    gst_pad_connect(pad, gst_element_get_pad(v_queue,"sink"));
    gst_bin_add(GST_BIN(pipeline),v_thread);
    gst_element_set_state(v_thread,GST_STATE_PLAYING);
  } else if (strcmp(gst_pad_get_name(pad), "private_stream_1.0") == 0) {
    gst_pad_connect(pad, gst_element_get_pad(a_queue,"sink"));
    gst_bin_add(GST_BIN(pipeline),a_thread);
    gst_element_set_state(a_thread,GST_STATE_PLAYING);
  }
}

void mpeg2parse_have_size(GstElement *videosink,gint width,gint height) {
  gtk_widget_set_usize(gtk_socket,width,height);
  gtk_widget_show_all(appwindow);
}

int main(int argc,char *argv[]) {
  GstElement *src, *parse;
  GstElement *v_decode, *show, *color;
  GstElement *a_decode, *osssink;

  g_print("have %d args\n",argc);

  gst_init(&argc,&argv);
  gnome_init("MPEG2 Video player","0.0.1",argc,argv);

  // ***** construct the main pipeline *****
  pipeline = GST_PIPELINE(gst_pipeline_new("pipeline"));
  g_return_val_if_fail(pipeline != NULL, -1);

  if (strstr(argv[1],"video_ts")) {
    src = gst_elementfactory_make("dvdsrc","src");
    g_print("using DVD source\n");
  } else {
    src = gst_elementfactory_make("disksrc","src");
  }

  g_return_val_if_fail(src != NULL, -1);
  gtk_object_set(GTK_OBJECT(src),"location",argv[1],NULL);
  if (argc >= 3) {
    gtk_object_set(GTK_OBJECT(src),"bytesperread",atoi(argv[2]),NULL);
    g_print("block size is %d\n",atoi(argv[2]));
  }
  g_print("should be using file '%s'\n",argv[1]);

  parse = gst_elementfactory_make("mpeg2parse","parse");
  //parse = gst_elementfactory_make("mpeg1parse","parse");
  g_return_val_if_fail(parse != NULL, -1);

  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(parse));

  gst_element_connect(src,"src",parse,"sink");


  // ***** pre-construct the video thread *****
  v_thread = GST_ELEMENT(gst_thread_new("v_thread"));
  g_return_val_if_fail(v_thread != NULL, -1);

  v_queue = gst_elementfactory_make("queue","v_queue");
  g_return_val_if_fail(v_queue != NULL, -1);

  v_decode = gst_elementfactory_make("mpeg2dec","decode_video");
  g_return_val_if_fail(v_decode != NULL, -1);

  color = gst_elementfactory_make("colorspace","color");
  g_return_val_if_fail(color != NULL, -1);

  show = gst_elementfactory_make("xvideosink","show");
  g_return_val_if_fail(show != NULL, -1);

  gst_bin_add(GST_BIN(v_thread),GST_ELEMENT(v_queue));
  gst_bin_add(GST_BIN(v_thread),GST_ELEMENT(v_decode));
  gst_bin_add(GST_BIN(v_thread),GST_ELEMENT(color));
  gst_bin_add(GST_BIN(v_thread),GST_ELEMENT(show));

  gst_element_connect(v_queue,"src",v_decode,"sink");
  gst_element_connect(v_decode,"src",color,"sink");
  gst_element_connect(color,"src",show,"sink");


  // ***** pre-construct the audio thread *****
  a_thread = GST_ELEMENT(gst_thread_new("a_thread"));
  g_return_val_if_fail(a_thread != NULL, -1);

  a_queue = gst_elementfactory_make("queue","a_queue");
  g_return_val_if_fail(a_queue != NULL, -1);
  
  a_decode = gst_elementfactory_make("a52dec","decode_audio");
  g_return_val_if_fail(a_decode != NULL, -1);

  osssink = gst_elementfactory_make("osssink","osssink");
  g_return_val_if_fail(osssink != NULL, -1);

  gst_bin_add(GST_BIN(a_thread),GST_ELEMENT(a_queue));
  gst_bin_add(GST_BIN(a_thread),GST_ELEMENT(a_decode));
  gst_bin_add(GST_BIN(a_thread),GST_ELEMENT(osssink));

  gst_element_connect(a_queue,"src",a_decode,"sink");
  gst_element_connect(a_decode,"src",osssink,"sink");


  // ***** construct the GUI *****
  appwindow = gnome_app_new("MPEG player","MPEG player");

  gtk_socket = gtk_socket_new ();
  gtk_widget_show (gtk_socket);

  gnome_app_set_contents(GNOME_APP(appwindow),
      	        GTK_WIDGET(gtk_socket));

  gtk_widget_realize (gtk_socket);
  gtk_socket_steal (GTK_SOCKET (gtk_socket), 
		  gst_util_get_int_arg (GTK_OBJECT(show), "xid"));

  gtk_signal_connect(GTK_OBJECT(parse),"new_pad",mpeg2parse_newpad, pipeline);
  gtk_signal_connect(GTK_OBJECT(src),"eos",GTK_SIGNAL_FUNC(eof),NULL);
  gtk_signal_connect(GTK_OBJECT(show),"have_size",mpeg2parse_have_size, pipeline);

  g_print("setting to PLAYING state\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);

  gtk_idle_add(idle_func,pipeline);

  gdk_threads_enter();
  gtk_main();
  gdk_threads_leave();

  return 0;
}
