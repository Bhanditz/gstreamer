/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000 Wim Taymans <wtay@chello.be>
 *               2004 Thomas Vander Stichele <thomas@apestaart.org>
 *
 * gst-launch.c: tool to launch GStreamer pipelines from the command line
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
#  include "config.h"
#endif

/* FIXME: hack alert */
#ifdef _MSC_VER
#define DISABLE_FAULT_HANDLER
#endif

#include <string.h>
#include <stdlib.h>
#include <signal.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifndef DISABLE_FAULT_HANDLER
#include <sys/wait.h>
#endif
#include <locale.h>             /* for LC_ALL */
#include "gst/gst-i18n-app.h"

#include <gst/gst.h>

/* FIXME: This is just a temporary hack.  We should have a better
 * check for siginfo handling. */
#ifdef SA_SIGINFO
#define USE_SIGINFO
#endif

extern volatile gboolean glib_on_error_halt;

#ifndef DISABLE_FAULT_HANDLER
static void fault_restore (void);
static void fault_spin (void);
static void sigint_restore (void);
#endif

static gint max_iterations = 0;
static GstElement *pipeline;
static gboolean caught_intr = FALSE;
static gboolean caught_error = FALSE;
static gboolean tags = FALSE;
static gboolean messages = FALSE;


#ifndef GST_DISABLE_LOADSAVE
static GstElement *
xmllaunch_parse_cmdline (const gchar ** argv)
{
  GstElement *pipeline = NULL, *e;
  GstXML *xml;
  gboolean err;
  const gchar *arg;
  gchar *element, *property, *value;
  GList *l;
  gint i = 0;

  if (!(arg = argv[0])) {
    g_print (_
        ("Usage: gst-xmllaunch <file.xml> [ element.property=value ... ]\n"));
    exit (1);
  }

  xml = gst_xml_new ();
  /* FIXME guchar from gstxml.c */
  err = gst_xml_parse_file (xml, (guchar *) arg, NULL);

  if (err != TRUE) {
    fprintf (stderr, _("ERROR: parse of xml file '%s' failed.\n"), arg);
    exit (1);
  }

  l = gst_xml_get_topelements (xml);
  if (!l) {
    fprintf (stderr, _("ERROR: no toplevel pipeline element in file '%s'.\n"),
        arg);
    exit (1);
  }

  if (l->next)
    fprintf (stderr,
        _("WARNING: only one toplevel element is supported at this time."));

  pipeline = GST_ELEMENT (l->data);

  while ((arg = argv[++i])) {
    element = g_strdup (arg);
    property = strchr (element, '.');
    value = strchr (element, '=');

    if (!(element < property && property < value)) {
      fprintf (stderr,
          _("ERROR: could not parse command line argument %d: %s.\n"), i,
          element);
      g_free (element);
      exit (1);
    }

    *property++ = '\0';
    *value++ = '\0';

    e = gst_bin_get_by_name (GST_BIN (pipeline), element);
    if (!e) {
      fprintf (stderr, _("WARNING: element named '%s' not found.\n"), element);
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

#ifndef DISABLE_FAULT_HANDLER
#ifndef USE_SIGINFO
static void
fault_handler_sighandler (int signum)
{
  fault_restore ();

  /* printf is used instead of g_print(), since it's less likely to
   * deadlock */
  switch (signum) {
    case SIGSEGV:
      printf ("Caught SIGSEGV\n");
      break;
    case SIGQUIT:
      printf ("Caught SIGQUIT\n");
      break;
    default:
      printf ("signo:  %d\n", signum);
      break;
  }

  fault_spin ();
}

#else /* USE_SIGINFO */

static void
fault_handler_sigaction (int signum, siginfo_t * si, void *misc)
{
  fault_restore ();

  /* printf is used instead of g_print(), since it's less likely to
   * deadlock */
  switch (si->si_signo) {
    case SIGSEGV:
      printf ("Caught SIGSEGV accessing address %p\n", si->si_addr);
      break;
    case SIGQUIT:
      printf ("Caught SIGQUIT\n");
      break;
    default:
      printf ("signo:  %d\n", si->si_signo);
      printf ("errno:  %d\n", si->si_errno);
      printf ("code:   %d\n", si->si_code);
      break;
  }

  fault_spin ();
}
#endif /* USE_SIGINFO */

static void
fault_spin (void)
{
  int spinning = TRUE;

  glib_on_error_halt = FALSE;
  g_on_error_stack_trace ("gst-launch");

  wait (NULL);

  /* FIXME how do we know if we were run by libtool? */
  printf ("Spinning.  Please run 'gdb gst-launch %d' to continue debugging, "
      "Ctrl-C to quit, or Ctrl-\\ to dump core.\n", (gint) getpid ());
  while (spinning)
    g_usleep (1000000);
}

static void
fault_restore (void)
{
  struct sigaction action;

  memset (&action, 0, sizeof (action));
  action.sa_handler = SIG_DFL;

  sigaction (SIGSEGV, &action, NULL);
  sigaction (SIGQUIT, &action, NULL);
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
#endif /* DISABLE_FAULT_HANDLER */

static void
print_tag (const GstTagList * list, const gchar * tag, gpointer unused)
{
  gint i, count;

  count = gst_tag_list_get_tag_size (list, tag);

  for (i = 0; i < count; i++) {
    gchar *str;

    if (gst_tag_get_type (tag) == G_TYPE_STRING) {
      if (!gst_tag_list_get_string_index (list, tag, i, &str))
        g_assert_not_reached ();
    } else {
      str =
          g_strdup_value_contents (gst_tag_list_get_value_index (list, tag, i));
    }

    if (i == 0) {
      g_print ("%15s: %s\n", gst_tag_get_nick (tag), str);
    } else {
      g_print ("               : %s\n", str);
    }

    g_free (str);
  }
}

#ifndef DISABLE_FAULT_HANDLER
/* we only use sighandler here because the registers are not important */
static void
sigint_handler_sighandler (int signum)
{
  g_print ("Caught interrupt -- ");

  sigint_restore ();

  caught_intr = TRUE;
}

static gboolean
check_intr (GstElement * pipeline)
{
  if (!caught_intr) {
    return TRUE;
  } else {
    GstBus *bus;
    GstMessage *message;

    caught_intr = FALSE;
    g_print ("Pausing pipeline.\n");

    bus = gst_element_get_bus (GST_ELEMENT (pipeline));
    message = gst_message_new_warning (GST_OBJECT (pipeline),
        NULL, "pipeline interrupted");
    gst_bus_post (bus, message);

    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    gst_element_get_state (pipeline, NULL, NULL, NULL);
    g_print ("Pipeline paused.\n");

    gst_object_unref (bus);

    return FALSE;
  }
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

  sigaction (SIGINT, &action, NULL);
}

static void
play_handler (int signum)
{
  switch (signum) {
    case SIGUSR1:
      g_print ("Caught SIGUSR1 - Play request.\n");
      gst_element_set_state (pipeline, GST_STATE_PLAYING);
      break;
    case SIGUSR2:
      g_print ("Caught SIGUSR2 - Stop request.\n");
      gst_element_set_state (pipeline, GST_STATE_NULL);
      break;
  }
}

static void
play_signal_setup (void)
{
  struct sigaction action;

  memset (&action, 0, sizeof (action));
  action.sa_handler = play_handler;
  sigaction (SIGUSR1, &action, NULL);
  sigaction (SIGUSR2, &action, NULL);
}
#endif /* DISABLE_FAULT_HANDLER */

static gboolean
event_loop (GstElement * pipeline, gboolean blocking)
{
  GstBus *bus;
  GstMessageType revent;
  GstMessage *message = NULL;

  bus = gst_element_get_bus (GST_ELEMENT (pipeline));

  g_timeout_add (50, (GSourceFunc) check_intr, pipeline);

  while (TRUE) {
    revent = gst_bus_poll (bus, GST_MESSAGE_ANY, blocking ? -1 : 0);

    /* if the poll timed out, only when !blocking */
    if (revent == GST_MESSAGE_UNKNOWN) {
      gst_object_unref (bus);
      return FALSE;
    }

    message = gst_bus_pop (bus);
    g_return_val_if_fail (message != NULL, TRUE);

    if (messages) {
      gchar *sstr;
      const GstStructure *s;

      s = gst_message_get_structure (message);
      sstr = gst_structure_to_string (s);
      g_print (_("Got Message from element \"%s\": %s\n"),
          GST_STR_NULL (GST_ELEMENT_NAME (GST_MESSAGE_SRC (message))), sstr);
      g_free (sstr);
    }

    switch (revent) {
      case GST_MESSAGE_EOS:
        g_print (_
            ("Got EOS from element \"%s\".\n"),
            GST_STR_NULL (GST_ELEMENT_NAME (GST_MESSAGE_SRC (message))));
        gst_message_unref (message);
        gst_object_unref (bus);
        return FALSE;
      case GST_MESSAGE_TAG:
        if (tags) {
          GstTagList *tags;

          gst_message_parse_tag (message, &tags);
          g_print (_("FOUND TAG      : found by element \"%s\".\n"),
              GST_STR_NULL (GST_ELEMENT_NAME (GST_MESSAGE_SRC (message))));
          gst_tag_list_foreach (tags, print_tag, NULL);
          gst_tag_list_free (tags);
        }
        gst_message_unref (message);
        break;
      case GST_MESSAGE_WARNING:{
        GError *gerror;
        gchar *debug;

        gst_message_parse_warning (message, &gerror, &debug);
        if (debug) {
          g_print ("WARNING: Element \"%s\" warns: %s\n",
              GST_STR_NULL (GST_ELEMENT_NAME (GST_MESSAGE_SRC (message))),
              debug);
        }
        gst_message_unref (message);
        if (gerror)
          g_error_free (gerror);
        g_free (debug);
        break;
      }
      case GST_MESSAGE_ERROR:{
        GError *gerror;
        gchar *debug;

        gst_message_parse_error (message, &gerror, &debug);
        gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
        gst_message_unref (message);
        if (gerror)
          g_error_free (gerror);
        g_free (debug);
        gst_object_unref (bus);
        return TRUE;
      }
      case GST_MESSAGE_STATE_CHANGED:{
        GstElementState old, new;

        gst_message_parse_state_changed (message, &old, &new);
        if (!(old == GST_STATE_PLAYING && new == GST_STATE_PAUSED &&
                GST_MESSAGE_SRC (message) == GST_OBJECT (pipeline))) {
          gst_message_unref (message);
          break;
        }
        g_print (_
            ("Element \"%s\" has gone from PLAYING to PAUSED, quitting.\n"),
            GST_STR_NULL (GST_ELEMENT_NAME (GST_MESSAGE_SRC (message))));
        /* cut out of the event loop if check_intr set us to PAUSED */
        gst_message_unref (message);
        gst_object_unref (bus);
        return FALSE;
      }
      default:
        /* just be quiet by default */
        gst_message_unref (message);
        break;
    }
  }

  g_assert_not_reached ();
  return TRUE;
}

int
main (int argc, char *argv[])
{
  gint i, j;

  /* options */
  gboolean verbose = FALSE;
  gboolean no_fault = FALSE;
  gboolean trace = FALSE;
  gchar *savefile = NULL;
  gchar *exclude_args = NULL;
  struct poptOption options[] = {
    {"tags", 't', POPT_ARG_NONE | POPT_ARGFLAG_STRIP, &tags, 0,
        N_("Output tags (also known as metadata)"), NULL},
    {"messages", 'm', POPT_ARG_NONE | POPT_ARGFLAG_STRIP, &messages, 0,
        N_("Output messages"), NULL},
    {"verbose", 'v', POPT_ARG_NONE | POPT_ARGFLAG_STRIP, &verbose, 0,
        N_("Output status information and property notifications"), NULL},
    {"exclude", 'X', POPT_ARG_STRING | POPT_ARGFLAG_STRIP, &exclude_args, 0,
        N_("Do not output status information of TYPE"), N_("TYPE1,TYPE2,...")},
#ifndef GST_DISABLE_LOADSAVE
    {"output", 'o', POPT_ARG_STRING | POPT_ARGFLAG_STRIP, &savefile, 0,
        N_("Save xml representation of pipeline to FILE and exit"), N_("FILE")},
#endif
    {"no-fault", 'f', POPT_ARG_NONE | POPT_ARGFLAG_STRIP, &no_fault, 0,
        N_("Do not install a fault handler"), NULL},
    {"trace", 'T', POPT_ARG_NONE | POPT_ARGFLAG_STRIP, &trace, 0,
        N_("Print alloc trace (if enabled at compile time)"), NULL},
    {"iterations", 'i', POPT_ARG_INT | POPT_ARGFLAG_STRIP, &max_iterations, 0,
        N_("Number of times to iterate pipeline"), NULL},
    POPT_TABLEEND
  };

  gchar **argvn;
  GError *error = NULL;
  gint res = 0;

  free (malloc (8));            /* -lefence */

#ifdef GETTEXT_PACKAGE
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

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

#ifndef DISABLE_FAULT_HANDLER
  if (!no_fault)
    fault_setup ();

  sigint_setup ();
  play_signal_setup ();
#endif

  if (trace) {
    if (!gst_alloc_trace_available ()) {
      g_warning ("Trace not available (recompile with trace enabled).");
    }
    gst_alloc_trace_print_live ();
  }

  /* make a null-terminated version of argv */
  argvn = g_new0 (char *, argc);
  memcpy (argvn, argv + 1, sizeof (char *) * (argc - 1));
#ifndef GST_DISABLE_LOADSAVE
  if (strstr (argv[0], "gst-xmllaunch")) {
    pipeline = xmllaunch_parse_cmdline ((const gchar **) argvn);
  } else
#endif
  {
    pipeline =
        (GstElement *) gst_parse_launchv ((const gchar **) argvn, &error);
  }
  g_free (argvn);

  if (!pipeline) {
    if (error) {
      fprintf (stderr, _("ERROR: pipeline could not be constructed: %s.\n"),
          error->message);
      g_error_free (error);
    } else {
      fprintf (stderr, _("ERROR: pipeline could not be constructed.\n"));
    }
    return 1;
  } else if (error) {
    fprintf (stderr, _("WARNING: erroneous pipeline: %s\n"), error->message);
    fprintf (stderr, _("         Trying to run anyway.\n"));
    g_error_free (error);
  }

  if (verbose) {
    gchar **exclude_list =
        exclude_args ? g_strsplit (exclude_args, ",", 0) : NULL;
    g_signal_connect (pipeline, "deep_notify",
        G_CALLBACK (gst_object_default_deep_notify), exclude_list);
  }
#ifndef GST_DISABLE_LOADSAVE
  if (savefile) {
    gst_xml_write_file (GST_ELEMENT (pipeline), fopen (savefile, "w"));
  }
#endif

  if (!savefile) {
    GstElementState state, pending;
    GstElementStateReturn ret;

    if (!GST_IS_BIN (pipeline)) {
      GstElement *real_pipeline = gst_element_factory_make ("pipeline", NULL);

      if (real_pipeline == NULL) {
        fprintf (stderr, _("ERROR: the 'pipeline' element wasn't found.\n"));
        return 1;
      }
      gst_bin_add (GST_BIN (real_pipeline), pipeline);
      pipeline = real_pipeline;
    }

    fprintf (stderr, _("PAUSE pipeline ...\n"));
    ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);

    switch (ret) {
      case GST_STATE_FAILURE:
        fprintf (stderr, _("ERROR: pipeline doesn't want to pause.\n"));
        res = -1;
        goto end;
      case GST_STATE_NO_PREROLL:
        fprintf (stderr, _("NO_PREROLL pipeline ...\n"));
        break;
      case GST_STATE_ASYNC:
        fprintf (stderr, _("PREROLL pipeline ...\n"));
        gst_element_get_state (pipeline, &state, &pending, NULL);
        /* fallthrough */
      case GST_STATE_SUCCESS:
        fprintf (stderr, _("PREROLLED pipeline ...\n"));
        break;
    }

    caught_error = event_loop (pipeline, FALSE);

    if (caught_error) {
      fprintf (stderr, _("ERROR: pipeline doesn't want to preroll.\n"));
    } else {
      GTimeVal tfthen, tfnow;
      GstClockTimeDiff diff;

      fprintf (stderr, _("RUNNING pipeline ...\n"));
      if (gst_element_set_state (pipeline,
              GST_STATE_PLAYING) == GST_STATE_FAILURE) {
        fprintf (stderr, _("ERROR: pipeline doesn't want to play.\n"));
        res = -1;
        goto end;
      }

      g_get_current_time (&tfthen);
      caught_error = event_loop (pipeline, TRUE);
      g_get_current_time (&tfnow);

      diff = GST_TIMEVAL_TO_TIME (tfnow) - GST_TIMEVAL_TO_TIME (tfthen);

      g_print (_("Execution ended after %" G_GUINT64_FORMAT " ns.\n"), diff);
    }
    while (g_main_context_iteration (NULL, FALSE));

    fprintf (stderr, _("PAUSE pipeline ...\n"));
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    gst_element_get_state (pipeline, &state, &pending, NULL);
    fprintf (stderr, _("READY pipeline ...\n"));
    gst_element_set_state (pipeline, GST_STATE_READY);
    gst_element_get_state (pipeline, &state, &pending, NULL);

  end:
    fprintf (stderr, _("NULL pipeline ...\n"));
    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_element_get_state (pipeline, &state, &pending, NULL);
  }

  fprintf (stderr, _("FREEING pipeline ...\n"));
  gst_object_unref (pipeline);

  gst_deinit ();
  if (trace)
    gst_alloc_trace_print_live ();

  return res;
}
