#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <gst/gst.h>

/* FIXME: This is just a temporary hack.  We should have a better
 * check for siginfo handling. */
#ifdef SA_SIGINFO
#define USE_SIGINFO
#endif

extern volatile gboolean glib_on_error_halt;
static void fault_restore (void);
static void fault_spin (void);
static void sigint_restore (void);

static guint64 iterations = 0;
static guint64 sum = 0;
static guint64 min = G_MAXINT64;
static guint64 max = 0;
static GstClock *s_clock;
static GstElement *pipeline;
gboolean caught_intr = FALSE;

gboolean
idle_func (gpointer data)
{
  gboolean busy;
  GTimeVal tfthen, tfnow;
  GstClockTimeDiff diff;

  if (s_clock) {
    //g_print ("%" G_GINT64_FORMAT "\n", gst_clock_get_time (s_clock));
  }

  g_get_current_time (&tfthen);
  busy = gst_bin_iterate (GST_BIN (data));
  iterations++;
  g_get_current_time (&tfnow);

  diff = GST_TIMEVAL_TO_TIME (tfnow) -
         GST_TIMEVAL_TO_TIME (tfthen);

  sum += diff; 
  min = MIN (min, diff);
  max = MAX (max, diff);

  if (!busy || caught_intr) {
    gst_main_quit ();
    g_print ("execution ended after %" G_GUINT64_FORMAT " iterations (sum %" G_GUINT64_FORMAT " ns, average %" G_GUINT64_FORMAT " ns, min %" G_GUINT64_FORMAT " ns, max %" G_GUINT64_FORMAT " ns)\n", 
		    iterations, sum, sum/iterations, min, max);
  }

  return busy;
}

#ifndef GST_DISABLE_LOADSAVE
static GstElement*
xmllaunch_parse_cmdline (const gchar **argv) 
{
  GstElement *pipeline = NULL, *e;
  GstXML *xml;
  gboolean err;
  const gchar *arg;
  gchar *element, *property, *value;
  GList *l;
  gint i = 0;
  
  if (!(arg = argv[0])) {
    g_print ("usage: gst-xmllaunch <file.xml> [ element.property=value ... ]\n");
    exit (1);
  }
  
  xml = gst_xml_new ();
  err = gst_xml_parse_file(xml, arg, NULL);
  
  if (err != TRUE) {
    fprintf (stderr, "ERROR: parse of xml file '%s' failed\n", arg);
    exit (1);
  }
  
  l = gst_xml_get_topelements (xml);
  if (!l) {
    fprintf (stderr, "ERROR: no toplevel pipeline element in file '%s'\n", arg);
    exit (1);
  }
    
  if (l->next)
    g_warning ("only one toplevel element is supported at this time");
  
  pipeline = GST_ELEMENT (l->data);
  
  while ((arg = argv[++i])) {
    element = g_strdup (arg);
    property = strchr (element, '.');
    value = strchr (element, '=');
    
    if (!(element < property && property < value)) {
      fprintf (stderr, "ERROR: could not parse command line argument %d: %s", i, element);
      g_free (element);
      exit (1);
    }
    
    *property++ = '\0';
    *value++ = '\0';
    
    e = gst_bin_get_by_name (GST_BIN (pipeline), element);
    if (!e) {
      g_warning ("element named '%s' not found", element);
    } else {
      gst_util_set_object_arg (G_OBJECT (e), property, value);
    }
    g_free (element);
  }
  
  if (!l)
    return NULL;
  else
    return l->data;
}
#endif

#ifndef USE_SIGINFO
static void 
fault_handler_sighandler (int signum)
{
  fault_restore ();

  if (signum == SIGSEGV) {
    g_print ("Caught SIGSEGV\n");
  }
  else if (signum == SIGQUIT){
    g_print ("Caught SIGQUIT\n");
  }
  else {
    g_print ("signo:  %d\n", signum);
  }

  fault_spin();
}

#else

static void 
fault_handler_sigaction (int signum, siginfo_t *si, void *misc)
{
  fault_restore ();

  if (si->si_signo == SIGSEGV) {
    g_print ("Caught SIGSEGV accessing address %p\n", si->si_addr);
  }
  else if (si->si_signo == SIGQUIT){
    g_print ("Caught SIGQUIT\n");
  }
  else {
    g_print ("signo:  %d\n", si->si_signo);
    g_print ("errno:  %d\n", si->si_errno);
    g_print ("code:   %d\n", si->si_code);
  }

  fault_spin();
}
#endif

static void
fault_spin (void)
{
  int spinning = TRUE;

  glib_on_error_halt = FALSE;
  g_on_error_stack_trace ("gst-launch");

  wait (NULL);

  /* FIXME how do we know if we were run by libtool? */
  g_print ("Spinning.  Please run 'gdb gst-launch %d' to continue debugging, "
  	   "Ctrl-C to quit, or Ctrl-\\ to dump core.\n",
  	   (gint) getpid ());
  while (spinning) g_usleep (1000000);
}

static void 
fault_restore (void)
{
  struct sigaction action;

  memset (&action, 0, sizeof (action));
  action.sa_handler = SIG_DFL;

  sigaction(SIGSEGV, &action, NULL);
  sigaction(SIGQUIT, &action, NULL);
}

static void 
fault_setup (void)
{
  struct sigaction action;

  memset (&action, 0, sizeof (action));
#ifdef USE_SIGINFO
  action.sa_sigaction = fault_handler_sigaction;
  action.sa_flags = SA_SIGINFO;
#else
  action.sa_handler = fault_handler_sighandler;
#endif

  sigaction (SIGSEGV, &action, NULL);
  sigaction (SIGQUIT, &action, NULL);
}

/* we only use sighandler here because the registers are not important */
static void 
sigint_handler_sighandler (int signum)
{
  g_print ("Caught interrupt\n");
  
  sigint_restore();

  caught_intr = TRUE;
}

static void
sigint_setup (void)
{
  struct sigaction action;

  memset (&action, 0, sizeof (action));
  action.sa_handler = sigint_handler_sighandler;

  sigaction (SIGINT, &action, NULL);
}

static void 
sigint_restore (void)
{
  struct sigaction action;

  memset (&action, 0, sizeof (action));
  action.sa_handler = SIG_DFL;

  sigaction(SIGINT, &action, NULL);
}

int
main(int argc, char *argv[])
{
  gint i, j;
  /* options */
  gboolean verbose = FALSE;
  gboolean no_fault = FALSE;
  gboolean trace = FALSE;
  gchar *savefile = NULL;
  gchar *exclude_args = NULL;
  struct poptOption options[] = {
    {"verbose",	'v',  POPT_ARG_NONE|POPT_ARGFLAG_STRIP,   &verbose,   0,
     "output status information and property notifications", NULL},
    {"exclude", 'X',  POPT_ARG_STRING|POPT_ARGFLAG_STRIP, &exclude_args,  0,
     "do not output status information of TYPE", "TYPE1,TYPE2,..."},
#ifndef GST_DISABLE_LOADSAVE
    {"output",	'o',  POPT_ARG_STRING|POPT_ARGFLAG_STRIP, &savefile, 0,
     "save xml representation of pipeline to FILE and exit", "FILE"},
#endif
    {"no-fault", 'f', POPT_ARG_NONE|POPT_ARGFLAG_STRIP,   &no_fault,   0,
     "Do not install a fault handler", NULL},
    {"trace",   't',  POPT_ARG_NONE|POPT_ARGFLAG_STRIP,   &trace,   0,
     "print alloc trace if enabled at compile time", NULL},
    POPT_TABLEEND
  };

  gchar **argvn;
  GError *error = NULL;
  gint res = 0;

  free (malloc (8)); /* -lefence */

  gst_alloc_trace_set_flags_all (GST_ALLOC_TRACE_LIVE);
  
  gst_init_with_popt_table (&argc, &argv, options);

  /* FIXpopt: strip short args, too. We do it ourselves for now */
  j = 1;
  for (i = 1; i < argc; i++) {
    if (*(argv[i]) == '-') {
      if (strlen (argv[i]) == 2) {
        gchar *c = argv[i];
        c++;
        if (*c == 'X' || *c == 'o') {
	  i++;
	}
      }
    } else {
      argv[j] = argv[i];
      j++;
    }
  }
  argc = j;

  if (!no_fault)
    fault_setup();

  sigint_setup();
  
  if (trace) {
    if (!gst_alloc_trace_available()) {
      g_warning ("trace not available (recompile with trace enabled)");
    }
    gst_alloc_trace_print_all ();
  }

  /* make a null-terminated version of argv */
  argvn = g_new0 (char*, argc);
  memcpy (argvn, argv+1, sizeof (char*) * (argc-1));
#ifndef GST_DISABLE_LOADSAVE
  if (strstr (argv[0], "gst-xmllaunch")) {
    pipeline = xmllaunch_parse_cmdline ((const gchar**)argvn);
  } 
  else 
#endif
  {
    pipeline = (GstElement*) gst_parse_launchv ((const gchar**)argvn, &error);
  }
  g_free (argvn);

  if (!pipeline) {
    if (error) {
      fprintf(stderr, "ERROR: pipeline could not be constructed: %s\n", error->message);
      g_error_free (error);
    } else {
      fprintf(stderr, "ERROR: pipeline could not be constructed\n");
    }
    exit(1);
  } else if (error) {
    fprintf(stderr, "WARNING: erroneous pipeline: %s\n         Trying to run anyway\n", error->message);
    g_error_free (error);
  }
  
  if (verbose) {
    gchar **exclude_list = exclude_args ? g_strsplit (exclude_args, ",", 0) : NULL;
    g_signal_connect (pipeline, "deep_notify", G_CALLBACK (gst_element_default_deep_notify), exclude_list);
  }
  g_signal_connect (pipeline, "error", G_CALLBACK (gst_element_default_error), NULL);
  
#ifndef GST_DISABLE_LOADSAVE
  if (savefile) {
    gst_xml_write_file (GST_ELEMENT (pipeline), fopen (savefile, "w"));
  }
#endif
  
  if (!savefile) {
  
    if (!GST_IS_BIN (pipeline)) {
      GstElement *real_pipeline = gst_element_factory_make ("pipeline", NULL);
      if (real_pipeline == NULL) {
        fprintf(stderr, "ERROR: The pipeline element wasn't found.\n");
        exit(1);
      }
      gst_bin_add (GST_BIN (real_pipeline), pipeline);
      pipeline = real_pipeline;
    }

    fprintf(stderr,"RUNNING pipeline\n");
    if (gst_element_set_state (pipeline, GST_STATE_PLAYING) == GST_STATE_FAILURE) {
      fprintf(stderr,"pipeline doesn't want to play\n");
      res = -1;
      goto end;
    }

    s_clock = gst_bin_get_clock (GST_BIN (pipeline));

    if (!GST_FLAG_IS_SET (GST_OBJECT (pipeline), GST_BIN_SELF_SCHEDULABLE)) {
        g_idle_add (idle_func, pipeline);
        gst_main ();
    } else {
        g_print ("waiting for the state change...\n");
        gst_element_wait_state_change (pipeline);
        g_print ("got the state change...\n");
    }

    gst_element_set_state (pipeline, GST_STATE_NULL);
  }

end:

  gst_object_unref (GST_OBJECT (pipeline));

  if (trace)
    gst_alloc_trace_print_all ();

  return res;
}
