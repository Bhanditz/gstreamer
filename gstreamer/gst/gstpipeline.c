/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2004,2005 Wim Taymans <wim@fluendo.com>
 *
 * gstpipeline.c: Overall pipeline management element
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

/**
 * SECTION:gstpipeline
 * @short_description: Top-level bin with clocking and bus management
                       functionality.
 * @see_also: #GstElement, #GstBin, #GstClock, #GstBus
 *
 * A #GstPipeline is a special #GstBin used as the toplevel container for
 * the filter graph. The #GstPipeline will manage the selection and 
 * distribution of a global #GstClock as well as provide a #GstBus to the 
 * application. It will also implement a default behavour for managing
 * seek events (see gst_element_seek()).
 *
 * gst_pipeline_new() is used to create a pipeline. when you are done with
 * the pipeline, use gst_object_unref() to free its resources including all
 * added #GstElement objects (if not otherwise referenced).
 *
 * Elements are added and removed from the pipeline using the #GstBin 
 * methods like gst_bin_add() and gst_bin_remove() (see #GstBin).
 *
 * Before changing the state of the #GstPipeline (see #GstElement) a #GstBus
 * can be retrieved with gst_pipeline_get_bus(). This bus can then be
 * used to receive #GstMessage from the elements in the pipeline.
 *
 * By default, a #GstPipeline will automatically flush the pending #GstBus
 * messages when going to the NULL state to ensure that no circular
 * references exist when no messages are read from the #GstBus. This
 * behaviour can be changed with gst_pipeline_set_auto_flush_bus().
 *
 * When the #GstPipeline performs the PAUSED to PLAYING state change it will
 * select a clock for the elements. The clock selection algorithm will by
 * default select a clock provided by an element that is most upstream 
 * (closest to the source). For live pipelines (ones that return 
 * #GST_STATE_CHANGE_NO_PREROLL from the gst_element_set_state() call) this
 * will select the clock provided by the live source. For normal pipelines
 * this will select a clock provided by the sinks (most likely the audio
 * sink). If no element provides a clock, a default #GstSystemClock is used.
 *
 * The clock selection can be controlled with the gst_pipeline_use_clock()
 * method, which will enforce a given clock on the pipeline. With
 * gst_pipeline_auto_clock() the default clock selection algorithm can be 
 * restored.
 *
 * A #GstPipeline maintains a stream time for the elements. The stream
 * time is defined as the difference between the current clock time and
 * the base time. When the pipeline goes to READY or a flushing seek is
 * performed on it, the stream time is reset to 0. When the pipeline is
 * set from PLAYING to PAUSED, the current clock time is sampled and used to 
 * configure the base time for the elements when the pipeline is set
 * to PLAYING again. This default behaviour can be changed with the
 * gst_pipeline_set_new_stream_time() method. 
 * 
 * When sending a flushing seek event to a GstPipeline (see 
 * gst_element_seek()), it will make sure that the pipeline is properly 
 * PAUSED and resumed as well as set the new stream time to 0 when the
 * seek succeeded.
 *
 * Last reviewed on 2006-03-12 (0.10.5)
 */

#include "gst_private.h"
#include "gsterror.h"
#include "gst-i18n-lib.h"

#include "gstpipeline.h"
#include "gstinfo.h"
#include "gstsystemclock.h"

GST_DEBUG_CATEGORY_STATIC (pipeline_debug);
#define GST_CAT_DEFAULT pipeline_debug

static GstElementDetails gst_pipeline_details =
GST_ELEMENT_DETAILS ("Pipeline object",
    "Generic/Bin",
    "Complete pipeline object",
    "Erik Walthinsen <omega@cse.ogi.edu>, Wim Taymans <wim@fluendo.com>");

/* Pipeline signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_DELAY           0
#define DEFAULT_AUTO_FLUSH_BUS  TRUE

enum
{
  PROP_0,
  PROP_DELAY,
  PROP_AUTO_FLUSH_BUS
      /* FILL ME */
};

#define GST_PIPELINE_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_PIPELINE, GstPipelinePrivate))

struct _GstPipelinePrivate
{
  /* with LOCK */
  gboolean auto_flush_bus;
};


static void gst_pipeline_base_init (gpointer g_class);
static void gst_pipeline_class_init (gpointer g_class, gpointer class_data);
static void gst_pipeline_init (GTypeInstance * instance, gpointer g_class);

static void gst_pipeline_dispose (GObject * object);
static void gst_pipeline_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_pipeline_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_pipeline_send_event (GstElement * element,
    GstEvent * event);

static GstClock *gst_pipeline_provide_clock_func (GstElement * element);
static GstStateChangeReturn gst_pipeline_change_state (GstElement * element,
    GstStateChange transition);

static GstBinClass *parent_class = NULL;

/* static guint gst_pipeline_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_pipeline_get_type (void)
{
  static GType pipeline_type = 0;

  if (G_UNLIKELY (pipeline_type == 0)) {
    static const GTypeInfo pipeline_info = {
      sizeof (GstPipelineClass),
      gst_pipeline_base_init,
      NULL,
      (GClassInitFunc) gst_pipeline_class_init,
      NULL,
      NULL,
      sizeof (GstPipeline),
      0,
      gst_pipeline_init,
      NULL
    };

    pipeline_type =
        g_type_register_static (GST_TYPE_BIN, "GstPipeline", &pipeline_info, 0);

    GST_DEBUG_CATEGORY_INIT (pipeline_debug, "pipeline", GST_DEBUG_BOLD,
        "debugging info for the 'pipeline' container element");
  }
  return pipeline_type;
}

static void
gst_pipeline_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstelement_class, &gst_pipeline_details);
}

static void
gst_pipeline_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (g_class);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);
  GstPipelineClass *klass = GST_PIPELINE_CLASS (g_class);

  parent_class = g_type_class_peek_parent (klass);

  g_type_class_add_private (klass, sizeof (GstPipelinePrivate));

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_pipeline_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_pipeline_get_property);

  /**
   * GstPipeline:delay
   *
   * The expected delay needed for elements to spin up to the
   * PLAYING state expressed in nanoseconds.
   * see gst_pipeline_set_delay() for more information on this option.
   *
   * Since: 0.10.5
   **/
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DELAY,
      g_param_spec_uint64 ("delay", "Delay",
          "Expected delay needed for elements "
          "to spin up to PLAYING in nanoseconds", 0, G_MAXUINT64, DEFAULT_DELAY,
          G_PARAM_READWRITE));

  /**
   * GstPipeline:auto-flush-bus:
   *
   * Whether or not to automatically flush all messages on the
   * pipeline's bus when going from READY to NULL state. Please see
   * gst_pipeline_set_auto_flush_bus() for more information on this option.
   *
   * Since: 0.10.4
   **/
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_AUTO_FLUSH_BUS,
      g_param_spec_boolean ("auto-flush-bus", "Auto Flush Bus",
          "Whether to automatically flush the pipeline's bus when going "
          "from READY into NULL state", DEFAULT_AUTO_FLUSH_BUS,
          G_PARAM_READWRITE));

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_pipeline_dispose);

  gstelement_class->send_event = GST_DEBUG_FUNCPTR (gst_pipeline_send_event);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_pipeline_change_state);
  gstelement_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_pipeline_provide_clock_func);
}

static void
gst_pipeline_init (GTypeInstance * instance, gpointer g_class)
{
  GstPipeline *pipeline = GST_PIPELINE (instance);
  GstBus *bus;

  pipeline->priv = GST_PIPELINE_GET_PRIVATE (pipeline);

  /* set default property values */
  pipeline->priv->auto_flush_bus = DEFAULT_AUTO_FLUSH_BUS;
  pipeline->delay = DEFAULT_DELAY;

  /* create and set a default bus */
  bus = gst_bus_new ();
  gst_element_set_bus (GST_ELEMENT_CAST (pipeline), bus);
  GST_DEBUG_OBJECT (pipeline, "set bus %" GST_PTR_FORMAT " on pipeline", bus);
  gst_object_unref (bus);
}

static void
gst_pipeline_dispose (GObject * object)
{
  GstPipeline *pipeline = GST_PIPELINE (object);

  GST_CAT_DEBUG_OBJECT (GST_CAT_REFCOUNTING, pipeline, "dispose");

  /* clear and unref any fixed clock */
  gst_object_replace ((GstObject **) & pipeline->fixed_clock, NULL);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_pipeline_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPipeline *pipeline = GST_PIPELINE (object);

  switch (prop_id) {
    case PROP_DELAY:
      gst_pipeline_set_delay (pipeline, g_value_get_uint64 (value));
      break;
    case PROP_AUTO_FLUSH_BUS:
      gst_pipeline_set_auto_flush_bus (pipeline, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_pipeline_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstPipeline *pipeline = GST_PIPELINE (object);

  switch (prop_id) {
    case PROP_DELAY:
      g_value_set_uint64 (value, gst_pipeline_get_delay (pipeline));
      break;
    case PROP_AUTO_FLUSH_BUS:
      g_value_set_boolean (value, gst_pipeline_get_auto_flush_bus (pipeline));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* default pipeline seeking code:
 *
 * If the pipeline is PLAYING and a flushing seek is done, set
 * the pipeline to PAUSED before doing the seek.
 *
 * A flushing seek also resets the stream time to 0 so that when
 * we go back to PLAYING after the seek, the base_time is recalculated
 * and redistributed to the elements.
 *
 */
static gboolean
do_pipeline_seek (GstElement * element, GstEvent * event)
{
  gdouble rate;
  GstSeekFlags flags;
  gboolean flush;
  gboolean was_playing = FALSE;
  gboolean res;

  /* we are only interested in the FLUSH flag of the seek event. */
  gst_event_parse_seek (event, &rate, NULL, &flags, NULL, NULL, NULL, NULL);

  flush = flags & GST_SEEK_FLAG_FLUSH;

  /* if flushing seek, get the current state */
  if (flush) {
    GstState state;

    /* need to call _get_state() since a bin state is only updated
     * with this call. */
    gst_element_get_state (element, &state, NULL, 0);
    was_playing = state == GST_STATE_PLAYING;

    if (was_playing) {
      /* and PAUSE when the pipeline was PLAYING, we don't need
       * to wait for the state change to complete since we are going
       * to flush out any preroll sample anyway */
      gst_element_set_state (element, GST_STATE_PAUSED);
    }
  }

  /* let parent class implement the seek behaviour */
  res = GST_ELEMENT_CLASS (parent_class)->send_event (element, event);

  /* if flushing seek restore previous state */
  if (flush) {
    gboolean need_reset;

    GST_OBJECT_LOCK (element);
    need_reset = GST_PIPELINE (element)->stream_time != GST_CLOCK_TIME_NONE;
    GST_OBJECT_UNLOCK (element);

    /* need to reset the stream time to 0 after a successfull flushing seek, 
     * unless the user explicitly disabled this behavior by setting stream 
     * time to NONE */
    if (need_reset && res)
      gst_pipeline_set_new_stream_time (GST_PIPELINE (element), 0);

    if (was_playing)
      /* and continue playing, this might return ASYNC in which case the
       * application can wait for the PREROLL to complete after the seek. 
       */
      gst_element_set_state (element, GST_STATE_PLAYING);
  }
  return res;
}

static gboolean
gst_pipeline_send_event (GstElement * element, GstEvent * event)
{
  gboolean res;
  GstEventType event_type = GST_EVENT_TYPE (event);

  switch (event_type) {
    case GST_EVENT_SEEK:
      /* do the default seek handling */
      res = do_pipeline_seek (element, event);
      break;
    default:
      /* else parent implements the defaults */
      res = GST_ELEMENT_CLASS (parent_class)->send_event (element, event);
      break;
  }

  return res;
}

/**
 * gst_pipeline_new:
 * @name: name of new pipeline
 *
 * Create a new pipeline with the given name.
 *
 * Returns: newly created GstPipeline
 *
 * MT safe.
 */
GstElement *
gst_pipeline_new (const gchar * name)
{
  return gst_element_factory_make ("pipeline", name);
}

/* MT safe */
static GstStateChangeReturn
gst_pipeline_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn result = GST_STATE_CHANGE_SUCCESS;
  GstPipeline *pipeline = GST_PIPELINE (element);
  GstClock *clock;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      GST_OBJECT_LOCK (element);
      if (element->bus)
        gst_bus_set_flushing (element->bus, FALSE);
      GST_OBJECT_UNLOCK (element);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    {
      GstClockTime new_base_time;

      /* when going to playing, select a clock */
      clock = gst_element_provide_clock (element);

      if (clock) {
        GstClockTime start_time, stream_time, delay;
        gboolean new_clock;

        start_time = gst_clock_get_time (clock);

        GST_OBJECT_LOCK (element);
        new_clock = element->clock != clock;
        stream_time = pipeline->stream_time;
        delay = pipeline->delay;
        GST_OBJECT_UNLOCK (element);

        if (new_clock) {
          /* now distribute the clock (which could be NULL). If some
           * element refuses the clock, this will return FALSE and
           * we effectively fail the state change. */
          if (!gst_element_set_clock (element, clock))
            goto invalid_clock;

          /* if we selected and distributed a new clock, let the app 
           * know about it */
          gst_element_post_message (element,
              gst_message_new_new_clock (GST_OBJECT_CAST (element), clock));
        }

        if (stream_time != GST_CLOCK_TIME_NONE)
          new_base_time = start_time - stream_time + delay;
        else
          new_base_time = GST_CLOCK_TIME_NONE;

        gst_object_unref (clock);
      } else {
        GST_DEBUG ("no clock, using base time of 0");
        new_base_time = 0;
      }

      if (new_base_time != GST_CLOCK_TIME_NONE)
        gst_element_set_base_time (element, new_base_time);
      else
        GST_DEBUG_OBJECT (pipeline,
            "NOT adjusting base time because stream time is NONE");
    }
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
      gboolean need_reset;

      /* only reset the stream time when the application did not
       * specify a stream time explicitly */
      GST_OBJECT_LOCK (element);
      need_reset = pipeline->stream_time != GST_CLOCK_TIME_NONE;
      GST_OBJECT_UNLOCK (element);

      if (need_reset)
        gst_pipeline_set_new_stream_time (pipeline, 0);
    }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      GST_OBJECT_LOCK (element);
      if ((clock = element->clock)) {
        GstClockTime now;

        gst_object_ref (clock);
        GST_OBJECT_UNLOCK (element);

        /* calculate the time when we stopped */
        now = gst_clock_get_time (clock);
        gst_object_unref (clock);

        GST_OBJECT_LOCK (element);
        /* store the current stream time */
        if (pipeline->stream_time != GST_CLOCK_TIME_NONE)
          pipeline->stream_time = now - element->base_time;
        GST_DEBUG_OBJECT (element,
            "stream_time=%" GST_TIME_FORMAT ", now=%" GST_TIME_FORMAT
            ", base time %" GST_TIME_FORMAT,
            GST_TIME_ARGS (pipeline->stream_time), GST_TIME_ARGS (now),
            GST_TIME_ARGS (element->base_time));
      }
      GST_OBJECT_UNLOCK (element);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      GST_OBJECT_LOCK (element);
      if (element->bus) {
        if (pipeline->priv->auto_flush_bus) {
          gst_bus_set_flushing (element->bus, TRUE);
        } else {
          GST_INFO_OBJECT (element, "not flushing bus, auto-flushing disabled");
        }
      }
      GST_OBJECT_UNLOCK (element);
      break;
  }
  return result;

  /* ERRORS */
invalid_clock:
  {
    /* we generate this error when the selected clock was not
     * accepted by some element */
    GST_ELEMENT_ERROR (pipeline, CORE, CLOCK,
        (_("Selected clock cannot be used in pipeline.")),
        ("Pipeline cannot operate with selected clock"));
    GST_DEBUG_OBJECT (pipeline,
        "Pipeline cannot operate with selected clock %p", clock);
    return GST_STATE_CHANGE_FAILURE;
  }
}

/**
 * gst_pipeline_get_bus:
 * @pipeline: a #GstPipeline
 *
 * Gets the #GstBus of @pipeline.
 *
 * Returns: a #GstBus, unref after usage.
 *
 * MT safe.
 */
GstBus *
gst_pipeline_get_bus (GstPipeline * pipeline)
{
  return gst_element_get_bus (GST_ELEMENT (pipeline));
}

/**
 * gst_pipeline_set_new_stream_time:
 * @pipeline: a #GstPipeline
 * @time: the new stream time to set
 *
 * Set the new stream time of @pipeline to @time. The stream time is used to
 * set the base time on the elements (see gst_element_set_base_time())
 * in the PAUSED->PLAYING state transition.
 *
 * Setting @time to #GST_CLOCK_TIME_NONE will disable the pipeline's management
 * of element base time. The application will then be responsible for
 * performing base time distribution. This is sometimes useful if you want to
 * synchronize capture from multiple pipelines, and you can also ensure that the
 * pipelines have the same clock.
 *
 * MT safe.
 */
void
gst_pipeline_set_new_stream_time (GstPipeline * pipeline, GstClockTime time)
{
  g_return_if_fail (GST_IS_PIPELINE (pipeline));

  GST_OBJECT_LOCK (pipeline);
  pipeline->stream_time = time;
  GST_OBJECT_UNLOCK (pipeline);

  GST_DEBUG_OBJECT (pipeline, "set new stream_time to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (time));

  if (time == GST_CLOCK_TIME_NONE)
    GST_DEBUG_OBJECT (pipeline, "told not to adjust base time");
}

/**
 * gst_pipeline_get_last_stream_time:
 * @pipeline: a #GstPipeline
 *
 * Gets the last stream time of @pipeline. If the pipeline is PLAYING,
 * the returned time is the stream time used to configure the element's
 * base time in the PAUSED->PLAYING state. If the pipeline is PAUSED, the 
 * returned time is the stream time when the pipeline was paused.
 *
 * This function returns #GST_CLOCK_TIME_NONE if the pipeline was
 * configured to not handle the management of the element's base time 
 * (see gst_pipeline_set_new_stream_time()).
 *
 * Returns: a #GstClockTime.
 *
 * MT safe.
 */
GstClockTime
gst_pipeline_get_last_stream_time (GstPipeline * pipeline)
{
  GstClockTime result;

  g_return_val_if_fail (GST_IS_PIPELINE (pipeline), GST_CLOCK_TIME_NONE);

  GST_OBJECT_LOCK (pipeline);
  result = pipeline->stream_time;
  GST_OBJECT_UNLOCK (pipeline);

  return result;
}

static GstClock *
gst_pipeline_provide_clock_func (GstElement * element)
{
  GstClock *clock = NULL;
  GstPipeline *pipeline = GST_PIPELINE (element);

  /* if we have a fixed clock, use that one */
  GST_OBJECT_LOCK (pipeline);
  if (GST_OBJECT_FLAG_IS_SET (pipeline, GST_PIPELINE_FLAG_FIXED_CLOCK)) {
    clock = pipeline->fixed_clock;
    gst_object_ref (clock);
    GST_OBJECT_UNLOCK (pipeline);

    GST_CAT_DEBUG (GST_CAT_CLOCK, "pipeline using fixed clock %p (%s)",
        clock, clock ? GST_STR_NULL (GST_OBJECT_NAME (clock)) : "-");
  } else {
    GST_OBJECT_UNLOCK (pipeline);
    /* let the parent bin select a clock */
    clock =
        GST_ELEMENT_CLASS (parent_class)->
        provide_clock (GST_ELEMENT (pipeline));
    /* no clock, use a system clock */
    if (!clock) {
      clock = gst_system_clock_obtain ();

      GST_CAT_DEBUG (GST_CAT_CLOCK, "pipeline obtained system clock: %p (%s)",
          clock, clock ? GST_STR_NULL (GST_OBJECT_NAME (clock)) : "-");
    } else {
      GST_CAT_DEBUG (GST_CAT_CLOCK, "pipeline obtained clock: %p (%s)",
          clock, clock ? GST_STR_NULL (GST_OBJECT_NAME (clock)) : "-");
    }
  }
  return clock;
}

/**
 * gst_pipeline_get_clock:
 * @pipeline: a #GstPipeline
 *
 * Gets the current clock used by @pipeline.
 *
 * Returns: a #GstClock, unref after usage.
 */
GstClock *
gst_pipeline_get_clock (GstPipeline * pipeline)
{
  g_return_val_if_fail (GST_IS_PIPELINE (pipeline), NULL);

  return gst_pipeline_provide_clock_func (GST_ELEMENT (pipeline));
}


/**
 * gst_pipeline_use_clock:
 * @pipeline: a #GstPipeline
 * @clock: the clock to use
 *
 * Force @pipeline to use the given @clock. The pipeline will
 * always use the given clock even if new clock providers are added
 * to this pipeline.
 *
 * If @clock is NULL all clocking will be disabled which will make
 * the pipeline run as fast as possible.
 *
 * MT safe.
 */
void
gst_pipeline_use_clock (GstPipeline * pipeline, GstClock * clock)
{
  g_return_if_fail (GST_IS_PIPELINE (pipeline));

  GST_OBJECT_LOCK (pipeline);
  GST_OBJECT_FLAG_SET (pipeline, GST_PIPELINE_FLAG_FIXED_CLOCK);

  gst_object_replace ((GstObject **) & pipeline->fixed_clock,
      (GstObject *) clock);
  GST_OBJECT_UNLOCK (pipeline);

  GST_CAT_DEBUG (GST_CAT_CLOCK, "pipeline using fixed clock %p (%s)", clock,
      (clock ? GST_OBJECT_NAME (clock) : "nil"));
}

/**
 * gst_pipeline_set_clock:
 * @pipeline: a #GstPipeline
 * @clock: the clock to set
 *
 * Set the clock for @pipeline. The clock will be distributed
 * to all the elements managed by the pipeline.
 *
 * Returns: TRUE if the clock could be set on the pipeline. FALSE if
 *   some element did not accept the clock.
 *
 * MT safe.
 */
gboolean
gst_pipeline_set_clock (GstPipeline * pipeline, GstClock * clock)
{
  g_return_val_if_fail (pipeline != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PIPELINE (pipeline), FALSE);

  return GST_ELEMENT_CLASS (parent_class)->set_clock (GST_ELEMENT (pipeline),
      clock);
}

/**
 * gst_pipeline_auto_clock:
 * @pipeline: a #GstPipeline
 *
 * Let @pipeline select a clock automatically. This is the default
 * behaviour. 
 *
 * Use this function if you previous forced a fixed clock with 
 * gst_pipeline_use_clock() and want to restore the default
 * pipeline clock selection algorithm.
 *
 * MT safe.
 */
void
gst_pipeline_auto_clock (GstPipeline * pipeline)
{
  g_return_if_fail (pipeline != NULL);
  g_return_if_fail (GST_IS_PIPELINE (pipeline));

  GST_OBJECT_LOCK (pipeline);
  GST_OBJECT_FLAG_UNSET (pipeline, GST_PIPELINE_FLAG_FIXED_CLOCK);

  gst_object_replace ((GstObject **) & pipeline->fixed_clock, NULL);
  GST_OBJECT_UNLOCK (pipeline);

  GST_CAT_DEBUG (GST_CAT_CLOCK, "pipeline using automatic clock");
}

/**
 * gst_pipeline_set_delay:
 * @pipeline: a #GstPipeline
 * @delay: the delay
 *
 * Set the expected delay needed for all elements to perform the
 * PAUSED to PLAYING state change. @delay will be added to the
 * base time of the elements so that they wait an additional @delay
 * amount of time before starting to process buffers.
 *
 * This option is used for tuning purposes and should normally not be 
 * used.
 *
 * MT safe.
 *
 * Since: 0.10.5
 */
void
gst_pipeline_set_delay (GstPipeline * pipeline, GstClockTime delay)
{
  g_return_if_fail (GST_IS_PIPELINE (pipeline));

  GST_OBJECT_LOCK (pipeline);
  pipeline->delay = delay;
  GST_OBJECT_UNLOCK (pipeline);
}

/**
 * gst_pipeline_get_delay:
 * @pipeline: a #GstPipeline
 *
 * Get the configured delay (see gst_pipeline_set_delay()).
 *
 * Returns: The configured delay.
 *
 * MT safe.
 *
 * Since: 0.10.5
 */
GstClockTime
gst_pipeline_get_delay (GstPipeline * pipeline)
{
  GstClockTime res;

  g_return_val_if_fail (GST_IS_PIPELINE (pipeline), GST_CLOCK_TIME_NONE);

  GST_OBJECT_LOCK (pipeline);
  res = pipeline->delay;
  GST_OBJECT_UNLOCK (pipeline);

  return res;
}

/**
 * gst_pipeline_set_auto_flush_bus:
 * @pipeline: a #GstPipeline
 * @auto_flush: whether or not to automatically flush the bus when
 * the pipeline goes from READY to NULL state
 *
 * Usually, when a pipeline goes from READY to NULL state, it automatically
 * flushes all pending messages on the bus, which is done for refcounting
 * purposes, to break circular references. 
 *
 * This means that applications that update state using (async) bus messages 
 * (e.g. do certain things when a pipeline goes from PAUSED to READY) might 
 * not get to see messages when the pipeline is shut down, because they might 
 * be flushed before they can be dispatched in the main thread. This behaviour
 * can be disabled using this function.
 *
 * It is important that all messages on the bus are handled when the 
 * automatic flushing is disabled else memory leaks will be introduced.
 *
 * MT safe.
 *
 * Since: 0.10.4
 */
void
gst_pipeline_set_auto_flush_bus (GstPipeline * pipeline, gboolean auto_flush)
{
  g_return_if_fail (GST_IS_PIPELINE (pipeline));

  GST_OBJECT_LOCK (pipeline);
  pipeline->priv->auto_flush_bus = auto_flush;
  GST_OBJECT_UNLOCK (pipeline);
}

/**
 * gst_pipeline_get_auto_flush_bus:
 * @pipeline: a #GstPipeline
 *
 * Check if @pipeline will automatically flush messages when going to
 * the NULL state.
 *
 * Returns: whether the pipeline will automatically flush its bus when
 * going from READY to NULL state or not.
 *
 * MT safe.
 *
 * Since: 0.10.4
 */
gboolean
gst_pipeline_get_auto_flush_bus (GstPipeline * pipeline)
{
  gboolean res;

  g_return_val_if_fail (GST_IS_PIPELINE (pipeline), FALSE);

  GST_OBJECT_LOCK (pipeline);
  res = pipeline->priv->auto_flush_bus;
  GST_OBJECT_UNLOCK (pipeline);

  return res;
}
