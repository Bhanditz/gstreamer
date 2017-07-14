
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/gstqueue.h>
#include <gst/gsttrashstack.h>
#include <stdlib.h>

typedef struct
{
  char *name;
  int size;
  int abi_size;
}
Struct;

#ifdef HAVE_CPU_I386
#include "struct_i386.h"
#define HAVE_ABI_SIZES
#else
/* in case someone wants to generate a new arch */
#include "struct_i386.h"
#endif

int
main (int argc, char *argv[])
{
  int i;

  if (argc > 1) {
    g_print ("/* Generated by GStreamer-%s */\n", GST_VERSION);
    g_print ("Struct list[] = {\n");
    for (i = 0; list[i].name; i++) {
      g_print ("  { \"%s\", sizeof (%s), %d },\n",
          list[i].name, list[i].name, list[i].size);
    }
    g_print ("  { NULL, 0, 0}\n");
    g_print ("};\n");
  } else {
    g_print ("Run './struct_size regen' to regenerate structs.h\n");

#ifdef HAVE_ABI_SIZES
    {
      gboolean ok = TRUE;

      for (i = 0; list[i].name; i++) {
        if (list[i].size != list[i].abi_size) {
          ok = FALSE;
          g_print ("sizeof(%s) is %d, expected %d\n",
              list[i].name, list[i].size, list[i].abi_size);
        }
      }
      if (ok) {
        g_print ("All structures expected size\n");
      } else {
        g_print ("failed\n");
        exit (1);
      }
    }
#else
    g_print ("No structure size list was generated for this architecture\n");
    g_print ("ignoring\n");
#endif
  }

  exit (0);
}
