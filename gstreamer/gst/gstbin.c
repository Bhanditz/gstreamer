/* GStreamer
 * 
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstbin.c: GstBin container object and support code
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

#include "gst_private.h"

#include "gstevent.h"
#include "gstbin.h"
#include "gstmarshal.h"
#include "gstxml.h"
#include "gstinfo.h"
#include "gsterror.h"

#include "gstscheduler.h"
#include "gstindex.h"
#include "gstutils.h"

static GstElementDetails gst_bin_details = GST_ELEMENT_DETAILS ("Generic bin",
    "Generic/Bin",
    "Simple container object",
    "Erik Walthinsen <omega@cse.ogi.edu>");

GType _gst_bin_type = 0;

static gboolean _gst_boolean_did_something_accumulator (GSignalInvocationHint *
    ihint, GValue * return_accu, const GValue * handler_return, gpointer dummy);

static void gst_bin_dispose (GObject * object);

static GstElementStateReturn gst_bin_change_state (GstElement * element);
static GstElementStateReturn gst_bin_change_state_norecurse (GstBin * bin);

#ifndef GST_DISABLE_INDEX
static void gst_bin_set_index (GstElement * element, GstIndex * index);
#endif

static void gst_bin_add_func (GstBin * bin, GstElement * element);
static void gst_bin_remove_func (GstBin * bin, GstElement * element);
static void gst_bin_child_state_change_func (GstBin * bin,
    GstElementState oldstate, GstElementState newstate, GstElement * child);
GstElementStateReturn gst_bin_set_state (GstElement * element,
    GstElementState state);

static GstClock *gst_bin_get_clock_func (GstElement * element);
static void gst_bin_set_clock_func (GstElement * element, GstClock * clock);

static gboolean gst_bin_iterate_func (GstBin * bin);

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr gst_bin_save_thyself (GstObject * object, xmlNodePtr parent);
static void gst_bin_restore_thyself (GstObject * object, xmlNodePtr self);
#endif

/* Bin signals and args */
enum
{
  ELEMENT_ADDED,
  ELEMENT_REMOVED,
  ITERATE,
  LAST_SIGNAL
};

enum
{
  ARG_0
      /* FILL ME */
};

static void gst_bin_base_init (gpointer g_class);
static void gst_bin_class_init (GstBinClass * klass);
static void gst_bin_init (GstBin * bin);

static GstElementClass *parent_class = NULL;
static guint gst_bin_signals[LAST_SIGNAL] = { 0 };

/**
 * gst_bin_get_type:
 *
 * Returns: the type of #GstBin
 */
GType
gst_bin_get_type (void)
{
  if (!_gst_bin_type) {
    static const GTypeInfo bin_info = {
      sizeof (GstBinClass),
      gst_bin_base_init,
      NULL,
      (GClassInitFunc) gst_bin_class_init,
      NULL,
      NULL,
      sizeof (GstBin),
      0,
      (GInstanceInitFunc) gst_bin_init,
      NULL
    };

    _gst_bin_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstBin", &bin_info, 0);
  }
  return _gst_bin_type;
}

static void
gst_bin_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstelement_class, &gst_bin_details);
}

static void
gst_bin_class_init (GstBinClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gst_bin_signals[ELEMENT_ADDED] =
      g_signal_new ("element-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GstBinClass, element_added), NULL,
      NULL, gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GST_TYPE_ELEMENT);
  gst_bin_signals[ELEMENT_REMOVED] =
      g_signal_new ("element-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GstBinClass, element_removed), NULL,
      NULL, gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GST_TYPE_ELEMENT);
  gst_bin_signals[ITERATE] =
      g_signal_new ("iterate", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstBinClass, iterate),
      _gst_boolean_did_something_accumulator, NULL, gst_marshal_BOOLEAN__VOID,
      G_TYPE_BOOLEAN, 0);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_bin_dispose);

#ifndef GST_DISABLE_LOADSAVE
  gstobject_class->save_thyself = GST_DEBUG_FUNCPTR (gst_bin_save_thyself);
  gstobject_class->restore_thyself =
      GST_DEBUG_FUNCPTR (gst_bin_restore_thyself);
#endif

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_bin_change_state);
  gstelement_class->set_state = GST_DEBUG_FUNCPTR (gst_bin_set_state);
#ifndef GST_DISABLE_INDEX
  gstelement_class->set_index = GST_DEBUG_FUNCPTR (gst_bin_set_index);
#endif

  klass->add_element = GST_DEBUG_FUNCPTR (gst_bin_add_func);
  klass->remove_element = GST_DEBUG_FUNCPTR (gst_bin_remove_func);
  klass->child_state_change =
      GST_DEBUG_FUNCPTR (gst_bin_child_state_change_func);
  klass->iterate = GST_DEBUG_FUNCPTR (gst_bin_iterate_func);
}

static gboolean
_gst_boolean_did_something_accumulator (GSignalInvocationHint * ihint,
    GValue * return_accu, const GValue * handler_return, gpointer dummy)
{
  gboolean did_something;

  did_something = g_value_get_boolean (handler_return);
  if (did_something) {
    g_value_set_boolean (return_accu, TRUE);
  }

  /* always continue emission */
  return TRUE;
}

static void
gst_bin_init (GstBin * bin)
{
  /* in general, we prefer to use cothreads for most things */
  GST_FLAG_SET (bin, GST_BIN_FLAG_PREFER_COTHREADS);

  bin->numchildren = 0;
  bin->children = NULL;
}

/**
 * gst_bin_new:
 * @name: name of new bin
 *
 * Create a new bin with given name.
 *
 * Returns: new bin
 */
GstElement *
gst_bin_new (const gchar * name)
{
  return gst_element_factory_make ("bin", name);
}

static GstClock *
gst_bin_get_clock_func (GstElement * element)
{
  if (GST_ELEMENT_SCHED (element))
    return gst_scheduler_get_clock (GST_ELEMENT_SCHED (element));

  return NULL;
}

static void
gst_bin_set_clock_func (GstElement * element, GstClock * clock)
{
  if (GST_ELEMENT_SCHED (element))
    gst_scheduler_use_clock (GST_ELEMENT_SCHED (element), clock);
}

/**
 * gst_bin_get_clock:
 * @bin: a #GstBin to get the clock of
 *
 * Gets the current clock of the (scheduler of the) bin.
 *
 * Returns: the #GstClock of the bin
 */
GstClock *
gst_bin_get_clock (GstBin * bin)
{
  g_return_val_if_fail (bin != NULL, NULL);
  g_return_val_if_fail (GST_IS_BIN (bin), NULL);

  return gst_bin_get_clock_func (GST_ELEMENT (bin));
}

/**
 * gst_bin_use_clock:
 * @bin: the bin to set the clock for
 * @clock: the clock to use.
 *
 * Force the bin to use the given clock. Use NULL to 
 * force it to use no clock at all.
 */
void
gst_bin_use_clock (GstBin * bin, GstClock * clock)
{
  g_return_if_fail (GST_IS_BIN (bin));

  gst_bin_set_clock_func (GST_ELEMENT (bin), clock);
}

/**
 * gst_bin_auto_clock:
 * @bin: the bin to autoclock
 *
 * Let the bin select a clock automatically.
 */
void
gst_bin_auto_clock (GstBin * bin)
{
  g_return_if_fail (GST_IS_BIN (bin));

  if (GST_ELEMENT_SCHED (bin))
    gst_scheduler_auto_clock (GST_ELEMENT_SCHED (bin));
}

#ifndef GST_DISABLE_INDEX
static void
gst_bin_set_index (GstElement * element, GstIndex * index)
{
  GstBin *bin = GST_BIN (element);

  g_return_if_fail (GST_IS_BIN (bin));

  g_list_foreach (bin->children, (GFunc) gst_element_set_index, index);
}
#endif

static void
gst_bin_set_element_sched (GstElement * element, GstScheduler * sched)
{
  GST_CAT_LOG (GST_CAT_SCHEDULING, "setting element \"%s\" sched to %p",
      GST_ELEMENT_NAME (element), sched);

  /* if it's actually a Bin */
  if (GST_IS_BIN (element)) {
    if (GST_FLAG_IS_SET (element, GST_BIN_FLAG_MANAGER)) {
      GST_CAT_DEBUG_OBJECT (GST_CAT_PARENTAGE, element,
          "child is already a manager, not resetting sched");
      if (GST_ELEMENT_SCHED (element))
        gst_scheduler_add_scheduler (sched, GST_ELEMENT_SCHED (element));
      return;
    }

    GST_CAT_DEBUG_OBJECT (GST_CAT_PARENTAGE, element,
        "setting child bin's scheduler to be the same as the parent's");
    gst_scheduler_add_element (sched, element);

    /* set the children's schedule */
    g_list_foreach (GST_BIN (element)->children,
        (GFunc) gst_bin_set_element_sched, sched);
  }
  /* otherwise, if it's just a regular old element */
  else if (!GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
    GList *pads;

    gst_scheduler_add_element (sched, element);

    /* set the sched pointer in all the pads */
    pads = element->pads;
    while (pads) {
      GstPad *pad;

      pad = GST_PAD (pads->data);
      pads = g_list_next (pads);

      /* we only operate on real pads */
      if (!GST_IS_REAL_PAD (pad))
        continue;

      /* if the peer element exists and is a candidate */
      if (GST_PAD_PEER (pad)) {
        if (gst_pad_get_scheduler (GST_PAD_PEER (pad)) == sched) {
          GST_CAT_LOG (GST_CAT_SCHEDULING,
              "peer is in same scheduler, telling scheduler");

          if (GST_PAD_IS_SRC (pad))
            gst_scheduler_pad_link (sched, pad, GST_PAD_PEER (pad));
          else
            gst_scheduler_pad_link (sched, GST_PAD_PEER (pad), pad);
        }
      }
    }
  }
}


static void
gst_bin_unset_element_sched (GstElement * element, GstScheduler * sched)
{
  if (GST_ELEMENT_SCHED (element) == NULL) {
    GST_CAT_DEBUG (GST_CAT_SCHEDULING, "element \"%s\" has no scheduler",
        GST_ELEMENT_NAME (element));
    return;
  }

  GST_CAT_DEBUG (GST_CAT_SCHEDULING,
      "removing element \"%s\" from its sched %p", GST_ELEMENT_NAME (element),
      GST_ELEMENT_SCHED (element));

  /* if it's actually a Bin */
  if (GST_IS_BIN (element)) {

    if (GST_FLAG_IS_SET (element, GST_BIN_FLAG_MANAGER)) {
      GST_CAT_DEBUG_OBJECT (GST_CAT_PARENTAGE, element,
          "child is already a manager, not unsetting sched");
      if (sched) {
        gst_scheduler_remove_scheduler (sched, GST_ELEMENT_SCHED (element));
      }
      return;
    }
    /* for each child, remove them from their schedule */
    g_list_foreach (GST_BIN (element)->children,
        (GFunc) gst_bin_unset_element_sched, sched);

    gst_scheduler_remove_element (GST_ELEMENT_SCHED (element), element);
  } else if (!GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
    /* otherwise, if it's just a regular old element */
    GList *pads;

    /* set the sched pointer in all the pads */
    pads = element->pads;
    while (pads) {
      GstPad *pad;

      pad = GST_PAD (pads->data);
      pads = g_list_next (pads);

      /* we only operate on real pads */
      if (!GST_IS_REAL_PAD (pad))
        continue;

      /* if the peer element exists and is a candidate */
      if (GST_PAD_PEER (pad)) {
        if (gst_pad_get_scheduler (GST_PAD_PEER (pad)) == sched) {
          GST_CAT_LOG (GST_CAT_SCHEDULING,
              "peer is in same scheduler, telling scheduler");

          if (GST_PAD_IS_SRC (pad))
            gst_scheduler_pad_unlink (sched, pad, GST_PAD_PEER (pad));
          else
            gst_scheduler_pad_unlink (sched, GST_PAD_PEER (pad), pad);
        }
      }
    }
    gst_scheduler_remove_element (GST_ELEMENT_SCHED (element), element);
  }
}


/**
 * gst_bin_add_many:
 * @bin: the bin to add the elements to
 * @element_1: the first element to add to the bin
 * @...: additional elements to add to the bin
 * 
 * Adds a NULL-terminated list of elements to a bin.  This function is
 * equivalent to calling #gst_bin_add() for each member of the list.
 */
void
gst_bin_add_many (GstBin * bin, GstElement * element_1, ...)
{
  va_list args;

  g_return_if_fail (GST_IS_BIN (bin));
  g_return_if_fail (GST_IS_ELEMENT (element_1));

  va_start (args, element_1);

  while (element_1) {
    gst_bin_add (bin, element_1);

    element_1 = va_arg (args, GstElement *);
  }

  va_end (args);
}

static void
gst_bin_add_func (GstBin * bin, GstElement * element)
{
  gint state_idx = 0;
  GstElementState state;
  GstScheduler *sched;

  /* the element must not already have a parent */
  g_return_if_fail (GST_ELEMENT_PARENT (element) == NULL);

  /* then check to see if the element's name is already taken in the bin */
  if (gst_object_check_uniqueness (bin->children,
          GST_ELEMENT_NAME (element)) == FALSE) {
    g_warning ("Name %s is not unique in bin %s, not adding\n",
        GST_ELEMENT_NAME (element), GST_ELEMENT_NAME (bin));
    return;
  }

  if (GST_STATE (element) > GST_STATE (bin)) {
    GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, bin,
        "setting state to receive element \"%s\"", GST_OBJECT_NAME (element));
    gst_element_set_state ((GstElement *) bin, GST_STATE (element));
  }

  /* set the element's parent and add the element to the bin's list of children */
  gst_object_set_parent (GST_OBJECT (element), GST_OBJECT (bin));

  bin->children = g_list_append (bin->children, element);
  bin->numchildren++;

  /* bump our internal state counter */
  state = GST_STATE (element);
  while (state >>= 1)
    state_idx++;
  bin->child_states[state_idx]++;

  /* now we have to deal with manager stuff 
   * we can only do this if there's a scheduler: 
   * if we're not a manager, and aren't attached to anything, we have no sched (yet) */
  sched = GST_ELEMENT_SCHED (bin);
  if (sched) {
    gst_bin_set_element_sched (element, sched);
  }

  GST_CAT_DEBUG_OBJECT (GST_CAT_PARENTAGE, bin, "added element \"%s\"",
      GST_OBJECT_NAME (element));

  g_signal_emit (G_OBJECT (bin), gst_bin_signals[ELEMENT_ADDED], 0, element);
}

/**
 * gst_bin_add:
 * @bin: #GstBin to add element to
 * @element: #GstElement to add to bin
 *
 * Adds the given element to the bin.  Sets the element's parent, and thus
 * adds a reference.
 */
void
gst_bin_add (GstBin * bin, GstElement * element)
{
  GstBinClass *bclass;

  g_return_if_fail (GST_IS_BIN (bin));
  g_return_if_fail (GST_IS_ELEMENT (element));

  GST_CAT_INFO_OBJECT (GST_CAT_PARENTAGE, bin, "adding element \"%s\"",
      GST_OBJECT_NAME (element));

  bclass = GST_BIN_GET_CLASS (bin);

  if (bclass->add_element) {
    bclass->add_element (bin, element);
  } else {
    GST_ELEMENT_ERROR (bin, CORE, FAILED, (NULL),
        ("cannot add element %s to bin %s",
            GST_ELEMENT_NAME (element), GST_ELEMENT_NAME (bin)));
  }
}

static void
gst_bin_remove_func (GstBin * bin, GstElement * element)
{
  gint state_idx = 0;
  GstElementState state;

  /* the element must have its parent set to the current bin */
  g_return_if_fail (GST_ELEMENT_PARENT (element) == (GstObject *) bin);

  /* the element must be in the bin's list of children */
  if (g_list_find (bin->children, element) == NULL) {
    g_warning ("no element \"%s\" in bin \"%s\"\n", GST_ELEMENT_NAME (element),
        GST_ELEMENT_NAME (bin));
    return;
  }

  /* remove this element from the list of managed elements */
  gst_bin_unset_element_sched (element, GST_ELEMENT_SCHED (bin));

  /* now remove the element from the list of elements */
  bin->children = g_list_remove (bin->children, element);
  bin->numchildren--;

  /* bump our internal state counter */
  state = GST_STATE (element);
  while (state >>= 1)
    state_idx++;
  bin->child_states[state_idx]--;

  GST_CAT_INFO_OBJECT (GST_CAT_PARENTAGE, bin, "removed child \"%s\"",
      GST_OBJECT_NAME (element));

  /* ref as we're going to emit a signal */
  gst_object_ref (GST_OBJECT (element));
  gst_object_unparent (GST_OBJECT (element));

  /* if we're down to zero children, force state to NULL */
  while (bin->numchildren == 0 && GST_ELEMENT_SCHED (bin) != NULL &&
      GST_STATE (bin) > GST_STATE_NULL) {
    GstElementState next = GST_STATE (bin) >> 1;

    GST_STATE_PENDING (bin) = next;
    gst_bin_change_state_norecurse (bin);
    if (!GST_STATE (bin) == next) {
      g_warning ("bin %s failed state change to %d", GST_ELEMENT_NAME (bin),
          next);
      break;
    }
  }
  g_signal_emit (G_OBJECT (bin), gst_bin_signals[ELEMENT_REMOVED], 0, element);

  /* element is really out of our control now */
  gst_object_unref (GST_OBJECT (element));
}

/**
 * gst_bin_remove:
 * @bin: #GstBin to remove element from
 * @element: #GstElement to remove
 *
 * Remove the element from its associated bin, unparenting it as well.
 * Unparenting the element means that the element will be dereferenced,
 * so if the bin holds the only reference to the element, the element
 * will be freed in the process of removing it from the bin.  If you
 * want the element to still exist after removing, you need to call
 * #gst_object_ref before removing it from the bin.
 */
void
gst_bin_remove (GstBin * bin, GstElement * element)
{
  GstBinClass *bclass;

  GST_CAT_DEBUG (GST_CAT_PARENTAGE, "[%s]: trying to remove child %s",
      GST_ELEMENT_NAME (bin), GST_ELEMENT_NAME (element));

  g_return_if_fail (GST_IS_BIN (bin));
  g_return_if_fail (GST_IS_ELEMENT (element));

  bclass = GST_BIN_GET_CLASS (bin);

  if (bclass->remove_element) {
    bclass->remove_element (bin, element);
  } else {
    g_warning ("cannot remove elements from bin %s\n", GST_ELEMENT_NAME (bin));
  }
}

/**
 * gst_bin_remove_many:
 * @bin: the bin to remove the elements from
 * @element_1: the first element to remove from the bin
 * @...: NULL-terminated list of elements to remove from the bin
 * 
 * Remove a list of elements from a bin. This function is equivalent
 * to calling #gst_bin_remove with each member of the list.
 */
void
gst_bin_remove_many (GstBin * bin, GstElement * element_1, ...)
{
  va_list args;

  g_return_if_fail (GST_IS_BIN (bin));
  g_return_if_fail (GST_IS_ELEMENT (element_1));

  va_start (args, element_1);

  while (element_1) {
    gst_bin_remove (bin, element_1);

    element_1 = va_arg (args, GstElement *);
  }

  va_end (args);
}

/**
 * gst_bin_child_state_change:
 * @bin: #GstBin with the child
 * @oldstate: The old child state
 * @newstate: The new child state
 * @child: #GstElement that signaled an changed state
 *
 * An internal function to inform the parent bin about a state change
 * of a child.
 */
void
gst_bin_child_state_change (GstBin * bin, GstElementState oldstate,
    GstElementState newstate, GstElement * child)
{
  GstBinClass *bclass;

  g_return_if_fail (GST_IS_BIN (bin));
  g_return_if_fail (GST_IS_ELEMENT (child));

  GST_CAT_LOG (GST_CAT_STATES, "child %s changed state in bin %s from %s to %s",
      GST_ELEMENT_NAME (child), GST_ELEMENT_NAME (bin),
      gst_element_state_get_name (oldstate),
      gst_element_state_get_name (newstate));

  bclass = GST_BIN_GET_CLASS (bin);

  if (bclass->child_state_change) {
    bclass->child_state_change (bin, oldstate, newstate, child);
  } else {
    g_warning ("cannot signal state change of child %s to bin %s\n",
        GST_ELEMENT_NAME (child), GST_ELEMENT_NAME (bin));
  }
}

static void
gst_bin_child_state_change_func (GstBin * bin, GstElementState oldstate,
    GstElementState newstate, GstElement * child)
{
  GstElementState old = 0, new = 0;
  gint old_idx = 0, new_idx = 0, i;

  old = oldstate;
  new = newstate;
  while (old >>= 1)
    old_idx++;
  while (new >>= 1)
    new_idx++;

  GST_LOCK (bin);
  bin->child_states[old_idx]--;
  bin->child_states[new_idx]++;

  for (i = GST_NUM_STATES - 1; i >= 0; i--) {
    if (bin->child_states[i] != 0) {
      gint state = (1 << i);

      if (GST_STATE (bin) != state) {
        GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, bin,
            "highest child state is %s, changing bin state accordingly",
            gst_element_state_get_name (state));
        GST_STATE_PENDING (bin) = state;
        GST_UNLOCK (bin);
        gst_bin_change_state_norecurse (bin);
        if (state != GST_STATE (bin)) {
          g_warning ("%s: state change in callback %d %d",
              GST_ELEMENT_NAME (bin), state, GST_STATE (bin));
        }
        return;
      }
      break;
    }
  }
  GST_UNLOCK (bin);
}

typedef gboolean (*GstBinForeachFunc) (GstBin * bin, GstElement * element,
    gpointer data);

/*
 * gst_bin_foreach:
 * @bin: bin to traverse
 * @func: function to call on each child
 * @data: user data handed to each function call
 *
 * Calls @func on each child of the bin. If @func returns FALSE, 
 * gst_bin_foreach() immediately returns.
 * It is assumed that calling @func may alter the set of @bin's children. @func
 * will only be called on children that were in @bin when gst_bin_foreach() was
 * called, and that are still in @bin when the child is reached.
 *
 * Returns: TRUE if @func always returned TRUE, FALSE otherwise
 **/
static gboolean
gst_bin_foreach (GstBin * bin, GstBinForeachFunc func, gpointer data)
{
  GList *kids, *walk;

  g_return_val_if_fail (GST_IS_BIN (bin), FALSE);
  g_return_val_if_fail (func != NULL, FALSE);

  kids = g_list_copy (bin->children);

  for (walk = kids; walk; walk = g_list_next (walk)) {
    GstElement *element = (GstElement *) walk->data;

    if (g_list_find (bin->children, element)) {
      gboolean res = func (bin, element, data);

      if (!res) {
        g_list_free (kids);
        return FALSE;
      }
    }
  }

  g_list_free (kids);
  return TRUE;
}

typedef struct
{
  GstElementState pending;
  GstElementStateReturn result;
}
SetKidStateData;
static int
set_kid_state_func (GstBin * bin, GstElement * child, gpointer user_data)
{
  GstElementState old_child_state;
  SetKidStateData *data = user_data;

  if (GST_FLAG_IS_SET (child, GST_ELEMENT_LOCKED_STATE)) {
    return TRUE;
  }

  old_child_state = GST_STATE (child);

  GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, bin,
      "changing state of child %s from current %s to pending %s",
      GST_ELEMENT_NAME (child), gst_element_state_get_name (old_child_state),
      gst_element_state_get_name (data->pending));

  switch (gst_element_set_state (child, data->pending)) {
    case GST_STATE_FAILURE:
      GST_CAT_INFO_OBJECT (GST_CAT_STATES, bin,
          "child '%s' failed to go to state %d(%s)",
          GST_ELEMENT_NAME (child),
          data->pending, gst_element_state_get_name (data->pending));

      gst_element_set_state (child, old_child_state);
      return FALSE;             /* error out to the caller */

    case GST_STATE_ASYNC:
      GST_CAT_INFO_OBJECT (GST_CAT_STATES, bin,
          "child '%s' is changing state asynchronously",
          GST_ELEMENT_NAME (child));
      data->result = GST_STATE_ASYNC;
      return TRUE;

    case GST_STATE_SUCCESS:
      GST_CAT_DEBUG (GST_CAT_STATES,
          "child '%s' changed state to %d(%s) successfully",
          GST_ELEMENT_NAME (child), data->pending,
          gst_element_state_get_name (data->pending));
      return TRUE;

    default:
      g_assert_not_reached ();
      return FALSE;             /* satisfy gcc */
  }
}

static GstElementStateReturn
gst_bin_change_state (GstElement * element)
{
  GstBin *bin;
  GstElementStateReturn ret;
  GstElementState old_state, pending;
  SetKidStateData data;

  g_return_val_if_fail (GST_IS_BIN (element), GST_STATE_FAILURE);

  bin = GST_BIN (element);

  old_state = GST_STATE (element);
  pending = GST_STATE_PENDING (element);

  GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
      "changing state of children from %s to %s",
      gst_element_state_get_name (old_state),
      gst_element_state_get_name (pending));

  if (pending == GST_STATE_VOID_PENDING)
    return GST_STATE_SUCCESS;

  data.pending = pending;
  data.result = GST_STATE_SUCCESS;
  if (!gst_bin_foreach (bin, set_kid_state_func, &data)) {
    GST_STATE_PENDING (element) = old_state;
    return GST_STATE_FAILURE;
  }

  GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
      "done changing bin's state from %s to %s, now in %s",
      gst_element_state_get_name (old_state),
      gst_element_state_get_name (pending),
      gst_element_state_get_name (GST_STATE (element)));

  if (data.result == GST_STATE_ASYNC)
    ret = GST_STATE_ASYNC;
  else {
    /* FIXME: this should have been done by the children already, no? */
    if (parent_class->change_state) {
      ret = parent_class->change_state (element);
    } else
      ret = GST_STATE_SUCCESS;
  }
  return ret;
}

GstElementStateReturn
gst_bin_set_state (GstElement * element, GstElementState state)
{
  GstBin *bin = GST_BIN (element);

  if (GST_STATE (bin) == state) {
    SetKidStateData data;

    data.pending = state;
    data.result = GST_STATE_SUCCESS;
    if (!gst_bin_foreach (bin, set_kid_state_func, &data)) {
      return GST_STATE_FAILURE;
    } else {
      return data.result;
    }
  } else {
    return GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS, set_state, (element,
            state), GST_STATE_FAILURE);
  }
}

static GstElementStateReturn
gst_bin_change_state_norecurse (GstBin * bin)
{
  GstElementStateReturn ret;

  if (parent_class->change_state) {
    GST_CAT_LOG_OBJECT (GST_CAT_STATES, bin, "setting bin's own state");
    ret = parent_class->change_state (GST_ELEMENT (bin));

    return ret;
  } else
    return GST_STATE_FAILURE;
}

static void
gst_bin_dispose (GObject * object)
{
  GstBin *bin = GST_BIN (object);

  GST_CAT_DEBUG_OBJECT (GST_CAT_REFCOUNTING, object, "dispose");

  gst_element_set_state (GST_ELEMENT (object), GST_STATE_NULL);

  while (bin->children) {
    gst_bin_remove (bin, GST_ELEMENT (bin->children->data));
  }
  g_assert (bin->children == NULL);
  g_assert (bin->numchildren == 0);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

/**
 * gst_bin_get_by_name:
 * @bin: #Gstbin to search
 * @name: the element name to search for
 *
 * Get the element with the given name from this bin.
 *
 * Returns: the element with the given name
 */
GstElement *
gst_bin_get_by_name (GstBin * bin, const gchar * name)
{
  GList *children;
  GstElement *child;

  g_return_val_if_fail (bin != NULL, NULL);
  g_return_val_if_fail (GST_IS_BIN (bin), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  GST_CAT_INFO (GST_CAT_PARENTAGE, "[%s]: looking up child element %s",
      GST_ELEMENT_NAME (bin), name);

  children = bin->children;
  while (children) {
    child = GST_ELEMENT (children->data);
    if (!strcmp (GST_OBJECT_NAME (child), name))
      return child;
    if (GST_IS_BIN (child)) {
      GstElement *res = gst_bin_get_by_name (GST_BIN (child), name);

      if (res)
        return res;
    }
    children = g_list_next (children);
  }

  return NULL;
}

/**
 * gst_bin_get_by_name_recurse_up:
 * @bin: #Gstbin to search
 * @name: the element name to search for
 *
 * Get the element with the given name from this bin. If the
 * element is not found, a recursion is performed on the parent bin.
 *
 * Returns: the element with the given name
 */
GstElement *
gst_bin_get_by_name_recurse_up (GstBin * bin, const gchar * name)
{
  GstElement *result = NULL;
  GstObject *parent;

  g_return_val_if_fail (bin != NULL, NULL);
  g_return_val_if_fail (GST_IS_BIN (bin), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  result = gst_bin_get_by_name (bin, name);

  if (!result) {
    parent = gst_object_get_parent (GST_OBJECT (bin));

    if (parent && GST_IS_BIN (parent)) {
      result = gst_bin_get_by_name_recurse_up (GST_BIN (parent), name);
    }
  }

  return result;
}

/**
 * gst_bin_get_list:
 * @bin: #Gstbin to get the list from
 *
 * Get the list of elements in this bin.
 *
 * Returns: a GList of elements
 */
const GList *
gst_bin_get_list (GstBin * bin)
{
  g_return_val_if_fail (GST_IS_BIN (bin), NULL);

  return bin->children;
}

/**
 * gst_bin_get_by_interface:
 * @bin: bin to find element in
 * @interface: interface to be implemented by interface
 *
 * Looks for the first element inside the bin that implements the given
 * interface. If such an element is found, it returns the element. You can
 * cast this element to the given interface afterwards.
 * If you want all elements that implement the interface, use
 * gst_bin_get_all_by_interface(). The function recurses bins inside bins.
 *
 * Returns: An element inside the bin implementing the interface.
 */
GstElement *
gst_bin_get_by_interface (GstBin * bin, GType interface)
{
  GList *walk;

  g_return_val_if_fail (GST_IS_BIN (bin), NULL);
  g_return_val_if_fail (G_TYPE_IS_INTERFACE (interface), NULL);

  walk = bin->children;
  while (walk) {
    if (G_TYPE_CHECK_INSTANCE_TYPE (walk->data, interface))
      return GST_ELEMENT (walk->data);
    if (GST_IS_BIN (walk->data)) {
      GstElement *ret;

      ret = gst_bin_get_by_interface (GST_BIN (walk->data), interface);
      if (ret)
        return ret;
    }
    walk = g_list_next (walk);
  }

  return NULL;
}

/**
 * gst_bin_get_all_by_interface:
 * @bin: bin to find elements in
 * @interface: interface to be implemented by interface
 *
 * Looks for all elements inside the bin that implements the given
 * interface. You can safely cast all returned elements to the given interface.
 * The function recurses bins inside bins. You need to free the list using
 * g_list_free() after use.
 *
 * Returns: An element inside the bin implementing the interface.
 */
GList *
gst_bin_get_all_by_interface (GstBin * bin, GType interface)
{
  GList *walk, *ret = NULL;

  g_return_val_if_fail (GST_IS_BIN (bin), NULL);
  g_return_val_if_fail (G_TYPE_IS_INTERFACE (interface), NULL);

  walk = bin->children;
  while (walk) {
    if (G_TYPE_CHECK_INSTANCE_TYPE (walk->data, interface)) {
      GST_DEBUG_OBJECT (bin, "element %s implements requested interface",
          GST_ELEMENT_NAME (GST_ELEMENT (walk->data)));
      ret = g_list_prepend (ret, walk->data);
    }
    if (GST_IS_BIN (walk->data)) {
      ret = g_list_concat (ret,
          gst_bin_get_all_by_interface (GST_BIN (walk->data), interface));
    }
    walk = g_list_next (walk);
  }

  return ret;
}

/**
 * gst_bin_sync_children_state:
 * @bin: #Gstbin to sync state
 *
 * Tries to set the state of the children of this bin to the same state of the
 * bin by calling gst_element_set_state for each child not already having a
 * synchronized state. 
 *
 * Returns: The worst return value of any gst_element_set_state. So if one child
 *          returns #GST_STATE_FAILURE while all others return #GST_STATE_SUCCESS
 *          this function returns #GST_STATE_FAILURE.
 */
GstElementStateReturn
gst_bin_sync_children_state (GstBin * bin)
{
  GList *children;
  GstElement *element;
  GstElementState state;
  GstElementStateReturn ret = GST_STATE_SUCCESS;

  g_return_val_if_fail (GST_IS_BIN (bin), GST_STATE_FAILURE);

  state = GST_STATE (bin);
  children = bin->children;
  GST_CAT_INFO (GST_CAT_STATES,
      "syncing state of children with bin \"%s\"'s state %s",
      GST_ELEMENT_NAME (bin), gst_element_state_get_name (state));

  while (children) {
    element = GST_ELEMENT (children->data);
    children = children->next;
    if (GST_STATE (element) != state) {
      switch (gst_element_set_state (element, state)) {
        case GST_STATE_SUCCESS:
          break;
        case GST_STATE_ASYNC:
          if (ret == GST_STATE_SUCCESS)
            ret = GST_STATE_ASYNC;
          break;
        case GST_STATE_FAILURE:
          ret = GST_STATE_FAILURE;
        default:
          /* make sure gst_element_set_state never returns this */
          g_assert_not_reached ();
      }
    }
  }

  return ret;
}

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr
gst_bin_save_thyself (GstObject * object, xmlNodePtr parent)
{
  GstBin *bin = GST_BIN (object);
  xmlNodePtr childlist, elementnode;
  GList *children;
  GstElement *child;

  if (GST_OBJECT_CLASS (parent_class)->save_thyself)
    GST_OBJECT_CLASS (parent_class)->save_thyself (GST_OBJECT (bin), parent);

  childlist = xmlNewChild (parent, NULL, "children", NULL);

  GST_CAT_INFO (GST_CAT_XML, "[%s]: saving %d children",
      GST_ELEMENT_NAME (bin), bin->numchildren);

  children = bin->children;
  while (children) {
    child = GST_ELEMENT (children->data);
    elementnode = xmlNewChild (childlist, NULL, "element", NULL);
    gst_object_save_thyself (GST_OBJECT (child), elementnode);
    children = g_list_next (children);
  }
  return childlist;
}

static void
gst_bin_restore_thyself (GstObject * object, xmlNodePtr self)
{
  GstBin *bin = GST_BIN (object);
  xmlNodePtr field = self->xmlChildrenNode;
  xmlNodePtr childlist;

  while (field) {
    if (!strcmp (field->name, "children")) {
      GST_CAT_INFO (GST_CAT_XML, "[%s]: loading children",
          GST_ELEMENT_NAME (object));
      childlist = field->xmlChildrenNode;
      while (childlist) {
        if (!strcmp (childlist->name, "element")) {
          GstElement *element =
              gst_xml_make_element (childlist, GST_OBJECT (bin));

          /* it had to be parented to find the pads, now we ref and unparent so
           * we can add it to the bin */
          gst_object_ref (GST_OBJECT (element));
          gst_object_unparent (GST_OBJECT (element));

          gst_bin_add (bin, element);
        }
        childlist = childlist->next;
      }
    }

    field = field->next;
  }
  if (GST_OBJECT_CLASS (parent_class)->restore_thyself)
    (GST_OBJECT_CLASS (parent_class)->restore_thyself) (object, self);
}
#endif /* GST_DISABLE_LOADSAVE */

static gboolean
gst_bin_iterate_func (GstBin * bin)
{
  GstScheduler *sched = GST_ELEMENT_SCHED (bin);

  /* only iterate if this is the manager bin */
  if (sched && sched->parent == GST_ELEMENT (bin)) {
    GstSchedulerState state;

    state = gst_scheduler_iterate (sched);

    if (state == GST_SCHEDULER_STATE_RUNNING) {
      return TRUE;
    } else if (state == GST_SCHEDULER_STATE_ERROR) {
      gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PAUSED);
    } else if (state == GST_SCHEDULER_STATE_STOPPED) {
      /* check if we have children scheds that are still running */
      /* FIXME: remove in 0.9? autouseless because iterations gone? */
      GList *walk;

      for (walk = sched->schedulers; walk; walk = g_list_next (walk)) {
        GstScheduler *test = walk->data;

        g_return_val_if_fail (test->parent, FALSE);
        if (GST_STATE (test->parent) == GST_STATE_PLAYING) {
          GST_CAT_DEBUG_OBJECT (GST_CAT_SCHEDULING, bin,
              "current bin is not iterating, but children are, "
              "so returning TRUE anyway...");
          g_usleep (1);
          return TRUE;
        }
      }
    }
  } else {
    g_warning ("bin \"%s\" is not the managing bin, can't be iterated on!\n",
        GST_ELEMENT_NAME (bin));
  }

  return FALSE;
}

/**
 * gst_bin_iterate:
 * @bin: a#GstBin to iterate.
 *
 * Iterates over the elements in this bin.
 *
 * Returns: TRUE if the bin did something useful. This value
 *          can be used to determine it the bin is in EOS.
 */
gboolean
gst_bin_iterate (GstBin * bin)
{
  gboolean running;

  g_return_val_if_fail (bin != NULL, FALSE);
  g_return_val_if_fail (GST_IS_BIN (bin), FALSE);

  GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, bin, "starting iteration");
  gst_object_ref (GST_OBJECT (bin));

  running = FALSE;
  g_signal_emit (G_OBJECT (bin), gst_bin_signals[ITERATE], 0, &running);

  gst_object_unref (GST_OBJECT (bin));
  GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, bin, "finished iteration");

  return running;
}
