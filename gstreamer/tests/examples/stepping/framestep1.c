#include <stdlib.h>
#include <gst/gst.h>

static void
event_loop (GstElement * pipe)
{
  GstBus *bus;
  GstMessage *message = NULL;

  bus = gst_element_get_bus (GST_ELEMENT (pipe));

  while (TRUE) {
    message = gst_bus_timed_pop_filtered (bus, GST_MESSAGE_ANY, -1);

    g_assert (message != NULL);

    switch (message->type) {
      case GST_MESSAGE_EOS:
        g_message ("got EOS");
        gst_message_unref (message);
        return;
      case GST_MESSAGE_WARNING:
      case GST_MESSAGE_ERROR:
      {
        GError *gerror;
        gchar *debug;

        gst_message_parse_error (message, &gerror, &debug);
        gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
        gst_message_unref (message);
        g_error_free (gerror);
        g_free (debug);
        return;
      }
      case GST_MESSAGE_STEP_DONE:
      {
        GstFormat format;
        guint64 amount;
        gdouble rate;
        guint64 duration;
        gboolean intermediate;

        gst_message_parse_step_done (message, &format, &amount, &rate,
            &duration, &intermediate);

        g_message ("step done: %" GST_TIME_FORMAT " skipped in %"
            G_GUINT64_FORMAT " frames", GST_TIME_ARGS (duration), amount);

        return;
      }
      default:
        gst_message_unref (message);
        break;
    }
  }
}

/* signalled when a new preroll buffer is available */
static void
new_preroll (GstElement * appsink, gpointer user_data)
{
  GstBuffer *buffer;

  g_signal_emit_by_name (appsink, "pull-preroll", &buffer);

  g_message ("have new-preroll buffer %p, timestamp %" GST_TIME_FORMAT, buffer,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));

  gst_buffer_unref (buffer);
}

int
main (int argc, char *argv[])
{
  GstElement *bin, *videotestsrc, *appsink;

  gst_init (&argc, &argv);

  /* create a new bin to hold the elements */
  bin = gst_pipeline_new ("pipeline");
  g_assert (bin);

  /* create a fake source */
  videotestsrc = gst_element_factory_make ("videotestsrc", "videotestsrc");
  g_assert (videotestsrc);
  g_object_set (videotestsrc, "num-buffers", 10, NULL);

  /* and a fake sink */
  appsink = gst_element_factory_make ("appsink", "appsink");
  g_assert (appsink);
  g_object_set (appsink, "emit-signals", TRUE, NULL);
  g_object_set (appsink, "sync", TRUE, NULL);
  g_signal_connect (appsink, "new-preroll", (GCallback) new_preroll, NULL);

  /* add objects to the main pipeline */
  gst_bin_add (GST_BIN (bin), videotestsrc);
  gst_bin_add (GST_BIN (bin), appsink);

  /* link the elements */
  gst_element_link_many (videotestsrc, appsink, NULL);

  /* go to the PAUSED state and wait for preroll */
  g_message ("prerolling first frame");
  gst_element_set_state (bin, GST_STATE_PAUSED);
  gst_element_get_state (bin, NULL, NULL, -1);

  /* step two frames, flush so that new preroll is queued */
  g_message ("stepping two frames");
  g_assert (gst_element_send_event (bin,
          gst_event_new_step (GST_FORMAT_BUFFERS, 2, 1.0, TRUE, FALSE)));

  /* blocks and returns when we received the step done message */
  event_loop (bin);

  /* wait for step to really complete */
  gst_element_get_state (bin, NULL, NULL, -1);

  g_message ("stepped two frames");

  g_message ("playing until EOS");
  gst_element_set_state (bin, GST_STATE_PLAYING);
  /* Run event loop listening for bus messages until EOS or ERROR */
  event_loop (bin);
  g_message ("finished");

  /* stop the bin */
  gst_element_set_state (bin, GST_STATE_NULL);

  exit (0);
}
