/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 Andy Wingo <wingo@pobox.com>
 *		      2006 Edward Hervey <bilboed@bilboed.com>
 *
 * gstghostpad.c: Proxy pads
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
 * SECTION:gstghostpad
 * @short_description: Pseudo link pads
 * @see_also: #GstPad
 *
 * GhostPads are useful when organizing pipelines with #GstBin like elements.
 * The idea here is to create hierarchical element graphs. The bin element
 * contains a sub-graph. Now one would like to treat the bin-element like other
 * #GstElements. This is where GhostPads come into play. A GhostPad acts as a
 * proxy for another pad. Thus the bin can have sink and source ghost-pads that
 * are associated with sink and source pads of the child elements.
 *
 * If the target pad is known at creation time, gst_ghost_pad_new() is the
 * function to use to get a ghost-pad. Otherwise one can use gst_ghost_pad_new_no_target()
 * to create the ghost-pad and use gst_ghost_pad_set_target() to establish the
 * association later on.
 *
 * Note that GhostPads add overhead to the data processing of a pipeline.
 *
 * Last reviewed on 2005-11-18 (0.9.5)
 */

#include "gst_private.h"

#include "gstghostpad.h"

#define GST_TYPE_PROXY_PAD              (gst_proxy_pad_get_type ())
#define GST_IS_PROXY_PAD(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PROXY_PAD))
#define GST_IS_PROXY_PAD_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_PROXY_PAD))
#define GST_PROXY_PAD(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PROXY_PAD, GstProxyPad))
#define GST_PROXY_PAD_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_PROXY_PAD, GstProxyPadClass))
#define GST_PROXY_PAD_CAST(obj)         ((GstProxyPad *)obj)

#define GST_PROXY_PAD_TARGET(pad)       (GST_PROXY_PAD (pad)->target)
#define GST_PROXY_PAD_INTERNAL(pad)     (GST_PROXY_PAD (pad)->internal)

typedef struct _GstProxyPad GstProxyPad;
typedef struct _GstProxyPadClass GstProxyPadClass;

#define GST_PROXY_GET_LOCK(pad) (GST_PROXY_PAD (pad)->proxy_lock)
#define GST_PROXY_LOCK(pad)     (g_mutex_lock (GST_PROXY_GET_LOCK (pad)))
#define GST_PROXY_UNLOCK(pad)   (g_mutex_unlock (GST_PROXY_GET_LOCK (pad)))

struct _GstProxyPad
{
  GstPad pad;

  /* with PROXY_LOCK */
  GMutex *proxy_lock;
  GstPad *target;
  GstPad *internal;
};

struct _GstProxyPadClass
{
  GstPadClass parent_class;

  /*< private > */
  gpointer _gst_reserved[1];
};


G_DEFINE_TYPE (GstProxyPad, gst_proxy_pad, GST_TYPE_PAD);

static GstPad *gst_proxy_pad_get_target (GstPad * pad);
static GstPad *gst_proxy_pad_get_internal (GstPad * pad);

static void gst_proxy_pad_dispose (GObject * object);
static void gst_proxy_pad_finalize (GObject * object);

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr gst_proxy_pad_save_thyself (GstObject * object,
    xmlNodePtr parent);
#endif


static void
gst_proxy_pad_class_init (GstProxyPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_proxy_pad_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_proxy_pad_finalize);

#ifndef GST_DISABLE_LOADSAVE
  {
    GstObjectClass *gstobject_class = (GstObjectClass *) klass;

    gstobject_class->save_thyself =
        GST_DEBUG_FUNCPTR (gst_proxy_pad_save_thyself);
  }
#endif
}

const GstQueryType *
gst_proxy_pad_do_query_type (GstPad * pad)
{
  GstPad *target = gst_proxy_pad_get_target (pad);
  const GstQueryType *res = NULL;

  if (target) {
    res = gst_pad_get_query_types (target);
    gst_object_unref (target);
  }
  return res;
}

static gboolean
gst_proxy_pad_do_event (GstPad * pad, GstEvent * event)
{
  gboolean res = FALSE;
  GstPad *internal = gst_proxy_pad_get_internal (pad);

  res = gst_pad_push_event (internal, event);
  gst_object_unref (internal);

  return res;
}

static gboolean
gst_proxy_pad_do_query (GstPad * pad, GstQuery * query)
{
  gboolean res = FALSE;
  GstPad *target = gst_proxy_pad_get_target (pad);

  if (target) {
    res = gst_pad_query (target, query);
    gst_object_unref (target);
  }

  return res;
}

static GList *
gst_proxy_pad_do_internal_link (GstPad * pad)
{
  GList *res = NULL;
  GstPad *target = gst_proxy_pad_get_target (pad);

  if (target) {
    res = gst_pad_get_internal_links (target);
    gst_object_unref (target);
  }

  return res;
}

static GstFlowReturn
gst_proxy_pad_do_bufferalloc (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstFlowReturn result;
  GstPad *internal = gst_proxy_pad_get_internal (pad);

  result = gst_pad_alloc_buffer (internal, offset, size, caps, buf);
  gst_object_unref (internal);

  return result;
}

static GstFlowReturn
gst_proxy_pad_do_chain (GstPad * pad, GstBuffer * buffer)
{
  GstPad *internal = gst_proxy_pad_get_internal (pad);
  GstFlowReturn res;

  res = gst_pad_push (internal, buffer);
  gst_object_unref (internal);

  return res;
}

static GstFlowReturn
gst_proxy_pad_do_getrange (GstPad * pad, guint64 offset, guint size,
    GstBuffer ** buffer)
{
  GstPad *internal = gst_proxy_pad_get_internal (pad);
  GstFlowReturn res;

  res = gst_pad_pull_range (internal, offset, size, buffer);
  gst_object_unref (internal);

  return res;
}

static gboolean
gst_proxy_pad_do_checkgetrange (GstPad * pad)
{
  gboolean result;
  GstPad *internal = gst_proxy_pad_get_internal (pad);

  result = gst_pad_check_pull_range (internal);
  gst_object_unref (internal);

  return result;
}

static GstCaps *
gst_proxy_pad_do_getcaps (GstPad * pad)
{
  GstPad *target = gst_proxy_pad_get_target (pad);
  GstCaps *res;

  if (target) {
    /* if we have a real target, proxy the call */
    GST_DEBUG_OBJECT (pad, "get caps of target");
    res = gst_pad_get_caps (target);
    gst_object_unref (target);
  } else {
    GstPadTemplate *templ = GST_PAD_PAD_TEMPLATE (pad);

    /* else, if we have a template, use that */
    if (templ) {
      res = GST_PAD_TEMPLATE_CAPS (templ);
      GST_DEBUG_OBJECT (pad,
          "using pad template %p with caps %p %" GST_PTR_FORMAT, templ, res,
          res);
      res = gst_caps_ref (res);
      goto done;
    }

    /* last resort, any caps */
    GST_DEBUG_OBJECT (pad, "pad has no template, returning ANY");
    res = gst_caps_new_any ();
  }

done:
  return res;
}

static gboolean
gst_proxy_pad_do_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstPad *target = gst_proxy_pad_get_target (pad);
  gboolean res = FALSE;

  if (target) {
    res = gst_pad_accept_caps (target, caps);
    gst_object_unref (target);
  }

  return res;
}

static void
gst_proxy_pad_do_fixatecaps (GstPad * pad, GstCaps * caps)
{
  GstPad *target = gst_proxy_pad_get_target (pad);

  if (target) {
    gst_pad_fixate_caps (target, caps);
    gst_object_unref (target);
  }
}

static gboolean
gst_proxy_pad_do_setcaps (GstPad * pad, GstCaps * caps)
{
  GstPad *target = gst_proxy_pad_get_target (pad);
  gboolean res;

  if (target) {
    res = gst_pad_set_caps (target, caps);
    gst_object_unref (target);
  } else {
    /* We don't have any target, but we shouldn't return FALSE since this
     * would stop the actual push of a buffer (which might trigger a pad block
     * or probe, or properly return GST_FLOW_NOT_LINKED.
     */
    res = TRUE;
  }
  return res;
}

static gboolean
gst_proxy_pad_set_target_unlocked (GstPad * pad, GstPad * target)
{
  GstPad *oldtarget;

  if (target) {
    GST_LOG_OBJECT (pad, "setting target %s:%s", GST_DEBUG_PAD_NAME (target));

    if (G_UNLIKELY (GST_PAD_DIRECTION (pad) != GST_PAD_DIRECTION (target)))
      goto wrong_direction;
  } else
    GST_LOG_OBJECT (pad, "clearing target");

  /* clear old target */
  if ((oldtarget = GST_PROXY_PAD_TARGET (pad))) {
    GST_PROXY_PAD_TARGET (pad) = NULL;
    gst_object_unref (oldtarget);
  }

  /* set and ref new target if any */
  if (target)
    GST_PROXY_PAD_TARGET (pad) = gst_object_ref (target);

  return TRUE;

  /* ERRORS */
wrong_direction:
  {
    GST_ERROR_OBJECT (pad,
        "target pad doesn't have the same direction as ourself");
    return FALSE;
  }
}

static gboolean
gst_proxy_pad_set_target (GstPad * pad, GstPad * target)
{
  gboolean result;

  GST_PROXY_LOCK (pad);
  result = gst_proxy_pad_set_target_unlocked (pad, target);
  GST_PROXY_UNLOCK (pad);

  return result;
}

static GstPad *
gst_proxy_pad_get_target (GstPad * pad)
{
  GstPad *target;

  GST_PROXY_LOCK (pad);
  target = GST_PROXY_PAD_TARGET (pad);
  if (target)
    gst_object_ref (target);
  GST_PROXY_UNLOCK (pad);

  return target;
}

static GstPad *
gst_proxy_pad_get_internal (GstPad * pad)
{
  GstPad *internal;

  GST_PROXY_LOCK (pad);
  internal = GST_PROXY_PAD_INTERNAL (pad);
  if (internal)
    gst_object_ref (internal);
  GST_PROXY_UNLOCK (pad);

  return internal;
}

static void
gst_proxy_pad_dispose (GObject * object)
{
  GstPad *pad = GST_PAD (object);
  GstPad **target_p;

  GST_PROXY_LOCK (pad);
  /* remove and unref the target */
  target_p = &GST_PROXY_PAD_TARGET (pad);
  gst_object_replace ((GstObject **) target_p, NULL);
  /* The internal is only cleared by GstGhostPad::dispose, since it is the 
   * parent of non-ghost GstProxyPad and owns the refcount on the internal.
   */
  GST_PROXY_UNLOCK (pad);

  G_OBJECT_CLASS (gst_proxy_pad_parent_class)->dispose (object);
}

static void
gst_proxy_pad_finalize (GObject * object)
{
  GstProxyPad *pad = GST_PROXY_PAD (object);

  g_mutex_free (pad->proxy_lock);
  pad->proxy_lock = NULL;

  G_OBJECT_CLASS (gst_proxy_pad_parent_class)->finalize (object);
}

static void
gst_proxy_pad_init (GstProxyPad * ppad)
{
  GstPad *pad = (GstPad *) ppad;

  ppad->proxy_lock = g_mutex_new ();

  gst_pad_set_query_type_function (pad,
      GST_DEBUG_FUNCPTR (gst_proxy_pad_do_query_type));
  gst_pad_set_event_function (pad, GST_DEBUG_FUNCPTR (gst_proxy_pad_do_event));
  gst_pad_set_query_function (pad, GST_DEBUG_FUNCPTR (gst_proxy_pad_do_query));
  gst_pad_set_internal_link_function (pad,
      GST_DEBUG_FUNCPTR (gst_proxy_pad_do_internal_link));

  gst_pad_set_getcaps_function (pad,
      GST_DEBUG_FUNCPTR (gst_proxy_pad_do_getcaps));
  gst_pad_set_acceptcaps_function (pad,
      GST_DEBUG_FUNCPTR (gst_proxy_pad_do_acceptcaps));
  gst_pad_set_fixatecaps_function (pad,
      GST_DEBUG_FUNCPTR (gst_proxy_pad_do_fixatecaps));
  gst_pad_set_setcaps_function (pad,
      GST_DEBUG_FUNCPTR (gst_proxy_pad_do_setcaps));
}

#ifndef GST_DISABLE_LOADSAVE
/**
 * gst_proxy_pad_save_thyself:
 * @pad: a ghost #GstPad to save.
 * @parent: the parent #xmlNodePtr to save the description in.
 *
 * Saves the ghost pad into an xml representation.
 *
 * Returns: the #xmlNodePtr representation of the pad.
 */
static xmlNodePtr
gst_proxy_pad_save_thyself (GstObject * object, xmlNodePtr parent)
{
  xmlNodePtr self;

  g_return_val_if_fail (GST_IS_PROXY_PAD (object), NULL);

  self = xmlNewChild (parent, NULL, (xmlChar *) "ghostpad", NULL);
  xmlNewChild (self, NULL, (xmlChar *) "name",
      (xmlChar *) GST_OBJECT_NAME (object));
  xmlNewChild (self, NULL, (xmlChar *) "parent",
      (xmlChar *) GST_OBJECT_NAME (GST_OBJECT_PARENT (object)));

  /* FIXME FIXME FIXME! */

  return self;
}
#endif /* GST_DISABLE_LOADSAVE */


/***********************************************************************
 * Ghost pads, implemented as a pair of proxy pads (sort of)
 */


struct _GstGhostPad
{
  GstProxyPad pad;

  /* with PROXY_LOCK */
  gulong notify_id;

  /*< private > */
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstGhostPadClass
{
  GstProxyPadClass parent_class;

  /*< private > */
  gpointer _gst_reserved[GST_PADDING];
};


G_DEFINE_TYPE (GstGhostPad, gst_ghost_pad, GST_TYPE_PROXY_PAD);

static void gst_ghost_pad_dispose (GObject * object);

/* Work around g_logv's use of G_GNUC_PRINTF because gcc chokes on %P, which we
 * use for GST_PTR_FORMAT. */
static void
gst_critical (const gchar * format, ...)
{
  va_list args;

  va_start (args, format);
  g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL, format, args);
  va_end (args);
}

/*
 * The parent_set and parent_unset are used to make sure that:
 * _ the internal and target are only linked when the GhostPad has a parent,
 * _ the internal and target are unlinked as soon as the GhostPad is removed
 *    from it's parent.
 */
static void
gst_ghost_pad_parent_set (GstObject * object, GstObject * parent)
{
  GstPad *gpad;
  GstPad *target;
  GstPad *internal;
  GstPadLinkReturn ret;

  gpad = GST_PAD (object);

  /* internal is never NULL */
  internal = gst_proxy_pad_get_internal (gpad);
  target = gst_proxy_pad_get_target (gpad);

  if (target) {
    if (GST_PAD_IS_SRC (internal))
      ret = gst_pad_link (internal, target);
    else
      ret = gst_pad_link (target, internal);
    gst_object_unref (target);

    if (ret != GST_PAD_LINK_OK)
      goto link_failed;
  }
  gst_object_unref (internal);

  if (GST_OBJECT_CLASS (gst_ghost_pad_parent_class)->parent_set)
    GST_OBJECT_CLASS (gst_ghost_pad_parent_class)->parent_set (object, parent);

  return;

  /* ERRORS */
link_failed:
  {
    /* a warning is all we can do */
    gst_object_unref (internal);
    g_warning ("could not link internal ghostpad");
    return;
  }
}

static void
gst_ghost_pad_parent_unset (GstObject * object, GstObject * parent)
{
  GstPad *gpad;
  GstPad *target;
  GstPad *internal;

  gpad = GST_PAD (object);
  internal = gst_proxy_pad_get_internal (gpad);
  target = gst_proxy_pad_get_target (gpad);

  if (target) {
    if (GST_PAD_IS_SRC (internal))
      gst_pad_unlink (internal, target);
    else
      gst_pad_unlink (target, internal);
    gst_object_unref (target);
  }
  gst_object_unref (internal);

  if (GST_OBJECT_CLASS (gst_ghost_pad_parent_class)->parent_unset)
    GST_OBJECT_CLASS (gst_ghost_pad_parent_class)->parent_unset (object,
        parent);
}


static void
gst_ghost_pad_class_init (GstGhostPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstObjectClass *gstobject_class = (GstObjectClass *) klass;

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_ghost_pad_dispose);
  gstobject_class->parent_set = GST_DEBUG_FUNCPTR (gst_ghost_pad_parent_set);
  gstobject_class->parent_unset =
      GST_DEBUG_FUNCPTR (gst_ghost_pad_parent_unset);
}

/* see gstghostpad design docs */
static gboolean
gst_ghost_pad_internal_do_activate_push (GstPad * pad, gboolean active)
{
  gboolean ret;
  GstPad *other;

  if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK)
    other = gst_proxy_pad_get_internal (pad);
  else
    other = gst_pad_get_peer (pad);

  if (G_LIKELY (other)) {
    ret = gst_pad_activate_push (other, active);
    gst_object_unref (other);
  } else
    ret = FALSE;

  return ret;
}

static gboolean
gst_ghost_pad_internal_do_activate_pull (GstPad * pad, gboolean active)
{
  gboolean ret;
  GstPad *other;

  if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK)
    other = gst_pad_get_peer (pad);
  else
    other = gst_proxy_pad_get_internal (pad);

  if (G_LIKELY (other)) {
    ret = gst_pad_activate_pull (other, active);
    gst_object_unref (other);
  } else
    ret = FALSE;

  return ret;
}

static gboolean
gst_ghost_pad_do_activate_push (GstPad * pad, gboolean active)
{
  gboolean ret;

  ret = TRUE;

  if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK) {
    GstPad *internal = gst_proxy_pad_get_internal (pad);

    if (internal) {
      ret = gst_pad_activate_push (internal, active);
      gst_object_unref (internal);
    }
  }
  return ret;
}

static gboolean
gst_ghost_pad_do_activate_pull (GstPad * pad, gboolean active)
{
  gboolean ret;
  GstPad *other;

  if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK)
    other = gst_pad_get_peer (pad);
  else
    other = gst_proxy_pad_get_internal (pad);

  if (G_LIKELY (other)) {
    ret = gst_pad_activate_pull (other, active);
    gst_object_unref (other);
  } else
    ret = FALSE;

  return ret;
}

static GstPadLinkReturn
gst_ghost_pad_do_link (GstPad * pad, GstPad * peer)
{
  GstPadLinkReturn ret;
  GstPad *internal;

  ret = GST_PAD_LINK_OK;

  internal = gst_proxy_pad_get_internal (pad);
  if (!gst_proxy_pad_set_target (internal, peer))
    goto failed;

  /* if we are a source pad, we should call the peer link function
   * if the peer has one */
  if (GST_PAD_IS_SRC (pad)) {
    if (GST_PAD_LINKFUNC (peer))
      ret = GST_PAD_LINKFUNC (peer) (peer, pad);
  }
done:
  gst_object_unref (internal);

  return ret;

  /* ERRORS */
failed:
  {
    ret = GST_PAD_LINK_REFUSED;
    goto done;
  }
}

static void
gst_ghost_pad_do_unlink (GstPad * pad)
{
  GstPad *target;
  GstPad *internal;

  target = gst_proxy_pad_get_target (pad);
  internal = gst_proxy_pad_get_internal (pad);

  GST_DEBUG_OBJECT (pad, "unlinking ghostpad");

  /* The target of the internal pad is no longer valid */
  gst_proxy_pad_set_target (internal, NULL);

  if (target) {
    if (GST_PAD_UNLINKFUNC (target))
      GST_PAD_UNLINKFUNC (target) (target);

    gst_object_unref (target);
  }

  gst_object_unref (internal);
}

static void
on_int_notify (GstPad * internal, GParamSpec * unused, GstGhostPad * pad)
{
  GstCaps *caps;

  g_object_get (internal, "caps", &caps, NULL);

  GST_OBJECT_LOCK (pad);
  gst_caps_replace (&(GST_PAD_CAPS (pad)), caps);
  GST_OBJECT_UNLOCK (pad);

  g_object_notify (G_OBJECT (pad), "caps");
  if (caps)
    gst_caps_unref (caps);
}

static void
gst_ghost_pad_init (GstGhostPad * pad)
{
  gst_pad_set_activatepull_function (GST_PAD (pad),
      GST_DEBUG_FUNCPTR (gst_ghost_pad_do_activate_pull));
  gst_pad_set_activatepush_function (GST_PAD (pad),
      GST_DEBUG_FUNCPTR (gst_ghost_pad_do_activate_push));
}

static void
gst_ghost_pad_dispose (GObject * object)
{
  GstPad *pad = GST_PAD (object);
  GstPad *internal;
  GstPad *intpeer;

  GST_PROXY_LOCK (pad);
  internal = GST_PROXY_PAD_INTERNAL (pad);

  gst_pad_set_activatepull_function (internal, NULL);
  gst_pad_set_activatepush_function (internal, NULL);

  g_signal_handler_disconnect (internal, GST_GHOST_PAD (pad)->notify_id);

  intpeer = gst_pad_get_peer (internal);
  if (intpeer) {
    if (GST_PAD_IS_SRC (internal))
      gst_pad_unlink (internal, intpeer);
    else
      gst_pad_unlink (intpeer, internal);
    gst_object_unref (intpeer);
  }

  GST_PROXY_PAD_INTERNAL (internal) = NULL;

  /* disposes of the internal pad, since the ghostpad is the only possible object
   * that has a refcount on the internal pad.
   */
  gst_object_unparent (GST_OBJECT_CAST (internal));

  GST_PROXY_UNLOCK (pad);

  G_OBJECT_CLASS (gst_ghost_pad_parent_class)->dispose (object);
}

static GstPad *
gst_ghost_pad_new_full (const gchar * name, GstPadDirection dir,
    GstPadTemplate * templ)
{
  GstPad *ret;
  GstPad *internal;

  g_return_val_if_fail (dir != GST_PAD_UNKNOWN, NULL);

  /* OBJECT CREATION */
  if (templ) {
    ret = g_object_new (GST_TYPE_GHOST_PAD, "name", name,
        "direction", dir, "template", templ, NULL);
  } else {
    ret = g_object_new (GST_TYPE_GHOST_PAD, "name", name,
        "direction", dir, NULL);
  }

  /* Set directional padfunctions for ghostpad */
  if (dir == GST_PAD_SINK) {
    gst_pad_set_bufferalloc_function (ret,
        GST_DEBUG_FUNCPTR (gst_proxy_pad_do_bufferalloc));
    gst_pad_set_chain_function (ret,
        GST_DEBUG_FUNCPTR (gst_proxy_pad_do_chain));
  } else {
    gst_pad_set_getrange_function (ret,
        GST_DEBUG_FUNCPTR (gst_proxy_pad_do_getrange));
    gst_pad_set_checkgetrange_function (ret,
        GST_DEBUG_FUNCPTR (gst_proxy_pad_do_checkgetrange));
  }

  gst_pad_set_link_function (ret, GST_DEBUG_FUNCPTR (gst_ghost_pad_do_link));
  gst_pad_set_unlink_function (ret,
      GST_DEBUG_FUNCPTR (gst_ghost_pad_do_unlink));


  /* INTERNAL PAD */
  internal =
      g_object_new (GST_TYPE_PROXY_PAD, "name", NULL,
      "direction", (dir == GST_PAD_SRC) ? GST_PAD_SINK : GST_PAD_SRC, NULL);

  /* Set directional padfunctions for internal pad */
  if (dir == GST_PAD_SRC) {
    gst_pad_set_bufferalloc_function (internal,
        GST_DEBUG_FUNCPTR (gst_proxy_pad_do_bufferalloc));
    gst_pad_set_chain_function (internal,
        GST_DEBUG_FUNCPTR (gst_proxy_pad_do_chain));
  } else {
    gst_pad_set_getrange_function (internal,
        GST_DEBUG_FUNCPTR (gst_proxy_pad_do_getrange));
    gst_pad_set_checkgetrange_function (internal,
        GST_DEBUG_FUNCPTR (gst_proxy_pad_do_checkgetrange));
  }

  GST_PROXY_LOCK (ret);

  if (!gst_object_set_parent (GST_OBJECT_CAST (internal),
          GST_OBJECT_CAST (ret)))
    goto parent_failed;

  /* The ghostpad is the parent of the internal pad and is the only object that
   * can have a refcount on the internal pad.
   * At this point, the GstGhostPad has a refcount of 1, and the internal pad has
   * a refcount of 1.
   * When the refcount of the GstGhostPad drops to 0, the ghostpad will dispose
   * it's refcount on the internal pad in the dispose method by un-parenting it.
   * This is why we don't take extra refcounts in the assignments below
   */
  GST_PROXY_PAD_INTERNAL (ret) = internal;
  GST_PROXY_PAD_INTERNAL (internal) = ret;

  /* could be more general here, iterating over all writable properties...
   * taking the short road for now tho */
  GST_GHOST_PAD_CAST (ret)->notify_id =
      g_signal_connect (internal, "notify::caps", G_CALLBACK (on_int_notify),
      ret);
  /* call function to init values */
  on_int_notify (internal, NULL, GST_GHOST_PAD_CAST (ret));

  gst_pad_set_activatepull_function (GST_PAD (internal),
      GST_DEBUG_FUNCPTR (gst_ghost_pad_internal_do_activate_pull));
  gst_pad_set_activatepush_function (GST_PAD (internal),
      GST_DEBUG_FUNCPTR (gst_ghost_pad_internal_do_activate_push));

  GST_PROXY_UNLOCK (ret);

  return ret;

  /* ERRORS */
parent_failed:
  {
    gst_critical ("Could not set internal pad %" GST_PTR_FORMAT, internal);
    GST_PROXY_UNLOCK (ret);
    gst_object_unref (ret);
    return NULL;
  }
}

/**
 * gst_ghost_pad_new_no_target:
 * @name: the name of the new pad, or NULL to assign a default name.
 * @dir: the direction of the ghostpad
 *
 * Create a new ghostpad without a target with the given direction.
 * A target can be set on the ghostpad later with the
 * gst_ghost_pad_set_target() function.
 *
 * The created ghostpad will not have a padtemplate.
 *
 * Returns: a new #GstPad, or NULL in case of an error.
 */
GstPad *
gst_ghost_pad_new_no_target (const gchar * name, GstPadDirection dir)
{
  g_return_val_if_fail (dir != GST_PAD_UNKNOWN, NULL);

  GST_LOG ("name:%s, direction:%d", name, dir);

  return gst_ghost_pad_new_full (name, dir, NULL);
}

/**
 * gst_ghost_pad_new:
 * @name: the name of the new pad, or NULL to assign a default name.
 * @target: the pad to ghost.
 *
 * Create a new ghostpad with @target as the target. The direction will be taken
 * from the target pad.
 *
 * Will ref the target.
 *
 * Returns: a new #GstPad, or NULL in case of an error.
 */
GstPad *
gst_ghost_pad_new (const gchar * name, GstPad * target)
{
  GstPad *ret;

  GST_LOG ("name:%s, target:%s:%s", name, GST_DEBUG_PAD_NAME (target));

  g_return_val_if_fail (GST_IS_PAD (target), NULL);
  g_return_val_if_fail (!gst_pad_is_linked (target), NULL);

  if ((ret = gst_ghost_pad_new_no_target (name, GST_PAD_DIRECTION (target))))
    if (!gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (ret), target))
      goto set_target_failed;

  return ret;

  /* ERRORS */
set_target_failed:
  {
    gst_object_unref (ret);
    return NULL;
  }
}

/**
 * gst_ghost_pad_new_from_template:
 * @name: the name of the new pad, or NULL to assign a default name.
 * @target: the pad to ghost.
 * @templ: the #GstPadTemplate to use on the ghostpad.
 *
 * Create a new ghostpad with @target as the target. The direction will be taken
 * from the target pad. The template used on the ghostpad will be @template.
 *
 * Will ref the target.
 *
 * Returns: a new #GstPad, or NULL in case of an error.
 *
 * Since: 0.10.10
 */

GstPad *
gst_ghost_pad_new_from_template (const gchar * name, GstPad * target,
    GstPadTemplate * templ)
{
  GstPad *ret;

  g_return_val_if_fail (GST_IS_PAD (target), NULL);
  g_return_val_if_fail (!gst_pad_is_linked (target), NULL);
  g_return_val_if_fail (templ != NULL, NULL);
  g_return_val_if_fail (templ->direction == GST_PAD_DIRECTION (target), NULL);

  if ((ret = gst_ghost_pad_new_full (name, GST_PAD_DIRECTION (target), templ)))
    if (!gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (ret), target))
      goto set_target_failed;

  return ret;

  /* ERRORS */
set_target_failed:
  {
    gst_object_unref (ret);
    return NULL;
  }
}

/**
 * gst_ghost_pad_new_no_target_from_template:
 * @name: the name of the new pad, or NULL to assign a default name.
 * @templ: the #GstPadTemplate to create the ghostpad from.
 *
 * Create a new ghostpad based on @templ, without setting a target. The
 * direction will be taken from the @templ.
 *
 * Returns: a new #GstPad, or NULL in case of an error.
 *
 * Since: 0.10.10
 */
GstPad *
gst_ghost_pad_new_no_target_from_template (const gchar * name,
    GstPadTemplate * templ)
{
  GstPad *ret;

  g_return_val_if_fail (templ != NULL, NULL);

  ret =
      gst_ghost_pad_new_full (name, GST_PAD_TEMPLATE_DIRECTION (templ), templ);

  return ret;
}

/**
 * gst_ghost_pad_get_target:
 * @gpad: the #GstGhostpad
 *
 * Get the target pad of #gpad. Unref target pad after usage.
 *
 * Returns: the target #GstPad, can be NULL if the ghostpad
 * has no target set. Unref target pad after usage.
 */
GstPad *
gst_ghost_pad_get_target (GstGhostPad * gpad)
{
  g_return_val_if_fail (GST_IS_GHOST_PAD (gpad), NULL);

  return gst_proxy_pad_get_target (GST_PAD_CAST (gpad));
}

/**
 * gst_ghost_pad_set_target:
 * @gpad: the #GstGhostpad
 * @newtarget: the new pad target
 *
 * Set the new target of the ghostpad @gpad. Any existing target
 * is unlinked and links to the new target are established.
 *
 * Returns: TRUE if the new target could be set, FALSE otherwise.
 */
gboolean
gst_ghost_pad_set_target (GstGhostPad * gpad, GstPad * newtarget)
{
  GstPad *internal;
  GstPad *oldtarget;
  GstObject *parent;
  gboolean result;
  GstPadLinkReturn lret;

  g_return_val_if_fail (GST_IS_GHOST_PAD (gpad), FALSE);

  GST_PROXY_LOCK (gpad);
  internal = GST_PROXY_PAD_INTERNAL (GST_PAD_CAST (gpad));

  GST_DEBUG_OBJECT (gpad, "set target %s:%s", GST_DEBUG_PAD_NAME (newtarget));

  /* clear old target */
  if ((oldtarget = GST_PROXY_PAD_TARGET (gpad))) {
    /* if we have an internal pad, unlink */
    if (internal) {
      if (GST_PAD_IS_SRC (internal))
        gst_pad_unlink (internal, oldtarget);
      else
        gst_pad_unlink (oldtarget, internal);
    }
  }

  result = gst_proxy_pad_set_target_unlocked (GST_PAD_CAST (gpad), newtarget);

  if (result && newtarget) {
    /* and link to internal pad if we are not unparent-ed */
    if ((parent = gst_object_get_parent (GST_OBJECT (gpad)))) {
      if (GST_PAD_IS_SRC (internal))
        lret = gst_pad_link (internal, newtarget);
      else
        lret = gst_pad_link (newtarget, internal);

      /* FIXME, do something with lret, possibly checking the return value and
       * undoing the set_target operation if it failed */

      gst_object_unref (parent);
    } else {
      /* we need to connect the internal pad once we have a parent */
      GST_DEBUG_OBJECT (gpad,
          "GhostPad doesn't have a parent, will connect internal pad later");
    }
  }
  GST_PROXY_UNLOCK (gpad);

  return result;
}
