#include <gst/gst.h>

void
type_found (GstElement *typefind, GstCaps* caps) 
{
  xmlDocPtr doc;
  xmlNodePtr parent;
  
  doc = xmlNewDoc ("1.0");  
  doc->xmlRootNode = xmlNewDocNode (doc, NULL, "Capabilities", NULL);

  parent = xmlNewChild (doc->xmlRootNode, NULL, "Caps1", NULL);
  gst_caps_save_thyself (caps, parent);

  xmlDocDump (stdout, doc);
}

int 
main(int argc, char *argv[]) 
{
  GstElement *bin, *disksrc, *typefind;

  gst_init(&argc,&argv);

  if (argc != 2) {
    g_print("usage: %s <filename>\n", argv[0]);
    exit(-1);
  }

  /* create a new bin to hold the elements */
  bin = gst_bin_new("bin");
  g_assert(bin != NULL);

  /* create a disk reader */
  disksrc = gst_elementfactory_make("disksrc", "disk_source");
  g_assert(disksrc != NULL);
  g_object_set(G_OBJECT(disksrc),"location", argv[1],NULL);

  typefind = gst_elementfactory_make("typefind", "typefind");
  g_assert(typefind != NULL);

  /* add objects to the main pipeline */
  gst_bin_add(GST_BIN(bin), disksrc);
  gst_bin_add(GST_BIN(bin), typefind);

  g_signal_connect (G_OBJECT (typefind), "have_type", 
		    G_CALLBACK (type_found), NULL);

  gst_pad_connect(gst_element_get_pad(disksrc,"src"),
                  gst_element_get_pad(typefind,"sink"));

  /* start playing */
  gst_element_set_state(GST_ELEMENT(bin), GST_STATE_PLAYING);

  gst_bin_iterate(GST_BIN(bin));

  gst_element_set_state(GST_ELEMENT(bin), GST_STATE_NULL);

  exit(0);
}

